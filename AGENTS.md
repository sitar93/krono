# AGENTS.md - KRONO Firmware Development Guide

Guidelines for agentic coding agents in this repository.

## Project Overview

KRONO is a multi-modal rhythm generator firmware for STM32F401CE/RC microcontrollers (Black Pill boards). Built with PlatformIO and libopencm3 framework.

---

## Build / Test Commands

### Build
```bash
platformio run -e blackpill_f411ce
```
Creates `firmware.bin` in `.pio/build/blackpill_f411ce/`.

### Upload (DFU)
```bash
platformio run -e blackpill_f411ce --target upload
```
Board must be in DFU mode (hold BOOT0, press NRST, release BOOT0).

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
- **No automated tests** - all testing is manual
- After changes: build, upload, observe hardware behavior

---

## Code Style Guidelines

### Language & Framework
- **Language:** C (C11 standard)
- **Framework:** libopencm3 for STM32 hardware abstraction

### Formatting
- **Indentation:** 4 spaces (no tabs)
- **Style:** K&R variant (braces on same line)
- **Line length:** Max 120 characters

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
- **Types (typedef):** `snake_case_t` (e.g., `operational_mode_t`)
- **Enums:** Prefix with category (e.g., `MODE_DEFAULT`)
- **Prefixes:** Driver: `io_`, `tap_`, `rtc_`, `persistence_`, `ext_clock_`; Module: `input_handler_`, `status_led_`, `mode_`, `clock_manager_`

### Includes Order
1. Standard C headers (`<stdbool.h>`, `<stdint.h>`)
2. libopencm3 (`<libopencm3/stm32/...>`, `<libopencm3/cm3/...>`)
3. Local project headers (`"drivers/io.h"`)

### Types
- Use fixed-width types: `uint32_t`, `int16_t`, etc.
- Use `bool` for boolean (include `<stdbool.h>`)
- Use `enum` for related constants, `struct` for compound data

### Error Handling
- Return `bool` for success/failure, `int` for error codes
- Validate inputs, check bounds, validate pointers

### Comments
- **API docs:** Doxygen `/** ... */`
- **Inline:** `//` single-line
- **Section headers:** `// --- Section Name ---`

### Globals
- Minimize globals; use `static` for file-scope
- Use `volatile` for ISR-shared variables

---

## Codebase Structure

```
src/
├── main.c                    # Core orchestrator, main loop
├── main_constants.h         # Pin definitions, timing constants
├── variables.h              # Tunable parameters
├── input_handler.c/h        # User input, debounce
├── clock_manager.c/h        # Clock generation, mode dispatch
├── status_led.c/h           # Status LED control
├── drivers/                 # Hardware abstraction
│   ├── io.c/h              # GPIO, outputs
│   ├── tap.c/h             # Tap tempo (EXTI)
│   ├── ext_clock.c/h       # External clock (EXTI)
│   ├── persistence.c/h     # Flash save/load
│   └── rtc.c/h             # RTC backup registers
├── modes/                   # Mode-specific rhythm logic
│   ├── modes.h             # Common interface
│   ├── mode_default.c/h    # Multiplication/Division
│   ├── mode_euclidean.c/h # Euclidean rhythms
│   ├── mode_musical.c/h    # Musical ratios
│   └── ...                 # Other modes
└── util/
    └── delay.c/h           # Delay utilities
```

---

## Development Workflow

1. Edit source files in `src/`
2. Build: `platformio run -e blackpill_f411ce`
3. Fix compilation errors (watch warnings)
4. Upload: `platformio run -e blackpill_f411ce --target upload`
5. Test manually on hardware

---

## Important Notes

- No automated tests - manual hardware testing only
- No lint/format tools configured
- Use `millis()` for timing (SysTick handler)
- Keep ISR callbacks short - defer processing to main loop
- Save state to Flash periodically or on mode changes
- Compiler flags: `-Wall -Wextra -Wno-unused-parameter -O1`