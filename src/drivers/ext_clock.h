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
 *        Notifies on every completed stable sequence (including same BPM as before).
 *        This function also clears the ready flag if it was set.
 *
 * @return true If a new validated interval is ready, false otherwise.
 */
bool ext_clock_has_new_validated_interval(void);

/**
 * @brief Gets the last validated stable interval measured between rising edges.
 *
 * @return uint32_t The last validated interval in ms, or 0 if none has been validated yet.
 */
uint32_t ext_clock_get_validated_interval(void);

/**
 * @brief Gets the timestamp (in ms) of the rising edge that completed the most recent
 *        stable validation sequence.
 *
 * @return uint32_t Validating-edge timestamp; meaningful only after a validation.
 */
uint32_t ext_clock_get_last_validated_event_time(void);

/**
 * @brief True if hard-follow lock should end: out-of-range break, or no in-range edge
 *        within (locked_interval_ms + EXT_CLOCK_LOCK_GRACE_MS). Wrap-safe.
 */
bool ext_clock_has_lost_lock(uint32_t current_time_ms, uint32_t locked_interval_ms);

/**
 * @brief Master period averaged over the last run of consistent edges (up to 64), Q16 milliseconds.
 *        Survives the cable being pulled (last good value). Returns false until a run of 4 edges exists.
 */
bool ext_clock_get_precise_period_q16(uint32_t *period_q16);

/**
 * @brief Mark a locally generated F1 (1A/1B) so near-term PB3 edges skip F1 forwarding only.
 */
void ext_clock_note_local_f1_pulse(uint32_t pulse_time_ms);

/**
 * @brief Consumes one pending external clock rising edge queued for F1 forwarding.
 *
 * @param pulse_time_ms Out: millis() timestamp of the debounced rising edge.
 * @return true if a pulse was consumed, false if the queue is empty.
 */
bool ext_clock_consume_pulse(uint32_t *pulse_time_ms);

/**
 * @brief Drop all queued F1-forwarding pulses (IRQ-safe).
 */
void ext_clock_flush_pulse_queue(void);

/**
 * @brief Enable/disable queuing of accepted in-range edges for F1 forwarding.
 *        Disabling also flushes the queue. IRQ-safe.
 */
void ext_clock_set_pulse_forwarding(bool enable);

/**
 * @brief Atomically flush the F1 queue and enable pulse forwarding (IRQ masked once).
 *        Call at slave entry after the validating edge has been applied separately.
 */
void ext_clock_arm_slave_forwarding(void);

/**
 * @brief Atomically disable forwarding and flush the F1 queue (IRQ masked once).
 *        Call at slave exit.
 */
void ext_clock_disarm_slave_forwarding(void);

#endif // EXT_CLOCK_H
