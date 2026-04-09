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

- Output: `.pio/build/blackpill_f411ce/` — after a successful build, post-scripts produce **`krono_code_<VERSION>.bin`**, **`.elf`**, **`.hex`** (version from the last non-empty line of **`CHANGELOG.txt`**, currently **v1.3.2**).
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
├── main.c                 # Orchestrator, callbacks, main loop, PA3 soft blink + pattern pump, millis(); save/load glue for mode fields
├── main_constants.h       # Timing bounds, shared defines
├── variables.h            # Tunable parameters
├── krono_aux_led_pattern.c/h  # Optional multi-pulse PA3 sequences (future tiers); cooperates with main soft-blink timer
├── input_handler.c/h      # Inputs, tap averaging, op-mode SM (+ Omega), ext clock, tempo dispatch, short-MOD mode actions (12–20)
├── clock_manager.c/h      # F1 pulse, mode_context, mode dispatch
├── status_led.c/h
├── drivers/               # io, tap (incl. tap_abort_capture), ext_clock, persistence, rtc
├── modes/                 # mode_*.c, modes.c/h, mode_mod_dispatch.c, mode_rhythm_shared.c/h
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

### Omega — selecting operational modes 11–20 (v1.3.2+)

Omega reuses the **same** op-mode change state machine as modes 1–10 (`handle_op_mode_sm` in `input_handler.c`). There is **no** long-press on Mode to enter a separate UI.

| Path | Condition | Confirm callback |
|------|-----------|------------------|
| Base | User qualifies with Tap hold ≥ `OP_MODE_TAP_HOLD_DURATION_MS`, releases Tap **before** `OP_MODE_TAP_OMEGA_HOLD_MS` from **first** press | `op_mode_change_cb(N)` with N = 1…10 |
| Omega | User **keeps holding** Tap until `OP_MODE_TAP_OMEGA_HOLD_MS` (~2 s from first press; see `variables.h`) | `op_mode_change_cb(N + 10)`; N wrapped to 1…10 if click count exceeds 10 |

**Timings** (`variables.h`): `OP_MODE_TAP_HOLD_DURATION_MS`, `OP_MODE_TAP_OMEGA_HOLD_MS`, `OP_MODE_TAP_OMEGA_MAX_HOLD_MS` (abort if Tap never released within this window from press start). For **multi-pulse** Aux (not used for Omega today): `AUX_LED_MULTI_PULSE_ON_MS`, `AUX_LED_MULTI_PULSE_GAP_MS` with `krono_aux_led_pattern_start()` in `krono_aux_led_pattern.c`.

**PA3 feedback**

- At **1 s** qualify and at **Omega arm**: `aux_led_blink_request_cb()` → `main.c` `pa3_soft_blink_arm()` (PA3 on + `status_led_pa3_blink_end_time`, typically +100 ms). `pa3_soft_blink_arm()` calls `krono_aux_led_pattern_cancel()` so a queued multi-pulse pattern does not fight a new soft blink.
- **Multi-pulse** (e.g. a future hold tier): `krono_aux_led_pattern_start(pulses, on_ms, gap_ms)`; `krono_aux_led_pattern_pump(now)` in the main loop; `krono_aux_led_pattern_active()` — while true, skip the generic PA3-off timeout. `krono_aux_led_cancel_soft_timer()` (in `main.c`) clears the soft deadline when a pattern starts.

**`main.c` coordination**

- Single-file hook `krono_aux_led_cancel_soft_timer()` — clears `status_led_pa3_blink_end_time`; used by `krono_aux_led_pattern.c` when starting a pattern.

**Interaction with modes 12–20 short MOD**

`handle_button_calc_mode_swap()` runs only when `current_op_mode_sm_state == INPUT_SM_IDLE` (and other gates). During op-mode change (including Omega), short MOD is consumed by the op-mode SM, not the rhythm-mode gesture path — unchanged from pre-Omega behavior.

