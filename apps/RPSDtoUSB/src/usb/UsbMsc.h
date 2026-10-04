// RPGame USB mass storage using Arduino-Pico's default Pico SDK stack.
// TinyUSB owns enumeration and BOT/SCSI dispatch. This module adds MSC
// beside the core's CDC interface, retaining its 1200-baud bootloader reset.
// Two sector caches translate USB packet callbacks into whole SD sectors.
// Main-loop peripheral operations must hold ScopedAccess: USB tasks also
// run in the core's IRQ and must not interrupt an SD/LCD bus transaction.
#pragma once
#include <stdint.h>
#include <USB.h>

// Arduino-Pico's TinyUSB tasks also run in an IRQ. Main-loop SD/LCD
// operations hold this lock so callbacks cannot interrupt an SPI transaction.


namespace usbmsc {

struct ScopedAccess {
    ScopedAccess() { mutex_enter_blocking(&USB.mutex); }
    ~ScopedAccess() { mutex_exit(&USB.mutex); }
    ScopedAccess(const ScopedAccess &) = delete;
    ScopedAccess &operator=(const ScopedAccess &) = delete;
};

// The storage behind the drive, in 512-byte blocks. Each whole sector
// has its own start/block/stop transaction; a card stream is never kept
// open between USB callbacks. Stop is called even after start/block fails.
struct BlockDevice {
    uint32_t (*blocks)();                                  // 0 = no medium
    bool (*readStart)(uint32_t lba);
    bool (*readBlock)(uint8_t *dst);                       // next block of the run
    bool (*readStop)();
    bool (*writeStart)(uint32_t lba, uint32_t count);
    bool (*writeBlock)(const uint8_t *src);                // next block of the run
    bool (*writeStop)();                                   // false: the run did not complete
};

// WAITING covers "not enumerated yet" and "bus suspended" (PC asleep, or the
// cable pulled while running on battery); EJECTED is CONFIGURED after the
// host ejected the medium.
enum State : uint8_t { OFF, WAITING, CONFIGURED, READING, WRITING, EJECTED };

void begin(const BlockDevice &dev);   // register MSC and reconnect USB
void observe(void (*start)(bool write, uint32_t lba), void (*end)());
// Command boundaries are SCSI READ/WRITE(10), while card transactions stay
// sector-sized. Observers run under the same USB mutex as block callbacks.
uint8_t *buffer();                    // scratch for monitor reads, under ScopedAccess
void poll();                          // service it; call as often as possible
State state();
bool busy();                          // an SD callback is in progress
void setReadOnly(bool ro);
bool readOnly();
void mediaChanged();                  // new medium (or none): un-eject, tell the host
void detach();                        // drop off the bus

// The serial function carries no data stream, just a way to ask for status:
int cdcRead();                                // last byte the host sent, or -1
bool cdcWrite(const char *s, uint8_t n);      // up to 144 bytes; false if busy

extern volatile uint32_t blocksRead, blocksWritten;

}  // namespace usbmsc
