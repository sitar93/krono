#include "rtc.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/rtc.h> // Include the main RTC header for bkp reg access
// #include <libopencm3/stm32/f4/rcc.h> // RCC functions likely included by rcc.h or rtc.h
// #include <libopencm3/stm32/f4/rtc.h> // RTC functions likely included by rtc.h

/**
 * @brief Initialize access to the Backup domain.
 * This is necessary to read/write RTC Backup Registers (RTC_BKPxR).
 */
void rtc_bkp_init(void) {
    // Enable Power Controller clock
    rcc_periph_clock_enable(RCC_PWR);
    // Disable Backup domain write protection
    pwr_disable_backup_domain_write_protect();

    // On F4, the backup domain needs the RTC clock to be enabled to access BKP registers.
    // Enable LSE oscillator and wait for it to be ready.
    rcc_osc_on(RCC_LSE);
    rcc_wait_for_osc_ready(RCC_LSE);

    // Select LSE as RTC clock source directly in Backup Domain Control Register (BDCR)
    RCC_BDCR |= RCC_BDCR_RTCSEL_LSE; 

    // Enable RTC clock directly in BDCR
    RCC_BDCR |= RCC_BDCR_RTCEN;
}

/**
 * @brief Write a 16-bit value to a specific Backup Register (RTC_BKPxR).
 * F4 uses 32-bit backup registers (RTC_BKPxR), we only use the lower 16 bits.
 * @param reg Backup register index (0-19 for F401).
 * @param data The 16-bit data to write.
 */
void rtc_bkp_write(uint8_t reg, uint16_t data) {
    // Backup register write protection is assumed to be disabled by rtc_bkp_init()
    // Direct register access:
    RTC_BKPXR(reg) = (uint32_t)data;
}

/**
 * @brief Read a 16-bit value from a specific Backup Register (RTC_BKPxR).
 * F4 uses 32-bit backup registers (RTC_BKPxR), we only read the lower 16 bits.
 * @param reg Backup register index (0-19 for F401).
 * @return uint16_t The 16-bit data read from the register.
 */
uint16_t rtc_bkp_read(uint8_t reg) {
    // Direct register access:
    return (uint16_t)(RTC_BKPXR(reg) & 0xFFFF);
}