### Modes 12–20 (short MOD path)

- **`MODE_USES_MOD_GESTURES(m)`** in `modes.h` — true for `MODE_DRIFT` … `MODE_ACCUMULATE`.
- **`handle_button_calc_mode_swap()`** in `input_handler.c`: short MOD release (within `CALC_SWAP_MAX_PRESS_DURATION_MS`) → `mod_press_cb(MOD_PRESS_EVENT_SINGLE)`.
- **`mod_press_cb`** wired in `main.c` → `mode_dispatch_mod_press()` → per-mode `mode_*_on_mod_press()`.
- **Timing:** `CALC_SWAP_MAX_PRESS_DURATION_MS` (short press) and `CALC_SWAP_COOLDOWN_MS` — in `variables.h`.
- **No MOD+TAP combo path:** modes 12–20 intentionally do not consume TAP for secondary actions; tap-tempo remains independent.
- **Persistence contract:** each mode in 12–20 exposes `mode_*_set_state(...)` / `mode_*_get_state(...)` (or equivalent getter for flags) so `mode_state.c` can restore and capture MOD-driven runtime state through `krono_state_t`.
- **Current behavior profile (firmware contract):**
  - `MODE_DRIFT`: elastic loop with stronger stochastic drift mutations.
  - `MODE_FILL`: drastic loop (`0..50->0`), low values intentionally sparse with kick emphasis.
  - `MODE_SKIP`: elastic probability loop.
  - `MODE_STUTTER`: drastic loop (`2->4->8->2`) plus micro-randomization after each full stutter cycle.
  - `MODE_MORPH`: continuous generative stream; MOD freezes current state, next MOD resumes to next generated state.
  - `MODE_MUTE`: random additive/subtractive mute flow; unmute introduces slight per-channel variation.
  - `MODE_DENSITY`: drastic loop (`0..200->0`) with regeneration at each density step.
  - `MODE_SONG`: bars `1-6` generated base loop, bars `7-8` generated variation; MOD schedules a brand-new base loop seed.
  - `MODE_ACCUMULATE`: automatic accumulation with drastic reset at max; each new activation applies slight random loop variation on that output; MOD toggles freeze/unfreeze.

---

## Important implementation notes

- `millis()` lives in `main.c` (SysTick 1 ms).
- Modes should prefer `context->current_time_ms` over raw `millis()` where timing must align with the clock manager.
- Mode-specific persisted fields are applied in `main.c` / `save_current_state()` (chaos divisor, swing indices, fixed-mode bank, and modes 12–20 MOD states) alongside `krono_state_t`.
- Persistence: `krono_state_t` in `persistence.h`; flash address `0x08060000`. Save is triggered via input-handler save path (see `README.md`).
- **LEDs (PA15 status, PA3 aux)** — see **`README.md` → LEDs: behavior vs firmware** for the user-facing table. Implementation summary:
  - **PA15:** `status_led.c` → `set_led(bool on)` → `set_output(STATUS_LED_PIN, on)` (logical **on** = GPIO **high** on the pin as driven by `io.c`). Pattern state machine: `pending_off_gap_ms` chooses gap after each pulse — `STATUS_LED_INTER_PULSE_OFF_MS` after a normal (N) pulse, `STATUS_LED_AFTER_LONG_OFF_MS` after a long (L) pulse; end-of-loop dark time uses `STATUS_LED_SEQUENCE_GAP_MS`. Pulse shapes: `status_led_pulse_count()` / `status_led_pulse_is_long()` for user modes 1–20; ON times `STATUS_LED_BASE_INTERVAL_MS` (N) and `STATUS_LED_LONG_ON_MS` (L). If a board shows light when the code intends dark, invert mapping inside `set_led()` only.
  - **PA3:** `main.c` `pa3_soft_blink_arm()` and optional `krono_aux_led_pattern_pump()` / `krono_aux_led_pattern_active()`; Omega uses the same soft blink as qualify via `aux_led_blink_request_cb()`.

