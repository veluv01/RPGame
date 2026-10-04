// The screen. Green-on-black like a spy's wristwatch: every element is a
// reading, nothing is decoration (the "secret agent" style; its pieces are
// Agent.h, shared with CHStlView). See Ui.h.
//
// A frame is drawn only when something changed or a short animation runs,
// and only between SCSI commands (the panel shares SPI1 with the card).
// The top part (status, gauge, graph, totals) is redrawn whole; the panel
// below only when what it shows changed, and only then flushed. The graph
// is written straight into the framebuffer, two columns a byte; text is
// RPGfx's 5x7 and the library's 3x5, the speed seven-segment digits.
//
// No particles: an event gets a beep (soft for files, louder for the card
// and the PC), and a big one (a format, new partitions, read-only, a size
// milestone, a block given up on) a Sizzle banner over the graph.
#include <RPGame.h>
#include <string.h>
#include "Ui.h"
#include "Agent.h"
#include "Fx.h"
#include "Sounds.h"
#include "Monitor.h"
#include "src/usb/UsbMsc.h"

namespace ui {

// ---- Palette ------------------------------------------------------------------
// The secret agent chrome in its house slots (Agent.h), then the panel's own
// colours. The values are the instrument panel's (RGB444).
using agent::tiny;
using agent::tinyR;
using agent::tinyW;
using agent::centred;
enum : uint8_t {
    BG = agent::BG, PALE = agent::PALE, GRID = agent::GRID, MID = agent::MID, GREEN = agent::LIVE,
    DIM = agent::DIM, RED = agent::ALERT, PANEL = agent::PANEL,
    GOLD, AMBER, RFILL, WHITE, WFILL, CYAN, AMID, GHOST,
};
static const uint16_t PALETTE[16] = {
    0x010, AGENT_CHROME,
    0xFD4,      // GOLD
    0xF91,      // AMBER: writes
    0x052,      // RFILL: under the read line
    0xFFF,      // WHITE
    0x630,      // WFILL: under the write line
    0x4CF,      // CYAN
    0xA50,      // AMID
    0x031,      // GHOST: the speed's dark segments
};

// ---- Layout -------------------------------------------------------------------
const int GAUGE_Y = 12;                        // the card's fill, 2 px
const int GY = 16, GH = 54;                    // graph rows GY .. GY + GH - 1
const int GB = GY + GH - 1;                    // its bottom row
const int STRIP_Y = GB + 2;                    // what each command touched, 2 px
const int SUM_Y = STRIP_Y + 4;                 // totals line (Tiny)
const int TAB_Y = SUM_Y + 8;                   // page tabs (Tiny)
const int PAGE_Y = TAB_Y + 8;                  // page body
const int LINES = 4, LINE_H = 9;
const uint32_t FULL_KBS = 512;                 // graph top: about what full-speed USB allows

enum Page : uint8_t { P_LOG, P_STATS, P_CARD, PAGES };
static uint8_t page = P_LOG, scroll = 0;

// ---- Card identity --------------------------------------------------------------
static uint8_t cid[16], cardType = 0;
static bool haveCid = false;

void setCard(const uint8_t *c, uint8_t type) {
    haveCid = c != nullptr;
    if (c) memcpy(cid, c, 16);
    cardType = type;
}

// ---- Text -----------------------------------------------------------------------
static inline char *put(char *p, const char *s) { return fmtStr(p, s); }
static char *num(char *p, uint32_t v) {
    char t[10];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
// Bytes as 3 significant digits: 512B 12.3K 357M 1.23G 15.9G.
static char *bytes(char *p, uint64_t b) {
    static const char UNIT[] = "BKMGT";
    int u = 0;
    uint64_t x100 = b * 100;
    while (x100 >= 100 * 1000ull && u < 4) { x100 >>= 10; u++; }   // /1024 at each step
    uint32_t v = (uint32_t)x100;                                  // value * 100
    if (!u) p = num(p, (uint32_t)b);
    else if (v >= 10000) p = num(p, (v + 50) / 100);
    else if (v >= 1000) { p = num(p, v / 100); *p++ = '.'; p = num(p, (v / 10) % 10); }
    else { p = num(p, v / 100); *p++ = '.'; *p++ = (char)('0' + (v / 10) % 10); p = num(p, v % 10); }
    *p++ = UNIT[u];
    *p = 0;
    return p;
}
// a * scale / b without 64-bit division: both are halved until it fits.
static uint32_t ratio(uint32_t a, uint32_t b, uint32_t scale) {
    while (a > 0xFFFFFFFFu / scale) { a >>= 1; b >>= 1; }
    return b ? a * scale / b : 0;
}
static char *hex(char *p, uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; i--) *p++ = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
    *p = 0;
    return p;
}

static inline void dot(int x, int y, uint8_t c);
static void small(int x, int y, const char *s, uint8_t c) { gfx_text(x, y, s, c); }   // 5x7, y = top

// ---- Direct framebuffer spans (the graph's inner loop) -------------------------
static inline void vspan(int x, int y0, int y1, uint8_t c) {   // rows y0..y1, y0 <= y1
    uint8_t *p = gfx_fb + y0 * GFX_FB_STRIDE + (x >> 1);
    if (x & 1) {
        uint8_t v = (uint8_t)(c << 4);
        for (int y = y0; y <= y1; y++, p += GFX_FB_STRIDE) *p = (uint8_t)((*p & 0x0F) | v);
    } else {
        for (int y = y0; y <= y1; y++, p += GFX_FB_STRIDE) *p = (uint8_t)((*p & 0xF0) | c);
    }
}
using agent::wipe;
static inline void dot(int x, int y, uint8_t c) {
    uint8_t *p = gfx_fb + y * GFX_FB_STRIDE + (x >> 1);
    *p = x & 1 ? (uint8_t)((*p & 0x0F) | (c << 4)) : (uint8_t)((*p & 0xF0) | c);
}

// ---- Events, as the log and the graph show them ---------------------------------------
struct EvLook { const char tag[5]; uint8_t c; };
static const EvLook LOOK[] = {
    {"", DIM},      {"NEW", GREEN}, {"DIR+", GREEN}, {"DEL", RED},    {"DIR-", RED},   {"REN", CYAN},
    {"MOVE", CYAN}, {"EDIT", AMBER}, {"CARD", PALE}, {"CARD", DIM}, {"USB", CYAN},   {"EJCT", GOLD},
    {"LOCK", GOLD}, {"RW", GOLD},    {"FMT", RED},   {"PART", RED},   {"CRC", AMBER},  {"FAIL", RED},
    {"GOAL", GOLD},
};

// The text of an event line: name (5x7) and value (Tiny, right).
static void evText(const mon::Event &e, char *name, char *val) {
    *val = 0;
    switch (e.type) {
        case mon::EV_NEW: case mon::EV_DEL: case mon::EV_REN: case mon::EV_MOVE: case mon::EV_MOD:
            put(name, e.name);
            if (e.live && e.value) {
                uint32_t pc = ratio(e.done, e.value, 100);
                put(num(val, pc > 99 ? 99 : pc), "%");
            } else {
                bytes(val, e.live && e.done > e.value ? e.done : e.value);
            }
            return;
        case mon::EV_NEWDIR: case mon::EV_DELDIR: put(name, e.name); return;
        case mon::EV_CARDIN: put(name, e.name[0] ? e.name : "INSERTED"); bytes(val, (uint64_t)e.value * 512); return;
        case mon::EV_CARDOUT: put(name, "REMOVED"); return;
        case mon::EV_PC: put(name, "PC CONNECTED"); return;
        case mon::EV_EJECT: put(name, "EJECTED BY PC"); return;
        case mon::EV_RO: put(name, "READ-ONLY"); return;
        case mon::EV_RW: put(name, "WRITABLE"); return;
        case mon::EV_FORMAT: put(name, "FORMATTED"); return;
        case mon::EV_PART: put(name, "NEW PARTITIONS"); return;
        case mon::EV_RETRY: put(put(name, e.name), " FIXED"); num(put(val, "x"), e.value); return;
        case mon::EV_FAIL: put(put(name, e.name), " FAILED"); hex(val, e.value, 6); return;
        case mon::EV_MILE: {
            char *p = e.value >= 1000 ? put(num(name, e.value / 1000), " GB ") : put(num(name, e.value), " MB ");
            put(p, e.name);
            return;
        }
    }
    put(name, "?");
}

// ---- Rates over the last second, from four 250 ms samples ------------------------------
struct Sample { uint32_t t, r, w, busy, cmds; };
static Sample smp[5];
static uint32_t nextSample = 0;
static uint32_t rateR, rateW, activePc, iops;   // KB/s, KB/s, %, commands/s

static void sampleRates(uint32_t now) {
    if ((int32_t)(now - nextSample) < 0) return;
    nextSample = now + 250;
    memmove(smp, smp + 1, sizeof smp - sizeof smp[0]);
    smp[4] = {now, usbmsc::blocksRead, usbmsc::blocksWritten, mon::st.busyUs, mon::st.cmdsR + mon::st.cmdsW};
    // The samples land between commands, so the window is a second or a little more.
    uint32_t ms = smp[4].t - smp[0].t;
    if (!ms) return;
    rateR = (smp[4].r - smp[0].r) * 500 / ms;
    rateW = (smp[4].w - smp[0].w) * 500 / ms;
    uint32_t b = (smp[4].busy - smp[0].busy) / (ms * 10);
    activePc = b > 100 ? 100 : b;
    iops = (smp[4].cmds - smp[0].cmds) * 1000 / ms;
}

// ---- Status bar -----------------------------------------------------------------------
static void sdIcon(int x, int y, uint8_t body, uint8_t label) {
    gfx_hline(x + 2, y, 5, body);
    gfx_hline(x + 1, y + 1, 6, body);
    gfx_fillRect(x, y + 2, 7, 7, body);
    dot(x + 3, y + 1, GOLD);
    dot(x + 5, y + 1, GOLD);
    gfx_fillRect(x + 1, y + 4, 5, 4, label);
}

static void statusBar(uint32_t now, const Status &s, bool active, bool write) {
    wipe(0, 11, PANEL);
    const char *word;
    uint8_t c, body, label = BG;
    bool blink = (now / 120) & 1;
    switch (s.mode) {
        case M_SAFE: word = "SAFE MODE"; c = GOLD; body = DIM; break;
        case M_NOCARD: word = "NO CARD"; c = RED; body = WFILL; label = BG; break;
        case M_WAITING: word = "STANDBY"; c = CYAN; body = DIM; break;
        case M_EJECTED: word = "EJECTED"; c = GOLD; body = GOLD; break;
        default:
            if (active && write) { word = "WRITING"; c = AMBER; body = AMBER; label = blink ? WHITE : WFILL; }
            else if (active) { word = "READING"; c = GREEN; body = GREEN; label = blink ? WHITE : RFILL; }
            else { word = "READY"; c = MID; body = MID; }
    }
    sdIcon(2, 1, body, label);
    small(13, 2, word, c);
    int x = 13 + gfx_textWidth(word) + 4;
#ifdef CHSD_TEST
    tiny(66, 3, "TEST", RED);                           // the fault-injecting build (tools/chsd_test.py)
#endif
    if (s.ro && s.mode != M_SAFE) {                     // a padlock: the PC cannot write
        gfx_rect(x + 1, 2, 3, 3, GOLD);
        gfx_fillRect(x, 4, 5, 5, GOLD);
        dot(x + 2, 6, PANEL);
    }
    // The speed now, as a watch would show it: dark segments behind lit ones.
    uint32_t v = rateR + rateW;
    if (v > 999) v = 999;
    bool w = rateW > rateR;
    uint8_t on = !v ? DIM : (w ? AMBER : GREEN);
    int dx = 95;
    for (int i = 0; i < 3; i++) {
        uint32_t p = i == 0 ? 100 : (i == 1 ? 10 : 1);
        int d = (v >= p || i == 2) ? (int)(v / p % 10) : -1;
        agent::seg7(dx + 6 * i, 1, d, on, GHOST);
    }
    if (v) {                                             // which way the data goes: up to the PC, down to the card
        int ax = 88;
        for (int k = 0; k < 3; k++) gfx_hline(ax + 2 - k, w ? 8 - k : 2 + k, 1 + 2 * k, on);
        gfx_vline(ax + 2, w ? 2 : 5, 4, on);
    }
    tiny(115, 0, "KB", DIM);
    tiny(115, 6, "/S", DIM);
}

static void gauge() {
    gfx_fillRect(0, GAUGE_Y, 128, 2, GRID);
    if (!mon::vol.clusters || mon::vol.freeClusters < 0) return;
    uint32_t pm = mon::usedPermille();
    int used = (int)(pm * 128 / 1000);
    if (!used && mon::vol.freeClusters < (int32_t)mon::vol.clusters) used = 1;
    uint8_t c = pm >= 970 ? RED : pm >= 900 ? AMBER : MID;
    gfx_fillRect(0, GAUGE_Y, used, 2, c);
    gfx_vline(used - 1, GAUGE_Y, 2, pm >= 900 ? c : GREEN);
}

// ---- The graph: one column per command, newest on the right ---------------------------
// Height in rows for a speed: GH rows is FULL_KBS (512, so a shift).
static inline int colHeight(uint32_t kbs) {
    if (!kbs) return 0;
    uint32_t h = (kbs * GH + FULL_KBS / 2) >> 9;
    return h < 1 ? 1 : (h > GH ? GH : (int)h);
}

static uint8_t regionColour(uint8_t f) {
    if (f & mon::RG_SYS) return WHITE;
    if (f & mon::RG_DIR) return CYAN;
    if (f & mon::RG_FAT) return GOLD;
    return f & mon::C_WRITE ? AMBER : GREEN;
}

static void markGlyph(int x, uint8_t type, uint8_t c) {
    int y = GY + 1;
    switch (type) {
        case mon::EV_NEW: case mon::EV_NEWDIR:          // +
            dot(x - 1, y + 1, c); dot(x, y + 1, c); dot(x + 1, y + 1, c); dot(x, y, c); dot(x, y + 2, c); break;
        case mon::EV_DEL: case mon::EV_DELDIR: case mon::EV_FAIL:   // x
            dot(x - 1, y, c); dot(x + 1, y, c); dot(x, y + 1, c); dot(x - 1, y + 2, c); dot(x + 1, y + 2, c); break;
        default:                                         // a diamond
            dot(x, y, c); dot(x - 1, y + 1, c); dot(x + 1, y + 1, c); dot(x, y + 2, c); break;
    }
}

// One pass over column pairs (two columns share a framebuffer byte): the
// rows where both are filled are plain byte stores, which is most rows.
// The area is already clear.
static void graph(uint32_t now) {
    uint32_t n = mon::columns;
    // Grid: dotted rules at 128, 256 and 384 KB/s; verticals every 16
    // commands that move with the data, as Task Manager's do with time.
    for (int k = 1; k < 4; k++) {
        uint8_t *p = gfx_fb + (GB - colHeight(FULL_KBS * k / 4)) * GFX_FB_STRIDE;
        uint8_t v = n & 1 ? (uint8_t)(GRID << 4) : GRID;
        for (int i = 0; i < GFX_FB_STRIDE; i++) p[i] = v;
    }
    for (int x = 127 - (int)(n % 16); x >= 0; x -= 16)
        for (int y = GY + (x & 1); y <= GB; y += 2) dot(x, y, GRID);
    tiny(1, GY + 1, "512K", DIM);

    int first = n >= 128 ? 0 : 128 - (int)n;
    uint8_t top[128], fl[128];                           // first lit row (GB + 1: none), flags
    for (int x = 0; x < 128; x++) {
        if (x < first) { top[x] = GB + 1; fl[x] = 0; continue; }
        const mon::Column &c = mon::hist[(n - 128 + x) % mon::HISTORY];
        top[x] = (uint8_t)(GB + 1 - colHeight(c.kbs));
        fl[x] = c.flags;
    }
    uint8_t *strip = gfx_fb + STRIP_Y * GFX_FB_STRIDE;
    for (int x = first & ~1; x < 128; x += 2) {
        // Fills start two rows under the line, which get a brighter shade.
        int ta = top[x] + 3, tb = top[x + 1] + 3;
        uint8_t ca = fl[x] & mon::C_WRITE ? WFILL : RFILL, cb = fl[x + 1] & mon::C_WRITE ? WFILL : RFILL;
        int both = ta > tb ? ta : tb;
        uint8_t *p = gfx_fb + both * GFX_FB_STRIDE + (x >> 1);
        uint8_t v = (uint8_t)(ca | (cb << 4));
        for (int y = both; y <= GB; y++, p += GFX_FB_STRIDE) *p = v;
        if (ta < both && ta <= GB) vspan(x, ta, (both <= GB ? both : GB + 1) - 1, ca);
        if (tb < both && tb <= GB) vspan(x + 1, tb, (both <= GB ? both : GB + 1) - 1, cb);
        uint8_t sv = (uint8_t)((x >= first ? regionColour(fl[x]) : PANEL) | (regionColour(fl[x + 1]) << 4));
        strip[x >> 1] = sv;
        strip[(x >> 1) + GFX_FB_STRIDE] = sv;
    }
    int prev = GB + 1;
    for (int x = first; x < 128; x++) {
        int t = top[x];
        uint8_t f = fl[x];
        bool w = f & mon::C_WRITE;
        if (t <= GB) {
            uint8_t mid = w ? AMID : DIM;
            if (t + 1 <= GB) dot(x, t + 1, mid);
            if (t + 2 <= GB) dot(x, t + 2, mid);
            // The line: from this column's top to the last one's, so it reads as a line.
            uint8_t line = f & (mon::C_FAIL | mon::C_RETRY) ? RED : (w ? AMBER : GREEN);
            if (prev > GB || prev == t) dot(x, t, line);
            else vspan(x, t < prev ? t : prev, t < prev ? prev : t, line);
        }
        prev = t;
        if (f & mon::C_FAIL) vspan(x, GY, GB, RED);
        const mon::Column &c = mon::hist[(n - 128 + x) % mon::HISTORY];
        if (c.mark) {
            uint8_t mc = LOOK[c.mark].c;
            for (int y = GY + 4; y < t; y += 2) dot(x, y, mc);
            markGlyph(x < 1 ? 1 : (x > 126 ? 126 : x), c.mark, mc);
        }
    }
    // The newest command glows while commands keep coming.
    if (n && now - mon::st.lastMs < 250 && top[127] <= GB) dot(127, top[127], WHITE);
}

// ---- Totals line and pages ----------------------------------------------------------------
static void totals() {
    char b[16];
    int x = tiny(0, SUM_Y, "R", DIM) + 2;
    bytes(b, (uint64_t)usbmsc::blocksRead * 512);
    x = tiny(x, SUM_Y, b, GREEN) + 6;
    x = tiny(x, SUM_Y, "W", DIM) + 2;
    bytes(b, (uint64_t)usbmsc::blocksWritten * 512);
    tiny(x, SUM_Y, b, AMBER);
    put(num(b, activePc), "%");
    tinyR(127, SUM_Y, b, activePc ? PALE : DIM);
    tinyR(127 - tinyW(b) - 4, SUM_Y, "BUSY", DIM);
}

static void tabs(uint32_t now) {
    static const char *const NAME[PAGES] = {"EVENTS", "STATS", "CARD"};
    agent::tabs(TAB_Y, NAME, PAGES, page);
    char b[12];
    if (page == P_STATS) {                              // how long the reader has been running
        uint32_t t = now / 1000;
        char *p = num(put(b, "UP "), t / 3600);
        *p++ = ':';
        *p++ = (char)('0' + t / 600 % 6); *p++ = (char)('0' + t / 60 % 10);
        *p++ = ':';
        *p++ = (char)('0' + t / 10 % 6); *p++ = (char)('0' + t % 10);
        *p = 0;
    }
    if (page == P_STATS || (page == P_LOG && mon::events)) {
        if (page == P_LOG) {
            uint32_t shown = mon::events < (uint32_t)mon::EVENTS ? mon::events : mon::EVENTS;
            char *p = num(b, scroll + 1);
            *p++ = '/';
            num(p, shown);
        }
        int bw = tinyW(b);
        gfx_fillRect(127 - bw - 2, TAB_Y - 1, bw + 3, 7, BG);
        tinyR(127, TAB_Y, b, DIM);
    }
}

static void logPage(uint32_t now) {
    if (!mon::events) {
        small(4, PAGE_Y + 6, "No activity yet", DIM);
        tiny(4, PAGE_Y + 18, "FILES THE PC ADDS, DELETES OR", DIM);
        tiny(4, PAGE_Y + 25, "RENAMES SHOW UP HERE BY NAME", DIM);
        return;
    }
    for (int i = 0; i < LINES; i++) {
        const mon::Event *e = mon::newest(scroll + i);
        if (!e) break;
        int y = PAGE_Y + i * LINE_H;
        const EvLook &l = LOOK[e->type];
        uint32_t age = now - e->t;
        bool fresh = scroll + i == 0 && age < 300;
        gfx_fillRect(0, y, 17, 7, fresh && ((age / 75) & 1) ? WHITE : l.c);
        tiny(9 - tinyW(l.tag) / 2, y + 1, l.tag, BG);
        char name[32], val[12];
        evText(*e, name, val);
        int vw = val[0] ? tinyW(val) + 3 : 0;
        int room = (127 - vw - 20) / 6;
        if ((int)strlen(name) > room) { name[room > 0 ? room - 1 : 0] = '~'; name[room > 0 ? room : 0] = 0; }
        uint8_t tc = scroll + i == 0 ? (fresh ? WHITE : PALE) : (i < 2 ? MID : DIM);
        small(20, y, name, tc);
        if (val[0]) tinyR(127, y + 1, val, e->live ? AMBER : (i + scroll == 0 ? PALE : DIM));
        if (e->live) {                                   // being written: how far along
            int w = 107;
            int done = e->value ? (int)ratio(e->done > e->value ? e->value : e->done, e->value, (uint32_t)w)
                                : (int)((now / 40) % w);
            gfx_hline(20, y + 8, w, GRID);
            if (e->value) gfx_hline(20, y + 8, done, AMBER);
            else gfx_hline(20 + done, y + 8, 8 < w - done ? 8 : w - done, AMBER);
        }
    }
}

// A line of the stats and card pages: label (dim) value (bright) pairs.
struct Pen {
    int x, y;
    Pen &l(const char *s) { x = tiny(x, y, s, DIM) + 3; return *this; }
    Pen &v(const char *s, uint8_t c = PALE) { x = tiny(x, y, s, c) + 5; return *this; }
};

// A count in at most 4 characters: 9999, 123K, 45M.
static char *count(char *p, uint32_t v) {
    if (v < 10000) return num(p, v);
    if (v < 1000000) return put(num(p, v / 1000), "K");
    return put(num(p, v / 1000000), "M");
}

// Three columns of label and value.
static void row(int y, const char *l1, const char *v1, uint8_t c1, const char *l2, const char *v2, uint8_t c2,
                const char *l3, const char *v3, uint8_t c3) {
    tiny(0, y, l1, DIM);  tiny(22, y, v1, c1);
    tiny(54, y, l2, DIM); tiny(73, y, v2, c2);
    tiny(89, y, l3, DIM); tiny(107, y, v3, c3);
}

static void statsPage() {
    char a[16], b[16], c[16];
    const mon::Stats &s = mon::st;
    int y = PAGE_Y;
    bytes(a, (uint64_t)usbmsc::blocksRead * 512);
    count(b, s.cmdsR);
    put(num(c, s.peakR), "K");
    row(y, "READ", a, GREEN, "CMDS", b, PALE, "PEAK", c, GREEN);
    bytes(a, (uint64_t)usbmsc::blocksWritten * 512);
    count(b, s.cmdsW);
    put(num(c, s.peakW), "K");
    row(y + 7, "WRITE", a, AMBER, "CMDS", b, PALE, "PEAK", c, AMBER);
    uint32_t cmds = s.cmdsR + s.cmdsW;
    put(num(a, activePc), "%");
    count(b, iops);
    bytes(c, cmds ? (uint64_t)((usbmsc::blocksRead + usbmsc::blocksWritten) / cmds) * 512 : 0);
    row(y + 14, "BUSY", a, PALE, "IOPS", b, PALE, "AVG", c, PALE);
    char *p = count(put(a, "+"), s.created);
    count(put(p, " -"), s.deleted);
    count(b, s.renamed);
    count(c, s.modified);
    row(y + 21, "FILES", a, PALE, "REN", b, CYAN, "EDIT", c, AMBER);
    p = count(put(a, "+"), s.dirsMade);
    count(put(p, " -"), s.dirsGone);
    count(b, s.retries);
    count(c, s.fails);
    row(y + 28, "DIRS", a, PALE, "CRC", b, s.retries ? AMBER : PALE, "FAIL", c, s.fails ? RED : PALE);
}

static const char *maker(uint8_t mid) {
    static const struct { uint8_t id; char name[9]; } M[] = {
        {0x01, "PANASONC"}, {0x02, "TOSHIBA"}, {0x03, "SANDISK"}, {0x1B, "SAMSUNG"}, {0x1D, "ADATA"},
        {0x27, "PHISON"}, {0x28, "LEXAR"}, {0x31, "SP"}, {0x41, "KINGSTON"}, {0x74, "TRANSCND"},
        {0x76, "PATRIOT"}, {0x82, "SONY"}, {0x9F, "KINGSTON"},
    };
    for (auto &m : M)
        if (m.id == mid) return m.name;
    return nullptr;
}

static void cardPage() {
    char a[24], b[16], c[16];
    int y = PAGE_Y;
    if (!mon::vol.blocks) { small(4, y + 6, "No card in the slot", DIM); return; }
    if (haveCid) {
        const char *m = maker(cid[0]);
        char *p = m ? put(a, m) : hex(put(a, "MID "), cid[0], 2);
        *p++ = ' ';
        for (int i = 3; i < 8; i++) if (cid[i] >= 0x20 && cid[i] < 0x7F) *p++ = (char)cid[i];
        *p = 0;
        p = num(put(b, "REV "), cid[8] >> 4);
        *p++ = '.';
        num(p, cid[8] & 15);
        Pen{0, y}.v(a).l(b);
        uint32_t sn = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16) | ((uint32_t)cid[11] << 8) | cid[12];
        hex(a, sn, 8);
        p = num(b, 2000 + ((cid[13] & 15) << 4) + (cid[14] >> 4));
        *p++ = '-';
        uint8_t mo = cid[14] & 15;
        *p++ = (char)('0' + mo / 10); *p++ = (char)('0' + mo % 10); *p = 0;
        Pen{0, y + 7}.l("SN").v(a).l("MADE").v(b);
    }
    static const char *const FS[] = {"NONE", "NO FAT", "FAT12", "FAT16", "FAT32", "EXFAT"};
    static const char *const TYPE[] = {"SD", "SDSC", "SDSC", "SDHC"};
    bytes(a, (uint64_t)mon::vol.blocks * 512);
    Pen{0, y + 14}.v(TYPE[cardType & 3], CYAN).v(a).v(FS[mon::vol.fs], CYAN).v(mon::vol.label[0] ? mon::vol.label : "NO LABEL", GOLD);
    if (mon::vol.fs >= mon::FS_FAT12) {
        bytes(a, mon::clusterBytes());
        num(b, mon::vol.partStart);
        bytes(c, (uint64_t)(mon::vol.fatEnd - mon::vol.fatStart) * 512);
        Pen{0, y + 21}.l("CLUSTER").v(a).l("AT").v(b).l("FATS").v(c);
        if (mon::vol.freeClusters >= 0) {
            bytes(a, mon::usedBytes());
            bytes(b, mon::freeBytes());
            put(num(c, (mon::usedPermille() + 5) / 10), "%");
            Pen{0, y + 28}.l("USED").v(a).l("FREE").v(b, GREEN).v(c, DIM);
        } else {
            Pen{0, y + 28}.l("FREE SPACE NOT KNOWN YET");
        }
    }
}

