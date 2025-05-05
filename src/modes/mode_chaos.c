#include "mode_chaos.h"
#include "drivers/io.h"          // For set_output, jack_output_t, NUM_JACK_OUTPUTS
#include "main_constants.h"    // For DEFAULT_PULSE_DURATION_MS
#include "util/delay.h"        // Contains millis()
#include <math.h>             // For fabsf if needed, maybe not
#include <stdlib.h>           // For abs if needed, maybe not
#include <stddef.h>           // For NULL
#include <stdbool.h>          // For true/false

// Lorenz system parameters
static const float LORENZ_SIGMA = 10.0f;
static const float LORENZ_RHO = 28.0f;
static const float LORENZ_BETA = 2.666f; // 8/3

// Simulation state
static float lorenz_x = 0.1f;
static float lorenz_y = 0.0f;
static float lorenz_z = 0.0f;

// Previous state for crossing detection
static float prev_lorenz_x = 0.1f;
static float prev_lorenz_y = 0.0f;
static float prev_lorenz_z = 0.0f;

// Simulation time step
static const float LORENZ_DT = 0.01f; // Integration time step

// Number of variable outputs (Outputs 2-6 for each group A/B)
#define MODE_CHAOS_NUM_VAR_OUTPUTS 5 // Outputs 2-6

// Thresholds for triggering outputs (fixed assignment)
static const float X_THRESHOLDS[MODE_CHAOS_NUM_VAR_OUTPUTS] = { 5.0f, 10.0f, 15.0f, -5.0f, -10.0f }; // Used for Group A
static const float YZ_THRESHOLDS[MODE_CHAOS_NUM_VAR_OUTPUTS] = { 10.0f, 20.0f, -10.0f, 30.0f, 10.0f }; // Used for Group B
static const bool YZ_USE_Y[MODE_CHAOS_NUM_VAR_OUTPUTS] = { true, false, true, false, false }; // Maps YZ_THRESHOLDS to y or z for Group B

// Output pulse timing state
static uint32_t trigger_start_time_ms[NUM_JACK_OUTPUTS] = {0}; // 0 means inactive

// Crossing counters for divide-by-N logic
static uint32_t x_crossing_counter[MODE_CHAOS_NUM_VAR_OUTPUTS] = {0};
static uint32_t yz_crossing_counter[MODE_CHAOS_NUM_VAR_OUTPUTS] = {0};

// Divisor state (modified by Calc Swap button in this mode)
// Constants defined in mode_chaos.h
static uint32_t chaos_current_divisor = CHAOS_DIVISOR_DEFAULT;

// --- Helper Functions ---

// Check for threshold crossing
static bool crossed_threshold(float current, float previous, float threshold) {
    // Check positive crossing (increasing)
    if (previous < threshold && current >= threshold) {
        return true;
    }
    // Check negative crossing (decreasing) - for negative thresholds
    if (threshold < 0 && previous > threshold && current <= threshold) {
         return true;
    }
    // Check positive crossing for negative thresholds might also be useful depending on desired trigger
    // if (threshold < 0 && previous < threshold && current >= threshold) return true;

    // Check negative crossing for positive thresholds
    if (threshold >= 0 && previous > threshold && current <= threshold) {
         return true; // Trigger on down-crossing too? Let's try yes for now.
    }
    return false;
}


// Set output trigger
static void trigger_output(jack_output_t output_index) {
    // is_pulsable_output >= JACK_OUT_1A is always true for unsigned types
    bool is_pulsable_output = (output_index <= JACK_OUT_6A) ||
                              (output_index >= JACK_OUT_1B && output_index <= JACK_OUT_6B);

    if (is_pulsable_output) {
        set_output(output_index, true); // true for HIGH
        trigger_start_time_ms[output_index] = millis();
    }
}

// --- Mode Interface Functions ---

void mode_chaos_init(void) {
    // Initialize Lorenz state
    lorenz_x = 0.1f;
    lorenz_y = 0.0f;
    lorenz_z = 0.0f;
    prev_lorenz_x = lorenz_x;
    prev_lorenz_y = lorenz_y;
    prev_lorenz_z = lorenz_z;

    // Reset trigger timers and outputs
    for (jack_output_t i = JACK_OUT_1A; i <= JACK_OUT_6B; ++i) {
         bool is_group_b_output = (i >= JACK_OUT_1B && i <= JACK_OUT_6B);
         if (i > JACK_OUT_6A && !is_group_b_output) continue;
         trigger_start_time_ms[i] = 0;
         set_output(i, false);
    }

    // Reset crossing counters
    for (int j = 0; j < MODE_CHAOS_NUM_VAR_OUTPUTS; ++j) {
        x_crossing_counter[j] = 0;
        yz_crossing_counter[j] = 0;
    }
    // Reset divisor to default
    chaos_current_divisor = CHAOS_DIVISOR_DEFAULT;
}

