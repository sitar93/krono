#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // For operational_mode_t and calculation_mode_t

// --- Callback Types ---

typedef void (*input_calc_mode_change_callback_t)(void);

/**
 * @brief Callback function type for tempo changes.
 * 
 * @param new_interval_ms The new tempo interval in milliseconds.
 * @param is_external_clock True if the tempo change is due to an external clock event, false otherwise (e.g., tap tempo).
 * @param event_timestamp_ms The timestamp (millis()) of the event that triggered the tempo change (e.g., the external clock pulse, or the last tap).
 */
typedef void (*input_tempo_change_callback_t)(uint32_t new_interval_ms, bool is_external_clock, uint32_t event_timestamp_ms);

/**
 * @brief Callback function type for operational mode changes.
 * 
 * @param mode_clicks The number of clicks on the mode button, indicating the desired mode.
 */
typedef void (*input_op_mode_change_callback_t)(uint8_t mode_clicks);


/**
 * @brief Initializes the Input Handler module.
 * 
 * @param tempo_cb Callback function for tempo changes.
 * @param op_mode_cb Callback function for operational mode changes.
 * @param calc_mode_cb Callback function for calculation mode changes.
 */
void input_handler_init(
    input_tempo_change_callback_t tempo_cb,
    input_op_mode_change_callback_t op_mode_cb,
    input_calc_mode_change_callback_t calc_mode_cb
);

/**
 * @brief Updates the Input Handler state.
 *        This function should be called periodically from the main loop.
 *        It processes tap inputs, external clock signals, and button presses for mode/swap changes.
 */
void input_handler_update(void);

#endif // INPUT_HANDLER_H
