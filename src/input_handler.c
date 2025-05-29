#include "input_handler.h"
#include "drivers/tap.h"
#include "drivers/io.h"
#include "drivers/ext_clock.h"
#include "main_constants.h"
#include "status_led.h" // For status_led_set_override
#include "util/delay.h" // For millis
#include "modes/modes.h"  // For operational_mode_t

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/cm3/nvic.h>
#include <stdlib.h> 
#include <limits.h> 

#define GATE_SWAP_DEBOUNCE_MS 10 
#define MODE_SWITCH_PA1_DEBOUNCE_MS 50 
#define OP_MODE_TIMEOUT_SAVE_MS 5000
#define OP_MODE_CONFIRM_TIMEOUT_MS 10000 // 10 seconds to confirm MOD clicks with TAP

// --- Forward declarations --- 
static void input_pins_init(void);
static void handle_taps_for_tempo(void);
static void handle_button_calc_mode_swap(void);
static void handle_op_mode_sm(uint32_t now, bool tap_pressed_now, bool mod_pressed_now_raw);
static void reset_op_mode_sm_vars(void); 
static void reset_calc_swap_sm_vars(void); 

// --- Module Static Variables ---
static input_tempo_change_callback_t tempo_change_cb = NULL;
static input_op_mode_change_callback_t op_mode_change_cb = NULL;
static input_calc_mode_change_callback_t calc_mode_change_cb = NULL;
static input_save_request_callback_t save_request_cb = NULL; 
static input_aux_led_blink_request_callback_t aux_led_blink_request_cb = NULL; 

// Tap Tempo
static uint32_t tap_intervals[NUM_INTERVALS_FOR_AVG] = {0};
static uint8_t tap_interval_index = 0;
static uint32_t last_reported_tap_tempo_interval = 0;

// External Clock State
static bool external_clock_active = false;
static uint32_t last_valid_external_clock_interval = 0;

// Op Mode change State Machine
typedef enum {
    INPUT_SM_IDLE,
    INPUT_SM_TAP_HELD_QUALIFYING,
    INPUT_SM_TAP_QUALIFIED_WAITING_RELEASE, 
    INPUT_SM_AWAITING_MOD_PRESS_OR_TIMEOUT, 
    INPUT_SM_AWAITING_CONFIRM_TAP 
} input_op_mode_sm_state_t;

static input_op_mode_sm_state_t current_op_mode_sm_state = INPUT_SM_IDLE;
static uint32_t tap_press_start_time = 0; 
static uint8_t op_mode_clicks_count = 0;
static bool just_exited_op_mode_sm = false; 

static uint32_t tap_release_time_for_timeout_logic = 0; 
static bool mod_pressed_during_tap_hold_phase = false;   
static operational_mode_t op_mode_snapshot_for_timeout; 
static uint32_t mode_confirm_state_enter_time = 0; 

static operational_mode_t last_known_main_op_mode = MODE_DEFAULT;

// PA1 (MOD button) Debounce for Op Mode Switching
static uint32_t pa1_mod_change_last_event_time = 0;
static bool pa1_mod_change_last_debounced_state = false; 
static bool pa1_mod_change_current_raw_state = false;
static bool pa1_mod_change_last_raw_state = false;    

// Calc Mode Swap Polling State (PA1 Button)
typedef enum {
    CALC_SWAP_SM_IDLE,
    CALC_SWAP_SM_MODE_PRESSED
} calc_swap_sm_state_t;
static calc_swap_sm_state_t current_calc_swap_sm_state = CALC_SWAP_SM_IDLE;
static uint32_t calc_swap_mode_press_start_time = 0;
static uint32_t last_calc_swap_trigger_time = 0;

// Gate Swap State (PB4)
static volatile bool ext_gate_swap_requested = false;
static volatile uint32_t last_gate_swap_isr_time = 0;

// --- Helper Functions ---

