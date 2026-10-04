/*
 * textview.cpp - scroll a text file that does not fit in RAM.
 *
 * ===========================================================================
 * THE IDEA
 * ===========================================================================
 * The viewer holds no text. Its entire state is ONE number: tvTop, the byte
 * offset of the first visible line. Everything on screen is re-derived from
 * the card each repaint, through a 256-byte sliding window. An 11 KB file and
 * a 300 MB file cost exactly the same.
 *
 * Scrolling DOWN is easy: wrap one line forward from tvTop and that offset is
 * the new top.
 *
 * Scrolling UP is the whole problem, and it is worth being precise about why.
 * With word wrap, "the previous line" is not a property of the bytes near
 * tvTop - where a wrapped line begins depends on where its *paragraph* began,
 * because that is what the greedy wrap ran from. Reading backwards to the
 * nearest space would give a different answer than the one scrolling forwards
 * produced, and the view would drift out of alignment with itself.
 *
 * So going up means: seek back to the start of the hard line (the previous
 * '\n') containing tvTop, re-wrap forward from there, and take the last
 * wrapped start strictly before tvTop. If tvTop already IS a hard line start,
 * step back one further paragraph first and take its last wrapped line.
 * That reproduces exactly the offsets scrolling down produced, so up and down
 * are true inverses and the view cannot drift.
 *
 * Work per keypress is bounded by the length of one paragraph. This card's
 * TELLTALE.TXT has 1618-character paragraphs, which is about 6 window reads
 * and 80 wrap steps - imperceptible. A file with NO newlines at all would
 * make one paragraph the whole file, so the backward scan gives up after
 * TV_SCAN_BACK bytes and treats that point as a line start. The wrap then
 * disagrees with what scrolling down produced, which is visible as a one-off
 * re-flow rather than as a hang. That is the deliberate trade: bounded work
 * beats exact alignment in a pathological file.
 * ===========================================================================
 */
#include "textview.h"

#define TV_COLS        21          /* 128 px / 6 px advance                */
#define TV_WINDOW     256          /* sliding read window, 256-aligned     */
#define TV_SCAN_BACK 4096          /* backward search limit for a '\n'     */
#define TV_TABSTOP      4

static File     tvFile;
static uint32_t tvSize;
static uint32_t tvTop;

/* The read window lives in the shared arena. */
static uint8_t *const tvBuf = scratch;
static uint32_t tvBufBase;
static uint16_t tvBufLen;

/* --------------------------------------------------------------------- */
/* Random access through one aligned window                               */
/* --------------------------------------------------------------------- */
/*
 * Aligning the window to 256 matters for the backward scan: an unaligned
 * window would refill on every single byte as the scan walked below its base,
 * turning a 4 KB search into 4096 seeks. Aligned, it refills once per 256
 * bytes in either direction, and the alignment also lines up with the SD
 * library's own 512-byte block cache.
 */
static int byteAt(uint32_t off)
{
    if (off >= tvSize) return -1;

    if (tvBufLen == 0 || off < tvBufBase || off >= tvBufBase + tvBufLen) {
        const uint32_t base = off & ~(uint32_t)(TV_WINDOW - 1);
        sdBegin();
        int n = 0;
        if (tvFile.seek(base)) n = tvFile.read(tvBuf, TV_WINDOW);
        sdEnd();
        if (n <= 0) { tvBufLen = 0; return -1; }
        tvBufBase = base;
        tvBufLen  = (uint16_t)n;
        if (off >= tvBufBase + tvBufLen) return -1;
    }
    return tvBuf[off - tvBufBase];
}

/* --------------------------------------------------------------------- */
/* Glyphs                                                                 */
/* --------------------------------------------------------------------- */
#define G_EOF   (-1)
#define G_BREAK (-2)               /* end of a hard line */

/*
 * Returns one displayable character and advances *p past every byte it
 * consumed. UTF-8 sequences collapse to a single glyph so that a byte offset
 * and a screen column stay in step - the file on this card is full of curly
 * quotes and em dashes, and rendering each as three '?' would wreck the wrap.
 */
