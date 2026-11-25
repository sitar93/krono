#include "drivers/ext_clock.h"
#include "main_constants.h"
#include "variables.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/syscfg.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// --- Configuration ---
#define EXT_CLOCK_PORT GPIOB
#define EXT_CLOCK_PIN GPIO3
#define EXT_CLOCK_EXTI EXTI3
#define EXT_CLOCK_NVIC_IRQ NVIC_EXTI3_IRQ

#define NUM_EXT_INTERVALS_FOR_VALIDATION 3
#define EXT_CLOCK_DEBOUNCE_MS 5

// --- Internal State ---
static volatile uint32_t last_edge_time_ms = 0;
static volatile bool has_last_edge = false;
static volatile uint32_t last_in_range_activity_ms = 0;
static volatile bool has_in_range_activity = false;
static volatile uint32_t last_debounce_time_ms = 0;
static volatile bool has_debounce_time = false;

static volatile uint32_t validated_interval_ms = 0;
static volatile bool has_validated_interval = false;
static volatile uint32_t last_validated_event_time_ms = 0;
static volatile bool g_ext_clock_validated_interval_ready = false;

static volatile bool ext_clock_forwarding_enabled = false;
static volatile bool ext_clock_out_of_range_break = false;
static volatile uint32_t last_local_f1_ms = 0;
static volatile bool has_local_f1 = false;

#define EXT_CLOCK_PULSE_QUEUE_LEN 8u
static volatile uint32_t g_ext_clock_pulse_queue[EXT_CLOCK_PULSE_QUEUE_LEN];
static volatile uint8_t g_ext_clock_pulse_queue_head = 0;
static volatile uint8_t g_ext_clock_pulse_queue_tail = 0;

static uint32_t ext_intervals[NUM_EXT_INTERVALS_FOR_VALIDATION] = {0};
static uint8_t ext_interval_index = 0;

/*
 * Precise period: average over a run of consecutive consistent edges (up to EXT_CLOCK_RUN_MAX intervals),
 * in Q16 milliseconds. The 3-interval validation is rounded to 1 ms, which makes the flywheel drift once the
 * cable is pulled; a long run resolves the period to a few hundredths of a millisecond.
 */
#define EXT_CLOCK_RUN_MIN 4u
#define EXT_CLOCK_RUN_MAX 64u
static volatile uint32_t run_start_ms = 0;
static volatile uint32_t run_count = 0; // intervals in the current run
static volatile uint32_t precise_period_q16 = 0;
static volatile bool has_precise_period = false;

static void ext_clock_queue_pulse_unlocked(uint32_t pulse_time_ms) {
    uint8_t next_head = (uint8_t)((g_ext_clock_pulse_queue_head + 1u) % EXT_CLOCK_PULSE_QUEUE_LEN);
    if (next_head == g_ext_clock_pulse_queue_tail) {
        g_ext_clock_pulse_queue_tail =
            (uint8_t)((g_ext_clock_pulse_queue_tail + 1u) % EXT_CLOCK_PULSE_QUEUE_LEN);
    }
    g_ext_clock_pulse_queue[g_ext_clock_pulse_queue_head] = pulse_time_ms;
    g_ext_clock_pulse_queue_head = next_head;
}

static void ext_clock_flush_pulse_queue_unlocked(void) {
    g_ext_clock_pulse_queue_head = 0;
    g_ext_clock_pulse_queue_tail = 0;
}

static uint32_t stability_tolerance_ms(uint32_t min_interval_ms) {
    uint32_t rel = (min_interval_ms * EXT_CLOCK_STABILITY_REL_NUM) / EXT_CLOCK_STABILITY_REL_DEN;
    if (rel < EXT_CLOCK_STABILITY_ABS_MS) {
        return EXT_CLOCK_STABILITY_ABS_MS;
    }
    return rel;
}

static void reset_ext_clock_validation_buffer(void) {
    ext_interval_index = 0;
    for (int i = 0; i < NUM_EXT_INTERVALS_FOR_VALIDATION; i++) {
        ext_intervals[i] = 0;
    }
}

static bool edge_is_local_echo_unlocked(uint32_t now) {
    if (!has_local_f1) {
        return false;
    }
    return ((uint32_t)(now - last_local_f1_ms) < EXT_CLOCK_ECHO_WINDOW_MS);
}

void exti3_isr(void) {
    if (exti_get_flag_status(EXT_CLOCK_EXTI)) {
        ext_clock_handle_irq();
        exti_reset_request(EXT_CLOCK_EXTI);
    }
}

