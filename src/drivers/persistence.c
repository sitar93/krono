#include "persistence.h"
#include "drivers/rtc.h" // Now using the simplified rtc_bkp_ functions
#include "../main_constants.h"
#include "modes/modes.h" // For operational_mode_t, calculation_mode_t
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/f4/flash.h> // For F4 specific flash defines
#include <libopencm3/cm3/nvic.h>      // For NVIC control
#include <libopencm3/stm32/timer.h>   // For timer peripheral defines (may not be needed for IRQ nums)
#include <string.h> // For memcpy, memset, memcmp
#include <stddef.h> // For size_t, offsetof
#include <stdint.h>

// Use the centrally defined storage address from the header
#define KRONO_STATE_STORAGE_ADDR PERSISTENCE_FLASH_STORAGE_ADDR
#define KRONO_STATE_MAGIC_NUMBER PERSISTENCE_MAGIC_NUMBER     // Use define from header

// Flash programming parallelism/voltage for F4
#define KRONO_FLASH_PROGRAM_PARALLELISM FLASH_CR_PROGRAM_X32

// Define the error mask manually if not present in headers
#define FLASH_SR_ALL_ERRORS (FLASH_SR_PGSERR | FLASH_SR_PGPERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR)


/**
 * @brief Helper function to determine the flash sector number from an address.
 *        This implementation is specific to STM32F401/F411 (512KB).
 *        Adjust if using different memory maps.
 *
 * @param address The address within flash memory.
 * @return The sector number (0-7), or -1 if the address is invalid.
 */
static int8_t get_flash_sector_from_address(uint32_t address) {
    if (address >= 0x08000000 && address < 0x08004000) return 0; // Sector 0, 16KB
    if (address >= 0x08004000 && address < 0x08008000) return 1; // Sector 1, 16KB
    if (address >= 0x08008000 && address < 0x0800C000) return 2; // Sector 2, 16KB
    if (address >= 0x0800C000 && address < 0x08010000) return 3; // Sector 3, 16KB
    if (address >= 0x08010000 && address < 0x08020000) return 4; // Sector 4, 64KB
    if (address >= 0x08020000 && address < 0x08040000) return 5; // Sector 5, 128KB
    if (address >= 0x08040000 && address < 0x08060000) return 6; // Sector 6, 128KB
    if (address >= 0x08060000 && address < 0x08080000) return 7; // Sector 7, 128KB (for 512KB devices like F401/F411)
    return -1; // Invalid address
}


/**
 * @brief Initializes the persistence module (if needed, e.g., BKP domain).
 */
void persistence_init(void) {
    rtc_bkp_init(); // Initialize backup domain access
}

/**
 * @brief Calculates a simple checksum for the given state data (excluding the checksum field itself).
 *
 * @param state Pointer to the krono_state_t struct.
 * @return uint32_t Calculated checksum.
 */
uint32_t persistence_calculate_checksum(const krono_state_t *state) {
    uint32_t checksum = 0;
    const uint8_t *data = (const uint8_t *)state;
    // Checksum covers all fields EXCEPT the checksum field itself
    for (size_t i = 0; i < offsetof(krono_state_t, checksum); ++i) {
        checksum += data[i];
    }
    return checksum;
}

// --- Public Function Implementations ---

