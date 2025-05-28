#include "input_handler.h"
#include "drivers/tap.h"       // For tap_detected(), tap_get_interval(), tap_init()
#include "drivers/io.h"        // For jack_get_digital_input(), set_output()
#include "drivers/ext_clock.h" // For external clock functionality
#include "main_constants.h"  // For timing constants, etc.
#include "status_led.h"    // For status_led_set_override()
#include "util/delay.h"      // For millis()

#include <libopencm3/stm32/rcc.h>    // Needed for RCC enables
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h> // For EXTI lines
#include <libopencm3/stm32/syscfg.h> // Needed for SYSCFG clock and EXTI config on F4
#include <libopencm3/cm3/nvic.h>    // For NVIC
#include <stdlib.h> // For abs()
#include <limits.h> // For UINT32_MAX

#define GATE_SWAP_DEBOUNCE_MS 10 // Debounce time for PB4 gate input (ms)
#define MODE_SWITCH_PA1_DEBOUNCE_MS 50 // Debounce time for PA1 during mode switching

// --- Module Static Variables ---

static input_tempo_change_callback_t tempo_change_cb = NULL;
static input_op_mode_change_callback_t op_mode_change_cb = NULL;
static input_calc_mode_change_callback_t calc_mode_change_cb = NULL;

// Tap Tempo
static uint32_t tap_intervals[NUM_INTERVALS_FOR_AVG] = {0};
static uint8_t tap_interval_index = 0;
static uint32_t last_reported_tap_tempo_interval = 0; // Keep track of last *tap* tempo sent

// External Clock State (managed by input_handler)
static bool external_clock_active = false; // Tracks if ext clock is currently driving the tempo
static uint32_t last_valid_external_clock_interval = 0; // Stores the last successfully validated external clock interval

// Op Mode change State Machine
typedef enum {
    INPUT_STATE_IDLE,
    INPUT_STATE_TAP_PRESSED_CHECK_DURATION,
    INPUT_STATE_MODE_CHANGE_ACTIVE
} input_state_t;

static input_state_t current_input_state = INPUT_STATE_IDLE;
static uint32_t tap_press_start_time = 0;
static uint8_t op_mode_clicks_count = 0;
static bool just_exited_mode_change = false; // Flag to prevent immediate re-entry

// PA1 Debounce for Mode Switching
static uint32_t pa1_mode_change_last_event_time = 0;
static bool pa1_mode_change_last_debounced_state = false; // true = pressed, false = released
static bool pa1_mode_change_current_raw_state = false; // true = pressed, false = released
static bool pa1_mode_change_last_raw_state = false;    // true = pressed, false = released


// Calc Mode Swap Polling State (PA1 Button)
typedef enum {
    CALC_SWAP_STATE_IDLE,
    CALC_SWAP_STATE_MODE_PRESSED
} calc_swap_state_t;

static calc_swap_state_t current_calc_swap_state = CALC_SWAP_STATE_IDLE;
static uint32_t calc_swap_mode_press_start_time = 0;
static uint32_t last_calc_swap_trigger_time = 0;

// Gate Swap State (PB4)
static volatile bool ext_gate_swap_requested = false; 
static volatile uint32_t last_gate_swap_isr_time = 0; // For debounce

// --- Helper Functions ---

static void reset_tap_calculation_vars(void) {
    tap_interval_index = 0;
    for (int i = 0; i < NUM_INTERVALS_FOR_AVG; i++) {
        tap_intervals[i] = 0;
    }
}

