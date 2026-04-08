#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static bool stutter_active;
static uint8_t stutter_length;
static bool stutter_ramp_up;
static uint16_t stutter_variation_mask[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t seq_pos;
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;

void mode_stutter_init(void) {
    mode_stutter_reset();
    next_step_time = 0;
}

void mode_stutter_reset(void) {
    stutter_active = false;
    stutter_length = 2;
    stutter_ramp_up = true;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        stutter_variation_mask[i] = 0;
    }
    seq_pos = 0;
    current_step = 0;
    next_step_time = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_stutter_reset_step(void) {
    current_step = 0;
    seq_pos = 0;
}

void mode_stutter_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Drastic loop: 2->4->8->2... (no descending/off cycle). */
    if (!stutter_active) {
        stutter_active = true;
        stutter_length = 2;
    } else if (stutter_length == 2) {
        stutter_length = 4;
    } else if (stutter_length == 4) {
        stutter_length = 8;
    } else {
        stutter_length = 2;
    }
    stutter_ramp_up = true;
}

void mode_stutter_set_state(bool active, uint8_t length, bool ramp_up, const uint16_t *variation_mask) {
    stutter_active = active;
    stutter_length = length;
    stutter_ramp_up = ramp_up;
    if (variation_mask) {
        memcpy(stutter_variation_mask, variation_mask, sizeof stutter_variation_mask);
    } else {
        memset(stutter_variation_mask, 0, sizeof stutter_variation_mask);
    }
    if (!stutter_active) {
        seq_pos = 0;
    }
}

void mode_stutter_get_state(bool *active, uint8_t *length, bool *ramp_up, uint16_t *variation_mask) {
    if (active) *active = stutter_active;
    if (length) *length = stutter_length;
    if (ramp_up) *ramp_up = stutter_ramp_up;
    if (variation_mask) {
        memcpy(variation_mask, stutter_variation_mask, sizeof stutter_variation_mask);
    }
}

void mode_stutter_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_stutter_reset_step();
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

    uint8_t bit_pos = stutter_active ? seq_pos : current_step;

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, i) ^ stutter_variation_mask[i];
        if ((base >> bit_pos) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    if (stutter_active && stutter_length >= 2) {
        seq_pos++;
        if (seq_pos >= stutter_length) {
            seq_pos = 0;
            /* After each full stutter cycle, nudge pattern for evolving feel. */
            int oi = rand() % MODE_RHYTHM_NUM_OUTPUTS;
            int b1 = rand() % 16;
            int b2 = rand() % 16;
            stutter_variation_mask[oi] ^= (uint16_t)((1u << b1) | (1u << b2));
        }
    } else {
        current_step++;
        if (current_step >= 16) {
            current_step = 0;
        }
    }
}
