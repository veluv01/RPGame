/*
 * FileBrowser - an on-screen microSD browser, GIF player and text viewer for
 *               the RPGame board (RP2040/RP2350, ST7735S 128x128, microSD on a
 *               shared hardware SPI).
 *
 *   UP / DOWN     move the highlight (hold to auto-repeat)
 *   LEFT / RIGHT  page up / page down
 *   A             open: enter a directory, play a .GIF, or read a text file
 *   B             go up one directory
 *   START         re-scan the current directory (or retry a failed mount)
 *
 * All eight buttons are switch-to-ground (netlist: SW1..SW8 pins 3 and 4 are
 * on GND), so they read LOW when pressed and use the internal pull-up.
 *
 * See gifplay.cpp for how a 128x128 animation is decoded with no frame buffer
 * at all, and textview.cpp for how a file larger than RAM is scrolled from a
 * single byte offset of state. rpgame.h covers the SPI hand-off and the
 * scratch arena the three modes take turns owning.
 *
 */
#include "rpgame.h"
#include "gifplay.h"
#include "textview.h"

/* --------------------------------------------------------------------- */
/* Directory model                                                        */
/* --------------------------------------------------------------------- */
/* The listing lives in the shared arena, which the GIF player and the text
 * viewer take over while they run - so it has to be rebuilt after either of
 * them returns. 128 entries is 2560 bytes of the 4352 available.
 *
 * size goes first on purpose: with name[13] leading, the uint32 forces three
 * bytes of padding before it AND the trailing bool pads the struct out to 24.
 * Leading with the aligned member makes it 20. */
#define MAX_ENTRIES 128

struct Entry {
    uint32_t size;
    char     name[13];      /* SD 1.3.0 hands out 8.3 names: 12 chars + NUL */
    bool     isDir;
};

static Entry *const entries = (Entry *)scratch;

static int  count     = 0;
static bool truncated = false;
static int  sel = 0;        /* highlighted index            */
static int  top = 0;        /* index drawn on the first row */

static char curPath[64] = "/";
static bool sdOk        = false;
static bool dirty       = true;

/* Playback reads about 10 KB per frame, and the bus clock is the difference
 * between hitting the GIF's frame delay and drifting behind it. Not every
 * card will run at 24 MHz on this flex, so mount tries fast and falls back. */
#define SD_FAST_HZ 24000000UL

/* --------------------------------------------------------------------- */
/* Layout                                                                 */
/* --------------------------------------------------------------------- */
#define LIST_BOT     (LIST_TOP + VISIBLE * ROW_H)
#define SCROLLBAR_W   3

/* --------------------------------------------------------------------- */
/* Formatting                                                             */
/* --------------------------------------------------------------------- */
/* At most 5 characters: "1023B", "1023K", "4095M". */
static void fmtSize(uint32_t v, char *out)
{
    char unit;
    if      (v < 1024UL)        { unit = 'B'; }
    else if (v < 1024UL * 1024) { unit = 'K'; v >>= 10; }
    else                        { unit = 'M'; v >>= 20; }
    fmtU32(v, out);
    const uint8_t n = (uint8_t)strlen(out);
    out[n]     = unit;
    out[n + 1] = 0;
}

/* --------------------------------------------------------------------- */
/* Path handling                                                          */
/* --------------------------------------------------------------------- */
/* Root is "/", everything below it carries no trailing slash
 * ("/GAMES/LEVELS"), which is what SD::open() wants. */
static bool pathPush(const char *name)
{
    size_t l = strlen(curPath);
    const size_t n = strlen(name);
    if (l + n + 2 > sizeof(curPath)) return false;
    if (curPath[l - 1] != '/') curPath[l++] = '/';
    strcpy(curPath + l, name);
    return true;
}

static bool pathPop(void)
{
    if (curPath[1] == 0) return false;              /* already at root */
    char *s = strrchr(curPath, '/');
    if (s == curPath) curPath[1] = 0;               /* back up to "/"  */
    else              *s = 0;
    return true;
}

