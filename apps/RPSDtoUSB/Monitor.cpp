// What the PC is doing to the card. See Monitor.h.
#include <Arduino.h>
#include <string.h>
#include "Monitor.h"

namespace mon {

Volume vol;
bool remountWanted = false;
Column hist[HISTORY];
uint32_t columns = 0;
Stats st;
Event ev[EVENTS];
uint32_t events = 0;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- Layout -------------------------------------------------------------------
uint64_t clusterBytes() { return (uint64_t)vol.spc * 512; }
uint64_t volumeBytes() { return (uint64_t)vol.clusters * clusterBytes(); }
uint64_t freeBytes() { return vol.freeClusters < 0 ? 0 : (uint64_t)vol.freeClusters * clusterBytes(); }
uint64_t usedBytes() { return vol.freeClusters < 0 ? 0 : volumeBytes() - freeBytes(); }
uint32_t usedPermille() {
    if (vol.freeClusters < 0 || !vol.clusters) return 0;
    uint32_t used = vol.clusters - (uint32_t)vol.freeClusters, all = vol.clusters;
    while (used > 0xFFFFFFFFu / 1000) { used >>= 1; all >>= 1; }
    return used * 1000 / all;
}

static uint32_t toClusters(uint32_t bytes) {
    uint32_t cb = vol.spc * 512u;
    return cb ? (bytes + cb - 1) / cb : 0;
}
static void freeAdjust(int32_t clusters) {
    if (vol.freeClusters < 0) return;
    int32_t f = vol.freeClusters + clusters;
    vol.freeClusters = f < 0 ? 0 : (f > (int32_t)vol.clusters ? (int32_t)vol.clusters : f);
}

static void copyLabel(Volume &vol, const uint8_t *p, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) vol.label[k++] = (char)(p[i] < 0x20 || p[i] > 0x7E ? '?' : p[i]);
    while (k && vol.label[k - 1] == ' ') k--;
    vol.label[k] = 0;
    if (!strcmp(vol.label, "NO NAME")) vol.label[0] = 0;
}

// A boot sector this code understands: FAT12/16/32 BPB or exFAT.
static bool bootSector(const uint8_t *b) {
    if (b[510] != 0x55 || b[511] != 0xAA) return false;
    if (!memcmp(b + 3, "EXFAT   ", 8)) return true;
    uint8_t spc = b[13];
    return (b[0] == 0xEB || b[0] == 0xE9) && le16(b + 11) == 512 && spc && !(spc & (spc - 1)) &&
           le16(b + 14) && (b[16] == 1 || b[16] == 2);
}

static void parseBoot(const uint8_t *b, uint32_t base, Volume &vol) {
    vol.partStart = base;
    if (!memcmp(b + 3, "EXFAT   ", 8)) {
        vol.fs = FS_EXFAT;
        vol.spc = (uint8_t)(1u << (b[109] & 7));
        vol.fatStart = base + le32(b + 80);
        vol.fatEnd = vol.fatStart + le32(b + 84) * (b[110] ? b[110] : 1);
        vol.rootEnd = vol.fatEnd;
        vol.dataStart = base + le32(b + 88);
        vol.clusters = le32(b + 92);
        vol.serial = le32(b + 100);
        if (b[112] <= 100) vol.freeClusters = (int32_t)(vol.clusters / 100 * (100 - b[112]));
        return;
    }
    uint32_t rsv = le16(b + 14), nf = b[16], rootEnt = le16(b + 17);
    uint32_t tot = le16(b + 19) ? le16(b + 19) : le32(b + 32);
    uint32_t fatSz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
    uint32_t rootSecs = (rootEnt * 32 + 511) / 512;
    uint32_t meta = rsv + nf * fatSz + rootSecs;
    vol.spc = b[13];
    vol.fatStart = base + rsv;
    vol.fatEnd = vol.fatStart + nf * fatSz;
    vol.rootEnd = vol.fatEnd + rootSecs;
    vol.dataStart = vol.rootEnd;
    vol.clusters = tot > meta ? (tot - meta) / vol.spc : 0;
    vol.fs = vol.clusters < 4085 ? FS_FAT12 : vol.clusters < 65525 ? FS_FAT16 : FS_FAT32;
    if (vol.fs == FS_FAT32) {
        copyLabel(vol, b + 71, 11);
        vol.serial = le32(b + 67);
        if (le16(b + 48) && le16(b + 48) < rsv) vol.fsinfo = base + le16(b + 48);
    } else {
        copyLabel(vol, b + 43, 11);
        vol.serial = le32(b + 39);
    }
}

