#include "input_tempo.h"

#include "drivers/tap.h"
#include "main_constants.h"

#include <stdbool.h>
#include <stdint.h>

static input_tempo_emit_callback_t tempo_emit_cb = 0;
static uint32_t last_reported_tap_tempo_interval = 0;

static uint32_t quad_gaps_ms[TAP_TEMPO_AVG_INTERVALS];
/** How many gaps stored toward the next boundary (0..3). */
static uint8_t gaps_stored = 0;
/** After a boundary pulse, next tap's interval (carry-in) is ignored. */
static bool await_quad_start = false;
static uint32_t last_pattern_tap_ms = 0;

/** false: next boundary is leading (tap 4, 12, …); true: next is trailing (8, 16, …). */
static bool next_boundary_is_trailing = false;
static uint32_t leading_boundary_median_ms = 0;
static uint32_t leading_boundary_press_ms = 0;
static bool leading_boundary_valid = false;

static uint32_t median_of_u32_3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t x = a;
    uint32_t y = b;
    uint32_t z = c;
    if (x > y) {
        uint32_t t = x;
        x = y;
        y = t;
    }
    if (y > z) {
        uint32_t t = y;
        y = z;
        z = t;
    }
    if (x > y) {
        uint32_t t = x;
        x = y;
        y = t;
    }
    return y;
}

static uint32_t clamp_interval(uint32_t ms) {
    if (ms < MIN_INTERVAL) {
        return MIN_INTERVAL;
    }
    if (ms > MAX_INTERVAL) {
        return MAX_INTERVAL;
    }
    return ms;
}

static void pattern_reset(void) {
    gaps_stored = 0;
    await_quad_start = false;
    last_pattern_tap_ms = 0;
    next_boundary_is_trailing = false;
    leading_boundary_valid = false;
    for (int i = 0; i < TAP_TEMPO_AVG_INTERVALS; i++) {
        quad_gaps_ms[i] = 0;
    }
}

void input_tempo_init(input_tempo_emit_callback_t emit_cb) {
    tempo_emit_cb = emit_cb;
    input_tempo_reset_calculation();
}

void input_tempo_reset_calculation(void) {
    pattern_reset();
    last_reported_tap_tempo_interval = 0;
}

static void emit_quadruple_boundary(uint32_t interval_ms, uint32_t press_time_ms) {
    if (!tempo_emit_cb || interval_ms == 0) {
        return;
    }
    tempo_emit_cb(interval_ms, false, press_time_ms, true);
    last_reported_tap_tempo_interval = interval_ms;
}

void input_tempo_handle_tap_event(void) {
    if (!tap_detected()) {
        return;
    }

    uint32_t press_ms = tap_get_last_press_time_ms();
    uint32_t interval = tap_get_interval();

    if (last_pattern_tap_ms != 0u && (press_ms - last_pattern_tap_ms) > TAP_PATTERN_IDLE_RESET_MS) {
        pattern_reset();
        tap_abort_capture();
    }

    last_pattern_tap_ms = press_ms;

    /* First physical edge of a session: no interval yet (tap.c). */
    if (interval == 0u) {
        return;
    }

    if (interval < MIN_INTERVAL || interval > MAX_INTERVAL) {
        pattern_reset();
        tap_abort_capture();
        return;
    }

    if (await_quad_start) {
        await_quad_start = false;
        return;
    }

    if (gaps_stored < TAP_TEMPO_AVG_INTERVALS) {
        quad_gaps_ms[gaps_stored++] = interval;
    }

    if (gaps_stored < TAP_TEMPO_AVG_INTERVALS) {
        return;
    }

    uint32_t med = median_of_u32_3(quad_gaps_ms[0], quad_gaps_ms[1], quad_gaps_ms[2]);
    med = clamp_interval(med);

    if (!next_boundary_is_trailing) {
        emit_quadruple_boundary(med, press_ms);
        leading_boundary_median_ms = med;
        leading_boundary_press_ms = press_ms;
        leading_boundary_valid = true;
        next_boundary_is_trailing = true;
    } else {
        uint32_t out = med;
        if (leading_boundary_valid &&
            (press_ms - leading_boundary_press_ms) <= TAP_QUAD_BLEND_WINDOW_MS) {
            uint32_t a = leading_boundary_median_ms;
            uint32_t b = med;
            out = (TAP_QUAD_BLEND_LEADING_NUM * a + TAP_QUAD_BLEND_TRAILING_NUM * b + TAP_QUAD_BLEND_DENOM / 2u) /
                  TAP_QUAD_BLEND_DENOM;
            out = clamp_interval(out);
        }
        emit_quadruple_boundary(out, press_ms);
        leading_boundary_valid = false;
        next_boundary_is_trailing = false;
    }

    gaps_stored = 0;
    await_quad_start = true;
}

uint32_t input_tempo_get_last_reported_interval(void) {
    return last_reported_tap_tempo_interval;
}

void input_tempo_set_last_reported_interval(uint32_t interval_ms) {
    last_reported_tap_tempo_interval = interval_ms;
}
