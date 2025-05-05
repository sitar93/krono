#include "delay.h"

// The system_ticks variable is declared as extern in delay.h
// extern volatile uint32_t system_ticks;

// Simple busy-wait delay (approximate)
void delay_ms(uint32_t ms) {
    // This factor is highly dependent on clock speed and optimization level.
    // Calibration might be needed. This is a rough estimate for 72MHz.
    for (uint32_t i = 0; i < ms; ++i) {
        for (volatile uint32_t j = 0; j < 7200; ++j) { // Adjust inner loop count based on testing
            __asm__ volatile ("nop");
        }
    }
}

// Inline function to get current tick count, potentially defined in header
// uint32_t millis(void) {
//     return system_ticks;
// }
