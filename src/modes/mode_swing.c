 \ 
#include "mode_swing.h"
#include "drivers/io.h"
#include "modes.h" // For mode_context_t
#include "main_constants.h" // For DEFAULT_PULSE_DURATION_MS

#include <string.h> // For memset

// --- Configuration ---
#define SWING_MEASURE_BEATS 4
// NUM_SWING_PROFILES is now in mode_swing.h (set to 8)
#define NUM_SWING_OUTPUTS 5 // For outputs 2-6

// Swing profiles (8 profiles total)
// Values are swing percentages (50 = no swing)
static const uint8_t swing_profile_0_none[NUM_SWING_OUTPUTS]        = {50, 50, 50, 50, 50};
static const uint8_t swing_profile_1_subtle[NUM_SWING_OUTPUTS]      = {52, 53, 54, 55, 56};
static const uint8_t swing_profile_2_light[NUM_SWING_OUTPUTS]       = {55, 57, 59, 61, 63};
static const uint8_t swing_profile_3_medium[NUM_SWING_OUTPUTS]      = {58, 61, 64, 67, 70}; // Default for init
static const uint8_t swing_profile_4_groovy[NUM_SWING_OUTPUTS]      = {62, 65, 68, 71, 74};
static const uint8_t swing_profile_5_heavy[NUM_SWING_OUTPUTS]       = {66, 69, 72, 75, 78};
static const uint8_t swing_profile_6_super_heavy[NUM_SWING_OUTPUTS] = {70, 73, 76, 79, 82};
static const uint8_t swing_profile_7_extreme[NUM_SWING_OUTPUTS]     = {75, 78, 81, 84, 87};

static const uint8_t* const swing_profiles_available[NUM_SWING_PROFILES] = {
    swing_profile_0_none,
    swing_profile_1_subtle,
    swing_profile_2_light,
    swing_profile_3_medium,
    swing_profile_4_groovy,
    swing_profile_5_heavy,
    swing_profile_6_super_heavy,
    swing_profile_7_extreme
};

// --- Module State ---
static uint8_t current_swing_profile_index_A = 3; // Default to Medium for Group A
static uint8_t current_swing_profile_index_B = 3; // Default to Medium for Group B

static uint32_t output_on_times[NUM_JACK_OUTPUTS];  // Time when pin should turn ON
static uint32_t output_off_times[NUM_JACK_OUTPUTS]; // Time when pin should turn OFF

// Helper function to calculate swing delay
static uint32_t calculate_delay(uint32_t beat_index, uint32_t tempo_interval, uint8_t swing_percent) {
    if (beat_index % 2 == 0 || swing_percent <= 50) { // No delay on even beats (0, 2 for SWING_MEASURE_BEATS=4) or if swing <= 50%
        return 0;
    }
    // Calculate delay for odd beats (1, 3 for SWING_MEASURE_BEATS=4)
    int32_t delay_ms = ((int32_t)swing_percent - 50) * (int32_t)tempo_interval / 100;
    return (delay_ms > 0) ? (uint32_t)delay_ms : 0;
}

// --- Mode Interface Functions ---

void mode_swing_init(void) {
    memset(output_on_times, 0, sizeof(output_on_times));
    memset(output_off_times, 0, sizeof(output_off_times));
    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
        set_output(pin, false);
    }
    // Default indices are set by global var initialization or by mode_swing_set_profile_indices later
    // For a clean init, ensure they are set to a known default if not relying on persistence load immediately.
    current_swing_profile_index_A = 3; // Default to Medium
    current_swing_profile_index_B = 3; // Default to Medium
}

