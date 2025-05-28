#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/pwr.h> // For backup domain
#include <libopencm3/stm32/timer.h> // For TIM3
#include <libopencm3/stm32/syscfg.h> // For SYSCFG clock
#include <stdio.h>

#include "main_constants.h"
#include "variables.h"
#include "drivers/persistence.h"
#include "drivers/io.h"
#include "drivers/tap.h" // Included for tap_is_button_pressed, tap_check_timeout
#include "drivers/ext_clock.h" // Include to allow checking activity
#include "input_handler.h"
#include "clock_manager.h"
#include "status_led.h"
#include "modes/modes.h" // For NUM_OPERATIONAL_MODES etc.
// Include all mode headers for reset functions
#include "modes/mode_default.h"
#include "modes/mode_euclidean.h"
#include "modes/mode_musical.h"
#include "modes/mode_probabilistic.h"
#include "modes/mode_sequential.h"
#include "modes/mode_swing.h"
#include "modes/mode_polyrhythm.h"
#include "modes/mode_phasing.h"
#include "modes/mode_chaos.h"


// --- Global State Variables ---
static volatile uint32_t system_millis = 0;
static krono_state_t current_state;

// Function pointer arrays for mode handlers
static void (*mode_init_funcs[NUM_OPERATIONAL_MODES])(void) = {
    [MODE_DEFAULT] = mode_default_init,
    [MODE_EUCLIDEAN] = mode_euclidean_init,
    [MODE_MUSICAL] = mode_musical_init,
    [MODE_PROBABILISTIC] = mode_probabilistic_init,
    [MODE_SEQUENTIAL] = mode_sequential_init,
    [MODE_SWING] = mode_swing_init,
    [MODE_POLYRHYTHM] = mode_polyrhythm_init,
    [MODE_PHASING] = mode_phasing_init,
    [MODE_CHAOS] = mode_chaos_init
};

static void (*mode_update_funcs[NUM_OPERATIONAL_MODES])(const mode_context_t*) = {
    [MODE_DEFAULT] = mode_default_update,
    [MODE_EUCLIDEAN] = mode_euclidean_update,
    [MODE_MUSICAL] = mode_musical_update,
    [MODE_PROBABILISTIC] = mode_probabilistic_update,
    [MODE_SEQUENTIAL] = mode_sequential_update,
    [MODE_SWING] = mode_swing_update,
    [MODE_POLYRHYTHM] = mode_polyrhythm_update,
    [MODE_PHASING] = mode_phasing_update,
    [MODE_CHAOS] = mode_chaos_update
};

static void (*mode_reset_funcs[NUM_OPERATIONAL_MODES])(void) = {
    [MODE_DEFAULT] = mode_default_reset,
    [MODE_EUCLIDEAN] = mode_euclidean_reset,
    [MODE_MUSICAL] = mode_musical_reset,
    [MODE_PROBABILISTIC] = mode_probabilistic_reset,
    [MODE_SEQUENTIAL] = mode_sequential_reset,
    [MODE_SWING] = mode_swing_reset,
    [MODE_POLYRHYTHM] = mode_polyrhythm_reset,
    [MODE_PHASING] = mode_phasing_reset,
    [MODE_CHAOS] = mode_chaos_reset
};

// --- Utility Functions ---
uint32_t millis(void) {
    return system_millis;
}

// --- System Clock and Tick Setup ---
static void clock_setup(void) {
    // Use libopencm3 helper for standard 84MHz setup on F401 from 8MHz HSE
    // Assumes an 8MHz crystal is present on the Krono PCB (as is common on Black Pill boards).
    // If using internal HSI (16MHz), a different setup function would be needed.
    // Ensure HSE is ready before configuring PLL
    rcc_osc_on(RCC_HSE);
    rcc_wait_for_osc_ready(RCC_HSE);
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]); // Correct F4 setup

    // Enable peripheral clocks needed early
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC); // May be needed for OSC pins if used
    rcc_periph_clock_enable(RCC_SYSCFG); // Needed for EXTI routing on F4
    rcc_periph_clock_enable(RCC_PWR);    // Needed for Backup domain access
    rcc_periph_clock_enable(RCC_TIM3);   // For pulse timer
}

static void systick_setup(void) {
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    // SysTick frequency = AHB Clock frequency / reload value
    // For 1ms tick, reload value = AHB Clock / 1000
    uint32_t ahb_freq = rcc_ahb_frequency; // Use the correct define for AHB frequency
    systick_set_reload(ahb_freq / 1000 - 1);
    systick_interrupt_enable();
    systick_counter_enable();
}

void sys_tick_handler(void) {
    system_millis++;
    // Handle tap timeout within SysTick for better accuracy
    tap_check_timeout(system_millis);
}

