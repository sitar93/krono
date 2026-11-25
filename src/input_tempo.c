#include "input_tempo.h"

#include "clock_manager.h"
#include "drivers/tap.h"
#include "drivers/tap_calib_store.h"
#include "main_constants.h"
#include "variables.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Tap tempo (v1.5): least-squares fit over the whole tap sequence.
 *
 *  - Two windows (chosen on logged sessions): the TEMPO is fitted on up to TAP_FIT_MAX_TAPS (64) taps so the
 *    player's slow waves average out; the PHASE anchor and the outlier test use the last TAP_FIT_PHASE_TAPS (16).
 *  - Every accepted tap is stored with its beat index (missed taps are allowed: an interval close to k beats
 *    advances the index by k). The tempo is the slope of the best-fit line through (index, time), in Q16 ms,
 *    so it gets more precise with every tap and has no 1 ms rounding (no drift afterwards).
 *  - Outliers (interval not close to a whole number of beats) are ignored; two in a row mean the player
 *    changed tempo, and the sequence restarts from those taps.
 *  - Tap TAP_FIT_LOCK_TAPS (4) locks: F1 fires on that press and the tempo is applied.
 *  - Every later tap refines the tempo and nudges the phase softly toward the fitted tap time
 *    (clock_manager_tap_refine): no jumps, no doubled or missing beats.
 *  - Human anticipation: the grid is placed tap_calib_get_offset_ms() after the taps (personal value from the
 *    power-on calibration, 0 if never calibrated). Phase only, the tempo is unaffected.
 *  - Integer-BPM snap (conservative): within min(2 sigma, 0.05 BPM) of a whole BPM, the exact BPM is used.
 *  - Resume: once locked, taps after a pause continue the same sequence if they land on the running grid
 *    (any number of beats later), so a few taps every now and then keep the phase indefinitely. Before the
 *    lock, TAP_PATTERN_IDLE_RESET_MS without taps starts a new sequence.
 * External clock never goes through here (input_handler ignores taps while hard-following).
 */

static input_tempo_emit_callback_t tempo_emit_cb = 0; // Kept for API compatibility (tap no longer uses it)
static uint32_t last_reported_tap_tempo_interval = 0;

static uint32_t fit_t0_ms = 0;                     // Time origin (first tap of the sequence)
static float fit_t[TAP_FIT_MAX_TAPS];              // Tap times relative to fit_t0_ms
static int32_t fit_x[TAP_FIT_MAX_TAPS];            // Beat index of each tap
static uint32_t fit_n = 0;                         // Accepted taps in the sequence
static bool fit_locked = false;
static uint32_t last_pattern_tap_ms = 0;
static uint32_t pending_outlier_ms = 0;            // Last rejected tap (tempo-change detection)
static bool has_pending_outlier = false;

static void pattern_reset(void) {
    fit_n = 0;
    fit_locked = false;
    last_pattern_tap_ms = 0;
    has_pending_outlier = false;
}

void input_tempo_init(input_tempo_emit_callback_t emit_cb) {
    tempo_emit_cb = emit_cb;
    (void)tempo_emit_cb;
    pattern_reset();
    last_reported_tap_tempo_interval = 0;
}

void input_tempo_reset_calculation(void) {
    pattern_reset();
}

void input_tempo_on_tap_timeout(void) {
    if (!fit_locked) {
        pattern_reset();
    }
}

void input_tempo_clear_last_reported(void) {
    last_reported_tap_tempo_interval = 0;
}

/** Start a new sequence with the given taps (absolute ms, consecutive beats). */
static void fit_start(const uint32_t *taps, uint32_t count) {
    fit_n = 0;
    fit_locked = false;
    has_pending_outlier = false;
    if (count == 0u) {
        return;
    }
    fit_t0_ms = taps[0];
    for (uint32_t i = 0; i < count && i < TAP_FIT_MAX_TAPS; i++) {
        fit_t[i] = (float)(taps[i] - fit_t0_ms);
        fit_x[i] = (int32_t)i;
        fit_n++;
    }
}

