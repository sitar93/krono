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
#include "modes/mode_fixed.h"

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
static bool external_clock_slaved = false;

static bool pending_tap_quadruple_boundary = false;
static uint32_t pending_tap_quadruple_interval_ms = 0;
static uint32_t pending_tap_quadruple_t0_ms = 0;
/** Validating-edge F1 already pulsed in begin_external_slave; mode update deferred to clock_manager_update. */
static bool pending_ext_slave_validate_mode = false;

// --- Helper Functions ---

static void generate_f1_pulse(void) {
    /* Echo window starts even if Omega skips 1A/1B GPIO (still a logical F1). */
    ext_clock_note_local_f1_pulse(millis());
    if (!MODE_SKIPS_AUTO_F1_CLOCK_ON_1AB(current_op_mode)) {
        set_output_high_for_duration(JACK_OUT_1A, DEFAULT_PULSE_DURATION_MS);
        set_output_high_for_duration(JACK_OUT_1B, DEFAULT_PULSE_DURATION_MS);
    }
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
    [MODE_CHAOS]         = mode_chaos_update,
    [MODE_FIXED]         = mode_fixed_update,
    [MODE_DRIFT]         = mode_drift_update,
    [MODE_FILL]          = mode_fill_update,
    [MODE_SKIP]          = mode_skip_update,
    [MODE_STUTTER]       = mode_stutter_update,
    [MODE_MORPH]         = mode_morph_update,
    [MODE_MUTE]          = mode_mute_update,
    [MODE_DENSITY]       = mode_density_update,
    [MODE_SONG]          = mode_song_update,
    [MODE_ACCUMULATE]    = mode_accumulate_update,
    [MODE_GAMMA_SEQUENTIAL_RESET] = mode_gamma_sequential_reset_update,
    [MODE_GAMMA_SEQUENTIAL_FREEZE] = mode_gamma_sequential_freeze_update,
    [MODE_GAMMA_SEQUENTIAL_TRIP] = mode_gamma_sequential_trip_update,
    [MODE_GAMMA_SEQUENTIAL_FIRE] = mode_gamma_sequential_fire_update,
    [MODE_GAMMA_SEQUENTIAL_BOUNCE] = mode_gamma_sequential_bounce_update,
    [MODE_GAMMA_PORTALS] = mode_gamma_portals_update,
    [MODE_GAMMA_COIN_TOSS] = mode_gamma_coin_toss_update,
    [MODE_GAMMA_RATCHET] = mode_gamma_ratchet_update,
    [MODE_GAMMA_ANTI_RATCHET] = mode_gamma_anti_ratchet_update,
    [MODE_GAMMA_START_STOP] = mode_gamma_start_stop_update
};

static uint32_t snap_phase_to_grid(uint32_t t0, uint32_t now, uint32_t interval_ms) {
    /* t0 == 0 is a valid millis() value after wrap; only reject future anchors. */
    if (interval_ms == 0u || t0 > now) {
        return now;
    }
    uint32_t late = now - t0;
    uint32_t k = late / interval_ms;
    return t0 + k * interval_ms;
}

static void run_mode_update(uint32_t now, bool f1_tick_this_cycle) {
    uint32_t ms_since_last_update = now - last_update_time_ms;

    current_mode_context.current_time_ms = now;
    current_mode_context.current_tempo_interval_ms = active_tempo_interval_ms;
    current_mode_context.calc_mode_changed = calc_mode_just_changed;
    current_mode_context.f1_rising_edge = f1_tick_this_cycle;
    current_mode_context.f1_counter = f1_tick_counter;
    current_mode_context.ms_since_last_call = ms_since_last_update;
    current_mode_context.sync_request = sync_requested;

    if (current_mode_context.bypass_first_update) {
        current_mode_context.bypass_first_update = false;
    } else if (mode_update_functions[current_op_mode] != NULL) {
        mode_update_functions[current_op_mode](&current_mode_context);
    }

    sync_requested = false;
    calc_mode_just_changed = false;
    last_update_time_ms = now;
}

static void apply_external_pulse(uint32_t edge_ms) {
    uint32_t now = millis();
    uint32_t t = edge_ms;
    if (t > now) {
        t = now;
    }
    last_f1_pulse_time_ms = t;
    generate_f1_pulse();
    f1_tick_counter++;
}

// --- Public Function Implementations ---