void mode_chaos_reset(void) {
    // Turn off variable outputs
    for (jack_output_t i = JACK_OUT_2A; i <= JACK_OUT_6B; ++i) {
         bool is_group_b_output = (i >= JACK_OUT_2B && i <= JACK_OUT_6B);
         if (i > JACK_OUT_6A && !is_group_b_output) continue;
         set_output(i, false);
         trigger_start_time_ms[i] = 0;
    }

    // Reset crossing counters
    for (int j = 0; j < MODE_CHAOS_NUM_VAR_OUTPUTS; ++j) {
        x_crossing_counter[j] = 0;
        yz_crossing_counter[j] = 0;
    }
    // Reset divisor to default
    chaos_current_divisor = CHAOS_DIVISOR_DEFAULT;
    // Don't reset simulation state here
}

void mode_chaos_update(const mode_context_t *context) {
    uint32_t current_ms = millis();

    // --- Adjust divisor on PA1 press --- 
    if (context->calc_mode_changed) { // Check if PA1 was pressed
        if (chaos_current_divisor <= CHAOS_DIVISOR_MIN) { // If at or below minimum
             chaos_current_divisor = CHAOS_DIVISOR_DEFAULT; // Reset to default
        } else {
            chaos_current_divisor -= CHAOS_DIVISOR_STEP;
            // Ensure it doesn't go below min after decrementing
            if (chaos_current_divisor < CHAOS_DIVISOR_MIN) {
                 chaos_current_divisor = CHAOS_DIVISOR_MIN;
            }
        } 
    }

    // 1. Turn off outputs whose pulse duration has expired
    for (jack_output_t i = JACK_OUT_1A; i <= JACK_OUT_6B; ++i) {
        bool is_group_b_output = (i >= JACK_OUT_1B && i <= JACK_OUT_6B);
        if (i > JACK_OUT_6A && !is_group_b_output) continue;
        if (trigger_start_time_ms[i] != 0 && (current_ms - trigger_start_time_ms[i] >= DEFAULT_PULSE_DURATION_MS)) {
            set_output(i, false);
            trigger_start_time_ms[i] = 0;
        }
    }

    // 2. Run Lorenz simulation steps
    uint32_t elapsed_ms = context->ms_since_last_call;
    if (elapsed_ms == 0) elapsed_ms = 1;
    if (elapsed_ms > 100) elapsed_ms = 100;
    int num_steps = (int)((float)elapsed_ms / (LORENZ_DT * 1000.0f));
    num_steps = num_steps > 0 ? num_steps : 1;
    prev_lorenz_x = lorenz_x;
    prev_lorenz_y = lorenz_y;
    prev_lorenz_z = lorenz_z;
    for (int i = 0; i < num_steps; ++i) {
        float dx = LORENZ_SIGMA * (lorenz_y - lorenz_x);
        float dy = lorenz_x * (LORENZ_RHO - lorenz_z) - lorenz_y;
        float dz = lorenz_x * lorenz_y - LORENZ_BETA * lorenz_z;
        lorenz_x += dx * LORENZ_DT;
        lorenz_y += dy * LORENZ_DT;
        lorenz_z += dz * LORENZ_DT;
    }

    // 3. Check for threshold crossings and trigger outputs
    // Threshold assignments are now FIXED: Group A=X, Group B=Y/Z

    // --- Group A Triggering (Outputs 2A-6A) --- 
    for (int i = 0; i < MODE_CHAOS_NUM_VAR_OUTPUTS; ++i) {
        jack_output_t output = (jack_output_t)(JACK_OUT_2A + i);
        float threshold = X_THRESHOLDS[i];
        bool crossed = crossed_threshold(lorenz_x, prev_lorenz_x, threshold);

        if (crossed) {
             x_crossing_counter[i]++;
             if (trigger_start_time_ms[output] == 0 && (x_crossing_counter[i] % chaos_current_divisor == 0)) {
                 trigger_output(output);
             }
        }
    }

    // --- Group B Triggering (Outputs 2B-6B) ---
    for (int i = 0; i < MODE_CHAOS_NUM_VAR_OUTPUTS; ++i) {
        jack_output_t output = (jack_output_t)(JACK_OUT_2B + i);
        float threshold = YZ_THRESHOLDS[i];
        float current_val = YZ_USE_Y[i] ? lorenz_y : lorenz_z;
        float prev_val = YZ_USE_Y[i] ? prev_lorenz_y : prev_lorenz_z;
        bool crossed = crossed_threshold(current_val, prev_val, threshold);

         if (crossed) {
             yz_crossing_counter[i]++;
             if (trigger_start_time_ms[output] == 0 && (yz_crossing_counter[i] % chaos_current_divisor == 0)) {
                 trigger_output(output);
             }
         }
    }
}

// --- Persistence Functions ---

uint32_t mode_chaos_get_divisor(void) {
    return chaos_current_divisor;
}

void mode_chaos_set_divisor(uint32_t divisor) {
    // Validate the loaded divisor against limits
    if (divisor >= CHAOS_DIVISOR_MIN && divisor <= CHAOS_DIVISOR_DEFAULT && (divisor % CHAOS_DIVISOR_STEP == 0)) {
        chaos_current_divisor = divisor;
    } else {
        chaos_current_divisor = CHAOS_DIVISOR_DEFAULT; // Use default if loaded value is invalid
    }
}