static void fit_push(uint32_t t_ms, int32_t x) {
    if (fit_n >= TAP_FIT_MAX_TAPS) { // Drop the oldest
        for (uint32_t i = 1; i < fit_n; i++) {
            fit_t[i - 1u] = fit_t[i];
            fit_x[i - 1u] = fit_x[i];
        }
        fit_n--;
    }
    if (fit_n > 0u && fit_t[0] > 1000000.0f) { // Rebase the time origin (keep float precision)
        uint32_t const shift = (uint32_t)fit_t[0];
        fit_t0_ms += shift;
        for (uint32_t i = 0; i < fit_n; i++) {
            fit_t[i] -= (float)shift;
        }
    }
    fit_t[fit_n] = (float)(t_ms - fit_t0_ms);
    fit_x[fit_n] = x;
    fit_n++;
}

static float fit_sigma_slope = 0.0f; // Standard error of the last fitted slope (ms per beat)

/**
 * Least-squares slope (ms per beat) and fitted time of the last tap (relative to fit_t0_ms), over the most recent
 * `last` taps (all if fewer). Also sets fit_sigma_slope.
 */
static bool fit_solve(uint32_t last, float *slope_ms, float *last_fit_ms) {
    uint32_t const n = (fit_n < last) ? fit_n : last;
    uint32_t const o = fit_n - n;
    if (n < 2u) {
        return false;
    }
    float sx = 0.0f, st = 0.0f;
    for (uint32_t i = o; i < fit_n; i++) {
        sx += (float)fit_x[i];
        st += fit_t[i];
    }
    float const mx = sx / (float)n;
    float const mt = st / (float)n;
    float sxx = 0.0f, sxt = 0.0f;
    for (uint32_t i = o; i < fit_n; i++) {
        float const dx = (float)fit_x[i] - mx;
        sxx += dx * dx;
        sxt += dx * (fit_t[i] - mt);
    }
    if (sxx <= 0.0f) {
        return false;
    }
    *slope_ms = sxt / sxx;
    *last_fit_ms = mt + *slope_ms * ((float)fit_x[fit_n - 1u] - mx);
    // Slope standard error from the residuals (needs >= 3 taps)
    fit_sigma_slope = 1e9f;
    if (n >= 3u) {
        float ssr = 0.0f;
        for (uint32_t i = o; i < fit_n; i++) {
            float const r = fit_t[i] - (mt + *slope_ms * ((float)fit_x[i] - mx));
            ssr += r * r;
        }
        fit_sigma_slope = sqrtf((ssr / (float)(n - 2u)) / sxx);
    }
    return true;
}

static uint32_t ms_to_q16(float ms) {
    return (uint32_t)(ms * 65536.0f + 0.5f);
}

/** Conservative integer-BPM snap: only when close AND statistically compatible. */
static float snap_to_integer_bpm(float slope_ms) {
    float const bpm = 60000.0f / slope_ms;
    float const sigma_bpm = 60000.0f * fit_sigma_slope / (slope_ms * slope_ms);
    float const target = roundf(bpm);
    float tol = TAP_BPM_SNAP_SIGMAS * sigma_bpm;
    if (tol > TAP_BPM_SNAP_MAX) {
        tol = TAP_BPM_SNAP_MAX;
    }
    if (target > 0.0f && fabsf(bpm - target) <= tol) {
        return 60000.0f / target;
    }
    return slope_ms;
}