static void reset_tap_calculation_vars(void) {
    tap_interval_index = 0;
    for (int i = 0; i < NUM_INTERVALS_FOR_AVG; i++) tap_intervals[i] = 0;
}

static void reset_op_mode_sm_vars(void) {
    current_op_mode_sm_state = INPUT_SM_IDLE;
    tap_press_start_time = 0;
    op_mode_clicks_count = 0;
    status_led_set_override(false, false); 
    tap_release_time_for_timeout_logic = 0;
    mod_pressed_during_tap_hold_phase = false;
    mode_confirm_state_enter_time = 0;
    
    pa1_mod_change_last_event_time = 0;
    pa1_mod_change_last_debounced_state = false;
    pa1_mod_change_current_raw_state = false;
    pa1_mod_change_last_raw_state = false;

    if (exti_get_flag_status(EXTI0)) { exti_reset_request(EXTI0); }
    
    just_exited_op_mode_sm = true;
}

static void reset_calc_swap_sm_vars(void) {
    current_calc_swap_sm_state = CALC_SWAP_SM_IDLE;
    calc_swap_mode_press_start_time = 0;
}

static void handle_taps_for_tempo(void) {
    if (tap_detected()) {
        uint32_t interval = tap_get_interval();
        uint32_t now_for_tap_event = millis(); 
        if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL) {
            tap_intervals[tap_interval_index++] = interval;
            if (tap_interval_index >= NUM_INTERVALS_FOR_AVG) {
                uint64_t interval_sum = 0;
                uint32_t min_val = UINT32_MAX, max_val = 0;
                for (int i = 0; i < NUM_INTERVALS_FOR_AVG; i++) {
                    interval_sum += tap_intervals[i];
                    if (tap_intervals[i] < min_val) min_val = tap_intervals[i];
                    if (tap_intervals[i] > max_val) max_val = tap_intervals[i];
                }
                if ((max_val - min_val) <= MAX_INTERVAL_DIFFERENCE) {
                    uint32_t avg_interval = (uint32_t)(interval_sum / NUM_INTERVALS_FOR_AVG);
                    if (avg_interval < MIN_INTERVAL) avg_interval = MIN_INTERVAL;
                    if (avg_interval > MAX_INTERVAL) avg_interval = MAX_INTERVAL;
                    if (tempo_change_cb && avg_interval != last_reported_tap_tempo_interval && avg_interval > 0) {
                        tempo_change_cb(avg_interval, false, now_for_tap_event); 
                        last_reported_tap_tempo_interval = avg_interval;
                    }
                 }
                reset_tap_calculation_vars();
            }
        } else {
            reset_tap_calculation_vars();
        }
    }
}

static void handle_button_calc_mode_swap(void) {
    uint32_t now = millis();
    bool mod_is_pressed_raw = !gpio_get(GPIOA, GPIO1); // true if MOD (PA1) is pressed

    switch (current_calc_swap_sm_state) {
        case CALC_SWAP_SM_IDLE:
            if (mod_is_pressed_raw) {
                if (current_op_mode_sm_state == INPUT_SM_IDLE) { 
                    current_calc_swap_sm_state = CALC_SWAP_SM_MODE_PRESSED;
                    calc_swap_mode_press_start_time = now;
                }
            }
            break;
        case CALC_SWAP_SM_MODE_PRESSED:
            if (!mod_is_pressed_raw) { // MOD button released
                if (now - calc_swap_mode_press_start_time <= CALC_SWAP_MAX_PRESS_DURATION_MS) {
                    if (now - last_calc_swap_trigger_time > CALC_SWAP_COOLDOWN_MS) {
                        if (calc_mode_change_cb) {
                            calc_mode_change_cb();
                        }
                        last_calc_swap_trigger_time = now;
                    }
                }
                reset_calc_swap_sm_vars(); 
            } else { // MOD button still pressed
                 if (now - calc_swap_mode_press_start_time > CALC_SWAP_MAX_PRESS_DURATION_MS) {
                     reset_calc_swap_sm_vars(); 
                 }
            }
            break;
    }
}

