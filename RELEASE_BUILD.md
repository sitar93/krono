# KRONO Release Build Guide

This guide explains how to generate the release files for KRONO firmware.

## Prerequisites

- PlatformIO installed
- STM32F411CE target environment configured

## Build Commands

### Standard Build

```bash
platformio run -e blackpill_f411ce
```

This generates:
- `firmware.bin` - Binary firmware
- `firmware.elf` - ELF debug symbols
- `firmware.hex` - Intel HEX format

### Clean Build (Recommended for Release)

Removes previous build artifacts before building:

```bash
platformio run -e blackpill_f411ce --target clean
platformio run -e blackpill_f411ce
```

### Output Files

After build, files are located in `.pio/build/blackpill_f411ce/`:

| File | Size | Description |
|------|------|-------------|
| `krono_code_vX.X.X.bin` | ~15 KB | Binary firmware for DFU upload |
| `krono_code_vX.X.X.elf` | ~156 KB | ELF with debug symbols |
| `krono_code_vX.X.X.hex` | ~44 KB | Intel HEX format |

The version number is automatically extracted from the last non-empty line in `CHANGELOG.txt`.

## Upload Commands

### DFU Upload (Requires Board in DFU Mode)

1. Hold BOOT0 button
2. Press NRST (reset)
3. Release BOOT0

Then run:

```bash
platformio run -e blackpill_f411ce --target upload
```

### Serial Monitor

For debugging output:

```bash
platformio device monitor
```

Requires separate USB-Serial adapter connected to:
- RX: PA9 (TX)
- GND: GND

## Memory Usage

Current build statistics:
- RAM: 1.0% (1336 bytes / 128KB)
- Flash: 3.0% (15524 bytes / 512KB)

## Troubleshooting

### Strange Build Errors

Run clean build:

```bash
platformio run -e blackpill_f411ce --target clean
platformio run -e blackpill_f411ce
```

### DFU Not Detected

Ensure board is in DFU mode:
1. Hold BOOT0
2. Press NRST
3. Release BOOT0

Check with: `dfu-util -l`