// ---- Overlays for the states with nothing to graph -------------------------------------
// The graph stays visible behind, dimmed: each colour to a darker one.
// (gfx_remapRect would do it, but lives in SRAM; this runs only when idle.)
static void dimRows(int y0, int y1) {
    static const uint8_t DIMMED[16] = {BG, DIM, PANEL, GRID, RFILL, GRID, WFILL, PANEL,
                                       WFILL, WFILL, PANEL, DIM, PANEL, GRID, WFILL, PANEL};
    for (uint8_t *p = gfx_fb + y0 * GFX_FB_STRIDE, *e = gfx_fb + y1 * GFX_FB_STRIDE; p < e; p++)
        *p = (uint8_t)(DIMMED[*p & 15] | (DIMMED[*p >> 4] << 4));
}

static void overlay(const Status &s) {
    if (s.mode == M_READY) return;
    dimRows(GY, STRIP_Y + 2);
    const char *title, *l1, *l2 = nullptr;
    uint8_t c;
    switch (s.mode) {
        case M_SAFE: title = "SAFE MODE"; c = GOLD; l1 = "USB serial only"; l2 = "Reset without B"; break;
        case M_NOCARD: title = "NO CARD"; c = RED; l1 = "Insert a card"; break;
        case M_WAITING: title = "STANDBY"; c = CYAN; l1 = "Waiting for the PC"; break;
        case M_EJECTED: title = "EJECTED"; c = GOLD; l1 = "A: back on the PC"; break;
        default: return;
    }
    agent::alert(GY + 8, title, l1, l2, c);
}

