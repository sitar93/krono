# AGENTS.md - KRONO Firmware Development Guide

This file provides guidelines for agentic coding agents operating in this repository.

## Project Overview

KRONO is a multi-modal rhythm generator firmware for STM32F401CE/RC microcontrollers (Black Pill boards). Built with PlatformIO and libopencm3 framework.

---

## Build / Test Commands

### Build (Compile)
```bash
platformio run -e blackpill_f411ce
```
Compiles the source code and creates `firmware.bin` in `.pio/build/blackpill_f411ce/`.

### Upload (DFU)
```bash
platformio run -e blackpill_f411ce --target upload
```
Uploads firmware via DFU protocol. Board must be in DFU mode (hold BOOT0, press NRST, release BOOT0).

### Clean Build
```bash
platformio run -e blackpill_f411ce --target clean
```
Removes `.pio` directory. Use when encountering strange build errors.

### Serial Monitor
```bash
platformio device monitor
```
View device output (requires separate USB-Serial adapter).

### Testing
- **No automated tests exist**. All testing is manual.
- After code changes: build, upload to hardware, and observe behavior.
- No single test command available.

### Linting/Formatting
- **No lint/format tools configured**.
- Manually use `clang-format` if available (K&R style, 4 spaces).
- Always run `platformio run -e blackpill_f411ce` after edits to verify compilation.

---

## Code Style Guidelines

### Language & Framework
- **Language:** C (C11 standard)
- **Framework:** libopencm3 for STM32 hardware abstraction

### Formatting
- **Indentation:** 4 spaces (no tabs)
- **Style:** K&R variant (braces on same line, multi-line function arguments indented)
- **Line length:** Max 120 characters preferred
- **Newlines:** Unix-style (`\n`)

Example:
```c
void example_function(int argument_one,
                     int argument_two)
{
    if (condition) {
        do_something();
    } else {
        do_something_else();
    }
}
```

### Naming Conventions
- **Functions/variables:** `snake_case` (e.g., `clock_manager_set_tempo`)
- **Macros/constants:** `UPPER_SNAKE_CASE` (e.g., `MAX_OUTPUTS`)
- **Types (typedef):** `snake_case_t` or `snake_case` (e.g., `operational_mode_t`)
- **Enums:** Prefix with relevant category (e.g., `MODE_DEFAULT`, `CALC_MODE_NORMAL`)
- **Prefixes:**
  - Driver functions: `io_`, `tap_`, `rtc_`, `persistence_`, `ext_clock_`
  - Module functions: `input_handler_`, `status_led_`, `mode_`, `clock_manager_`

### Includes
- **Local headers:** Use `"path/to/local.h"` (e.g., `"drivers/io.h"`)
- **Framework headers:** Use `<libopencm3/stm32/...>` or `<libopencm3/cm3/...>`
- **Standard headers:** Use `<stdbool.h>`, `<stdint.h>`, etc.

Order:
1. Standard C headers (`<stdbool.h>`, `<stdint.h>`)
2. libopencm3 framework headers (`<libopencm3/...>`)
3. Local project headers (`"drivers/io.h"`, etc.)

### Types
- Use standard fixed-width types: `uint32_t`, `int16_t`, etc.
- Use `bool` for boolean values (include `<stdbool.h>`)
- Use `enum` for related constants
- Use `struct` for compound data types
- Use `typedef` for opaque types or function pointers

### Error Handling
- Return values: `bool` for success/failure, `int` for error codes
- No exception handling (C does not support it)
- Validate inputs at function boundaries
- Use defensive programming: check bounds, validate pointers

### Comments
- **API/Function documentation:** Doxygen style `/** ... */`
- **Inline explanations:** `//` single-line comments
- **Section headers:** `// --- Section Name ---` for logical divisions
- Always comment in English

Example:
```c
/**
 * @brief Initialize the clock manager with default tempo.
 * @param initial_tempo_ms Initial tempo interval in milliseconds.
 * @return true if initialization successful, false otherwise.
 */
bool clock_manager_init(uint32_t initial_tempo_ms);
```

### Global Variables
- Minimize global variables
- Use `static` for file-scope globals
- Use `volatile` for variables shared between ISR and main code
- Prefix globals with `g_` only when necessary for disambiguation

### Code Organization
- Header files (`.h`): Declarations, documentation, macros, types
- Implementation files (`.c`): Definitions
- One header + one source file per logical module

---

## Codebase Structure

```
src/
├── main.c                    # Core orchestrator, main loop, state management
├── main_constants.h         # Fundamental constants (pins, timing)
├── variables.h              # Tunable parameters
├── input_handler.c/h        # User input processing, debounce
├── clock_manager.c/h        # Clock generation, mode dispatch
├── status_led.c/h           # Status LED control
├── drivers/                 # Hardware abstraction layer
│   ├── io.c/h              # GPIO, outputs, timer pulses
│   ├── tap.c/h             # Tap tempo input (EXTI)
│   ├── ext_clock.c/h       # External clock input (EXTI)
│   ├── persistence.c/h     # Flash memory save/load
│   └── rtc.c/h             # RTC backup registers
├── modes/                   # Mode-specific rhythm logic
│   ├── modes.h             # Common interface (mode_context_t, signatures)
│   ├── mode_default.c/h    # Default mode
│   ├── mode_euclidean.c/h  # Euclidean rhythms
│   ├── mode_chaos.c/h      # Chaos mode
│   ├── mode_swing.c/h      # Swing mode
│   └── ...                 # Other modes
└── util/
    └── delay.c/h           # Utility delay functions
```

---

## Development Workflow

1. **Make changes** to source files in `src/`
2. **Build** with `platformio run -e blackpill_f411ce`
3. **Fix compilation errors** (pay attention to warnings)
4. **Upload** to hardware: `platformio run -e blackpill_f411ce --target upload`
5. **Test manually** by observing hardware behavior

---

## Important Notes

- No automated tests - all verification is manual hardware testing
- No lint/format tools - match existing code style manually
- SWD debugging likely unavailable (PB3/PB4 pins used for other functions)
- Save state to Flash periodically or on mode changes
- Use `millis()` for timing (provided by SysTick handler)
- Keep ISR callbacks short - do minimal work, defer processing to main loop
