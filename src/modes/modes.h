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
    NUM_OPERATIONAL_MODES // Keep this last for counting
} operational_mode_t;

// Defines the two calculation variations available within certain modes.
typedef enum {
    CALC_MODE_NORMAL = 0, // Default/Primary calculation
    CALC_MODE_SWAPPED,    // Swapped/Secondary calculation
    NUM_CALCULATION_MODES // Keep this last for counting
} calculation_mode_t;

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

#endif // MODES_H
