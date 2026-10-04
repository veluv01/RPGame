// Saving: see Save.h. The record is
//   u32 magic; u8 version, flag; u16 seq; data; (pad to 4) u32 crc
// with the CRC (CRC-32, reflected) over everything before it.
#include "Save.h"
#include <Arduino.h>
#include <RPGfx.h>
#include <string.h>
#include "RamFunc.h"
#include "Flash.h"

namespace save {

static const uint32_t PAGE = 256;
static const uint32_t PAGE_A = nv::SAVE_A, PAGE_B = nv::SAVE_B;

struct Header { uint32_t magic; uint8_t version, flag; uint16_t seq; };

static uint16_t lastSeq = 0;
static bool broken = false;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t crcAt(uint16_t size) { return (uint32_t)(sizeof(Header) + size + 3) & ~3u; }

#ifndef CHSIM
extern "C" uint8_t __flash_binary_end;
bool available() { return !broken && uintptr_t(&__flash_binary_end) <= 0x10000000u + nv::SAVE_A; }
#else
bool available() { return !broken; }
#endif
static bool twoPages() { return true; }
static const uint8_t *page(uint32_t a) { return nv::read(a); }
static bool writePage(uint32_t addr, const uint8_t *buf) { return nv::writeSector(addr, buf, PAGE); }

static bool valid(const uint8_t *p, uint32_t magic, uint8_t version, uint16_t size) {
    const Header *h = (const Header *)p;
    uint32_t at = crcAt(size);                  // (word-aligned: pages are)
    return h->magic == magic && h->version == version && *(const uint32_t *)(p + at) == crc32(p, at);
}

const void *read(uint32_t magic, uint8_t version, uint16_t size, uint8_t *flag) {
    if (!available() || size > MAX_DATA) return nullptr;
    const uint8_t *a = page(PAGE_A), *b = page(PAGE_B);
    bool va = twoPages() && valid(a, magic, version, size), vb = valid(b, magic, version, size);
    const uint8_t *r = nullptr;
    if (va && vb) r = (int16_t)(((const Header *)a)->seq - ((const Header *)b)->seq) > 0 ? a : b;
    else r = va ? a : (vb ? b : nullptr);
    if (!r) return nullptr;
    const Header *h = (const Header *)r;
    lastSeq = h->seq;
    if (flag) *flag = h->flag;
    return r + sizeof(Header);
}

void *buffer() {
    gfx_wait();
    uint8_t *buf = gfx_chunkScratch();          // idle between gfx_wait() and the next flush
    memset(buf, 0, PAGE);
    return buf + sizeof(Header);
}

bool write(uint32_t magic, uint8_t version, uint16_t size, uint8_t flag) {
    if (!available() || size > MAX_DATA) return false;
    uint8_t *buf = gfx_chunkScratch();
    Header *h = (Header *)buf;
    h->magic = magic;
    h->version = version;
    h->flag = flag;
    h->seq = (uint16_t)(lastSeq + 1);
    uint32_t at = crcAt(size);
    *(uint32_t *)(buf + at) = crc32(buf, at);
    uint32_t addr = ((h->seq & 1) || !twoPages()) ? PAGE_B : PAGE_A;   // take turns
    if (!writePage(addr, buf)) { broken = true; return false; }
    lastSeq = h->seq;
    return true;
}

}  // namespace save