static void reset_to_idle_state(void) {
    current_input_state = INPUT_STATE_IDLE;
    tap_press_start_time = 0;
    op_mode_clicks_count = 0;
    status_led_set_override(false, false); // Turn off LED override
    reset_tap_calculation_vars();
    ext_gate_swap_requested = false;

    // Reset PA1 debounce vars for mode switching
    pa1_mode_change_last_event_time = 0;
    pa1_mode_change_last_debounced_state = false; 
    pa1_mode_change_current_raw_state = false;
    pa1_mode_change_last_raw_state = false;

    // Reset calc swap state
    current_calc_swap_state = CALC_SWAP_STATE_IDLE;
    calc_swap_mode_press_start_time = 0;
    
    // Clear potentially pending tap flags
    if (exti_get_flag_status(EXTI0)) { exti_reset_request(EXTI0); }
    if (tap_detected()) { (void)tap_get_interval(); }
    if (exti_get_flag_status(EXTI4)) { exti_reset_request(EXTI4); }
}

static void reset_calc_swap_state_local(void) {
    current_calc_swap_state = CALC_SWAP_STATE_IDLE;
    calc_swap_mode_press_start_time = 0;
}

// Handles Tap Tempo logic ONLY when external clock is timed out
static void handle_taps_for_tempo(void) {
    if (tap_detected()) {
        uint32_t interval = tap_get_interval();
        uint32_t now_for_tap_event = millis(); // Timestamp for tap event
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
                    if (tempo_change_cb && avg_interval != last_reported_tap_tempo_interval) {
                        tempo_change_cb(avg_interval, false, now_for_tap_event); // false for is_external_clock
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

// Handles Calc Mode Swap logic (PA1 short press - Button)
static void handle_button_calc_mode_swap(void) {
    uint32_t now = millis();
    bool mode_pressed_now_raw = !gpio_get(GPIOA, GPIO1); // Button PA1 pulled high, pressed = LOW

    switch (current_calc_swap_state) {
        case CALC_SWAP_STATE_IDLE:
            if (mode_pressed_now_raw) {
                current_calc_swap_state = CALC_SWAP_STATE_MODE_PRESSED;
                calc_swap_mode_press_start_time = now;
            }
            break;
        case CALC_SWAP_STATE_MODE_PRESSED:
            if (!mode_pressed_now_raw) { // Button released
                if (now - calc_swap_mode_press_start_time <= CALC_SWAP_MAX_PRESS_DURATION_MS) {
                    if (now - last_calc_swap_trigger_time > CALC_SWAP_COOLDOWN_MS) {
                        if (calc_mode_change_cb) {
                            calc_mode_change_cb();
                        }
                        last_calc_swap_trigger_time = now;
                    }
                }
                reset_calc_swap_state_local();
            } else { // Button still pressed
                 if (now - calc_swap_mode_press_start_time > CALC_SWAP_MAX_PRESS_DURATION_MS) {
                     reset_calc_swap_state_local();
                 }
            }
            break;
    }
}


// Handles Op Mode switching logic (PA0 long press + PA1 clicks)
static void handle_op_mode_switching(uint32_t now, bool tap_pressed_now, bool mode_pressed_now_raw_unused) {
     static bool tap_button_currently_pressed_for_confirm = false; 

     switch (current_input_state) {
        case INPUT_STATE_IDLE:
            {
                bool skip_tap_check_this_cycle = just_exited_mode_change;
                if (just_exited_mode_change) {
                    just_exited_mode_change = false; 
                }
                if (!skip_tap_check_this_cycle && tap_pressed_now && current_calc_swap_state == CALC_SWAP_STATE_IDLE) {
                    current_input_state = INPUT_STATE_TAP_PRESSED_CHECK_DURATION;
                    tap_press_start_time = now;
                    if (tap_detected()) { (void)tap_get_interval(); }
                    reset_tap_calculation_vars();
                }
            }
            break;

        case INPUT_STATE_TAP_PRESSED_CHECK_DURATION:
            if (!tap_pressed_now) { 
                current_input_state = INPUT_STATE_IDLE;
                tap_press_start_time = 0;
            } else if (now - tap_press_start_time >= OP_MODE_TAP_HOLD_DURATION_MS) { 
                current_input_state = INPUT_STATE_MODE_CHANGE_ACTIVE;
                op_mode_clicks_count = 0;
                reset_tap_calculation_vars();
                if (tap_detected()) { (void)tap_get_interval(); } 
                
                // Initialize PA1 debounce vars for mode switching
                pa1_mode_change_current_raw_state = !gpio_get(GPIOA, GPIO1); // PA1 active LOW
                pa1_mode_change_last_raw_state = pa1_mode_change_current_raw_state;
                pa1_mode_change_last_debounced_state = pa1_mode_change_current_raw_state; // Initial debounced state is current raw state
                pa1_mode_change_last_event_time = now;

                // Set initial LED state based on PA1's current (debounced) state
                if (pa1_mode_change_last_debounced_state) { // If PA1 is currently pressed
                    status_led_set_override(true, false); // LED OFF
                } else { // If PA1 is currently released
                    status_led_set_override(true, true);  // LED ON
                }
                
                tap_button_currently_pressed_for_confirm = tap_pressed_now; 
            }
            break;

        case INPUT_STATE_MODE_CHANGE_ACTIVE:
            if (tap_detected()) { (void)tap_get_interval(); }
            if (exti_get_flag_status(EXTI0)) { exti_reset_request(EXTI0); }

            // Debounce PA1 (Mode button)
            // LED Behavior in this state (PA15, controlled by status_led_set_override):
            // - Upon entering INPUT_STATE_MODE_CHANGE_ACTIVE: Initial LED state set based on PA1 (see above).
            // - When PA1 (Mode button) is pressed (debounced edge): LED is turned OFF.
            // - When PA1 (Mode button) is released (debounced edge): LED is turned ON, click counted.
            pa1_mode_change_current_raw_state = !gpio_get(GPIOA, GPIO1); // PA1 active LOW

            if (pa1_mode_change_current_raw_state != pa1_mode_change_last_raw_state) {
                pa1_mode_change_last_event_time = now; // Reset debounce timer on change
            }
            pa1_mode_change_last_raw_state = pa1_mode_change_current_raw_state;

            if ((now - pa1_mode_change_last_event_time) > MODE_SWITCH_PA1_DEBOUNCE_MS) {
                if (pa1_mode_change_current_raw_state != pa1_mode_change_last_debounced_state) {
                    pa1_mode_change_last_debounced_state = pa1_mode_change_current_raw_state;
                    if (pa1_mode_change_last_debounced_state == false) { // PA1 newly RELEASED (debounced)
                        op_mode_clicks_count++;
                        status_led_set_override(true, true); // LED ON
                    } else { // PA1 newly PRESSED (debounced)
                        status_led_set_override(true, false); // LED OFF
                    }
                }
            }

            // Handle PA0 (Tap button) press for confirmation/exit
            if (tap_pressed_now && !tap_button_currently_pressed_for_confirm) { // PA0 newly PRESSED
                 if (op_mode_change_cb && op_mode_clicks_count > 0) {
                     op_mode_change_cb(op_mode_clicks_count);
                 }
                 just_exited_mode_change = true; 
                 reset_to_idle_state(); 
            }
            tap_button_currently_pressed_for_confirm = tap_pressed_now; 
            break;

        default:
            reset_to_idle_state();
            break;
    }
}


static void input_pins_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA); 
    rcc_periph_clock_enable(RCC_GPIOB); 
    rcc_periph_clock_enable(RCC_SYSCFG); 

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO1); // PA1 (Mode/Swap)
    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO4); // PB4 (Gate Swap)

    tap_init(); // PA0 (Tap) & EXTI0
    ext_clock_init(); // PB3 (Ext Clock) & EXTI3

    exti_select_source(EXTI4, GPIOB); 
    exti_set_trigger(EXTI4, EXTI_TRIGGER_RISING); 
    exti_enable_request(EXTI4); 
    nvic_enable_irq(NVIC_EXTI4_IRQ);
}

