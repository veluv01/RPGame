// What the PC is doing to the card, worked out from the blocks going past.
//
// The reader only ever sees "read n blocks at LBA x" and "write these
// blocks". Knowing the card's layout (read from its own boot sectors when
// it goes in) turns that into: the PC is reading the FAT, writing a
// directory, streaming file data. And a directory block carries the
// directory entries themselves, so comparing a written directory block
// with the last copy seen of it says which file was created, deleted,
// renamed or grew, by name.
//
// Everything here runs inside the transfer, between two blocks, so the
// per-block work is a few compares unless the block is a directory.
#pragma once
#include <stdint.h>

namespace mon {

// ---- The card's layout ------------------------------------------------------
enum Fs : uint8_t { FS_NONE, FS_RAW, FS_FAT12, FS_FAT16, FS_FAT32, FS_EXFAT };
enum Region : uint8_t { RG_DATA = 1, RG_DIR = 2, RG_FAT = 4, RG_SYS = 8 };

struct Volume {
    uint8_t fs;
    uint8_t spc;                       // sectors per cluster
    uint32_t blocks;                   // the whole card
    uint32_t partStart, partEnd;
    uint32_t fatStart, fatEnd;         // every FAT copy
    uint32_t rootEnd;                  // FAT12/16: the fixed root directory ends here (= fatEnd on FAT32)
    uint32_t dataStart;
    uint32_t clusters;
    uint32_t fsinfo;                   // FAT32 FSInfo block (0: none)
    int32_t freeClusters;              // -1: not known
    uint32_t serial;                   // the volume's serial number
    char label[12];
};
extern Volume vol;

typedef bool (*ReadFn)(uint32_t lba, uint8_t *dst);
// A card went in (blocks > 0) or out (0). Reads its MBR, boot sector and
// FSInfo through rd into buf (512 B). Forgets everything about the last card.
void mount(uint32_t blocks, ReadFn rd, uint8_t *buf);
// The PC rewrote the partition table or a boot sector: mount() again when
// the card is free.
extern bool remountWanted;

uint64_t clusterBytes();
uint64_t freeBytes();                  // 0 when not known
uint64_t usedBytes();
uint64_t volumeBytes();
uint32_t usedPermille();               // 0..1000; 0 when not known

// ---- Per-command hooks, from the block device ------------------------------
void cmdStart(bool write, uint32_t lba);
void cmdBlock(uint32_t lba, const uint8_t *data);   // a block moved, CRC-good (data 4-byte aligned)
void cmdRetry();
void cmdFail();
void cmdEnd();

// ---- History: one column per READ or WRITE command -------------------------
enum : uint8_t { C_WRITE = 0x80, C_RETRY = 0x40, C_FAIL = 0x20 };   // | Region bits
struct Column {
    uint16_t kbs;                      // KB/s while the command ran
    uint8_t flags;
    uint8_t mark;                      // the most telling event it caused (EvType), 0 none
};
const int HISTORY = 128;
extern Column hist[HISTORY];
extern uint32_t columns;               // commands so far; the newest is hist[(columns - 1) % HISTORY]

struct Stats {
    uint32_t cmdsR, cmdsW;
    uint32_t peakR, peakW;             // KB/s, commands of 32 blocks or more
    uint32_t busyUs;                   // time in commands, total
    uint16_t created, deleted, modified, renamed, dirsMade, dirsGone;
    uint32_t retries, fails;
    uint32_t lastMs;                   // millis() when the last command ended
    bool lastWrite;                    // ... and which way it went
};
extern Stats st;

// ---- Events -----------------------------------------------------------------
enum EvType : uint8_t {
    EV_NONE, EV_NEW, EV_NEWDIR, EV_DEL, EV_DELDIR, EV_REN, EV_MOVE, EV_MOD,
    EV_CARDIN, EV_CARDOUT, EV_PC, EV_EJECT, EV_RO, EV_RW, EV_FORMAT, EV_PART,
    EV_RETRY, EV_FAIL, EV_MILE,
};
struct Event {
    uint8_t type;
    uint8_t live;                      // EV_NEW still being written: file data goes to `done`
    uint16_t nameHash;
    uint32_t value;                    // bytes, MB or blocks, by type
    uint32_t done;                     // EV_NEW: file data written since it was created
    uint32_t col;                      // `columns` when it happened (its command's column + 1)
    uint32_t t;                        // millis() when it happened or last changed
    char name[24];
};
const int EVENTS = 8;
extern Event ev[EVENTS];
extern uint32_t events;                // so far; the newest is ev[(events - 1) % EVENTS]
void event(uint8_t type, const char *name, uint32_t value);   // from the sketch (card, USB, buttons)
const Event *newest(int back);         // 0 = newest; nullptr past the end
void tick();                           // from loop(): a file not written to for 2 s is done

}  // namespace mon
