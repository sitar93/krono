#ifndef LIBOPENCM3_BKP_LOCAL_H
#define LIBOPENCM3_BKP_LOCAL_H

/*
 * NOTE (Project Krono): This header (and bkp.c) appear to be legacy code
 * from an earlier version or an adaptation from STM32F1 examples.
 * The KRONO project uses the RTC backup registers via functionality
 * defined in src/drivers/rtc.h and implemented in src/drivers/rtc.c.
 *
 * This file is NOT currently included or used by the main KRONO firmware.
 */

#include <stdint.h> // Include for uint16_t

// Define only the necessary register IDs used by main.c
#define BKP_DR1                         0x04
#define BKP_DR2                         0x08
#define BKP_DR3                         0x0C // Added for Calculation Mode
#define BKP_DR4                         0x10
#define BKP_DR5                         0x14

// Function prototypes
uint16_t bkp_read_data_register(uint16_t reg);
void bkp_write_data_register(uint16_t reg, uint16_t data);

#endif // LIBOPENCM3_BKP_LOCAL_H
