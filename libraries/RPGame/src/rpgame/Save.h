// Saving: a record in flash that survives power cycles and re-uploads.
//
// Two reserved 4 KiB flash sectors hold alternating 256-byte records.
// Both standalone and menu linker profiles stop below these sectors.
// Each record contains a game's magic, version, sequence, flag and CRC.
// There are two shared slots, not a separate save bank for every game.
// Installing a package preserves the slots; another game's saves may
// replace them. A torn write leaves the other sector intact.
//
//     struct SaveData { Options opt; Stats stats; };     // up to 244 bytes
//     const uint32_t MAGIC = save::magic("CHXX");
//
//     SaveData d;
//     if (save::load(MAGIC, 1, d)) { ... }                // at start-up
//     save::store(MAGIC, 1, d);                           // after gfx_wait()
//
// Change the version when SaveData changes shape: old records are then
// ignored instead of misread. A flag byte travels in the header (most of
// the casino games mark "a game in progress" with it).
//
// The page is built in RPGfx's chunk scratch, so store() must run between
// gfx_wait() and the next flush. read() points straight into flash: a game
// can take just the part it needs.
#pragma once
#include <stdint.h>

namespace save {

static const uint16_t MAX_DATA = 256 - 12;     // a page less the header and CRC

// Four characters as a magic number ("CHCR" -> 0x52434843).
constexpr uint32_t magic(const char (&s)[5]) {
    return (uint32_t)(uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 |
           (uint32_t)(uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24;
}

bool available();                   // false: the image is too big, or a write failed

// The newest valid record with this magic, version and data size: its data
// (in flash), or nullptr. *flag gets the header's flag byte.
const void *read(uint32_t magic, uint8_t version, uint16_t size, uint8_t *flag = nullptr);
// Writing in place: fill buffer() (zeroed, MAX_DATA bytes), then write().
void *buffer();
bool write(uint32_t magic, uint8_t version, uint16_t size, uint8_t flag = 0);

// The same for a struct, copied.
template <class T> bool load(uint32_t magic, uint8_t version, T &data, uint8_t *flag = nullptr) {
    static_assert(sizeof(T) <= MAX_DATA, "save data must fit one flash page");
    const T *p = (const T *)read(magic, version, sizeof(T), flag);
    if (!p) return false;
    data = *p;
    return true;
}
template <class T> bool store(uint32_t magic, uint8_t version, const T &data, uint8_t flag = 0) {
    static_assert(sizeof(T) <= MAX_DATA, "save data must fit one flash page");
    if (!available()) return false;
    *(T *)buffer() = data;
    return write(magic, version, sizeof(T), flag);
}

}  // namespace save
