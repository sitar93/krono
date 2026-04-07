# KRONO Knowledge Base

Quick reference guide for development. Does not replace reading the code, but contains essential information for understanding how to operate.

---

## General Architecture

### Main Flow
```
main() 
  → system_init() 
    → persistence_load_state()    # Load state from Flash
    → clock_manager_init()       # Initialize clock manager
    → input_handler_init()       # Initialize input handler
    → status_led_init()          # Initialize status LED
    
  → while(1) loop
    → input_handler_update()     # Read input, handle tap/mode
    → clock_manager_update()     # Generate F1, call active mode
    → status_led_update()        # Update status LED
```

### Critical Dependencies
- `millis()` is defined in `main.c` (SysTick 1ms)
- All modes use `mode_context_t` passed from `clock_manager`
- Callbacks register state changes to `main.c`

---

## Main Global Variables

### In main.c
```c
krono_state_t current_state;           // Persistent state
operational_mode_t g_current_op_mode;  // Current mode (MODE_DEFAULT, etc.)
calculation_mode_t g_current_calc_mode;// CALC_MODE_NORMAL or CALC_MODE_SWAPPED
bool state_changed_for_saving;          // Save flag
```

### In clock_manager.c
```c
mode_context_t current_mode_context;    // Context passed to modes
operational_mode_t current_op_mode;    // Active mode
uint32_t active_tempo_interval_ms;     // Active tempo interval (tap or ext)
uint32_t f1_tick_counter;              // F1 counter (0, 1, 2, ...)
```

### In mode_context_t (passed to modes)
```c
uint32_t current_time_ms;              // Current millis()
uint32_t current_tempo_interval_ms;    // Tempo interval
calculation_mode_t calc_mode;          // Normal or Swapped
bool calc_mode_changed;                // True if PA1 pressed (swap)
bool f1_rising_edge;                   // True if F1 ticked this cycle
uint32_t f1_counter;                  // F1 counter since last sync
uint32_t ms_since_last_call;           // Time passed since last update
bool sync_request;                     // True if sync requested (mode change)
bool bypass_first_update;              // Skip first update (some modes)
```

---

## Mode Structure

Each mode must implement:

```c
// mode_mymode.h
#ifndef MODE_MYMODE_H
#define MODE_MYMODE_H
#include "modes.h"  // For mode_context_t

void mode_mymode_init(void);
void mode_mymode_update(const mode_context_t* context);
void mode_mymode_reset(void);
#endif

// mode_mymode.c
#include "mode_mymode.h"
#include "../drivers/io.h"
#include "../main_constants.h"

void mode_mymode_init(void) {
    mode_mymode_reset();
}

void mode_mymode_update(const mode_context_t* context) {
    // MAIN LOGIC
    // - Check context->f1_rising_edge for base clock
    // - Check context->calc_mode for swap
    // - Use set_output_high_for_duration() for trigger
    // - Use context->current_tempo_interval_ms for timing
}

void mode_mymode_reset(void) {
    // Turn off all outputs controlled by this mode
    // Reset internal state variables
}
```

### Registration in modes.c
Add to `mode_reset_functions[]` and `mode_init_functions[]`.

### Registration in clock_manager.c
Add to `mode_update_functions[]`:
```c
[MODE_MYMODE] = mode_mymode_update,
```

### Define enum in modes.h
```c
typedef enum {
    MODE_DEFAULT = 0,
    // ... other modes ...
    MODE_MYMODE,  // Before NUM_OPERATIONAL_MODES
    NUM_OPERATIONAL_MODES
} operational_mode_t;
```

---

## Useful Functions for Modes

### Output Management
```c
// Timed pulse (fixed duration from variables.h)
set_output_high_for_duration(jack_output_t output, DEFAULT_PULSE_DURATION_MS);

// Immediate on/off state (for LED or toggle)
set_output(jack_output_t output, bool state);

// Turn off all outputs (utility)
io_all_outputs_off();
io_cancel_all_timed_pulses();
```

