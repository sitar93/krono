/**
 * @file mode_phasing.c
 * @brief Phasing Mode implementation with derived outputs.
 */
#include "mode_phasing.h"
#include "drivers/io.h"
#include "main_constants.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h> // For fabsf if needed, or just manual calc
#include <limits.h> // For UINT32_MAX
#include <string.h> // For memset

#define NUM_PHASING_OUTPUTS 5 // Outputs 2-6
#define NUM_DELTA_LEVELS 3

// --- Output Configuration ---
static const jack_output_t group_a_pins[NUM_PHASING_OUTPUTS] = { JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A };
static const jack_output_t group_b_pins[NUM_PHASING_OUTPUTS] = { JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B };

// Factors for outputs 3-6 (index 1-4). Index 0 (Output 2) uses factor 1/1.
// {Multiplier, Divisor}
static const uint16_t output_factors[NUM_PHASING_OUTPUTS][2] = {
    {1, 1}, // Output 2: Base interval
    {1, 2}, // Output 3: Half interval (Double speed)
    {2, 1}, // Output 4: Double interval (Half speed)
    {1, 3}, // Output 5: 1/3 interval (Triple speed)
    {3, 1}  // Output 6: Triple interval (1/3 speed)
};

// Frequency offsets in BPM for Group B
static const float delta_f_values_bpm[NUM_DELTA_LEVELS] = {
    0.1f,  // Small offset
    1.0f,  // Medium offset
    5.0f   // Large offset
};

// --- State ---
typedef struct {
    uint32_t ms_counter;         // Accumulator for time towards next trigger
    uint32_t pulse_ms_remaining; // How many ms left for the current pulse
} phasing_output_state_t;

static phasing_output_state_t state_a[NUM_PHASING_OUTPUTS];
static phasing_output_state_t state_b[NUM_PHASING_OUTPUTS];
static uint8_t current_delta_level = 0;

// --- Forward Declarations of Static Helpers ---
static void update_phasing_output(jack_output_t pin, phasing_output_state_t* state, uint32_t target_interval, uint32_t pulse_duration, uint32_t ms_elapsed);
static uint32_t calculate_derived_interval(uint32_t base_interval, uint16_t multiplier, uint16_t divisor);

// --- Initialization ---
void mode_phasing_init(void) {
    current_delta_level = 0;
    mode_phasing_reset(); // Clear state and outputs
}

// --- Update Function ---
void mode_phasing_update(const mode_context_t* context) {
    // Handle Calculation Mode change (cycles through delta levels for Group B)
    if (context->calc_mode_changed) {
        current_delta_level = (current_delta_level + 1) % NUM_DELTA_LEVELS;
        // Optional: Reset counters on delta change?
        // mode_phasing_reset(); // Might be too abrupt, let's recalculate intervals only.
    }

    // Handle Sync Request (e.g., mode change)
    if (context->sync_request) {
        mode_phasing_reset();
        // No need to return, update logic below will run with reset state
    }

    // --- Calculate Base Intervals --- 
    uint32_t base_interval_a_ms = 0;
    uint32_t base_interval_b_ms = 0;

    if (context->current_tempo_interval_ms == 0) { // Avoid division by zero
        // Keep intervals at 0 if tempo is invalid/zero
    } else {
        // Group A interval is the main tempo interval
        base_interval_a_ms = context->current_tempo_interval_ms;
        if (base_interval_a_ms < MIN_INTERVAL) base_interval_a_ms = MIN_INTERVAL;
        if (base_interval_a_ms > MAX_INTERVAL) base_interval_a_ms = MAX_INTERVAL;

        // Calculate Group B base interval based on frequency offset
        float f_a_bpm = 60000.0f / (float)context->current_tempo_interval_ms;
        float f_b_bpm = f_a_bpm + delta_f_values_bpm[current_delta_level];

        if (f_b_bpm <= 0.0f) {
            base_interval_b_ms = UINT32_MAX; // Stop clock B if frequency is zero/negative
        } else {
            base_interval_b_ms = (uint32_t)(60000.0f / f_b_bpm);
        }

        if (base_interval_b_ms < MIN_INTERVAL) base_interval_b_ms = MIN_INTERVAL;
        if (base_interval_b_ms > MAX_INTERVAL) base_interval_b_ms = MAX_INTERVAL;
    }

    // --- Update each output --- 
    uint32_t ms_elapsed = context->ms_since_last_call;

    for (int i = 0; i < NUM_PHASING_OUTPUTS; ++i) {
        // Calculate target interval for this specific output (A and B)
        uint16_t multiplier = output_factors[i][0];
        uint16_t divisor    = output_factors[i][1];

        uint32_t target_interval_a = calculate_derived_interval(base_interval_a_ms, multiplier, divisor);
        uint32_t target_interval_b = calculate_derived_interval(base_interval_b_ms, multiplier, divisor);

        // Update state machine for output A
        update_phasing_output(group_a_pins[i], &state_a[i], target_interval_a, DEFAULT_PULSE_DURATION_MS, ms_elapsed);
        
        // Update state machine for output B
        update_phasing_output(group_b_pins[i], &state_b[i], target_interval_b, DEFAULT_PULSE_DURATION_MS, ms_elapsed);
    }
}

