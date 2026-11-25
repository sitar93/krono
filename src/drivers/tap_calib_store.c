#include "tap_calib_store.h"

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/flash.h>

/** Fixed, version-independent layout. Never reorder: older firmware must keep reading it. */
typedef struct {
    uint32_t magic;     // TAP_CALIB_MAGIC
    uint32_t version;   // Layout version (1)
    int32_t offset_ms;  // Human latency compensation
    uint32_t checksum;  // magic ^ version ^ offset ^ TAP_CALIB_XOR
} tap_calib_record_t;

#define TAP_CALIB_MAGIC 0x54415043u // "TAPC"
#define TAP_CALIB_VERSION 1u
#define TAP_CALIB_XOR 0xA5C3F00Fu
#define TAP_CALIB_LIMIT_MS 200

static int32_t calib_offset_ms = 0;

static uint32_t record_checksum(const tap_calib_record_t *r) {
    return r->magic ^ r->version ^ (uint32_t)r->offset_ms ^ TAP_CALIB_XOR;
}

int32_t tap_calib_load(void) {
    const tap_calib_record_t *r = (const tap_calib_record_t *)TAP_CALIB_FLASH_ADDR;
    calib_offset_ms = 0;
    if (r->magic == TAP_CALIB_MAGIC && r->version == TAP_CALIB_VERSION && r->checksum == record_checksum(r) &&
        r->offset_ms >= -TAP_CALIB_LIMIT_MS && r->offset_ms <= TAP_CALIB_LIMIT_MS) {
        calib_offset_ms = r->offset_ms;
    }
    return calib_offset_ms;
}

int32_t tap_calib_get_offset_ms(void) {
    return calib_offset_ms;
}

static bool wait_flash_ok(void) {
    uint32_t sr;
    do {
        sr = FLASH_SR;
    } while (sr & FLASH_SR_BSY);
    return (sr & (FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_WRPERR)) == 0u;
}

bool tap_calib_save(int32_t offset_ms) {
    if (offset_ms < -TAP_CALIB_LIMIT_MS || offset_ms > TAP_CALIB_LIMIT_MS) {
        return false;
    }
    tap_calib_record_t rec = {TAP_CALIB_MAGIC, TAP_CALIB_VERSION, offset_ms, 0u};
    rec.checksum = record_checksum(&rec);

    /* Mask IRQs for the whole erase+program window (single-bank F4 stalls fetch). */
    cm_disable_interrupts();
    flash_unlock();
    flash_erase_sector(TAP_CALIB_FLASH_SECTOR, 2);
    bool ok = wait_flash_ok();
    const uint32_t *src = (const uint32_t *)&rec;
    for (uint32_t i = 0; ok && i < sizeof(rec) / sizeof(uint32_t); i++) {
        flash_program_word(TAP_CALIB_FLASH_ADDR + i * 4u, src[i]);
        ok = wait_flash_ok();
    }
    flash_lock();
    cm_enable_interrupts();

    if (!ok) {
        return false;
    }
    const tap_calib_record_t *r = (const tap_calib_record_t *)TAP_CALIB_FLASH_ADDR;
    if (r->magic != rec.magic || r->offset_ms != rec.offset_ms || r->checksum != rec.checksum) {
        return false;
    }
    calib_offset_ms = offset_ms;
    return true;
}
