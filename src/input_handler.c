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

// Op Mode change State Machine
typedef enum {
    INPUT_STATE_IDLE,
    INPUT_STATE_TAP_PRESSED_CHECK_DURATION,
    INPUT_STATE_MODE_CHANGE_ACTIVE
} input_state_t;

static input_state_t current_input_state = INPUT_STATE_IDLE;
static uint32_t tap_press_start_time = 0;
static uint8_t op_mode_clicks_count = 0;
static bool mode_button_currently_pressed = false; // Tracks PA1 state *during* MODE_CHANGE_ACTIVE
static bool just_exited_mode_change = false; // Flag to prevent immediate re-entry

// Calc Mode Swap Polling State (PA1 Button)
typedef enum {
    CALC_SWAP_STATE_IDLE,
    CALC_SWAP_STATE_MODE_PRESSED
} calc_swap_state_t;

static calc_swap_state_t current_calc_swap_state = CALC_SWAP_STATE_IDLE;
static uint32_t calc_swap_mode_press_start_time = 0;
static uint32_t last_calc_swap_trigger_time = 0;

// Gate Swap State (PB4)
static volatile bool ext_gate_swap_requested = false; // Renamed from pa12_swap_requested
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
    mode_button_currently_pressed = false;
    current_calc_swap_state = CALC_SWAP_STATE_IDLE;
    calc_swap_mode_press_start_time = 0;
    status_led_set_override(false, false);
    reset_tap_calculation_vars();
    ext_gate_swap_requested = false; // Also reset gate swap request
    // Clear potentially pending tap flags
    if (exti_get_flag_status(EXTI0)) { exti_reset_request(EXTI0); }
    if (tap_detected()) { (void)tap_get_interval(); }
    if (exti_get_flag_status(EXTI4)) { exti_reset_request(EXTI4); } // Clear PB4/EXTI4 gate swap flag
}

static void reset_calc_swap_state_local(void) {
    current_calc_swap_state = CALC_SWAP_STATE_IDLE;
    calc_swap_mode_press_start_time = 0;
}

// Handles Tap Tempo logic ONLY when external clock is timed out
static void handle_taps_for_tempo(void) {
    if (tap_detected()) {
        uint32_t interval = tap_get_interval();
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
                        tempo_change_cb(avg_interval);
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
    bool mode_pressed_now = !gpio_get(GPIOA, GPIO1); // Button PA1 pulled high, pressed = LOW

    switch (current_calc_swap_state) {
        case CALC_SWAP_STATE_IDLE:
            if (mode_pressed_now) {
                current_calc_swap_state = CALC_SWAP_STATE_MODE_PRESSED;
                calc_swap_mode_press_start_time = now;
            }
            break;
        case CALC_SWAP_STATE_MODE_PRESSED:
            if (!mode_pressed_now) { // Button released
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
                     // Held too long, likely for Op Mode switch, cancel calc swap check
                     reset_calc_swap_state_local();
                 }
            }
            break;
    }
}


