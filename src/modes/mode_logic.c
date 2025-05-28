#include "mode_logic.h"
#include "modes.h" // Explicit include
#include "../drivers/io.h" // For set_output, set_output_high_for_duration and jack_output_t enums
#include "../main_constants.h" // Includes variables.h for DEFAULT_PULSE_DURATION_MS
#include <stdbool.h>
#include <stdint.h>
#include <string.h> // For memset
// #include <libopencm3/stm32/gpio.h> // No longer needed for debug
// #include "../util/delay.h" // No longer needed for debug

// --- Constant Definitions ---
#ifndef NUM_OUTPUTS_PER_GROUP // Define locally if not found in included headers
#define NUM_OUTPUTS_PER_GROUP 6
#endif

// Local arrays to map index to jack enum (mirroring io.h)
static const jack_output_t JACK_OUT_A[NUM_OUTPUTS_PER_GROUP] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};

static const jack_output_t JACK_OUT_B[NUM_OUTPUTS_PER_GROUP] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

// --- Type Definitions ---
typedef struct {
    bool outputs[NUM_OUTPUTS_PER_GROUP]; // Index 0 = Output 1, Index 1 = Output 2, etc.
} default_output_set_t;

// --- Module State ---
// Store previous state for edge detection (outputs 2-6 -> index 1-5)
static bool prev_output_a_state[NUM_OUTPUTS_PER_GROUP] = {false};
static bool prev_output_b_state[NUM_OUTPUTS_PER_GROUP] = {false};

// --- Forward declaration of internal helper ---
static void calculate_default_outputs(uint32_t current_tempo_interval, uint32_t current_time_ms, default_output_set_t* set_a, default_output_set_t* set_b);
// ---

// --- Default Mode Calculation Logic (adapted from mode_default.c) ---
static const float default_factors_a[NUM_OUTPUTS_PER_GROUP] = {
    1.0f, 2.0f, 4.0f, 0.5f, 0.25f, 3.0f
};

static const float default_factors_b[NUM_OUTPUTS_PER_GROUP] = {
    1.0f, 0.5f, 0.25f, 2.0f, 4.0f, 6.0f
};

static bool is_output_on(uint32_t interval_ms, uint32_t current_time_ms, float factor) {
    if (factor == 0.0f) return false;
    uint32_t period_ticks = (uint32_t)((float)interval_ms / factor);
    if (period_ticks == 0) return true;
    return (current_time_ms % period_ticks) < (period_ticks / 2);
}

static void calculate_default_outputs(uint32_t current_tempo_interval, uint32_t current_time_ms, default_output_set_t* set_a, default_output_set_t* set_b) {
    for (int i = 1; i < NUM_OUTPUTS_PER_GROUP; ++i) {
        set_a->outputs[i] = is_output_on(current_tempo_interval, current_time_ms, default_factors_a[i]);
        set_b->outputs[i] = is_output_on(current_tempo_interval, current_time_ms, default_factors_b[i]);
    }
}
// --- End Default Mode Calculation Logic ---

void mode_logic_init(void) {
    // Ensure previous states start at false when mode is initialized
    memset(prev_output_a_state, 0, sizeof(prev_output_a_state));
    memset(prev_output_b_state, 0, sizeof(prev_output_b_state));
}

void mode_logic_reset(void) {
    // Turn off outputs 2-6 for both groups when resetting/changing mode
    for (int i = 1; i < NUM_OUTPUTS_PER_GROUP; ++i) {
         // Use set_output directly for immediate turn off, not timed pulse
         set_output(JACK_OUT_A[i], false);
         set_output(JACK_OUT_B[i], false);
    }
    // Reset previous states as well
    memset(prev_output_a_state, 0, sizeof(prev_output_a_state));
    memset(prev_output_b_state, 0, sizeof(prev_output_b_state));
}

void mode_logic_update(const mode_context_t *context) {
    if (!context->f1_rising_edge) {
        return;
    }

    default_output_set_t default_a;
    default_output_set_t default_b;
    calculate_default_outputs(context->current_tempo_interval_ms, context->current_time_ms, &default_a, &default_b);

    for (int i = 1; i < NUM_OUTPUTS_PER_GROUP; ++i) { // Outputs 2-6 (index 1-5)
        bool input_a = default_a.outputs[i];
        bool input_b = default_b.outputs[i];

        bool result_xor = input_a ^ input_b;         // XOR
        bool result_nor = !(input_a || input_b);    // NOR

        bool current_a_state, current_b_state;

        // Determine the desired state based on calc_mode
        if (context->calc_mode == CALC_MODE_NORMAL) {
            current_a_state = result_xor;
            current_b_state = result_nor;
        } else { // Swapped (CALC_MODE_SWAPPED)
            current_a_state = result_nor;
            current_b_state = result_xor;
        }

        // Trigger A output only on rising edge
        if (current_a_state && !prev_output_a_state[i]) {
            set_output_high_for_duration(JACK_OUT_A[i], DEFAULT_PULSE_DURATION_MS);
        }
        // Trigger B output only on rising edge
        if (current_b_state && !prev_output_b_state[i]) {
            set_output_high_for_duration(JACK_OUT_B[i], DEFAULT_PULSE_DURATION_MS);
        }

        // Update previous state for next tick's comparison
        prev_output_a_state[i] = current_a_state;
        prev_output_b_state[i] = current_b_state;
    }
}
