#include "mode_state.h"

#include "modes/mode_chaos.h"
#include "modes/mode_swing.h"
#include "modes/mode_fixed.h"
#include "modes/mode_rhythm_shared.h"

void mode_state_validate(krono_state_t *state) {
    if (state->chaos_mode_divisor < CHAOS_DIVISOR_MIN ||
        state->chaos_mode_divisor > CHAOS_DIVISOR_DEFAULT ||
        (state->chaos_mode_divisor % CHAOS_DIVISOR_STEP != 0)) {
        state->chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT;
    }
    if (state->swing_profile_index_A >= NUM_SWING_PROFILES) {
        state->swing_profile_index_A = 3;
    }
    if (state->swing_profile_index_B >= NUM_SWING_PROFILES) {
        state->swing_profile_index_B = 3;
    }
    if (state->fixed_bank >= NUM_FIXED_BANKS) {
        state->fixed_bank = 0;
    }
    if (state->drift_probability > 100) state->drift_probability = 0;
    if (state->fill_density > 50) state->fill_density = 0;
    if (state->skip_probability > 100) state->skip_probability = 0;
    if (state->stutter_length != 2 && state->stutter_length != 4 && state->stutter_length != 8) {
        state->stutter_length = 2;
    }
    state->mute_mask &= 0x03FFu;
    if (state->mute_count > MODE_RHYTHM_NUM_OUTPUTS) state->mute_count = MODE_RHYTHM_NUM_OUTPUTS;
    if (state->density_pct > 200) state->density_pct = 100;
    if (state->accumulate_active_count < 1 || state->accumulate_active_count > MODE_RHYTHM_NUM_OUTPUTS) {
        state->accumulate_active_count = 1;
    }
    state->accumulate_active_mask &= 0x03FFu;
    if (state->accumulate_active_mask == 0) state->accumulate_active_mask = 1u;
    for (int i = 0; i < MODE_RHYTHM_NUM_OUTPUTS; i++) {
        state->accumulate_phase_offsets[i] &= 0x0Fu;
    }
}

void mode_state_apply_runtime(operational_mode_t op_mode, const krono_state_t *state) {
    if (op_mode == MODE_CHAOS) {
        mode_chaos_set_divisor(state->chaos_mode_divisor);
    }
    if (op_mode == MODE_SWING) {
        mode_swing_set_profile_indices(state->swing_profile_index_A, state->swing_profile_index_B);
    }
    if (op_mode == MODE_FIXED) {
        mode_fixed_set_bank(state->fixed_bank);
    }
    mode_drift_set_state(state->drift_active, state->drift_probability, state->drift_ramp_up);
    mode_fill_set_state(state->fill_density, state->fill_ramp_up);
    mode_skip_set_state(state->skip_active, state->skip_probability, state->skip_ramp_up);
    mode_stutter_set_state(state->stutter_active, state->stutter_length, state->stutter_ramp_up, state->stutter_variation_mask);
    mode_morph_set_state(state->morph_frozen, state->morph_generation, state->morph_patterns);
    mode_mute_set_state(state->mute_mask, state->mute_count, state->mute_ramp_up, state->mute_variation_mask);
    mode_density_set_state(state->density_pct, state->density_ramp_up);
    mode_song_set_state(state->song_variation_seed, state->song_variation_pending);
    mode_accumulate_set_state(state->accumulate_active_count, state->accumulate_add_pending,
                              state->accumulate_active_mask, state->accumulate_phase_offsets,
                              state->accumulate_variation_masks);
}

void mode_state_capture_for_save(operational_mode_t op_mode,
                                 const krono_state_t *current_state,
                                 krono_state_t *state_to_save) {
    if (op_mode == MODE_CHAOS) {
        state_to_save->chaos_mode_divisor = mode_chaos_get_divisor();
    } else {
        state_to_save->chaos_mode_divisor = current_state->chaos_mode_divisor;
    }

    if (op_mode == MODE_SWING) {
        mode_swing_get_profile_indices(&state_to_save->swing_profile_index_A, &state_to_save->swing_profile_index_B);
    } else {
        state_to_save->swing_profile_index_A = current_state->swing_profile_index_A;
        state_to_save->swing_profile_index_B = current_state->swing_profile_index_B;
    }

    if (op_mode == MODE_FIXED) {
        state_to_save->fixed_bank = mode_fixed_get_bank();
    } else {
        state_to_save->fixed_bank = current_state->fixed_bank;
    }

    mode_drift_get_state(&state_to_save->drift_active, &state_to_save->drift_probability, &state_to_save->drift_ramp_up);
    mode_fill_get_state(&state_to_save->fill_density, &state_to_save->fill_ramp_up);
    mode_skip_get_state(&state_to_save->skip_active, &state_to_save->skip_probability, &state_to_save->skip_ramp_up);
    mode_stutter_get_state(&state_to_save->stutter_active, &state_to_save->stutter_length,
                           &state_to_save->stutter_ramp_up, state_to_save->stutter_variation_mask);
    mode_morph_get_state(&state_to_save->morph_frozen, &state_to_save->morph_generation, state_to_save->morph_patterns);
    mode_mute_get_state(&state_to_save->mute_mask, &state_to_save->mute_count,
                        &state_to_save->mute_ramp_up, state_to_save->mute_variation_mask);
    mode_density_get_state(&state_to_save->density_pct, &state_to_save->density_ramp_up);
    mode_song_get_state(&state_to_save->song_variation_seed, &state_to_save->song_variation_pending);
    mode_accumulate_get_state(&state_to_save->accumulate_active_count, &state_to_save->accumulate_add_pending,
                              &state_to_save->accumulate_active_mask, state_to_save->accumulate_phase_offsets,
                              state_to_save->accumulate_variation_masks);
}