---

## Adding a new operational mode (checklist)

1. Add `MODE_*` to `operational_mode_t` in `modes.h` (before `NUM_OPERATIONAL_MODES`).
2. Add `mode_*_init/update/reset` in new `mode_*.c` / `mode_*.h`, declared in `modes.h`.
3. Register in `modes.c` (init/reset tables).
4. Register update in `clock_manager.c` (`mode_update_functions[]`).
5. Update `status_led.c` (`status_led_pulse_count` / `status_led_pulse_is_long`) so the new user mode index matches the intended N/L pattern (see existing rules for modes 1–20).
6. Extend `krono_state_t` and `main.c` load/save paths if the mode needs new persisted fields.
7. If the mode uses short MOD actions like 12–20: extend `MODE_USES_MOD_GESTURES`, add `mode_*_on_mod_press`, register in **`mode_mod_dispatch.c`**, implement at least `MOD_PRESS_EVENT_SINGLE`.
8. Update user-facing **`README.md`** (modes table + input notes if behavior differs).
9. Document the release in **`CHANGELOG.txt`**.
10. Build: `platformio run -e blackpill_f411ce`.

---

## Debug playbook (tempo / input)

**Boundaries:** capture (ISR / `tap.c`) → validate (`input_handler.c`) → propagate (callbacks in `main.c`) → apply (`clock_manager.c`).

**Symptoms:**

- Tempo changes “too early” → `NUM_INTERVALS_FOR_AVG`, `reset_tap_calculation_vars()` / external-clock reset paths in `input_handler.c`.
- Phase feels wrong → timestamp passed into `clock_manager_set_internal_tempo()`.
- Tap ignored after gestures → op-mode SM draining `tap_detected()` in non-idle states.
- Modes 12–20 MOD short ignored → check `CALC_SWAP_MAX_PRESS_DURATION_MS`, cooldown (`last_calc_swap_trigger_time`), `INPUT_SM_IDLE` gating, and `external_clock_active` early return.
- Multi-pulse Aux looks like one long blink if fired as two quick `aux_led_blink_request_cb()` calls → use `krono_aux_led_pattern_start()` + pump + `krono_aux_led_pattern_active()` guard in `main.c`.
- Status LED “off” segments look lit, or mode 20 shows too many flashes → check `set_led()` vs LED wiring (logical on/off vs GPIO); verify `pending_off_gap_ms` and pulse indices in `status_led_update()`.
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
- Artifacts under `.pio/build/blackpill_f411ce/` (including renamed `krono_code_v*.*.*.{bin,elf,hex}` from post-build scripts).
- Version string is taken from the **last non-empty line** of **`CHANGELOG.txt`** (see `scripts/get_version.py`).

### When asked to create release files (agents)

1. Ensure **`CHANGELOG.txt`** reflects the **current** release (single `vX.Y.Z` line at the end for that version).
2. Run `platformio run -e blackpill_f411ce -t clean` then `platformio run -e blackpill_f411ce`.
3. Create **`release/vX.Y.Z/`** (match the version from step 1).
4. Copy **`krono_code_vX.Y.Z.bin`**, **`.hex`**, and **`.elf`** from `.pio/build/blackpill_f411ce/` into that folder.
5. Add **`release_notes.txt`** in the same folder: **Markdown** for GitHub **Draft a new release** (description field). Use a **discursive, user-facing** tone: what changed, **why it helps**, and what things **are** (avoid C identifiers, file names, and callback names). For the **status LED**, include the **operational modes table** as in **`README.md`** (same Markdown table). Optionally add a short maintainer footer (build/DFU pointer). Do **not** name this file `README.txt`.

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
5. Update `CHANGELOG.txt` and **`README.md`** (and this file if architecture/input contracts change).
