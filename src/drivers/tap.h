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
 * @brief Checks if a new tap interval has been detected since the last call.
 * Clears the internal flag upon being called.
 * @return true if a new interval is ready, false otherwise.
 */
bool tap_detected(void);

/**
 * @brief Gets the last measured interval between taps in milliseconds.
 * Call only after tap_detected() returns true.
 * @return uint32_t The interval in milliseconds.
 */
uint32_t tap_get_interval(void);

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
 */
void tap_check_timeout(uint32_t current_time_ms);


#ifdef __cplusplus
}
#endif