static void parseFsinfo(const uint8_t *b) {
    if (le32(b) != 0x41615252 || le32(b + 484) != 0x61417272) return;
    uint32_t f = le32(b + 488);
    vol.freeClusters = f <= vol.clusters ? (int32_t)f : -1;
}

static void dirForget();

void mount(uint32_t blocks, ReadFn rd, uint8_t *b) {
    memset(&vol, 0, sizeof vol);
    vol.freeClusters = -1;
    vol.blocks = blocks;
    remountWanted = false;
    dirForget();
    if (!blocks) return;
    vol.fs = FS_RAW;
    vol.partEnd = blocks;
    if (!rd(0, b)) return;
    uint32_t base = 0, end = blocks;
    if (!bootSector(b)) {                                   // MBR: the first FAT/exFAT partition
        if (b[510] != 0x55 || b[511] != 0xAA) return;
        for (int i = 0; i < 4 && !base; i++) {
            const uint8_t *e = b + 446 + 16 * i;
            uint8_t t = e[4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x07 || t == 0x0B || t == 0x0C || t == 0x0E) {
                base = le32(e + 8);
                end = base + le32(e + 12);
            }
        }
        if (!base || base >= blocks || !rd(base, b) || !bootSector(b)) return;
    }
    vol.partEnd = end;
    parseBoot(b, base, vol);
    if (vol.fsinfo && rd(vol.fsinfo, b)) parseFsinfo(b);
    if (vol.fs == FS_FAT16 && vol.fatEnd > vol.fatStart) {
        // FAT16 keeps no free count: the first FAT is at most 256 blocks, count it.
        uint32_t used = 0, n = vol.clusters + 2, fatBlocks = (vol.fatEnd - vol.fatStart) / 2;
        for (uint32_t s = 0; s < fatBlocks && s * 256 < n; s++) {
            if (!rd(vol.fatStart + s, b)) return;
            for (uint32_t i = s ? 0 : 2; i < 256 && s * 256 + i < n; i++)
                if (le16(b + 2 * i)) used++;
        }
        vol.freeClusters = (int32_t)(vol.clusters - used);
    }
}

static bool fatFs() { return vol.fs >= FS_FAT12 && vol.fs <= FS_FAT32; }

static uint8_t regionOf(uint32_t lba) {
    if (vol.fs < FS_FAT12) return lba ? RG_DATA : RG_SYS;
    if (lba < vol.fatStart) return RG_SYS;                // MBR, boot sector, FSInfo, reserved
    if (lba < vol.fatEnd) return RG_FAT;
    if (lba < vol.rootEnd) return RG_DIR;                 // FAT12/16 root directory
    return RG_DATA;
}

// ---- Events -------------------------------------------------------------------
static uint32_t lastData = 0;                             // millis() of the last file-data write
static Event *liveEv = nullptr;                           // the file being written, if any

static void endLive() {
    if (liveEv) liveEv->live = 0;
    liveEv = nullptr;
}

void tick() {
    if (liveEv && millis() - lastData > 2000) endLive();
}

const Event *newest(int back) {
    if (back < 0 || (uint32_t)back >= events || back >= EVENTS) return nullptr;
    return &ev[(events - 1 - back) % EVENTS];
}

static Event *push(uint8_t type, const char *name, uint32_t value, uint16_t hash) {
    Event &e = ev[events % EVENTS];
    if (liveEv == &e || (type != EV_MILE && type != EV_RETRY)) endLive();   // a new file ends the last one
    e.type = type;
    e.live = 0;
    e.nameHash = hash;
    e.value = value;
    e.done = 0;
    e.col = columns;
    e.t = millis();
    strncpy(e.name, name ? name : "", sizeof e.name - 1);
    e.name[sizeof e.name - 1] = 0;
    events++;
    return &e;
}

void event(uint8_t type, const char *name, uint32_t value) { push(type, name, value, 0); }

