// The intro, the card's folders and the refusals: everything before a
// model is open. Drawn as CHSDtoUSB's instrument panel is (Agent.h): a
// status bar with the folder's name and a seven-segment count, a gauge for
// where the list is, tag chips (DIR bright, STL in the wires' blue), the
// highlight that slides to the row picked, and key chips along the bottom.
#pragma GCC optimize("Os")
#include "App.h"
#include "Card.h"
#include <string.h>

using namespace agent;

Arena arena;

char *putCount(char *p, uint32_t v) {
    if (v < 100000) return fmtInt(p, (int32_t)v);
    return fmtStr(fmtInt(p, (int32_t)((v + 500) / 1000)), "K");
}

void cubeIcon(int x, int y, uint8_t edge, uint8_t face) {
    gfx_fillRect(x + 1, y + 3, 5, 5, face);
    gfx_rect(x, y + 2, 6, 7, edge);
    gfx_hline(x + 2, y, 5, edge);
    gfx_vline(x + 6, y, 7, edge);
    gfx_pixel(x + 1, y + 1, edge);
    gfx_pixel(x + 5, y + 1, edge);
    gfx_pixel(x + 5, y + 8, BG);
}

namespace browser {

// ---- The folder -------------------------------------------------------------
static const uint8_t MAX_DEPTH = 8, VISIBLE = 10, LIST_TOP = 24, ROW_H = 9;
static Entry *const entries = arena.entries;
static uint8_t count, stls;
static bool truncated;
static int sel, top;
static uint32_t dirs[MAX_DEPTH];        // the folders opened, root first
static uint8_t depth;
static int8_t selAt[MAX_DEPTH], topAt[MAX_DEPTH];   // the highlight in each folder above
static char path[64] = "/";
static int barY = -1;                   // the highlight's row on screen, x4 (it slides)
static uint8_t t;                       // frames on this screen

// Message screen.
static const char *msgTitle, *msgL1, *msgL2;
static char msgBuf[3][22];

static bool isStl(const char *n11) { return n11[8] == 'S' && n11[9] == 'T' && n11[10] == 'L'; }

static bool collect(const char *n, const fat::File &f, bool isDir, void *) {
    if (!isDir && !isStl(n)) return true;
    if (isDir && !memcmp(n, "SYSTEM~", 7)) return true;      // Windows' System Volume Information
    if (count == MAX_ENTRIES) { truncated = true; return false; }
    Entry &e = entries[count++];
    char *p = e.name;
    for (int i = 0; i < 8 && n[i] != ' '; i++) *p++ = n[i];
    if (n[8] != ' ') {
        *p++ = '.';
        for (int i = 8; i < 11 && n[i] != ' '; i++) *p++ = n[i];
    }
    *p = 0;
    e.cluster = f.cluster;
    e.size = f.size;
    e.isDir = isDir;
    if (!isDir) stls++;
    return true;
}

static int cmp(const Entry &a, const Entry &b) {
    if (a.isDir != b.isDir) return a.isDir ? -1 : 1;
    return strcmp(a.name, b.name);
}

// Reads the current folder; keeps sel/top if they still fit.
static void scan() {
    count = stls = 0;
    truncated = false;
    if (card::state() == card::READY) {
        fat::File d;
        if (depth) { d.cluster = dirs[depth - 1]; d.size = 0; }
        else fat::root(d);
        card::list(d, collect, nullptr);
    }
    for (int i = 1; i < count; i++) {               // insertion sort: folders, then names
        Entry key = entries[i];
        int j = i - 1;
        while (j >= 0 && cmp(entries[j], key) > 0) { entries[j + 1] = entries[j]; j--; }
        entries[j + 1] = key;
    }
    if (sel >= count) sel = count ? count - 1 : 0;
    if (top > sel) top = sel;
    if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
    barY = -1;
}

static void mountAndScan() {
    card::mount();
    depth = 0;
    sel = top = 0;
    fmtStr(path, "/");
    scan();
}

void back() {
    scan();
    screen = Scr::List;
    t = 0;
}

void message(const char *title, const char *l1, const char *l2) {
    // The texts may point into the list, which the caller may be about to
    // lose: keep copies.
    msgTitle = strncpy(msgBuf[0], title, 21);
    msgL1 = l1 ? strncpy(msgBuf[1], l1, 21) : nullptr;
    msgL2 = l2 ? strncpy(msgBuf[2], l2, 21) : nullptr;
    fx::shake(14, 3);
    audio::sfx(Sfx::Deny);
    screen = Scr::Msg;
    t = 0;
}

static void enterFolder(const Entry &e) {
    size_t l = strlen(path);
    if (depth == MAX_DEPTH || l + strlen(e.name) + 2 > sizeof path) {
        message("TOO DEEP", "Folders nest 8 deep", "at most here");
        return;
    }
    selAt[depth] = (int8_t)sel;
    topAt[depth] = (int8_t)top;
    dirs[depth++] = e.cluster;
    fmtStr(fmtStr(path + l, e.name), "/");
    sel = top = 0;
    scan();
    audio::sfx(Sfx::Open);
}

static void leaveFolder() {
    path[strlen(path) - 1] = 0;                     // the trailing '/'
    *(strrchr(path, '/') + 1) = 0;
    depth--;
    sel = selAt[depth];
    top = topAt[depth];
    scan();
    audio::sfx(Sfx::Back);
}

// ---- Screens ------------------------------------------------------------------
static void moveSel(int d) {
    if (!count) return;
    int s = sel + d;
    if (s < 0) s = 0;
    if (s >= count) s = count - 1;
    if (s == sel) return;
    sel = s;
    if (sel < top) top = sel;
    if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
    audio::sfx(Sfx::Move);
}

static void listUpdate() {
    if (rpgame.justPressed(START_BUTTON)) {
        mountAndScan();
        audio::sfx(card::state() == card::READY ? Sfx::Ready : Sfx::Deny);
        t = 0;
        return;
    }
    if (rpgame.repeat(UP_BUTTON)) moveSel(-1);
    if (rpgame.repeat(DOWN_BUTTON)) moveSel(1);
    if (rpgame.repeat(LEFT_BUTTON)) moveSel(-VISIBLE);
    if (rpgame.repeat(RIGHT_BUTTON)) moveSel(VISIBLE);
    if (rpgame.justPressed(B_BUTTON) && depth) leaveFolder();
    else if (rpgame.justPressed(A_BUTTON) && count) {
        const Entry &e = entries[sel];
        if (e.isDir) {
            enterFolder(e);
        } else {
            char name[13];
            strcpy(name, e.name);                   // the scan overwrites the list
            viewer::open(name, e.cluster, e.size);
        }
    }
}

static void listDraw() {
    // The status bar: this folder's name (CARD at the root) and how many
    // models it holds.
    char title[13];
    if (depth) {
        const char *s = path + strlen(path) - 2;
        while (*s != '/') s--;
        char *p = fmtStr(title, s + 1);
        p[-1] = 0;                                  // the trailing '/'
    } else {
        fmtStr(title, "CARD");
    }
    card::State st = card::state();
    bool ok = st == card::READY;
    statusBar(ok ? title : st == card::EXFAT ? "EXFAT" : st == card::NOFS ? "NO FAT" : "NO CARD", ok ? LIVE : ALERT);
    cubeIcon(2, 1, ok ? LIVE : ALERT, PANEL);
    readout(stls, LIVE, "STL", "");
    // Where the list is: the gauge under the bar, as the reader's card fill.
    gauge(0, 12, 128, 2, count ? sel + 1 : 0, count, MID);

    if (!ok) {
        if (st == card::EXFAT) alert(40, "EXFAT", "Reformat the card", "as FAT32", ALERT);
        else if (st == card::NOFS) alert(40, "NO FAT", "No FAT16/FAT32", "volume on the card", ALERT);
        else alert(40, "NO CARD", "Insert a FAT16 or", "FAT32 card", ALERT);
        keys(120, "START:RETRY");
        return;
    }

    // The path, its deepest end if it is long; the count on the right.
    const char *p = path;
    size_t l = strlen(p);
    if (l > 20) p += l - 20;
    tiny(0, 16, p, DIM);
    char buf[16];
    fmtStr(fmtInt(buf, count), truncated ? "+ ITEMS" : " ITEMS");
    tinyR(127, 16, buf, DIM);

    if (!count) {
        alert(44, "EMPTY", "No .STL files or", "folders here", MID);
    } else {
        // The highlight slides to the row picked.
        int want = (LIST_TOP + (sel - top) * ROW_H) * 4;
        barY = barY < 0 ? want : barY + (want - barY) / 2;
        int by = (barY + 2) / 4;
        gfx_fillRect(0, by - 1, 124, ROW_H, PANEL);
        brackets(0, by - 1, 124, ROW_H, 3, LIVE);
        for (int i = 0; i < VISIBLE && top + i < count; i++) {
            const Entry &e = entries[top + i];
            int y = LIST_TOP + i * ROW_H;
            bool hi = top + i == sel;
            chip(2, y, 15, e.isDir ? "DIR" : "STL", hi && (t & 8) ? PALE : e.isDir ? LIVE : WIRE_CHIP);
            gfx_text(20, y, e.name, hi ? PALE : e.isDir ? LIVE : WIRE_TEXT);
            if (!e.isDir) {
                // Triangles, from the size: exact for a binary STL.
                uint32_t n = e.size > STL_HEADER_BYTES ? (e.size - STL_HEADER_BYTES) / STL_RECORD_BYTES : 0;
                putCount(buf, n);
                tinyR(121, y + 1, buf, hi ? PALE : DIM);
            }
        }
        if (count > VISIBLE) {                      // a dotted track and its thumb
            int h = VISIBLE * ROW_H, th = h * VISIBLE / count;
            if (th < 6) th = 6;
            int y = LIST_TOP - 1 + (h - th) * top / (count - VISIBLE);
            for (int r = LIST_TOP - 1; r < LIST_TOP - 1 + h; r += 2) gfx_pixel(126, r, GRID);
            gfx_fillRect(125, y, 3, th, MID);
        }
    }
    gfx_hline(0, 116, 128, GRID);
    keys(120, depth ? "A:OPEN B:UP START:CARD" : "A:OPEN START:CARD");
}

// The intro: the glass is swept, a cube turns, the name types itself out,
// the card is read and its state typed below. Any button hurries it along.
static const uint8_t INTRO_MOUNT = 46, INTRO_END = 84;
static const char NAME[] = "STL VIEWER", SUB[] = "WIREFRAME UPLINK";

static void introUpdate() {
    if (t < INTRO_MOUNT && t > 6 && rpgame.justPressedMask()) t = INTRO_MOUNT;
    if (t == INTRO_MOUNT) {
        mountAndScan();
        audio::sfx(card::state() == card::READY ? Sfx::Ready : Sfx::Deny);
    }
    if (t > 22 && t < 22 + 2 * (int)sizeof NAME && !(t & 1)) audio::blip(2600, 4, true);
    if (t >= INTRO_END || (t > INTRO_MOUNT + 6 && rpgame.justPressedMask())) {
        screen = Scr::List;
        t = 0;
    }
}

// A wireframe cube turning over the name: the renderer's own trig and
// line, the wires' blue ramp, the far edges drawn first and dimmest.
static void introCube(int cx, int cy, int r, uint16_t yaw) {
    static const uint16_t PITCH = 0x0E00;           // about 20 degrees, from above
    int32_t cy_ = stlCos(yaw), sy = stlSin(yaw), cp = stlCos(PITCH), sp = stlSin(PITCH);
    int16_t px[8], py[8], pz[8];
    for (int i = 0; i < 8; i++) {
        int32_t x = i & 1 ? r : -r, y = i & 2 ? r : -r, z = i & 4 ? r : -r;
        int32_t x1 = (x * cy_ - z * sy) >> 14, z1 = (x * sy + z * cy_) >> 14;
        int32_t y1 = (y * cp - z1 * sp) >> 14, z2 = (y * sp + z1 * cp) >> 14;
        px[i] = (int16_t)(cx + x1);
        py[i] = (int16_t)(cy + y1);
        pz[i] = (int16_t)z2;
    }
    // The 12 edges join corners one bit apart; by depth, far first.
    uint8_t ea[12], eb[12];
    int n = 0;
    for (int i = 0; i < 8; i++)
        for (int bit = 1; bit < 8; bit <<= 1)
            if (!(i & bit)) { ea[n] = (uint8_t)i; eb[n] = (uint8_t)(i | bit); n++; }
    for (int k = 0; k < 12; k++) {
        int best = k;
        for (int j = k + 1; j < 12; j++)
            if (pz[ea[j]] + pz[eb[j]] > pz[ea[best]] + pz[eb[best]]) best = j;
        uint8_t ta = ea[k], tb = eb[k];
        ea[k] = ea[best]; eb[k] = eb[best]; ea[best] = ta; eb[best] = tb;
        int s = 4 - (pz[ea[k]] + pz[eb[k]]) * 3 / (2 * r);    // 1 (far) .. 7 (near)
        if (s < 0) s = 0;
        if (s > STL_SHADES - 1) s = STL_SHADES - 1;
        stlLine(px[ea[k]], py[ea[k]], px[eb[k]], py[eb[k]], (uint8_t)(STL_SHADE0 + s));
    }
}

static void introDraw() {
    card::State st = card::state();
    bool up = t >= INTRO_MOUNT, ok = st == card::READY;
    statusBar(up ? (ok ? "ONLINE" : "NO CARD") : "STANDBY", up ? (ok ? LIVE : ALERT) : MID);
    cubeIcon(2, 1, up ? (ok ? LIVE : ALERT) : MID, PANEL);
    readout(t < INTRO_MOUNT ? (uint32_t)t * 100 / INTRO_MOUNT : 100, LIVE, "%", "");
    gauge(0, 12, 128, 2, t < INTRO_MOUNT ? t : INTRO_MOUNT, INTRO_MOUNT, MID);
    if (t < 26) sweep(14 + t * 5);
    if (t >= 8) introCube(64, 42, t < 20 ? (t - 8) + 4 : 16, (uint16_t)(t * 700));
    brackets(6, 18, 116, 50, 5, t < 26 ? GRID : MID);
    typed(64 - 60, 76, NAME, t > 22 ? (t - 22) / 2 : 0, LIVE, 2, t < INTRO_MOUNT && (t & 4));
    typed(64 - 48, 96, SUB, t > 34 ? (t - 34) : 0, DIM);
    if (up) {
        const char *s = ok ? "CARD ONLINE" : st == card::EXFAT ? "CARD EXFAT" : "NO CARD";
        typed(64 - gfx_textWidth(s) / 2, 108, s, (t - INTRO_MOUNT) / 2, ok ? LIVE : ALERT);
    }
    tiny(0, 122, "V" STLV_VERSION, GRID);
}

static void msgUpdate() {
    if (t > 20 && rpgame.justPressedMask()) {
        fx::clear();
        if (card::state() != card::READY) mountAndScan();
        else back();
        screen = Scr::List;
        t = 0;
    }
}

static void msgDraw() {
    statusBar("ALERT", ALERT);
    cubeIcon(2, 1, ALERT, PANEL);
    gauge(0, 12, 128, 2, 1, 1, ALERT);
    alert(36, msgTitle, nullptr, nullptr, ALERT);
    if (msgL1) typed(64 - gfx_textWidth(msgL1) / 2, 74, msgL1, t, PALE);
    if (msgL2) typed(64 - gfx_textWidth(msgL2) / 2, 86, msgL2, t > 10 ? t - 10 : 0, DIM);
    if (t > 20) keys(120, "ANY:BACK");
}

void begin() {
    screen = Scr::Intro;
    t = 0;
}

void frame() {
    switch (screen) {
        case Scr::Intro: introUpdate(); break;
        case Scr::List: listUpdate(); break;
        case Scr::Msg: msgUpdate(); break;
        default: break;
    }
    if (screen == Scr::View) return;                // a model was opened: the viewer draws
    if (t < 255) t++;
    fx::update();
    pal::tick();
    audio::update();

    gfx_wait();
    pal::commit();
    gfx_clear(BG);
    switch (screen) {
        case Scr::Intro: introDraw(); break;
        case Scr::List: listDraw(); break;
        default: msgDraw(); break;
    }
    fx::applyShake(0, 127, BG);
    gfx_flushAsync();
}

}  // namespace browser
