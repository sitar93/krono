#include "modes.h"
#include <stddef.h> // For NULL

// Include headers for all modes
#include "mode_default.h"
#include "mode_euclidean.h"
#include "mode_musical.h"
#include "mode_probabilistic.h"
#include "mode_sequential.h"
#include "mode_swing.h"
#include "mode_polyrhythm.h"
#include "mode_logic.h"
#include "mode_phasing.h"
#include "mode_chaos.h"
#include "mode_fixed.h"

// Function pointer table for mode reset functions
static void (*mode_reset_functions[NUM_OPERATIONAL_MODES])(void) = {
    mode_default_reset,
    mode_euclidean_reset,
    mode_musical_reset,
    mode_probabilistic_reset,
    mode_sequential_reset,
    mode_swing_reset,
    mode_polyrhythm_reset,
    mode_logic_reset,
    mode_phasing_reset,
    mode_chaos_reset,
    mode_fixed_reset,
    mode_drift_reset,
    mode_fill_reset,
    mode_skip_reset,
    mode_stutter_reset,
    mode_morph_reset,
    mode_mute_reset,
    mode_density_reset,
    mode_song_reset,
    mode_accumulate_reset,
    mode_gamma_sequential_reset_reset,
    mode_gamma_sequential_freeze_reset,
    mode_gamma_sequential_trip_reset,
    mode_gamma_sequential_fire_reset,
    mode_gamma_sequential_bounce_reset,
    mode_gamma_portals_reset,
    mode_gamma_coin_toss_reset,
    mode_gamma_ratchet_reset,
    mode_gamma_anti_ratchet_reset,
    mode_gamma_start_stop_reset
};

// Function pointer table for mode init functions
static void (*mode_init_functions[NUM_OPERATIONAL_MODES])(void) = {
    mode_default_init,
    mode_euclidean_init,
    mode_musical_init,
    mode_probabilistic_init,
    mode_sequential_init,
    mode_swing_init,
    mode_polyrhythm_init,
    mode_logic_init,
    mode_phasing_init,
    mode_chaos_init,
    mode_fixed_init,
    mode_drift_init,
    mode_fill_init,
    mode_skip_init,
    mode_stutter_init,
    mode_morph_init,
    mode_mute_init,
    mode_density_init,
    mode_song_init,
    mode_accumulate_init,
    mode_gamma_sequential_reset_init,
    mode_gamma_sequential_freeze_init,
    mode_gamma_sequential_trip_init,
    mode_gamma_sequential_fire_init,
    mode_gamma_sequential_bounce_init,
    mode_gamma_portals_init,
    mode_gamma_coin_toss_init,
    mode_gamma_ratchet_init,
    mode_gamma_anti_ratchet_init,
    mode_gamma_start_stop_init
};


/**
 * @brief Resets the state of the currently active mode.
 * @param mode The operational mode to reset.
 */
void mode_reset_current(operational_mode_t mode) {
    if (mode < NUM_OPERATIONAL_MODES) {
        if (mode_reset_functions[mode] != NULL) {
            mode_reset_functions[mode]();
        }
    }
}

/**
 * @brief Initializes the state of the currently active mode.
 * @param mode The operational mode to initialize.
 */
void mode_init_current(operational_mode_t mode) {
    if (mode < NUM_OPERATIONAL_MODES) {
        if (mode_init_functions[mode] != NULL) {
            mode_init_functions[mode]();
        }
    }
}
