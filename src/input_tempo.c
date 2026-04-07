#include "input_tempo.h"

#include "drivers/tap.h"
#include "main_constants.h"

#include <stdint.h>

static input_tempo_emit_callback_t tempo_emit_cb = 0;
static uint32_t tap_intervals[TAP_LOCK_INTERVAL_SAMPLES];
static uint8_t tap_interval_index = 0;
static uint32_t last_reported_tap_tempo_interval = 0;
static bool tap_tempo_locked = false;
static uint32_t refined_interval_ms = 0;

void input_tempo_init(input_tempo_emit_callback_t emit_cb) {
    tempo_emit_cb = emit_cb;
    input_tempo_reset_calculation();
}

void input_tempo_reset_calculation(void) {
    tap_interval_index = 0;
    tap_tempo_locked = false;
    refined_interval_ms = 0;
    last_reported_tap_tempo_interval = 0;
    for (int i = 0; i < TAP_LOCK_INTERVAL_SAMPLES; i++) {
        tap_intervals[i] = 0;
    }
}

static void sort_u32_3(uint32_t *a, uint32_t *b, uint32_t *c) {
    if (*a > *b) {
        uint32_t t = *a;
        *a = *b;
        *b = t;
    }
    if (*b > *c) {
        uint32_t t = *b;
        *b = *c;
        *c = t;
    }
    if (*a > *b) {
        uint32_t t = *a;
        *a = *b;
        *b = t;
    }
}

/** @return true if spread is acceptable; *median_out is middle of sorted triple. */
static bool lock_triple_coherent(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t *median_out) {
    uint32_t a = i0, b = i1, c = i2;
    sort_u32_3(&a, &b, &c);
    uint32_t spread = c - a;
    uint32_t med = b;
    uint32_t allow = (med * TAP_INTERVAL_SPREAD_PERCENT) / 100;
    if (allow < TAP_INTERVAL_SPREAD_MIN_MS) {
        allow = TAP_INTERVAL_SPREAD_MIN_MS;
    }
    if (spread > allow) {
        return false;
    }
    if (spread > MAX_INTERVAL_DIFFERENCE) {
        return false;
    }
    *median_out = med;
    return true;
}

static void emit_tap_tempo(uint32_t interval_ms, uint32_t press_time_ms) {
    if (!tempo_emit_cb || interval_ms == 0) {
        return;
    }
    tempo_emit_cb(interval_ms, false, press_time_ms);
    last_reported_tap_tempo_interval = interval_ms;
}

void input_tempo_handle_tap_event(void) {
    if (!tap_detected()) {
        return;
    }

    uint32_t press_ms = tap_get_last_press_time_ms();
    uint32_t interval = tap_get_interval();

    if (interval < MIN_INTERVAL || interval > MAX_INTERVAL) {
        input_tempo_reset_calculation();
        return;
    }

    if (!tap_tempo_locked) {
        tap_intervals[tap_interval_index++] = interval;
        if (tap_interval_index < TAP_LOCK_INTERVAL_SAMPLES) {
            return;
        }

        uint32_t med = 0;
        if (!lock_triple_coherent(tap_intervals[0], tap_intervals[1], tap_intervals[2], &med)) {
            input_tempo_reset_calculation();
            return;
        }

        tap_tempo_locked = true;
        refined_interval_ms = med;
        tap_interval_index = 0;
        emit_tap_tempo(refined_interval_ms, press_ms);
        return;
    }

    // Locked: refine BPM with EMA; always re-sync phase to this tap (follow the drummer).
    uint32_t dev = interval > refined_interval_ms ? interval - refined_interval_ms
                                                  : refined_interval_ms - interval;
    uint32_t thresh = (refined_interval_ms * TAP_REFINE_OUTLIER_FRAC_NUM) / TAP_REFINE_OUTLIER_FRAC_DEN;
    if (thresh < 1) {
        thresh = 1;
    }

    if (dev > thresh) {
        emit_tap_tempo(refined_interval_ms, press_ms);
        return;
    }

    {
        int32_t delta = (int32_t)interval - (int32_t)refined_interval_ms;
        int32_t adj = delta / (int32_t)TAP_REFINE_EMA_DIVISOR;
        int32_t next = (int32_t)refined_interval_ms + adj;
        if (next < (int32_t)MIN_INTERVAL) {
            next = (int32_t)MIN_INTERVAL;
        }
        if (next > (int32_t)MAX_INTERVAL) {
            next = (int32_t)MAX_INTERVAL;
        }
        refined_interval_ms = (uint32_t)next;
    }

    emit_tap_tempo(refined_interval_ms, press_ms);
}

uint32_t input_tempo_get_last_reported_interval(void) {
    return last_reported_tap_tempo_interval;
}

void input_tempo_set_last_reported_interval(uint32_t interval_ms) {
    last_reported_tap_tempo_interval = interval_ms;
}