bool persistence_load_state(krono_state_t *state) {
    if (!state) {
        return false;
    }

    krono_state_t stored_state;
    // Read directly from the start of the designated storage address
    memcpy(&stored_state, (void *)KRONO_STATE_STORAGE_ADDR, sizeof(krono_state_t));

    // Validate magic number
    if (stored_state.magic_number != KRONO_STATE_MAGIC_NUMBER) {
        // Invalid magic number likely means uninitialized flash or corruption.
        // Initialize state to safe defaults.
        memset(state, 0, sizeof(krono_state_t)); // Zero out the structure
        state->magic_number = KRONO_STATE_MAGIC_NUMBER;
        state->op_mode = MODE_DEFAULT; // Default operational mode
        state->tempo_interval = DEFAULT_TEMPO_INTERVAL; // Use default tempo
        // Initialize all per-mode calculation modes to default (Normal)
        for (int i = 0; i < NUM_OPERATIONAL_MODES; ++i) {
            state->calc_mode_per_op_mode[i] = CALC_MODE_NORMAL;
        }
        // state->chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT; // Remove if chaos mode removed
        state->checksum = persistence_calculate_checksum(state);
        // Attempt to save this default state back to flash
        // NOTE: Saving is bypassed if persistence_save_state itself is bypassed
        persistence_save_state(state); 
        return true; // Return true, indicating we now have a valid (default) state
    }

    // Validate checksum
    uint32_t calculated_checksum = persistence_calculate_checksum(&stored_state);
    if (calculated_checksum != stored_state.checksum) {
        // Checksum mismatch indicates corruption. Handle similarly to invalid magic number.
        memset(state, 0, sizeof(krono_state_t));
        state->magic_number = KRONO_STATE_MAGIC_NUMBER;
        state->op_mode = MODE_DEFAULT;
        state->tempo_interval = DEFAULT_TEMPO_INTERVAL;
        for (int i = 0; i < NUM_OPERATIONAL_MODES; ++i) {
            state->calc_mode_per_op_mode[i] = CALC_MODE_NORMAL;
        }
        // state->chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT; // Remove if chaos mode removed
        state->checksum = persistence_calculate_checksum(state);
        persistence_save_state(state); // Save default state
        return true; // Return true, using default state
    }

    // Data seems valid, copy it to the output struct
    memcpy(state, &stored_state, sizeof(krono_state_t));
    return true;
}

bool persistence_save_state(const krono_state_t *state) {
    if (!state) {
        return false;
    }

    // Determine the sector number from the storage address
    int8_t sector_num = get_flash_sector_from_address(KRONO_STATE_STORAGE_ADDR);
    if (sector_num < 0) {
         return false; // Invalid storage address configuration
    }

    krono_state_t state_to_save;
    memcpy(&state_to_save, state, sizeof(krono_state_t)); // Copy current state

    // Ensure magic number is set and calculate checksum *before* writing
    state_to_save.magic_number = KRONO_STATE_MAGIC_NUMBER;
    state_to_save.checksum = persistence_calculate_checksum(&state_to_save);


    // --- Restore Original Flash Code Start ---
    // --- Disable Timer Interrupts --- 
    nvic_disable_irq(NVIC_TIM2_IRQ);
    nvic_disable_irq(NVIC_TIM3_IRQ);

    flash_unlock();
    flash_clear_status_flags(); // Clear flags before operation

    // Erase the sector
    flash_erase_sector(sector_num, KRONO_FLASH_PROGRAM_PARALLELISM);
    flash_wait_for_last_operation();

    // Check for errors after erase
    bool success = true;
    if ((FLASH_SR & FLASH_SR_ALL_ERRORS) != 0) {
        success = false;
    } else {
        flash_clear_status_flags(); // Clear potential EOP flag

        // Program the data word by word (32-bit words)
        uint32_t *data_ptr = (uint32_t *)&state_to_save;
        size_t num_words = sizeof(krono_state_t) / sizeof(uint32_t);
        uint32_t address = KRONO_STATE_STORAGE_ADDR;

        // Basic check for struct size alignment
        if (sizeof(krono_state_t) % sizeof(uint32_t) != 0) {
            success = false;
        } else {
            for (size_t i = 0; i < num_words; ++i) {
                flash_program_word(address + (i * sizeof(uint32_t)), data_ptr[i]);
                flash_wait_for_last_operation();

                // Check for errors after program
                if ((FLASH_SR & FLASH_SR_ALL_ERRORS) != 0) {
                    success = false;
                    break;
                }
                flash_clear_status_flags(); // Clear potential EOP flag
            }
        }
    }

    flash_lock();

    // --- Re-enable Timer Interrupts --- 
    nvic_enable_irq(NVIC_TIM2_IRQ);
    nvic_enable_irq(NVIC_TIM3_IRQ);

    // Verification step (Performed AFTER interrupts are re-enabled)
    if (success) {
        krono_state_t verify_state;
        memcpy(&verify_state, (void *)KRONO_STATE_STORAGE_ADDR, sizeof(krono_state_t));
        if (memcmp(&state_to_save, &verify_state, sizeof(krono_state_t)) != 0) {
            success = false; // Verification failed
        }
    }

    return success;
// --- Original Flash Code End ---
}
