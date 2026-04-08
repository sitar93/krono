#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static uint16_t density_patterns[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t density_pct;
static uint8_t current_step;
static uint32_t next_step_time;
static bool pending_recalc;
static calculation_mode_t s_calc;

static void recalc_density_patterns(void) {
    for (int oi = 0; oi < MODE_RHYTHM_NUM_OUTPUTS; oi++) {
        uint16_t base = mode_rhythm_base_pattern(s_calc, oi);
        uint16_t p = base;
        if (density_pct < 100) {
            uint8_t rem = (uint8_t)(100 - density_pct);
            for (int b = 0; b < 16; b++) {
                if ((p >> b) & 1) {
                    if ((uint8_t)(rand() % 100) < rem) {
                        p &= (uint16_t)~(1u << b);
                    }
                }
            }
        } else if (density_pct > 100) {
            uint8_t add = (uint8_t)((density_pct > 200) ? 100 : (density_pct - 100));
            for (int b = 0; b < 16; b++) {
                if (((p >> b) & 1) == 0) {
                    if ((uint8_t)(rand() % 100) < add) {
                        p |= (uint16_t)(1u << b);
                    }
                }
            }
        } else {
            p = base;
        }
        density_patterns[oi] = p;
    }
    pending_recalc = false;
}

void mode_density_init(void) {
    mode_density_reset();
    next_step_time = 0;
}

void mode_density_reset(void) {
    density_pct = 100;
    current_step = 0;
    next_step_time = 0;
    pending_recalc = true;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        density_patterns[i] = 0;
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_density_reset_step(void) {
    current_step = 0;
}

void mode_density_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Drastic loop: 0..200 then immediate reset to 0. */
    if (density_pct >= 200) {
        density_pct = 0;
    } else {
        density_pct = (uint8_t)(density_pct + 10);
    }
    pending_recalc = true;
}

void mode_density_set_state(uint8_t pct, bool ramp_up) {
    density_pct = pct;
    (void)ramp_up;
    pending_recalc = true;
}

void mode_density_get_state(uint8_t *pct, bool *ramp_up) {
    if (pct) *pct = density_pct;
    if (ramp_up) *ramp_up = true;
}

void mode_density_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_density_reset_step();
    }
    if (context->calc_mode_changed) {
        pending_recalc = true;
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

    if (current_step == 0 && pending_recalc) {
        recalc_density_patterns();
    }

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        uint16_t pattern = density_patterns[i];
        if ((pattern >> current_step) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
