// Host model of RPGame flash: erased bytes and whole-sector writes.
#include <stdint.h>
#include <string.h>
#include <vector>
static std::vector<uint8_t> flash(0x400000, 0xff);
const uint8_t *rpgame_host_flash_read(uint32_t off) { return off < flash.size() ? flash.data() + off : nullptr; }
bool rpgame_host_flash_write(uint32_t off, const uint8_t *data, uint32_t n) {
    if (off % 4096 || n > 4096 || off + 4096 > flash.size()) return false;
    memset(flash.data() + off, 0xff, 4096);
    memcpy(flash.data() + off, data, n);
    return true;
}
