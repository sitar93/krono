#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>
#include <stdint.h>

#define BOUNCE_CH 12u
#define BOUNCE_PULSES 6u

static const jack_output_t BOUNCE_JACK[BOUNCE_CH] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A,
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

/** Per-output absolute fire times from scene t0 (pulse 0 at t0). */
static uint32_t bnc_abs[BOUNCE_CH][BOUNCE_PULSES];
static uint8_t bnc_next[BOUNCE_CH]; /* next pulse index to fire (1..5); 6 = done */
static bool bounce_active;

static uint32_t lerp_u32(uint32_t a, uint32_t b, uint8_t step, uint8_t max_step) {
    if (max_step == 0u) {
        return a;
    }
    int64_t va = (int64_t)a;
    int64_t vb = (int64_t)b;
    int64_t out = va + (vb - va) * (int64_t)step / (int64_t)max_step;
    if (out < 0) {
        return 0u;
    }
    return (uint32_t)out;
}

/** Row 0 = 1A: gaps 1, 3/4, 1/2, 1/3, 1/4 s. Row 5 = 6A: fastest train. */
static void bounce_accel_gaps_ms(uint8_t row, uint32_t *g5) {
    static const uint32_t g1a[5] = { 1000u, 750u, 500u, 333u, 250u };
    static const uint32_t g2a[5] = { 750u, 604u, 458u, 313u, 167u };
    static const uint32_t g6a[5] = { 180u, 140u, 100u, 70u, 45u };
    for (int k = 0; k < 5; k++) {
        uint32_t t1 = g1a[k];
        uint32_t t2 = g2a[k];
        uint32_t t6 = g6a[k];
        if (row <= 1u) {
            g5[k] = (row == 0u) ? t1 : t2;
        } else {
            g5[k] = lerp_u32(t2, t6, (uint8_t)(row - 1u), 4u);
        }
    }
}

/** Row 0 = 1B: gaps 1, 4/3, 3/2, 2, 5/2 s. Row 5 = 6B: longest decel. */
static void bounce_decel_gaps_ms(uint8_t row, uint32_t *g5) {
    static const uint32_t g1b[5] = { 1000u, 1333u, 1500u, 2000u, 2500u };
    static const uint32_t g6b[5] = { 1800u, 2400u, 2700u, 3600u, 4500u };
    for (int k = 0; k < 5; k++) {
        g5[k] = lerp_u32(g1b[k], g6b[k], row, 5u);
    }
}

static void bounce_build_row(uint8_t idx, uint32_t t0) {
    uint32_t gaps[5];
    uint32_t t = t0;

    bnc_abs[idx][0] = t0;
    if (idx < 6u) {
        bounce_accel_gaps_ms(idx, gaps);
    } else {
        bounce_decel_gaps_ms((uint8_t)(idx - 6u), gaps);
    }
    for (int k = 0; k < 5; k++) {
        t += gaps[k];
        bnc_abs[idx][(uint8_t)(k + 1)] = t;
    }
}

static void bounce_arm(uint32_t t0) {
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        bounce_build_row(i, t0);
        bnc_next[i] = 1u;
    }
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        set_output_high_for_duration(BOUNCE_JACK[i], DEFAULT_PULSE_DURATION_MS);
    }
    bounce_active = true;
}

void mode_gamma_sequential_bounce_init(void) {
    mode_gamma_sequential_bounce_reset();
}

void mode_gamma_sequential_bounce_update(const mode_context_t *context) {
    (void)context;
    if (!bounce_active) {
        return;
    }
    uint32_t now = millis();
    bool any = false;
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        if (bnc_next[i] >= BOUNCE_PULSES) {
            continue;
        }
        any = true;
        if (now >= bnc_abs[i][bnc_next[i]]) {
            set_output_high_for_duration(BOUNCE_JACK[i], DEFAULT_PULSE_DURATION_MS);
            bnc_next[i]++;
        }
    }
    if (!any) {
        bounce_active = false;
    }
}

void mode_gamma_sequential_bounce_reset(void) {
    bounce_active = false;
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        bnc_next[i] = BOUNCE_PULSES;
    }
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_sequential_bounce_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    bounce_arm(millis());
}