static void handle_op_mode_sm(uint32_t now, bool tap_pressed_now, bool mod_is_pressed_raw) {
    static bool tap_confirm_action_taken_this_press = false;

    pa1_mod_change_current_raw_state = mod_is_pressed_raw;
    if (pa1_mod_change_current_raw_state != pa1_mod_change_last_raw_state) {
        pa1_mod_change_last_event_time = now;
    }
    pa1_mod_change_last_raw_state = pa1_mod_change_current_raw_state;

    bool old_debounced_mod_state = pa1_mod_change_last_debounced_state;
    if ((now - pa1_mod_change_last_event_time) > MODE_SWITCH_PA1_DEBOUNCE_MS) {
        if (pa1_mod_change_current_raw_state != pa1_mod_change_last_debounced_state) {
            pa1_mod_change_last_debounced_state = pa1_mod_change_current_raw_state;
        }
    }
    bool mod_button_is_debounced_pressed = pa1_mod_change_last_debounced_state; 
    bool mod_button_just_pressed_debounced = (mod_button_is_debounced_pressed && !old_debounced_mod_state);
    bool mod_button_just_released_debounced = (!mod_button_is_debounced_pressed && old_debounced_mod_state);

    switch (current_op_mode_sm_state) {
        case INPUT_SM_IDLE:
            if (just_exited_op_mode_sm && !tap_pressed_now) {
                just_exited_op_mode_sm = false; 
            }
            if (!just_exited_op_mode_sm && tap_pressed_now && current_calc_swap_sm_state == CALC_SWAP_SM_IDLE) {
                current_op_mode_sm_state = INPUT_SM_TAP_HELD_QUALIFYING;
                tap_press_start_time = now;
                if (tap_detected()) { (void)tap_get_interval(); } 
                reset_tap_calculation_vars(); 
                pa1_mod_change_last_debounced_state = mod_is_pressed_raw; 
                pa1_mod_change_last_event_time = now; 
            }
            break;

        case INPUT_SM_TAP_HELD_QUALIFYING:
            if (!tap_pressed_now) { 
                reset_op_mode_sm_vars(); 
            } else if (now - tap_press_start_time >= OP_MODE_TAP_HOLD_DURATION_MS) { 
                op_mode_snapshot_for_timeout = last_known_main_op_mode; 
                mod_pressed_during_tap_hold_phase = false; 
                op_mode_clicks_count = 0; 
                status_led_set_override(true, true); 
                if(aux_led_blink_request_cb) { aux_led_blink_request_cb(); }
                current_op_mode_sm_state = INPUT_SM_TAP_QUALIFIED_WAITING_RELEASE;
            }
            break;

        case INPUT_SM_TAP_QUALIFIED_WAITING_RELEASE: 
            if (tap_pressed_now) { 
                if (mod_button_is_debounced_pressed) {
                    status_led_set_override(true, false);
                } else {
                    status_led_set_override(true, true);
                }
                if (mod_button_just_released_debounced) { 
                    op_mode_clicks_count++;
                    mod_pressed_during_tap_hold_phase = true;
                }
            } else { // TAP released
                status_led_set_override(true, true); 
                if (mod_pressed_during_tap_hold_phase) { 
                    if (op_mode_clicks_count > 0) {
                        current_op_mode_sm_state = INPUT_SM_AWAITING_CONFIRM_TAP;
                        mode_confirm_state_enter_time = now; 
                        tap_confirm_action_taken_this_press = false;
                    } else { 
                        reset_op_mode_sm_vars(); 
                    }
                } else { 
                    tap_release_time_for_timeout_logic = now; 
                    current_op_mode_sm_state = INPUT_SM_AWAITING_MOD_PRESS_OR_TIMEOUT;
                }
            }
            break;

        case INPUT_SM_AWAITING_MOD_PRESS_OR_TIMEOUT: 
            if (mod_button_just_pressed_debounced) { 
                tap_release_time_for_timeout_logic = 0; 
            }
            if (mod_button_is_debounced_pressed) { 
                status_led_set_override(true, false);
            } else {
                status_led_set_override(true, true);
            }
            if (mod_button_just_released_debounced) { 
                op_mode_clicks_count = 1;
                current_op_mode_sm_state = INPUT_SM_AWAITING_CONFIRM_TAP;
                mode_confirm_state_enter_time = now; 
                tap_confirm_action_taken_this_press = false; 
                return; 
            }
            if (tap_release_time_for_timeout_logic != 0 && (now - tap_release_time_for_timeout_logic >= OP_MODE_TIMEOUT_SAVE_MS)) {
                if (aux_led_blink_request_cb) { aux_led_blink_request_cb(); } 
                if (save_request_cb) { save_request_cb(); }
                reset_op_mode_sm_vars(); 
            } 
            break;

        case INPUT_SM_AWAITING_CONFIRM_TAP: 
            if (mod_button_is_debounced_pressed) {
                status_led_set_override(true, false);
            } else {
                status_led_set_override(true, true);
            }
            if (mod_button_just_released_debounced) { 
                op_mode_clicks_count++;
                mode_confirm_state_enter_time = now; 
            }

            if (tap_pressed_now) { 
                 if (!tap_confirm_action_taken_this_press) {
                    if (op_mode_clicks_count > 0) { 
                        if (aux_led_blink_request_cb) { aux_led_blink_request_cb(); }
                        if (op_mode_change_cb) { op_mode_change_cb(op_mode_clicks_count); }
                    }
                    reset_op_mode_sm_vars(); 
                    tap_confirm_action_taken_this_press = true; 
                 }
            } else {
                tap_confirm_action_taken_this_press = false; 
            }
            
            if (now - mode_confirm_state_enter_time >= OP_MODE_CONFIRM_TIMEOUT_MS) {
                reset_op_mode_sm_vars(); 
            }
            break;

        default:
            reset_op_mode_sm_vars();
            break;
    }
}

