#include "aux_led.h"

#include <stddef.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>

#include "variables.h"

// --- PWM ---
#define AUX_LED_PWM_TICK_HZ 1000000u // 1 MHz timer tick
#define AUX_LED_PWM_PERIOD  1000u    // 1 kHz PWM, duty 0..1000

static bool pwm_ready = false;
static bool event_on = false;
static bool force_on = false;
static bool status_steady_on = false; // Last rendered status was AUX_LED_STATUS_ON: events invert

// --- Count display ---
typedef enum {
    COUNT_IDLE = 0,
    COUNT_WAIT_START, // Waiting for mode state to settle after the MOD action
    COUNT_ON,
    COUNT_OFF,
} count_phase_t;

static count_phase_t count_phase = COUNT_IDLE;
static uint8_t count_total = 0;
static uint8_t count_index = 0;
static uint8_t count_value = 0;
static uint32_t count_deadline = 0;
static bool count_request = false;

/** Perceived brightness (0..1000) to PWM duty (0..1000) with a quadratic gamma. */
static uint32_t perceived_to_duty(uint32_t level) {
    if (level > 1000u) {
        level = 1000u;
    }
    return (level * level) / 1000u;
}

static void write_duty(uint32_t duty) {
    if (!pwm_ready) {
        return;
    }
    timer_set_oc_value(TIM2, TIM_OC4, duty);
}

void aux_led_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_TIM2);
    rcc_periph_reset_pulse(RST_TIM2);

    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM2, (rcc_get_timer_clk_freq(TIM2) / AUX_LED_PWM_TICK_HZ) - 1u);
    timer_set_period(TIM2, AUX_LED_PWM_PERIOD - 1u);
    timer_enable_preload(TIM2);
    timer_set_oc_mode(TIM2, TIM_OC4, TIM_OCM_PWM1);
    timer_enable_oc_preload(TIM2, TIM_OC4);
    timer_set_oc_value(TIM2, TIM_OC4, event_on ? AUX_LED_PWM_PERIOD : 0u);
    timer_enable_oc_output(TIM2, TIM_OC4);
    timer_generate_event(TIM2, TIM_EGR_UG);
    timer_enable_counter(TIM2);

    // PA3 -> TIM2_CH4 (AF1)
    gpio_set_af(GPIOA, GPIO_AF1, GPIO3);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO3);
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3);

    pwm_ready = true;
}

void aux_led_set_event(bool on) {
    event_on = on;
    if (force_on) {
        return;
    }
    if (on) {
        // Immediate; on a steady-on status the flash is a brief off instead
        write_duty(status_steady_on ? 0u : AUX_LED_PWM_PERIOD);
    } else {
        write_duty(0u); // Status layer resumes on the next aux_led_update()
    }
}

void aux_led_force_on(bool on) {
    force_on = on;
    write_duty(on ? AUX_LED_PWM_PERIOD : 0u); // Immediate: holds during IRQ-masked flash saves
}

void aux_led_request_count(void) {
    count_request = true;
}

void aux_led_cancel_count(void) {
    count_request = false;
    count_phase = COUNT_IDLE;
}

/** Same encoding as the status LED: 10 = one long; otherwise tens long + units short. */
static bool count_pulse_is_long(uint8_t total, uint8_t index) {
    uint8_t tens = (uint8_t)(total / 10u);
    return index < tens;
}

static uint8_t count_pulse_total(uint8_t value) {
    return (uint8_t)((value / 10u) + (value % 10u));
}

/**
 * Count layer: repeats the enumerated value as N flashes, then AUX_LED_COUNT_CYCLE_PAUSE_MS dark, forever while
 * the mode reports AUX_LED_STATUS_COUNT. A MOD action restarts the cycle after AUX_LED_COUNT_START_DELAY_MS.
 * Returns perceived brightness, or -1 if the count layer is inactive.
 */