void mode_swing_update(const mode_context_t* context) {
    uint32_t current_time = context->current_time_ms;

    // Handle profile change immediately if calc_mode_changed is true (from PA1 or PB4).
    if (context->calc_mode_changed) { 
        current_swing_profile_index_A = (current_swing_profile_index_A + 1) % NUM_SWING_PROFILES; 
        current_swing_profile_index_B = (current_swing_profile_index_B - 1 + NUM_SWING_PROFILES) % NUM_SWING_PROFILES; 
    }

    // --- Check for scheduled ON events ---
    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
        bool is_group_b = (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B);
        if (pin > JACK_OUT_6A && !is_group_b) continue; // Skip invalid enum range

        if (output_on_times[pin] != 0 && current_time >= output_on_times[pin]) {
            if (output_off_times[pin] == 0) { // Ensure it's not already ON and waiting for OFF
                set_output(pin, true);
                output_off_times[pin] = current_time + DEFAULT_PULSE_DURATION_MS; // Set off_time based on actual on_time
            }
            output_on_times[pin] = 0; // Clear the scheduled ON event
        }
    }

    // --- Check for scheduled OFF events ---
    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
        bool is_group_b = (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B);
        if (pin > JACK_OUT_6A && !is_group_b) continue; // Skip invalid enum range

        if (output_off_times[pin] != 0 && current_time >= output_off_times[pin]) {
            set_output(pin, false);
            output_off_times[pin] = 0; // Clear the scheduled OFF event
        }
    }

    // --- Schedule events on F1 Tick ---
    if (context->f1_rising_edge) {
        uint32_t beat_index = (context->f1_counter - 1) % SWING_MEASURE_BEATS; 
        uint32_t f1_tick_time = current_time; // Use current_time from context for f1 scheduling base

        const uint8_t *active_percentages_A = swing_profiles_available[current_swing_profile_index_A];
        const uint8_t *active_percentages_B = swing_profiles_available[current_swing_profile_index_B];

        // --- Group A (1A-6A) ---
        for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6A; ++pin) {
            uint32_t delay = 0;
            if (pin >= JACK_OUT_2A) { // Outputs 2A-6A get swing from profile A
                 int index_in_profile = pin - JACK_OUT_2A; 
                 if (index_in_profile < NUM_SWING_OUTPUTS) { 
                    delay = calculate_delay(beat_index, context->current_tempo_interval_ms, active_percentages_A[index_in_profile]);
                 }
            } // Output 1A (F1) has no swing, delay remains 0
            
            uint32_t trigger_on_time = f1_tick_time + delay;

            if (output_off_times[pin] == 0 && output_on_times[pin] == 0) { // If output is ready for a new trigger
                 if (delay == 0) { // No swing delay, or F1 output
                    set_output(pin, true);
                    output_off_times[pin] = trigger_on_time + DEFAULT_PULSE_DURATION_MS;
                 } else { // Swing delay applies
                    output_on_times[pin] = trigger_on_time;
                 }
            }
        }

        // --- Group B (1B-6B) ---
         for (jack_output_t pin = JACK_OUT_1B; pin <= JACK_OUT_6B; ++pin) {
            uint32_t delay = 0;
            if (pin >= JACK_OUT_2B) { // Outputs 2B-6B get swing from profile B
                 int index_in_profile = pin - JACK_OUT_2B; 
                 if (index_in_profile < NUM_SWING_OUTPUTS) { 
                     delay = calculate_delay(beat_index, context->current_tempo_interval_ms, active_percentages_B[index_in_profile]);
                 }
            } // Output 1B (F1) has no swing, delay remains 0

            uint32_t trigger_on_time = f1_tick_time + delay;

            if (output_off_times[pin] == 0 && output_on_times[pin] == 0) { // If output is ready for a new trigger
                 if (delay == 0) { // No swing delay, or F1 output
                    set_output(pin, true);
                    output_off_times[pin] = trigger_on_time + DEFAULT_PULSE_DURATION_MS;
                 } else { // Swing delay applies
                    output_on_times[pin] = trigger_on_time;
                 }
            }
        }
    } 
}

void mode_swing_reset(void) {
    memset(output_on_times, 0, sizeof(output_on_times));
    memset(output_off_times, 0, sizeof(output_off_times));
     for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
        bool is_group_b = (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B);
        if (pin > JACK_OUT_6A && !is_group_b) continue; 
        set_output(pin, false);
    }
    // Reset to default medium profiles
    current_swing_profile_index_A = 3; 
    current_swing_profile_index_B = 3; 
}

// Functions for persistence
void mode_swing_set_profile_indices(uint8_t profile_index_a, uint8_t profile_index_b) {
    if (profile_index_a < NUM_SWING_PROFILES) {
        current_swing_profile_index_A = profile_index_a;
    } else {
        current_swing_profile_index_A = 3; // Default if invalid
    }
    if (profile_index_b < NUM_SWING_PROFILES) {
        current_swing_profile_index_B = profile_index_b;
    } else {
        current_swing_profile_index_B = 3; // Default if invalid
    }
}

void mode_swing_get_profile_indices(uint8_t *p_profile_index_a, uint8_t *p_profile_index_b) {
    if (p_profile_index_a) {
        *p_profile_index_a = current_swing_profile_index_A;
    }
    if (p_profile_index_b) {
        *p_profile_index_b = current_swing_profile_index_B;
    }
}
