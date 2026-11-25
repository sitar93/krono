# AGENTS.md — KRONO firmware (contributors & coding agents)

This file is the **single technical reference** for anyone modifying the codebase (humans or agents). End-user documentation lives in **`README.md`**. Version history is in **`CHANGELOG.txt`**.

---

## Quick orientation

| Topic | Where |
|--------|--------|
| Usage, hardware, modes (user) | `README.md` |
| OMEGA tier (modes 21–30): behavior table (user) + firmware notes (this file) | `README.md` + **OMEGA / internal Gamma** section below |
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

- Output: `.pio/build/blackpill_f411ce/` — **`krono_code_<VERSION>.bin`** = PlatformIO DFU-suffixed image (**same as v1.4.0**; required by https://jolin.tech/krono_updater), **`.hex`**, **`.elf`**, optional **`_raw.bin`** (Cube only). Do not strip/pad the primary web `.bin`. Version from **`CHANGELOG.txt`**.
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
├── krono_aux_led_pattern.c/h  # Multi-pulse PA3 sequences (tier announcements); cooperates with main soft-blink timer
├── aux_led.c/h            # PA3 = TIM2_CH4 PWM renderer: event layer > count layer > status (steady / breathe)
├── aux_led_mode_status.c/h # Per-mode status mapping for the MOD LED
├── tap_calibration.c/h    # Boot-time tap latency calibration (hold MOD at power-on)
├── input_handler.c/h      # Inputs, tap averaging, op-mode SM (+ Omega + Gamma), ext clock, tempo dispatch, short-MOD mode actions (12–30)
├── clock_manager.c/h      # F1 pulse, mode_context, mode dispatch
├── status_led.c/h
├── drivers/               # io, tap, ext_clock, persistence, rtc, analog_out (PWM DAC), audio_gates (1-bit voices),
│                          #   tap_calib_store (flash sector 6)
├── modes/                 # mode_*.c, modes.c/h, mode_mod_dispatch.c, mode_rhythm_shared.c/h,
│                          #   mode_delta.c (31-35), mode_delta_audio.c (36-40), mode_delta_shared.h
└── util/
platformio.ini
```

---

## Runtime data flow (tempo)

1. `tap.c` — EXTI0 on PA0: intervals → `tap_detected()` / `tap_get_interval()`.
2. `input_handler.c` — tap quadruple policy in `input_tempo.c`; tempo callback on boundaries + external clock; op-mode SM can drain tap events.
3. `main.c` — `on_tap_tempo_change` → `clock_manager_arm_tap_quadruple_boundary()` on tap 4/8/…, else `clock_manager_set_internal_tempo()` (external / fallback).
4. `clock_manager.c` — schedules F1; passes `mode_context_t` to active `mode_*_update()`.

**Tap policy (v1.5.0, supersedes the v1.4.0 text below):** `input_tempo.c` fits a least-squares line through
(beat index, tap time). Tempo = slope over up to `TAP_FIT_MAX_TAPS` (64) taps, phase anchor = last 16
(`TAP_FIT_PHASE_TAPS`). Lock on tap 4 (`clock_manager_tap_lock`, F1 at the press + calibration offset); later taps
`clock_manager_tap_refine` (Q16 tempo + soft phase correction `TAP_PHASE_CORR_PCT` per beat in the flywheel).
Outliers (> `TAP_FIT_OUTLIER_TOL` beat) ignored; two in a row restart; skipped beats allowed; once locked, taps
after a pause resume the same sequence. Whole-BPM snap within min(2 sigma, `TAP_BPM_SNAP_MAX`). Tap latency offset
from `tap_calib_store` (flash sector 6, 0x08040000; **firmware must stay below 0x08040000**). Tuned on logged
sessions with `../krono_tap_lab/krono_tap_lab.html` (browser emulation of this exact algorithm).

**Historical tap policy (v1.4.0):** Boundaries **4, 8, 12, …** each fire **F1** once and set tempo from the **median** of three gaps (carry-in after a boundary still dropped). **Leading** boundaries (4, 12, …): emit that median. **Trailing** (8, 16, …): if **≤ `TAP_QUAD_BLEND_WINDOW_MS`** (3 s) after the previous leading press, tempo = **(30×leading median + 70×trailing median) / 100** (rounded); else trailing median only. Clicks between boundaries do not retune. **`TAP_PATTERN_IDLE_RESET_MS`** (5 s) resets the pattern. **`clock_manager_arm_tap_quadruple_boundary`** runs in **`clock_manager_update`**.

**Clock In edge (v1.5.0):** the input stage (NPN, 10k pull-up) inverts, so PB3 / PB4 EXTI use the FALLING pin
edge = external rising edge. Precise master period from long runs of consistent edges (`ext_clock_get_precise_period_q16`,
Q16 ms) feeds `clock_manager_set_tempo_q16_keep_phase`; on lock loss tempo and phase are kept (no re-anchor). An
edge arriving shortly after a flywheel F1 re-anchors phase without a second F1. Flywheel compares use signed math.

**External clock (v1.4.1.6):** PB3 rising edges are measured while **internal F1 always keeps running** (flywheel). Hard-follow starts only after a **stable triplet** (`max−min ≤ max(EXT_CLOCK_STABILITY_ABS_MS, min×5%)`). Edges within `EXT_CLOCK_ECHO_WINDOW_MS` of a local F1 are still measured for validation/lock but **not** queued for F1 forwarding (autopatch must not double-F1; must not drop real masters near our tempo). On first lock: save `tempo_before_ext_ms`, `clock_manager_begin_external_slave()` (flush queue, arm forwarding, one F1 on validating edge; mode update deferred). While following, in-range non-echo edges snap F1; out-of-range edges break follow immediately. Lock loss: no non-echo in-range edge within `(locked_interval + EXT_CLOCK_LOCK_GRACE_MS)` → end follow and keep session tempo on the flywheel (no multi-second mute). Tap is ignored only while hard-following. Unfollow restore priority: **session** validated interval → `tempo_before_ext_ms` → `DEFAULT_TEMPO_INTERVAL`. PB3 stays `GPIO_PUPD_NONE`; never reclaim **PA13/PA14** (SWD). Free **PA15/PB3/PB4** once in `io_init` via `io_release_jtag_only_pins()` (**v1.4.1.4/1.5** — do not re-run after EXTI arm). **v1.4.1.3+:** queued edges use per-edge timestamps; wall-clock mode update while following; pulses retrigger.

### BETA / OMEGA selection (mode families)

Mode-family selection reuses the **same** op-mode change state machine as modes 1–10 (`handle_op_mode_sm` in `input_handler.c`). There is **no** long-press on Mode to enter a separate UI.

| Path | Condition | Confirm callback |
|------|-----------|------------------|
| ALPHA (1–10) | User qualifies with Tap hold ≥ `OP_MODE_TAP_HOLD_DURATION_MS`, releases Tap **before** `OP_MODE_TAP_OMEGA_HOLD_MS` from **first** press | `op_mode_change_cb(N)` with N = 1…10 |
| BETA (11–20) | User **keeps holding** Tap until `OP_MODE_TAP_OMEGA_HOLD_MS` (~2 s from first press; see `variables.h`) | `op_mode_change_cb(N + 10)`; N wrapped to 1…10 if click count exceeds 10 |
| OMEGA (21–30, internal Gamma) | User **keeps holding** Tap until `OP_MODE_TAP_GAMMA_HOLD_MS` (~3 s from first press) | `op_mode_change_cb(N + 20)`; N wrapped to 1…10; supersedes BETA for confirm if both thresholds crossed |

**Timings** (`variables.h`): `OP_MODE_TAP_HOLD_DURATION_MS`, `OP_MODE_TAP_OMEGA_HOLD_MS`, `OP_MODE_TAP_GAMMA_HOLD_MS`, `OP_MODE_TAP_OMEGA_MAX_HOLD_MS` (abort if Tap never released within this window from press start). **OMEGA Aux (internal Gamma threshold)** uses `AUX_LED_MULTI_PULSE_ON_MS` / `AUX_LED_MULTI_PULSE_GAP_MS` via `krono_aux_led_pattern_start(2, …)` in `main.c` (BETA remains single soft blink).

**PA3 feedback**

- At **1 s** qualify and at **BETA arm**: `aux_led_blink_request_cb()` → `main.c` `pa3_soft_blink_arm()` (PA3 on + `status_led_pa3_blink_end_time`, typically +100 ms). `pa3_soft_blink_arm()` calls `krono_aux_led_pattern_cancel()` so a queued multi-pulse pattern does not fight a new soft blink.
- At **OMEGA arm** (~3 s, internal Gamma threshold): `gamma_aux_pattern_cb` → `krono_aux_led_pattern_start(2, …)`; pump + `krono_aux_led_pattern_active()` as below.
- **Multi-pulse** pattern: `krono_aux_led_pattern_pump(now)` in the main loop; `krono_aux_led_pattern_active()` — while true, skip the generic PA3-off timeout. `krono_aux_led_cancel_soft_timer()` (in `main.c`) clears the soft deadline when a pattern starts.

**`main.c` coordination**

- Single-file hook `krono_aux_led_cancel_soft_timer()` — clears `status_led_pa3_blink_end_time`; used by `krono_aux_led_pattern.c` when starting a pattern.

**Interaction with BETA modes (11–20) short MOD**

`handle_button_calc_mode_swap()` runs only when `current_op_mode_sm_state == INPUT_SM_IDLE` (and other gates). During op-mode change (including BETA/OMEGA selection), short MOD is consumed by the op-mode SM, not the rhythm-mode gesture path.

### Modes 12–20 and OMEGA / internal Gamma (short MOD path)

- **`MODE_USES_MOD_GESTURES(m)`** in `modes.h` — true for `MODE_DRIFT` … `MODE_ACCUMULATE` and Gamma modes (`MODE_GAMMA_SEQUENTIAL_RESET` … as added).
- **`handle_button_calc_mode_swap()`** in `input_handler.c`: short MOD release (within `CALC_SWAP_MAX_PRESS_DURATION_MS`) → `mod_press_cb(MOD_PRESS_EVENT_SINGLE)`.
- **PB4 CV gate** (`input_handler_update`, after `ext_gate_swap_requested`): if `MODE_USES_MOD_GESTURES`, same as short MOD → `mod_press_cb`; else FIXED → bank; else calc swap.
- **Gamma clock outputs:** `MODE_SKIPS_AUTO_F1_CLOCK_ON_1AB(m)` — when true, `clock_manager` does not drive 1A/1B as the automatic base-clock mirror; the mode drives them. **Mode 21** sequential clock: one jack per F1 — **A** 1A→6A, then **B** 1B→6B (12-step cycle); `CALC_MODE_SWAPPED` reverses the full cycle. **Mode 22:** same 12-step sweep; MOD/CV **toggles freeze** (hold step); `gamma_seq_freeze_step` is **0..11** in `krono_state_t`. **Mode 23:** six **trip** patterns; MOD/CV cycles pattern index and resets step; `gamma_seq_trip_*` in flash. **(1)** inverted pairs 1A+6B…6A+1B (6 F1); **(2)** bounce on those pairs, 10 steps (ends 2A+5B); **(3)** stairs 1A,1B,…,6A,6B; **(4)** circle 1A→6A then 6B→1B; **(5)** convergence pairs (1A+4B, 6A+3B, …); **(6)** groups (four outs × 3). **Calc swapped** = play current pattern **backwards**. Internal `f1_rising_edge` / `f1_counter` still advance from tempo.
- **Modes 24–30:** Fire, Bounce, Portals, Coin toss, Ratchet, Anti-ratchet, Start-stop — see **`README.md`** modes table. Files: `mode_gamma_sequential_fire.c`, `mode_gamma_sequential_bounce.c`, `mode_gamma_portals.c`, `mode_gamma_coin_toss.c`, `mode_gamma_clock_family.c`. Extra persistence: `gamma_portals_div_on_a` (**true** = Portals **multiply** / T/k), `gamma_coin_invert`, `gamma_ratchet_double`, `gamma_antiratchet_half`, `gamma_startstop_muted`.
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

### DELTA tier (31-40)

- Entry: TAP hold `OP_MODE_TAP_DELTA_HOLD_MS` (4 s) → 4 Aux pulses; confirm N+30. Abort at `OP_MODE_TAP_OMEGA_MAX_HOLD_MS` (6 s).
- `drivers/analog_out.c`: TIM1 CH2N/CH3N/CH1N (1A/2A/2B), 84 MHz / 12 = 7 MHz PWM, DMA burst (DCR/DMAR, DMA2 S5 ch6) of
  per-period dithered CCR values; measured duty→voltage table `AO_CAL_DUTY` at 93 % amplitude (0..~8.4 V). 3A = TIM5_CH3
  with CCR fed by DMA1 S6 ch2 paced by TIM4 (CV only). Audio streaming: HT/TC interrupt renders 8 samples per half
  (~36.5 kHz), 32x expansion, RCR 5. 1A and 1B can never both be analog (same TIM1 CH2N).
- `drivers/audio_gates.c`: TIM4 40 kHz ISR, 10 one-bit voices (square with pulse width, LFSR noise, logic, high).
- Persistence: `krono_state_t.delta_param[10]`. Audio modes silence for 2 s on entry and before every flash save
  (`mode_delta_audio_hold`) because IRQs are masked while saving.
- LFO modes clocks: 1B base, 3B counter-phase; automation gates on 4A 5A 6A 4B 5B 6B.

### MOD LED (PA3) and inputs

- PA3 is TIM2_CH4 PWM (`aux_led.c`); `set_output(JACK_OUT_AUX_LED_PA3, x)` drives its event layer. Status per mode in
  `aux_led_mode_status.c` (steady = freeze / B bank, breathe = modulation, looping count = enumerated state).
- MOD button electrically reads HIGH when pressed on this PCB (the op-mode SM's debounced flag is inverted; see
  `op_mode_mod_feedback` and `tap_calibration.c`). Do not "fix" one side without the other.

## Important implementation notes

- `millis()` lives in `main.c` (SysTick 1 ms).
- Modes should prefer `context->current_time_ms` over raw `millis()` where timing must align with the clock manager.
- Mode-specific persisted fields are applied in `main.c` / `save_current_state()` (chaos divisor, swing indices, fixed-mode bank, modes 12–20 MOD states, and Gamma fields for modes 22–23 / 26–30) alongside `krono_state_t`.
- Persistence: `krono_state_t` in `persistence.h`; flash address `0x08060000`. Save is triggered via input-handler save path (see `README.md`).
- **LEDs (PA15 status, PA3 aux)** — see **`README.md` → LEDs: behavior vs firmware** for the user-facing table. Implementation summary:
  - **PA15:** `status_led.c` → `set_led(bool on)` → `set_output(STATUS_LED_PIN, on)` (logical **on** = GPIO **high** on the pin as driven by `io.c`). Pattern state machine: `pending_off_gap_ms` chooses gap after each pulse — `STATUS_LED_INTER_PULSE_OFF_MS` after a normal (N) pulse, `STATUS_LED_AFTER_LONG_OFF_MS` after a long (L) pulse; end-of-loop dark time uses `STATUS_LED_SEQUENCE_GAP_MS`. Pulse shapes: `status_led_pulse_count()` / `status_led_pulse_is_long()` — user mode **1–9** = that many **N** pulses; **≥10** = **(tens)×L** then **(units)×N** (e.g. 20=L,L; 21=L,L,N; 30=L,L,L). ON times `STATUS_LED_BASE_INTERVAL_MS` (N) and `STATUS_LED_LONG_ON_MS` (L). If a board shows light when the code intends dark, invert mapping inside `set_led()` only.
  - **PA3:** `main.c` `pa3_soft_blink_arm()` and optional `krono_aux_led_pattern_pump()` / `krono_aux_led_pattern_active()`; Omega uses the same soft blink as qualify via `aux_led_blink_request_cb()`.

---

## Adding a new operational mode (checklist)

1. Add `MODE_*` to `operational_mode_t` in `modes.h` (before `NUM_OPERATIONAL_MODES`).
2. Add `mode_*_init/update/reset` in new `mode_*.c` / `mode_*.h`, declared in `modes.h`.
3. Register in `modes.c` (init/reset tables).
4. Register update in `clock_manager.c` (`mode_update_functions[]`).
5. Update `status_led.c` (`status_led_pulse_count` / `status_led_pulse_is_long`) so the new user mode index matches the intended N/L pattern (see existing rules for modes 1–30).
6. Extend `krono_state_t` and `main.c` load/save paths if the mode needs new persisted fields.
7. If the mode uses short MOD actions like 12–20: extend `MODE_USES_MOD_GESTURES`, add `mode_*_on_mod_press`, register in **`mode_mod_dispatch.c`**, implement at least `MOD_PRESS_EVENT_SINGLE`.
8. Update user-facing **`README.md`** (modes table + input notes if behavior differs).
9. Document the release in **`CHANGELOG.txt`**.
10. Build: `platformio run -e blackpill_f411ce`.

---

## Debug playbook (tempo / input)

**Boundaries:** capture (ISR / `tap.c`) → validate (`input_handler.c`) → propagate (callbacks in `main.c`) → apply (`clock_manager.c`).

**Symptoms:**

- Tap tempo “stuck” or wrong phase → `TAP_PATTERN_IDLE_RESET_MS`, median + blend in `input_tempo.c`; external-clock reset paths in `input_handler.c`.
- Phase feels wrong → timestamp passed into `clock_manager_set_internal_tempo()`.
- Tap ignored after gestures → op-mode SM draining `tap_detected()` in non-idle states.
- Modes 12–30 MOD short ignored → check `CALC_SWAP_MAX_PRESS_DURATION_MS`, cooldown (`last_calc_swap_trigger_time`), `INPUT_SM_IDLE` gating, and `external_clock_active` early return.
- Multi-pulse Aux looks like one long blink if fired as two quick `aux_led_blink_request_cb()` calls → use `krono_aux_led_pattern_start()` + pump + `krono_aux_led_pattern_active()` guard in `main.c`.
- Status LED “off” segments look lit, or mode 20 shows too many flashes → check `set_led()` vs LED wiring (logical on/off vs GPIO); verify `pending_off_gap_ms` and pulse indices in `status_led_update()`.
- External clock issues → hard-follow only after validation; forwarding/`ext_clock_arm_slave_forwarding`; lock loss = one missed beat or out-of-range; echo window for autopatch; restore uses session/`tempo_before_ext_ms`.
- Internal tempo jumps to ~70 BPM after noise → ensure follow is not armed on raw presence; check unfollow restore path.
- Autopatch 1A/1B→Clock In double-hits → confirm echo skips forwarding only; normal/slow Clock In ignored → echo must not drop validation.

**Manual checks:** steady taps → lock on 4th tap; Clock In unpatched → internal F1 stable; ext lock after ~4 edges; disconnect → session/pre-slave tempo (not accidental default); re-plug same BPM → re-slave.

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

## Clock In change checklist

Before merging any edit to `ext_clock.*`, `clock_manager.*` (F1 / slave) or the `input_handler` external-lock path,
re-read those files and answer:

1. **Slow / normal external master (~40-120 BPM):** do rising edges still enter validation (interval buffer + ready
   flag), not only forwarding? The echo window must **not** `return` early before measuring / holding the lock.
2. **Fast external master:** still validates; out-of-range still breaks follow without a multi-second mute.
3. **Tap tempo:** ignored only while hard-following; works again immediately after lock loss / unfollow.
4. **Autopatch 1A/1B → Clock In:** the internal flywheel keeps running; echoes may skip **forwarding** only;
   1A/1B must not go silent.
5. **Hard-follow:** in-range non-echo edges still queue → `apply_external_pulse`; the flywheel covers a missed beat.
6. **Lock loss:** one missed period (+ grace) or out-of-range, not `EXT_CLOCK_TIMEOUT_MS` (~15 s) as the normal path.
7. **Edge polarity:** PB3 / PB4 trigger on the pin FALLING edge (inverting input stage) = external rising edge.

If any answer is "no" or "unclear", fix before release.

## Release build notes

- **Primary `krono_code_<ver>.bin` = PlatformIO's DFU-suffixed `firmware.bin`, renamed only** (16-byte DFU suffix,
  tail signature `UFD`), exactly as since v1.4.0: required by https://jolin.tech/krono_updater. Never strip the suffix,
  never 0xFF-pad to a sector, never replace it with a raw `objcopy` image (that soft-bricked units). A raw
  `krono_code_<ver>_raw.bin` may be shipped for STM32CubeProgrammer only. Quick check: a good online `.bin` ends with
  `UFD` + length 16 and its size = image + 16.

- Prefer a clean build before release: `clean` then `run`.
- Artifacts under `.pio/build/blackpill_f411ce/` (including renamed `krono_code_v*.*.*.{bin,elf,hex}` from post-build scripts).
- Version string is taken from the **last non-empty line** of **`CHANGELOG.txt`** (see `scripts/get_version.py`).

### When asked to create release files (agents)

1. Ensure **`CHANGELOG.txt`** reflects the **current** release (the **last non-empty line** must start with `vX.Y.Z` or `vX.Y.Z.W` — see `scripts/get_version.py`).
2. Run `platformio run -e blackpill_f411ce -t clean` then `platformio run -e blackpill_f411ce`.
3. Create **`release/<version>/`** (match the version from step 1).
4. Copy **`krono_code_<version>.bin`** (DFU-suffixed, historical), **`.hex`**, **`.elf`**, optional **`_raw.bin`** — or run **`powershell -ExecutionPolicy Bypass -File scripts/release_bundle.ps1`** (requires UFD suffix on primary `.bin`).
5. Add **`release_notes.txt`** in the same folder: **Markdown** for GitHub **Draft a new release** (description field). Use a **discursive, user-facing** tone: what changed, **why it helps**, and what things **are** (avoid C identifiers, file names, and callback names). For the **status LED**, include the **operational modes table** as in **`README.md`** (same Markdown table). Optionally add a short maintainer footer (build/DFU pointer). Do **not** name this file `README.txt`.
6. **DFU upload:** from repo root, **`powershell -ExecutionPolicy Bypass -File scripts/dfu_upload.ps1`** runs PlatformIO upload for `blackpill_f411ce` (device in STM32 bootloader — see **Upload (DFU)** above; uses `%USERPROFILE%\.platformio\penv\Scripts\platformio.exe` when present).

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
