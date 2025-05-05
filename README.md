# KRONO MODULE

Shield: [![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

This work is licensed under a
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License][cc-by-nc-sa].

[![CC BY-NC-SA 4.0][cc-by-nc-sa-image]][cc-by-nc-sa]

[cc-by-nc-sa]: https://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-image]: https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg

## Overview
KRONO is a multimodal time control module for hardware synthesizers, primarily implemented and tested on an **STM32F411CE** microcontroller (often found on "Black Pill" development boards, used here on the Krono PCB), though it may work on STM32F401CE with appropriate target changes. It provides tap tempo clock generation with multiple output modes and rhythmic variations, persisting its state across power cycles using internal Flash memory.

## Hardware Setup (STM32F411 Version)
- **PA0:** Tap Input (Button to GND)
- **PA1:** Mode/Calculation Swap Input (Button to GND)
- **PB3:** External Clock Input - *Note: Conflicts with default SWD pin SWO*
- **PB4:** External Gate Swap Input (External Gate for Calculation Swap) - *Note: Conflicts with potential JTAG pin NJTRST*
- **PA15:** Status LED Output (Indicates current operational mode)
- **PA3:** Aux/Change Indicator LED Output (Blinks briefly on tempo/mode/swap changes)
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
    - Handles Operational Mode switching logic (Tap long press + Mode clicks).
    - Handles Calculation Mode swap logic (Mode button short press or PB4 gate high).
    - Handles External Clock detection (using validated intervals from `ext_clock.c`) and tempo override.
    - Calls registered callback functions in `main.c` when tempo, operational mode, or calculation mode changes.
- **`src/status_led.c`/`h`**: Manages the status LED (PA15).
    - Provides functions to initialize, update (based on current mode and time), and reset the blinking sequence.
- **`src/clock_manager.c`/`h`**: Manages clock generation.
    - Generates the base F1 clock (Outputs 1A/1B) based on the current tempo (either tapped or validated external).
    - Creates the `mode_context_t` struct.
    - Calls the `update()` function of the currently active operational mode (`mode_xxx_update`).
    - Provides sync functions triggered by state changes.
- **`src/main_constants.h`**: Defines shared constants used across multiple modules (timing, pins, etc.).
- **`platformio.ini`**: PlatformIO project configuration file.
- **`KODING.md`**: Development notes, build/upload commands, and code style guidelines.
- **`README.md`**: This file.

## Guida Rapida all'Uso
KRONO è progettato per essere il centro ritmico del tuo setup. Ecco come iniziare:

1.  **Collegamento:**
    *   Collega le uscite desiderate (Gruppo A: 1A-6A, Gruppo B: 1B-6B) ai moduli che vuoi controllare.
    *   Collega un pulsante momentaneo (normalmente aperto, collegato a massa quando premuto) a PA0 (Tap).
    *   Collega un altro pulsante momentaneo a PA1 (Mode/Swap).
    *   **Opzionale:** Collega un segnale di clock esterno a PB3 (External Clock Input).
    *   **Opzionale:** Collega un segnale di gate esterno a PB4 (External Gate Swap Input).
    *   L'uscita LED PA15 mostra la modalità corrente con lampeggi periodici.
    *   L'uscita LED PA3 lampeggia brevemente quando il tempo, la modalità o lo stato di swap cambiano.
2.  **Accensione:** Dai alimentazione al modulo. KRONO caricherà automaticamente l'ultima modalità, tempo e stato di swap utilizzati per quella modalità dalla memoria Flash.
3.  **Imposta il Tempo:**
    *   **Tap Tempo:** Premi il pulsante Tap (PA0) a tempo con il ritmo desiderato. Dopo 3 tap validi (intervalli stabili), KRONO calcolerà il tempo medio e inizierà a generare clock. L'LED PA3 lampeggerà.
    *   **External Clock:** Se un segnale di clock stabile è collegato a PB3, KRONO si sincronizzerà automaticamente a quel tempo dopo aver validato alcuni impulsi consecutivi, ignorando il Tap Tempo. L'LED PA3 lampeggerà.
    *   Le uscite 1A e 1B generano sempre il clock principale (tapped o external).
    *   *Nota: Il tempo (sia tapped che external) viene salvato solo quando si cambia/seleziona una modalità operativa.*
4.  **Scegli la Modalità:**
    *   Tieni premuto il pulsante Tap (PA0), quindi premi il pulsante Mode (PA1) una o più volte. Rilascia il pulsante Tap per confermare. Questo fa ciclare le modalità operative: `Default -> Euclidean -> Musical -> Probabilistic -> Sequential -> Swing -> Polyrhythm -> Logic -> Phasing -> Chaos -> Default`.
    *   L'LED PA3 lampeggerà alla conferma.
    *   *Questo salverà anche il tempo corrente e lo stato di swap corrente per la modalità selezionata.*
    *   Per salvare lo stato corrente (tempo e swap) senza cambiare modalità, basta riselezionare la modalità corrente usando la stessa procedura.
    *   L'LED di stato (PA15) ti mostrerà la modalità attiva lampeggiando periodicamente: 1 volta per Default, 2 per Euclidean, ..., 10 per Chaos.
5.  **Esplora le Uscite:** (Il comportamento dipende dalla modalità attiva; ogni gruppo A/B ora ha 5 uscite variabili {2-6} oltre alla 1A/1B)
    *   **Default Mode:** Moltiplicazioni/Divisioni dirette del tempo sulle uscite 2-6.
    *   **Euclidean Mode:** Ritmi Euclidei basati su K/N predefiniti sulle uscite 2-6.
    *   **Musical Mode:** Rapporti ritmici musicali predefiniti sulle uscite 2-6.
    *   **Probabilistic Mode:** Trigger basati su probabilità predefinite per ciascuna uscita 2-6.
    *   **Sequential Mode:** Clock basati su sequenze matematiche rispetto al clock principale sulle uscite 2-6.
    *   **Swing Mode:** Applica percentuali di swing *diverse per ogni uscita* (2-6), ritardando i beat pari.
    *   **Polyrhythm Mode:** Uscite 2-5 generano poliritmi X:Y. Uscita 6 è la *somma* (OR logico) delle uscite 2-5 del suo gruppo.
    *   **Logic Mode:** Uscite 2-6 sono funzioni logiche (AND, OR, XOR, NAND, XNOR) degli ingressi Tap (PA0) e Mode (PA1).
    *   **Phasing Mode:** Uscite 2-6 generano impulsi sfasati rispetto al clock principale (1A/1B), con sfasamenti diversi.
    *   **Chaos Mode:** Uscite 2-6 generano pattern ritmici basati su mappe logistiche caotiche.
6.  **Inverti i Gruppi (Calculation Swap):**
    *   **Pulsante:** Premi brevemente *solo* il pulsante Mode/Swap (PA1).
    *   **Gate:** Invia un segnale di gate HIGH a PB4.
    *   Questo inverte le funzioni/pattern assegnate ai Gruppi A e B *all'interno della modalità corrente*.
    *   L'LED PA3 lampeggerà.
    *   *Nota: Lo stato di swap viene salvato solo quando si cambia/seleziona una modalità operativa.*
7.  **Salvataggio:** Il salvataggio dello stato (tempo, modalità operativa e stato di swap per quella modalità) avviene automaticamente *solo* quando si seleziona una modalità operativa (tramite la procedura Tap Hold + Mode Press). Non avviene al cambio di tempo (esterno o tap) o al cambio di banco (swap).

## Core Functionality (Dettagli Tecnici)
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
- **Core State:** `current_tempo_interval`, `current_op_mode`, `current_calc_mode` are static variables in `main.c`.
- **Persistence:** `persistence.c`/`h` handles saving/loading the `krono_state_t` struct (containing the core state variables plus magic number and checksum) to/from a designated Flash page.
    - `persistence_load_state()` called during `system_init()` in `main.c`.
    - `persistence_save_state()` called via `save_current_state()` helper in `main.c`. **Note:** This save operation is triggered *only* when the operational mode is selected/changed via the `on_op_mode_change` callback. Changes to tempo or calculation mode alone do *not* trigger a save.

### Input Handling (`input_handler.c`)
- **Initialization:** `input_handler_init()` configures PA1, PB4 (EXTI4) and registers callbacks from `main.c`. Calls `tap_init()` (PA0/EXTI0) and `ext_clock_init()` (PB3/EXTI3).
- **Tap Tempo:** `handle_taps_for_tempo()` processes taps detected by `tap.c` and calls `on_tempo_change` callback *only if external clock has timed out*.
- **External Clock:** `input_handler_update()` checks `ext_clock_has_timed_out()` and `ext_clock_has_new_validated_interval()`. If a new validated interval is ready, it calls `on_tempo_change` with the result from `ext_clock_get_validated_interval()`. If timed out, it reverts to tap tempo.
- **Op Mode Switch:** `handle_op_mode_switching()` polls PA0/PA1 for Tap Hold + Mode Press sequence and calls `on_op_mode_change` callback. *This callback triggers the state save.*
- **Calc Mode Swap:**
    - **Button:** `handle_button_calc_mode_swap()` polls PA1 for short press and calls `on_calc_mode_change` callback.
    - **Gate:** `exti4_isr()` handles PB4 interrupt (rising edge), sets `ext_gate_swap_requested` flag. `input_handler_update()` checks flag and calls `on_calc_mode_change` callback.
    - *Neither `on_calc_mode_change` trigger triggers a state save.*
- **Callbacks:** `on_tempo_change`, `on_op_mode_change`, `on_calc_mode_change` in `main.c` are called by the input handler. These callbacks update the core state variables and trigger the PA3 Aux LED blink by setting `status_led_pa3_blink_end_time`.

### External Control Inputs
- **PB3 (External Clock Input):**
    - Configured as an input with an interrupt (EXTI3) triggered on the rising edge.
    - The `ext_clock.c` driver measures the time between consecutive rising edges.
    - Includes debounce logic and requires multiple consecutive intervals to be stable (within `MAX_INTERVAL_DIFFERENCE`) and within the allowed tempo range (`MIN_INTERVAL` to `MAX_INTERVAL`) before validating the external clock.
    - Once validated, the `input_handler` detects this via `ext_clock_has_new_validated_interval()` and `ext_clock_get_validated_interval()`.
    - A validated external clock overrides the Tap Tempo. The `on_tempo_change` callback is called with the validated external interval.
    - If the external clock signal stops for longer than `EXT_CLOCK_TIMEOUT_MS`, `ext_clock_has_timed_out()` returns true, and the system reverts to using the last known Tap Tempo (or waits for new taps).
    - The tempo derived from the external clock *is* saved to Flash memory, but only when an operational mode selection triggers the save.
    - *Note: This pin (PB3) is also the default SWO debug pin.*
- **PB4 (External Gate Swap Input):**
    - Configured as an input with a pull-down resistor and an interrupt (EXTI4) triggered on the rising edge (when the gate signal goes HIGH).
    - The `exti4_isr()` interrupt handler in `input_handler.c` detects the rising edge on PB4.
    - It sets a flag (`ext_gate_swap_requested`).
    - The main `input_handler_update()` function checks this flag.
    - If the flag is set, it calls the `on_calc_mode_change` callback (the same one used by the PA1 button) to trigger the Calculation Swap.
    - This provides an alternative way to trigger the swap using an external gate/trigger signal instead of the PA1 button.
    - *Note: This pin (PB4) can conflict with JTAG debugging (NJTRST).*

### Operational Modes (`modes/`)
- Each mode (`mode_default.c`, `mode_euclidean.c`, ..., `mode_chaos.c`) implements:
    - `_init()`: Called when the mode becomes active.
    - `_update(context)`: Called repeatedly by `clock_manager`. Receives timing, tempo (tapped or external), calc mode, and F1 edge info via `mode_context_t`. Responsible for calculating and setting its respective output pins (Groups A/B, outputs 2-6) based on the context.
    - `_reset()`: Called by `main.c` during mode change or sync operations. Responsible for turning off its outputs and resetting internal state.
- `modes.h` defines the common interface and enums.
- **Calculation Swap:** The `current_calc_mode` within the `mode_context_t` tells the `_update()` function whether to use Set 1 or Set 2 logic/patterns for Group A vs Group B.

### Status Indication (`status_led.c`, PA15)
- `status_led_update()` manages the blinking logic based on `current_op_mode` passed from `main.c`.
- `status_led_reset()` is called during sync to restart the blink sequence.
- `status_led_set_override()` used by `input_handler` during op mode selection.

### Aux/Change Indicator LED (PA3)
- Managed by `main.c` using a simple software timer (`status_led_pa3_blink_end_time`).
- The `on_tempo_change`, `on_op_mode_change`, and `on_calc_mode_change` callbacks in `main.c` trigger a brief blink by setting the pin HIGH and recording the end time.
- The main loop (`while(1)`) checks if the blink duration has passed and sets the pin LOW.

### Main Loop (`main.c`)
- Initializes modules in `system_init()`.
- Enters `while(1)` loop:
    1. `input_handler_update()`: Processes inputs, checks external clock, potentially triggering callbacks (`on_tempo_change`, `on_op_mode_change`, `on_calc_mode_change`).
    2. `clock_manager_update()`: Updates F1 clock (based on current tempo derived from callbacks), creates context, calls active `mode_xxx_update()`.
    3. `status_led_update()`: Updates the main status LED (PA15).
    4. Checks and manages the Aux LED (PA3) software timer.
    5. Checks and manages the state save cooldown timer.
- Callbacks (`on_op_mode_change`, etc.) update the main state variables. `on_op_mode_change` also triggers the state save logic (via flag and cooldown in main loop).
- State saving logic within the main loop checks `state_changed_for_saving` flag and cooldown timer before calling `save_current_state()`.
