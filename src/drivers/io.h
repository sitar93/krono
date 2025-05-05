#pragma once
#include <libopencm3/stm32/gpio.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= PIN DEFINITIONS ================= */

/**
 * @brief Input jack enumeration
 */
typedef enum {
    JACK_IN_TAP          = 0, ///< Tap input (PA0)
    JACK_IN_MODE_SWAP,       ///< Mode/Calculation Swap input (PA1)
    JACK_IN_GATE_SWAP,       ///< External Gate input for Calculation Swap (PB4) - Was PA12
    // JACK_IN_CLOCK and JACK_IN_CV are deprecated/internal names
} jack_input_t;

/**
 * @brief Output jack enumeration (Group A and Group B) - Now 6 outputs per group
 */
typedef enum {
    // Group A (Multiplication Mode Default - Active HIGH)
    JACK_OUT_1A = 0, ///< Factor 1 (PB0) - Always mirrors JACK_OUT_1B
    JACK_OUT_2A,     ///< Factor 2 (PB1)
    JACK_OUT_3A,     ///< Factor 3 (PA2) - Was PC13
    JACK_OUT_4A,     ///< Factor 4 (PB15) - Was previously used for Status LED
    JACK_OUT_5A,     ///< Factor 5 (PB5)
    JACK_OUT_6A,     ///< Factor 6 (PB6)

    // Group B (Division Mode Default - Active HIGH)
    JACK_OUT_1B,     ///< Factor 1 (PB14) - Always mirrors JACK_OUT_1A
    JACK_OUT_2B,     ///< Factor -2 (PB13)
    JACK_OUT_3B,     ///< Factor -3 (PB12)
    JACK_OUT_4B,     ///< Factor -4 (PB8)
    JACK_OUT_5B,     ///< Factor -5 (PB9)
    JACK_OUT_6B,     ///< Factor -6 (PB10)

    // Define unused/special pins explicitly
    JACK_OUT_UNUSED_PB2, ///< Unused Output (PB2)
    JACK_OUT_UNUSED_PB3, ///< Unused Output (PB3) - Now used for External Clock Input
    JACK_OUT_UNUSED_PB4, ///< Unused Output (PB4) - Now used for External Gate Swap Input
    JACK_OUT_UNUSED_PB7, ///< Unused Output (PB7) - Previously JACK_OUT_7A
    JACK_OUT_UNUSED_PB11,///< Unused Output (PB11) - Previously JACK_OUT_7B
    JACK_OUT_STATUS_LED_PA15, ///< Status LED Output (PA15)
    JACK_OUT_AUX_LED_PA3,     ///< Auxiliary LED Output (PA3)

    NUM_JACK_OUTPUTS // Total number of defined output enums (including unused/special)
} jack_output_t;

/* Port configuration */
// #define JACK_IN_PORT   GPIOA  ///< Port for all inputs (No longer true, PA0/PA1 on A, PB4 on B)

/* ================= PUBLIC INTERFACE ================= */

/**
 * @brief Initialize all I/O pins according to the latest mapping
 */
void io_init(void);

/**
 * @brief Initialize the hardware timer based pulse management system.
 * Must be called after system clocks are configured.
 */
void pulse_timer_init(void);

/**
 * @brief Set output state for a specific jack
 * @param output Output jack enum to configure
 * @param state true = HIGH, false = LOW
 */
void set_output(jack_output_t output, bool state);

/**
 * @brief Set an output HIGH for a specific duration in milliseconds.
 * The pin will automatically be set LOW after the duration expires.
 * Uses a hardware timer (TIM3) interrupt for accurate timing.
 * @param output Output jack enum to pulse.
 * @param duration_ms Duration of the pulse in milliseconds.
 */
void set_output_high_for_duration(jack_output_t output, uint32_t duration_ms);

/**
 * @brief Forcibly stops all active timed pulses and sets outputs LOW.
 * Disables and re-enables TIM3 IRQ internally for safety.
 */
void io_cancel_all_timed_pulses(void);

/**
 * @brief Set all physical jack outputs (Groups A & B) to LOW state.
 */
void io_all_outputs_off(void);

/* Function io_update_pulse_timers removed as logic moved to TIM3 ISR */

/**
 * @brief Read digital input state
 * @param input Input pin enum to read
 * @return true if signal is HIGH
 */
bool jack_get_digital_input(jack_input_t input);

/**
 * @brief Read normalized analog value (0.0-1.0) - Not Implemented
 * @param input Analog input pin (not used as analog)
 * @return Currently always 0.0f
 */
float jack_get_analog_input(jack_input_t input);

/**
 * @brief Check if a tap has been detected (wrapper for tap driver)
 * @return true if a tap was detected since last check
 */
bool jack_is_tap_detected(void);

/**
 * @brief Enable/disable hardware protections (placeholder)
 * @param enable true to enable protections
 */
void io_set_protections(bool enable);

/**
 * @brief Sets the output protection feature (placeholder)
 * @param enabled true to enable protection
 */
void set_output_protection(bool enabled);

#ifdef DEBUG
/**
 * @brief Prints current configuration via UART (placeholder)
 */
void io_dump_config(void);
#endif

#ifdef __cplusplus
}
#endif
