#include "mode_state.h"

#include "modes/mode_chaos.h"
#include "modes/mode_swing.h"
#include "modes/mode_binary.h"

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
    if (state->binary_bank >= NUM_BINARY_BANKS) {
        state->binary_bank = 0;
    }
}

void mode_state_apply_runtime(operational_mode_t op_mode, const krono_state_t *state) {
    if (op_mode == MODE_CHAOS) {
        mode_chaos_set_divisor(state->chaos_mode_divisor);
    }
    if (op_mode == MODE_SWING) {
        mode_swing_set_profile_indices(state->swing_profile_index_A, state->swing_profile_index_B);
    }
    if (op_mode == MODE_BINARY) {
        mode_binary_set_bank(state->binary_bank);
    }
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

    if (op_mode == MODE_BINARY) {
        state_to_save->binary_bank = mode_binary_get_bank();
    } else {
        state_to_save->binary_bank = current_state->binary_bank;
    }
}
