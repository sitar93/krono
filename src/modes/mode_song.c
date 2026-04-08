#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdint.h>
#include <stdbool.h>

static uint16_t generated_patterns[MODE_RHYTHM_NUM_OUTPUTS];
static uint16_t variation_patterns[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t song_step;
static uint8_t current_step;
static uint32_t next_step_time;
static bool variation_pending;
static uint32_t variation_seed;
static uint32_t song_mod_nonce;
static calculation_mode_t s_calc;

#define SONG_DEFAULT_SEED 0xC0FFEE01u

static int popcount16(uint16_t x) {
    int c = 0;
    for (int i = 0; i < 16; i++) {
        if ((x >> i) & 1) {
            c++;
        }
    }
    return c;
}

static uint32_t song_lcg(uint32_t *st) {
    *st = *st * 1103515245u + 12345u;
    return *st;
}

static uint16_t generate_track(uint32_t *rng, uint16_t base) {
    int bd = popcount16(base);
    uint32_t r = song_lcg(rng);
    /* Wider swing so each regenerated base loop is clearly different. */
    int delta = (int)(r % 81u) - 40;
    int target = bd + (bd * delta) / 100;
    if (target < 0) {
        target = 0;
    }
    if (target > 16) {
        target = 16;
    }

    uint16_t p = 0;
    int positions[16];
    for (int i = 0; i < 16; i++) {
        positions[i] = i;
    }
    for (int i = 15; i > 0; i--) {
        uint32_t rv = song_lcg(rng);
        int j = (int)(rv % (uint32_t)(i + 1));
        int tmp = positions[i];
        positions[i] = positions[j];
        positions[j] = tmp;
    }
    for (int k = 0; k < target; k++) {
        p |= (uint16_t)(1u << positions[k]);
    }
    return p;
}

static void song_regenerate(uint32_t seed) {
    uint32_t rng = seed;
    for (int oi = 0; oi < MODE_RHYTHM_NUM_OUTPUTS; oi++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, oi);
        generated_patterns[oi] = generate_track(&rng, base);
        variation_patterns[oi] = generate_track(&rng, generated_patterns[oi]);
    }
}

void mode_song_init(void) {
    mode_song_reset();
    next_step_time = 0;
}

void mode_song_reset(void) {
    song_step = 0;
    current_step = 0;
    next_step_time = 0;
    variation_pending = false;
    variation_seed = SONG_DEFAULT_SEED;
    song_mod_nonce = 0;
    s_calc = CALC_MODE_NORMAL;
    song_regenerate(variation_seed);
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_song_reset_step(void) {
    current_step = 0;
    song_step = 0;
}

void mode_song_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    /* Each MOD press schedules a brand-new random base loop. */
    song_mod_nonce++;
    variation_seed = ts_ms ^ (song_mod_nonce * 2654435761u) ^ 0xA5C3E91Du;
    variation_pending = true;
}

void mode_song_set_state(uint32_t seed, bool pending) {
    variation_seed = seed;
    variation_pending = pending;
}

void mode_song_get_state(uint32_t *seed, bool *pending) {
    if (seed) *seed = variation_seed;
    if (pending) *pending = variation_pending;
}

void mode_song_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_song_reset_step();
    }
    if (context->calc_mode_changed) {
        song_regenerate(variation_seed);
    }

    uint32_t now = context->current_time_ms;
    uint32_t step_interval = context->current_tempo_interval_ms / 4;
    if (step_interval < 5) {
        step_interval = 5;
    }

    if (next_step_time == 0) {
        next_step_time = now;
    }

    if (now < next_step_time) {
        return;
    }

    next_step_time += step_interval;
    if (next_step_time < now) {
        next_step_time = now + step_interval;
    }

    uint8_t bit_pos = (uint8_t)(song_step % 16u);

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t pat = (song_step < 96) ? generated_patterns[i] : variation_patterns[i];
        if ((pat >> bit_pos) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    song_step++;
    if (song_step >= 128) {
        song_step = 0;
        if (variation_pending) {
            song_regenerate(variation_seed);
            variation_pending = false;
        }
    }

    current_step = (uint8_t)(song_step % 16u);
}
