#pragma once
#include <stdint.h>

/**
 * @brief Simple millisecond delay function 
 * @param ms Number of milliseconds to delay
 */
void delay_ms(uint32_t ms);

// Declare the system tick variable defined in main.c
extern volatile uint32_t system_ticks;

// millis() is now declared globally in main_constants.h
// (The actual function definition is in main.c)
