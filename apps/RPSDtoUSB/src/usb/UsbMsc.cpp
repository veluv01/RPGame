// RP2040/RP2350 USB MSC adapter. TinyUSB owns USB/BOT; this adapter owns SD blocks.
// Derived interface/UI: bateske/CHSDtoUSB, GPL-3.0. See ../../LICENSE.
#include <Arduino.h>
#include <tusb-msc.h>
#include <class/msc/msc.h>
#include <USB.h>
#include <string.h>
#include "UsbMsc.h"

#ifdef USE_TINYUSB
#error "RPSDtoUSB uses the default Pico SDK USB stack (native TinyUSB callbacks)."
#endif


namespace usbmsc {
static BlockDevice device;
static bool enabled, ro, ejected, changed;
static volatile bool active;
static State lastOp = CONFIGURED;
static uint32_t lastActivity;
volatile uint32_t blocksRead, blocksWritten;
// The native core dispatches USB packets smaller than an SD sector.
static uint8_t monitorScratch[512] __attribute__((aligned(4)));
uint8_t *buffer() { return monitorScratch; }
static uint8_t readCache[512] __attribute__((aligned(4)));
static uint8_t writeCache[512] __attribute__((aligned(4)));
static uint32_t readLba, writeLba;
static bool readValid, writePending;
static uint16_t nextWriteOffset;
static void (*commandStart)(bool,uint32_t);
static void (*commandEnd)();
static bool commandActive, commandIsWrite;
void observe(void (*start)(bool,uint32_t), void (*end)()) { commandStart=start;commandEnd=end; }
static void finishCommand() {
    if(commandActive && commandEnd)commandEnd();
    commandActive=false;
}
static void startCommand(bool write,uint32_t lba) {
    if(commandActive && write!=commandIsWrite)finishCommand();
    if(!commandActive) {
        commandActive=true;commandIsWrite=write;
        if(commandStart)commandStart(write,lba);
    }
}

static uint8_t descriptor[TUD_MSC_DESC_LEN];
void begin(const BlockDevice &dev) {
    device = dev;
    USB.disconnect();
    uint8_t in = USB.registerEndpointIn();
    uint8_t out = USB.registerEndpointOut();
    const uint8_t bytes[] = { TUD_MSC_DESCRIPTOR(1, 0, out, in, 64) };
    memcpy(descriptor, bytes, sizeof descriptor);
    USB.registerInterface(1, USBClass::simpleInterface, descriptor, sizeof descriptor, 2, 0);
    enabled = true;
    ejected = false;
    USB.connect();
}
void poll() { ScopedAccess access; tud_task(); }
bool busy() { return active; }
State state() {
    if (!enabled) return OFF;
    if (!tud_mounted() || tud_suspended()) return WAITING;
    if (ejected) return EJECTED;
    if (uint32_t(millis() - lastActivity) < 200) return lastOp;
    return CONFIGURED;
}
void setReadOnly(bool value) { ro = value; changed = true; }
bool readOnly() { return ro; }
void mediaChanged() { finishCommand(); changed = true; ejected = false; readValid = writePending = false; }
void detach() { finishCommand(); enabled = false; writePending = false; tud_disconnect(); }
// Main-loop callers hold ScopedAccess; do not acquire the USB mutex twice.
int cdcRead() { return tud_cdc_available() ? tud_cdc_read_char() : -1; }
bool cdcWrite(const char *s, uint8_t n) {
    if (!tud_cdc_connected() || tud_cdc_write_available() < n) return false;
    tud_cdc_write(s, n);
    tud_cdc_write_flush();
    return true;
}

static bool ready(uint8_t lun) {
    if (lun != 0 || !enabled || ejected || !device.blocks || !device.blocks()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0);
        return false;
    }
    if (changed) {
        changed = false;
        tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0);
        return false;
    }
    return true;
}
static bool validRange(uint8_t lun, uint32_t lba, uint32_t count) {
    uint32_t capacity = device.blocks ? device.blocks() : 0;
    if (!count || count > capacity || lba > capacity - count) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return false;
    }
    return true;
}
} // namespace usbmsc

