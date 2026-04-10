#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the tap input pin (PA0) and interrupt.
 */
void tap_init(void);

/**
 * @brief Checks if a new tap edge has been detected since the last call.
 * Clears the internal flag upon being called.
 * @return true on the first edge after reset (interval 0) or on each subsequent debounced edge.
 */
bool tap_detected(void);

/**
 * @brief Interval since the previous accepted edge in milliseconds.
 * After the first edge in a sequence this is 0; from the second edge onward it is the gap used for tap tempo.
 */
uint32_t tap_get_interval(void);

/**
 * @brief Millisecond timestamp of the last tap edge that produced the current interval.
 * Call immediately after tap_detected() returns true (before the next tap overwrites state).
 */
uint32_t tap_get_last_press_time_ms(void);

/**
 * @brief Returns the raw state of the tap button (PA0).
 * @return true if button is pressed (PA0 is LOW), false otherwise.
 */
bool tap_is_button_pressed(void);

/**
 * @brief Checks if the time since the last tap exceeds the timeout.
 * Resets the internal tap sequence tracking if timed out.
 * Call periodically (e.g., from SysTick).
 * @param current_time_ms Current system time in milliseconds.
 * @return true if the in-progress tap sequence was cleared.
 */
bool tap_check_timeout(uint32_t current_time_ms);

/**
 * @brief Clears tap-tempo capture (e.g. after TAP was used as MOD combo modifier).
 */
void tap_abort_capture(void);

#ifdef __cplusplus
}
#endif
