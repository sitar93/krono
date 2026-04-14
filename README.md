# KRONO - Multi-Modal Programmable Pattern Generator

[![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

KRONO is a 4HP programmable multi-mode clock and rhythm generator with tap tempo, external sync, per-mode state persistence, CV group swap, and 12 independent outputs.

[cc-by-nc-sa]: https://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg

## Why KRONO

- Copy, sync, divide, and multiply clocks with algorithmic flexibility.
- Generate complex synchronized rhythm patterns on multiple outputs.
- Explore Euclidean, chaos, polyrhythm, and musical-ratio timing models.
- Keep your system in sync using tap tempo or an external clock.
- Save and recall the state for each mode across power cycles.

## Main features

- 12 independent outputs (1A-6A, 1B-6B).
- 30 operational modes grouped in:
  - **ALPHA**: modes 1-10 (pulse origins / timing logic)
  - **BETA**: modes 11-20 (drum pattern variations)
  - **OMEGA**: modes 21-30 (sequential logic)
- Weighted tap tempo and external clock sync.
- Persistent state for tempo, active mode, swap state, and mode parameters.
- Fast-Skip mode selection up to 30 modes with max 10 clicks.
- STM32F411 platform.
- Open source firmware.

## Specs

- Width: 4HP
- Depth: 30mm
- Power draw: +12V 50mA, -12V 5mA

## Technical notes (firmware)

### Platform and toolchain

- MCU target: **STM32F411CE** (Black Pill-class on Krono PCB)
- Framework/toolchain: **PlatformIO + libopencm3**
- PlatformIO environment: `blackpill_f411ce`

### I/O mapping (firmware pins)

- `PA0` - TAP button
- `PA1` - MOD button (mode actions / swap)
- `PB3` - external clock input
- `PB4` - external gate input (mirrors MOD behavior)
- `PA15` - Status LED
- `PA3` - Aux LED
- Group A outputs (`1A..6A`): `PB0`, `PB1`, `PA2`, `PB15`, `PB5`, `PB6`
- Group B outputs (`1B..6B`): `PB14`, `PB13`, `PB12`, `PB8`, `PB9`, `PB10`

### Persistence and timing behavior

- State is saved in Flash (tempo, mode, swap, and mode-specific parameters).
- Save path: hold `TAP` to enter mode selection, then wait 5s without changes.
- External clock has priority when stable; on timeout, firmware falls back to internal timing.
- In OMEGA modes (21-30), outputs `1A/1B` may be mode-driven (not always plain base clock mirror).

## Quick start

### 1) Set base tempo

- **Tap tempo**: press `TAP` at least 4 times rhythmically. KRONO averages valid intervals.
- **External clock**: send clock to `TAP IN`. KRONO validates and copies the external tempo.

In modes 1-20, base tempo is available on outputs `1A` and `1B`.

### 2) Select mode

1. Hold `TAP` for at least 1 second, then release.
2. Click `MOD` X times to pick mode X.
3. Click `TAP` to confirm and activate.

Status LED blink encoding:
- Short blink = units
- Long blink = tens
- Example: mode 2 = 2 short blinks; mode 12 = 1 long + 2 short; mode 30 = 3 long.

### 3) Fast-Skip (mode families)

Hold `TAP` and release at the desired tier:

- ~1s: **ALPHA** (modes 1-10)
- ~2s: **BETA** (modes 11-20)
- ~3s: **OMEGA** (modes 21-30)

Then click `MOD` for index (1-10) and confirm with `TAP`.

### 4) Save state

Enter mode selection (`TAP` hold >1s), then wait 5s without changing mode.
KRONO saves tempo, swap, mode, and active mode parameters in flash.

## Operational modes

### ALPHA (1-10): Pulse origins

1. **RATIOS** - direct tempo multiplications/divisions
2. **EUCLIDEAN** - K pulses distributed over N steps
3. **MUSICAL** - musical timing ratios
4. **PROBABILISTIC** - trigger probability per output
5. **SEQUENTIAL** - mathematical sequences (e.g. Fibonacci, primes)
6. **SWING** - output-specific shuffle/swing timing
7. **POLYRHYTHM** - independent X:Y ratios per output
8. **LOGIC** - logic operations between internal clocks
9. **PHASING** - phase-shifted pulse groups
10. **CHAOS** - chaotic threshold-based trigger generation

### BETA (11-20): Drum pattern variations

11. **FIXED** - 10 banks of fixed 16-step drum patterns
12. **DRIFT** - fixed pattern with stochastic mutation
13. **FILL** - fill-oriented groove behavior
14. **SKIP** - probabilistic hit skipping
15. **STUTTER** - variable stutter loops
16. **MORPH** - continuously evolving generative stream
17. **MUTE** - additive/subtractive mute flow
18. **DENSITY** - sparse-to-dense trigger control
19. **SONG** - 8-bar structure (base + variation)
20. **ACCUMULATE** - progressive random output accumulation

### OMEGA (21-30): Sequential logic

21. **RESET** - 12-step sweep with reset
22. **FREEZE** - 12-step sweep with hold
23. **TRIP** - six scripted sequential patterns
24. **FIRE** - inverted pair cascade
25. **BOUNCE** - unison hit then split acceleration/deceleration
26. **PORTALS** - held-level alternating A/B doors
27. **COIN TOSS** - weighted A/B selection by column
28. **RATCHET** - A multiply / B divide, MOD doubles
29. **ANTI-RATCHET** - A multiply / B divide, MOD halves
30. **STARTnSTOP** - global mute with phase memory

## I/O cheat sheet

- **TAP button**: set tempo, enter mode selection, confirm mode.
- **MOD button / MOD IN**: mode parameter actions.
- **TAP IN**: external clock sync.
- **Status LED**: active mode indication.
- **Aux LED**: feedback for tap, mode changes, parameter updates, and save events.
- **OUT 1A / 1B**: base clock reference (modes 1-20).

## Build firmware

Requires [PlatformIO](https://platformio.org/):

```bash
platformio run -e blackpill_f411ce
platformio run -e blackpill_f411ce --target upload
```

DFU upload workflow (if needed): hold `BOOT0`, pulse `NRST`, release `BOOT0`, then upload.

## Firmware structure

- `src/main.c` - boot, loop, callbacks, save/load glue.
- `src/input_handler.c` - tap, mode selection state machine, MOD/CV input routing.
- `src/clock_manager.c` - clock scheduling and mode update dispatch.
- `src/modes/` - one file per mode behavior.
- `src/drivers/` - low-level I/O, tap, external clock, persistence, RTC.
- `src/variables.h` - timing constants and tunables.

## Links

- Demos and documentation: [jolin.tech/KRONO](https://jolin.tech/KRONO)
- VCV Rack plugin: [github.com/sitar93/krono_vcv](https://github.com/sitar93/krono_vcv)

## License

This project is licensed under [CC BY-NC-SA 4.0][cc-by-nc-sa].