### Output Mapping
```c
// Defined in drivers/io.h
JACK_OUT_1A = 0,  // PB0
JACK_OUT_2A,      // PB1
JACK_OUT_3A,      // PA2
JACK_OUT_4A,      // PB15
JACK_OUT_5A,      // PB5
JACK_OUT_6A,      // PB6

JACK_OUT_1B,      // PB14
JACK_OUT_2B,      // PB13
JACK_OUT_3B,      // PB12
JACK_OUT_4B,      // PB8
JACK_OUT_5B,      // PB9
JACK_OUT_6B,      // PB10
```

### Timing
```c
// Constants in main_constants.h
MIN_INTERVAL = 33ms      // ~1818 BPM max
MAX_INTERVAL = 6000ms    // 10 BPM min
MIN_CLOCK_INTERVAL = 10ms

// In variables.h
DEFAULT_PULSE_DURATION_MS = 10  // Trigger duration

// Current time
uint32_t now = context->current_time_ms;
```

### Timing Calculation
```c
// Clock-independent timing division
uint32_t interval = tempo / factor;
if (interval < MIN_CLOCK_INTERVAL) interval = MIN_CLOCK_INTERVAL;

// F1 counter for sync timing
if (context->f1_rising_edge) {
    f1_counter++;
    if (f1_counter % factor == 0) {
        // Trigger every 'factor' F1 ticks
    }
}
---

## Calc Mode (Swap)

The `calc_mode` determines which parameter set to use:

```c
bool use_set_a = (context->calc_mode == CALC_MODE_NORMAL);

