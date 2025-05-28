#ifndef EXT_CLOCK_H
#define EXT_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initializes the External Clock input pin (PB3) and associated EXTI interrupt.
 */
void ext_clock_init(void);

/**
 * @brief Interrupt handler function for the external clock input (EXTI3).
 *        This function should be called from the corresponding ISR (e.g., exti3_isr).
 *        It handles debounce and interval validation.
 */
void ext_clock_handle_irq(void);

/**
 * @brief Checks if a new validated external clock interval is ready since the last check.
 *        A validated interval is one derived from a sequence of stable input pulses.
 *        This function also clears the ready flag if it was set.
 *
 * @return true If a new validated interval is ready, false otherwise.
 */
bool ext_clock_has_new_validated_interval(void);

/**
 * @brief Gets the last validated stable interval measured between rising edges.
 *        This interval is only updated when a sequence of stable intervals is detected.
 *
 * @return uint32_t The last validated interval in ms, or 0 if no stable interval has been
 *                  validated yet or if the clock has timed out.
 */
uint32_t ext_clock_get_validated_interval(void);

/**
 * @brief Gets the timestamp (in ms) of the rising edge of the external clock pulse
 *        that completed the most recent interval validation.
 *
 * @return uint32_t The timestamp of the validating event, or 0 if no interval
 *                  has been validated yet or if the clock has timed out.
 */
uint32_t ext_clock_get_last_validated_event_time(void);

/**
 * @brief Checks if the external clock signal has stopped (timed out).
 *        Timeout detection is based on the time since the last valid (debounced)
 *        ISR execution.
 *
 * @param current_time_ms The current system time (e.g., from millis()).
 * @return true If the time since the last known clock activity exceeds
 *              EXT_CLOCK_TIMEOUT_MS (defined in main_constants.h), false otherwise.
 */
bool ext_clock_has_timed_out(uint32_t current_time_ms);

#endif // EXT_CLOCK_H
