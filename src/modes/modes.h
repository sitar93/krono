#ifndef MODES_H
#define MODES_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/io.h" // For jack_output_pin_t
#include "main_constants.h" // For timing constants like MIN_INTERVAL, MAX_INTERVAL

// --- Enums ---

// Defines the available operational modes.
// NOTE: The order here determines the order of cycling through modes.
//       It also corresponds to the number of blinks of the status LED.
typedef enum {
    MODE_DEFAULT = 0,
    MODE_EUCLIDEAN,
    MODE_MUSICAL,
    MODE_PROBABILISTIC,
    MODE_SEQUENTIAL,
    MODE_SWING,
    MODE_POLYRHYTHM,
    MODE_LOGIC,             // 8: Logic Gates
    MODE_PHASING,           // 9: Phasing Clocks
    MODE_CHAOS,             // 10: Deterministic Chaos
    MODE_FIXED,             // 11: Fixed Pattern Sequencer
    MODE_DRIFT,             // 12
    MODE_FILL,
    MODE_SKIP,
    MODE_STUTTER,
    MODE_MORPH,
    MODE_MUTE,
    MODE_DENSITY,
    MODE_SONG,
    MODE_ACCUMULATE,        // 20
    MODE_GAMMA_SEQUENTIAL_RESET, // 21 (Gamma): 1A…6A then 1B…6B; MOD/CV = phase reset
    MODE_GAMMA_SEQUENTIAL_FREEZE, // 22: same 12-step sweep; MOD/CV toggles freeze (hold step)
    MODE_GAMMA_SEQUENTIAL_TRIP, // 23: six trip patterns; MOD/CV cycles pattern
    MODE_GAMMA_SEQUENTIAL_FIRE, // 24: MOD 1A+6B; each F1 paired 2A+5B … 6A+1B
    MODE_GAMMA_SEQUENTIAL_BOUNCE, // 25: MOD one-shot decel A / accel B scene
    MODE_GAMMA_PORTALS, // 26: A/B held “doors”; divide vs multiply swap cadence
    MODE_GAMMA_COIN_TOSS, // 27: F1 coin flip per pair; MOD inverts probabilities
    MODE_GAMMA_RATCHET, // 28: 1A–6A mult, 1B–6B div; MOD ×2 toggle
    MODE_GAMMA_ANTI_RATCHET, // 29: same routing; MOD ÷2 (half-speed) toggle
    MODE_GAMMA_START_STOP, // 30: same routing; MOD toggles output mute latch
    NUM_OPERATIONAL_MODES // Keep this last for counting
} operational_mode_t;

// Defines the two calculation variations available within certain modes.
typedef enum {
    CALC_MODE_NORMAL = 0, // Default/Primary calculation
    CALC_MODE_SWAPPED,    // Swapped/Secondary calculation
    NUM_CALCULATION_MODES // Keep this last for counting
} calculation_mode_t;

/** MOD for modes 12–20: only short press (SINGLE). DOUBLE is unused, kept for stable enum layout. */
typedef enum {
    MOD_PRESS_EVENT_SINGLE = 0,
    MOD_PRESS_EVENT_DOUBLE
} mod_press_event_t;

// --- Structs ---

// Structure to pass context from Clock Manager to the active mode's update function.
typedef struct {
    uint32_t current_time_ms;       // Current system time in milliseconds
    uint32_t current_tempo_interval_ms; // Current tempo interval in milliseconds
    calculation_mode_t calc_mode;   // Current calculation mode (Normal/Swapped)
    bool calc_mode_changed;         // True if calc_mode changed since the last update
    bool f1_rising_edge;            // True if the base F1 clock just ticked (rising edge)
    uint32_t f1_counter;            // Counter for F1 ticks since last sync
    uint32_t ms_since_last_call;    // Milliseconds elapsed since the last call to the mode update function
    bool sync_request;              // True if an external sync (e.g., mode change, manual reset) occurred
    bool bypass_first_update;       // <<< ADDED BACK: True to skip the first update after a problematic mode is selected
} mode_context_t;


// --- Common Mode Function Prototypes ---
// These allow main.c or other modules to interact with the modes generically.

/**
 * @brief Resets the state of the currently active mode.
 * @param mode The operational mode to reset.
 */
void mode_reset_current(operational_mode_t mode);

/**
 * @brief Initializes the state of the newly selected mode.
 * @param mode The operational mode to initialize.
 */
void mode_init_current(operational_mode_t mode);

// Specific mode function prototypes (implementations are in mode_xxx.c)

