#include "io.h"
#include "tap.h" // Needed for tap_detected() wrapper
#include "../main_constants.h" // Needed for millis() declaration and JACK_... enums
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h> // Added for TIM3
#include <libopencm3/cm3/nvic.h>    // Added for NVIC
// #include <libopencm3/stm32/common/timer_common_all.h> // timer_reset seems missing in link stage
#include <limits.h> // For UINT32_MAX
#include <stdint.h> // For int32_t
#include <libopencm3/stm32/syscfg.h> // For SYSCFG clock (F4)

// Pin definitions (Internal verification/reference - the jack_output_map is the source of truth)
// ... (pin mapping comments as before) ...

// Complete mapping of output pins defined in io.h enum
static const struct {
    uint32_t port;
    uint16_t pin;
} jack_output_map[NUM_JACK_OUTPUTS] = { // Size based on enum definition in io.h
    // Group A
    [JACK_OUT_1A] = {GPIOB, GPIO0},
    [JACK_OUT_2A] = {GPIOB, GPIO1},
    [JACK_OUT_3A] = {GPIOA, GPIO2}, // Was PC13
    [JACK_OUT_4A] = {GPIOB, GPIO15},
    [JACK_OUT_5A] = {GPIOB, GPIO5},
    [JACK_OUT_6A] = {GPIOB, GPIO6},
    // Group B
    [JACK_OUT_1B] = {GPIOB, GPIO14},
    [JACK_OUT_2B] = {GPIOB, GPIO13},
    [JACK_OUT_3B] = {GPIOB, GPIO12},
    [JACK_OUT_4B] = {GPIOB, GPIO8},
    [JACK_OUT_5B] = {GPIOB, GPIO9},
    [JACK_OUT_6B] = {GPIOB, GPIO10},
    // Unused/Special pins
    [JACK_OUT_UNUSED_PB2] = {GPIOB, GPIO2},
    // PB3 is Ext Clock Input, no map entry needed
    // PB4 is Ext Gate Swap Input, no map entry needed
    [JACK_OUT_UNUSED_PB7] = {GPIOB, GPIO7},
    [JACK_OUT_UNUSED_PB11]= {GPIOB, GPIO11},
    [JACK_OUT_STATUS_LED_PA15] = {GPIOA, GPIO15},
    [JACK_OUT_AUX_LED_PA3] = {GPIOA, GPIO3},
};

// Internal state for output protection (placeholder)
static bool output_protection_enabled = false;

// --- Pulse Timer Mechanism ---
typedef struct {
    uint32_t end_time_ms; // Time when the pulse should end
    bool active;          // Is a pulse currently active on this pin?
} pulse_timer_t;

// This state is managed by the timer ISR
// IMPORTANT: Size is now NUM_JACK_OUTPUTS but timer logic only applies to Group A/B
static volatile pulse_timer_t pulse_timers[NUM_JACK_OUTPUTS];

// --- Public Function Implementations ---

