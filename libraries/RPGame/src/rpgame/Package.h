#pragma once
#include <stdint.h>
#include "Flash.h"
namespace pkg {
constexpr uint32_t FAMILY = 0xe48bff5a, LAYOUT = 0x52504703;
struct Header {
    uint8_t magic[8];
    uint32_t version, family, layout, offset, bytes, payloadCrc;
    char title[32], author[32], release[16];
    uint8_t reserved[396];
    uint32_t crc;
};
static_assert(sizeof(Header) == 512, "Package header size");
struct Reader {
    void *ctx;
    uint32_t size;
    bool (*read)(void *, uint32_t offset, uint8_t *dst, uint32_t bytes);
};
enum Error { OK, IO, FORMAT, TARGET, LENGTH, HEADER_CRC, PAYLOAD_CRC, FLASH, IMAGE };
const char *message(Error);
uint32_t crc32(const uint8_t *, uint32_t);
Error header(const Reader &, Header &);
Error validate(const Reader &, Header &);
// Latest metadata wins even if its state says installation was interrupted.
bool installed(Header &);
bool verifyInstalled();
typedef void (*Progress)(uint32_t done, uint32_t total);
Error install(const Reader &, Progress = nullptr);
}
