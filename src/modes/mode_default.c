#include "modes.h"
#include "../drivers/io.h"
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h> // For memset

#include "../main_constants.h"

#define NUM_DEFAULT_FACTORED_OUTPUTS 5
static uint32_t default_factors[NUM_DEFAULT_FACTORED_OUTPUTS] = {2, 3, 4, 5, 6};
static jack_output_t group_a_outputs[NUM_DEFAULT_FACTORED_OUTPUTS] = { JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A };
static jack_output_t group_b_outputs[NUM_DEFAULT_FACTORED_OUTPUTS] = { JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B };

// State for Multiplication (independent timing)
static uint32_t next_mult_trigger_time[NUM_JACK_OUTPUTS];

// State for Division (uses f1_tick counter)
static uint32_t div_counters[NUM_JACK_OUTPUTS];

static bool waiting_for_first_f1 = false; // Flag to signal we are waiting for the first F1 tick after reset

void mode_default_init(void) {
    mode_default_reset(); // Clear state and outputs
    waiting_for_first_f1 = true; // Set the flag
}

void mode_default_update(const mode_context_t* context) {
    uint32_t current_time = context->current_time_ms;
    uint32_t tempo_interval = context->current_tempo_interval_ms;
    bool tempo_valid = (tempo_interval >= MIN_INTERVAL && tempo_interval <= MAX_INTERVAL);
    bool mult_drives_group_a = (context->calc_mode == CALC_MODE_NORMAL);

    // --- Initial Synchronization Logic ---
    if (waiting_for_first_f1) {
        if (context->f1_rising_edge && tempo_valid) {
            // First F1 tick received after reset, synchronize everything to this moment
            waiting_for_first_f1 = false;
            uint32_t first_sync_time = current_time; // Use current time as the reference

            // Reset division counters (already 0 from reset, but good practice)
            memset(div_counters, 0, sizeof(div_counters));

            // Schedule the *first* multiplication trigger for all outputs *simultaneously*
            // based on their respective intervals from this sync point.
            for (int i = 0; i < NUM_DEFAULT_FACTORED_OUTPUTS; i++) {
                uint32_t factor = default_factors[i];
                if (factor == 0) continue;

                uint32_t mult_interval = tempo_interval / factor;
                if (mult_interval < MIN_CLOCK_INTERVAL) mult_interval = MIN_CLOCK_INTERVAL;

                jack_output_t pin_a = group_a_outputs[i];
                jack_output_t pin_b = group_b_outputs[i];
                jack_output_t mult_pin = mult_drives_group_a ? pin_a : pin_b;
                jack_output_t div_pin = mult_drives_group_a ? pin_b : pin_a; // Needed for full initialization

                // Schedule first mult trigger relative to the first F1 tick time
                next_mult_trigger_time[mult_pin] = first_sync_time + mult_interval;
                
                // Ensure the other pin's timer is also initialized (might be unused if swap occurs later)
                jack_output_t other_mult_pin = mult_drives_group_a ? pin_b : pin_a;
                 next_mult_trigger_time[other_mult_pin] = first_sync_time + mult_interval; // Initialize even if not active mult now

                 // Emit first division pulse IF factor is 1 (though factors start at 2 here)
                 // The main logic below will handle factor >= 2 correctly after counter increments
                if (div_counters[div_pin] >= factor -1) { // Check if it should trigger on the *next* (this) tick
                     // The division logic below will trigger now since f1_tick is true and counter will hit factor
                }

                // Also trigger the first multiplication if its interval is 0 (or very small, effectively immediate)
                // This shouldn't happen with tempo_interval / factor but good edge case check.
                // We trigger relative to the *next* schedule time calculated above.
                 if (current_time >= next_mult_trigger_time[mult_pin]) {
                      set_output_high_for_duration(mult_pin, DEFAULT_PULSE_DURATION_MS);
                      // Reschedule based on the intended first time + interval
                      next_mult_trigger_time[mult_pin] = next_mult_trigger_time[mult_pin] + mult_interval; 
                 }

            }
            // Don't proceed further in this update cycle, wait for the next one
            return;
        } else if (!tempo_valid) {
            // If tempo becomes invalid while waiting, just keep waiting
             return;
        } else {
             // Still waiting for the first f1_tick
             return;
        }
    }

    // --- Normal Update Logic (runs after first F1 tick) ---
    if (!tempo_valid) {
        // If tempo becomes invalid, stop sending clocks but keep state.
        // Don't reset counters or next_mult_trigger_time here. Let them freeze.
        // Optionally, could reset next_mult_trigger_time far in the future.
        return;
    }

    for (int i = 0; i < NUM_DEFAULT_FACTORED_OUTPUTS; i++) {
        uint32_t factor = default_factors[i];
        if (factor == 0) continue;

        jack_output_t pin_a = group_a_outputs[i];
        jack_output_t pin_b = group_b_outputs[i];
        jack_output_t mult_pin = mult_drives_group_a ? pin_a : pin_b;
        jack_output_t div_pin = mult_drives_group_a ? pin_b : pin_a;

        // --- MULTIPLICATION LOGIC (Independent Timing) ---
        uint32_t mult_interval = tempo_interval / factor;
        if (mult_interval < MIN_CLOCK_INTERVAL) mult_interval = MIN_CLOCK_INTERVAL;

        // Check if it's time to trigger the multiplication output
        if (current_time >= next_mult_trigger_time[mult_pin]) {
             set_output_high_for_duration(mult_pin, DEFAULT_PULSE_DURATION_MS);
             // Schedule next trigger time. Add interval to the *scheduled* time to prevent drift.
             next_mult_trigger_time[mult_pin] += mult_interval;
             // Basic catch-up: if we are way behind schedule, reset next trigger relative to now + interval
              if (next_mult_trigger_time[mult_pin] < current_time) {
                  next_mult_trigger_time[mult_pin] = current_time + mult_interval;
              }
        }

        // --- DIVISION LOGIC (F1 Tick Based) ---
        if (context->f1_rising_edge) {
            div_counters[div_pin]++;
            if (div_counters[div_pin] >= factor) {
                set_output_high_for_duration(div_pin, DEFAULT_PULSE_DURATION_MS);
                div_counters[div_pin] = 0; // Reset counter *after* triggering
            }
        }
    }
    // No need to handle turning pins off, io_update_pulse_timers does it.
}

void mode_default_reset(void) {
    // Clear division counters
    memset(div_counters, 0, sizeof(div_counters));

    // Reset multiplication timers (initialize to 'now', will be properly set on first F1 tick)
    uint32_t now = millis();
    for (int i = 0; i < NUM_JACK_OUTPUTS; i++) {
         next_mult_trigger_time[i] = now; // Placeholder initialization
     }

    // Turn off outputs immediately using set_output to also clear any pending pulses in io.c
    for (int i = 0; i < NUM_DEFAULT_FACTORED_OUTPUTS; i++) {
        set_output(group_a_outputs[i], false);
        set_output(group_b_outputs[i], false);
    }
    // Note: F1 outputs (1A/1B) are handled by the clock_manager and reset elsewhere
    waiting_for_first_f1 = true; // Ensure we wait for sync on next init/activation
}
