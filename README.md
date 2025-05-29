# KRONO - Make Your Own Time

Shield: [![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

This work is licensed under a
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License][cc-by-nc-sa].

[![CC BY-NC-SA 4.0][cc-by-nc-sa-image]][cc-by-nc-sa]

[cc-by-nc-sa]: https://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-image]: https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg

## Overview
KRONO is a multimodal time control module for eurorack synthesizers, primarily implemented and tested on an **STM32F411CE** microcontroller (often found on "Black Pill" development boards, used here on the Krono PCB), though it may work on STM32F401CE with appropriate target changes. It provides tap tempo clock generation with multiple output modes and rhythmic variations, persisting its state across power cycles using internal Flash memory.

## Hardware Setup
- **PA0:** Tap Input (Button to GND)
- **PA1:** Mode/Calculation Swap Input (Button to GND)
- **PB3:** External Clock Input - *Note: Conflicts with default SWD pin SWO*
- **PB4:** External Gate Swap Input (External Gate for Calculation Swap) - *Note: Conflicts with potential JTAG pin NJTRST*
- **PA15:** Status LED Output (Indicates current operational mode with blinking, or mode change state with solid ON/OFF)
- **PA3:** Aux/Change Indicator LED Output (Blinks briefly for various events)
- **Group A Outputs (JACK_OUT_1A..6A):** Clock outputs. PB0, PB1, PA2, PB15, PB5, PB6.
- **Group B Outputs (JACK_OUT_1B..6B):** Clock outputs. PB14, PB13, PB12, PB8, PB9, PB10.
- *See `src/drivers/io.h` for specific pin mapping (`JACK_OUT_...` enums).*
- **Important:** Uses PB3 and PB4, which conflict with standard debug pins. SWD/JTAG debugging is likely unavailable when using these inputs. Upload via DFU is recommended.

## Building and Installing
Uses PlatformIO.
- **Compile:** User will run `platformio run -e blackpill_f411ce` externally.
- **Upload:** `platformio run -e blackpill_f411ce --target upload` (Uses DFU protocol. Ensure board is in DFU mode. See `KODING.md` for details)

## Project Structure
- **`src/main.c`**: Core application orchestrator.
    - Initializes the system and all modules.
    - Contains the main application loop (`while(1)`).
    - Manages the core application state (tempo, operational mode, calculation mode) based on callbacks from the Input Handler.
    - Implements callbacks for state changes triggered by the Input Handler.
    - Calls update functions for `input_handler`, `clock_manager`, and `status_led` in the main loop.
    - Manages the Aux LED (PA3) blinking via software timer in the main loop.
- **`src/drivers/`**: Hardware Abstraction Layer (HAL).
  - **`io.c`/`h`**: Handles GPIO configuration and basic output control (including Aux LED PA3).
  - **`tap.c`/`h`**: Handles Tap Tempo input detection (PA0, EXTI0) and interval measurement.
  - **`persistence.c`/`h`**: Handles saving and loading application state (`krono_state_t`) to/from internal Flash memory.
  - **`ext_clock.c`/`h`**: Handles External Clock input detection (PB3, EXTI3), validation, and interval measurement.
- **`src/modes/`**: Mode-specific logic.
  - **`modes.h`**: Defines the `operational_mode_t`, `calculation_mode_t` enums, the `mode_context_t` struct, and function prototypes for each mode.
  - **`mode_default.c`/`h`**: Implements Default (Multiplication/Division) mode.
  - **`mode_euclidean.c`/`h`**: Implements Euclidean Rhythm mode.
  - **`mode_musical.c`/`h`**: Implements Musical Ratios mode.
  - **`mode_probabilistic.c`/`h`**: Implements Probabilistic Trigger mode.
  - **`mode_sequential.c`/`h`**: Implements Sequential (Fibonacci/Primes) mode.
  - **`mode_swing.c`/`h`**: Implements Swing/Shuffle mode (per output).
  - **`mode_polyrhythm.c`/`h`**: Implements Polyrhythm mode (X:Y ratios and sum).
  - **`mode_logic.c`/`h`**: Implements Logic mode.
  - **`mode_phasing.c`/`h`**: Implements Phasing mode.
  - **`mode_chaos.c`/`h`**: Implements Chaos mode.
- **`src/util/`**: Utility functions.
  - **`delay.c`/`h`**: Provides simple blocking delay functions (`delay_ms`).
- **`src/input_handler.c`/`h`**: Manages user inputs.
    - Initializes PA0 (via `tap.c`), PA1, PB3 (via `ext_clock.c`), and PB4.
    - Handles Tap Tempo logic (averaging, validation).
    - Handles Operational Mode switching logic (new state machine for TAP hold, MOD clicks, TAP confirm).
    - Handles Calculation Mode swap logic (Mode button short press or PB4 gate high).
    - Handles External Clock detection (using validated intervals from `ext_clock.c`) and tempo override.
    - Calls registered callback functions in `main.c` for tempo, operational mode, calculation mode changes, and save requests.
- **`src/status_led.c`/`h`**: Manages the status LED (PA15).
    - Provides functions to initialize, update (based on current mode and time), and reset the blinking sequence. Includes override functionality for mode change.
- **`src/clock_manager.c`/`h`**: Manages clock generation.
    - Generates the base F1 clock (Outputs 1A/1B) based on the current tempo (either tapped or validated external).
    - Creates the `mode_context_t` struct.
    - Calls the `update()` function of the currently active operational mode (`mode_xxx_update`).
    - Provides sync functions triggered by state changes.
- **`src/main_constants.h`**: Defines shared constants used across multiple modules (timing, pins, etc.).
- **`platformio.ini`**: PlatformIO project configuration file.
- **`KODING.md`**: Development notes, build/upload commands, and code style guidelines.
- **`README.md`**: This file.

## Quick Start Guide
KRONO is designed to be the rhythmic center of your setup. Here's how to get started:

1.  **Connections:**
    *   Connect the desired outputs (Group A: 1A-6A, Group B: 1B-6B) to the modules you want to control.
    *   Connect a momentary button (normally open, connected to ground when pressed) to PA0 (Tap).
    *   Connect another momentary button to PA1 (Mode/Swap).
    *   **Optional:** Connect an external clock signal to PB3 (External Clock Input).
    *   **Optional:** Connect an external gate signal to PB4 (External Gate Swap Input).
    *   The PA15 LED (Status LED) shows the current mode with periodic blinks. During mode change, it is solid ON (or OFF when MOD is pressed).
    *   The PA3 LED (Aux LED) flashes briefly for various events like tempo changes, calculation swap, entering mode change, confirming a new mode, or state saving via timeout.
2.  **Power Up:** Apply power to the module. KRONO will automatically load the last saved state (tempo, operational mode, swap state for that mode, and specific mode parameters like Swing profile or Chaos divisor) from Flash memory.
3.  **Set the Tempo:**
    *   **Tap Tempo:** Press the Tap button (PA0) in time with the desired rhythm. After 3 valid taps (stable intervals), KRONO will calculate the average tempo and start generating clocks. The PA3 LED will flash.
    *   **External Clock:** If a stable clock signal is connected to PB3, KRONO will automatically sync to that tempo after validating a few consecutive pulses, ignoring Tap Tempo. The PA3 LED will flash.
    *   Outputs 1A and 1B always generate the main clock (tapped or external).
    *   *Note: The tempo is part of the state that gets saved, but saving only occurs as described in the "Saving / Mode Change" section below.*
4.  **Saving / Mode Change:**
    *   **Enter Mode Change:** Press and hold the Tap button (PA0) for more than `OP_MODE_TAP_HOLD_DURATION_MS` (e.g., 2 seconds). 
        *   The Status LED (PA15) will turn ON solid.
        *   The Aux LED (PA3) will blink once to indicate entry into mode change sequence.
    *   **Release TAP button:** The Status LED (PA15) remains ON solid. A 5-second timer for automatic state saving begins.
    *   **Option A: Let Save Timer Expire (No MOD press):**
        *   If you do *not* press the Mode button (PA1) within 5 seconds after releasing TAP, the currently active state (tempo, current operational mode, its calculation swap state, and any mode-specific parameters like Swing profile or Chaos divisor) will be automatically saved to Flash memory.
        *   The Aux LED (PA3) will blink once to indicate the save has occurred.
        *   The Status LED (PA15) will return to its normal blinking pattern for the current mode.
        *   This is the **only** way the general state of the module is saved.
    *   **Option B: Select a New Mode (Press MOD within 5 seconds):**
        *   If you press the Mode button (PA1) within 5 seconds of releasing TAP, the 5-second save timer is cancelled.
        *   Each time you press the Mode button (PA1):
            *   The Status LED (PA15) will turn OFF while MOD is held, and back ON solid when MOD is released.
            *   The Aux LED (PA3) will *not* blink during these individual MOD presses for mode cycling.
            *   Internally, a click counter increments to select the next operational mode (`Default -> Euclidean -> ... -> Chaos -> Default`).
        *   **Confirm New Mode:** After selecting the desired mode with PA1 clicks, press the Tap button (PA0) briefly to confirm.
            *   The new operational mode becomes active.
            *   If `op_mode_clicks_count > 0` (meaning you actually cycled through modes), the Aux LED (PA3) will blink once to indicate confirmation.
            *   The Status LED (PA15) returns to its normal blinking pattern for the newly selected mode.
            *   **Important:** Changing the operational mode this way does *not* automatically save the state. To save the new mode (and current tempo/swap state for it), you must re-enter the mode change sequence (hold TAP, release TAP) and let the 5-second save timer expire for that new mode.
    *   **Timeout during Mode Selection:** If, after pressing MOD at least once, you do not confirm with a TAP press within `OP_MODE_CONFIRM_TIMEOUT_MS` (e.g., 10 seconds), the mode change sequence is aborted, and the module reverts to the operational mode it was in before you started the mode change sequence. No state is saved.
5.  **Explore the Outputs:** (Behavior depends on the active mode; each group A/B now has 5 variable outputs {2-6} in addition to 1A/1B)
    *   **Default Mode:** Direct tempo multiplications/divisions on outputs 2-6.
    *   **Euclidean Mode:** Euclidean rhythms based on predefined K/N on outputs 2-6.
    *   **Musical Mode:** Predefined musical rhythmic ratios on outputs 2-6.
    *   **Probabilistic Mode:** Triggers based on predefined probabilities for each output 2-6.
    *   **Sequential Mode:** Clocks based on mathematical sequences relative to the main clock on outputs 2-6.
    *   **Swing Mode:** Applies a selected swing profile (percentage) to outputs 2-6, delaying the even beats. The selected swing profile (0-7, corresponding to 50% to 85% swing) is saved with the module state. To change the swing profile, use the TAP+MOD sequence to enter mode change, then, while in the Swing mode, additional MOD clicks will cycle through the 8 available swing profiles. Confirm the selection with TAP. The new profile is then active and will be saved if the 5-second save timer is allowed to expire.
    *   **Polyrhythm Mode:** Outputs 2-5 generate X:Y polyrhythms. Output 6 is the *sum* (logical OR) of the outputs 2-5 in its group.
    *   **Logic Mode:** Outputs 2-6 are logic functions (AND, OR, XOR, NAND, XNOR) of the Tap (PA0) and Mode (PA1) inputs.
    *   **Phasing Mode:** Outputs 2-6 generate pulses phased relative to the main clock (1A/1B), with different phase shifts.
    *   **Chaos Mode:** Outputs 2-6 generate rhythmic patterns based on chaotic logistic maps. The chaos divisor is saved with the module state.
6.  **Swap Groups (Calculation Swap):**
    *   **Button:** Briefly press *only* the Mode/Swap button (PA1) when *not* in the mode change sequence.
    *   **Gate:** Send a HIGH gate signal to PB4.
    *   This inverts the functions/patterns assigned to Groups A and B *within the current mode*.
    *   The PA3 LED will flash.
    *   *Note: The swap state is part of the overall state that is saved via the 5-second timeout in the mode change procedure.*

## Core Functionality (Technical Details)
### Timing and Tempo
- **System Tick:** 1ms tick via SysTick (`sys_tick_handler` in `main.c`). Provides `millis()`.
- **Tap Tempo:** Handled by `input_handler.c`.
    - Uses `tap.c` (EXTI0 on PA0) for raw tap detection.
    - Averages last `NUM_INTERVALS_FOR_AVG` valid intervals (within `MIN_INTERVAL`, `MAX_INTERVAL`, and `MAX_INTERVAL_DIFFERENCE`).
    - Calls `on_tempo_change` callback in `main.c` when a new stable tap tempo is calculated.
- **External Clock:** Handled by `input_handler.c`.
    - Uses `ext_clock.c` (EXTI3 on PB3) for raw edge detection, debounce, and interval validation.
    - Requires `NUM_EXT_INTERVALS_FOR_VALIDATION` stable consecutive intervals within range and tolerance.
    - When a stable interval is validated, `input_handler.c` detects this via `ext_clock_has_new_validated_interval()` and retrieves it using `ext_clock_get_validated_interval()`.
    - Calls `on_tempo_change` callback in `main.c` with the validated interval, overriding tap tempo.
    - Timeout logic (`ext_clock_has_timed_out`) reverts to tap tempo if external clock stops.
- **Clock Generation:** Handled by `clock_manager.c`.
    - **Base Clock (F1):** Generates pulse on `JACK_OUT_1A`/`1B` at the `active_tempo_interval_ms` rate (from tap or validated external).
    - **Mode Clocks:** Creates `mode_context_t` and calls the active `mode_xxx_update()` function.

### State Management
- **Core State:** `current_tempo_interval`, `g_current_op_mode`, `g_current_calc_mode` are key state variables primarily managed in `main.c` but updated via callbacks from `input_handler.c`.
- **Persistence:** `persistence.c`/`h` handles saving/loading the `krono_state_t` struct (containing the core state variables, mode-specific parameters like Chaos divisor and Swing profile, magic number, and checksum) to/from a designated Flash page.
    - `persistence_load_state()` called during `system_init()` in `main.c`.
    - `persistence_save_state()` is called via `save_current_state()` in `main.c`. This save operation is triggered *exclusively* by a callback (`on_save_request_from_input_handler`) from `input_handler.c` when the 5-second save timeout occurs after entering the mode change sequence (TAP hold then release, without subsequent MOD presses).

### Input Handling (`input_handler.c`)
- **Initialization:** `input_handler_init()` configures PA0 (Tap), PA1 (Mode/Swap), PB3 (Ext. Clock), PB4 (Ext. Gate Swap) and registers callbacks from `main.c` (including the `on_aux_led_blink_request_from_input_handler` and `on_save_request_from_input_handler`).
- **Tap Tempo:** `handle_taps_for_tempo()` processes taps if not in mode change sequence.
- **External Clock:** Overrides tap tempo if a valid external clock is detected. Reverts to tap tempo on timeout.
- **Op Mode Switch & Saving (`handle_op_mode_sm`):** A state machine manages this:
    1.  **Entry:** TAP held > `OP_MODE_TAP_HOLD_DURATION_MS`. Status LED (PA15) ON solid. Aux LED (PA3) blinks once.
    2.  **TAP Release:** Status LED (PA15) remains ON. A 5-second timer (`OP_MODE_TIMEOUT_SAVE_MS`) starts.
    3.  **Timeout (No MOD press):** After 5 seconds, `save_request_cb` is called (triggers save in `main.c`). Aux LED (PA3) blinks. State machine resets.
    4.  **MOD Press (within 5s):** 5-second save timer is cancelled. Status LED (PA15) OFF while MOD pressed, ON when released. `op_mode_clicks_count` increments on MOD release. Aux LED (PA3) does *not* blink for these MOD presses.
    5.  **TAP Confirm (after MOD presses):** If `op_mode_clicks_count > 0`, `op_mode_change_cb` is called (changes mode in `main.c`). Aux LED (PA3) blinks once. State machine resets. This action does *not* save state.
    6.  **Timeout (after MOD presses, no TAP confirm):** After `OP_MODE_CONFIRM_TIMEOUT_MS`, state machine resets, reverting to previous mode. No save.
- **Calc Mode Swap:**
    - **Button (PA1):** Short press when not in Op Mode Switch sequence triggers `on_calc_mode_change` callback.
    - **Gate (PB4):** Rising edge on gate input triggers `on_calc_mode_change` callback.
    - Aux LED (PA3) blinks for Calc Mode Swap.
- **Callbacks:** `input_handler.c` calls registered functions in `main.c` to effect changes or request actions (like saving or blinking Aux LED).

### External Control Inputs
- **PB3 (External Clock Input):**
    - Validated external clock overrides Tap Tempo. The tempo *is* part of the state saved, but saving only happens via the 5-second mode change timeout.
    - *Note: This pin (PB3) is also the default SWO debug pin.*
- **PB4 (External Gate Swap Input):**
    - Triggers Calculation Swap. 
    - *Note: This pin (PB4) can conflict with JTAG debugging (NJTRST).*

### Operational Modes (`modes/`)
- Each mode implements `_init()`, `_update(context)`, `_reset()`.
- **Calculation Swap:** `current_calc_mode` in `mode_context_t` dictates pattern variations.
- **Mode-Specific Parameters:** Some modes like Swing (profile index) and Chaos (divisor) have parameters that are now part of the `krono_state_t` and are saved/loaded.

### Status Indication (`status_led.c`, PA15)
- Normal operation: Blinks periodically to show current mode (1 blink for Default, etc.).
- Mode Change Sequence: Behavior controlled by `status_led_set_override()` from `input_handler.c` (Solid ON, or OFF when MOD is pressed).

### Aux/Change Indicator LED (PA3)
- Managed by `main.c` using a software timer (`status_led_pa3_blink_end_time`) and `set_output()`.
- Blinks briefly when:
    - Tempo is set (Tap or External Clock).
    - Calculation Mode is swapped.
    - Mode Change sequence is entered (TAP held > 2s).
    - Mode Change is confirmed by TAP (if a new mode was selected via MOD clicks).
    - State is saved via the 5-second timeout in the mode change sequence.

### Main Loop (`main.c`)
- Initializes modules.
- Loop: Calls `input_handler_update()`, `clock_manager_update()`, `status_led_update()`.
- Manages Aux LED (PA3) software timer.
- Manages state saving cooldown via `state_changed_for_saving` flag (set by `on_save_request_from_input_handler`) and `SAVE_STATE_COOLDOWN_MS`.