static void input_pins_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA); 
    rcc_periph_clock_enable(RCC_GPIOB); 
    rcc_periph_clock_enable(RCC_SYSCFG); 

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO1); // PA1 (MOD)
    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO4); // PB4 (CV Gate Swap)

    tap_init(); 
    ext_clock_init(); 

    exti_select_source(EXTI4, GPIOB); 
    exti_set_trigger(EXTI4, EXTI_TRIGGER_RISING); 
    exti_enable_request(EXTI4); 
    nvic_enable_irq(NVIC_EXTI4_IRQ);
}

void input_handler_init(
    input_tempo_change_callback_t tempo_cb_param,
    input_op_mode_change_callback_t op_mode_cb_param,
    input_calc_mode_change_callback_t calc_mode_cb_param,
    input_save_request_callback_t save_req_cb_param,
    input_aux_led_blink_request_callback_t aux_blink_cb_param 
) {
    tempo_change_cb = tempo_cb_param;
    op_mode_change_cb = op_mode_cb_param;
    calc_mode_change_cb = calc_mode_cb_param; 
    save_request_cb = save_req_cb_param; 
    aux_led_blink_request_cb = aux_blink_cb_param; 

    input_pins_init(); 
    last_calc_swap_trigger_time = 0;
    last_reported_tap_tempo_interval = 0;
    ext_gate_swap_requested = false;
    external_clock_active = false;
    last_valid_external_clock_interval = 0;
    last_known_main_op_mode = MODE_DEFAULT; 
    
    reset_calc_swap_sm_vars();
    reset_op_mode_sm_vars(); 
    just_exited_op_mode_sm = false; 
}