/* General I/O initialization */
void io_init(void) {
    // Enable clocks for GPIO ports A, B, C
    rcc_periph_clock_enable(RCC_GPIOA); // PA0, PA1, PA2, PA3 (Aux), PA15 (Status)
    rcc_periph_clock_enable(RCC_GPIOB); // Most outputs, PB3 (Ext Clk), PB4 (Ext Gate)
    rcc_periph_clock_enable(RCC_GPIOC); // Might still be needed for oscillator or other pins
    rcc_periph_clock_enable(RCC_SYSCFG); // Required for EXTI line configuration on F4

    // IMPORTANT: PB3 (Ext Clk) and PB4 (Ext Gate) conflict with SWD/JTAG (SWO, NJTRST).
    // We assume DFU is used for uploading.
    // Ensure they aren't configured for SWD/JTAG function in other parts of the setup.

    // Input configuration is handled by respective drivers (tap_init, input_handler_init, ext_clock_init)

    // Configure all defined output pins in the map (including Aux LED)
    for (jack_output_t j = 0; j < NUM_JACK_OUTPUTS; j++) {
        // Check if the entry in the map is valid (port is assigned)
        // Skip if port is 0 (shouldn't happen with current map)
        if (jack_output_map[j].port == 0) continue;

        bool is_active_output = (j <= JACK_OUT_6A) ||
                                (j >= JACK_OUT_1B && j <= JACK_OUT_6B) ||
                                (j == JACK_OUT_STATUS_LED_PA15) ||
                                (j == JACK_OUT_AUX_LED_PA3); // Include Aux LED

        // Pins defined in jack_output_map but not used as active outputs
        // Note: PB3/PB4 are technically in the map but not configured here as they are inputs.
        bool is_unused_map_entry = (j == JACK_OUT_UNUSED_PB2) ||
                                   (j == JACK_OUT_UNUSED_PB7) ||
                                   (j == JACK_OUT_UNUSED_PB11);

        if (is_active_output) {
            gpio_mode_setup(jack_output_map[j].port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, jack_output_map[j].pin);
            gpio_set_output_options(jack_output_map[j].port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, jack_output_map[j].pin);
            gpio_clear(jack_output_map[j].port, jack_output_map[j].pin); // Initialize LOW
        } else if (is_unused_map_entry) {
            // Configure truly unused pins listed in the map as input pull-down
            // (General unused pins are configured in main.c configure_unused_pins)
            gpio_mode_setup(jack_output_map[j].port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, jack_output_map[j].pin);
        }
        // Implicit else: Pins like PB3, PB4 are handled by their respective input drivers
    }
}

// Initialize the hardware timer (TIM3) for pulse management
void pulse_timer_init(void) {
     // Initialize pulse timers state
    for (int i = 0; i < NUM_JACK_OUTPUTS; i++) {
        pulse_timers[i].active = false;
        pulse_timers[i].end_time_ms = 0;
    }

    rcc_periph_clock_enable(RCC_TIM3); // Enable TIM3 clock

    // Reset TIM3 registers to default state before configuration
    rcc_periph_reset_pulse(RST_TIM3);

    // Configure TIM3 for 1ms interrupts.
    uint32_t timer_clock_freq = rcc_get_timer_clk_freq(TIM3); // Get actual timer frequency
    uint32_t prescaler = (timer_clock_freq / 1000000) - 1; // For 1MHz count frequency
    uint32_t period = 1000 - 1; // Count to 1000 for 1ms period (1MHz / 1000 = 1kHz)

    timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM3, prescaler);
    timer_set_period(TIM3, period);
    timer_enable_preload(TIM3);
    timer_enable_irq(TIM3, TIM_DIER_UIE);
    nvic_enable_irq(NVIC_TIM3_IRQ);
    timer_enable_counter(TIM3);
}

// Hardware Timer 3 Interrupt Service Routine
void tim3_isr(void) {
    if (timer_get_flag(TIM3, TIM_SR_UIF)) {
        timer_clear_flag(TIM3, TIM_SR_UIF);

        uint32_t current_time_ms = millis();

        // Iterate ONLY over outputs managed by this timer (Group A/B)
        for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j++) {
            // Skip the gap between groups if enums aren't contiguous
            if (j > JACK_OUT_6A && j < JACK_OUT_1B) continue;

            if (((volatile pulse_timer_t*)&pulse_timers[j])->active) {
                 uint32_t end_time = ((volatile pulse_timer_t*)&pulse_timers[j])->end_time_ms;

                 if ((int32_t)(current_time_ms - end_time) >= 0) {
                    if (jack_output_map[j].port != 0) {
                        gpio_clear(jack_output_map[j].port, jack_output_map[j].pin);
                    }
                    ((volatile pulse_timer_t*)&pulse_timers[j])->active = false;
                }
            }
        }
    }
}

// Read digital input state
bool jack_get_digital_input(jack_input_t input) {
    switch(input) {
        case JACK_IN_TAP:          return gpio_get(GPIOA, GPIO0);
        case JACK_IN_MODE_SWAP:    return !gpio_get(GPIOA, GPIO1); // PA1 pulled up, pressed = LOW
        case JACK_IN_GATE_SWAP:    return gpio_get(GPIOB, GPIO4);
        default:                   return false;
    }
}

// Read analog input (Not Implemented)
float jack_get_analog_input(jack_input_t input) {
    (void)input; return 0.0f;
}

// Wrapper for tap detection status
bool jack_is_tap_detected(void) {
    return tap_detected();
}

