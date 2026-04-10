#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>

/* Same 12-step sweep as mode 21: 1A…6A then 1B…6B. */
static const jack_output_t SEQ_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t SEQ_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

#define GAMMA_SEQ_FULL_CYCLE 12u

static uint8_t seq_step;
static bool frozen;

void mode_gamma_sequential_freeze_set_state(bool frz, uint8_t step) {
    frozen = frz;
    seq_step = (step >= GAMMA_SEQ_FULL_CYCLE) ? 0u : step;
}

void mode_gamma_sequential_freeze_get_state(bool *frz, uint8_t *step) {
    if (frz) {
        *frz = frozen;
    }
    if (step) {
        *step = seq_step;
    }
}

void mode_gamma_sequential_freeze_init(void) {
    mode_gamma_sequential_freeze_reset();
}

void mode_gamma_sequential_freeze_reset(void) {
    seq_step = 0;
    frozen = false;
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_sequential_freeze_update(const mode_context_t *context) {
    if (!context->f1_rising_edge) {
        return;
    }

    uint32_t pos = (uint32_t)(seq_step % GAMMA_SEQ_FULL_CYCLE);
    if (context->calc_mode == CALC_MODE_SWAPPED) {
        pos = (GAMMA_SEQ_FULL_CYCLE - 1u) - pos;
    }

    if (pos < 6u) {
        set_output_high_for_duration(SEQ_A[pos], DEFAULT_PULSE_DURATION_MS);
    } else {
        set_output_high_for_duration(SEQ_B[pos - 6u], DEFAULT_PULSE_DURATION_MS);
    }

    if (!frozen) {
        seq_step = (uint8_t)((seq_step + 1u) % GAMMA_SEQ_FULL_CYCLE);
    }
}

void mode_gamma_sequential_freeze_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    frozen = !frozen;
}
