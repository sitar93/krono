# KRONO - Multi-Modal Programmable Pattern Generator

[![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

KRONO is a 4HP programmable multi-mode clock and rhythm generator with tap tempo, external sync, per-mode state persistence, CV group swap, 12 independent outputs and, since v1.5, LFO / audio generation on selected outputs.

[cc-by-nc-sa]: https://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg

## Why KRONO

- Copy, sync, divide, and multiply clocks with algorithmic flexibility.
- Generate complex synchronized rhythm patterns on multiple outputs.
- Explore Euclidean, chaos, polyrhythm, and musical-ratio timing models.
- Keep your system in sync using tap tempo or an external clock.
- Save and recall the state for each mode across power cycles.
- Generate self-evolving LFOs, chaotic CV and lo-fi audio voices (DELTA tier).

## Main features

- 12 independent outputs (1A-6A, 1B-6B).
- 40 operational modes grouped in:
  - **ALPHA**: modes 1-10 (pulse origins / timing logic)
  - **BETA**: modes 11-20 (drum pattern variations)
  - **OMEGA**: modes 21-30 (sequential logic)
  - **DELTA**: modes 31-40 (self-evolving modulation and audio)
- Precise tap tempo (refines with every tap) and external clock sync.
- Optional personal tap latency calibration.
- Persistent state for tempo, active mode, swap state, and mode parameters.
- Fast-Skip mode selection up to 40 modes with max 10 clicks.
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
- External clock has priority when a stable master is present (hard sync per edge); if the master drops or goes out of range, Krono keeps the last tempo immediately (flywheel). Autopatch of 1A/1B into Clock In keeps running on internal timing.
- In OMEGA modes (21-30), outputs `1A/1B` may be mode-driven (not always plain base clock mirror).
- Clock In and MOD IN react to the rising edge of the external signal.
- DELTA modes (31-40) turn `1A`, `2A`, `2B` (and `3A` for CV) into analog outputs (0 to ~8 V, unipolar) using
  high-frequency PWM smoothed by the existing output stage; `1B` = base clock, `3B` = counter-phase clock.

## Quick start

### 1) Set base tempo

- **Tap tempo**: press `TAP` at least 4 times rhythmically. The tempo locks on the 4th tap; every further tap
  refines it smoothly. See **Tap tempo** below.
- **External clock**: send clock to `TAP IN`. KRONO validates and copies the external tempo.

In modes 1-20, base tempo is available on outputs `1A` and `1B`; in DELTA modes (31-40) on `1B`.

### 2) Select mode

1. Hold `TAP` for at least 1 second, then release.
2. Click `MOD` X times to pick mode X.
3. Click `TAP` to confirm and activate.

Status LED blink encoding:
- Short blink = units
- Long blink = tens
- Example: mode 2 = 2 short blinks; mode 12 = 1 long + 2 short; mode 30 = 3 long; mode 40 = 4 long.

While selecting, both LEDs are off and the MOD LED lights while MOD is pressed. On confirm, both LEDs stay lit
while the selection is saved.

### 3) Fast-Skip (mode families)

Hold `TAP` and release at the desired tier:

- ~1s: **ALPHA** (modes 1-10) - MOD LED 1 blink
- ~2s: **BETA** (modes 11-20) - 2 quick blinks
- ~3s: **OMEGA** (modes 21-30) - 3 quick blinks
- ~4s: **DELTA** (modes 31-40) - 4 quick blinks

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

### DELTA (31-40): Modulation and audio

Self-evolving modes. `1B` = base clock, `3B` = counter-phase clock. MOD selects the variant shown in the last column
(persisted).

| Mode | Info | MOD |
|---|---|---|
| **31 TIDES** | 4 tempo-synced LFOs (1A: 1 beat, 2A: 4, 2B: 16, 3A: 2). The waveform slowly morphs into the next one. Gates on cycle starts and peaks | waveform |
| **32 ORBIT** | One sine at 4 phases (1A, 2A, 2B, 3A) rotating across the outputs. Direction reverses and speed drifts | rotation length |
| **33 CHIMERA** | Morphing wavetables (3A = morph position). The bank changes every 8 bars | bank |
| **34 WANDER** | Random voltages at different rates. Range and change probability evolve | smooth / stepped / slewed |
| **35 BUTTERFLY** | Lorenz attractor (x, y, z, speed). Drifts between calm and chaotic phases | speed |
| **36 MONOLITH** | Modal drone with a slow chord progression and very slow glides. Analog voices morph sine -> saw -> triangle and breathe; pulse voices with slow PWM swell in and out | progression on / frozen |
| **37 STATIC** | Noise and crackle that change colour and density | rain / wind / radio / geiger |
| **38 PULSAR** | Drum machine (analog kick, zap and tom, plus 1-bit hats, snare and blips). Patterns mutate, with fills | techno / broken / half-time / glitch |
| **39 ALLOY** | Two oscillators with sum, ring mod and logic combinations. The interval glides over time | evolution speed |
| **40 ASCENT** | Arpeggiator that mutates notes and progression, with rests and ratchets | scale |

Audio modes (36-40) follow the tempo for pitch (the beat rate, octave-shifted and snapped to the nearest semitone),
output unipolar signals (patch into AC-coupled inputs or attenuate) and stay silent for 2 s after a mode change or
a save.

## I/O cheat sheet

- **TAP button**: set tempo, enter mode selection, confirm mode.
- **MOD button / MOD IN**: mode parameter actions.
- **TAP IN**: external clock sync.
- **Status LED** (under TAP): active mode indication.
- **Aux LED** (under MOD): feedback for tap, mode changes, parameter updates and save events, plus the state of
  the current mode: steady = freeze / stop / B bank active, breathing = continuous modulation, looping blinks =
  selected bank / pattern / profile number.
- **OUT 1A / 1B**: base clock reference (modes 1-20); in DELTA modes `1B` = base clock, `3B` = counter-phase clock.

## Tap tempo

Tap the **TAP** button along with the music and KRONO follows you.

- **Tap 4 times** in time with the beat. On the 4th tap the tempo locks and KRONO starts playing from that tap
  (MOD LED double flash). **Keep tapping** for more precision: every extra tap refines the tempo with no jumps in
  the rhythm. After 16-32 taps KRONO stays in time with the music for a long time, even after you stop.
- **Back in phase:** if KRONO slowly drifts away from the music, tap a few times along with the beat; it realigns
  smoothly within a beat or two, even after a long pause.
- **Mistakes are forgiven:** a single off-beat tap is ignored; skipped beats are counted correctly.
- **New tempo:** just start tapping it. After two taps that do not fit the old tempo KRONO starts over, and it
  locks to the new tempo about four taps in.
- **Round tempos:** when your taps are very close to a whole BPM value (120, 128...), KRONO settles exactly on it.
- With an external clock in **TAP IN**, the clock takes priority and TAP is ignored. When the clock is removed,
  KRONO keeps its tempo and phase.

## Tap latency calibration (optional)

Most people tap slightly ahead of the beat they hear. KRONO can measure your personal timing and compensate for it.

1. Hold **MOD** while powering on, for about 2 s, until the two LEDs blink alternately. Release MOD.
2. Outputs **1A** and **1B** click at 100 BPM (TAP LED lit). Tap **TAP** 16 times along with the click (patching
   1A into a sound and tapping to what you hear works best). The first 4 taps are a warm-up.
3. **Success:** both LEDs give two quick bursts of three flashes, the value is stored and KRONO starts normally.
   **Try again:** both LEDs lit for 1 s (taps too irregular, or you sped up / slowed down), then the click restarts.
   **Cancel:** press MOD (one long flash); the procedure also cancels itself after 40 s. The previous calibration is kept.

The calibration survives power cycles, saves and firmware updates via the online updater; it only affects tap
tempo. Without calibration no compensation is applied.

## Build firmware

Requires [PlatformIO](https://platformio.org/):

```bash
platformio run -e blackpill_f411ce
platformio run -e blackpill_f411ce --target upload
```

DFU upload workflow (if needed): hold `BOOT0`, pulse `NRST`, release `BOOT0`, then upload.

## Firmware structure

- `src/main.c` - boot, loop, callbacks, save/load glue.
- `src/input_handler.c` - mode selection state machine, MOD/CV input routing, external clock lock.
- `src/input_tempo.c` - tap tempo (least-squares fit).
- `src/clock_manager.c` - clock scheduling and mode update dispatch.
- `src/aux_led.c`, `src/aux_led_mode_status.c` - MOD LED rendering and per-mode status.
- `src/tap_calibration.c` - power-on tap latency calibration.
- `src/modes/` - one file per mode behavior (`mode_delta.c` / `mode_delta_audio.c` for 31-40).
- `src/drivers/` - low-level I/O, tap, external clock, persistence, analog outputs (PWM DAC), 1-bit audio voices,
  calibration storage.
- `src/variables.h` - timing constants and tunables.

## Links

- Demos and documentation: [jolin.tech/KRONO](https://jolin.tech/KRONO)
- VCV Rack plugin: [github.com/sitar93/krono_vcv](https://github.com/sitar93/krono_vcv)

## License

This project is licensed under [CC BY-NC-SA 4.0][cc-by-nc-sa].
