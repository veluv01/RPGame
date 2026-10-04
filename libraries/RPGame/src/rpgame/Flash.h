#pragma once
#include <stdint.h>
namespace nv {
constexpr uint32_t FLASH_BYTES = 0x400000;
constexpr uint32_t APP_OFFSET = 0x080000;
constexpr uint32_t APP_END = 0x3F0000;
constexpr uint32_t SAVE_A = APP_END;
constexpr uint32_t SAVE_B = APP_END + 0x1000;
constexpr uint32_t META_A = APP_END + 0x2000;
constexpr uint32_t META_B = APP_END + 0x3000;
constexpr uint32_t WRITABLE_END = META_B + 0x1000;
constexpr uint32_t SECTOR = 4096;
const uint8_t *read(uint32_t offset);
// Sector-aligned offset; 1..4096 bytes. Unused bytes become 0xff.
// The launcher and the core's EEPROM/filesystem area are excluded.
bool writeSector(uint32_t offset, const uint8_t *data, uint32_t bytes);
}
