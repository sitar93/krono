/*
 * NOTE (Project Krono): This file (and bkp.h) appear to be legacy code,
 * likely adapted from libopencm3's STM32F1 implementation or an earlier
 * project version. The KRONO project targets STM32F4, where backup register
 * access is handled differently (typically via the RTC peripheral).
 *
 * The functional equivalent for KRONO is implemented in src/drivers/rtc.c.
 * These bkp.c/h files are NOT currently used by the main KRONO firmware
 * and are kept primarily for historical reference. They were likely created
 * before the RTC-based persistence mechanism was fully implemented.
 *
 * Original Copyright follows:
 * Copyright (C) 2012, Karl Palsson <karlp@tweak.net.au>
 * Copyright (C) 2012, Alexander Nusov <alexander.nusov@gmail.com>
 * Based on code from STMicroelectronics
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT of THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
// #include <libopencm3/stm32/f1/bkp.h> // Original header replaced by local
#include "bkp.h" // Include local header for prototypes
#include <libopencm3/stm32/memorymap.h> // Needed for BKP_BASE definition
#include <libopencm3/cm3/common.h> // Needed for MMIO16

// Define BKP base address here
#ifndef BKP_BASE
#define BKP_BASE (PERIPH_BASE_APB1 + 0x6C00)
#endif

/** @brief Read a backup register.
 *
 * These are 16-bit registers in a separate power domain. They will retain
 * their values through standby mode, and through VDD power cycles if VBAT is
 * supplied.
 *
 * @note If the backup domain hasn't been enabled, this will enable the clocks
 * for the power controller and the backup domain, and enable the backup domain.
 *
 * @param[in] reg u16. Backup Register number offset (e.g., BKP_DR1 defined in bkp.h).
 * @returns The 16-bit value currently stored in the specified register.
 */
uint16_t bkp_read_data_register(uint16_t reg) // reg is now the offset
{
	/* Make sure the clocks are running */
	rcc_periph_clock_enable(RCC_PWR);
	rcc_periph_clock_enable(RCC_BKP);

	return MMIO16(BKP_BASE + reg); // Use offset directly
}

/** @brief Write a backup register.
 *
 * These are 16-bit registers in a separate power domain. They will retain
 * their values through standby mode, and through VDD power cycles if VBAT is
 * supplied.
 *
 * @note If the backup domain hasn't been enabled, this will enable the clocks
 * for the power controller and the backup domain, and enable the backup domain.
 *
 * @param[in] reg u16. Backup Register number offset (e.g., BKP_DR1 defined in bkp.h).
 * @param[in] data u16. Value to write into the register.
 */
void bkp_write_data_register(uint16_t reg, uint16_t data) // reg is now the offset
{
	/* Make sure the clocks are running */
	rcc_periph_clock_enable(RCC_PWR);
	rcc_periph_clock_enable(RCC_BKP);

	/* Write permission is required */
	pwr_disable_backup_domain_write_protect();

	MMIO16(BKP_BASE + reg) = data; // Use offset directly

	/* Write permission should be disabled */
	pwr_enable_backup_domain_write_protect();
}
