#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static uint16_t patterns_a[MODE_RHYTHM_NUM_OUTPUTS];
static uint16_t patterns_b[MODE_RHYTHM_NUM_OUTPUTS];
static uint16_t morphed_patterns[MODE_RHYTHM_NUM_OUTPUTS];
static bool morph_frozen;
static uint8_t current_step;
static uint32_t next_step_time;
static uint32_t morph_generation;
static calculation_mode_t s_calc;

static uint32_t morph_step_rng(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static void load_base_from_calc(void) {
    calculation_mode_t primary = s_calc;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        patterns_a[i] = mode_rhythm_base_pattern(primary, i);
        patterns_b[i] = patterns_a[i];
        morphed_patterns[i] = patterns_a[i];
    }
}

static void morph_generate_next(void) {
    static const uint32_t primes[MODE_RHYTHM_NUM_OUTPUTS] = {
        2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u, 23u, 29u
    };
    for (int oi = 0; oi < MODE_RHYTHM_NUM_OUTPUTS; oi++) {
        uint16_t prev = morphed_patterns[oi];
        uint16_t base = patterns_a[oi];
        uint32_t seed = (morph_generation + 1u) * 2654435761u;
        seed ^= primes[oi] * 40503u;
        seed ^= (uint32_t)(prev << (oi % 7));
        uint32_t r = morph_step_rng(seed);

        int flips = 1 + (int)(r % 3u);
        uint16_t next = prev;
        for (int k = 0; k < flips; k++) {
            r = morph_step_rng(r + (uint32_t)(k * 17 + oi));
            uint8_t bit = (uint8_t)(r & 0x0Fu);
            next ^= (uint16_t)(1u << bit);
        }

        /* Pull slightly toward base to keep coherent musical identity. */
        r = morph_step_rng(r ^ 0x9E3779B9u);
        uint8_t anchor = (uint8_t)(r & 0x0Fu);
        if ((base >> anchor) & 1u) {
            next |= (uint16_t)(1u << anchor);
        } else {
            next &= (uint16_t)~(1u << anchor);
        }

        patterns_b[oi] = next;
        morphed_patterns[oi] = next;
    }
    morph_generation++;
}

void mode_morph_init(void) {
    mode_morph_reset();
    next_step_time = 0;
}

void mode_morph_reset(void) {
    morph_frozen = false;
    current_step = 0;
    next_step_time = 0;
    morph_generation = 0;
    s_calc = CALC_MODE_NORMAL;
    load_base_from_calc();
    morph_generate_next();
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_morph_reset_step(void) {
    current_step = 0;
}

void mode_morph_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Freeze current value; next press resumes and advances to the next generated state. */
    if (morph_frozen) {
        morph_frozen = false;
        morph_generate_next();
    } else {
        morph_frozen = true;
    }
}

void mode_morph_set_state(bool frozen, uint32_t generation, const uint16_t *morphed) {
    morph_frozen = frozen;
    morph_generation = generation;
    if (morphed) {
        memcpy(morphed_patterns, morphed, sizeof morphed_patterns);
        memcpy(patterns_b, morphed, sizeof patterns_b);
    }
}

void mode_morph_get_state(bool *frozen, uint32_t *generation, uint16_t *morphed) {
    if (frozen) {
        *frozen = morph_frozen;
    }
    if (generation) {
        *generation = morph_generation;
    }
    if (morphed) {
        memcpy(morphed, morphed_patterns, sizeof morphed_patterns);
    }
}

void mode_morph_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_morph_reset_step();
    }
    if (context->calc_mode_changed) {
        load_base_from_calc();
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

    if (!morph_frozen && current_step == 0) {
        morph_generate_next();
    }

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t pattern = morphed_patterns[i];
        if ((pattern >> current_step) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
