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

/** @brief Normal (N) pulse ON time (ms). */
#define STATUS_LED_BASE_INTERVAL_MS 120

/** @brief Long (L) pulse ON time (ms); keep much larger than N so L is unambiguous. */
#define STATUS_LED_LONG_ON_MS 1200

/** @brief LED off between consecutive pulses after a normal (N) pulse (ms). */
#define STATUS_LED_INTER_PULSE_OFF_MS 500

/** @brief LED off after a long (L) pulse, before the next pulse (ms); slightly longer than N gap. */
#define STATUS_LED_AFTER_LONG_OFF_MS 650

/** @brief LED off after the full pattern ends, before the next repetition (ms). */
#define STATUS_LED_SEQUENCE_GAP_MS 400

// --- Input Handler Timing ---

/** @brief Max time (ms) between double presses to count as mode change sequence. */
#define MULTI_PRESS_WINDOW_MS 500

/** @brief Debounce time (ms) for detecting stable double press state. */
#define MULTI_PRESS_DEBOUNCE_MS 50

// --- Op Mode Switching (Tap Held + Mode Press) ---

/** @brief Minimum hold duration (ms) for TAP (PA0) before MODE (PA1) presses are counted for Op Mode switching. */
// --- Tap tempo (input_tempo.c, v1.5 least-squares fit) ---
/** Tempo window: taps kept for the BPM fit (older ones drop out). Long = averages out the player's waves. */
#define TAP_FIT_MAX_TAPS 64u
/** Phase window: the most recent taps used for the phase anchor and the outlier test (responsive). */
#define TAP_FIT_PHASE_TAPS 16u
/** Taps needed to lock the tempo (F1 fires on that press). */
#define TAP_FIT_LOCK_TAPS 4u
/** A tap is accepted if within this fraction of a beat from a whole number of beats (else outlier). */
#define TAP_FIT_OUTLIER_TOL 0.25f
// --- Tap latency calibration (tap_calibration.c; hold MOD at power-on) ---
/** MOD must be held this long at boot to enter calibration. */
#define TAP_CALIB_ENTRY_HOLD_MS 1500u
/** Reference click tempo and pulse length on 1A / 1B. */
#define TAP_CALIB_BPM 100u
#define TAP_CALIB_CLICK_MS 30u
/** Measured taps further than this from their median are discarded. */
#define TAP_CALIB_OUTLIER_MS 40
/** Calibration fails (retry) if the kept taps spread more than this (mean absolute deviation). */
#define TAP_CALIB_MAX_SPREAD_MS 30
/** Stored offset is clamped to +- this. */
#define TAP_CALIB_MAX_OFFSET_MS 60
/** Calibration fails if the asynchrony drifts more than this per tap (player off the click tempo). */
#define TAP_CALIB_MAX_DRIFT_MS 3
/** Cancel calibration after this long. */
#define TAP_CALIB_TIMEOUT_MS 40000u
/** Soft phase correction after a refining tap: percent of the remaining error applied per beat. */
#define TAP_PHASE_CORR_PCT 60
/**
 * Integer-BPM snap (conservative): the exact whole BPM is used only if the fitted tempo is within
 * min(SIGMAS x its uncertainty, MAX) of it - i.e. close AND statistically compatible.
 */
#define TAP_BPM_SNAP_SIGMAS 2.0f
#define TAP_BPM_SNAP_MAX 0.25f
/** While locked, a tap after a pause is accepted if it lands within this many beats of the running grid. */
#define TAP_FIT_MAX_GAP_BEATS 256

#define OP_MODE_TAP_HOLD_DURATION_MS 1000

/** @brief From same TAP press start: hold past this (ms) while still holding after qualify → Omega (same Aux blink as qualify); MOD→modes 11–20. */
#define OP_MODE_TAP_OMEGA_HOLD_MS 2000

/** @brief From TAP press start: hold past this (ms) while still holding → Gamma arm; PA3 double short pulse; MOD→modes 21+ (N+20). */
#define OP_MODE_TAP_GAMMA_HOLD_MS 3000

/** @brief From TAP press start: hold past this (ms) → DELTA arm (31-40); PA3 four short pulses; MOD→N+30. */
#define OP_MODE_TAP_DELTA_HOLD_MS 4000

/** @brief Abort op-mode change SM if TAP never released within this total hold from first press (ms). */
#define OP_MODE_TAP_OMEGA_MAX_HOLD_MS 6000

/**
 * Multi-pulse Aux (PA3) timings for krono_aux_led_pattern_start() — e.g. a future hold tier (~3 s) for modes 21–30
 * with two distinct flashes (not used by Omega today; Omega uses the single 100 ms soft blink).
 */
#define AUX_LED_MULTI_PULSE_ON_MS 55
#define AUX_LED_MULTI_PULSE_GAP_MS 100

// --- Aux LED (PA3, under MOD) mode-state display (aux_led.c) ---
/** Steady on (freeze / stop, bank B): perceived brightness (0..1000). Input flashes become brief offs. */
#define AUX_LED_STEADY_LEVEL 800
/** Breathing (modulation active): cycle period; peak perceived brightness for the lowest / highest amount. */
#define AUX_LED_BREATH_PERIOD_MS 2400
#define AUX_LED_BREATH_MIN_PEAK 150
#define AUX_LED_BREATH_MAX_PEAK 700
/** Count display (enumerated state), repeated in a loop: long = tens, short = units. */
#define AUX_LED_COUNT_START_DELAY_MS 200
#define AUX_LED_COUNT_SHORT_ON_MS 120
#define AUX_LED_COUNT_LONG_ON_MS 600
#define AUX_LED_COUNT_OFF_MS 220
#define AUX_LED_COUNT_AFTER_LONG_OFF_MS 350
/** Dark pause between repeated count cycles. */
#define AUX_LED_COUNT_CYCLE_PAUSE_MS 2000


/** @brief Time window (ms) after the LAST Mode press (while Tap is held) to wait for another Mode press before finalizing Op Mode selection. */
#define OP_MODE_MULTI_PRESS_WINDOW_MS 500

// --- Calc Mode Swap / MOD button (PA1) Timing (v1.4.0 behaviour) ---

/** @brief Max duration (ms) for a MODE (PA1) press to count as short press (fire on release). */
#define CALC_SWAP_MAX_PRESS_DURATION_MS 500

/** @brief Minimum time (ms) between consecutive short-MOD / calc-swap actions. */
#define CALC_SWAP_COOLDOWN_MS 100

/**
 * @brief Op-mode select MOD clicks (Tap-hold + MOD) debounce — v1.4.0 MODE_SWITCH_PA1_DEBOUNCE_MS.
 */
#define MOD_BUTTON_DEBOUNCE_MS 50

// --- State Persistence Timing ---

/** @brief Minimum time (ms) between consecutive saves to Flash to reduce wear. */
#define SAVE_STATE_COOLDOWN_MS 1000


#endif // VARIABLES_H
