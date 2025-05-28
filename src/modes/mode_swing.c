		#include "mode_swing.h"
		#include "drivers/io.h"
		#include "modes.h"
		#include "main_constants.h"
		
		#include <string.h> // For memset
		
		// --- Configuration ---
		#define SWING_MEASURE_BEATS 4
		#define NUM_SWING_OUTPUTS 5 
		
		// Standard swing percentage sets for outputs 2-6
		static const uint8_t swing_percentages_set1[NUM_SWING_OUTPUTS] = { 55, 60, 66, 70, 75 }; // Normal: Group A, Swapped: Group B
		static const uint8_t swing_percentages_set2[NUM_SWING_OUTPUTS] = { 75, 70, 66, 60, 55 }; // Normal: Group B, Swapped: Group A
		
		// --- Module State ---
		static uint32_t output_on_times[NUM_JACK_OUTPUTS]; // Time when pin should turn ON
		static uint32_t output_off_times[NUM_JACK_OUTPUTS]; // Time when pin should turn OFF
		
		// Helper function to calculate swing delay
		static uint32_t calculate_delay(uint32_t beat_index, uint32_t tempo_interval, uint8_t swing_percent) {
		    if (beat_index % 2 == 0 || swing_percent <= 50) { 
		        return 0; 
		    }
		    int32_t delay_ms = ((int32_t)swing_percent - 50) * (int32_t)tempo_interval / 100;
		    return (delay_ms > 0) ? (uint32_t)delay_ms : 0; // Ensure positive delay
		}
		
		// --- Mode Interface Functions ---
		
		void mode_swing_init(void) {
		    memset(output_on_times, 0, sizeof(output_on_times));
		    memset(output_off_times, 0, sizeof(output_off_times));
		    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
		        if (((pin >= JACK_OUT_1A && pin <= JACK_OUT_6A)) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
		             set_output(pin, false);
		        }
		    }
		}
		
		void mode_swing_update(const mode_context_t* context) {
		    uint32_t current_time = context->current_time_ms;
		
		    // --- Check for scheduled ON events ---
		    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
		        if (((pin >= JACK_OUT_1A && pin <= JACK_OUT_6A)) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
		            if (output_on_times[pin] != 0 && current_time >= output_on_times[pin]) {
		                if (output_off_times[pin] == 0) { 
		                    set_output(pin, true);
		                    output_off_times[pin] = current_time + DEFAULT_PULSE_DURATION_MS;
		                }
		                output_on_times[pin] = 0;
		            }
		        }
		    }
		
		    // --- Check for scheduled OFF events ---
		    for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
		        if (((pin >= JACK_OUT_1A && pin <= JACK_OUT_6A)) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
		             if (output_off_times[pin] != 0 && current_time >= output_off_times[pin]) {
		                set_output(pin, false);
		                output_off_times[pin] = 0;
		            }
		        }
		    }
		
		    // --- Schedule events on F1 Tick ---
		    if (context->f1_rising_edge) { // Fixed field name
		        uint32_t beat_index = (context->f1_counter - 1) % SWING_MEASURE_BEATS;
		        uint32_t f1_tick_time = current_time;
		
		        // Assign percentage sets based on calc_mode
		        const uint8_t *percentages_a = (context->calc_mode == CALC_MODE_NORMAL) ? swing_percentages_set1 : swing_percentages_set2;
		        const uint8_t *percentages_b = (context->calc_mode == CALC_MODE_NORMAL) ? swing_percentages_set2 : swing_percentages_set1;
		
		        // --- Group A (1A-6A) ---
		        for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6A; ++pin) {
		            uint32_t delay = 0;
		            if (pin >= JACK_OUT_2A) {
		                 int index = pin - JACK_OUT_2A;
		                 if (index >= 0 && index < NUM_SWING_OUTPUTS) { // Added index bounds check
		                    delay = calculate_delay(beat_index, context->current_tempo_interval_ms, percentages_a[index]); // Fixed field name
		                 }
		            }
		            uint32_t trigger_on_time = f1_tick_time + delay;
		
		            if (output_off_times[pin] == 0 && output_on_times[pin] == 0) {
		                 if (delay == 0) { 
		                    set_output(pin, true);
		                    output_off_times[pin] = trigger_on_time + DEFAULT_PULSE_DURATION_MS;
		                 } else { 
		                    output_on_times[pin] = trigger_on_time;
		                 }
		            }
		        }
		
		        // --- Group B (1B-6B) ---
		         for (jack_output_t pin = JACK_OUT_1B; pin <= JACK_OUT_6B; ++pin) {
		            uint32_t delay = 0;
		            if (pin >= JACK_OUT_2B) {
		                 int index = pin - JACK_OUT_2B;
		                 if (index >= 0 && index < NUM_SWING_OUTPUTS) { // Added index bounds check
		                     delay = calculate_delay(beat_index, context->current_tempo_interval_ms, percentages_b[index]); // Fixed field name
		                 }
		            }
		            uint32_t trigger_on_time = f1_tick_time + delay;
		
		            if (output_off_times[pin] == 0 && output_on_times[pin] == 0) {
		                 if (delay == 0) { 
		                    set_output(pin, true);
		                    output_off_times[pin] = trigger_on_time + DEFAULT_PULSE_DURATION_MS;
		                 } else { 
		                    output_on_times[pin] = trigger_on_time;
		                 }
		            }
		        }
		    } // end if(f1_rising_edge)
		}
		
		void mode_swing_reset(void) {
		    memset(output_on_times, 0, sizeof(output_on_times));
		    memset(output_off_times, 0, sizeof(output_off_times));
		     for (jack_output_t pin = JACK_OUT_1A; pin <= JACK_OUT_6B; ++pin) {
		        if (((pin >= JACK_OUT_1A && pin <= JACK_OUT_6A)) || (pin >= JACK_OUT_1B && pin <= JACK_OUT_6B)) {
		             set_output(pin, false);
		        }
		    }
		}

