#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static uint8_t skip_probability;
static bool skip_active;
static bool skip_ramp_up;
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;

void mode_skip_init(void) {
    mode_skip_reset();
    next_step_time = 0;
}

void mode_skip_reset(void) {
    skip_active = false;
    skip_probability = 0;
    skip_ramp_up = true;
    current_step = 0;
    next_step_time = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_skip_reset_step(void) {
    current_step = 0;
}

void mode_skip_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Elastic loop: 10..100..0..100 with bounce at limits. */
    if (!skip_active) {
        skip_active = true;
        skip_probability = 10;
        skip_ramp_up = true;
    } else if (skip_ramp_up) {
        if (skip_probability >= 100) {
            skip_ramp_up = false;
            skip_probability = 90;
        } else {
            skip_probability = (uint8_t)(skip_probability + 10);
        }
    } else {
        if (skip_probability == 0) {
            skip_ramp_up = true;
            skip_probability = 10;
        } else {
            skip_probability = (uint8_t)(skip_probability - 10);
        }
    }
}

void mode_skip_set_state(bool active, uint8_t probability, bool ramp_up) {
    skip_active = active;
    skip_probability = probability;
    skip_ramp_up = ramp_up;
}

void mode_skip_get_state(bool *active, uint8_t *probability, bool *ramp_up) {
    if (active) *active = skip_active;
    if (probability) *probability = skip_probability;
    if (ramp_up) *ramp_up = skip_ramp_up;
}

void mode_skip_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_skip_reset_step();
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

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, i);
        if (((base >> current_step) & 1)) {
            bool play = true;
            if (skip_active && skip_probability > 0 &&
                (uint8_t)(rand() % 100) < skip_probability) {
                play = false;
            }
            if (play) {
                set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
            }
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
