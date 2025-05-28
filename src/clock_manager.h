#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // Includes modes types (operational_mode_t, calculation_mode_t, mode_context_t)

/**
 * @brief Initializes the Clock Manager module.
 *
 * @param initial_op_mode The operational mode to start with.
 * @param initial_tempo_interval The tempo interval (in ms) to start with.
 */
void clock_manager_init(operational_mode_t initial_op_mode, uint32_t initial_tempo_interval);

/**
 * @brief Sets the tempo interval. If the tempo is from an external clock,
 *        it also resets the F1 pulse phase to align with the external event.
 *
 * @param interval_ms The new tempo interval in milliseconds.
 * @param is_external_clock True if the tempo change is due to an external clock event.
 * @param event_timestamp_ms Timestamp of the event that triggered the tempo change.
 */
void clock_manager_set_internal_tempo(uint32_t interval_ms, bool is_external_clock, uint32_t event_timestamp_ms);

/**
 * @brief Gets the current active tempo interval being used by the clock manager.
 * @return uint32_t The current tempo interval in milliseconds.
 */
uint32_t clock_manager_get_current_tempo_interval(void);

/**
 * @brief Updates the clock manager state, generates F1 clock, and calls the active mode's update.
 *        This should be called periodically from the main loop.
 */
void clock_manager_update(void);

/**
 * @brief Changes the currently active operational mode.
 *
 * @param new_mode The operational mode to switch to.
 */
void clock_manager_set_operational_mode(operational_mode_t new_mode);

/**
 * @brief Resets the internal F1 clock phase and signals a sync event to the active mode.
 * @param is_calc_mode_change True if the sync is due to a calculation mode change, false otherwise (e.g., op mode change).
 */
void clock_manager_sync_flags(bool is_calc_mode_change);

/**
 * @brief Sets the calculation mode (Normal/Swapped) within the mode context.
 *
 * @param new_mode The calculation mode to set.
 */
void clock_manager_set_calc_mode(calculation_mode_t new_mode);


#endif // CLOCK_MANAGER_H