static int32_t count_render(uint32_t now, const aux_led_status_t *status) {
    if (status->kind != AUX_LED_STATUS_COUNT || status->count == 0u) {
        count_request = false;
        count_phase = COUNT_IDLE;
        return -1;
    }
    if (count_request || count_phase == COUNT_IDLE) {
        count_request = false;
        count_phase = COUNT_WAIT_START;
        count_deadline = now + AUX_LED_COUNT_START_DELAY_MS;
    }

    switch (count_phase) {
    case COUNT_WAIT_START:
        if ((int32_t)(now - count_deadline) >= 0) {
            count_value = status->count; // Latch so one cycle is always consistent
            count_total = count_pulse_total(count_value);
            count_index = 0;
            count_phase = COUNT_ON;
            count_deadline = now + (count_pulse_is_long(count_value, 0) ? AUX_LED_COUNT_LONG_ON_MS
                                                                        : AUX_LED_COUNT_SHORT_ON_MS);
            return 1000;
        }
        return 0;
    case COUNT_ON:
        if ((int32_t)(now - count_deadline) >= 0) {
            bool was_long = count_pulse_is_long(count_value, count_index);
            count_index++;
            if (count_index >= count_total) {
                count_phase = COUNT_WAIT_START; // Pause between cycles
                count_deadline = now + AUX_LED_COUNT_CYCLE_PAUSE_MS;
                return 0;
            }
            count_phase = COUNT_OFF;
            count_deadline = now + (was_long ? AUX_LED_COUNT_AFTER_LONG_OFF_MS : AUX_LED_COUNT_OFF_MS);
            return 0;
        }
        return 1000;
    case COUNT_OFF:
        if ((int32_t)(now - count_deadline) >= 0) {
            count_phase = COUNT_ON;
            count_deadline = now + (count_pulse_is_long(count_value, count_index) ? AUX_LED_COUNT_LONG_ON_MS
                                                                                  : AUX_LED_COUNT_SHORT_ON_MS);
            return 1000;
        }
        return 0;
    default:
        count_phase = COUNT_IDLE;
        return 0;
    }
}

/** Triangle 0..1..0 over AUX_LED_BREATH_PERIOD_MS; peak scales with `amount` (1..1000). */
static uint32_t breathe_level(uint32_t now, uint32_t amount) {
    uint32_t const half = AUX_LED_BREATH_PERIOD_MS / 2u;
    uint32_t phase = now % AUX_LED_BREATH_PERIOD_MS;
    uint32_t ramp = (phase < half) ? phase : (AUX_LED_BREATH_PERIOD_MS - phase);
    if (amount > 1000u) {
        amount = 1000u;
    }
    uint32_t peak = AUX_LED_BREATH_MIN_PEAK + (amount * (AUX_LED_BREATH_MAX_PEAK - AUX_LED_BREATH_MIN_PEAK)) / 1000u;
    return (ramp * peak) / half;
}

static uint32_t status_level(uint32_t now, const aux_led_status_t *status) {
    switch (status->kind) {
    case AUX_LED_STATUS_ON:
        return AUX_LED_STEADY_LEVEL;
    case AUX_LED_STATUS_BREATHE:
        return breathe_level(now, status->level);
    default:
        return 0u;
    }
}

void aux_led_update(uint32_t now_ms, const aux_led_status_t *status, bool suppress_status) {
    if (suppress_status) {
        aux_led_cancel_count();
    }
    status_steady_on = !suppress_status && status != NULL && status->kind == AUX_LED_STATUS_ON;
    if (force_on) {
        write_duty(AUX_LED_PWM_PERIOD);
        return;
    }
    if (event_on) {
        write_duty(status_steady_on ? 0u : AUX_LED_PWM_PERIOD);
        return;
    }
    if (suppress_status || status == NULL) {
        write_duty(0u);
        return;
    }

    int32_t const count_level = count_render(now_ms, status);
    if (count_level >= 0) {
        write_duty(perceived_to_duty((uint32_t)count_level));
        return;
    }
    write_duty(perceived_to_duty(status_level(now_ms, status)));
}
