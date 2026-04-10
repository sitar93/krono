#include "modes.h"
#include "clock_manager.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>

/**
 * Mode 21 — sequential clock: one jack per F1. Full A sweep 1A→6A, then B 1B→6B
 * (12-step cycle). CALC swapped = play that cycle backwards (6B…1B, then 6A…1A).
 */
static const jack_output_t SEQ_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t SEQ_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

#define GAMMA_SEQ_FULL_CYCLE 12u

void mode_gamma_sequential_reset_init(void) {
    mode_gamma_sequential_reset_reset();
}

void mode_gamma_sequential_reset_update(const mode_context_t *context) {
    if (!context->f1_rising_edge) {
        return;
    }

    uint32_t c = context->f1_counter;
    if (c == 0u) {
        return;
    }

    uint32_t pos = (c - 1u) % GAMMA_SEQ_FULL_CYCLE;
    if (context->calc_mode == CALC_MODE_SWAPPED) {
        pos = (GAMMA_SEQ_FULL_CYCLE - 1u) - pos;
    }

    if (pos < 6u) {
        set_output_high_for_duration(SEQ_A[pos], DEFAULT_PULSE_DURATION_MS);
    } else {
        set_output_high_for_duration(SEQ_B[pos - 6u], DEFAULT_PULSE_DURATION_MS);
    }
}

void mode_gamma_sequential_reset_reset(void) {
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_sequential_reset_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    clock_manager_restart_beat_phase_now();
}
