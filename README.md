# KRONO - Make Your Own Time

Shield: [![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

This work is licensed under a
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License][cc-by-nc-sa].

[![CC BY-NC-SA 4.0][cc-by-nc-sa-image]][cc-by-nc-sa]

[cc-by-nc-sa]: https://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-image]: https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg

## Documentation map

| Resource | Purpose |
|----------|---------|
| **This file (`README.md`)** | Primary user reference: hardware, usage, modes, behavior |
| **`CHANGELOG.txt`** | What changed from version to version |
| **`LICENSE.txt`** | Full license text |
| **`AGENTS.md`** | Developers and AI agents: build, architecture, input/MOD logic, extending modes |

---

## Overview

KRONO is a multimodal time control module for Eurorack synthesizers, implemented on an **STM32F411CE** microcontroller (Black Pill–class boards on the Krono PCB; STM32F401 may work with target changes). It provides tap and external-clock tempo, multiple rhythmic output modes, and **persistent state** in internal Flash.

---

## Hardware setup

- **PA0:** Tap (button to GND)
- **PA1:** Mode / calculation swap (button to GND)
- **PB3:** External clock input — *conflicts with default SWO (SWD)*
- **PB4:** External gate (calculation swap) — *can conflict with JTAG (e.g. NJTRST)*
- **PA15:** Status LED (blink pattern = operational mode; solid during mode-change UI)
- **PA3:** Aux LED (short blinks for tempo, swap, mode UI, save)
- **Group A (`JACK_OUT_1A`…`6A`):** PB0, PB1, PA2, PB15, PB5, PB6  
- **Group B (`JACK_OUT_1B`…`6B`):** PB14, PB13, PB12, PB8, PB9, PB10  

Exact enums: `src/drivers/io.h` (`JACK_OUT_...`).

**Debugging:** PB3/PB4 usage often prevents normal SWD/JTAG. Prefer **DFU upload** (see below).

---

## Building and installing

Requires [PlatformIO](https://platformio.org/). From the project root:

```bash
platformio run -e blackpill_f411ce
platformio run -e blackpill_f411ce --target upload
```

**DFU:** hold **BOOT0**, pulse **NRST**, release **BOOT0**, then upload. More detail and troubleshooting: **`AGENTS.md`**.

Release-style artifacts (renamed binaries, hex) appear under `.pio/build/blackpill_f411ce/` after a successful build (see **`AGENTS.md`** and `CHANGELOG.txt` for versioning).

---

## Quick start

1. **Patch:** Connect outputs, Tap (PA0), Mode (PA1). Optionally external clock (PB3) and gate (PB4).
2. **Power:** Last saved state loads from Flash (tempo, mode, swap, mode-specific parameters where applicable).
3. **Tempo**
   - **Tap:** After **three measured intervals** (fourth tap confirms), the module averages valid intervals and updates tempo; **F1 phase is not reset** on internal tap (interval only). The aux LED blinks on tempo updates.
   - **External clock:** When stable, PB3 overrides tap; on timeout, tempo falls back (see behavior in technical section below).
4. **Calculation swap:** Short **Mode** press, or rising gate on **PB4** (when not in mode-change UI and the active mode is **not** in the 12–20 rhythm set). In modes **12–20**, a short **Mode** press runs that mode’s primary action instead; **PB4** still performs calculation swap. Aux LED blinks on swap and on mode actions where implemented.

### Mode change and saving (step by step)

- **Enter mode change:** Hold **Tap (PA0)** longer than `OP_MODE_TAP_HOLD_DURATION_MS` (typically ~1–2 s; exact value in `variables.h`). Status LED (PA15) goes **solid ON**. Aux LED (PA3) blinks once.
- **Release Tap:** Status LED stays solid ON. A **5 s** window starts (`OP_MODE_TIMEOUT_SAVE_MS`).
- **Option A — Save only:** Do **not** press Mode within 5 s. Current state (tempo, mode, per-mode swap, mode-specific parameters such as swing/chaos and all MOD-driven values in modes 11–20) is **written to Flash**. Aux blinks once; Status LED returns to normal blinking for the current mode. This is the **primary save path**.
- **Option B — Change mode:** Press **Mode (PA1)** within 5 s (cancels the save timer). Each **release** of Mode increments the internal click counter toward the next operational mode. Status LED is OFF while Mode is held, ON when released; Aux does **not** blink on each Mode press. When the desired mode is selected, press **Tap** briefly to **confirm**. The new mode activates; Aux blinks once. **Saving** the new configuration still requires running **Option A** later (hold/release Tap, wait 5 s without Mode).
- **Abort:** If you pressed Mode at least once but never confirm with Tap, after `OP_MODE_CONFIRM_TIMEOUT_MS` (~10 s) the UI exits and the **previous** mode is restored; nothing is saved.

### Explore outputs (by mode family)

- **Default:** Multiplications vs divisions on outputs 2–6.
- **Euclidean / Musical / Probabilistic / Sequential / Swing / Polyrhythm / Phasing / Chaos / Fixed:** See the table below; **Swap** inverts or exchanges A/B data sets as described per mode.
- **Swing:** Additional profile selection via the mode-change flow while Swing is active (see `variables.h` / firmware).
- **Fixed (mode 11):** **MOD** or **PB4** cycles **banks** 0–9; transition can be aligned to pattern boundaries in firmware.
- **Drift … Accumulate (12–20):** 16-step rhythms on **2A–6B** only; see table below. **MOD short** drives each mode behavior directly (**elastic loop** where noted). **MOD+TAP is not used**.

---

## Operational modes (reference)

Mode order and **status LED blink count** (1 = Default … 20 = Accumulate):

| # | Mode | Summary |
|---|------|---------|
| 1 | **DEFAULT** | Group A: clocks at **multiples** ×2…×6 of base on 2A–6A. Group B: **divisions** /2…/6 on 2B–6B. **Swap:** inverts A/B roles (A divisions, B multiplications). |
| 2 | **EUCLIDEAN** | Euclidean rhythms with distinct K/N sets per group on outputs 2–6. **Swap:** swaps K/N sets between A and B. |
| 3 | **MUSICAL** | Rhythmic ratios vs base tempo on 2–6 per group. **Swap:** swaps ratio sets A/B. |
| 4 | **PROBABILISTIC** | Per-output trigger probabilities on each F1; A rising, B decreasing curves. **Swap:** inverts curves between groups. |
| 5 | **SEQUENTIAL** | Fibonacci-style vs primes-style sequences on A/B. **Swap:** alternate sequence sets (e.g. Lucas / composites). |
| 6 | **SWING** | Per-output swing on even beats; multiple profiles. **Swap:** swaps swing sets. Profiles persist in saved state. |
| 7 | **POLYRHYTHM** | X:Y polyrhythms on 2–5; output 6 = logical OR of 2–5 in that group. **Swap:** swaps X:Y sets. |
| 8 | **LOGIC** | Combines **Default-mode** derived signals: Group A **XOR** between paired A/B default outputs; Group B **NOR**. **Swap:** swaps gate types (A↔B roles in that scheme). |
| 9 | **PHASING** | Group B at slightly detuned rate vs A; derived clocks on 3–6. **Swap:** cycles deviation amount. |
| 10 | **CHAOS** | Lorenz attractor threshold crossings; shared divisor across outputs 2–6. **Swap:** steps divisor (wrapped). Divisor persisted. |
| 11 | **FIXED** | 16-step fixed patterns at **4×** main clock; drum-style mapping on 2–6; **10 banks** (0–9), **MOD** or **PB4** advances bank; banks persisted. |
| 12 | **DRIFT** | Fixed base pattern with stochastic mutation at bar boundaries. **MOD:** elastic loop on drift probability (`10→...→100→...→0`), with stronger unpredictability and occasional larger jumps at higher values. MOD state persisted. |
| 13 | **FILL** | Fill-focused groove shaping with sparse low-end behavior at low values. **MOD:** **drastic loop** on fill (`0→10→...→50→0`), no gradual descent. Low values stay very empty with kick emphasis; each step is intentionally more audible. MOD state persisted. |
| 14 | **SKIP** | Base pattern; probabilistic skipping of hits. **MOD:** elastic loop on skip probability: `10→...→100→...→0→...`. MOD state persisted. |
| 15 | **STUTTER** | Base pattern with stutter lengths. **MOD:** **drastic loop** `2→4→8→2...` (no descending/off cycle). After each full stutter cycle, the base rhythm is slightly randomized to keep motion alive. MOD state persisted. |
| 16 | **MORPH** | Fully generative morph stream (A→B→C→D... continuously evolving pseudo-chaotically while staying musically coherent). **MOD:** freeze current state; next press resumes and advances to the next generated state. Freeze state persisted. |
| 17 | **MUTE** | Random additive/subtractive mute flow by output. **MOD:** each press mutes or unmutes one random channel depending on phase; each unmute also applies a slight random pattern variation to that channel. Mute state persisted. |
| 18 | **DENSITY** | Density-sculpted variation layer. **MOD:** **drastic loop** `0→10→...→200→0`; low values are highly sparse; each increase regenerates rhythmic variation (not just density). MOD state persisted. |
| 19 | **SONG** | Sectioned form with generated loops: bars **1–6** = generated base loop, bars **7–8** = generated variation loop. **MOD:** schedules a brand-new random base loop for the next cycle; without MOD, the current base loop repeats. Variation state persisted. |
| 20 | **ACCUMULATE** | Automatic accumulation loop with random output activation and random phase offsets on each newly activated output. Each activation also applies a slight random loop variation on the activated output. At max accumulation it resets **drastically** to minimum and repeats. **MOD:** freeze/unfreeze automatic accumulation. Accumulation state persisted. |

**Modes 12–20 — input detail:** only **short MOD** (release within `CALC_SWAP_MAX_PRESS_DURATION_MS` in `variables.h`). No MOD+TAP path.

---

## Project structure (firmware)

- **`src/main.c`** — Init, main loop, callbacks, aux LED timer, `millis()`; load/save wiring for chaos divisor, swing profiles, fixed-mode bank, and rhythm-mode MOD states (12–20).
- **`src/input_handler.c`** — Pin init, op-mode state machine, tap-interval averaging, external clock handoff, tempo callback dispatch, calc/fixed swap, short-MOD dispatch for modes 12–20.
- **`src/clock_manager.c`** — F1 generation, `mode_context_t`, dispatch to `mode_*_update`.
- **`src/drivers/`** — `io`, `tap`, `ext_clock`, `persistence`, `rtc`.
- **`src/modes/`** — One implementation per operational mode; `modes.h` / `modes.c` registry.
- **`src/main_constants.h`**, **`src/variables.h`** — Timing and tunables.
- **`platformio.ini`** — Environment `blackpill_f411ce`.

For **how to add a mode** or **debug**, see **`AGENTS.md`**.

---

## Core behavior (technical summary)

### Timing

- **SysTick** in `main.c` → `millis()` (1 ms).
- **Tap:** `tap.c` (EXTI0) → **`input_handler.c`** averages `NUM_INTERVALS_FOR_AVG` intervals within min/max and spread limits → callback → **`clock_manager_set_internal_tempo()`** (interval only for internal tap; external clock aligns phase).
- **External clock:** `ext_clock.c` validates intervals; `input_handler.c` overrides tap while valid; on timeout, reverts using last valid external or last tap interval where applicable.

### Clock

- **F1** on `JACK_OUT_1A` / `1B` at `active_tempo_interval_ms`.
- Active mode receives **`mode_context_t`** each cycle (`f1_rising_edge`, `calc_mode`, etc.).

### Persistence

- **`krono_state_t`** in Flash (magic + checksum). Save is requested through the **mode-change UI timeout** path (see Quick start). It includes mode-specific parameters, including MOD-driven states for modes 11–20. Cooldown: `SAVE_STATE_COOLDOWN_MS` in `variables.h`.

### Inputs (detail)

- **Op-mode sequence:** `input_handler.c` state machine (`handle_op_mode_sm`): hold tap → release → 5 s save window or MOD clicks → TAP confirm; confirm timeout can abort to previous mode.
- **Calc swap:** short MOD or PB4 (when idle path allows); **fixed** uses MOD for **bank**; **modes 12–20** use short MOD for **mode UI** (see table), PB4 still swaps calc A/B.

---

## Version history

See **`CHANGELOG.txt`** for line-by-line release notes.

---

## License

Summary at the top of this file; full text in **`LICENSE.txt`**.
