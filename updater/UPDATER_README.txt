# KRONO Updater

A web-based firmware updater for the KRONO Eurorack module using WebUSB and STM32 DFU protocol.

## Overview

The KRONO Updater is a browser-based tool that allows users to update their KRONO Eurorack module firmware directly from a web browser without requiring any software installation. It implements the STM32 DFU (Device Firmware Update) protocol following the official AN3156 specification.

## Features

- **No Installation Required**: Runs entirely in the browser using WebUSB API
- **STM32F411 Support**: Specifically designed for the STM32F411 microcontroller
- **Automatic Firmware Download**: Fetches latest firmware from GitHub releases
- **Cross-Platform**: Works on Windows, macOS, and Linux (with supported browsers)
- **Progress Tracking**: Real-time programming progress with detailed status messages
- **Mobile Support**: Compatible with Android devices via USB OTG

## Browser Compatibility

### Supported Browsers
- **Chrome 61+** (Recommended)
- **Microsoft Edge 79+** (Chromium-based)
- **Opera 48+**
- **Chrome for Android** (with USB OTG adapter)

### Unsupported Browsers
- Firefox (all versions)
- Safari (all versions)
- Internet Explorer
- iOS devices (iPhone/iPad)

**Note**: This updater requires WebUSB API support and HTTPS connection. Chrome is recommended for best compatibility.

## Requirements

- Supported web browser (see compatibility above)
- USB-C cable capable of data transfer
- KRONO module
- Windows users: WinUSB driver installation (first time only)

## Windows Setup (First Time Only)

Windows users need to install the WinUSB driver before using the updater for the first time:

1. **Download Zadig**: Get it from [https://zadig.akeo.ie/](https://zadig.akeo.ie/)
2. **Connect KRONO in DFU Mode**:
   - Connect KRONO via USB-C cable on the back
   - Hold BOOT button (left), press RESET button (right) once, release both
3. **Run Zadig as Administrator**: Right-click → "Run as administrator"
4. **Configure Driver**:
   - Click "Options" → "List All Devices"
   - Select "STM32 BOOTLOADER" device
   - Ensure "WinUSB" is selected as driver
   - Click "Replace Driver" or "Install Driver"
5. **Complete Installation**: Wait for confirmation and close Zadig

## Usage Instructions

### Step 1: Download Latest Firmware
1. Click "DOWNLOAD LATEST FIRMWARE" button
2. The browser will download the latest firmware from GitHub
3. Note the downloaded file location

### Step 2: Prepare KRONO
1. Connect KRONO to computer via USB-C cable (back of module)
2. Enter DFU mode:
   - Hold BOOT button (left)
   - Press RESET button (right) once
   - Release both buttons

### Step 3: Initialize Connection
1. Click "START" button
2. Authorize USB access when browser prompts
3. Wait for "STM32F411 connected and ready for firmware update" message

### Step 4: Upload Firmware
1. Select the downloaded firmware file (.bin, .hex, or .dfu)
2. Click "PROGRAM" button
3. Wait for programming to complete
4. Disconnect KRONO when "Programming Complete" appears

## Technical Implementation

### STM32 DFU Protocol
The updater implements the official STM32 DFU protocol (AN3156) with:
- Page erase before programming
- Address pointer setting
- Chunk-based data transfer (2048 bytes default)
- Manifestation phase handling
- Error recovery mechanisms

### Key Components

#### STM32DFU Class
- **Connection Management**: WebUSB device enumeration and interface claiming
- **Protocol Implementation**: DFU state machine and command handling
- **Error Handling**: Retry mechanisms and state recovery
- **Progress Tracking**: Real-time upload progress reporting

#### File Format Support
- **Binary (.bin)**: Raw firmware data
- **Intel HEX (.hex)**: Hexadecimal format
- **DFU (.dfu)**: DFU file format with automatic suffix removal

#### Safety Features
- Device state validation
- Transfer size optimization
- Automatic retry on communication errors
- Comprehensive error reporting

## Configuration

### Default Settings
```javascript
transferSize: 2048,        // Bytes per transfer
maxRetries: 5,             // Communication retry attempts
statusCheckDelay: 35,      // Status polling interval (ms)
flashAddress: 0x08000000,  // STM32F411 flash start address
```

### Customizable Parameters
- Transfer chunk size (auto-detected from device)
- Retry count for failed operations
- Status polling intervals
- Flash memory addressing

## Troubleshooting

### Common Issues

**"No STM32 DFU Device Found"**
- Ensure KRONO is in DFU mode (BOOT + RESET sequence)
- Check USB cable supports data transfer
- Verify browser compatibility
- Windows: Confirm WinUSB driver installation

**"Authorization Denied"**
- Click "Connect" when browser requests USB permission
- Try refreshing page and reconnecting
- Ensure HTTPS connection

**Programming Fails**
- Verify firmware file compatibility with STM32F411
- Check USB connection stability
- Try refreshing page and starting over
- Ensure KRONO remains in DFU mode during programming

### Alternative Tool
If browser compatibility issues occur, use ST's official [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) as an alternative.

## Development

### File Structure
```
krono_updater.html
├── HTML Structure
│   ├── Browser compatibility info
│   ├── Windows driver setup (accordion)
│   ├── Step-by-step instructions
│   └── Status display
├── JavaScript Implementation
│   ├── STM32DFU class
│   ├── WebUSB integration
│   ├── File handling
│   └── UI event handlers
└── CSS Styling
    ├── Responsive design
    ├── Status message styling
    └── Mobile optimizations
```

### Key Functions
- `STM32DFU.connect()`: Device enumeration and connection
- `STM32DFU.download()`: Firmware programming with progress
- `pageErase()`: Flash memory preparation
- `setAddressPointer()`: Memory addressing setup
- `waitForState()`: DFU state machine handling

## Security Considerations

- HTTPS required for WebUSB access
- No data persistence in browser storage
- Direct device communication without intermediary servers
- Firmware verification through DFU protocol checksums

License
This project is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0).

You are free to:

Share — copy and redistribute the material in any medium or format
Adapt — remix, transform, and build upon the material

Under the following terms:

Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made
NonCommercial — You may not use the material for commercial purposes
ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions under the same license as the original

This project implements the open STM32 DFU protocol specification and is designed for use with the KRONO Eurorack module.
For the full license text, visit: https://creativecommons.org/licenses/by-nc-sa/4.0/

## Support

For issues related to:
- **KRONO Module**: Contact info@jolin.tech
- **Browser Compatibility**: Check WebUSB support status
- **Windows Drivers**: Refer to Zadig documentation
- **Firmware Updates**: Check GitHub releases for latest versions and notes