// Mode Default (Multiplication/Division)
void mode_default_init(void);
void mode_default_update(const mode_context_t* context);
void mode_default_reset(void);

// Mode Euclidean
void mode_euclidean_init(void);
void mode_euclidean_update(const mode_context_t* context);
void mode_euclidean_reset(void);

// Mode Musical Ratios
void mode_musical_init(void);
void mode_musical_update(const mode_context_t* context);
void mode_musical_reset(void);

// Mode Probabilistic
void mode_probabilistic_init(void);
void mode_probabilistic_update(const mode_context_t* context);
void mode_probabilistic_reset(void);

// Mode Sequential (Fibonacci/Primes)
void mode_sequential_init(void);
void mode_sequential_update(const mode_context_t* context);
void mode_sequential_reset(void);

// Mode Swing
void mode_swing_init(void);
void mode_swing_update(const mode_context_t* context);
void mode_swing_reset(void);

// Mode Polyrhythm
void mode_polyrhythm_init(void);
void mode_polyrhythm_update(const mode_context_t* context);
void mode_polyrhythm_reset(void);

// Mode Logic Gates
void mode_logic_init(void);
void mode_logic_update(const mode_context_t* context);
void mode_logic_reset(void);

// Mode Phasing
void mode_phasing_init(void);
void mode_phasing_update(const mode_context_t* context);
void mode_phasing_reset(void);

// Mode Chaos
void mode_chaos_init(void);
void mode_chaos_update(const mode_context_t* context);
void mode_chaos_reset(void);

// Mode Fixed Pattern Sequencer
void mode_fixed_init(void);
void mode_fixed_update(const mode_context_t* context);
void mode_fixed_reset(void);

// Modes 12–20 (rhythm pattern + short MOD)
void mode_drift_init(void);
void mode_drift_update(const mode_context_t *context);
void mode_drift_reset(void);
void mode_drift_reset_step(void);
void mode_drift_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_drift_set_state(bool active, uint8_t probability, bool ramp_up);
void mode_drift_get_state(bool *active, uint8_t *probability, bool *ramp_up);

void mode_fill_init(void);
void mode_fill_update(const mode_context_t *context);
void mode_fill_reset(void);
void mode_fill_reset_step(void);
void mode_fill_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_fill_set_state(uint8_t density, bool ramp_up);
void mode_fill_get_state(uint8_t *density, bool *ramp_up);

void mode_skip_init(void);
void mode_skip_update(const mode_context_t *context);
void mode_skip_reset(void);
void mode_skip_reset_step(void);
void mode_skip_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_skip_set_state(bool active, uint8_t probability, bool ramp_up);
void mode_skip_get_state(bool *active, uint8_t *probability, bool *ramp_up);

void mode_stutter_init(void);
void mode_stutter_update(const mode_context_t *context);
void mode_stutter_reset(void);
void mode_stutter_reset_step(void);
void mode_stutter_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_stutter_set_state(bool active, uint8_t length, bool ramp_up, const uint16_t *variation_mask);
void mode_stutter_get_state(bool *active, uint8_t *length, bool *ramp_up, uint16_t *variation_mask);

void mode_morph_init(void);
void mode_morph_update(const mode_context_t *context);
void mode_morph_reset(void);
void mode_morph_reset_step(void);
void mode_morph_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_morph_set_state(bool frozen, uint32_t generation, const uint16_t *morphed);
void mode_morph_get_state(bool *frozen, uint32_t *generation, uint16_t *morphed);

void mode_mute_init(void);
void mode_mute_update(const mode_context_t *context);
void mode_mute_reset(void);
void mode_mute_reset_step(void);
void mode_mute_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_mute_set_state(uint16_t muted_mask, uint8_t mute_count, bool ramp_up, const uint16_t *variation_mask);
void mode_mute_get_state(uint16_t *muted_mask, uint8_t *mute_count, bool *ramp_up, uint16_t *variation_mask);

void mode_density_init(void);
void mode_density_update(const mode_context_t *context);
void mode_density_reset(void);
void mode_density_reset_step(void);
void mode_density_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_density_set_state(uint8_t density_pct, bool ramp_up);
void mode_density_get_state(uint8_t *density_pct, bool *ramp_up);

void mode_song_init(void);
void mode_song_update(const mode_context_t *context);
void mode_song_reset(void);
void mode_song_reset_step(void);
void mode_song_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_song_set_state(uint32_t variation_seed, bool variation_pending);
void mode_song_get_state(uint32_t *variation_seed, bool *variation_pending);