// ---- Frames ---------------------------------------------------------------------------------
static uint32_t lastFrame = 0xFFFF0000u, lastSig = 0, seenEvents = 0;
static uint32_t panelSig = 0, panelMs = 0, panelEvents = 0;
static bool panelValid = false;

void begin() {
    pal::init(PALETTE);
    pal::setCycling(false);             // no LUT rebuilds behind the card's back
}

void button(uint8_t b) {
    uint32_t shown = mon::events < (uint32_t)mon::EVENTS ? mon::events : mon::EVENTS;
    if (b == RIGHT_BUTTON) { page = (uint8_t)((page + 1) % PAGES); }
    else if (b == LEFT_BUTTON) { page = (uint8_t)((page + PAGES - 1) % PAGES); }
    else if (b == DOWN_BUTTON && page == P_LOG && scroll + LINES < shown) scroll++;
    else if (b == UP_BUTTON && scroll) scroll--;
    lastSig = 0;
    panelValid = false;
}

// New events: a beep, and for the big ones a banner over the graph. (The
// log's chip flashes by itself, logPage().)
static void announce() {
    const int CY = GY + GH / 2;
    for (; seenEvents < mon::events; seenEvents++) {
        const mon::Event &e = mon::ev[seenEvents % mon::EVENTS];
        if (scroll) scroll++;                            // keep the lines being read in place
        switch (e.type) {
            case mon::EV_NEW: case mon::EV_NEWDIR: audio::sfx(Sfx::FileNew); break;
            case mon::EV_DEL: case mon::EV_DELDIR: audio::sfx(Sfx::FileGone); break;
            case mon::EV_REN: case mon::EV_MOVE: case mon::EV_MOD: audio::sfx(Sfx::FileTouch); break;
            case mon::EV_CARDIN: audio::sfx(Sfx::CardIn); break;
            case mon::EV_CARDOUT: audio::sfx(Sfx::CardOut); break;
            case mon::EV_PC: audio::sfx(Sfx::Connect); break;
            case mon::EV_EJECT: audio::sfx(Sfx::Eject); break;
            case mon::EV_RO: fx::banner("LOCKED", fx::B_GREEN, CY, 50); audio::sfx(Sfx::Lock); break;
            case mon::EV_RW: fx::banner("UNLOCKED", fx::B_GREEN, CY, 50); audio::sfx(Sfx::Unlock); break;
            case mon::EV_FORMAT: fx::banner("FORMATTED", fx::B_RED, CY, 60); audio::sfx(Sfx::Big); break;
            case mon::EV_PART: fx::banner("PARTITIONED", fx::B_RED, CY, 60); audio::sfx(Sfx::Big); break;
            case mon::EV_MILE: {
                char b[12];
                if (e.value >= 1000) put(num(b, e.value / 1000), " GB");
                else put(num(b, e.value), " MB");
                fx::banner(b, fx::B_GREEN, CY, 50);
                audio::sfx(Sfx::Goal);
                break;
            }
            case mon::EV_FAIL: fx::banner("BLOCK FAILED", fx::B_RED, CY, 60); audio::sfx(Sfx::Fail); break;
            default: break;
        }
    }
    uint32_t shown = mon::events < (uint32_t)mon::EVENTS ? mon::events : mon::EVENTS;
    if (scroll + LINES > shown) scroll = shown > LINES ? (uint8_t)(shown - LINES) : 0;
}