input_tempo_event_t input_tempo_handle_tap_event(void) {
    if (!tap_detected()) {
        return INPUT_TEMPO_EVT_NONE;
    }
    uint32_t const press_ms = tap_get_last_press_time_ms();
    (void)tap_get_interval(); // Consume the driver's interval (timestamps are used instead)

    if (!fit_locked && last_pattern_tap_ms != 0u && (press_ms - last_pattern_tap_ms) > TAP_PATTERN_IDLE_RESET_MS) {
        pattern_reset();
    }
    uint32_t const prev_tap_ms = last_pattern_tap_ms;
    last_pattern_tap_ms = press_ms;

    // First tap of a sequence
    if (fit_n == 0u) {
        fit_start(&press_ms, 1u);
        return INPUT_TEMPO_EVT_COUNTED;
    }

    uint32_t const last_ms = fit_t0_ms + (uint32_t)fit_t[fit_n - 1u];
    uint32_t const gap = press_ms - last_ms;

    // Current beat estimate: fitted slope, or the first interval
    float est = 0.0f;
    float last_fit = 0.0f;
    if (!fit_solve(TAP_FIT_PHASE_TAPS, &est, &last_fit)) {
        est = (float)gap;
    }
    if (gap < MIN_INTERVAL || (fit_n == 1u && gap > MAX_INTERVAL)) {
        uint32_t const t[1] = {press_ms};
        fit_start(t, 1u);
        return INPUT_TEMPO_EVT_COUNTED;
    }

    // How many beats since the last accepted tap? Accept only if close to a whole number (missed taps OK)
    float const beats_f = (float)gap / est;
    int32_t k = (int32_t)(beats_f + 0.5f);
    float const err = (float)gap - (float)k * est;
    int32_t const max_k = fit_locked ? TAP_FIT_MAX_GAP_BEATS : 4;
    bool const consistent = (k >= 1) && (k <= max_k) && (err < TAP_FIT_OUTLIER_TOL * est) &&
                            (err > -TAP_FIT_OUTLIER_TOL * est);

    if (!consistent) {
        if (has_pending_outlier) {
            // Two outliers in a row: the player changed tempo. Restart from them.
            uint32_t const t[2] = {pending_outlier_ms, press_ms};
            uint32_t const d = press_ms - pending_outlier_ms;
            if (d >= MIN_INTERVAL && d <= MAX_INTERVAL) {
                fit_start(t, 2u);
            } else {
                fit_start(&t[1], 1u);
            }
            return INPUT_TEMPO_EVT_COUNTED;
        }
        has_pending_outlier = true;
        pending_outlier_ms = press_ms;
        (void)prev_tap_ms;
        return INPUT_TEMPO_EVT_NONE; // Ignored tap
    }
    has_pending_outlier = false;
    fit_push(press_ms, fit_x[fit_n - 1u] + k);

    float phase_slope = 0.0f;
    float slope = 0.0f;
    float tempo_last_fit = 0.0f;
    // Phase window first, tempo window last (fit_sigma_slope must belong to the tempo fit for the snap)
    if (!fit_solve(TAP_FIT_PHASE_TAPS, &phase_slope, &last_fit) ||
        !fit_solve(TAP_FIT_MAX_TAPS, &slope, &tempo_last_fit) ||
        slope < (float)MIN_INTERVAL || slope > (float)MAX_INTERVAL) {
        return INPUT_TEMPO_EVT_COUNTED;
    }
    (void)phase_slope;
    (void)tempo_last_fit;
    slope = snap_to_integer_bpm(slope);
    last_reported_tap_tempo_interval = (uint32_t)(slope + 0.5f);

    if (!fit_locked) {
        if (fit_n >= TAP_FIT_LOCK_TAPS) {
            fit_locked = true;
            // F1 on this press, delayed by the typical human anticipation
            clock_manager_tap_lock(ms_to_q16(slope), press_ms + (uint32_t)tap_calib_get_offset_ms());
            return INPUT_TEMPO_EVT_LOCKED;
        }
        return INPUT_TEMPO_EVT_COUNTED;
    }

    // Locked: refine tempo, soft phase nudge toward the fitted time of this tap
    uint32_t const anchor = fit_t0_ms + (uint32_t)(last_fit + 0.5f) + (uint32_t)tap_calib_get_offset_ms();
    clock_manager_tap_refine(ms_to_q16(slope), anchor);
    return INPUT_TEMPO_EVT_COUNTED;
}

uint32_t input_tempo_get_last_reported_interval(void) {
    return last_reported_tap_tempo_interval;
}

void input_tempo_set_last_reported_interval(uint32_t interval_ms) {
    last_reported_tap_tempo_interval = interval_ms;
}