// Handles Op Mode switching logic (PA0 long press + PA1 clicks)
static void handle_op_mode_switching(uint32_t now, bool tap_pressed_now, bool mode_pressed_now) {
     static bool tap_button_currently_pressed = false; // Tracks previous PA0 state
     switch (current_input_state) {
        case INPUT_STATE_IDLE:
            {
                bool skip_tap_check_this_cycle = just_exited_mode_change;
                if (just_exited_mode_change) {
                    just_exited_mode_change = false;
                }
                // Enter check duration state if PA0 pressed and not in calc swap button state
                if (!skip_tap_check_this_cycle && tap_pressed_now && current_calc_swap_state == CALC_SWAP_STATE_IDLE) {
                    current_input_state = INPUT_STATE_TAP_PRESSED_CHECK_DURATION;
                    tap_press_start_time = now;
                    // Clear tap flags
                     if (tap_detected()) { (void)tap_get_interval(); }
                     reset_tap_calculation_vars();
                }
            }
            break;

        case INPUT_STATE_TAP_PRESSED_CHECK_DURATION:
            if (!tap_pressed_now) { // Released too early
                current_input_state = INPUT_STATE_IDLE;
                tap_press_start_time = 0;
            } else if (now - tap_press_start_time >= OP_MODE_TAP_HOLD_DURATION_MS) { // Held long enough
                current_input_state = INPUT_STATE_MODE_CHANGE_ACTIVE;
                op_mode_clicks_count = 0;
                mode_button_currently_pressed = false; // Reset PA1 tracker for this state
                status_led_set_override(true, true); // LED ON
                reset_tap_calculation_vars();
                if (tap_detected()) { (void)tap_get_interval(); } // Clear flags
                tap_button_currently_pressed = true; // Set PA0 state tracker for use in MODE_CHANGE_ACTIVE
            }
            break;

        case INPUT_STATE_MODE_CHANGE_ACTIVE:
            // Ignore tap tempo events
            if (tap_detected()) { (void)tap_get_interval(); }
            if (exti_get_flag_status(EXTI0)) { exti_reset_request(EXTI0); } // Clear HW flag just in case

            // Handle PA1 (Mode button) clicks
            if (mode_pressed_now && !mode_button_currently_pressed) { // PA1 newly pressed
                status_led_set_override(true, false); // LED OFF
                mode_button_currently_pressed = true;
            }
            if (!mode_pressed_now && mode_button_currently_pressed) { // PA1 newly released
                op_mode_clicks_count++;
                status_led_set_override(true, true); // LED ON
                mode_button_currently_pressed = false;
            }

            // Handle PA0 (Tap button) press for confirmation/exit
            if (tap_pressed_now && !tap_button_currently_pressed) {
                 if (!mode_pressed_now && mode_button_currently_pressed) {
                     op_mode_clicks_count++;
                     mode_button_currently_pressed = false;
                 }
                 if (op_mode_change_cb && op_mode_clicks_count > 0) {
                     op_mode_change_cb(op_mode_clicks_count);
                 }
                 just_exited_mode_change = true; // Set flag to prevent immediate re-entry
                 reset_to_idle_state(); // Reset state machine & LED override
            }
            tap_button_currently_pressed = tap_pressed_now;
            break;

        default:
            reset_to_idle_state();
            break;
    }
    // Update previous PA0 state if not in the active mode change state
    if(current_input_state != INPUT_STATE_MODE_CHANGE_ACTIVE) {
         tap_button_currently_pressed = tap_pressed_now;
    }
}


static void input_pins_init(void) {
    // Enable peripheral clocks (moved RCC enables here for clarity)
    rcc_periph_clock_enable(RCC_GPIOA); // For PA0, PA1
    rcc_periph_clock_enable(RCC_GPIOB); // For PB4
    rcc_periph_clock_enable(RCC_SYSCFG); // For EXTI config

    // PA1 (Mode/Swap Button) - Pulled HIGH internally, activate on LOW
    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO1);

    // PB4 (Gate Swap Input) - Configure as input with PULLDOWN, activate on HIGH
    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO4);

    // Initialize Tap driver (configures PA0 and EXTI0)
    tap_init();

    // Initialize External Clock driver (configures PB3 and EXTI3)
    ext_clock_init();

    // Configure EXTI for PB4 (Gate Swap)
    exti_select_source(EXTI4, GPIOB); // Select PB4 for EXTI4
    exti_set_trigger(EXTI4, EXTI_TRIGGER_RISING); // Trigger on rising edge (gate going high)
    exti_enable_request(EXTI4); // Enable interrupt request line

    // Enable EXTI4 interrupt line in NVIC
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
    external_clock_active = false; // Initialize ext clock state
    reset_to_idle_state();
}