extern "C" {
uint8_t tud_msc_get_maxlun_cb() { return 1; }
void tud_msc_inquiry_cb(uint8_t, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    memcpy(vendor, "RPGame  ", 8);
    memcpy(product, "microSD Reader  ", 16);
    memcpy(revision, "0.1 ", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { return usbmsc::ready(lun); }
void tud_msc_capacity_cb(uint8_t, uint32_t *blocks, uint16_t *size) {
    *blocks = usbmsc::device.blocks ? usbmsc::device.blocks() : 0;
    *size = 512;
}
bool tud_msc_is_writable_cb(uint8_t) { return !usbmsc::readOnly(); }
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t, bool start, bool loadEject) {
    if (lun != 0) return false;
    if (loadEject) { usbmsc::finishCommand(); usbmsc::ejected = !start; usbmsc::writePending = false; }
    return true;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bytes) {
    using namespace usbmsc;
    if (!ready(lun)) return -1;
    uint32_t sectors = uint32_t((uint64_t(offset) + bytes + 511) / 512);
    if (offset >= 512 || !validRange(lun, lba, sectors)) return -1;
    startCommand(false,lba);
    active = true; lastOp = READING; lastActivity = millis();
    uint32_t left = bytes;
    auto *dst = static_cast<uint8_t *>(buffer);
    while (left) {
        if (!readValid || readLba != lba) {
            bool ok = device.readStart(lba);
            if (ok) ok = device.readBlock(readCache);
            bool stopped = device.readStop();
            if (!ok || !stopped) {
                readValid = false; active = false;
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0);
                return -1;
            }
            readLba = lba; readValid = true; ++blocksRead;
        }
        uint32_t n = left < 512 - offset ? left : 512 - offset;
        memcpy(dst, readCache + offset, n);
        dst += n; left -= n; ++lba; offset = 0;
    }
    active = false;
    return bytes;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bytes) {
    using namespace usbmsc;
    if (!ready(lun)) return -1;
    if (ro) { tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0); return -1; }
    uint32_t sectors = uint32_t((uint64_t(offset) + bytes + 511) / 512);
    if (offset >= 512 || !validRange(lun, lba, sectors)) return -1;
    startCommand(true,lba);
    active = true; lastOp = WRITING; lastActivity = millis();
    uint32_t left = bytes;
    while (left) {
        if (offset == 0) {
            // Start/restart a whole sector. An interrupted partial write is
            // discarded, so a USB reset cannot mix data from two commands.
            writeLba = lba; nextWriteOffset = 0; writePending = true;
        }
        if (!writePending || writeLba != lba || nextWriteOffset != offset) {
            active = false; writePending = false;
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0);
            return -1;
        }
        uint32_t n = left < 512 - offset ? left : 512 - offset;
        memcpy(writeCache + offset, buffer, n);
        nextWriteOffset += n;
        buffer += n; left -= n;
        if (nextWriteOffset == 512) {
            bool ok = device.writeStart(lba, 1);
            if (ok) ok = device.writeBlock(writeCache);
            bool stopped = device.writeStop();
            writePending = false; readValid = false;
            if (!ok || !stopped) {
                active = false;
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0);
                return -1;
            }
            ++blocksWritten;
        }
        ++lba; offset = 0;
    }
    active = false;
    return bytes;
}
void tud_msc_read10_complete_cb(uint8_t) { usbmsc::readValid = false; usbmsc::finishCommand(); }
void tud_msc_write10_complete_cb(uint8_t) { usbmsc::writePending = false; usbmsc::finishCommand(); }
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t bytes) {
    if (!usbmsc::ready(lun)) return -1;
    switch (command[0]) {
    case 0x35: // SYNCHRONIZE CACHE(10): writeStop already waits for programming.
        return 0;
    case 0x2F: { // VERIFY(10), BYTCHK=0 (no comparison data)
        if (command[1] & 0x06) break;
        uint32_t lba = (uint32_t(command[2]) << 24) | (uint32_t(command[3]) << 16) |
                       (uint32_t(command[4]) << 8) | command[5];
        uint32_t count = (uint32_t(command[7]) << 8) | command[8];
        return !count || usbmsc::validRange(lun, lba, count) ? 0 : -1;
    }
    case 0x5A: { // MODE SENSE(10), header only, write-protect bit.
        const uint8_t response[8] = {0, 6, 0, uint8_t(usbmsc::readOnly() ? 0x80 : 0), 0, 0, 0, 0};
        uint16_t n = bytes < sizeof response ? bytes : sizeof response;
        memcpy(buffer, response, n);
        return n;
    }
    default: break;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}
} // extern C
