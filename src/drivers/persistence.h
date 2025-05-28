#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // Include the new modes header

// --- Constants ---
#define PERSISTENCE_MAGIC_NUMBER 0xDEADBEEF // Example magic number
// Define the flash START ADDRESS for storing the state.
// For STM32F411CE (512KB Flash), Sector 7 (last sector, 128KB) starts at 0x08060000.
#define PERSISTENCE_FLASH_STORAGE_ADDR 0x08060000

// --- Data Structure ---

// Define a structure to hold the persistent state
typedef struct {
    uint32_t magic_number;       // To validate data integrity
    operational_mode_t op_mode; // Last active operational mode
    // Store calculation mode for each operational mode
    calculation_mode_t calc_mode_per_op_mode[NUM_OPERATIONAL_MODES]; 
    uint32_t tempo_interval;     // Last known tempo interval
    uint32_t chaos_mode_divisor; // Specific divisor setting for Chaos mode
    uint32_t checksum;           // Simple checksum for validation
} krono_state_t;

// --- Function Prototypes ---

/**
 * @brief Initializes the persistence module (if needed, e.g., check flash page).
 */
void persistence_init(void);

/**
 * @brief Calculates a simple checksum for the given state data (excluding the checksum field itself).
 *
 * @param state Pointer to the krono_state_t struct.
 * @return uint32_t Calculated checksum.
 */
uint32_t persistence_calculate_checksum(const krono_state_t *state);

/**
 * @brief Loads the krono state from Flash memory.
 *
 * @param state Pointer to a krono_state_t struct to be filled.
 * @return true if loading was successful and data is valid, false otherwise.
 */
bool persistence_load_state(krono_state_t *state);

/**
 * @brief Saves the krono state to Flash memory.
 *
 * @param state Pointer to the krono_state_t struct containing the state to save.
 * @return true if saving was successful, false otherwise.
 */
bool persistence_save_state(const krono_state_t *state);

#endif // PERSISTENCE_H
