#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // Include the modes header for NUM_OPERATIONAL_MODES
#include "modes/mode_swing.h" // For NUM_SWING_PROFILES (used in main.c for validation)
#include "modes/mode_rhythm_shared.h"

// --- Constants ---
#define PERSISTENCE_MAGIC_NUMBER 0xDEADBEEF // Example magic number
#define PERSISTENCE_FLASH_STORAGE_ADDR 0x08060000

// --- Data Structure ---
typedef struct {
    uint32_t magic_number;       // To validate data integrity
    operational_mode_t op_mode; // Last active operational mode
#if SAVE_CALC_MODE_PER_OP_MODE
    calculation_mode_t calc_mode_per_op_mode[NUM_OPERATIONAL_MODES]; 
#endif
    uint32_t tempo_interval;     // Last known tempo interval
    uint32_t chaos_mode_divisor; // Specific divisor setting for Chaos mode
    uint8_t swing_profile_index_A; // Active swing profile index for MODE_SWING Group A
    uint8_t swing_profile_index_B; // Active swing profile index for MODE_SWING Group B
    uint8_t fixed_bank;            // Active bank for MODE_FIXED (0..9)
    uint8_t fixed_sequence;        // Legacy sequence field for mode 11
    bool drift_active;
    uint8_t drift_probability;
    bool drift_ramp_up;
    uint8_t fill_density;
    bool fill_ramp_up;
    bool skip_active;
    uint8_t skip_probability;
    bool skip_ramp_up;
    bool stutter_active;
    uint8_t stutter_length;
    bool stutter_ramp_up;
    uint16_t stutter_variation_mask[MODE_RHYTHM_NUM_OUTPUTS];
    bool morph_frozen;
    uint32_t morph_generation;
    uint16_t morph_patterns[MODE_RHYTHM_NUM_OUTPUTS];
    uint16_t mute_mask;
    uint8_t mute_count;
    bool mute_ramp_up;
    uint16_t mute_variation_mask[MODE_RHYTHM_NUM_OUTPUTS];
    uint8_t density_pct;
    bool density_ramp_up;
    uint32_t song_variation_seed;
    bool song_variation_pending;
    uint8_t accumulate_active_count;
    bool accumulate_add_pending;
    uint16_t accumulate_active_mask;
    uint8_t accumulate_phase_offsets[MODE_RHYTHM_NUM_OUTPUTS];
    uint16_t accumulate_variation_masks[MODE_RHYTHM_NUM_OUTPUTS];
    bool gamma_seq_freeze_frozen;
    uint8_t gamma_seq_freeze_step; /* 0..11, mode 22 sequencer (1A…6A then 1B…6B) */
    uint8_t gamma_seq_trip_pattern; /* 0..5 */
    uint8_t gamma_seq_trip_step;
    bool gamma_portals_div_on_a; /* mode 26: true = multiply path (T/k swap); false = divide (every k F1) */
    bool gamma_coin_invert;
    bool gamma_ratchet_double;
    bool gamma_antiratchet_half;
    bool gamma_startstop_muted;
    uint32_t checksum;           // Simple checksum for validation
} krono_state_t;

// --- Function Prototypes ---
void persistence_init(void);
uint32_t persistence_calculate_checksum(const krono_state_t *state);
bool persistence_load_state(krono_state_t *state);
bool persistence_save_state(const krono_state_t *state);

#endif // PERSISTENCE_H
