#include "Flash.h"
#include <string.h>
#ifdef CHSIM
extern const uint8_t *rpgame_host_flash_read(uint32_t);
extern bool rpgame_host_flash_write(uint32_t, const uint8_t *, uint32_t);
#else
#include <Arduino.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#if !defined(PICO_RP2350) || !defined(__riscv)
#error "RPGame 0.3 storage/launcher targets RP2350 RISC-V; use 0.2 for other targets"
#endif
static_assert(PICO_FLASH_SIZE_BYTES == nv::FLASH_BYTES, "Select 4 MB flash, no filesystem");
static_assert(FS_START == FS_END, "RPGame launcher/storage requires no internal filesystem");
#endif
namespace nv {
const uint8_t *read(uint32_t offset) {
    if (offset >= FLASH_BYTES) return nullptr;
#ifdef CHSIM
    return rpgame_host_flash_read(offset);
#else
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
#endif
}
bool writeSector(uint32_t offset, const uint8_t *data, uint32_t bytes) {
    if (!data || !bytes || bytes > SECTOR || offset % SECTOR ||
        offset < APP_OFFSET || offset > WRITABLE_END - SECTOR) return false;
#ifdef CHSIM
    return rpgame_host_flash_write(offset, data, bytes);
#else
    if (get_core_num() != 0) return false;
    // Arduino-Pico's core-1 FIFO coordination, as used by its EEPROM.
    alignas(4) static uint8_t sector[SECTOR];
    static bool busy = false;
    uint32_t irq = save_and_disable_interrupts();
    if (busy) { restore_interrupts(irq); return false; }
    busy = true;
    restore_interrupts(irq);
    memset(sector, 0xff, sizeof sector);
    memcpy(sector, data, bytes);
    if (memcmp(read(offset), sector, SECTOR) != 0) {
        irq = save_and_disable_interrupts();
        rp2040.idleOtherCore();
        flash_range_erase(offset, SECTOR);
        flash_range_program(offset, sector, SECTOR);
        rp2040.resumeOtherCore();
        restore_interrupts(irq);
    }
    bool ok = memcmp(read(offset), sector, SECTOR) == 0;
    busy = false;
    return ok;
#endif
}
}
