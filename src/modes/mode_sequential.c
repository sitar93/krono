#include "mode_sequential.h"
#include "drivers/io.h"
#include "main_constants.h" // For DEFAULT_PULSE_DURATION_MS
#include <stdbool.h> // For bool type

// --- Module Constants ---
// Outputs 2A/2B to 5A/5B are sequence outputs, 6A/6B are sums
#define NUM_SEQUENCE_GENERATORS 4
#define SUM_OUTPUT_INDEX 4 // Index in the arrays below (0-based)

// Sequences for outputs 2-5
static const uint32_t FIBONACCI_SEQ[NUM_SEQUENCE_GENERATORS] = {1, 2, 3, 5};
static const uint32_t PRIMES_SEQ[NUM_SEQUENCE_GENERATORS] = {2, 3, 5, 7};
static const uint32_t LUCAS_SEQ[NUM_SEQUENCE_GENERATORS] = {2, 1, 3, 4};
static const uint32_t COMPOSITE_SEQ[NUM_SEQUENCE_GENERATORS] = {4, 6, 8, 9};

// Map abstract groups A/B to physical jack outputs (including sum outputs)
static const jack_output_t group_a_jacks[NUM_SEQUENCE_GENERATORS + 1] = {
    JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, // Sequence outputs
    JACK_OUT_6A                                        // Sum output
};
static const jack_output_t group_b_jacks[NUM_SEQUENCE_GENERATORS + 1] = {
    JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, // Sequence outputs
    JACK_OUT_6B                                        // Sum output
};

// --- Module State ---
// No internal clock_count needed, using context->f1_counter directly

// --- Public Function Implementations ---

void mode_sequential_init(void) {
    // Reset outputs on initialization
    mode_sequential_reset();
}

void mode_sequential_update(const mode_context_t* context) {
    if (!context->f1_rising_edge) {
        return; // Only act on the F1 rising edge
    }

    uint32_t current_clock_count = context->f1_counter;

    const uint32_t* seq_a = (context->calc_mode == CALC_MODE_NORMAL) ? FIBONACCI_SEQ : LUCAS_SEQ;
    const uint32_t* seq_b = (context->calc_mode == CALC_MODE_NORMAL) ? PRIMES_SEQ : COMPOSITE_SEQ;

    bool group_a_sum_trigger = false;
    bool group_b_sum_trigger = false;

    // Update Group A sequence outputs (2A-5A) and check for sum trigger
    for (int i = 0; i < NUM_SEQUENCE_GENERATORS; i++) {
        uint32_t divisor = seq_a[i];
        bool trigger = false;
        if (divisor > 0) { // Handle 1 in Fib/Lucas and standard division
            if ((current_clock_count % divisor) == 0) {
                trigger = true;
                set_output_high_for_duration(group_a_jacks[i], DEFAULT_PULSE_DURATION_MS); // Use central constant
            }
        }
        group_a_sum_trigger = group_a_sum_trigger || trigger; // OR logic for sum
    }

    // Update Group B sequence outputs (2B-5B) and check for sum trigger
    for (int i = 0; i < NUM_SEQUENCE_GENERATORS; i++) {
        uint32_t divisor = seq_b[i];
         bool trigger = false;
        if (divisor > 0) { // Should always be true for Primes/Composites > 1
            if ((current_clock_count % divisor) == 0) {
                 trigger = true;
                set_output_high_for_duration(group_b_jacks[i], DEFAULT_PULSE_DURATION_MS); // Use central constant
            }
        }
        group_b_sum_trigger = group_b_sum_trigger || trigger; // OR logic for sum
    }

    // Trigger sum outputs (6A/6B) if any sequence output in their group triggered
    if (group_a_sum_trigger) {
        set_output_high_for_duration(group_a_jacks[SUM_OUTPUT_INDEX], DEFAULT_PULSE_DURATION_MS); // Use central constant
    }
     if (group_b_sum_trigger) {
        set_output_high_for_duration(group_b_jacks[SUM_OUTPUT_INDEX], DEFAULT_PULSE_DURATION_MS); // Use central constant
    }
}

void mode_sequential_reset(void) {
    // Turn off all outputs (2-6 A/B)
    for (int i = 0; i < NUM_SEQUENCE_GENERATORS + 1; i++) {
        set_output(group_a_jacks[i], false);
        set_output(group_b_jacks[i], false);
    }
    // No internal state needs resetting here as we use context->f1_counter
}