void mode_accumulate_init(void);
void mode_accumulate_update(const mode_context_t *context);
void mode_accumulate_reset(void);
void mode_accumulate_reset_step(void);
void mode_accumulate_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_accumulate_set_state(uint8_t active_count, bool add_pending, uint16_t active_mask,
                               const uint8_t *phase_offsets, const uint16_t *variation_masks);
void mode_accumulate_get_state(uint8_t *active_count, bool *add_pending, uint16_t *active_mask,
                               uint8_t *phase_offsets, uint16_t *variation_masks);

void mode_gamma_sequential_reset_init(void);
void mode_gamma_sequential_reset_update(const mode_context_t *context);
void mode_gamma_sequential_reset_reset(void);
void mode_gamma_sequential_reset_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);

void mode_gamma_sequential_freeze_init(void);
void mode_gamma_sequential_freeze_update(const mode_context_t *context);
void mode_gamma_sequential_freeze_reset(void);
void mode_gamma_sequential_freeze_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_sequential_freeze_set_state(bool frozen, uint8_t step);
void mode_gamma_sequential_freeze_get_state(bool *frozen, uint8_t *step);

void mode_gamma_sequential_trip_init(void);
void mode_gamma_sequential_trip_update(const mode_context_t *context);
void mode_gamma_sequential_trip_reset(void);
void mode_gamma_sequential_trip_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_sequential_trip_set_state(uint8_t pattern_id, uint8_t step_idx);
void mode_gamma_sequential_trip_get_state(uint8_t *pattern_id, uint8_t *step_idx);

void mode_gamma_sequential_fire_init(void);
void mode_gamma_sequential_fire_update(const mode_context_t *context);
void mode_gamma_sequential_fire_reset(void);
void mode_gamma_sequential_fire_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);

void mode_gamma_sequential_bounce_init(void);
void mode_gamma_sequential_bounce_update(const mode_context_t *context);
void mode_gamma_sequential_bounce_reset(void);
void mode_gamma_sequential_bounce_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);

void mode_gamma_portals_init(void);
void mode_gamma_portals_update(const mode_context_t *context);
void mode_gamma_portals_reset(void);
void mode_gamma_portals_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_portals_set_state(bool multiply_mode);
void mode_gamma_portals_get_state(bool *multiply_mode);

void mode_gamma_coin_toss_init(void);
void mode_gamma_coin_toss_update(const mode_context_t *context);
void mode_gamma_coin_toss_reset(void);
void mode_gamma_coin_toss_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_coin_toss_set_state(bool invert_probabilities);
void mode_gamma_coin_toss_get_state(bool *invert_probabilities);

void mode_gamma_ratchet_init(void);
void mode_gamma_ratchet_update(const mode_context_t *context);
void mode_gamma_ratchet_reset(void);
void mode_gamma_ratchet_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_ratchet_set_state(bool double_speed);
void mode_gamma_ratchet_get_state(bool *double_speed);

void mode_gamma_anti_ratchet_init(void);
void mode_gamma_anti_ratchet_update(const mode_context_t *context);
void mode_gamma_anti_ratchet_reset(void);
void mode_gamma_anti_ratchet_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_anti_ratchet_set_state(bool half_speed);
void mode_gamma_anti_ratchet_get_state(bool *half_speed);

void mode_gamma_start_stop_init(void);
void mode_gamma_start_stop_update(const mode_context_t *context);
void mode_gamma_start_stop_reset(void);
void mode_gamma_start_stop_on_mod_press(mod_press_event_t ev, uint32_t ts_ms);
void mode_gamma_start_stop_set_state(bool muted);
void mode_gamma_start_stop_get_state(bool *muted);

/** True for modes 12–20 and Gamma family: MOD short-press → mode_*_on_mod_press (not calc/fixed swap). */
#define MODE_USES_MOD_GESTURES(m) \
    (((int)(m) >= (int)MODE_DRIFT && (int)(m) <= (int)MODE_ACCUMULATE) || \
     ((int)(m) >= (int)MODE_GAMMA_SEQUENTIAL_RESET && (int)(m) < (int)NUM_OPERATIONAL_MODES))

/** Gamma: internal F1 timing stays; 1A/1B are not driven as the automatic base-clock mirror. */
#define MODE_SKIPS_AUTO_F1_CLOCK_ON_1AB(m) ((int)(m) >= (int)MODE_GAMMA_SEQUENTIAL_RESET)

void mode_dispatch_mod_press(operational_mode_t op, mod_press_event_t ev, uint32_t ts_ms);

#endif // MODES_H
