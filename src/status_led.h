#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>
#include "modes/modes.h" // For operational_mode_t
#include "variables.h"   // Timing constants

/**
 * @brief Initializes the status LED module.
 */
void status_led_init(void);

/**
 * @brief Sets the operational mode for the status LED blinking pattern.
 * @param op_mode The operational mode.
 */
void status_led_set_mode(operational_mode_t op_mode);

/**
 * @brief Updates the status LED state.
 * @details Handles blinking logic based on the current time and the internally set mode.
 *          Should be called periodically.
 * @param current_time_ms The current system time in milliseconds.
 */
void status_led_update(uint32_t current_time_ms);

/**
 * @brief Resets the status LED blink sequence.
 * @details Turns the LED off immediately and resets sequence timing.
 */
void status_led_reset(void);

// --- Constants ---
// Moved to variables.h


/**
 * @brief Overrides the normal blinking pattern with a fixed state.
 *
 * @param override_active True to activate override, false to return to normal blinking.
 * @param fixed_state The state (true=ON, false=OFF) to set when override is active.
 */
void status_led_set_override(bool override_active, bool fixed_state);

#endif // STATUS_LED_H
