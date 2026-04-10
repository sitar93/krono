#ifndef MAIN_CONSTANTS_H
#define MAIN_CONSTANTS_H

#include <stdint.h> // Needed for uint32_t
#include "variables.h" // Include tunable parameters

/** Slowest tempo treated as in-spec (BPM). Beat period = 60000 / BPM (e.g. 20 -> 3000 ms). */
#define KRONO_MIN_BPM_SUPPORTED 20u
#define KRONO_MS_AT_MIN_BPM (60000u / KRONO_MIN_BPM_SUPPORTED)

/** Core CPU clock (Hz). Must match rcc_clock_setup_pll in main.c system_init(). */
#define KRONO_CPU_HZ 84000000u
#define KRONO_CPU_CYCLES_PER_MS (KRONO_CPU_HZ / 1000u)

// --- Timing & Tap ---
/** Three gaps per quadruple; tempo uses their median (clicks 1–4, 5–8, …). */
#define TAP_TEMPO_AVG_INTERVALS 3
#define NUM_INTERVALS_FOR_AVG TAP_TEMPO_AVG_INTERVALS
/** Trailing boundary (8, 16, …): blend with leading median if within this ms of click 4/12/…. */
#define TAP_QUAD_BLEND_WINDOW_MS 3000u
#define TAP_QUAD_BLEND_LEADING_NUM 30u
#define TAP_QUAD_BLEND_TRAILING_NUM 70u
#define TAP_QUAD_BLEND_DENOM 100u
/** External clock stability (ms); tap tempo no longer uses spread/median lock. */
#define MAX_INTERVAL_DIFFERENCE 4000
/** No tap for this long resets the quadruple click counter (fresh 1–4 cycle). */
#define TAP_PATTERN_IDLE_RESET_MS 5000u
#define MIN_INTERVAL 33       // ~1818 BPM upper bound (60000/33)
/** Max F1 period (ms). Must be >= KRONO_MS_AT_MIN_BPM so clock in / tap can reach KRONO_MIN_BPM_SUPPORTED. */
#define MAX_INTERVAL 12000u   // ~5 BPM; still allows 20 BPM (3000 ms) and slower external/tap trains
#if MAX_INTERVAL < KRONO_MS_AT_MIN_BPM
#error "MAX_INTERVAL must be >= KRONO_MS_AT_MIN_BPM (20 BPM)"
#endif
#define MIN_CLOCK_INTERVAL 10 // Minimum interval for generated clocks (affects max frequency)
#define DEFAULT_TEMPO_BPM 70
#define DEFAULT_TEMPO_INTERVAL (60000 / DEFAULT_TEMPO_BPM) // ~857ms

/** Silence on PB3 before external clock is dropped; must exceed longest valid period (MAX_INTERVAL). */
#define EXT_CLOCK_TIMEOUT_MS (MAX_INTERVAL + 3000u)

// Output Pulse Duration is now in variables.h

#define DEBOUNCE_DELAY_MS 50      // Debounce delay for tap input in ms
/** Max silence after a tap before tap capture aborts; must exceed MAX_INTERVAL (slow tempos). */
#define TAP_TIMEOUT_MS (MAX_INTERVAL + 12000u)

// --- MOD gestures (modes 12–20): double-click window after first short press ---
#define MOD_GESTURE_DOUBLE_GAP_MS 400

// --- Mode Switching ---
// NEW: Tap Held + Mode Press Logic
#define BUTTON_DEBOUNCE_MS              25  // ms - General debounce time for single button presses (Tap, Mode)
#define MODE_SELECT_TAP_HOLD_TIMEOUT_MS 300 // ms - How long Tap must be held MINIMUM before releasing it without Mode presses cancels the sequence (adjust as needed)
#define MODE_SELECT_MULTI_PRESS_WINDOW_MS 750 // ms - Time window after the LAST Mode press (while Tap is held) to wait for another Mode press before finalizing

// --- Status LED ---
#define STATUS_LED_PIN JACK_OUT_STATUS_LED_PA15 // Use the enum for PA15
// Uses parameters from variables.h (status LED N/L on-times and inter-pulse vs end-of-sequence gaps)
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
