#include "clock_manager.h"
#include "drivers/io.h"
#include "drivers/ext_clock.h" // Include external clock driver
#include "modes/modes.h"     // Includes mode types and specific update functions
// Include all specific mode headers that define the update functions
#include "modes/mode_default.h"
#include "modes/mode_euclidean.h"
#include "modes/mode_musical.h"
#include "modes/mode_probabilistic.h"
#include "modes/mode_sequential.h"
#include "modes/mode_swing.h"
#include "modes/mode_polyrhythm.h"
#include "modes/mode_phasing.h"
#include "modes/mode_chaos.h"

#include "main_constants.h"  // For DEFAULT_PULSE_DURATION_MS
#include "util/delay.h"      // For millis()
#include <stddef.h>          // For NULL

// --- Module State ---
static mode_context_t current_mode_context;
static operational_mode_t current_op_mode = MODE_DEFAULT;

// Tempo & Timing (Internal state)
static uint32_t active_tempo_interval_ms = DEFAULT_TEMPO_INTERVAL;
static uint32_t last_f1_pulse_time_ms = 0;
static uint32_t last_update_time_ms = 0; // For ms_since_last_call
static uint32_t f1_tick_counter = 0; // Counter for mode context
static bool sync_requested = false; // Flag for mode sync
static bool calc_mode_just_changed = false; // Flag for mode calc switch

// --- Helper Functions ---

static void generate_f1_pulse(void) {
    // Generate fixed duration pulse on outputs 1A and 1B
    set_output_high_for_duration(JACK_OUT_1A, DEFAULT_PULSE_DURATION_MS);
    set_output_high_for_duration(JACK_OUT_1B, DEFAULT_PULSE_DURATION_MS);
}

// Array of function pointers for mode update functions
static void (*mode_update_functions[NUM_OPERATIONAL_MODES])(const mode_context_t*) = {
    [MODE_DEFAULT]       = mode_default_update,
    [MODE_EUCLIDEAN]     = mode_euclidean_update,
    [MODE_MUSICAL]       = mode_musical_update,
    [MODE_PROBABILISTIC] = mode_probabilistic_update,
    [MODE_SEQUENTIAL]    = mode_sequential_update,
    [MODE_SWING]         = mode_swing_update,
    [MODE_POLYRHYTHM]    = mode_polyrhythm_update,
    [MODE_LOGIC]         = mode_logic_update,
    [MODE_PHASING]       = mode_phasing_update,
    [MODE_CHAOS]         = mode_chaos_update // <-- Added Chaos mode update
};

// --- Public Function Implementations ---

void clock_manager_init(operational_mode_t initial_op_mode, uint32_t initial_tempo_interval) {
    current_op_mode = initial_op_mode;
    mode_init_current(current_op_mode); // Use the correct init function
    active_tempo_interval_ms = initial_tempo_interval;
    last_f1_pulse_time_ms = millis(); // Initialize to prevent immediate pulse
    f1_tick_counter = 0;

    // Initialize context (some parts will be updated each cycle)
    current_mode_context.f1_rising_edge = false;
    current_mode_context.current_time_ms = last_f1_pulse_time_ms;
    current_mode_context.current_tempo_interval_ms = initial_tempo_interval; // Use parameter
    current_mode_context.calc_mode = CALC_MODE_NORMAL; // Will be updated by main
    current_mode_context.f1_counter = f1_tick_counter;
    current_mode_context.calc_mode_changed = false;
    current_mode_context.sync_request = false;
    current_mode_context.ms_since_last_call = 0;
    current_mode_context.bypass_first_update = false; // <<< ADDED BACK: Initialize flag
    last_update_time_ms = last_f1_pulse_time_ms;
}

