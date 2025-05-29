#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // Include the modes header for NUM_OPERATIONAL_MODES
#include "modes/mode_swing.h" // For NUM_SWING_PROFILES (used in main.c for validation)

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
    uint32_t checksum;           // Simple checksum for validation
} krono_state_t;

// --- Function Prototypes ---
void persistence_init(void);
uint32_t persistence_calculate_checksum(const krono_state_t *state);
bool persistence_load_state(krono_state_t *state);
bool persistence_save_state(const krono_state_t *state);

#endif // PERSISTENCE_H