/**
 * @brief External rising-edge handler (PB3 falling, inverted input stage). Debounce, measure intervals,
 *        validate, optional F1 queue. The EXTI edge is authoritative - the pin level is not re-checked.
 */
void ext_clock_handle_irq(void) {
    uint32_t now = millis();

    if (has_debounce_time && (now - last_debounce_time_ms) < EXT_CLOCK_DEBOUNCE_MS) {
        return;
    }
    last_debounce_time_ms = now;
    has_debounce_time = true;

    /*
     * Echo of our own F1 (1A/1B → Clock In): still measure/validate/hold lock so a real
     * external master near our tempo is not dropped. Only skip F1 forwarding to avoid
     * double-trigger while autopatching or phase-aligned.
     */
    bool is_echo = edge_is_local_echo_unlocked(now);

    /* Accepted rising edge (post-debounce). */
    if (has_last_edge) {
        uint32_t interval = now - last_edge_time_ms;

        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL) {
            last_in_range_activity_ms = now;
            has_in_range_activity = true;

            if (ext_clock_forwarding_enabled && !is_echo) {
                ext_clock_queue_pulse_unlocked(now);
            }

            // Precise period run: restart if this interval disagrees with the run average (tempo change)
            if (run_count == 0u) {
                run_start_ms = last_edge_time_ms;
                run_count = 1u;
            } else {
                uint32_t const avg = (now - run_start_ms) / (run_count + 1u);
                uint32_t const diff = (interval > avg) ? (interval - avg) : (avg - interval);
                if (diff > stability_tolerance_ms(avg)) {
                    run_start_ms = last_edge_time_ms;
                    run_count = 1u;
                } else {
                    run_count++;
                    if (run_count >= EXT_CLOCK_RUN_MAX) { // Slide: keep the newest half
                        run_start_ms += (now - run_start_ms) / 2u;
                        run_count /= 2u;
                    }
                }
            }
            if (run_count >= EXT_CLOCK_RUN_MIN) {
                precise_period_q16 = (uint32_t)((((uint64_t)(now - run_start_ms)) << 16) / run_count);
                has_precise_period = true;
            }

            ext_intervals[ext_interval_index++] = interval;
            if (ext_interval_index >= NUM_EXT_INTERVALS_FOR_VALIDATION) {
                uint64_t interval_sum = 0;
                uint32_t min_val = UINT32_MAX;
                uint32_t max_val = 0;
                for (int i = 0; i < NUM_EXT_INTERVALS_FOR_VALIDATION; i++) {
                    interval_sum += ext_intervals[i];
                    if (ext_intervals[i] < min_val) {
                        min_val = ext_intervals[i];
                    }
                    if (ext_intervals[i] > max_val) {
                        max_val = ext_intervals[i];
                    }
                }

                uint32_t tolerance = stability_tolerance_ms(min_val);
                if ((max_val - min_val) <= tolerance) {
                    uint32_t avg_interval =
                        (uint32_t)(interval_sum / NUM_EXT_INTERVALS_FOR_VALIDATION);
                    if (avg_interval < MIN_INTERVAL) {
                        avg_interval = MIN_INTERVAL;
                    }
                    if (avg_interval > MAX_INTERVAL) {
                        avg_interval = MAX_INTERVAL;
                    }

                    validated_interval_ms = avg_interval;
                    has_validated_interval = true;
                    last_validated_event_time_ms = now;
                    g_ext_clock_validated_interval_ready = true;
                }
                reset_ext_clock_validation_buffer();
            }
        } else {
            /* Out of range: break hard-follow immediately; do not refresh lock activity. */
            reset_ext_clock_validation_buffer();
            run_count = 0u; // Keep the last precise period for the flywheel
            if (ext_clock_forwarding_enabled) {
                ext_clock_out_of_range_break = true;
            }
        }
    }

    last_edge_time_ms = now;
    has_last_edge = true;
}