void clock_manager_init(operational_mode_t initial_op_mode, uint32_t initial_tempo_interval) {
    current_op_mode = initial_op_mode;
    mode_init_current(current_op_mode); // Use the correct init function
    active_tempo_interval_ms = initial_tempo_interval;
    last_f1_pulse_time_ms = millis(); // Initialize to prevent immediate pulse
    f1_tick_counter = 0;
    external_clock_slaved = false;

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

void clock_manager_arm_tap_quadruple_boundary(uint32_t interval_ms, uint32_t event_timestamp_ms) {
    if (interval_ms > 0) {
        pending_tap_quadruple_boundary = true;
        pending_tap_quadruple_interval_ms = interval_ms;
        pending_tap_quadruple_t0_ms = event_timestamp_ms;
    }
}

void clock_manager_set_external_clock_slaved(bool slaved) {
    external_clock_slaved = slaved;
}

void clock_manager_begin_external_slave(uint32_t interval_ms, uint32_t validating_edge_ms) {
    if (interval_ms == 0u) {
        return;
    }

    active_tempo_interval_ms = interval_ms;

    /*
     * Hard-follow Clock In: arm edge→F1 forwarding for subsequent non-echo edges.
     * Internal timer keeps running as flywheel (see clock_manager_update).
     * Validating edge gets exactly one F1 here; mode update runs in clock_manager_update.
     */
    ext_clock_arm_slave_forwarding();
    external_clock_slaved = true;

    apply_external_pulse(validating_edge_ms);
    pending_ext_slave_validate_mode = true;
}

void clock_manager_end_external_slave(void) {
    ext_clock_disarm_slave_forwarding();
    external_clock_slaved = false;
    pending_ext_slave_validate_mode = false;
}

void clock_manager_set_internal_tempo(uint32_t interval_ms, bool is_external_clock, uint32_t event_timestamp_ms) {
    (void)is_external_clock;
    if (interval_ms > 0) {
        uint32_t now = millis();
        uint32_t t0 = event_timestamp_ms;
        if (t0 > now) {
            t0 = now;
        }
        active_tempo_interval_ms = interval_ms;

        if (external_clock_slaved) {
            /* Follow mode: accept tempo updates; phase stays on last ext/internal F1. */
            return;
        }

        /*
         * Internal/tap fallback: align phase to the anchor on the tempo grid.
         * Tap boundaries use clock_manager_arm_tap_quadruple_boundary instead.
         */
        last_f1_pulse_time_ms = snap_phase_to_grid(t0, now, interval_ms);
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
    bool frame_had_f1 = false;
    bool did_ext_edge_mode = false;

    if (external_clock_slaved) {
        if (pending_ext_slave_validate_mode) {
            pending_ext_slave_validate_mode = false;
            run_mode_update(now, true);
            frame_had_f1 = true;
            did_ext_edge_mode = true;
        }
        uint32_t edge_ms = 0;
        while (ext_clock_consume_pulse(&edge_ms)) {
            uint32_t t = edge_ms;
            if (t > now) {
                t = now;
            }
            apply_external_pulse(t);
            /* Use edge time so queued bursts keep distinct phase for mult/div modes. */
            run_mode_update(t, true);
            frame_had_f1 = true;
            did_ext_edge_mode = true;
        }
    }

    /*
     * Internal flywheel always armed: while hard-following, ext edges reset last_f1 so this
     * path only fires when a beat is missed (or when not following). Autopatch stays alive.
     */
    if (pending_tap_quadruple_boundary) {
        pending_tap_quadruple_boundary = false;
        uint32_t t0 = pending_tap_quadruple_t0_ms;
        if (t0 > now) {
            t0 = now;
        }
        active_tempo_interval_ms = pending_tap_quadruple_interval_ms;
        last_f1_pulse_time_ms = snap_phase_to_grid(t0, now, active_tempo_interval_ms);
        generate_f1_pulse();
        f1_tick_counter += 1u;
        frame_had_f1 = true;
    } else if (active_tempo_interval_ms > 0 &&
               (now - last_f1_pulse_time_ms) >= active_tempo_interval_ms) {
        last_f1_pulse_time_ms += active_tempo_interval_ms;
        generate_f1_pulse();
        f1_tick_counter++;
        frame_had_f1 = true;
    }

    if (external_clock_slaved) {
        if (!did_ext_edge_mode && frame_had_f1) {
            run_mode_update(now, true);
        }
        /*
         * Time-based mult needs non-F1 updates between edges so fast ext clocks do not starve.
         */
        run_mode_update(now, false);
    } else {
        run_mode_update(now, frame_had_f1);
    }
}

/**
 * @brief Sets flags to indicate a sync event should occur on the next update cycle.
 * Called by main.c when op mode or calc mode changes.
 */
void clock_manager_sync_flags(bool is_calc_mode_change) {
    sync_requested = true;
    calc_mode_just_changed = is_calc_mode_change;
    f1_tick_counter = 0;
}

void clock_manager_restart_beat_phase_now(void) {
    uint32_t now = millis();
    last_f1_pulse_time_ms = now;
    f1_tick_counter = 0;
    sync_requested = true;
    calc_mode_just_changed = false;
}


void clock_manager_set_calc_mode(calculation_mode_t new_mode) {
    current_mode_context.calc_mode = new_mode;
}

void clock_manager_clear_calc_mode_changed(void) {
    calc_mode_just_changed = false;
}