// The bottom panel (tabs and page) is redrawn only when what it shows
// changed, and at most twice a second unless an event just came in:
// its text is most of a frame's drawing. Rows it leaves alone are not
// flushed either.
static uint32_t panelSigOf(uint32_t now, const Status &s) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(page); mix(scroll); mix(s.mode); mix(mon::events);
    if (s.mode == M_SAFE) return h;
    if (page == P_LOG) {
        for (int i = 0; i < LINES; i++) {
            const mon::Event *e = mon::newest(scroll + i);
            if (!e) break;
            mix(e->t); mix(e->done); mix(e->value); mix(e->type); mix(e->live);
            if (e->live && !e->value) mix(now / 40);         // the "size not known" sweep
            if (!i && !scroll && now - e->t < 300) mix(now / 75);   // a new event's chip flashes
        }
    } else if (page == P_STATS) {
        mix(usbmsc::blocksRead >> 4); mix(usbmsc::blocksWritten >> 4);
        mix(mon::st.cmdsR); mix(mon::st.cmdsW); mix(mon::st.peakR); mix(mon::st.peakW);
        mix(activePc); mix(iops); mix(mon::st.created); mix(mon::st.deleted); mix(mon::st.renamed);
        mix(mon::st.modified); mix(mon::st.dirsMade); mix(mon::st.dirsGone); mix(mon::st.retries);
        mix(mon::st.fails); mix(now / 1000);
    } else {
        mix((uint32_t)mon::vol.freeClusters); mix(mon::vol.blocks); mix(haveCid); mix(cardType); mix(mon::vol.fs);
    }
    return h;
}


