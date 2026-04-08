#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static uint16_t drifted_patterns[MODE_RHYTHM_NUM_OUTPUTS];
static bool drift_active;
static uint8_t drift_probability;
static bool drift_ramp_up;
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;

static void reload_base_patterns(void) {
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        drifted_patterns[i] = mode_rhythm_base_pattern(s_calc, i);
    }
}

void mode_drift_init(void) {
    mode_drift_reset();
    next_step_time = 0;
}

void mode_drift_reset(void) {
    drift_active = false;
    drift_probability = 0;
    drift_ramp_up = true;
    current_step = 0;
    next_step_time = 0;
    reload_base_patterns();
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_drift_reset_step(void) {
    current_step = 0;
}

void mode_drift_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Elastic loop: 10..100..0..100 with bounce at limits. */
    if (!drift_active) {
        drift_active = true;
        drift_probability = 10;
        drift_ramp_up = true;
    } else if (drift_ramp_up) {
        if (drift_probability >= 100) {
            drift_ramp_up = false;
            drift_probability = 90;
        } else {
            drift_probability = (uint8_t)(drift_probability + 10);
        }
    } else {
        if (drift_probability == 0) {
            drift_ramp_up = true;
            drift_probability = 10;
        } else {
            drift_probability = (uint8_t)(drift_probability - 10);
        }
    }
}

void mode_drift_set_state(bool active, uint8_t probability, bool ramp_up) {
    drift_active = active;
    drift_probability = probability;
    drift_ramp_up = ramp_up;
}

void mode_drift_get_state(bool *active, uint8_t *probability, bool *ramp_up) {
    if (active) *active = drift_active;
    if (probability) *probability = drift_probability;
    if (ramp_up) *ramp_up = drift_ramp_up;
}

void mode_drift_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_drift_reset_step();
    }
    if (context->calc_mode_changed) {
        reload_base_patterns();
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

    if (current_step == 0 && drift_active && drift_probability > 0) {
        /* Increase unpredictability with multiple micro-mutations per bar. */
        uint8_t mutations = (uint8_t)(1u + (rand() % 3));
        for (uint8_t m = 0; m < mutations; m++) {
            if ((uint8_t)(rand() % 100) < drift_probability) {
                int oi = rand() % MODE_RHYTHM_NUM_OUTPUTS;
                int bi = rand() % 16;
                drifted_patterns[oi] ^= (uint16_t)(1u << bi);
            }
        }
        /* Rare larger jump for non-linear evolution at higher drift values. */
        if ((uint8_t)(rand() % 100) < (uint8_t)(drift_probability / 2u)) {
            int oi = rand() % MODE_RHYTHM_NUM_OUTPUTS;
            int bj = rand() % 16;
            int bk = rand() % 16;
            drifted_patterns[oi] ^= (uint16_t)((1u << bj) | (1u << bk));
        }
    }

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t pattern = drifted_patterns[i];
        if ((pattern >> current_step) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
