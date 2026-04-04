#ifndef MODE_SWING_H
#define MODE_SWING_H

#include <stdint.h>

// NUM_SWING_PROFILES is used by persistence.c and main.c to validate indices.
#define NUM_SWING_PROFILES 8 

// The declarations of mode_swing_init, mode_swing_update, mode_swing_reset
// are found in modes.h as part of the common mode interface.

// Specific functions for Swing mode to get/set its unique state.
void mode_swing_set_profile_indices(uint8_t profile_index_a, uint8_t profile_index_b);
void mode_swing_get_profile_indices(uint8_t *p_profile_index_a, uint8_t *p_profile_index_b);

#endif // MODE_SWING_H
