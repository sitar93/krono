#include "modes.h"
#include "../drivers/io.h"
// #include "../status_led.h" // <<< Removed debug include
#include <stdint.h>
#include <stdbool.h>
#include <limits.h> // For UINT32_MAX

// --- Include main constants ---
#include "../main_constants.h"

// --- Mode-Specific Defines ---
#define NUM_MUSICAL_FACTORED_OUTPUTS 5 // Outputs 2A/2B to 6A/6B

// --- Mode-Specific Global Variables (static) ---
// Adjusted for 5 outputs
static const uint16_t musical_num_set1[] = {1, 1, 8, 6, 4};
static const uint16_t musical_den_set1[] = {6, 8, 1, 5, 5};
static const uint16_t musical_num_set2[] = {1, 3, 5, 7, 9};
static const uint16_t musical_den_set2[] = {7, 4, 3, 2, 4};

static jack_output_t group_a_outputs[NUM_MUSICAL_FACTORED_OUTPUTS] = { JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A };
static jack_output_t group_b_outputs[NUM_MUSICAL_FACTORED_OUTPUTS] = { JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B };

static uint32_t last_toggle_time_a[NUM_MUSICAL_FACTORED_OUTPUTS] = {0};
static bool state_a[NUM_MUSICAL_FACTORED_OUTPUTS] = {false};
static uint32_t last_toggle_time_b[NUM_MUSICAL_FACTORED_OUTPUTS] = {0};
static bool state_b[NUM_MUSICAL_FACTORED_OUTPUTS] = {false};

// --- Function Implementations ---

void mode_musical_init(void) {
    // status_led_set_override(true, false); // <<< Removed debug
    mode_musical_reset();
    // status_led_set_override(true, true);  // <<< Removed debug
}

void mode_musical_update(const mode_context_t* context) {
    bool set1_drives_group_a = (context->calc_mode == CALC_MODE_NORMAL);
    bool tempo_valid = (context->current_tempo_interval_ms >= MIN_INTERVAL && context->current_tempo_interval_ms <= MAX_INTERVAL);

    for (int i = 0; i < NUM_MUSICAL_FACTORED_OUTPUTS; i++) {
        uint32_t target_interval_set1 = UINT32_MAX, target_interval_set2 = UINT32_MAX;

        if (tempo_valid) {
            // Calculate Set 1 interval
            uint16_t num1 = musical_num_set1[i];
            uint16_t den1 = musical_den_set1[i];
            if (den1 > 0) { // Use denominator for check
                // Use 32-bit calculation (potential overflow for slow tempos)
                uint32_t temp1 = context->current_tempo_interval_ms * num1;
                target_interval_set1 = temp1 / den1;
            } else {
                 target_interval_set1 = UINT32_MAX; // Avoid division by zero
            }

            // Calculate Set 2 interval
            uint16_t num2 = musical_num_set2[i];
            uint16_t den2 = musical_den_set2[i];
            if (den2 > 0) { // Use denominator for check
                // Use 32-bit calculation
                uint32_t temp2 = context->current_tempo_interval_ms * num2;
                target_interval_set2 = temp2 / den2;
            } else {
                 target_interval_set2 = UINT32_MAX; // Avoid division by zero
            }
        }

        // Clamp to minimum interval
        if (target_interval_set1 < MIN_CLOCK_INTERVAL) target_interval_set1 = MIN_CLOCK_INTERVAL;
        if (target_interval_set2 < MIN_CLOCK_INTERVAL) target_interval_set2 = MIN_CLOCK_INTERVAL;

        // --- Assign intervals and state based on calc_mode ---
        uint32_t interval_a = set1_drives_group_a ? target_interval_set1 : target_interval_set2;
        uint32_t interval_b = set1_drives_group_a ? target_interval_set2 : target_interval_set1;
        uint32_t* last_toggle_a_ptr = &last_toggle_time_a[i];
        bool* state_a_ptr = &state_a[i];
        uint32_t* last_toggle_b_ptr = &last_toggle_time_b[i];
        bool* state_b_ptr = &state_b[i];

        // --- Process Physical Group A ---
        if (interval_a != UINT32_MAX) {
            uint32_t time_since_toggle_a = context->current_time_ms - *last_toggle_a_ptr;
            uint32_t pulse_width_a = DEFAULT_PULSE_DURATION_MS; // Use central constant
            if (pulse_width_a >= interval_a) pulse_width_a = interval_a > 1 ? 1 : 0;
            if (pulse_width_a == 0 && interval_a > 0) pulse_width_a = 1;
            uint32_t duration_a = *state_a_ptr ? pulse_width_a : (interval_a > pulse_width_a ? interval_a - pulse_width_a : 1);
             if(duration_a == 0) duration_a = 1;

            if (time_since_toggle_a >= duration_a) {
                *state_a_ptr = !(*state_a_ptr);
                set_output(group_a_outputs[i], *state_a_ptr);
                *last_toggle_a_ptr = context->current_time_ms;
            }
        } else {
             // Turn off output if interval is invalid
            if(*state_a_ptr) { *state_a_ptr = false; set_output(group_a_outputs[i], false); }
        }

        // --- Process Physical Group B ---
        if (interval_b != UINT32_MAX) {
            uint32_t time_since_toggle_b = context->current_time_ms - *last_toggle_b_ptr;
            uint32_t pulse_width_b = DEFAULT_PULSE_DURATION_MS; // Use central constant
            if (pulse_width_b >= interval_b) pulse_width_b = interval_b > 1 ? 1 : 0;
            if (pulse_width_b == 0 && interval_b > 0) pulse_width_b = 1;
            uint32_t duration_b = *state_b_ptr ? pulse_width_b : (interval_b > pulse_width_b ? interval_b - pulse_width_b : 1);
             if(duration_b == 0) duration_b = 1;

            if (time_since_toggle_b >= duration_b) {
                *state_b_ptr = !(*state_b_ptr);
                set_output(group_b_outputs[i], *state_b_ptr);
                *last_toggle_b_ptr = context->current_time_ms;
            }
        } else {
             // Turn off output if interval is invalid
            if(*state_b_ptr) { *state_b_ptr = false; set_output(group_b_outputs[i], false); }
        }
    }
}

void mode_musical_reset(void) {
     // Turn off all outputs controlled by this mode and reset state
    for (int i = 0; i < NUM_MUSICAL_FACTORED_OUTPUTS; i++) {
        set_output(group_a_outputs[i], false);
        state_a[i] = false;
        last_toggle_time_a[i] = 0;

        set_output(group_b_outputs[i], false);
        state_b[i] = false;
        last_toggle_time_b[i] = 0;
    }
}