static int readGlyph(uint32_t *p)
{
    const int b = byteAt(*p);
    if (b < 0) return G_EOF;
    (*p)++;

    if (b == '\n') return G_BREAK;
    if (b == '\r') {                              /* CRLF or a bare CR */
        if (byteAt(*p) == '\n') (*p)++;
        return G_BREAK;
    }
    if (b == '\t') return '\t';
    if (b < 32)    return ' ';
    if (b < 127)   return b;
    if (b == 127)  return '?';

    /* UTF-8 */
    uint32_t cp;
    int extra;
    if      ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
    else                         return '?';      /* stray continuation byte */

    while (extra--) {
        const int c = byteAt(*p);
        if (c < 0 || (c & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (uint32_t)(c & 0x3F);
        (*p)++;
    }

    switch (cp) {
        case 0x00A0: return ' ';
        case 0x2018: case 0x2019: case 0x2032: return '\'';
        case 0x201C: case 0x201D: case 0x2033: return '"';
        case 0x2010: case 0x2011: case 0x2012:
        case 0x2013: case 0x2014: case 0x2015: return '-';
        case 0x2022: return '*';
        case 0x2026: return '.';
        default:     return '?';
    }
}

static uint32_t skipSpacesFrom(uint32_t p)
{
    for (;;) {
        const int b = byteAt(p);
        if (b != ' ' && b != '\t') return p;
        p++;
    }
}

/* --------------------------------------------------------------------- */
/* Wrapping                                                               */
/* --------------------------------------------------------------------- */
/*
 * Lay out the wrapped line that starts at `start`. Fills `out` (which must
 * hold TV_COLS + 1 chars) and returns the offset the NEXT wrapped line starts
 * at. Greedy word wrap; a word longer than the screen is broken hard.
 *
 * This function is the single definition of where lines begin. Rendering,
 * scrolling down and scrolling up all go through it, which is what keeps them
 * consistent with each other.
 */
static uint32_t wrapLine(uint32_t start, char *out)
{
    uint8_t  col = 0;
    uint32_t p = start;
    uint32_t breakAt = 0;                          /* offset just past a space */
    uint8_t  breakCol = 0;
    uint32_t next;

    for (;;) {
        const uint32_t glyphStart = p;
        const int g = readGlyph(&p);

        if (g == G_EOF)   { next = glyphStart; break; }
        if (g == G_BREAK) { next = p;          break; }

        if (g == '\t') {
            uint8_t target = (uint8_t)((col / TV_TABSTOP + 1) * TV_TABSTOP);
            if (target > TV_COLS) target = TV_COLS;
            while (col < target) out[col++] = ' ';
            breakAt  = p;
            breakCol = col;
        } else {
            out[col++] = (char)g;
            if (g == ' ') { breakAt = p; breakCol = col; }
        }

        if (col >= TV_COLS) {
            /* The line is full. Peek at what follows before deciding to wrap:
             * if the paragraph ends here anyway there is nothing to wrap. */
            uint32_t q = p;
            const int nx = readGlyph(&q);
            if (nx == G_EOF)   { next = p; break; }
            if (nx == G_BREAK) { next = q; break; }
            if (nx == ' ' || nx == '\t') { next = skipSpacesFrom(p); break; }

            if (breakCol > 0) {                    /* break at the last space */
                col  = breakCol;
                next = skipSpacesFrom(breakAt);
            } else {
                next = p;                          /* word longer than a line */
            }
            break;
        }
    }

    while (col > 0 && out[col - 1] == ' ') col--;   /* trailing spaces */
    out[col] = 0;

    if (next <= start) next = start + 1;            /* never stall */
    if (next > tvSize) next = tvSize;
    return next;
}

/* --------------------------------------------------------------------- */
/* Scrolling                                                              */
/* --------------------------------------------------------------------- */
/* Start of the hard line containing byte p, i.e. the smallest s <= p with
 * s == 0 or file[s-1] == '\n'. Examines bytes [.., p-1]. */
static uint32_t hardLineStart(uint32_t p)
{
    if (p == 0) return 0;
    const uint32_t lo = (p > TV_SCAN_BACK) ? (p - TV_SCAN_BACK) : 0;
    uint32_t i = p;
    while (i > lo) {
        i--;
        if (byteAt(i) == '\n') return i + 1;
    }
    return lo;      /* exact when lo == 0; a bounded approximation otherwise */
}

static void scrollDown(void)
{
    if (tvTop >= tvSize) return;
    char line[TV_COLS + 1];
    const uint32_t next = wrapLine(tvTop, line);
    if (next < tvSize) tvTop = next;
}

static void scrollUp(void)
{
    if (tvTop == 0) return;

    uint32_t hs = hardLineStart(tvTop);
    if (hs == tvTop) hs = hardLineStart(tvTop - 1);   /* previous paragraph */

    char line[TV_COLS + 1];
    uint32_t cur = hs, prev = hs;
    while (cur < tvTop) {
        prev = cur;
        const uint32_t next = wrapLine(cur, line);
        if (next <= cur) break;
        cur = next;
    }
    tvTop = prev;
}

static void pageDown(void) { for (int i = 0; i < VISIBLE - 1; i++) scrollDown(); }
static void pageUp(void)   { for (int i = 0; i < VISIBLE - 1; i++) scrollUp();   }

/* --------------------------------------------------------------------- */
/* Drawing                                                                */
/* --------------------------------------------------------------------- */
static void tvDraw(const char *name)
{
    gfx_clear(C_BG);

    gfx_fillRect(0, 0, GFX_W, HDR_H, C_HDR_BG);
    gfx_text(2, 2, name, C_HDR_TEXT);

    char line[TV_COLS + 1];
    uint32_t off = tvTop;
    for (int i = 0; i < VISIBLE; i++) {
        if (off >= tvSize) break;
        const uint32_t next = wrapLine(off, line);
        gfx_text(2, LIST_TOP + i * ROW_H + 2, line, C_TEXT);
        if (next <= off) break;
        off = next;
    }

    gfx_fillRect(0, STATUS_Y, GFX_W, GFX_H - STATUS_Y, C_HDR_BG);

    char pct[8];
    fmtU32(tvSize ? (uint32_t)((uint64_t)tvTop * 100u / tvSize) : 100u, pct);
    const uint8_t n = (uint8_t)strlen(pct);
    pct[n] = '%'; pct[n + 1] = 0;
    gfx_text(3, STATUS_Y + 3, pct, C_HDR_TEXT);

    gfx_text(GFX_W - 3 - 13 * 6, STATUS_Y + 3, "B back  A pg", C_DIM);

    gfx_flush();
}

/* --------------------------------------------------------------------- */
/* Entry point                                                            */
/* --------------------------------------------------------------------- */
bool textView(const char *path)
{
    sdBegin();
    tvFile = SD.open(path);
    sdEnd();
    if (!tvFile) return false;

    sdBegin();
    tvSize = tvFile.size();
    sdEnd();

    tvTop     = 0;
    tvBufBase = 0;
    tvBufLen  = 0;

    tvDraw(path);

    /* A opened this file and is still held. Wait for the pad to come up
     * before listening, so the press that got here cannot also page down. */
    while (btnAnyDown()) { }
    btnResetAll();

    bool dirty = false;
    for (;;) {
        if (btnPressed(bB, false)) break;

        if (btnPressed(bUp,    true)) { scrollUp();   dirty = true; }
        if (btnPressed(bDown,  true)) { scrollDown(); dirty = true; }
        if (btnPressed(bLeft,  true)) { pageUp();     dirty = true; }
        if (btnPressed(bRight, true)) { pageDown();   dirty = true; }
        if (btnPressed(bA,     true)) { pageDown();   dirty = true; }
        if (btnPressed(bStart, false)) { tvTop = 0;   dirty = true; }

        if (dirty) { tvDraw(path); dirty = false; }
        delay(5);
    }

    sdBegin();
    tvFile.close();
    sdEnd();
    return true;
}
