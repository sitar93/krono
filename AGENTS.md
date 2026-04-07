# AGENTS.md — KRONO firmware (contributors & coding agents)

This file is the **single technical reference** for anyone modifying the codebase (humans or agents). End-user documentation lives in **`README.md`**. Version history is in **`CHANGELOG.txt`**.

---

## Quick orientation

| Topic | Where |
|--------|--------|
| Usage, hardware, modes (user) | `README.md` |
| What changed per release | `CHANGELOG.txt` |
| License | `LICENSE.txt` |
| Build, style, architecture, debug | This file (`AGENTS.md`) |

---

## Project overview

KRONO is a multi-modal rhythm generator firmware for **STM32F411CE** (Black Pill / Krono PCB), built with **PlatformIO** and **libopencm3**. Target environment: `blackpill_f411ce`.

---

## Build / test commands

```bash
platformio run -e blackpill_f411ce
platformio run -e blackpill_f411ce --target upload
platformio run -e blackpill_f411ce --target clean
platformio device monitor
```

- Output: `.pio/build/blackpill_f411ce/` (`firmware.bin`, renamed post-build per `CHANGELOG.txt` / project scripts).
- **No automated tests** — verify on hardware after upload.

### Upload (DFU)

1. Hold **BOOT0** (often near USB).
2. Press and release **NRST** (reset).
3. Release **BOOT0**.
4. Device should enumerate as STM32 bootloader; run upload command.

**Windows:** If DFU fails, ensure `dfu-util` is available; Zadig/WinUSB may be needed for `STM32 BOOTLOADER`.

---

## Code style

- **Repository language:** All source code, comments, and project documentation files (`README.md`, `AGENTS.md`, `CHANGELOG.txt`, etc.) are written in **English**, even when discussion happens in other languages.
- **Language:** C (C11).
- **Indentation:** 4 spaces, no tabs.
- **Braces:** K&R (opening brace on same line).
- **Line length:** max 120 characters.
- **Names:** `snake_case` functions/variables; `UPPER_SNAKE_CASE` macros; `snake_case_t` typedefs.
- **Prefixes:** drivers `io_`, `tap_`, `rtc_`, `persistence_`, `ext_clock_`; modules `input_handler_`, `status_led_`, `mode_`, `clock_manager_`.
- **Includes:** stdlib → libopencm3 → local `"headers.h"`.
- **Comments:** Doxygen `/** */` for APIs; `//` inline; `// --- Section ---` for blocks.
- **ISR:** keep short; defer work to the main loop.
- **Lint/format:** not configured; match existing files.

---

## Repository layout (firmware)

```
src/
├── main.c                 # Orchestrator, callbacks, main loop, millis(); save/load glue for mode fields
├── main_constants.h       # Timing bounds, shared defines
├── variables.h            # Tunable parameters
├── input_handler.c/h      # Inputs, tap averaging, op-mode SM, ext clock, tempo callback dispatch
├── clock_manager.c/h      # F1 pulse, mode_context, mode dispatch
├── status_led.c/h
├── drivers/               # io, tap, ext_clock, persistence, rtc
├── modes/                 # mode_*.c, modes.c, modes.h
└── util/
platformio.ini
```

---

## Runtime data flow (tempo)

1. `tap.c` — EXTI0 on PA0: intervals → `tap_detected()` / `tap_get_interval()`.
2. `input_handler.c` — collects `NUM_INTERVALS_FOR_AVG` intervals; on stable consensus calls tempo callback; routes tap vs external clock; op-mode SM can drain tap events.
3. `main.c` — `on_tap_tempo_change` → `clock_manager_set_internal_tempo()`.
4. `clock_manager.c` — schedules F1; passes `mode_context_t` to active `mode_*_update()`.

**Current tap policy:** tempo update after the **4th tap** (three intervals collected when `NUM_INTERVALS_FOR_AVG == 3`). **Internal tap** updates interval only (`clock_manager_set_internal_tempo`); **external clock** aligns phase and emits an immediate F1 pulse.

