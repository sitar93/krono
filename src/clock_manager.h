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
 * @brief Sets the tempo interval (tap or external validated interval).
 *        Does not reset f1_tick_counter. Beat phase is aligned to event_timestamp_ms
 *        (tap press time from input_tempo, or ext_clock validating edge) so ongoing taps
 *        stay on the same grid as the internal F1 clock.
 *
 * @param interval_ms The new tempo interval in milliseconds.
 * @param is_external_clock Unused (same phase logic for tap and external interval updates).
 * @param event_timestamp_ms Beat anchor (ms); if 0 or in the future, uses millis().
 */
void clock_manager_set_internal_tempo(uint32_t interval_ms, bool is_external_clock, uint32_t event_timestamp_ms);

/**
 * @brief Set the tempo with sub-millisecond precision (Q16 ms) without touching the beat phase.
 *        Used for the precise external-clock period, while following and after the cable is pulled.
 */
void clock_manager_set_tempo_q16_keep_phase(uint32_t interval_q16);

/** Tap tempo lock: tempo (Q16 ms) applied and F1 fired at the press (anchor_ms). Ignored while slaved. */
void clock_manager_tap_lock(uint32_t interval_q16, uint32_t anchor_ms);

/**
 * Tap tempo refinement: new tempo (Q16 ms) and a soft phase correction toward anchor_ms (the fitted tap time),
 * spread over the next beats (no jumps, no doubled / missing beats). Ignored while slaved.
 */
void clock_manager_tap_refine(uint32_t interval_q16, uint32_t anchor_ms);

/**
 * @brief Arm F1 pulse + tempo on the next clock_manager_update (tap quadruple boundary).
 *        Ensures mode update sees f1_rising_edge in the same frame as the 1A/1B pulse.
 */
void clock_manager_arm_tap_quadruple_boundary(uint32_t interval_ms, uint32_t event_timestamp_ms);

/**
 * @brief Enable or disable hard-follow of F1 to external clock rising edges.
 *        Prefer clock_manager_begin_external_slave / end_external_slave for session transitions.
 *        Internal flywheel remains active either way.
 */
void clock_manager_set_external_clock_slaved(bool slaved);

/**
 * @brief Enter Clock In hard-follow: set tempo, arm F1 forwarding (flush backlog), apply exactly
 *        one F1 for the validating edge, then snap to subsequent non-echo edges. Internal timer
 *        stays as flywheel if a beat is missed.
 */
void clock_manager_begin_external_slave(uint32_t interval_ms, uint32_t validating_edge_ms);

/**
 * @brief Leave Clock In hard-follow (disarm forwarding + clear follow). Tempo restore is caller's job.
 */
void clock_manager_end_external_slave(void);

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
 * @brief Restarts the internal F1 beat phase at the current time (no 1A/1B auto pulse).
 *        Used by Gamma mode 21 MOD/CV reset so the next tick aligns to “now” without catch-up bursts.
 */
void clock_manager_restart_beat_phase_now(void);

/**
 * @brief Sets the calculation mode (Normal/Swapped) within the mode context.
 *
 * @param new_mode The calculation mode to set.
 */
void clock_manager_set_calc_mode(calculation_mode_t new_mode);

/**
 * @brief Clears the calc_mode_changed flag to prevent unwanted bank swaps.
 */
void clock_manager_clear_calc_mode_changed();


#endif // CLOCK_MANAGER_H
