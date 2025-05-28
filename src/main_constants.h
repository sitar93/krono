#ifndef MAIN_CONSTANTS_H
#define MAIN_CONSTANTS_H

#include <stdint.h> // Needed for uint32_t
#include "variables.h" // Include tunable parameters

// --- Timing & Tap ---
#define NUM_INTERVALS_FOR_AVG 3 // We need 3 intervals for a stable average
#define MAX_INTERVAL_DIFFERENCE 3000 // Max difference allowed between intervals in ms
#define MIN_INTERVAL 33       // Corresponds to approx. 1818 BPM (60000ms / 33ms)
#define MAX_INTERVAL 6000 // Corresponds to 10 BPM (60000ms / 6000ms = 10 BPM)
#define MIN_CLOCK_INTERVAL 10 // Minimum interval for generated clocks (affects max frequency)
#define DEFAULT_TEMPO_BPM 70
#define DEFAULT_TEMPO_INTERVAL (60000 / DEFAULT_TEMPO_BPM) // ~857ms

// Timeout for external clock detection (in ms)
// If no valid pulse is detected for this duration, the system reverts to tap tempo.
#define EXT_CLOCK_TIMEOUT_MS 5000

// Output Pulse Duration is now in variables.h

#define DEBOUNCE_DELAY_MS 50      // Debounce delay for tap input in ms
#define TAP_TIMEOUT_MS 10000      // Timeout for tap sequence in ms (10 seconds)

// --- Mode Switching ---
// NEW: Tap Held + Mode Press Logic
#define BUTTON_DEBOUNCE_MS              25  // ms - General debounce time for single button presses (Tap, Mode)
#define MODE_SELECT_TAP_HOLD_TIMEOUT_MS 300 // ms - How long Tap must be held MINIMUM before releasing it without Mode presses cancels the sequence (adjust as needed)
#define MODE_SELECT_MULTI_PRESS_WINDOW_MS 750 // ms - Time window after the LAST Mode press (while Tap is held) to wait for another Mode press before finalizing

// --- Status LED ---
#define STATUS_LED_PIN JACK_OUT_STATUS_LED_PA15 // Use the enum for PA15
// Uses parameters from variables.h (STATUS_LED_BASE_INTERVAL_MS, STATUS_LED_END_OFF_MS)
// Old constants for reference (can be removed if variables.h is always used):
// #define STATUS_BLINK_ON_MS 100
// #define STATUS_BLINK_OFF_MS 150
// #define STATUS_BLINK_SEQUENCE_INTERVAL_MS 2000


// --- Global Utility Function Declarations ---

/**
 * @brief Gets the number of milliseconds elapsed since the system started.
 * Defined in main.c, based on SysTick.
 * @return Current system time in milliseconds.
 */
uint32_t millis(void); // Declared here, defined in main.c


#endif // MAIN_CONSTANTS_H