// ---- The command in progress -----------------------------------------------------
static struct {
    bool write;
    uint8_t flags, mark;
    uint32_t lba, blocks, t0, retries;
} cur;

// Which event a graph column shows when its command caused several.
static const uint8_t MARK_RANK[] = {0, 4, 4, 5, 5, 3, 3, 1, 0, 0, 0, 0, 0, 0, 6, 6, 2, 7, 0};
static void mark(uint8_t type) {
    if (MARK_RANK[type] > MARK_RANK[cur.mark]) cur.mark = type;
}

// ---- Directories -----------------------------------------------------------------
// A slot's signature: 0 for nothing (free, deleted, long-name part, label,
// "." or ".."), else a hash of its name (bits 0-15, never 0), a hash of its
// cluster, size and modification time (16-23) and its first character
// (24-31: a deleted entry loses it, so this restores it).
static uint32_t slotSig(const uint8_t *e) {
    uint8_t c = e[0], a = e[11];
    if (!c || c == 0xE5 || c == '.' || a == 0x0F || (a & 0x08)) return 0;
    const uint32_t *w = (const uint32_t *)__builtin_assume_aligned(e, 4);
    uint32_t h = ((w[0] * 0x9E3779B1u ^ w[1]) * 0x85EBCA77u ^ w[2]) * 0xC2B2AE3Du;   // name + attributes
    uint32_t m = ((w[5] * 0x9E3779B1u ^ w[6]) * 0x85EBCA77u ^ w[7]) * 0xC2B2AE3Du;   // cluster, time, size
    uint16_t nh = (uint16_t)(h >> 16);
    return (nh ? nh : 1) | (m >> 24 << 16) | ((uint32_t)c << 24);
}

// Could this block be a directory? Every entry must be well formed: free
// (and then so is the rest), a long-name part with its fixed zero fields,
// or a short entry with legal attribute bits. File data fails on the first
// entry almost always.
static bool dirShape(const uint8_t *b) {
    bool any = false;
    for (int i = 0; i < 512; i += 32) {
        const uint8_t *e = b + i;
        if (!e[0]) {                                      // end of directory: the rest unused too
            for (int j = i + 32; j < 512; j += 32)
                if (b[j]) return false;
            return any;
        }
        uint8_t a = e[11];
        if (a == 0x0F ? (e[12] || e[26] || e[27]) : ((a & 0xC0) || (e[12] & ~0x18))) return false;
        any = true;
    }
    return any;
}

// ... and a block not seen before must also have legal short names.
static bool dirNames(const uint8_t *b) {
    // Not in a short name: controls, lower case, DEL and " * + , / : ; < = > ? [ \ ] |
    // (a bit per character from 0x20 to 0x7F). '.' only in "." and "..".
    static const uint32_t BAD[3] = {0xFC009C04, 0x38000000, 0x97FFFFFE};
    for (int i = 0; i < 512 && b[i]; i += 32) {
        const uint8_t *e = b + i;
        if (e[11] == 0x0F) continue;
        bool dot = e[0] == '.';
        for (int k = 0; k < 11; k++) {
            uint8_t c = e[k];
            if (!k && (c == 0xE5 || c == 0x05)) continue;
            if (c < 0x20 || (c < 0x80 && (BAD[(c >> 5) - 1] >> (c & 31) & 1))) return false;
            if (c == '.' && !dot) return false;
        }
    }
    return true;
}