// Placeholder for output protection setting
void set_output_protection(bool enabled) {
    output_protection_enabled = enabled;
}

// Placeholder for general protection setting
void io_set_protections(bool enable) {
    set_output_protection(enable);
}

// Set the state (HIGH/LOW) of a specific output jack
void set_output(jack_output_t jack, bool state) {
    if (jack >= NUM_JACK_OUTPUTS || jack_output_map[jack].port == 0) return;

     // Apply only to defined active outputs (including Aux LED now)
     bool is_active_output = (jack <= JACK_OUT_6A) ||
                             (jack >= JACK_OUT_1B && jack <= JACK_OUT_6B) ||
                             (jack == JACK_OUT_STATUS_LED_PA15) ||
                             (jack == JACK_OUT_AUX_LED_PA3);

    if (!is_active_output) return;

    if (state) { gpio_set(jack_output_map[jack].port, jack_output_map[jack].pin); }
    else { gpio_clear(jack_output_map[jack].port, jack_output_map[jack].pin); }
}

// Set output high for a duration using the hardware timer mechanism (Revised)
// This now ONLY handles Group A and Group B outputs.
// Aux LED must be pulsed manually or with a different mechanism.
void set_output_high_for_duration(jack_output_t jack, uint32_t duration_ms) {
    if (jack >= NUM_JACK_OUTPUTS || duration_ms == 0 || jack_output_map[jack].port == 0) {
        return;
    }

    // Define which outputs are managed by the hardware pulse timer
    bool is_pulsable_output = (jack <= JACK_OUT_6A) ||                     // Group A
                              (jack >= JACK_OUT_1B && jack <= JACK_OUT_6B); // Group B

    // If it's not an output type handled by this timer mechanism, simply return.
    if (!is_pulsable_output) {
        return;
    }

    // --- Proceed only if it IS a pulsable output (Groups A/B) ---

    nvic_disable_irq(NVIC_TIM3_IRQ);

    if (!pulse_timers[jack].active) {
        set_output(jack, true); // Use basic set_output
        uint32_t current_time = millis();
        ((volatile pulse_timer_t*)&pulse_timers[jack])->end_time_ms = current_time + duration_ms;
        ((volatile pulse_timer_t*)&pulse_timers[jack])->active = true;
    }

    nvic_enable_irq(NVIC_TIM3_IRQ);
}


/**
 * @brief Forcibly stops all active timed pulses (Group A/B only) and sets outputs LOW.
 * Disables and re-enables TIM3 IRQ internally for safety.
 */
void io_cancel_all_timed_pulses(void) {
    nvic_disable_irq(NVIC_TIM3_IRQ); // Enter critical section

    // Iterate ONLY over Group A/B outputs
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j++) {
         if (j > JACK_OUT_6A && j < JACK_OUT_1B) continue;

         // Check if it's an output jack managed by the pulse timer
         bool is_pulsable_output = (j <= JACK_OUT_6A) ||
                                   (j >= JACK_OUT_1B && j <= JACK_OUT_6B);

        if (is_pulsable_output && jack_output_map[j].port != 0) {
            gpio_clear(jack_output_map[j].port, jack_output_map[j].pin);
            ((volatile pulse_timer_t*)&pulse_timers[j])->active = false;
            ((volatile pulse_timer_t*)&pulse_timers[j])->end_time_ms = 0;
        }
    }

    nvic_enable_irq(NVIC_TIM3_IRQ); // Exit critical section
}


/**
 * @brief Set all physical jack outputs (Groups A & B) to LOW state.
 *        Does NOT affect Status LED or Aux LED.
 */
void io_all_outputs_off(void) {
    // Iterate through Group A and Group B outputs
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j++) {
        if (j > JACK_OUT_6A && j < JACK_OUT_1B) continue;

        bool is_group_ab_output = (j <= JACK_OUT_6A) ||
                                  (j >= JACK_OUT_1B && j <= JACK_OUT_6B);

        if (is_group_ab_output && jack_output_map[j].port != 0) {
            gpio_clear(jack_output_map[j].port, jack_output_map[j].pin);
        }
    }
}


#ifdef DEBUG
void io_dump_config(void) {}
#endif