void clock_manager_set_internal_tempo(uint32_t interval_ms, bool is_external_clock, uint32_t event_timestamp_ms) {
    if (interval_ms > 0) {
        active_tempo_interval_ms = interval_ms;
        if (is_external_clock) {
            // Align F1 pulse to the external clock event timestamp
            last_f1_pulse_time_ms = event_timestamp_ms;
            f1_tick_counter = 0; // Reset counter on external sync
            generate_f1_pulse(); // Generate immediate pulse to align
            // The next pulse will be scheduled based on the new active_tempo_interval_ms from this event_timestamp_ms
        } else {
            // For tap tempo or internal changes, we might not want to reset phase immediately,
            // or reset it to 'now' if that's the desired behavior.
            // Current behavior: just update interval, phase continues unless a sync_flags is called.
            // If phase reset is needed for tap: last_f1_pulse_time_ms = event_timestamp_ms (which would be 'now' from input_handler for tap)
        }
    }
}

uint32_t clock_manager_get_current_tempo_interval(void) {
    return active_tempo_interval_ms;
}

void clock_manager_set_operational_mode(operational_mode_t new_mode) {
    if (new_mode != current_op_mode) {
        mode_reset_current(current_op_mode); // Reset the old mode
        current_op_mode = new_mode;
        mode_init_current(current_op_mode); // Initialize the new mode
        f1_tick_counter = 0; // Reset counter on mode change

        // <<< ADDED BACK: Set flag to bypass first update for specific modes >>>
        if (new_mode == MODE_MUSICAL || new_mode == MODE_POLYRHYTHM) {
            current_mode_context.bypass_first_update = true;
        } else {
            current_mode_context.bypass_first_update = false;
        }
    }
}

void clock_manager_update(void) {
    uint32_t now = millis();
    bool f1_tick_this_cycle = false;
    uint32_t ms_since_last_update = now - last_update_time_ms;

    // No longer check external clock events here. Tempo is managed via callbacks from input_handler.

    // --- Run Internal Timer ---
    // Check if the internal timer is due based on the *current* active interval
    if ((now - last_f1_pulse_time_ms >= active_tempo_interval_ms)) {
        generate_f1_pulse();
        last_f1_pulse_time_ms = now; // Could also use += interval for drift correction
        f1_tick_this_cycle = true;
        f1_tick_counter++;
    }

    // --- Update Mode Context ---
    current_mode_context.current_time_ms = now;
    current_mode_context.current_tempo_interval_ms = active_tempo_interval_ms;
    // current_mode_context.calc_mode is updated by clock_manager_set_calc_mode
    current_mode_context.calc_mode_changed = calc_mode_just_changed; // Pass flag
    current_mode_context.f1_rising_edge = f1_tick_this_cycle;
    current_mode_context.f1_counter = f1_tick_counter;
    current_mode_context.ms_since_last_call = ms_since_last_update;
    current_mode_context.sync_request = sync_requested; // Pass flag

    // --- Call Active Mode Update (or bypass if flagged) ---
    if (current_mode_context.bypass_first_update) {
        current_mode_context.bypass_first_update = false; // Reset flag and skip update this cycle
    } else {
        if (mode_update_functions[current_op_mode] != NULL) {
            mode_update_functions[current_op_mode](&current_mode_context);
        }
    }

    // Reset sync/calc mode flags after they have been processed (or bypassed)
    sync_requested = false;
    calc_mode_just_changed = false;

    // Update time for next cycle
    last_update_time_ms = now;
}

// This is the old simple sync, now replaced by the one below
// void clock_manager_sync(void) {
//     last_f1_pulse_time_ms = millis();
//     f1_tick_counter = 0;
// }

/**
 * @brief Sets flags to indicate a sync event should occur on the next update cycle.
 * Called by main.c when op mode or calc mode changes.
 */
void clock_manager_sync_flags(bool is_calc_mode_change) {
    sync_requested = true;
    calc_mode_just_changed = is_calc_mode_change;
    // Reset F1 counter immediately upon sync request?
    f1_tick_counter = 0;
    // Reset internal pulse timer as well?
    // last_f1_pulse_time_ms = millis(); // Maybe not, let mode handle sync
}


void clock_manager_set_calc_mode(calculation_mode_t new_mode) {
    current_mode_context.calc_mode = new_mode;
}
