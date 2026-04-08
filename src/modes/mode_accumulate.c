#include "mode_rhythm_shared.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static uint8_t active_count;
static uint32_t accum_step;
static bool add_pending; /* Persisted as freeze flag */
static uint8_t current_step;
static uint32_t next_step_time;
static calculation_mode_t s_calc;
static bool active_flags[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t phase_offsets[MODE_RHYTHM_NUM_OUTPUTS];
static uint16_t variation_masks[MODE_RHYTHM_NUM_OUTPUTS];
static uint8_t bars_since_change;

static void accumulate_activate_random(void) {
    int candidates[MODE_RHYTHM_NUM_OUTPUTS];
    int n = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        if (!active_flags[i]) {
            candidates[n++] = i;
        }
    }
    if (n <= 0) {
        return;
    }
    int idx = candidates[rand() % n];
    active_flags[idx] = true;
    phase_offsets[idx] = (uint8_t)(rand() % 16);
    /* Each activation slightly reshapes the active loop for this output. */
    uint8_t b1 = (uint8_t)(rand() % 16);
    uint8_t b2 = (uint8_t)(rand() % 16);
    variation_masks[idx] ^= (uint16_t)((1u << b1) | (1u << b2));
}

static void accumulate_reset_to_minimum(void) {
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        active_flags[i] = false;
        variation_masks[i] = 0;
    }
    active_count = 1;
    bars_since_change = 0;
    accumulate_activate_random();
}

void mode_accumulate_init(void) {
    mode_accumulate_reset();
    next_step_time = 0;
}

void mode_accumulate_reset(void) {
    active_count = 1;
    accum_step = 0;
    add_pending = false;
    current_step = 0;
    next_step_time = 0;
    bars_since_change = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        active_flags[i] = false;
        phase_offsets[i] = 0;
        variation_masks[i] = 0;
    }
    accumulate_activate_random();
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        set_output(mode_rhythm_jacks[i], false);
    }
}

void mode_accumulate_reset_step(void) {
    current_step = 0;
}

void mode_accumulate_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    /* MOD toggles freeze of auto-accumulation. */
    add_pending = !add_pending;
}

void mode_accumulate_set_state(uint8_t count, bool pending, uint16_t active_mask,
                               const uint8_t *phases, const uint16_t *variations) {
    active_count = count;
    add_pending = pending;
    uint8_t actual_active = 0;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        active_flags[i] = ((active_mask >> i) & 1u) != 0;
        if (active_flags[i]) {
            actual_active++;
        }
        phase_offsets[i] = phases ? (uint8_t)(phases[i] & 0x0Fu) : (uint8_t)((i * 3) % 16);
        variation_masks[i] = variations ? variations[i] : 0;
    }
    if (actual_active > 0) {
        active_count = actual_active;
    }
    if (active_count == 0) {
        active_count = 1;
        active_flags[0] = true;
    }
    bars_since_change = 0;
}

void mode_accumulate_get_state(uint8_t *count, bool *pending, uint16_t *active_mask,
                               uint8_t *phases, uint16_t *variations) {
    if (count) *count = active_count;
    if (pending) *pending = add_pending;
    if (active_mask) {
        uint16_t m = 0;
        for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
            if (active_flags[i]) {
                m |= (uint16_t)(1u << i);
            }
        }
        *active_mask = m;
    }
    if (phases) {
        memcpy(phases, phase_offsets, sizeof phase_offsets);
    }
    if (variations) {
        memcpy(variations, variation_masks, sizeof variation_masks);
    }
}

void mode_accumulate_update(const mode_context_t *context) {
    s_calc = context->calc_mode;
    if (context->sync_request) {
        mode_accumulate_reset_step();
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

    accum_step++;
    if (current_step == 0) {
        if (!add_pending) {
            bars_since_change++;
            if (bars_since_change >= active_count) {
                bars_since_change = 0;
                if (active_count < MODE_RHYTHM_NUM_OUTPUTS) {
                    active_count++;
                    accumulate_activate_random();
                } else {
                    /* Drastic loop reset to minimum once max accumulation is reached. */
                    accumulate_reset_to_minimum();
                }
            }
        }
    }

    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        if (!active_flags[i]) {
            continue;
        }
        uint16_t base = mode_rhythm_base_pattern(s_calc, i) ^ variation_masks[i];
        uint8_t pos = (uint8_t)((current_step + phase_offsets[i]) & 0x0Fu);
        if ((base >> pos) & 1) {
            set_output_high_for_duration(mode_rhythm_jacks[i], DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}