void input_handler_update_main_op_mode(operational_mode_t mode) {
    last_known_main_op_mode = mode;
}

void input_handler_update(void) {
    uint32_t now = millis();
    bool tap_pressed_now = jack_get_digital_input(JACK_IN_TAP);
    bool mod_is_pressed_raw = !gpio_get(GPIOA, GPIO1); 

    handle_op_mode_sm(now, tap_pressed_now, mod_is_pressed_raw);

    if (current_op_mode_sm_state != INPUT_SM_IDLE) {
        if (tap_detected()) {
            (void)tap_get_interval(); 
        }
        if (current_calc_swap_sm_state != CALC_SWAP_SM_IDLE) {
             reset_calc_swap_sm_vars();
        }
        return; 
    }
    
    if (ext_clock_has_new_validated_interval()) {
        uint32_t validated_interval = ext_clock_get_validated_interval();
        uint32_t event_time = ext_clock_get_last_validated_event_time();

        if (!external_clock_active || validated_interval != last_valid_external_clock_interval) {
            if (tempo_change_cb && validated_interval > 0) {
                tempo_change_cb(validated_interval, true, event_time);
            }
        }
        external_clock_active = true;
        last_valid_external_clock_interval = validated_interval;
        reset_tap_calculation_vars(); 

        if (current_calc_swap_sm_state != CALC_SWAP_SM_IDLE) {
            reset_calc_swap_sm_vars();
        }
    } else if (ext_clock_has_timed_out(now)) {
        if (external_clock_active) { 
            external_clock_active = false; 
            if (tempo_change_cb) {
                uint32_t new_internal_tempo_to_set = DEFAULT_TEMPO_INTERVAL;

                if (last_valid_external_clock_interval > 0 &&
                    last_valid_external_clock_interval >= MIN_INTERVAL &&
                    last_valid_external_clock_interval <= MAX_INTERVAL) {
                    new_internal_tempo_to_set = last_valid_external_clock_interval;
                } else if (last_reported_tap_tempo_interval > 0 &&
                           last_reported_tap_tempo_interval >= MIN_INTERVAL &&
                           last_reported_tap_tempo_interval <= MAX_INTERVAL) {
                    new_internal_tempo_to_set = last_reported_tap_tempo_interval;
                }

                last_reported_tap_tempo_interval = new_internal_tempo_to_set;
                
                tempo_change_cb(new_internal_tempo_to_set, false, now); 
            }
            last_valid_external_clock_interval = 0; 
        }
    }

    if (external_clock_active) {
        return; 
    }

    handle_button_calc_mode_swap();
    handle_taps_for_tempo();

    if (ext_gate_swap_requested) {
        // CV Gate Swap (PB4) triggers calc_mode_change if conditions met
        // No longer requires PA1 (MOD button) to be pressed simultaneously
        if (now - last_calc_swap_trigger_time > CALC_SWAP_COOLDOWN_MS) {
            if (calc_mode_change_cb) {
                calc_mode_change_cb();
            }
            last_calc_swap_trigger_time = now; // Update cooldown timer for CV swap as well
        }
        ext_gate_swap_requested = false; 
    }
}

void exti4_isr(void) {
    if (exti_get_flag_status(EXTI4)) {
        uint32_t now = millis();
        if (now - last_gate_swap_isr_time >= GATE_SWAP_DEBOUNCE_MS) {
             if (gpio_get(GPIOB, GPIO4)) { // Check if PB4 (CV gate) is high
                 // Request CV swap if OpMode SM is IDLE and external clock is NOT active
                 // PA1 (MOD button) status is NOT checked here.
                 if (current_op_mode_sm_state == INPUT_SM_IDLE && !external_clock_active) {
                    ext_gate_swap_requested = true;
                 }
                 last_gate_swap_isr_time = now; 
             }
        }
        exti_reset_request(EXTI4); 
    }
}
