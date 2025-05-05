#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // For operational_mode_t and calculation_mode_t

// --- Callback Types ---

/** 
 * @brief Callback function type for tempo changes initiated by tap tempo.
 * @param new_interval_ms The newly calculated average interval in milliseconds.
 */
typedef void (*input_tempo_change_callback_t)(uint32_t new_interval_ms);

/** 
 * @brief Callback function type for operational mode changes.
 * @param mode_clicks The number of clicks detected during the mode change sequence.
 */
typedef void (*input_op_mode_change_callback_t)(uint8_t mode_clicks);

/** 
 * @brief Callback function type for calculation mode swaps.
 */
typedef void (*input_calc_mode_change_callback_t)(void);


// --- Public Functions ---

/**
 * @brief Initializes the input handler module.
 * Configures input pins and registers callback functions.
 *
 * @param tempo_cb Callback function for tempo changes.
 * @param op_mode_cb Callback function for operational mode changes.
 * @param calc_mode_cb Callback function for calculation mode swaps.
 */
void input_handler_init(
    input_tempo_change_callback_t tempo_cb,
    input_op_mode_change_callback_t op_mode_cb,
    input_calc_mode_change_callback_t calc_mode_cb
);

/**
 * @brief Main update function for the input handler.
 * Checks for button presses (tap, mode, swap) and calls registered callbacks.
 * Manages state machines for tap tempo averaging, mode switching, and calc swap.
 * Should be called periodically from the main loop.
 */
void input_handler_update(void);

#endif // INPUT_HANDLER_H