// --- State Management and Callbacks ---
static void save_current_state(bool save_tempo) {
    krono_state_t state_to_save = current_state; // Work with a copy
    if (!save_tempo) {
        // If not saving tempo (e.g., using external clock), load the last saved tempo
        // to avoid overwriting it in flash with the potentially volatile external tempo.
        krono_state_t temp_load_state;
        if (persistence_load_state(&temp_load_state)) { // Load existing state
            state_to_save.tempo_interval = temp_load_state.tempo_interval;
        }
        // Ensure tempo is within valid bounds if loaded was 0
        if (state_to_save.tempo_interval < MIN_INTERVAL || state_to_save.tempo_interval > MAX_INTERVAL) {
             state_to_save.tempo_interval = DEFAULT_TEMPO_INTERVAL;
        }
    }
    persistence_save_state(&state_to_save);
}

// Callback for Input Handler when tempo changes
void on_tempo_change(uint32_t new_interval) {
    if (current_state.tempo_interval != new_interval) {
        current_state.tempo_interval = new_interval;
        // Save state only if the tempo change did NOT come from external clock
        uint32_t now = millis(); // Get current time for timeout check
        save_current_state(ext_clock_has_timed_out(now)); // Save if timed out (i.e., using tap)
        clock_manager_set_tempo(current_state.tempo_interval);
    }
}

// Callback for Input Handler when operational mode changes
void on_op_mode_change(uint8_t num_clicks) {
    uint8_t current_mode_index = (uint8_t)current_state.op_mode;
    uint8_t next_mode_index = (current_mode_index + num_clicks) % NUM_OPERATIONAL_MODES;
    current_state.op_mode = (operational_mode_t)next_mode_index;

    save_current_state(false); // Don't save tempo on mode change

    // Reset and Initialize the new mode
    if (mode_reset_funcs[current_mode_index]) {
        mode_reset_funcs[current_mode_index]();
    }
    if (mode_init_funcs[current_state.op_mode]) {
        mode_init_funcs[current_state.op_mode]();
    }

    clock_manager_sync(); // Reset clock phase on mode change
    status_led_reset();   // Reset LED sequence
    io_cancel_all_timed_pulses(); // Ensure all outputs are off
}

// Callback for Input Handler when calculation mode changes (swap)
void on_calc_mode_change(void) {
    uint8_t current_mode_idx = (uint8_t)current_state.op_mode;
    // Toggle the calculation mode for the *current* operational mode
    current_state.calc_mode_per_op_mode[current_mode_idx] =
        (current_state.calc_mode_per_op_mode[current_mode_idx] == CALC_MODE_NORMAL)
            ? CALC_MODE_SWAPPED
            : CALC_MODE_NORMAL;

    save_current_state(false); // Don't save tempo on calc mode change
    // No need to reset modes/clocks/leds, just update the state
}

// Callback for Input Handler to override status LED
void on_status_led_override(bool enable, bool state) {
    status_led_set_override(enable, state);
}


// --- Initialization ---
static void system_init(void) {
    clock_setup();
    systick_setup();
    io_init();
    pulse_timer_init(); // Init the pulse timer after IO
    persistence_init(); // Init persistence (initializes backup domain access)

    // Load persistent state or initialize to defaults
    if (!persistence_load_state(&current_state)) {
        // If loading failed critically (shouldn't happen with current implementation)
        // Re-initialize to absolute defaults (though load_state handles defaults)
        current_state.magic_number = KRONO_STATE_MAGIC_NUMBER;
        current_state.op_mode = MODE_DEFAULT;
        current_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
        for(int i=0; i<NUM_OPERATIONAL_MODES; ++i) {
             current_state.calc_mode_per_op_mode[i] = CALC_MODE_NORMAL;
        }
        // current_state.chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT; // Remove if chaos mode removed
        current_state.checksum = persistence_calculate_checksum(&current_state);
        persistence_save_state(&current_state); // Attempt to save defaults
    }
     // Ensure loaded tempo is within reasonable bounds
     if (current_state.tempo_interval < MIN_INTERVAL || current_state.tempo_interval > MAX_INTERVAL) {
         current_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
     }

    // Initialize modules with loaded/default state
    input_handler_init(on_tempo_change, on_op_mode_change, on_calc_mode_change);
    clock_manager_init(current_state.tempo_interval, mode_update_funcs);
    status_led_init();

    // Initialize the current operational mode
    if (mode_init_funcs[current_state.op_mode]) {
        mode_init_funcs[current_state.op_mode]();
    }
}

// --- Main Loop ---
int main(void) {
    system_init();

    while (1) {
        // Update Input Handler (reads buttons, checks external clock/gate)
        input_handler_update();

        // Update Clock Manager (generates base clock, calls current mode update)
        clock_manager_update(current_state.op_mode, current_state.calc_mode_per_op_mode[current_state.op_mode]);

        // Update Status LED
        status_led_update(current_state.op_mode, system_millis);

        // Simple loop delay or sleep can be added here if needed
        // delay_ms(1); // Example - avoid burning CPU if possible
    }

    return 0; // Should never reach here
}
