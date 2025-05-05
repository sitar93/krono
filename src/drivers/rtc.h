#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize access to the Backup domain (PWR clock enable, disable write protection).
 * Must be called before reading or writing backup registers.
 */
void rtc_bkp_init(void);

/**
 * @brief Write a 16-bit value to a specific Backup Register (RTC_BKPxR).
 * Assumes rtc_bkp_init() has been called previously.
 *
 * @param reg The backup register index (e.g., 1 for RTC_BKP1R).
 * @param data The 16-bit value to write.
 */
void rtc_bkp_write(uint8_t reg, uint16_t data);

/**
 * @brief Read a 16-bit value from a specific Backup Register (RTC_BKPxR).
 * Assumes rtc_bkp_init() has been called previously.
 *
 * @param reg The backup register index (e.g., 1 for RTC_BKP1R).
 * @return uint16_t The 16-bit value read from the register.
 */
uint16_t rtc_bkp_read(uint8_t reg);


#ifdef __cplusplus
}
#endif
