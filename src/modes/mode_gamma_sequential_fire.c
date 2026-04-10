#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>

static const jack_output_t FIRE_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t FIRE_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

/**
 * -1 idle.
 * MOD: 1A + 6B together.
 * 0..4: next F1 rising fires paired (2A+5B), (3A+4B), … (6A+1B) — A steps up, B steps down.
 */
static int8_t fire_step = -1;

void mode_gamma_sequential_fire_init(void) {
    mode_gamma_sequential_fire_reset();
}

void mode_gamma_sequential_fire_update(const mode_context_t *context) {
    if (fire_step < 0 || !context->f1_rising_edge) {
        return;
    }

    if (fire_step <= 4) {
        uint8_t ai = (uint8_t)fire_step + 1u; /* 1..5 → 2A..6A */
        uint8_t bi = (uint8_t)(4u - (unsigned)fire_step); /* 4..0 → 5B..1B */
        set_output_high_for_duration(FIRE_A[ai], DEFAULT_PULSE_DURATION_MS);
        set_output_high_for_duration(FIRE_B[bi], DEFAULT_PULSE_DURATION_MS);
        fire_step++;
        if (fire_step > 4) {
            fire_step = -1;
        }
    }
}

void mode_gamma_sequential_fire_reset(void) {
    fire_step = -1;
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_sequential_fire_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    set_output_high_for_duration(FIRE_A[0], DEFAULT_PULSE_DURATION_MS);
    set_output_high_for_duration(FIRE_B[5], DEFAULT_PULSE_DURATION_MS);
    fire_step = 0;
}
