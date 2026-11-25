#ifndef TAP_CALIB_STORE_H
#define TAP_CALIB_STORE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Personal tap-latency calibration, stored in its own flash sector (sector 6, 0x08040000).
 *
 * The online updater and DFU uploads only erase the sectors spanned by the firmware image (well below
 * 0x08040000), and the normal state save uses sector 7, so this value survives mode/tempo saves, firmware
 * updates and future changes of the settings format. Only a full chip erase clears it.
 */
#define TAP_CALIB_FLASH_ADDR 0x08040000u
#define TAP_CALIB_FLASH_SECTOR 6u

/** Load at boot. Returns the stored offset (ms) or 0 if none / invalid. */
int32_t tap_calib_load(void);

/** Current offset in ms (beats are placed this much after the fitted taps; positive = player taps early). */
int32_t tap_calib_get_offset_ms(void);

/** Erase sector 6 and store a new offset (blocks ~1-2 s with IRQs masked). Returns true on verified write. */
bool tap_calib_save(int32_t offset_ms);

#endif