// The name of the file in slot i: its long name if the parts are in this
// block, else the 8.3 name. first: its first character (lost if deleted).
static void nameOf(const uint8_t *d, int i, uint8_t first, char *out, int max) {
    static const uint8_t OFF[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int n = 0;
    for (int j = i - 1; j >= 0 && n < max - 1; j--) {
        const uint8_t *e = d + 32 * j;
        if (e[11] != 0x0F) break;
        for (int k = 0; k < 13 && n < max - 1; k++) {
            uint16_t c = le16(e + OFF[k]);
            if (!c || c == 0xFFFF) { j = 0; break; }
            out[n++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    if (!n) {
        const uint8_t *e = d + 32 * i;
        uint8_t nt = e[12];
        for (int k = 0; k < 11 && n < max - 2; k++) {
            uint8_t c = k ? e[k] : first;
            if (c == ' ') { if (k < 8) k = 7; continue; }
            if (k == 8) out[n++] = '.';
            if (c >= 'A' && c <= 'Z' && (nt & (k < 8 ? 0x08 : 0x10))) c += 32;
            out[n++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    out[n] = 0;
}

// The last few directory blocks seen, as slot signatures. Least recently
// used goes first.
const int DIRCACHE = 12;
static struct DirBlock { uint32_t lba, used; uint32_t sig[16]; } dc[DIRCACHE];
static uint32_t dcClock = 0;
static const uint32_t NONE = 0xFFFFFFFF;

static void dirForget() {
    for (int i = 0; i < DIRCACHE; i++) dc[i].lba = NONE;
}
static DirBlock *dirFind(uint32_t lba) {
    for (int i = 0; i < DIRCACHE; i++)
        if (dc[i].lba == lba) return &dc[i];
    return nullptr;
}
static void dirStore(DirBlock *c, uint32_t lba, const uint32_t *sig) {
    if (!c) {
        c = &dc[0];
        for (int i = 1; i < DIRCACHE; i++)
            if (dc[i].used < c->used) c = &dc[i];
    }
    c->lba = lba;
    c->used = ++dcClock;
    memcpy(c->sig, sig, sizeof c->sig);
}

// A file that left one place and turned up in another within a moment is
// the same file: renamed or moved, not deleted and created.
static struct Pending { uint32_t idx, t, size, where; uint8_t meta; uint16_t hash; } lastGone, lastNew;
static uint32_t curLba;                                   // the directory block being compared
static uint32_t whereOf(int slot) { return curLba << 4 | (uint32_t)slot; }

static Event *recent(uint32_t idx) {
    return idx && events - idx < EVENTS ? &ev[(idx - 1) % EVENTS] : nullptr;
}
// (`where` tells a file deleted right after it was created, which is in
// the same slot, from the same file appearing elsewhere.)
static bool pairs(const Pending &p, uint8_t meta, uint32_t size, uint32_t where) {
    return p.idx && p.meta == meta && p.size == size && size && p.where != where && millis() - p.t < 3000;
}
static void remember(Pending &p, uint8_t meta, uint32_t size, uint16_t hash, uint32_t where) {
    p.idx = events;
    p.where = where;
    p.t = millis();
    p.meta = meta;
    p.size = size;
    p.hash = hash;
}

static void gone(const uint8_t *d, int i, uint32_t old) {
    const uint8_t *e = d + 32 * i;
    bool dir = e[11] & 0x10;
    uint32_t size = le32(e + 28);
    uint8_t meta = (uint8_t)(old >> 16);
    Event *n = pairs(lastNew, meta, size, whereOf(i)) ? recent(lastNew.idx) : nullptr;
    if (n && (n->type == EV_NEW || n->type == EV_NEWDIR)) {   // created first, then the old one went
        bool moved = lastNew.hash == (uint16_t)old;
        if (n->type == EV_NEW) st.created--; else st.dirsMade--;
        n->type = moved ? EV_MOVE : EV_REN;
        st.renamed++;
        freeAdjust(dir ? 0 : (int32_t)toClusters(size));      // the creation had charged it
        if (dir) freeAdjust(1);
        lastNew.idx = 0;
        mark(n->type);
        return;
    }
    char name[24];
    nameOf(d, i, (uint8_t)(old >> 24), name, sizeof name);
    if (e[0] != 0xE5) strcpy(name, "?");                     // the slot was wiped
    push(dir ? EV_DELDIR : EV_DEL, name, size, (uint16_t)old);
    if (dir) { st.dirsGone++; freeAdjust(1); }
    else { st.deleted++; freeAdjust((int32_t)toClusters(size)); }
    remember(lastGone, meta, size, (uint16_t)old, whereOf(i));
    mark(dir ? EV_DELDIR : EV_DEL);
}

static void created(const uint8_t *d, int i, uint32_t sig) {
    const uint8_t *e = d + 32 * i;
    bool dir = e[11] & 0x10;
    uint32_t size = le32(e + 28);
    uint8_t meta = (uint8_t)(sig >> 16);
    char name[24];
    nameOf(d, i, e[0], name, sizeof name);
    Event *g = pairs(lastGone, meta, size, whereOf(i)) ? recent(lastGone.idx) : nullptr;
    if (g && (g->type == EV_DEL || g->type == EV_DELDIR)) {   // the old one went first
        bool moved = lastGone.hash == (uint16_t)sig;
        if (g->type == EV_DEL) { st.deleted--; freeAdjust(-(int32_t)toClusters(size)); }
        else { st.dirsGone--; freeAdjust(-1); }
        g->type = moved ? EV_MOVE : EV_REN;
        g->nameHash = (uint16_t)sig;
        g->t = millis();
        strcpy(g->name, name);
        st.renamed++;
        lastGone.idx = 0;
        mark(g->type);
        return;
    }
    Event *n = push(dir ? EV_NEWDIR : EV_NEW, name, size, (uint16_t)sig);
    if (dir) { st.dirsMade++; freeAdjust(-1); }
    else { st.created++; freeAdjust(-(int32_t)toClusters(size)); n->live = 1; liveEv = n; lastData = millis(); }
    remember(lastNew, meta, size, (uint16_t)sig, whereOf(i));
    mark(dir ? EV_NEWDIR : EV_NEW);
}

static void changed(const uint8_t *d, int i, uint32_t sig) {
    const uint8_t *e = d + 32 * i;
    if (e[11] & 0x10) return;                                // a directory's times: not news
    uint32_t size = le32(e + 28);
    uint16_t h = (uint16_t)sig;
    for (int k = 0; k < EVENTS && (uint32_t)k < events; k++) {
        Event &x = ev[(events - 1 - k) % EVENTS];
        if (x.nameHash != h || (x.type != EV_NEW && x.type != EV_MOD)) continue;
        if (x.value != size) {
            freeAdjust((int32_t)toClusters(x.value) - (int32_t)toClusters(size));
            x.value = size;
            x.t = millis();
        }
        if (x.type == EV_NEW && lastNew.idx && recent(lastNew.idx) == &x) {   // its final size and cluster
            lastNew.size = size;
            lastNew.meta = (uint8_t)(sig >> 16);
            lastNew.where = whereOf(i);
        }
        return;
    }
    char name[24];
    nameOf(d, i, e[0], name, sizeof name);
    push(EV_MOD, name, size, h);
    st.modified++;
    mark(EV_MOD);
}

static void dirBlock(uint32_t lba, const uint8_t *d, bool write) {
    uint32_t sig[16];
    for (int i = 0; i < 16; i++) sig[i] = slotSig(d + 32 * i);
    DirBlock *c = dirFind(lba);
    curLba = lba;
    if (write) {
        // Compare with the last copy seen; a block never seen is news only if
        // it continues one that was (a directory growing into a fresh block).
        static const uint32_t EMPTY[16] = {0};
        const uint32_t *old = c ? c->sig : (dirFind(lba - 1) ? EMPTY : nullptr);
        if (old) {
            for (int i = 0; i < 16; i++)
                if (old[i] && (old[i] & 0xFFFF) != (sig[i] & 0xFFFF)) {
                    // Same place, same cluster and size, new name: renamed in place.
                    if (sig[i] && ((old[i] ^ sig[i]) & 0xFF0000) == 0) continue;
                    gone(d, i, old[i]);
                }
            for (int i = 0; i < 16; i++) {
                if (!sig[i] || (old[i] & 0xFFFF) == (sig[i] & 0xFFFF)) continue;
                if (old[i] && ((old[i] ^ sig[i]) & 0xFF0000) == 0) {
                    char name[24];
                    nameOf(d, i, d[32 * i], name, sizeof name);
                    push(EV_REN, name, le32(d + 32 * i + 28), (uint16_t)sig[i]);
                    st.renamed++;
                    mark(EV_REN);
                    lastNew.idx = lastGone.idx = 0;
                    continue;
                }
                created(d, i, sig[i]);
            }
            for (int i = 0; i < 16; i++)
                if (sig[i] && old[i] != sig[i] && (old[i] & 0xFFFF) == (sig[i] & 0xFFFF)) changed(d, i, sig[i]);
        }
    }
    if (c || d[0]) dirStore(c, lba, sig);                 // (a block whose first slot is free is all free)
}

// ---- Commands ----------------------------------------------------------------------
void cmdStart(bool write, uint32_t lba) {
    cur.write = write;
    cur.flags = write ? C_WRITE : 0;
    cur.mark = 0;
    cur.lba = lba;
    cur.blocks = 0;
    cur.retries = 0;
    cur.t0 = micros();
}

void cmdBlock(uint32_t lba, const uint8_t *d) {
    cur.blocks++;
    uint8_t rg = regionOf(lba);
    if (rg == RG_DATA && fatFs() && dirShape(d) && (dirFind(lba) || dirNames(d))) rg = RG_DIR;
    cur.flags |= rg;
    if (rg == RG_DIR) {
        if (fatFs()) dirBlock(lba, d, cur.write);
        return;
    }
    if (!cur.write) return;
    if (rg == RG_DATA) {
        if (liveEv) {
            liveEv->done += 512;
            lastData = millis();
            if (liveEv->value && liveEv->done >= liveEv->value) endLive();
        }
        return;
    }
    if (rg != RG_SYS) return;
    if (lba == vol.fsinfo && vol.fsinfo) { parseFsinfo(d); return; }
    // A new partition table or a new file system (format). Rewritten
    // with the same layout is not news: Linux, for one, flags the boot
    // sector dirty on mount and clean on unmount.
    uint8_t type = 0;
    if (lba == 0 && vol.partStart && !bootSector(d)) {
        const uint8_t *e = d + 446;
        if (d[510] != 0x55 || d[511] != 0xAA || le32(e + 8) != vol.partStart || le32(e + 8) + le32(e + 12) != vol.partEnd)
            type = EV_PART;
    } else if (lba == vol.partStart) {
        Volume v = {};
        if (bootSector(d)) parseBoot(d, lba, v);
        if (v.fs != vol.fs || v.serial != vol.serial || v.fatStart != vol.fatStart || v.dataStart != vol.dataStart ||
            v.clusters != vol.clusters)
            type = EV_FORMAT;
    }
    if (!type) return;
    if (!remountWanted) { push(type, "", 0, 0); mark(type); }
    remountWanted = true;
}

void cmdRetry() {
    cur.retries++;
    cur.flags |= C_RETRY;
    st.retries++;
}

void cmdFail() {
    cur.flags |= C_FAIL;
    st.fails++;
}

static const uint16_t MILESTONES[] = {100, 250, 500, 1000, 2000, 5000, 10000, 20000, 50000, 0};
static uint32_t totalR, totalW;                           // blocks, for the milestones

void cmdEnd() {
    uint32_t dt = micros() - cur.t0;
    // KB/s = blocks / 2 / (dt / 1e6), in 32-bit: dt in 32 us units (a READ (10) moves at most 65535 blocks).
    uint32_t units = dt >> 5;
    uint32_t kbs = cur.blocks * 15625u / (units ? units : 1);
    if (kbs > 0xFFFF) kbs = 0xFFFF;
    st.busyUs += dt;
    uint32_t &total = cur.write ? totalW : totalR;
    uint32_t before = total >> 11;                         // MB
    total += cur.blocks;
    for (int i = 0; MILESTONES[i]; i++)
        if (before < MILESTONES[i] && (total >> 11) >= MILESTONES[i]) {
            push(EV_MILE, cur.write ? "WRITTEN" : "READ", MILESTONES[i], 0);
            break;
        }
    if (cur.write) { st.cmdsW++; if (cur.blocks >= 32 && kbs > st.peakW) st.peakW = kbs; }
    else { st.cmdsR++; if (cur.blocks >= 32 && kbs > st.peakR) st.peakR = kbs; }
    if (cur.flags & C_FAIL) { push(EV_FAIL, cur.write ? "WRITE" : "READ", cur.lba + cur.blocks, 0); mark(EV_FAIL); }
    else if (cur.retries) { push(EV_RETRY, cur.write ? "WRITE" : "READ", cur.retries, 0); mark(EV_RETRY); }
    st.lastMs = millis();
    st.lastWrite = cur.write;
    Column &c = hist[columns % HISTORY];
    c.kbs = (uint16_t)kbs;
    c.flags = cur.flags;
    c.mark = cur.mark;
    columns++;
}

}  // namespace mon
