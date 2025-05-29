#include "persistence.h"
#include "main_constants.h"
#include "modes/mode_chaos.h" // For CHAOS_DIVISOR_DEFAULT, CHAOS_DIVISOR_MIN, CHAOS_DIVISOR_STEP
#include "modes/mode_swing.h" // For NUM_SWING_PROFILES 

#include <libopencm3/stm32/flash.h>
#include <string.h> // For memcpy and memset

// Helper function to get a default state (not exposed in header)
static krono_state_t get_default_krono_state(void) {
    krono_state_t default_state;
    default_state.magic_number = PERSISTENCE_MAGIC_NUMBER;
    default_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
    default_state.op_mode = MODE_DEFAULT;
#if SAVE_CALC_MODE_PER_OP_MODE
    for (int i = 0; i < NUM_OPERATIONAL_MODES; i++) {
        default_state.calc_mode_per_op_mode[i] = CALC_MODE_NORMAL;
    }
#endif
    default_state.swing_profile_index_A = 3; // Default to Medium swing for Group A
    default_state.swing_profile_index_B = 3; // Default to Medium swing for Group B
    default_state.chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT;
    
    default_state.checksum = 0; // Checksum must be calculated last over a zeroed checksum field
    default_state.checksum = persistence_calculate_checksum(&default_state);
    return default_state;
}

void persistence_init(void) {
    // No specific initialization needed for now.
}

uint32_t persistence_calculate_checksum(const krono_state_t *state) {
    uint32_t checksum = 0;
    const uint8_t *p_data = (const uint8_t *)state;
    for (size_t i = 0; i < offsetof(krono_state_t, checksum); ++i) {
        checksum += p_data[i];
    }
    return checksum;
}

bool persistence_load_state(krono_state_t *state) {
    if (!state) return false;

    const krono_state_t *flash_state = (const krono_state_t *)PERSISTENCE_FLASH_STORAGE_ADDR;

    if (flash_state->magic_number != PERSISTENCE_MAGIC_NUMBER) {
        *state = get_default_krono_state();
        return false; 
    }

    uint32_t calculated_checksum = persistence_calculate_checksum(flash_state);
    if (flash_state->checksum != calculated_checksum) {
        *state = get_default_krono_state();
        return false; 
    }

    memcpy(state, flash_state, sizeof(krono_state_t));
    
    if (state->op_mode >= NUM_OPERATIONAL_MODES) state->op_mode = MODE_DEFAULT;
    if (state->tempo_interval < MIN_INTERVAL || state->tempo_interval > MAX_INTERVAL) {
        state->tempo_interval = DEFAULT_TEMPO_INTERVAL;
    }
    // Use CHAOS_DIVISOR_DEFAULT as max saveable for now if CHAOS_DIVISOR_MAX_SAVEABLE is not defined
    if (state->chaos_mode_divisor < CHAOS_DIVISOR_MIN || 
        state->chaos_mode_divisor > CHAOS_DIVISOR_DEFAULT || 
        (state->chaos_mode_divisor % CHAOS_DIVISOR_STEP != 0)) {
        state->chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT;
    }
    if (state->swing_profile_index_A >= NUM_SWING_PROFILES) state->swing_profile_index_A = 3;
    if (state->swing_profile_index_B >= NUM_SWING_PROFILES) state->swing_profile_index_B = 3;

#if SAVE_CALC_MODE_PER_OP_MODE
    for (int i = 0; i < NUM_OPERATIONAL_MODES; i++) {
        if (state->calc_mode_per_op_mode[i] > CALC_MODE_SWAPPED) {
            state->calc_mode_per_op_mode[i] = CALC_MODE_NORMAL;
        }
    }
#endif
    // Ensure the loaded state also has a correct checksum if any field was corrected by validation.
    // state->checksum = persistence_calculate_checksum(state);

    return true; 
}

bool persistence_save_state(const krono_state_t *state) {
    if (!state) return false;

    krono_state_t state_to_write = *state;
    state_to_write.checksum = 0; // Zero out checksum field before calculating
    state_to_write.checksum = persistence_calculate_checksum(&state_to_write);

    flash_unlock();
    
    // Erase sector 7 (0x08060000 - 0x0807FFFF for STM32F411CE)
    // FLASH_PROGRAM_X32 for F4 is (2 << 8), for erase psize is not used this way in erase func
    // flash_erase_sector takes sector number and voltage range (which implies psize for erase)
    // Voltage range 3 (2.7-3.6V) => PSIZE = x32 for erase.
    // The libopencm3 function `flash_erase_sector` for F4 takes (sector_idx, voltage_idx)
    // where voltage_idx corresponds to PSIZE. For 2.7-3.6V, voltage_idx is 2 (FLASH_PSIZE_X32 for erase).
    // Let's assume voltage range allows x32 parallelism. The erase func handles psize.
    flash_erase_sector(7, 2); // Sector 7, Voltage range index 2 (PSIZE x32 for erase)
    
    uint32_t flash_sr_status;
    do {
        flash_sr_status = FLASH_SR;
    } while ((flash_sr_status & FLASH_SR_BSY));

    if (flash_sr_status & (FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_WRPERR)) {
        flash_lock();
        return false; // Erase error
    }
    FLASH_CR |= FLASH_CR_PG; // Enable programming

    uint32_t *p_src = (uint32_t *)&state_to_write;
    uint32_t flash_addr = PERSISTENCE_FLASH_STORAGE_ADDR;
    size_t num_words = (sizeof(krono_state_t) + sizeof(uint32_t) - 1) / sizeof(uint32_t); // Ceiling division

    for (size_t i = 0; i < num_words; ++i) {
        flash_program_word(flash_addr + (i * sizeof(uint32_t)), p_src[i]);
        do {
            flash_sr_status = FLASH_SR;
        } while ((flash_sr_status & FLASH_SR_BSY));
        if (flash_sr_status & (FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_WRPERR)) {
            FLASH_CR &= ~FLASH_CR_PG; // Disable programming on error
            flash_lock();
            return false; // Programming error
        }
    }
    
    FLASH_CR &= ~FLASH_CR_PG; // Disable programming after successful write
    flash_lock();

    krono_state_t verify_state;
    memcpy(&verify_state, (const void *)PERSISTENCE_FLASH_STORAGE_ADDR, sizeof(krono_state_t));
    if (memcmp(&state_to_write, &verify_state, sizeof(krono_state_t)) == 0) {
        return true; 
    } else {
        return false; 
    }
}