**External clock:** `ext_clock.c` validated intervals override tap; timeout falls back to internal tempo.

---

## Important implementation notes

- `millis()` lives in `main.c` (SysTick 1 ms).
- Modes should prefer `context->current_time_ms` over raw `millis()` where timing must align with the clock manager.
- Mode-specific persisted fields are applied in `main.c` / `save_current_state()` (chaos divisor, swing indices, binary bank) alongside `krono_state_t`.
- Persistence: `krono_state_t` in `persistence.h`; flash address `0x08060000`. Save is triggered via input-handler save path (see `README.md`).

---

## Adding a new operational mode (checklist)

1. Add `MODE_*` to `operational_mode_t` in `modes.h` (before `NUM_OPERATIONAL_MODES`).
2. Add `mode_*_init/update/reset` in new `mode_*.c` / `mode_*.h`, declared in `modes.h`.
3. Register in `modes.c` (init/reset tables).
4. Register update in `clock_manager.c` (`mode_update_functions[]`).
5. Update `status_led.c` blink count for the new mode.
6. Extend `krono_state_t` and `main.c` load/save paths if the mode needs new persisted fields.
7. Update user-facing `README.md` (modes section).
8. Document the release in `CHANGELOG.txt`.
9. Build: `platformio run -e blackpill_f411ce`.

---

## Debug playbook (tempo / input)

**Boundaries:** capture (ISR / `tap.c`) → validate (`input_handler.c`) → propagate (callbacks in `main.c`) → apply (`clock_manager.c`).

**Symptoms:**

- Tempo changes “too early” → `NUM_INTERVALS_FOR_AVG`, `reset_tap_calculation_vars()` / external-clock reset paths in `input_handler.c`.
- Phase feels wrong → timestamp passed into `clock_manager_set_internal_tempo()`.
- Tap ignored after gestures → op-mode SM draining `tap_detected()` in non-idle states.
- External clock issues → `external_clock_active` gating in `input_handler_update()`.

**Manual checks:** steady taps → lock on 4th tap; ext clock priority; disconnect ext clock → fallback.

---

## Cursor / VS Code / PlatformIO IDE

If the PlatformIO sidebar stays on “initializing”, point the extension at the existing Core, e.g. in `.vscode/settings.json`:

```json
{
    "platformio-ide.useBuiltinPIOCore": false,
    "platformio-ide.customPATH": "C:\\Users\\<you>\\.platformio\\penv\\Scripts;C:\\Users\\<you>\\.platformio\\penv;${env:PATH}",
    "platformio-ide.coreExecutable": "C:\\Users\\<you>\\.platformio\\penv\\Scripts\\platformio.exe"
}
```

Use **Tasks: Run Task** for `PlatformIO: Build/Upload/Clean` if defined in `.vscode/tasks.json`. Keybindings belong in **User Keyboard Shortcuts (JSON)**, not always in the project.

---

## Release build notes

- Prefer a clean build before release: `clean` then `run`.
- Artifacts under `.pio/build/blackpill_f411ce/` (including renamed `krono_code_v*.*.*.bin` per post-build scripts).
- Version line is driven from `CHANGELOG.txt` in this project’s build helpers.

---

## Optional: PlatformIO Core in a Python venv (Linux/macOS)

```bash
python3 -m venv ~/platformio_venv
source ~/platformio_venv/bin/activate
pip install -U pip platformio
platformio --version
```

Add `~/platformio_venv/bin` to `PATH` if desired.

---

## Future mode ideas (not implemented)

Historical brainstorming lived in removed `NEXT_MODES.txt`. New concepts should be tracked in design notes or tickets before implementation.

---

## Agent workflow reminder

1. Edit `src/`.
2. `platformio run -e blackpill_f411ce`.
3. Fix warnings where reasonable.
4. Flash and test on hardware.
5. Update `CHANGELOG.txt` and `README.md` when behavior or user-visible details change.