Perf perf;

bool frame(uint32_t now, const Status &s, int &y0, int &y1) {
#ifdef CHSIM_NOSCREEN
    if (panelValid) return false;                        // the simulator's screen-off comparison
#endif
    sampleRates(now);
    bool active = now - mon::st.lastMs < 400 && mon::columns;
    const mon::Event *e0 = mon::newest(0);
    bool anim = fx::bannerActive() || (e0 && now - e0->t < 320) || (active && s.mode == M_READY);
    uint32_t sig = mon::columns * 2654435761u ^ mon::events * 40503u ^ (e0 ? e0->t + e0->done : 0) ^
                   (rateR << 7) ^ (rateW << 17) ^ activePc ^ ((uint32_t)s.mode << 28) ^ ((uint32_t)s.ro << 31) ^
                   ((uint32_t)page << 24) ^ ((uint32_t)scroll << 20) ^ (uint32_t)mon::vol.freeClusters ^ mon::vol.blocks ^
                   (uint32_t)active << 27 ^ (page == P_STATS ? now / 1000 * 7919u : 0);   // (its clock)
    // 5 frames a second while commands stream (a frame holds up the next
    // command by its drawing and flush, ~10 ms), 25 for a short animation
    // otherwise, none while nothing changes.
    uint32_t every = active ? 200 : (anim ? 40 : 100);
    if (now - lastFrame < every) return false;
    if (!anim && sig == lastSig && panelValid) return false;
    lastFrame = now;
    lastSig = sig;
    announce();

    uint32_t ps = panelSigOf(now, s);
    bool panel = !panelValid ||
                 (ps != panelSig && (mon::events != panelEvents || now - panelMs >= 500 || (e0 && now - e0->t < 300)));

    gfx_wait();                                          // the last frame may still be going out
    uint32_t t0 = micros();
    wipe(0, panel ? GFX_H : TAB_Y - 1, BG);
    statusBar(now, s, active, mon::st.lastWrite);
    gauge();
    graph(now);
    totals();
    if (panel) {
        tabs(now);
        if (s.mode == M_SAFE) {
            small(4, PAGE_Y + 6, "Holding B at power-on", DIM);
            small(4, PAGE_Y + 16, "keeps USB as a plain", DIM);
            small(4, PAGE_Y + 26, "serial port.", DIM);
        } else if (page == P_LOG) logPage(now);
        else if (page == P_STATS) statsPage();
        else cardPage();
        panelSig = ps;
        panelMs = now;
        panelEvents = mon::events;
        panelValid = true;
    }
    overlay(s);
    fx::drawBanner();
    y0 = 0;
    y1 = panel ? GFX_H : TAB_Y - 1;
    uint32_t dt = micros() - t0;
    perf.frames++;
    perf.totalUs += dt;
    if (dt > perf.maxUs) perf.maxUs = dt;
    perf.rows += (uint32_t)(y1 - y0);
    return true;
}

void goodbye() {
    gfx_wait();
    gfx_clear(BG);
    centred(56, "MENU", GREEN, 2);
}

}  // namespace ui
