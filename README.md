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
| **`AGENTS.md`** | Developers and agents: build, architecture, input/MOD logic, extending modes |

---

## Overview

KRONO is a multimodal time control module for Eurorack synthesizers, implemented on an **STM32F411CE** microcontroller (Black Pill–class boards on the Krono PCB; STM32F401 may work with target changes). It provides tap and external-clock tempo, multiple rhythmic output modes, and **persistent state** in internal Flash.

## Related links

- **Official module page:** [jolin.tech/krono](https://jolin.tech/krono)
- **VCV Rack plugin repository:** [sitar93/krono_vcv](https://github.com/sitar93/krono_vcv)

---

## Hardware setup

- **PA0:** Tap (button to GND)
- **PA1:** Mode / calculation swap (button to GND)
- **PB3:** External clock input — *conflicts with default SWO (SWD)*
- **PB4:** External gate (calculation swap) — *can conflict with JTAG (e.g. NJTRST)*
- **PA15:** Status LED — see **LEDs: behavior vs firmware** below and `variables.h` / `status_led.c`.
- **PA3:** Aux LED — see **LEDs: behavior vs firmware** below; tempo, swap, mode-change / save feedback, Omega single blink (same style as qualify).
- **Group A (`JACK_OUT_1A`…`6A`):** PB0, PB1, PA2, PB15, PB5, PB6  
- **Group B (`JACK_OUT_1B`…`6B`):** PB14, PB13, PB12, PB8, PB9, PB10  

Exact enums: `src/drivers/io.h` (`JACK_OUT_...`).

**Debugging:** PB3/PB4 usage often prevents normal SWD/JTAG. Prefer **DFU upload** (see below).

### LEDs: behavior vs firmware

What you see on the panel follows **logical** on/off in firmware, mapped to GPIO via `set_output()` in `drivers/io.c` (active level depends on the LED wiring).

| LED | Pin | When it looks **ON** (typical Krono wiring) | Main code paths |
|-----|-----|---------------------------------------------|-----------------|
| **Status** | PA15 (`JACK_OUT_STATUS_LED_PA15`) | `status_led.c` calls `set_led(true)` → `set_output(STATUS_LED_PIN, true)` (GPIO **high** = logical on). **Off** during gaps and pulses uses `set_led(false)` → GPIO **low**. During mode-change UI, `status_led_set_override()` holds a fixed logical on/off (solid “on” while waiting). | Normal: `status_led_update()` N/L pattern. Override: `input_handler.c` during TAP/MOD mode-change states. |
| **Aux** | PA3 (`JACK_OUT_AUX_LED_PA3`) | `main.c` arms short blinks via `pa3_soft_blink_arm()` (`set_output` + `status_led_pa3_blink_end_time`, typically 100 ms). Optional multi-pulse sequences use `krono_aux_led_pattern_*` in `krono_aux_led_pattern.c`; while a pattern is active, the main loop skips the simple off-timeout so pulses are not clipped. GPIO **high** = logical on (same convention as Status). | Tempo/swap/save/calc callbacks, mode-change qualify/Omega feedback (`aux_led_blink_request_cb`), future multi-pulse tiers via `krono_aux_led_pattern_start()`. |

**Status pattern (user modes 1–30):** short **N** pulses use `STATUS_LED_BASE_INTERVAL_MS` ON time; long **L** uses `STATUS_LED_LONG_ON_MS`. Dark gap after a **normal** pulse: `STATUS_LED_INTER_PULSE_OFF_MS`; after a **long** pulse: `STATUS_LED_AFTER_LONG_OFF_MS`; after the **whole** pattern, before it repeats: `STATUS_LED_SEQUENCE_GAP_MS`. Encoding: 1–9 → N×count; 10 → [L]; 11–19 → [L] + (mode−10)×N; 20 → [L,L]; 21–30 → tens/units (e.g. 21 = L,L,N; 30 = L,L,L).

If status “off” ever appears **on** (or the opposite), the panel may use inverted LED wiring — adjust `set_led()` in `status_led.c` (single place).

---

## Building and installing

Requires [PlatformIO](https://platformio.org/). From the project root:

```bash
platformio run -e blackpill_f411ce
platformio run -e blackpill_f411ce --target upload
```

**DFU:** hold **BOOT0**, pulse **NRST**, release **BOOT0**, then upload. More detail and troubleshooting: **`AGENTS.md`**.

Release-style artifacts (renamed binaries, hex) appear under `.pio/build/blackpill_f411ce/` after a successful build (see **`AGENTS.md`** and `CHANGELOG.txt` for versioning). To copy them into **`release/<version>/`**, run **`powershell -ExecutionPolicy Bypass -File scripts/release_bundle.ps1`** from the project root after a successful build. For DFU upload from the CLI, use **`powershell -ExecutionPolicy Bypass -File scripts/dfu_upload.ps1`** (BOOT0 + reset as in **`AGENTS.md`**).

---

## Quick start

1. **Patch:** Connect outputs, Tap (PA0), Mode (PA1). Optionally external clock (PB3) and gate (PB4).
2. **Power:** Last saved state loads from Flash (tempo, mode, swap, mode-specific parameters where applicable).
3. **Tempo**
   - **Tap:** **Every fourth click** (4, 8, 12, …) emits **one pulse on the main beat outputs** and sets the beat period to the **median** of the three gaps in that group (same gap rules as before). On clicks **8, 16, …**, if within **3 s** of the previous boundary click (4, 12, …), the period blends **30%** of that earlier median and **70%** of the new one; otherwise only the new median. Clicks in between do **not** retune the clock. **5 s** without Tap restarts counting. **Sequencer step is not reset** on tap tempo. Aux LED blinks on each boundary.
   - **External clock:** When stable, PB3 overrides tap; on timeout, tempo falls back (see behavior in technical section below).
4. **Calculation swap:** Short **MOD** (front **Mode** button). If you use the **PB4** gate input, it **mirrors MOD** for routing: in **modes 12–20** and **21–30** a short press (button or gate) is always that mode’s **MOD gesture**; in **Fixed (11)** it advances **banks**; otherwise it triggers **calculation swap** when the UI is idle. Aux LED blinks on swap and on mode actions where implemented.

### Mode change and saving (step by step)

- **Enter mode change:** Hold **Tap (PA0)** longer than `OP_MODE_TAP_HOLD_DURATION_MS` (typically ~1 s; exact value in `variables.h`). Status LED (PA15) goes **solid ON**. Aux LED (PA3) blinks **once** (soft timer in firmware).
- **Omega — modes 11–20 (optional):** From the same hold, **keep holding Tap** until `OP_MODE_TAP_OMEGA_HOLD_MS` (~**2 s** from the **first** press; see `variables.h`). Aux LED (PA3) blinks **once** (same soft blink as at qualify). That arms **Omega**: each **Mode** click still counts as before, but **Tap** confirm selects operational mode **N + 10** (modes 11–20 in the table below), i.e. callback argument **N + 10** for N = 1…10. If you **release Tap after the 1 s qualify but before the Omega threshold**, you stay on the **base** path (modes **1–10** only).
- **Gamma — modes 21–30 (optional):** While still holding Tap past qualify, **keep holding** until `OP_MODE_TAP_GAMMA_HOLD_MS` (~**3 s** from the **first** press; see `variables.h`). Aux LED uses the **double short pulse** pattern (`krono_aux_led_pattern`). That arms **Gamma** and **clears** Omega for this session: **Tap** confirm selects mode **N + 20** (modes **21–30**). If Tap is released **before** the Gamma threshold, **Omega** (~2 s) can still apply (**N + 10**, modes 11–20); **before** the Omega threshold, only base modes **1–10** apply.
- **Abort long hold:** If Tap is never released within `OP_MODE_TAP_OMEGA_MAX_HOLD_MS` (~**5 s** from first press), the mode-change UI exits without applying a new mode.
- **Release Tap:** Status LED stays solid ON. A **5 s** window starts (`OP_MODE_TIMEOUT_SAVE_MS`).
- **Option A — Save only:** Do **not** press Mode within 5 s. Current state (tempo, mode, per-mode swap, mode-specific parameters such as swing/chaos, MOD-driven values in modes 11–20, and Gamma toggles where applicable) is **written to Flash**. Aux blinks once; Status LED returns to normal blinking for the current mode. This is the **primary save path**.
- **Option B — Change mode:** Press **Mode (PA1)** within 5 s (cancels the save timer). Each **release** of Mode increments the internal click counter toward the next operational mode. Status LED is OFF while Mode is held, ON when released; Aux does **not** blink on each Mode press. When the desired mode is selected, press **Tap** briefly to **confirm**. If **Omega** was armed (extra Aux blink while still holding Tap past the Omega threshold), the counter maps to **modes 11–20**; otherwise to **modes 1–10**. The new mode activates; Aux blinks once. **Saving** the new configuration still requires running **Option A** later (hold/release Tap, wait 5 s without Mode).
- **Abort:** If you pressed Mode at least once but never confirm with Tap, after `OP_MODE_CONFIRM_TIMEOUT_MS` (~10 s) the UI exits and the **previous** mode is restored; nothing is saved.

### Explore outputs (by mode family)

- **Default:** Multiplications vs divisions on outputs 2–6.
- **Euclidean / Musical / Probabilistic / Sequential / Swing / Polyrhythm / Phasing / Chaos / Fixed:** See the table below; **Swap** inverts or exchanges A/B data sets as described per mode.
- **Swing:** Additional profile selection via the mode-change flow while Swing is active (see `variables.h` / firmware).
- **Fixed (mode 11):** **MOD** cycles **banks** 0–9 (same via **PB4** gate if wired); transition can be aligned to pattern boundaries in firmware.
- **Drift … Accumulate (12–20):** 16-step rhythms on **2A–6B** only; see table below. **MOD short** drives each mode behavior directly (**elastic loop** where noted). **MOD+TAP is not used**.
- **Gamma (21–30):** stepped clocks and scripted scenes; **1A/1B** are not a simple duplicate of the main beat in these modes. **MOD** drives the per-mode actions in the table (**PB4** mirrors **MOD** if connected); see **`AGENTS.md`** for wiring and persistence.

---

## Operational modes (reference)

Mode order and **status LED pattern** (user mode 1 = Default … 20 = Accumulate; **21–30**: **SEQUENTIAL RESET** … **STARTnSTOP** — see table; N = short ON, L = long ON; dark gaps from `STATUS_LED_INTER_PULSE_OFF_MS`, `STATUS_LED_AFTER_LONG_OFF_MS`, and end-of-loop `STATUS_LED_SEQUENCE_GAP_MS` in `variables.h`):

| # | Mode | Summary |
|---|------|---------|
| 1 | **DEFAULT** | Group A: clocks at **multiples** ×2…×6 of base on 2A–6A. Group B: **divisions** /2…/6 on 2B–6B. **Swap:** inverts A/B roles (A divisions, B multiplications). |
| 2 | **EUCLIDEAN** | Euclidean rhythms with distinct K/N sets per group on outputs 2–6. **Swap:** swaps K/N sets between A and B. |
| 3 | **MUSICAL** | Rhythmic ratios vs base tempo on 2–6 per group. **Swap:** swaps ratio sets A/B. |
| 4 | **PROBABILISTIC** | Per-output trigger probabilities on each **beat**; A rising, B decreasing curves. **Swap:** inverts curves between groups. |
| 5 | **SEQUENTIAL** | Fibonacci-style vs primes-style sequences on A/B. **Swap:** alternate sequence sets (e.g. Lucas / composites). |
| 6 | **SWING** | Per-output swing on even beats; multiple profiles. **Swap:** swaps swing sets. Profiles persist in saved state. |
| 7 | **POLYRHYTHM** | X:Y polyrhythms on 2–5; output 6 = logical OR of 2–5 in that group. **Swap:** swaps X:Y sets. |
| 8 | **LOGIC** | Combines **Default-mode** derived signals: Group A **XOR** between paired A/B default outputs; Group B **NOR**. **Swap:** swaps gate types (A↔B roles in that scheme). |
| 9 | **PHASING** | Group B at slightly detuned rate vs A; derived clocks on 3–6. **Swap:** cycles deviation amount. |
| 10 | **CHAOS** | Lorenz attractor threshold crossings; shared divisor across outputs 2–6. **Swap:** steps divisor (wrapped). Divisor persisted. |
| 11 | **FIXED** | 16-step fixed patterns at **4×** main clock; drum-style mapping on 2–6; **10 banks** (0–9), **MOD** advances bank; banks persisted. |
| 12 | **DRIFT** | Fixed base pattern with stochastic mutation at bar boundaries. **MOD:** elastic loop on drift probability (`10→...→100→...→0`), with stronger unpredictability and occasional larger jumps at higher values. MOD state persisted. |
| 13 | **FILL** | Fill-focused groove shaping with sparse low-end behavior at low values. **MOD:** **drastic loop** on fill (`0→10→...→50→0`), no gradual descent. Low values stay very empty with kick emphasis; each step is intentionally more audible. MOD state persisted. |
| 14 | **SKIP** | Base pattern; probabilistic skipping of hits. **MOD:** elastic loop on skip probability: `10→...→100→...→0→...`. MOD state persisted. |
| 15 | **STUTTER** | Base pattern with stutter lengths. **MOD:** **drastic loop** `2→4→8→2...` (no descending/off cycle). After each full stutter cycle, the base rhythm is slightly randomized to keep motion alive. MOD state persisted. |
| 16 | **MORPH** | Fully generative morph stream (A→B→C→D... continuously evolving pseudo-chaotically while staying musically coherent). **MOD:** freeze current state; next press resumes and advances to the next generated state. Freeze state persisted. |
| 17 | **MUTE** | Random additive/subtractive mute flow by output. **MOD:** each press mutes or unmutes one random channel depending on phase; each unmute also applies a slight random pattern variation to that channel. Mute state persisted. |
| 18 | **DENSITY** | Density-sculpted variation layer. **MOD:** **drastic loop** `0→10→...→200→0`; low values are highly sparse; each increase regenerates rhythmic variation (not just density). MOD state persisted. |
| 19 | **SONG** | Sectioned form with generated loops: bars **1–6** = generated base loop, bars **7–8** = generated variation loop. **MOD:** schedules a brand-new random base loop for the next cycle; without MOD, the current base loop repeats. Variation state persisted. |
| 20 | **ACCUMULATE** | Automatic accumulation loop with random output activation and random phase offsets on each newly activated output. Each activation also applies a slight random loop variation on the activated output. At max accumulation it resets **drastically** to minimum and repeats. **MOD:** freeze/unfreeze automatic accumulation. Accumulation state persisted. |
| 21 | **SEQUENTIAL RESET** | **One output active per beat**, full sweep **1A…6A** then **1B…6B** (12 steps). **MOD:** restart beat phase. **Swap:** play the 12-step cycle backward. **1A/1B** follow this pattern, not a separate copy of the main beat. |
| 22 | **SEQUENTIAL FREEZE** | Same sweep as 21. **MOD:** toggle freeze (hold step). Persisted. |
| 23 | **SEQUENTIAL TRIP** | Six scripted trip patterns. **MOD:** next pattern, reset step. **Swap:** reverse step order. Persisted. |
| 24 | **SEQUENTIAL FIRE** | **MOD:** **1A** and **6B** together; then on **each following beat** one **pair** steps **up** on A and **down** on B (**2A+5B**, **3A+4B**, … **6A+1B**). Normal trigger length. |
| 25 | **SEQUENTIAL BOUNCE** | **MOD:** all **12** jacks fire **once** together, then each **row** runs a **mini burst**: A side **speeds up** (gaps shrink; top A row finishes first), B side **slows down** (bottom B row finishes last). **Six** extra pulses per jack after the unison; timing in **fixed seconds**, not locked to current BPM. |
| 26 | **PORTALS** | **Held levels** (not short triggers): each column is a “door” — one side **high**, one **low**, for the whole beat. **Divide** (default): row **1** swaps every beat, row **2** every two beats, row **3** every three, and so on. **MOD:** **multiply** — higher rows **alternate faster within the beat**. Persisted (multiply vs divide). |
| 27 | **COIN TOSS** | On **each beat**, per column random **A** vs **B** with fixed odds (85/75/65/50/25/15 % for **A**). **MOD:** flip to **mirrored** odds. Persisted. |
| 28 | **RATCHET** | **1A–6A** multiplication, **1B–6B** division (like Default, including first outputs). **MOD:** **double** the effective “fast” side **without** restarting the bar—subdivisions **stretch** to match; the underlying beat grid **does not** jump. Persisted. |
| 29 | **ANTI-RATCHET** | Same routing as 28. **MOD:** **halve** that effective speed the same way—**no** extra pause, **no** forced reset of the divided side. Persisted. |
| 30 | **STARTnSTOP** | Same routing as 28. **MOD:** **mute** all gates; **internal** timing **keeps running** so you return in phase when unmuted. Persisted. |

**Modes 12–20 — input detail:** only **short MOD** (release within `CALC_SWAP_MAX_PRESS_DURATION_MS` in `variables.h`). No MOD+TAP path.

**Gamma 21–30:** same short **MOD** gesture as 12–20 (**PB4** mirrors **MOD** if wired); details per row. Implementation and saved fields are summarized in **`AGENTS.md`** (Gamma section).

---

## Project structure (firmware)

- **`src/main.c`** — Init, main loop, callbacks, PA3 soft blink + `krono_aux_led_pattern_pump`, `millis()`; load/save wiring for chaos divisor, swing profiles, fixed-mode bank, rhythm-mode MOD states (12–20), and Gamma toggles (modes 22–23, 26–30) in `krono_state_t`.
- **`src/krono_aux_led_pattern.c`** / **`.h`** — Optional multi-pulse Aux LED sequences (coexists with soft blink in `main.c`).
- **`src/input_handler.c`** — Pin init, op-mode state machine (including **Omega** and **Gamma** extended Tap holds for modes 11–20 and 21–30), tap-interval averaging, external clock handoff, tempo callback dispatch, calc/fixed swap, short-MOD dispatch for modes 12–30.
- **`src/clock_manager.c`** — Main beat scheduling, `mode_context_t`, dispatch to `mode_*_update`.
- **`src/drivers/`** — `io`, `tap`, `ext_clock`, `persistence`, `rtc`.
- **`src/modes/`** — One implementation per operational mode; `modes.h` / `modes.c` registry.
- **`src/main_constants.h`**, **`src/variables.h`** — Timing and tunables.
- **`platformio.ini`** — Environment `blackpill_f411ce`.

For **how to add a mode** or **debug**, see **`AGENTS.md`**.

---

## Core behavior (technical summary)

### Timing

- **SysTick** in `main.c` → `millis()` (1 ms).
- **Tap / external clock (PB3):** `tap.c` or `ext_clock.c` → **`input_handler.c`** → **`clock_manager_set_internal_tempo()`** — new interval, beat grid aligned to the event timestamp; **`f1_tick_counter` is not cleared**.
- **External clock:** `ext_clock.c` validates intervals; `input_handler.c` overrides tap while valid; on timeout, reverts using last valid external or last tap interval where applicable.

### Clock

- **Main beat** on outputs **1A** and **1B** at the active tempo interval.
- Each beat, the active mode receives timing context from the firmware (edge flags, swap state, etc.); see **`AGENTS.md`** for structure and hooks.

### Persistence

- **`krono_state_t`** in Flash (magic + checksum). Save is requested through the **mode-change UI timeout** path (see Quick start). It includes mode-specific parameters, including MOD-driven states for modes 11–20. Cooldown: `SAVE_STATE_COOLDOWN_MS` in `variables.h`.

### Inputs (detail)

- **Op-mode sequence:** `input_handler.c` state machine (`handle_op_mode_sm`): hold tap (≥ `OP_MODE_TAP_HOLD_DURATION_MS`) → optional continued hold to `OP_MODE_TAP_OMEGA_HOLD_MS` for Omega (modes 11–20) or to `OP_MODE_TAP_GAMMA_HOLD_MS` for Gamma (modes 21–30) → release → 5 s save window or MOD clicks → TAP confirm; confirm timeout can abort to previous mode. Omega uses the same soft Aux blink as qualify; Gamma arms a double pulse via `krono_aux_led_pattern_start()` — see **`AGENTS.md`**.
- **Calc swap:** short **MOD** (or **PB4** mirroring **MOD**); **Fixed** uses **MOD** for **bank**; **modes 12–20** and **21–30** use short **MOD** for **mode gestures** (see table); otherwise **MOD** triggers **calculation swap** when the UI is idle.

---

## Version history

See **`CHANGELOG.txt`** for line-by-line release notes.

---

## License

Summary at the top of this file; full text in **`LICENSE.txt`**.