// --- Reset Function ---
void mode_phasing_reset(void) {
    memset(state_a, 0, sizeof(state_a));
    memset(state_b, 0, sizeof(state_b));
    for (int i = 0; i < NUM_PHASING_OUTPUTS; ++i) {
        set_output(group_a_pins[i], false);
        set_output(group_b_pins[i], false);
    }
}

// --- Static Helper Functions ---

/**
 * @brief Calculates a derived interval based on a base interval and factors.
 */
static uint32_t calculate_derived_interval(uint32_t base_interval, uint16_t multiplier, uint16_t divisor) {
    if (base_interval == 0 || base_interval == UINT32_MAX || divisor == 0) {
        return UINT32_MAX; // Treat 0 or max base interval, or div by zero as invalid
    }
    uint64_t result = ((uint64_t)base_interval * multiplier) / divisor;
    if (result > MAX_INTERVAL) return MAX_INTERVAL;
    // If result is less than minimum pulse width, make it effectively immediate but valid
    if (result < MIN_INTERVAL) return MIN_INTERVAL; 
    return (uint32_t)result;
}

/**
 * @brief Updates the state for a single phasing output.
 * Manages the ms counter, triggers output on interval completion, and handles pulse timing.
 */
static void update_phasing_output(jack_output_t pin, phasing_output_state_t* state, uint32_t target_interval, uint32_t pulse_duration, uint32_t ms_elapsed) {
    
    // Handle invalid interval - ensure output is off
    if (target_interval == UINT32_MAX || target_interval == 0) {
        if (state->pulse_ms_remaining > 0) { // If it was mid-pulse
             state->pulse_ms_remaining = 0;
             set_output(pin, false);
        }
        state->ms_counter = 0; // Reset counter
        return;
    }
    
    // Decrement remaining pulse time if active
    if (state->pulse_ms_remaining > 0) {
        if (ms_elapsed >= state->pulse_ms_remaining) {
            state->pulse_ms_remaining = 0;
            set_output(pin, false);
        } else {
            state->pulse_ms_remaining -= ms_elapsed;
        }
    }

    // Increment the time counter
    state->ms_counter += ms_elapsed;

    // Check if the target interval is reached
    if (state->ms_counter >= target_interval) {
        // Trigger only if not already pulsing
        if (state->pulse_ms_remaining == 0) {
            set_output(pin, true);
            // Ensure pulse duration isn't longer than the interval itself
            uint32_t actual_pulse_duration = (pulse_duration >= target_interval && target_interval > 0) ? target_interval -1 : pulse_duration;
             if (actual_pulse_duration == 0) actual_pulse_duration = 1; // Minimum 1ms pulse if interval allows
            state->pulse_ms_remaining = actual_pulse_duration;
        }
        // Subtract the interval period from the counter (prevents drift)
        // Use modulo if counter could exceed 2*interval?
        // Safer to subtract for now.
         state->ms_counter -= target_interval; 
         // Clamp counter to 0 if subtraction resulted in negative (shouldn't happen with unsigned)
         // if (state->ms_counter > target_interval) { // Check for potential wrap-around? No, unsigned subtraction handles this.
         //    state->ms_counter = 0;
        // }
    }
}
