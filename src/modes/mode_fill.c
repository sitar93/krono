#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static uint8_t fill_density;
static bool fill_ramp_up;
static uint16_t fill_mask[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;

static void regenerate_fill_mask(void) {
    /* Map 0..50 to 6 strong stages so each MOD click is clearly audible. */
    static const uint8_t keep_probs[6] = { 0, 8, 22, 40, 62, 85 };
    static const uint8_t add_probs[6]  = { 0, 4, 12, 24, 38, 56 };
    uint8_t stage = (uint8_t)(fill_density / 10u);
    if (stage > 5u) {
        stage = 5u;
    }

    for (int oi = 0; oi < MODE_RHYTHM_NUM_OUTPUTS; oi++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, oi);
        uint16_t m = 0;
        /* Keep kicks always present; progressively fade-in and enrich other channels. */
        if (oi <= 1) {
            m = base;
        } else if (stage > 0) {
            uint8_t keep_prob = keep_probs[stage];
            uint8_t add_prob = add_probs[stage];
            for (int b = 0; b < 16; b++) {
                bool base_on = ((base >> b) & 1) != 0;
                if (base_on) {
                    if ((uint8_t)(rand() % 100) < keep_prob) {
                        m |= (uint16_t)(1u << b);
                    }
                } else if ((uint8_t)(rand() % 100) < add_prob) {
                    m |= (uint16_t)(1u << b);
                }
            }
        }
        fill_mask[oi] = m;
    }
}

void mode_fill_init(void) {
    mode_fill_reset();
    next_step_time = 0;
}

void mode_fill_reset(void) {
    fill_density = 0;
    fill_ramp_up = true;
    current_step = 0;
    next_step_time = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        fill_mask[i] = 0;
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_fill_reset_step(void) {
    current_step = 0;
}

void mode_fill_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Drastic loop: 0..50 then immediate reset to 0. */
    (void)fill_ramp_up;
    if (fill_density >= 50) {
        fill_density = 0;
    } else {
        fill_density = (uint8_t)(fill_density + 10);
    }
    fill_ramp_up = true;
}

void mode_fill_set_state(uint8_t density, bool ramp_up) {
    fill_density = density;
    (void)ramp_up;
    fill_ramp_up = true;
}

void mode_fill_get_state(uint8_t *density, bool *ramp_up) {
    if (density) *density = fill_density;
    if (ramp_up) *ramp_up = fill_ramp_up;
}

void mode_fill_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_fill_reset_step();
    }
    if (context->calc_mode_changed) {
        regenerate_fill_mask();
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

    if (current_step == 0) {
        regenerate_fill_mask();
    }

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, i);
        uint16_t extra = fill_mask[i];
        if (((base >> current_step) & 1) || ((extra >> current_step) & 1)) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