// Quick ternary alternative
const uint32_t* active_factors = use_set_a ? factors_set1 : factors_set2;
```

---

## Input State Machine Handling (Do not touch unless bug)

### Tap Tempo
- 3 taps averaged with stability validation (within MAX_INTERVAL_DIFFERENCE)
- Timeout TAP_TIMEOUT_MS = 10s resets sequence
- External clock has priority over tap tempo

### Mode Change
1. Hold TAP > OP_MODE_TAP_HOLD_DURATION_MS (1s)
2. Status LED turns ON solid
3. Release TAP
4. Press MOD to cycle modes (0-10 clicks)
5. Press TAP to confirm
6. Without confirmation, 10s timeout restores previous mode

### State Saving
- Only after 5s timeout in mode change (without pressing MOD)
- Or explicit (future)

---

## Important Constants

### In variables.h
```c
#define DEFAULT_PULSE_DURATION_MS  10    // Trigger duration
#define SAVE_CALC_MODE_PER_OP_MODE 1     // Save swap per mode
#define STATUS_LED_BASE_INTERVAL_MS 200  // LED blink timing
#define OP_MODE_TAP_HOLD_DURATION_MS 1000 // Hold for mode change
#define OP_MODE_MULTI_PRESS_WINDOW_MS 500 // Window for multiple clicks
#define SAVE_STATE_COOLDOWN_MS 1000      // Save cooldown
```

### In main_constants.h
```c
#define NUM_INTERVALS_FOR_AVG 3       // Taps for average
#define MAX_INTERVAL_DIFFERENCE 3000   // Max tap difference
#define MIN_INTERVAL 33                 // ~1818 BPM
#define MAX_INTERVAL 6000              // 10 BPM
#define MIN_CLOCK_INTERVAL 10          // Min output interval
#define DEFAULT_TEMPO_INTERVAL 857      // 70 BPM
```

---

## Common Mistakes to Avoid

### 1. Not resetting state
Every mode MUST have a `_reset()` that:
- Turns off all outputs it manages
- Resets internal counters

### 2. Not handling f1_rising_edge
Modes must check `context->f1_rising_edge` to operate in sync with F1, otherwise:
- Every cycle will call the logic (too fast)
- Each mode has different requirements

### 3. Using millis() directly instead of context->current_time_ms
Always use `context->current_time_ms` for consistency and sync timing.

### 4. Not handling invalid tempo
If `context->current_tempo_interval_ms` is out of range, do not generate output.

### 5. Not declaring functions in modes.h
All mode functions must be declared in `modes.h` as part of the common interface.

---

## Debug

### Status LED (PA15)
- Each mode has a different blink pattern
- 1 blink = DEFAULT, 2 = EUCLIDEAN, ..., 11 = BINARY

### Aux LED (PA3)
- Blinks for: tempo change, calc mode change, entering mode change, mode confirm

### Serial Monitor
```bash
platformio device monitor
```
(Requires separate USB-Serial adapter)

---

## Adding New Mode - Checklist

1. Add enum in `modes.h` (before `NUM_OPERATIONAL_MODES`)
2. Create `mode_mymode.c` and `.h`
3. Implement `_init()`, `_update()`, `_reset()`
4. Add initialization in `modes.c`:
   - `mode_reset_functions[MODE_MYMODE] = mode_mymode_reset;`
   - `mode_init_functions[MODE_MYMODE] = mode_mymode_init;`
5. Add in `clock_manager.c`:
   - Include header
   - `mode_update_functions[MODE_MYMODE] = mode_mymode_update;`
6. Update `status_led.c` for new blink count
7. Update `MODES_DESCRIPTIONS.txt`
8. Build and test

---

## Mode-Specific Notes

### DEFAULT
- Group A: multiplications (x2-x6)
- Group B: divisions (/2-/6)
- Uses independent timing + F1 counter

### EUCLIDEAN
- Fixed K/N per output
- Uses Bjorklund algorithm (step % N calculation)
- F1 rising edge only

### MUSICAL
- Numerator/denominator ratios
- First update bypass (flag in context)

### SWING
- 8 profiles (0-7), 50% = no swing
- PA1 cycles profiles when active
- Delay on odd beats

### POLYRHYTHM
- X:Y ratios
- Output 6 = logical OR sum
- First update bypass

### CHAOS
- Lorenz system (sigma=10, rho=28, beta=8/3)
- PA1 decrements divisor
- Group A = X threshold, Group B = Y/Z threshold

### BINARY
- 16-step binary pattern sequencer (Mode 11)
- Outputs 2A-6A and 2B-6B (10 channels)
- Output mapping: 2=Kick, 3=Snare, 4=Clap, 5=Open HH, 6=Closed HH
- Group A and B have slightly different patterns for variation
- 10 unique banks (0-9) with different styles (Techno, Industrial, Trance, Minimal, Breakbeat, etc.)
- Sequencer runs at 4x speed relative to main clock (16 steps in 4 clock cycles)
- MOD button press or CV gate (PB4) cycles through the 10 banks
- Bank change applies at end of current 16-step cycle

---

## Persistence

### krono_state_t (in persistence.h)
```c
typedef struct {
    uint32_t magic_number;         // 0xDEADBEEF
    operational_mode_t op_mode;
    calculation_mode_t calc_mode_per_op_mode[NUM_OPERATIONAL_MODES];
    uint32_t tempo_interval;
    uint32_t chaos_mode_divisor;
    uint8_t swing_profile_index_A;
    uint8_t swing_profile_index_B;
    uint8_t binary_bank;           // Bank for BINARY mode (0-9)
    uint8_t binary_sequence;      // Reserved (set to 0)
    uint32_t checksum;
} krono_state_t;
```

### Flash Address
`0x08060000` (sector 7, 64KB)

### Saving
- Only via 5s timeout after mode change
- Never automatic on mode change

---

## Useful Links

- `src/main.c`: Main loop, callbacks
- `src/clock_manager.c`: Mode dispatch, F1 generation
- `src/input_handler.c`: Input state machine
- `src/drivers/io.h`: Pin definitions
- `src/modes/modes.h`: Shared enums and structs
- `src/variables.h`: Tunable parameters
- `platformio.ini`: Build config
