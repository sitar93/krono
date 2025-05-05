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
    mode_chaos_reset
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
    mode_chaos_init
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