/* Full path of the highlighted entry, into a caller-owned buffer. */
static bool selectedPath(char *out, size_t cap)
{
    if (count == 0) return false;
    const size_t l = strlen(curPath);
    const size_t n = strlen(entries[sel].name);
    if (l + n + 2 > cap) return false;
    strcpy(out, curPath);
    size_t w = l;
    if (out[w - 1] != '/') out[w++] = '/';
    strcpy(out + w, entries[sel].name);
    return true;
}

/* --------------------------------------------------------------------- */
/* Scanning                                                               */
/* --------------------------------------------------------------------- */
/* Directories first, then case-insensitive by name. Insertion sort: n is at
 * most 128 and this runs once per directory change, so the O(n^2) is free.
 *
 * The comparison is ASCII-only on purpose. toupper() drags in newlib's locale
 * tables, and __global_locale is 364 bytes of SRAM that the LZW suffix table
 * needs more than this does. */
static int entryCmp(const Entry &a, const Entry &b)
{
    if (a.isDir != b.isDir) return a.isDir ? -1 : 1;
    const char *p = a.name, *q = b.name;
    while (*p && *q) {
        char cp = *p++, cq = *q++;
        if (cp >= 'a' && cp <= 'z') cp = (char)(cp - 32);
        if (cq >= 'a' && cq <= 'z') cq = (char)(cq - 32);
        if (cp != cq) return cp < cq ? -1 : 1;
    }
    return (int)(unsigned char)*p - (int)(unsigned char)*q;
}

