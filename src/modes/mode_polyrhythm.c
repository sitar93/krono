#include "mode_polyrhythm.h"
#include "drivers/io.h"
// #include "../status_led.h" // <<< Removed debug include
#include "modes.h"
#include "main_constants.h"

#include <string.h> // For memset
#include <stdint.h> // For uint8_t, uint32_t, uint64_t
#include <stdbool.h> // For bool

// --- Configuration ---

#define NUM_POLY_OUTPUTS 4 // Outputs 2-5 per group generate polyrhythms
                           // Outputs 6A/6B are sums

// Define the X (Output Beats) and Y (Base Beats) for outputs 2-5
// Set A (Defaults for Group A outputs 2A-5A)
static const uint8_t poly_x_setA[NUM_POLY_OUTPUTS] = { 3, 4, 5, 7 };
static const uint8_t poly_y_setA[NUM_POLY_OUTPUTS] = { 2, 2, 3, 4 };

// Set B (Defaults for Group B outputs 2B-5B)
static const uint8_t poly_x_setB[NUM_POLY_OUTPUTS] = { 5, 7, 6, 11 }; 
static const uint8_t poly_y_setB[NUM_POLY_OUTPUTS] = { 2, 3, 4,  4 };

// --- Module State ---
static uint32_t next_trigger_time[NUM_JACK_OUTPUTS]; // Store scheduled ON times for poly outputs 2-5
static uint32_t output_off_times[NUM_JACK_OUTPUTS];  // Store scheduled OFF times for all outputs (1-6)

// --- Mode Interface Functions ---

void mode_polyrhythm_init(void) {
    // status_led_set_override(true, false); // <<< Removed debug
    memset(next_trigger_time, 0, sizeof(next_trigger_time));
    memset(output_off_times, 0, sizeof(output_off_times));
    // Reset should handle turning pins off
    // status_led_set_override(true, true); // <<< Removed debug
}

void mode_polyrhythm_update(const mode_context_t* context) {
    uint32_t current_time = context->current_time_ms;
    uint32_t tempo_interval = context->current_tempo_interval_ms; // Fixed field name

    // --- Check for scheduled OFF events ---
    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
        // Simplified check: pin >= JACK_OUT_1A is always true
        if ((pin <= JACK_OUT_6A) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
            if (output_off_times[pin] != 0 && current_time >= output_off_times[pin]) {
                set_output(pin, false);
                output_off_times[pin] = 0;
            }
        }
    }

    // --- Trigger events for 1A/1B on F1 Tick ---
    if (context->f1_rising_edge) { // Fixed field name
         if (output_off_times[JACK_OUT_1A] == 0) {
            set_output(JACK_OUT_1A, true);
            output_off_times[JACK_OUT_1A] = current_time + DEFAULT_PULSE_DURATION_MS;
         }
         if (output_off_times[JACK_OUT_1B] == 0) {
            set_output(JACK_OUT_1B, true);
            output_off_times[JACK_OUT_1B] = current_time + DEFAULT_PULSE_DURATION_MS;
         }
    }

    // --- Trigger Polyrhythm Outputs (2-5) and Sum Outputs (6) ---
    bool trigger_6a = false;
    bool trigger_6b = false;

    // Assign ratio sets based on calculation mode
    const uint8_t *active_x_a, *active_y_a, *active_x_b, *active_y_b;
    if (context->calc_mode == CALC_MODE_NORMAL) {
        active_x_a = poly_x_setA; active_y_a = poly_y_setA;
        active_x_b = poly_x_setB; active_y_b = poly_y_setB;
    } else { 
        active_x_a = poly_x_setB; active_y_a = poly_y_setB;
        active_x_b = poly_x_setA; active_y_b = poly_y_setA;
    }

    // Group A outputs (2A-5A)
    for (jack_output_t pin = JACK_OUT_2A; pin <= JACK_OUT_5A; ++pin) {
        int index = pin - JACK_OUT_2A;
        if (index < 0 || index >= NUM_POLY_OUTPUTS) continue;

        uint8_t X = active_x_a[index];
        uint8_t Y = active_y_a[index];
        if (X == 0) continue; 

        // Use 32-bit calculation (potential overflow)
        uint32_t output_interval = (Y * tempo_interval) / X;
        if (output_interval == 0) output_interval = 1;

        if (next_trigger_time[pin] == 0 || current_time >= next_trigger_time[pin]) {
             if (output_off_times[pin] == 0) { 
                set_output(pin, true);
                output_off_times[pin] = current_time + DEFAULT_PULSE_DURATION_MS;
                trigger_6a = true;
                uint32_t scheduled_on_time = (next_trigger_time[pin] == 0) ? current_time : next_trigger_time[pin];
                next_trigger_time[pin] = scheduled_on_time + output_interval;
             } else {
                 if (next_trigger_time[pin] != 0) { 
                     next_trigger_time[pin] += output_interval;
                 } else {
                     next_trigger_time[pin] = current_time + output_interval; 
                 }
             }
        }
    }

    // Group B outputs (2B-5B)
    for (jack_output_t pin = JACK_OUT_2B; pin <= JACK_OUT_5B; ++pin) {
        int index = pin - JACK_OUT_2B;
        if (index < 0 || index >= NUM_POLY_OUTPUTS) continue;

        uint8_t X = active_x_b[index];
        uint8_t Y = active_y_b[index];
        if (X == 0) continue;

        // Use 32-bit calculation (potential overflow for slow tempos)
        uint32_t temp_interval = (uint32_t)Y * tempo_interval;
        uint32_t output_interval = temp_interval / X;
         if (output_interval == 0) output_interval = 1; 

        if (next_trigger_time[pin] == 0 || current_time >= next_trigger_time[pin]) {
             if (output_off_times[pin] == 0) { 
                set_output(pin, true);
                output_off_times[pin] = current_time + DEFAULT_PULSE_DURATION_MS;
                trigger_6b = true;
                uint32_t scheduled_on_time = (next_trigger_time[pin] == 0) ? current_time : next_trigger_time[pin];
                next_trigger_time[pin] = scheduled_on_time + output_interval;
             } else {
                 if (next_trigger_time[pin] != 0) { 
                     next_trigger_time[pin] += output_interval;
                 } else {
                     next_trigger_time[pin] = current_time + output_interval; 
                 }
             }
        }
    }
    
    // Trigger Sum Outputs (6A / 6B)
    if (trigger_6a && output_off_times[JACK_OUT_6A] == 0) {
        set_output(JACK_OUT_6A, true);
        output_off_times[JACK_OUT_6A] = current_time + DEFAULT_PULSE_DURATION_MS;
    }
    if (trigger_6b && output_off_times[JACK_OUT_6B] == 0) {
        set_output(JACK_OUT_6B, true);
        output_off_times[JACK_OUT_6B] = current_time + DEFAULT_PULSE_DURATION_MS;
    }
}

void mode_polyrhythm_reset(void) {
    memset(next_trigger_time, 0, sizeof(next_trigger_time));
    memset(output_off_times, 0, sizeof(output_off_times));
    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
         // Simplified check: pin >= JACK_OUT_1A is always true
         if ((pin <= JACK_OUT_6A) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
             set_output(pin, false);
        }
    }
}
