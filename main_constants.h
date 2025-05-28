#pragma once

// --- System Timing ---
#define SYSTEM_TICK_MS 1             // System tick interval in milliseconds
#define DEFAULT_TEMPO_BPM 120.0f
#define DEFAULT_TEMPO_INTERVAL (uint32_t)(60000.0f / DEFAULT_TEMPO_BPM) // Default interval in ms

// --- Input Handling Timing ---
#define DEBOUNCE_DELAY_MS 50         // Debounce delay for buttons
#define MODE_SWITCH_HOLD_DURATION_MS 500 // Hold time for both buttons for op mode change
#define CALC_SWAP_MAX_PRESS_DURATION_MS (MODE_SWITCH_HOLD_DURATION_MS - 10) // Max duration for calc swap short press
#define CALC_SWAP_COOLDOWN_MS 200    // Minimum time between calc swap triggers (button or gate)
#define TAP_TIMEOUT_MS 5000          // Time after last tap to reset calculation (5 seconds)

// --- External Clock Timing ---
#define EXTERNAL_CLOCK_TIMEOUT_MS 2000 // Time after last external clock edge to revert to tap tempo

// --- Tap Tempo Calculation ---
#define NUM_INTERVALS_FOR_AVG 4      // Number of tap intervals to average
#define MIN_INTERVAL 100             // Minimum tap/clock interval in ms (600 BPM)
#define MAX_INTERVAL 3000            // Maximum tap/clock interval in ms (20 BPM)
#define MAX_INTERVAL_DIFFERENCE 100  // Max difference between min/max interval in average set (ms)

// --- Pulse Widths ---
#define CLOCK_PULSE_WIDTH_MS 5       // Default width for generated clock pulses

// --- Status LED Timing ---
#define STATUS_LED_BLINK_INTERVAL_MS 500 // Base interval for LED blinking
#define STATUS_LED_PULSE_WIDTH_MS 50   // Duration the LED stays on during a blink

// --- Persistence --- //
// Define the flash page address for storing the state.
// For STM32F103C8 (64KB Flash), last page is at 0x0800FC00 (Page 63).
// For STM32F401CE (512KB Flash), last sector (Sector 7) starts at 0x08060000.
// Use the start of the last sector for F4.
#define PERSISTENCE_FLASH_PAGE_ADDR 0x08060000
#define PERSISTENCE_MAGIC_NUMBER 0xDEADBEEF

