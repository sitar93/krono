#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static const jack_output_t COIN_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t COIN_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};
/** Percent chance for A; B is the complement. */
static const uint8_t COIN_P_A[6] = { 85u, 75u, 65u, 50u, 25u, 15u };

static bool coin_invert_latch;

void mode_gamma_coin_toss_set_state(bool invert) {
    coin_invert_latch = invert;
}

void mode_gamma_coin_toss_get_state(bool *invert) {
    if (invert != NULL) {
        *invert = coin_invert_latch;
    }
}

void mode_gamma_coin_toss_init(void) {
    mode_gamma_coin_toss_reset();
}

void mode_gamma_coin_toss_reset(void) {
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

void mode_gamma_coin_toss_update(const mode_context_t *context) {
    if (!context->f1_rising_edge) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        uint8_t p_a = COIN_P_A[i];
        if (coin_invert_latch) {
            p_a = (uint8_t)(100u - (unsigned)p_a);
        }
        uint8_t r = (uint8_t)(rand() % 100);
        if (r < p_a) {
            set_output_high_for_duration(COIN_A[i], DEFAULT_PULSE_DURATION_MS);
        } else {
            set_output_high_for_duration(COIN_B[i], DEFAULT_PULSE_DURATION_MS);
        }
    }
}

void mode_gamma_coin_toss_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    coin_invert_latch = !coin_invert_latch;
}