static void sortEntries(void)
{
    for (int i = 1; i < count; i++) {
        const Entry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entryCmp(entries[j], key) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
}

static void scanDir(void)
{
    count     = 0;
    truncated = false;
    sel       = 0;
    top       = 0;

    sdBegin();
    File dir = SD.open(curPath);
    if (dir) {
        dir.rewindDirectory();
        for (;;) {
            File e = dir.openNextFile();
            if (!e) break;

            if (count < MAX_ENTRIES) {
                Entry &en = entries[count++];
                strncpy(en.name, e.name(), sizeof(en.name) - 1);
                en.name[sizeof(en.name) - 1] = 0;
                en.isDir = e.isDirectory();
                en.size  = en.isDir ? 0 : e.size();
            } else {
                truncated = true;
            }
            e.close();
        }
        dir.close();
    }
    sdEnd();

    sortEntries();
    dirty = true;
}

/* --------------------------------------------------------------------- */
/* Drawing                                                                */
/* --------------------------------------------------------------------- */
/* Fit a string into `chars` columns. Paths keep their tail - the interesting
 * end is the deepest directory - while names keep their head. */
static void fitText(int x, int y, const char *s, uint8_t c, size_t chars, bool keepTail)
{
    const size_t n = strlen(s);
    if (n <= chars) { gfx_text(x, y, s, c); return; }

    char buf[24];
    if (chars > sizeof(buf) - 1) chars = sizeof(buf) - 1;

    if (keepTail) {
        buf[0] = '<';
        memcpy(buf + 1, s + n - (chars - 1), chars - 1);
    } else {
        memcpy(buf, s, chars - 1);
        buf[chars - 1] = '>';
    }
    buf[chars] = 0;
    gfx_text(x, y, buf, c);
}

static void drawScrollbar(void)
{
    if (count <= VISIBLE) return;

    const int x = GFX_W - SCROLLBAR_W;
    const int h = LIST_BOT - LIST_TOP;
    gfx_fillRect(x, LIST_TOP, SCROLLBAR_W, h, C_BAR);

    int thumb = (h * VISIBLE) / count;
    if (thumb < 6) thumb = 6;
    const int span = count - VISIBLE;
    const int y = LIST_TOP + ((h - thumb) * top) / (span ? span : 1);
    gfx_fillRect(x, y, SCROLLBAR_W, thumb, C_HDR_TEXT);
}

static bool isGif(const Entry &e) { return !e.isDir && extEquals(e.name, "gif"); }

static void drawBrowser(void)
{
    gfx_clear(C_BG);

    gfx_fillRect(0, 0, GFX_W, HDR_H, C_HDR_BG);
    fitText(2, 2, curPath, C_HDR_TEXT, 21, true);

    const int rowW    = (count > VISIBLE) ? (GFX_W - SCROLLBAR_W - 1) : GFX_W;
    const int sizeEnd = rowW - 3;

    if (count == 0) gfx_text(19, LIST_TOP + 34, "(empty folder)", C_DIM);

    for (int i = 0; i < VISIBLE; i++) {
        const int idx = top + i;
        if (idx >= count) break;

        const Entry &e = entries[idx];
        const int    y = LIST_TOP + i * ROW_H;
        const bool  on = (idx == sel);

        if (on) gfx_fillRect(0, y, rowW, ROW_H, C_SEL_BG);

        char name[15];
        strncpy(name, e.name, sizeof(name) - 2);
        name[sizeof(name) - 2] = 0;
        if (e.isDir) {
            const size_t n = strlen(name);
            name[n]     = '/';
            name[n + 1] = 0;
        }

        const uint8_t nc = on ? C_SEL_TEXT : (e.isDir ? C_DIR : C_TEXT);
        fitText(3, y + 2, name, nc, 13, false);

        if (!e.isDir) {
            char sz[8];
            fmtSize(e.size, sz);
            gfx_text(sizeEnd - (int)strlen(sz) * 6, y + 2, sz,
                     on ? C_SEL_TEXT : C_DIM);
        }
    }

    drawScrollbar();

    gfx_fillRect(0, STATUS_Y, GFX_W, GFX_H - STATUS_Y, C_HDR_BG);

    char left[16];
    if (count == 0) {
        strcpy(left, "0 items");
    } else {
        char a[12], b[12];
        fmtU32((uint32_t)sel + 1, a);
        fmtU32((uint32_t)count, b);
        strcpy(left, a);
        strcat(left, "/");
        strcat(left, b);
        if (truncated) strcat(left, "+");
    }
    gfx_text(3, STATUS_Y + 3, left, C_DIM);

    if (count > 0) {
        const char *hint = entries[sel].isDir ? "A open"
                         : isGif(entries[sel]) ? "A play"
                                               : "A read";
        gfx_text(GFX_W - 3 - (int)strlen(hint) * 6, STATUS_Y + 3, hint, C_HDR_TEXT);
    }

    gfx_flush();
}

static void drawSdError(void)
{
    gfx_clear(C_BG);
    gfx_fillRect(0, 0, GFX_W, HDR_H, C_HDR_BG);
    gfx_text(2, 2, "SD CARD", C_ERR);

    int y = LIST_TOP + 4;
    gfx_text(3, y, "Mount failed.",     C_ERR);   y += 14;
    gfx_text(3, y, "Card inserted?",    C_TEXT);  y += 10;
    gfx_text(3, y, "FAT16 / FAT32?",    C_TEXT);  y += 14;
#if RPGAME_PIZERO_ONBOARD_SD
    gfx_text(3, y, "Onboard SD slot", C_TEXT); y += 10;
    gfx_text(3, y, "Reinsert and retry.", C_DIM);
#else
    gfx_text(3, y, "Wiring:",           C_TEXT);  y += 10;
    char wiring[24];
    snprintf(wiring, sizeof wiring, "CLK GP%d MISO GP%d", RPGAME_SD_SPI_SCK, RPGAME_SD_SPI_MISO);
    gfx_text(3, y, wiring, C_DIM); y += 10;
    snprintf(wiring, sizeof wiring, "MOSI GP%d CS GP%d", RPGAME_SD_SPI_MOSI, PIN_SD_CS);
    gfx_text(3, y, wiring, C_DIM);
#endif

    gfx_fillRect(0, STATUS_Y, GFX_W, GFX_H - STATUS_Y, C_HDR_BG);
    gfx_text(3, STATUS_Y + 3, "START = retry", C_HDR_TEXT);
    gfx_flush();
}

/* --------------------------------------------------------------------- */
/* Selection                                                              */
/* --------------------------------------------------------------------- */
static void moveSel(int delta)
{
    if (count == 0) return;

    sel += delta;
    if (sel < 0)      sel = 0;
    if (sel >= count) sel = count - 1;

    if (sel < top)                                top = sel;
    if (sel >= top + VISIBLE)                     top = sel - VISIBLE + 1;
    if (count > VISIBLE && top > count - VISIBLE) top = count - VISIBLE;
    if (top < 0)                                  top = 0;

    dirty = true;
}

/* --------------------------------------------------------------------- */
/* Opening things                                                         */
/* --------------------------------------------------------------------- */
/* The player and the viewer both take over the scratch arena, so the listing
 * is gone by the time they return and has to be rebuilt. The directory has
 * not changed and the sort is deterministic, so putting the cursor back where
 * it was is exact rather than a guess. */
static void reopenListing(int keepSel, int keepTop)
{
    scanDir();
    if (keepSel < count) { sel = keepSel; top = keepTop; }
    if (top > sel)                                top = sel;
    if (sel >= top + VISIBLE)                     top = sel - VISIBLE + 1;
    if (count > VISIBLE && top > count - VISIBLE) top = count - VISIBLE;
    if (top < 0)                                  top = 0;
}

static void openSelected(void)
{
    if (count == 0) return;

    if (entries[sel].isDir) {
        if (pathPush(entries[sel].name)) scanDir();
        return;
    }

    char path[80];
    if (!selectedPath(path, sizeof(path))) return;

    const int keepSel = sel, keepTop = top;

    if (isGif(entries[sel])) {
        splashMessage("Loading...", C_TEXT);
        if (gifPlay(path) == GIF_ERROR) {
            splashMessage(gifErrorText, C_ERR);
            while (btnAnyDown()) { }
            delay(1200);
        }
    } else if (!textView(path)) {
        splashMessage("Cannot open", C_ERR);
        delay(1200);
    }

    reopenListing(keepSel, keepTop);
    btnResetAll();
    dirty = true;
}

/* --------------------------------------------------------------------- */
/* Lifecycle                                                              */
/* --------------------------------------------------------------------- */
static bool mountCard(void)
{
    sdBegin();
    bool ok = SD.begin(SD_FAST_HZ, PIN_SD_CS);
    if (!ok) ok = SD.begin(PIN_SD_CS);      /* retry at the conservative clock */
    sdEnd();
    return ok;
}

static void remount(void)
{
    splashMessage("Reading card...", C_TEXT);
    strcpy(curPath, "/");
    sdOk = mountCard();
    if (sdOk) scanDir();
    dirty = true;
}

void setup()
{
    pinMode(PIN_BTN_UP,     INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
    pinMode(PIN_BTN_LEFT,   INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT,  INPUT_PULLUP);
    pinMode(PIN_BTN_A,      INPUT_PULLUP);
    pinMode(PIN_BTN_B,      INPUT_PULLUP);
    pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
    pinMode(PIN_BTN_START,  INPUT_PULLUP);

    /* Graphics first: on shared-bus boards it parks SD_CS high while the
     * panel initializes. Dedicated SD initialization owns its own CS. */
    gfx_begin(GFX_DIV2, GFX_16BPP);
    gfx_setPalette(PALETTE, 16);

    remount();
}

void loop()
{
    if (!sdOk) {
        if (dirty) { drawSdError(); dirty = false; }
        if (btnPressed(bStart, false)) remount();
        delay(10);
        return;
    }

    if (btnPressed(bUp,    true)) moveSel(-1);
    if (btnPressed(bDown,  true)) moveSel(+1);
    if (btnPressed(bLeft,  true)) moveSel(-VISIBLE);
    if (btnPressed(bRight, true)) moveSel(+VISIBLE);

    if (btnPressed(bA, false)) openSelected();
    if (btnPressed(bB, false)) { if (pathPop()) scanDir(); }
    if (btnPressed(bStart, false)) scanDir();

    if (dirty) { drawBrowser(); dirty = false; }

    delay(5);
}
