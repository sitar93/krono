# KRONO Module KODING.md

This file provides guidelines for developing the KRONO module firmware using PlatformIO and the libopencm3 framework for STM32. Targetting STM32F401CE/RC (the microcontroller used on the Krono PCB, often found on Black Pill dev boards like WeAct V3.0).

## Commands

- **Build:** `platformio run -e blackpill_f411ce`
- **Upload:** `platformio run -e blackpill_f411ce --target upload`
  - **Note:** Uses DFU protocol. Ensure board is in DFU mode (usually via BOOT0 pin/button).
  - PlatformIO should automatically detect the DFU device. May require `dfu-util` to be installed system-wide.
  - The `upload_protocol = dfu` and `board = blackpill_f411ce` settings are configured in `platformio.ini`.
- **Clean:** `platformio run -e blackpill_f411ce --target clean`
- **Monitor:** `platformio device monitor` (Check `platformio.ini` for port/speed, usually requires separate USB-Serial adapter connected to the Krono PCB's appropriate pins, as DFU uses the main USB port)
- **Lint/Format:** None configured. Match existing style. `clang-format` can be used manually if available (K&R style, 4 spaces).
- **Testing:** Manual testing only (upload and observe). No automated tests or single-test commands available. SWD debugging is likely unavailable due to PB3/PB4 pin usage.

## Code Style (libopencm3 based)

- **Language:** C
- **Formatting:** K&R style variant, 4-space indentation. Match surrounding code.
- **Naming:**
    - `snake_case` for functions/variables.
    - `UPPER_SNAKE_CASE` for macros/constants.
    - Prefix driver funcs (`io_`, `tap_`, `rtc_`, `persistence_`).
    - Prefix module funcs (`input_handler_`, `status_led_`, `mode_`, `clock_manager_`).
- **Includes:** Use `"local.h"` for project headers and `<libopencm3/stm32/...>` or `<libopencm3/cm3/...>` for framework headers.
- **Types:** Standard C types (`uint32_t`, `bool`, `float`). Use `enum`/`struct` for clarity.
- **Error Handling:** Primarily via function return values (`bool`, error codes). No structured exception handling.
- **Comments:** `/** Doxygen */` for API/function documentation, `//` for inline explanations.
- **Globals:** Minimize. Use `static` where possible. Use `volatile` for ISR-shared variables.

## Codebase Structure

- See `README.md` for a detailed breakdown. Key areas:
- `src/main.c`: Core orchestrator, main loop, state management, callbacks.
- `src/drivers/`: Hardware Abstraction Layer (GPIO, Tap Input, Persistence, RTC, Ext Clock).
- `src/modes/`: Mode-specific logic (Default, Euclidean, etc.). `modes.h` defines the common interface (`mode_context_t`, function signatures).
- `src/util/`: Utility functions (e.g., `delay.c`).
- `src/input_handler.c/h`: User input processing (Tap, Mode Switch, Calc Swap), debounce logic, callback invocation.
- `src/status_led.c/h`: Status LED control based on mode.
- `src/clock_manager.c/h`: Base clock generation (F1), mode context creation, active mode `_update()` dispatch.
- `src/main_constants.h`, `src/variables.h`: Shared constants and tunable parameters.
- `platformio.ini`: Build configuration.

## Agent Notes

- No `.cursor` or `.github/copilot-instructions.md` files found.
- Prefer `Replace` tool for files >= 100 lines or if `Edit` fails.
- Always run `platformio run -e blackpill_f411ce` after edits to check for compilation errors. Verify functionality by uploading and observing hardware behavior.
- Comment always in English.