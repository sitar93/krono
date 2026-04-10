#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>

#define GAMMA_TRIP_NUM_PATTERNS 6u
#define GAMMA_TRIP_MAX_OUTS 4u

typedef struct {
    uint8_t n_out;
    jack_output_t outs[GAMMA_TRIP_MAX_OUTS];
} gamma_trip_step_t;

typedef struct {
    const gamma_trip_step_t *steps;
    uint8_t num_steps;
} gamma_trip_pattern_def_t;

/*
 * Mode 23 trip patterns — see README / AGENTS.md (Gamma).
 * MOD/CV cycles pattern 1→6; calc swapped reverses step order within the pattern.
 */
/* 1: inverted pairs 1A+6B … 6A+1B */
static const gamma_trip_step_t TRIP_P0[] = {
    {2u, {JACK_OUT_1A, JACK_OUT_6B}},
    {2u, {JACK_OUT_2A, JACK_OUT_5B}},
    {2u, {JACK_OUT_3A, JACK_OUT_4B}},
    {2u, {JACK_OUT_4A, JACK_OUT_3B}},
    {2u, {JACK_OUT_5A, JACK_OUT_2B}},
    {2u, {JACK_OUT_6A, JACK_OUT_1B}},
};

/* 2: bounce on same pairs — up 1…6 then down to 2 (10 steps) */
static const gamma_trip_step_t TRIP_P1[] = {
    {2u, {JACK_OUT_1A, JACK_OUT_6B}},
    {2u, {JACK_OUT_2A, JACK_OUT_5B}},
    {2u, {JACK_OUT_3A, JACK_OUT_4B}},
    {2u, {JACK_OUT_4A, JACK_OUT_3B}},
    {2u, {JACK_OUT_5A, JACK_OUT_2B}},
    {2u, {JACK_OUT_6A, JACK_OUT_1B}},
    {2u, {JACK_OUT_5A, JACK_OUT_2B}},
    {2u, {JACK_OUT_4A, JACK_OUT_3B}},
    {2u, {JACK_OUT_3A, JACK_OUT_4B}},
    {2u, {JACK_OUT_2A, JACK_OUT_5B}},
};

/* 3: stairs interleaved */
static const gamma_trip_step_t TRIP_P2[] = {
    {1u, {JACK_OUT_1A}}, {1u, {JACK_OUT_1B}}, {1u, {JACK_OUT_2A}}, {1u, {JACK_OUT_2B}},
    {1u, {JACK_OUT_3A}}, {1u, {JACK_OUT_3B}}, {1u, {JACK_OUT_4A}}, {1u, {JACK_OUT_4B}},
    {1u, {JACK_OUT_5A}}, {1u, {JACK_OUT_5B}}, {1u, {JACK_OUT_6A}}, {1u, {JACK_OUT_6B}},
};

/* 4: circle — A 1…6 then B 6…1 */
static const gamma_trip_step_t TRIP_P3[] = {
    {1u, {JACK_OUT_1A}}, {1u, {JACK_OUT_2A}}, {1u, {JACK_OUT_3A}}, {1u, {JACK_OUT_4A}},
    {1u, {JACK_OUT_5A}}, {1u, {JACK_OUT_6A}},
    {1u, {JACK_OUT_6B}}, {1u, {JACK_OUT_5B}}, {1u, {JACK_OUT_4B}}, {1u, {JACK_OUT_3B}},
    {1u, {JACK_OUT_2B}}, {1u, {JACK_OUT_1B}},
};

/* 5: convergence (explicit pairs) */
static const gamma_trip_step_t TRIP_P4[] = {
    {2u, {JACK_OUT_1A, JACK_OUT_4B}},
    {2u, {JACK_OUT_6A, JACK_OUT_3B}},
    {2u, {JACK_OUT_2A, JACK_OUT_5B}},
    {2u, {JACK_OUT_5A, JACK_OUT_2B}},
    {2u, {JACK_OUT_3A, JACK_OUT_6B}},
    {2u, {JACK_OUT_4A, JACK_OUT_1B}},
};

/* 6: groups */
static const gamma_trip_step_t TRIP_P5[] = {
    {4u, {JACK_OUT_1A, JACK_OUT_6A, JACK_OUT_3B, JACK_OUT_4B}},
    {4u, {JACK_OUT_2A, JACK_OUT_5A, JACK_OUT_2B, JACK_OUT_5B}},
    {4u, {JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_1B, JACK_OUT_6B}},
};

static const gamma_trip_pattern_def_t TRIP_DEFS[GAMMA_TRIP_NUM_PATTERNS] = {
    {TRIP_P0, 6u},
    {TRIP_P1, 10u},
    {TRIP_P2, 12u},
    {TRIP_P3, 12u},
    {TRIP_P4, 6u},
    {TRIP_P5, 3u},
};

static uint8_t pattern_id;
static uint8_t step_idx;

static uint8_t trip_effective_step_index(uint8_t raw_step, uint8_t len, bool swapped) {
    if (len == 0u) {
        return 0;
    }
    if (swapped) {
        return (uint8_t)(len - 1u - raw_step);
    }
    return raw_step;
}

void mode_gamma_sequential_trip_set_state(uint8_t pat, uint8_t step) {
    if (pat >= GAMMA_TRIP_NUM_PATTERNS) {
        pat = 0;
    }
    pattern_id = pat;
    uint8_t len = TRIP_DEFS[pattern_id].num_steps;
    if (len == 0u) {
        step_idx = 0;
        return;
    }
    if (step >= len) {
        step = 0;
    }
    step_idx = step;
}

void mode_gamma_sequential_trip_get_state(uint8_t *pat, uint8_t *step) {
    if (pat) {
        *pat = pattern_id;
    }
    if (step) {
        *step = step_idx;
    }
}

void mode_gamma_sequential_trip_init(void) {
    mode_gamma_sequential_trip_reset();
}

void mode_gamma_sequential_trip_reset(void) {
    pattern_id = 0;
    step_idx = 0;
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_sequential_trip_update(const mode_context_t *context) {
    if (!context->f1_rising_edge) {
        return;
    }

    const gamma_trip_pattern_def_t *def = &TRIP_DEFS[pattern_id];
    uint8_t len = def->num_steps;
    if (len == 0u) {
        return;
    }

    bool swapped = (context->calc_mode == CALC_MODE_SWAPPED);
    uint8_t ei = trip_effective_step_index(step_idx, len, swapped);
    const gamma_trip_step_t *st = &def->steps[ei];

    for (uint8_t k = 0; k < st->n_out && k < GAMMA_TRIP_MAX_OUTS; k++) {
        set_output_high_for_duration(st->outs[k], DEFAULT_PULSE_DURATION_MS);
    }

    step_idx = (uint8_t)((step_idx + 1u) % len);
}

void mode_gamma_sequential_trip_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    pattern_id = (uint8_t)((pattern_id + 1u) % GAMMA_TRIP_NUM_PATTERNS);
    step_idx = 0;
}