// --- Public Function Implementations ---

void input_handler_init(
    input_tempo_change_callback_t tempo_cb,
    input_op_mode_change_callback_t op_mode_cb,
    input_calc_mode_change_callback_t calc_mode_cb
) {
    tempo_change_cb = tempo_cb;
    op_mode_change_cb = op_mode_cb;
    calc_mode_change_cb = calc_mode_cb;
    input_pins_init();
    just_exited_mode_change = false;
    last_calc_swap_trigger_time = 0;
    last_reported_tap_tempo_interval = 0;
    ext_gate_swap_requested = false;
    external_clock_active = false;
    last_valid_external_clock_interval = 0; // Initialize the new variable
    reset_to_idle_state(); 
}

void input_handler_update(void) {
    uint32_t now = millis();
    bool tap_pressed_now = jack_get_digital_input(JACK_IN_TAP);
    bool mode_pressed_now_raw = !gpio_get(GPIOA, GPIO1); // PA1 is pulled up, pressed = LOW

    // --- Handle External Clock ---
    bool ext_timed_out = ext_clock_has_timed_out(now);

    if (!ext_timed_out) {
        if (ext_clock_has_new_validated_interval()) {
            uint32_t validated_interval = ext_clock_get_validated_interval();
            uint32_t event_time = ext_clock_get_last_validated_event_time(); // Get event timestamp
            if (validated_interval > 0 && tempo_change_cb) {
                tempo_change_cb(validated_interval, true, event_time); // true for is_external_clock
                external_clock_active = true;
                last_valid_external_clock_interval = validated_interval; // Store the last valid external interval
                reset_tap_calculation_vars(); 
            }
        }
        // If external clock is active but no new interval, it means it's still ticking at the last_valid_external_clock_interval.
        // No explicit action needed here as tempo_change_cb was already called with the validated_interval.
    } else { // External clock has timed out
        if (external_clock_active) { // If it *was* active and now timed out
            external_clock_active = false;
            if (tempo_change_cb) {
                // If a valid external clock was previously active, use its last value.
                // The event_timestamp here is less critical as it's a timeout event, using 'now'.
                if (last_valid_external_clock_interval > 0) {
                    tempo_change_cb(last_valid_external_clock_interval, false, now); // false: no longer driven by ext clock
                } else {
                    // Fallback to tap tempo if no valid external interval was ever stored
                    tempo_change_cb(last_reported_tap_tempo_interval, false, now);
                }
            }
        }
        // If external clock was not active and is timed out, last_valid_external_clock_interval might still hold an old value.
        // It's important that it's only used if external_clock_active *transitions* from true to false.
        // If it simply times out without ever having been active, we rely on tap tempo.
    }

    // --- Handle Tap Tempo ---
    if (ext_timed_out && current_input_state == INPUT_STATE_IDLE) {
         handle_taps_for_tempo();
    }

    // --- Handle Button State Machines ---
    handle_op_mode_switching(now, tap_pressed_now, mode_pressed_now_raw); 

    // --- Handle Calc Mode Swap (Button PA1) ---
    if (current_input_state == INPUT_STATE_IDLE) { 
        handle_button_calc_mode_swap(); 
    }

    // --- Handle Gate Swap Request (PB4) ---
    if (current_input_state == INPUT_STATE_IDLE) { 
        if (ext_gate_swap_requested) {
            if (calc_mode_change_cb) {
                calc_mode_change_cb();
            }
            ext_gate_swap_requested = false; 
            reset_calc_swap_state_local(); 
            last_calc_swap_trigger_time = now; 
        }
    }
}


// --- Interrupt Service Routine for EXTI4 (Gate Swap PB4) ---
void exti4_isr(void) {
    if (exti_get_flag_status(EXTI4)) {
        uint32_t now = millis();
        if (now - last_gate_swap_isr_time >= GATE_SWAP_DEBOUNCE_MS) {
             if (gpio_get(GPIOB, GPIO4)) { 
                 ext_gate_swap_requested = true;
                 last_gate_swap_isr_time = now; 
             }
        }
        exti_reset_request(EXTI4); 
    }
}
