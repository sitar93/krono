#ifndef VARIABLES_H
#define VARIABLES_H

/**
 * @file variables.h
 * @brief Centralized definitions for tunable timing parameters.
 */

// --- Output Pulse ---

/** @brief Default duration (ms) for all output pulses (1A/B and Mode Outputs). */
#define DEFAULT_PULSE_DURATION_MS 10

// --- Calculation Mode Persistence Configuration ---

/** @brief Define as 1 to save Calculation Mode (swap state) per Operational Mode.
 *         Define as 0 to use a single global Calculation Mode saved across all modes.
 */
#define SAVE_CALC_MODE_PER_OP_MODE 1

// --- Status LED Timing ---

/** @brief Base time unit for status LED blinking (ms). Adjust for speed. */
#define STATUS_LED_BASE_INTERVAL_MS 200

/** @brief Minimum duration LED stays on after a blink sequence finishes (ms). */
#define STATUS_LED_END_OFF_MS 500

// --- Input Handler Timing ---

/** @brief Max time (ms) between double presses to count as mode change sequence. */
#define MULTI_PRESS_WINDOW_MS 500

/** @brief Debounce time (ms) for detecting stable double press state. */
#define MULTI_PRESS_DEBOUNCE_MS 50

// --- Op Mode Switching (Tap Held + Mode Press) ---

/** @brief Minimum hold duration (ms) for TAP (PA0) before MODE (PA1) presses are counted for Op Mode switching. */
#define OP_MODE_TAP_HOLD_DURATION_MS 1000

/** @brief Time window (ms) after the LAST Mode press (while Tap is held) to wait for another Mode press before finalizing Op Mode selection. */
#define OP_MODE_MULTI_PRESS_WINDOW_MS 500

// --- Calc Mode Swap Timing ---

/** @brief Max duration (ms) for a MODE (PA1) press to be considered a 'short press' for Calc Mode Swap. */
#define CALC_SWAP_MAX_PRESS_DURATION_MS 500

/** @brief Minimum time (ms) between consecutive Calc Mode Swaps to prevent bounce. */
#define CALC_SWAP_COOLDOWN_MS 100

// --- State Persistence Timing ---

/** @brief Minimum time (ms) between consecutive saves to Flash to reduce wear. */
#define SAVE_STATE_COOLDOWN_MS 1000


#endif // VARIABLES_H