void input_handler_update(void) {
    uint32_t now = millis();
    // Read button states
    bool tap_pressed_now = jack_get_digital_input(JACK_IN_TAP);
    bool mode_pressed_now = !gpio_get(GPIOA, GPIO1); // PA1 is pulled up, pressed = LOW

    // --- Handle External Clock ---
    bool ext_timed_out = ext_clock_has_timed_out(now);

    if (!ext_timed_out) {
        // External clock is potentially active (not timed out)
        if (ext_clock_has_new_validated_interval()) {
            // A new stable interval has been validated
            uint32_t validated_interval = ext_clock_get_validated_interval();
            if (validated_interval > 0 && tempo_change_cb) {
                // Report the new validated tempo if it's valid
                tempo_change_cb(validated_interval);
                external_clock_active = true;
                // Reset tap calculations as external clock takes precedence
                 reset_tap_calculation_vars(); 
            }
        } else {
            // No new validated interval, but clock hasn't timed out.
            // Keep using the current tempo (which might be the last validated ext clock tempo).
        }
    } else {
        // External clock HAS timed out
        if (external_clock_active) {
            // We were using external clock, but it just timed out.
            // Revert to the last known *tap* tempo (or wait for new taps).
            external_clock_active = false;
            if (tempo_change_cb) {
                // Report the last known tap tempo (might be 0 if none was set)
                tempo_change_cb(last_reported_tap_tempo_interval);
            }
             // Reset external clock state in driver? Not needed, timeout handles it.
        }
        // External clock is timed out AND wasn't the active source, so Tap Tempo can run.
    }

    // --- Handle Tap Tempo ---
    // Only run if external clock is timed out AND we are in the idle state
    if (ext_timed_out && current_input_state == INPUT_STATE_IDLE) {
         handle_taps_for_tempo(); // This now uses last_reported_tap_tempo_interval
    }


    // --- Handle Button State Machines ---
    // Run Op Mode Switching state machine FIRST to update state based on buttons.
    handle_op_mode_switching(now, tap_pressed_now, mode_pressed_now);

    // --- Handle Calc Mode Swap (Button PA1) ---
    // Allow Button Calc Mode Swap (PA1 short press) only if the state is still IDLE
    // after checking for Op Mode sequence initiation.
    if (current_input_state == INPUT_STATE_IDLE) {
        handle_button_calc_mode_swap();
    }

    // --- Handle Gate Swap Request (PB4) ---
    // Process Gate Swap request only if we are STILL in the IDLE state
    // after running the op mode switching logic and button calc swap logic.
    // This prevents the CV from interfering with button-initiated actions.
    if (current_input_state == INPUT_STATE_IDLE) {
        if (ext_gate_swap_requested) {
            if (calc_mode_change_cb) {
                calc_mode_change_cb();
            }
            ext_gate_swap_requested = false; // Consume the request
            // Reset button calc swap state to prevent accidental trigger on release
            reset_calc_swap_state_local();
            // Apply cooldown to button swap after gate swap
            last_calc_swap_trigger_time = now;
        }
    }
}


// --- Interrupt Service Routine for EXTI4 (Gate Swap PB4) ---
void exti4_isr(void) {
    if (exti_get_flag_status(EXTI4)) {
        uint32_t now = millis();
        // Debounce check for Gate Swap Input
        if (now - last_gate_swap_isr_time >= GATE_SWAP_DEBOUNCE_MS) {
             // Read the current state of PB4. Only trigger swap if the gate is HIGH.
             if (gpio_get(GPIOB, GPIO4)) {
                 ext_gate_swap_requested = true;
                 last_gate_swap_isr_time = now; // Update time only after a valid trigger
             }
        }
        exti_reset_request(EXTI4); // Clear the interrupt flag regardless of debounce
    }
}

// Note: EXTI0 (Tap PA0) is handled in tap.c
// Note: EXTI3 (Ext Clock PB3) is handled in ext_clock.c
