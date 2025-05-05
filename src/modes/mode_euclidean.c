#include "modes.h"
#include "../drivers/io.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h> // For floorf

// --- Include main constants ---
#include "../main_constants.h"

// --- Mode-Specific Defines ---
#define NUM_EUCLIDEAN_FACTORED_OUTPUTS 5 // Outputs 2A/2B to 6A/6B

// --- Mode-Specific Global Variables (static) ---
// Adjusted for 5 outputs
static const uint8_t euclidean_k_set1[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { 2, 3, 3, 4, 5 };
static const uint8_t euclidean_n_set1[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { 5, 7, 8, 9, 11 };
static const uint8_t euclidean_k_set2[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { 3, 4, 5, 6, 7 };
static const uint8_t euclidean_n_set2[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { 4, 6, 7, 8, 9 };

static jack_output_t group_a_outputs[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A };
static jack_output_t group_b_outputs[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = { JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B };

// Step counters remain necessary for the Euclidean logic itself
static uint32_t step_a[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = {0};
static uint32_t step_b[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = {0};

// on_time_a and on_time_b are no longer needed
// static uint32_t on_time_a[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = {0};
// static uint32_t on_time_b[NUM_EUCLIDEAN_FACTORED_OUTPUTS] = {0};

// --- Helper Function ---

// Simple Euclidean pulse check based on Bjorklund's algorithm concept
static bool get_euclidean_pulse(uint8_t k, uint8_t n, uint32_t step) {
    if (n == 0) return false;
    if (k == 0) return false;
    if (k >= n) return true;
    // Calculate current step modulo N
    uint32_t current_step_mod = step % n;
    // Calculate previous step modulo N, handling wrap-around
    // Use 0U for unsigned integer literal comparison and cast results to avoid signed/unsigned warning
    uint32_t prev_step_mod = (current_step_mod == 0U) ? (uint32_t)(n - 1) : (uint32_t)(current_step_mod - 1);

    // Using integer division approach to avoid floats
    // Check if floor(current_step * k / n) != floor(previous_step * k / n)
    uint32_t current_floor = (current_step_mod * k) / n;
    uint32_t prev_floor = (prev_step_mod * k) / n;

    return (current_floor != prev_floor);
}

// --- Function Implementations ---

void mode_euclidean_init(void) {
    mode_euclidean_reset();
}

void mode_euclidean_update(const mode_context_t* context) {
    bool set1_drives_group_a = (context->calc_mode == CALC_MODE_NORMAL);

    // Euclidean steps only happen on the F1 rising edge
    if (context->f1_rising_edge) {
        for(int i = 0; i < NUM_EUCLIDEAN_FACTORED_OUTPUTS; i++) {
            // Determine which K/N set and state applies to which physical group
            const uint8_t* k_set_a = set1_drives_group_a ? euclidean_k_set1 : euclidean_k_set2;
            const uint8_t* n_set_a = set1_drives_group_a ? euclidean_n_set1 : euclidean_n_set2;
            uint32_t* step_ptr_a = &step_a[i];
            // uint32_t* on_time_ptr_a = &on_time_a[i]; // No longer needed

            const uint8_t* k_set_b = set1_drives_group_a ? euclidean_k_set2 : euclidean_k_set1;
            const uint8_t* n_set_b = set1_drives_group_a ? euclidean_n_set2 : euclidean_n_set1;
            uint32_t* step_ptr_b = &step_b[i];
            // uint32_t* on_time_ptr_b = &on_time_b[i]; // No longer needed

            uint8_t k_a = k_set_a[i];
            uint8_t n_a = n_set_a[i];
            uint8_t k_b = k_set_b[i];
            uint8_t n_b = n_set_b[i];

            // Calculate pulse for Group A
            (*step_ptr_a)++; // Increment step counter
            bool pulse_a = get_euclidean_pulse(k_a, n_a, *step_ptr_a);
            if (n_a > 0) *step_ptr_a %= n_a; else *step_ptr_a = 0; // Wrap step counter
            if (pulse_a) {
                // Use the dedicated function for timed pulse
                set_output_high_for_duration(group_a_outputs[i], DEFAULT_PULSE_DURATION_MS);
                // *on_time_ptr_a = context->current_time_ms; // No longer needed
            }

            // Calculate pulse for Group B
            (*step_ptr_b)++; // Increment step counter
             bool pulse_b = get_euclidean_pulse(k_b, n_b, *step_ptr_b);
            if (n_b > 0) *step_ptr_b %= n_b; else *step_ptr_b = 0; // Wrap step counter
            if (pulse_b) {
                 // Use the dedicated function for timed pulse
                 set_output_high_for_duration(group_b_outputs[i], DEFAULT_PULSE_DURATION_MS);
                // *on_time_ptr_b = context->current_time_ms; // No longer needed
            }
        }
    }

    // The loop checking pulse duration is no longer needed here, io_update_pulse_timers handles it.
    /*
    for(int i = 0; i < NUM_EUCLIDEAN_FACTORED_OUTPUTS; i++) {
        uint32_t* on_time_ptr_a = &on_time_a[i];
        uint32_t* on_time_ptr_b = &on_time_b[i];

        // Turn off Group A output after pulse width
        if (*on_time_ptr_a != 0 && (context->current_time_ms - *on_time_ptr_a >= DEFAULT_PULSE_DURATION_MS)) {
            set_output(group_a_outputs[i], false);
            *on_time_ptr_a = 0; // Reset on time
        }

        // Turn off Group B output after pulse width
        if (*on_time_ptr_b != 0 && (context->current_time_ms - *on_time_ptr_b >= DEFAULT_PULSE_DURATION_MS)) {
            set_output(group_b_outputs[i], false);
            *on_time_ptr_b = 0; // Reset on time
        }
    }
    */
}

void mode_euclidean_reset(void) {
    // Turn off all outputs controlled by this mode using set_output
    // which also clears any pending pulse timers in io.c
    for (int i = 0; i < NUM_EUCLIDEAN_FACTORED_OUTPUTS; i++) {
        set_output(group_a_outputs[i], false);
        step_a[i] = 0;
        // on_time_a[i] = 0; // No longer needed

        set_output(group_b_outputs[i], false);
        step_b[i] = 0;
        // on_time_b[i] = 0; // No longer needed
    }
}
