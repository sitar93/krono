#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static bool muted[MODE_RHYTHM_NUM_OUTPUTS];
static uint16_t variation_mask[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t mute_count;
static bool mute_ramp_up;
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;

static int pick_random_index_by_state(bool want_muted) {
    uint8_t count = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        if (muted[i] == want_muted) {
            count++;
        }
    }
    if (count == 0) {
        return -1;
    }

    uint8_t pick = (uint8_t)(rand() % count);
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        if (muted[i] == want_muted) {
            if (pick == 0) {
                return i;
            }
            pick--;
        }
    }
    return -1;
}

void mode_mute_init(void) {
    mode_mute_reset();
    next_step_time = 0;
}

void mode_mute_reset(void) {
    memset(muted, 0, sizeof muted);
    memset(variation_mask, 0, sizeof variation_mask);
    mute_count = 0;
    mute_ramp_up = true;
    current_step = 0;
    next_step_time = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_mute_reset_step(void) {
    current_step = 0;
}

void mode_mute_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* Elastic loop with random channels: 0..N muted, then N..0 unmuted. */
    if (mute_ramp_up) {
        int idx = pick_random_index_by_state(false);
        if (idx >= 0) {
            muted[idx] = true;
            mute_count++;
        }
        if (mute_count >= MODE_RHYTHM_NUM_OUTPUTS) {
            mute_ramp_up = false;
        }
    } else {
        int idx = pick_random_index_by_state(true);
        if (idx >= 0) {
            muted[idx] = false;
            /* On unmute, nudge the pattern so each re-entry is slightly different. */
            uint8_t b1 = (uint8_t)(rand() % 16);
            uint8_t b2 = (uint8_t)(rand() % 16);
            variation_mask[idx] ^= (uint16_t)((1u << b1) | (1u << b2));
            if (mute_count > 0) {
                mute_count--;
            }
        }
        if (mute_count == 0) {
            mute_ramp_up = true;
        }
    }
}

void mode_mute_set_state(uint16_t muted_mask, uint8_t count, bool ramp_up, const uint16_t *variation) {
    uint8_t actual = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        muted[i] = ((muted_mask >> i) & 1u) != 0;
        if (muted[i]) {
            actual++;
        }
    }
    mute_count = actual;
    if (count < mute_count) {
        mute_count = count;
    }
    mute_ramp_up = ramp_up;
    if (variation) {
        memcpy(variation_mask, variation, sizeof variation_mask);
    } else {
        memset(variation_mask, 0, sizeof variation_mask);
    }
}

void mode_mute_get_state(uint16_t *muted_mask, uint8_t *count, bool *ramp_up, uint16_t *variation) {
    if (muted_mask) {
        uint16_t m = 0;
        for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
            if (muted[i]) {
                m |= (uint16_t)(1u << i);
            }
        }
        *muted_mask = m;
    }
    if (count) *count = mute_count;
    if (ramp_up) *ramp_up = mute_ramp_up;
    if (variation) {
        memcpy(variation, variation_mask, sizeof variation_mask);
    }
}

void mode_mute_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_mute_reset_step();
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
        if (muted[i]) {
            continue;
        }
        uint16_t base = mode_rhythm_base_pattern(s_calc, i) ^ variation_mask[i];
        if ((base >> current_step) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