void ext_clock_init(void) {
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_SYSCFG);

    /* Keep floating (NONE): PCB clock-in bias is unknown; pull-down can chatter against an external pull-up.
     * False slave is prevented by validate-before-follow; echo only blocks F1 forwarding. */
    gpio_mode_setup(EXT_CLOCK_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, EXT_CLOCK_PIN);

    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    exti_select_source(EXT_CLOCK_EXTI, EXT_CLOCK_PORT);
    /* The input stage (NPN, 10k pull-up) inverts: the external rising edge is the PB3 FALLING edge.
     * Triggering on PB3 rising followed the END of each external pulse (half a period late on square clocks). */
    exti_set_trigger(EXT_CLOCK_EXTI, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXT_CLOCK_EXTI);

    last_edge_time_ms = 0;
    has_last_edge = false;
    last_in_range_activity_ms = 0;
    has_in_range_activity = false;
    last_debounce_time_ms = 0;
    has_debounce_time = false;
    validated_interval_ms = 0;
    has_validated_interval = false;
    last_validated_event_time_ms = 0;
    g_ext_clock_validated_interval_ready = false;
    ext_clock_forwarding_enabled = false;
    ext_clock_out_of_range_break = false;
    last_local_f1_ms = 0;
    has_local_f1 = false;
    run_count = 0u;
    has_precise_period = false;
    ext_clock_flush_pulse_queue_unlocked();
    reset_ext_clock_validation_buffer();
}

void ext_clock_note_local_f1_pulse(uint32_t pulse_time_ms) {
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    last_local_f1_ms = pulse_time_ms;
    has_local_f1 = true;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
}

bool ext_clock_consume_pulse(uint32_t *pulse_time_ms) {
    bool consumed = false;
    uint32_t pulse_time = 0;

    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    if (g_ext_clock_pulse_queue_tail != g_ext_clock_pulse_queue_head) {
        pulse_time = g_ext_clock_pulse_queue[g_ext_clock_pulse_queue_tail];
        g_ext_clock_pulse_queue_tail =
            (uint8_t)((g_ext_clock_pulse_queue_tail + 1u) % EXT_CLOCK_PULSE_QUEUE_LEN);
        consumed = true;
    }
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);

    if (consumed && pulse_time_ms != NULL) {
        *pulse_time_ms = pulse_time;
    }
    return consumed;
}

void ext_clock_flush_pulse_queue(void) {
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ext_clock_flush_pulse_queue_unlocked();
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
}

void ext_clock_set_pulse_forwarding(bool enable) {
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ext_clock_forwarding_enabled = enable;
    if (!enable) {
        ext_clock_flush_pulse_queue_unlocked();
        ext_clock_out_of_range_break = false;
    }
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
}

void ext_clock_arm_slave_forwarding(void) {
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ext_clock_flush_pulse_queue_unlocked();
    ext_clock_forwarding_enabled = true;
    ext_clock_out_of_range_break = false;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
}

void ext_clock_disarm_slave_forwarding(void) {
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ext_clock_forwarding_enabled = false;
    ext_clock_out_of_range_break = false;
    ext_clock_flush_pulse_queue_unlocked();
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
}

bool ext_clock_has_new_validated_interval(void) {
    bool ready;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ready = g_ext_clock_validated_interval_ready;
    g_ext_clock_validated_interval_ready = false;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return ready;
}

uint32_t ext_clock_get_validated_interval(void) {
    uint32_t interval = 0;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    if (has_validated_interval) {
        interval = validated_interval_ms;
    }
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return interval;
}

uint32_t ext_clock_get_last_validated_event_time(void) {
    uint32_t event_time = 0;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    if (has_validated_interval) {
        event_time = last_validated_event_time_ms;
    }
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return event_time;
}

bool ext_clock_get_precise_period_q16(uint32_t *period_q16) {
    bool ok;
    uint32_t p;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ok = has_precise_period;
    p = precise_period_q16;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    if (ok && period_q16 != NULL) {
        *period_q16 = p;
    }
    return ok;
}

bool ext_clock_has_lost_lock(uint32_t current_time_ms, uint32_t locked_interval_ms) {
    uint32_t last_activity;
    bool have_activity;
    bool oor_break;

    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    oor_break = ext_clock_out_of_range_break;
    if (oor_break) {
        ext_clock_out_of_range_break = false;
    }
    have_activity = has_in_range_activity;
    last_activity = last_in_range_activity_ms;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);

    if (oor_break) {
        return true;
    }

    if (!have_activity) {
        return true;
    }

    if (locked_interval_ms < MIN_INTERVAL) {
        locked_interval_ms = MIN_INTERVAL;
    }

    uint32_t hold_ms = locked_interval_ms + EXT_CLOCK_LOCK_GRACE_MS;
    return ((uint32_t)(current_time_ms - last_activity) > hold_ms);
}
