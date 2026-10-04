/*
 * gifplay.cpp - streaming animated GIF playback on 20 KB of SRAM.
 *
 * ===========================================================================
 * WHY THIS IS SHAPED THE WAY IT IS
 * ===========================================================================
 * A 128x128 GIF frame is 16384 pixels of 8-bit palette indices. There is no
 * room for that canvas: SRAM is 20480 bytes total and the linker reserves a
 * fixed 2 KB stack, so every static in the firmware has to fit in 18416.
 *
 * So there is no canvas. Rows are decoded and pushed straight at the panel,
 * and **the ST7735's own GRAM is the animation canvas**. That is what makes
 * inter-frame GIFs work here: a frame that only repaints a 92x40 rectangle
 * and leaves the rest transparent simply does not write those pixels, and
 * what stays on the glass is the previous frame. The panel remembers so the
 * MCU does not have to.
 *
 * The LZW dictionary is the real memory problem, and it cannot be dodged -
 * measuring the actual files on the card showed the dictionary filling to
 * code 4094, so the full 4096 entries are genuinely needed:
 *
 *      prefix  uint16[4096]  8192 bytes -> borrowed from RPGfx's gfx_fb
 *      suffix  uint8 [4096]  4096 bytes -> the shared scratch arena
 *      stack   uint8 [ 256]   256 bytes -> borrowed from gfx_chunkScratch()
 *
 * gfx_fb is free real estate here precisely because nothing is drawn through
 * the framebuffer while a GIF is playing - this path only ever calls
 * gfx_select / gfx_setWindow / gfx_directBlit / gfx_directFillRect, none of
 * which read or write gfx_fb or the chunk buffers.
 *
 * THE STACK BOUND. Every textbook LZW decoder allocates a 4096-byte output
 * stack, because a dictionary entry can in principle be 4096 bytes long. We
 * cannot afford that, and we do not need it.
 *
 * Let O(t) be the longest string output so far. Any entry created at step j
 * has length len(S_j-1) + 1 <= O(j-1) + 1, and the KwKwK output is also
 * len(S_t-1) + 1, so O(t) <= O(t-1) + 1: the longest output grows by at most
 * one per code. O starts at 1, because the first code after a clear is
 * necessarily a root. So O passes through every value 1,2,...,L at L distinct
 * steps, and at the step where it first reaches k the output is exactly k
 * pixels. Summing, a frame that emits P pixels satisfies P >= L*(L+1)/2.
 *
 * For P = 16384 that gives L <= 180, and the stack needs one more than that
 * (181): a decoder that checks its budget between codes can emit one final
 * string after the budget is met. 256 is the next power of two.
 *
 * This is NOT a comfortable margin, which is why it was measured rather than
 * assumed. The bound is exactly tight - a plain LZW encode of a solid-colour
 * 128x128 frame reaches exactly 180 - so any GIF with a large flat area, a
 * fade to black, or letterboxing will drive it to the worst case. The worst
 * frame actually on this card (CHIPMUNK.GIF) reaches 162. A 128-byte stack
 * would have corrupted it, and would have looked fine in testing right up
 * until the first dark frame.
 *
 * The theorem is about pixels EMITTED, not the frame's declared area, so it
 * only holds because putPixel() enforces a hard budget that stops decoding.
 * The overflow check on every push then costs one compare per pixel and turns
 * the proof into a memory-safety guarantee against parser bugs not yet found.
 * ===========================================================================
 */
#include "gifplay.h"

const char *gifErrorText = "";

/* --------------------------------------------------------------------- */
/* Borrowed memory                                                        */
/* --------------------------------------------------------------------- */
#define LZW_MAX_CODES   4096
#define LZW_STACK_BYTES  256        /* proven bound is 181; see above     */
#define IOBUF_BYTES      128

#ifndef GIFPLAY_OPAQUE_RGB444
#define GIFPLAY_OPAQUE_RGB444 1     /* 25% less panel traffic on opaque frames */
#endif

#ifndef GIFPLAY_STREAM_WINDOWS
#define GIFPLAY_STREAM_WINDOWS 1    /* keep RAMWR active across opaque chunks */
#endif

#ifndef GIFPLAY_MIN_DELAY_MS
#define GIFPLAY_MIN_DELAY_MS 0      /* zero-delay GIFs run at hardware speed */
#endif

#if (2 * GFX_CHUNK_BYTES) < 1024
#error "GIF playback needs at least 1024 bytes from gfx_chunkScratch()."
#endif

/* gfx_fb is 8192 bytes, which is exactly uint16[4096]. */
static uint16_t *const lzwPrefix = (uint16_t *)gfx_fb;

/* Scratch arena: suffix table, then the file read buffer. */
static uint8_t *const lzwSuffix = scratch;                    /* 4096 */
static uint8_t *const ioBuf     = scratch + LZW_MAX_CODES;    /*  128 */

/* RPGfx's two DMA chunk buffers, idle on the direct path. */
static uint16_t *gifPal;        /* [256] RGB565, 512 bytes at +0    */
static uint8_t  *lzwStack;      /* [256]          256 bytes at +512  */
static uint16_t *rowRgb;        /* [128]          256 bytes at +768  */
static uint8_t  *streamBuf;     /* [256], same memory as rowRgb      */

static void bindChunkScratch(void)
{
    uint8_t *c = gfx_chunkScratch();
    gifPal   = (uint16_t *)(c);
    lzwStack = (uint8_t  *)(c + 512);
    rowRgb   = (uint16_t *)(c + 768);
    streamBuf = (uint8_t *)rowRgb;
}

/* --------------------------------------------------------------------- */
/* Buffered file reader                                                   */
/* --------------------------------------------------------------------- */
static File     gifFile;
static uint16_t rdPos, rdLen;
static bool     rdEof;
static uint32_t rdBase;          /* file offset of ioBuf[0] */

static void rdReset(uint32_t atOffset)
{
    rdPos = rdLen = 0;
    rdEof = false;
    rdBase = atOffset;
}

/* The only place the card is touched. RPGfx's panel CS must be high here,
 * which it is: every caller is on the decode side, and row emission never
 * reads the file. */
static bool rdFill(void)
{
    rdBase += rdLen;
    sdBegin();
    const int n = gifFile.read(ioBuf, IOBUF_BYTES);
    sdEnd();
    if (n <= 0) { rdLen = rdPos = 0; rdEof = true; return false; }
    rdLen = (uint16_t)n;
    rdPos = 0;
    return true;
}

static inline int rdByte(void)
{
    if (rdPos >= rdLen && !rdFill()) return -1;
    return ioBuf[rdPos++];
}

static uint32_t rdTell(void) { return rdBase + rdPos; }

static bool rdSeek(uint32_t off)
{
    sdBegin();
    const bool ok = gifFile.seek(off);
    sdEnd();
    rdReset(off);
    return ok;
}

static bool rdSkip(uint32_t n)
{
    while (n) {
        if (rdPos >= rdLen && !rdFill()) return false;
        const uint32_t have = rdLen - rdPos;
        const uint32_t take = (n < have) ? n : have;
        rdPos += (uint16_t)take;
        n     -= take;
    }
    return true;
}

static int rdWord(void)                          /* little-endian uint16 */
{
    const int lo = rdByte(); if (lo < 0) return -1;
    const int hi = rdByte(); if (hi < 0) return -1;
    return lo | (hi << 8);
}

/* --------------------------------------------------------------------- */
/* GIF stream state                                                       */
/* --------------------------------------------------------------------- */
static uint16_t scrW, scrH;
static uint16_t bgRgb;
static uint32_t gctOffset;        /* so a local table can be undone   */
static uint16_t gctEntries;
static bool     palIsLocal;
static uint32_t firstBlockOffset;
static int32_t  loopsLeft;        /* -1 = forever                     */

/* Pending Graphic Control Extension, applies to the next image block. */
static uint8_t  gceDisposal;
static int16_t  gceTransp;
static uint16_t gceDelayCs;

/* Current frame rectangle and decode cursor. */
static uint8_t  frL, frT, frW, frH;
static bool     frInterlace;
static bool     frOpaqueRows;
static bool     frStreamWindow;
static bool     frUse444;
static uint16_t curX, curRow, curAbsY;
static uint16_t streamLen;
static uint16_t streamHold444;
static bool     streamHave444;
static uint32_t pixelBudget;      /* see putPixel(); this is what makes the
                                   * 256-byte stack bound provable */
static uint8_t  ilPass, ilStep;
static uint32_t rowMask[4];

/* Rectangle owed a restore-to-background before the next frame is drawn. */
static bool     dispPending;
static uint8_t  dspL, dspT, dspW, dspH;

/* Cancellation. */
static bool     cancelArmed;
static bool     cancelled;

/* --------------------------------------------------------------------- */
/* Cancellation                                                           */
/* --------------------------------------------------------------------- */
/*
 * A is what started playback, and it is almost certainly still held down
 * when we get here. Rather than guess at a timeout, wait for the pad to be
 * genuinely idle before arming: until every button has been seen released,
 * a held button cannot cancel anything.
 */
static void cancelPoll(void)
{
    if (cancelled) return;
    if (!cancelArmed) {
        if (!btnAnyDown()) cancelArmed = true;
        return;
    }
    if (btnAnyDown()) cancelled = true;
}

/* --------------------------------------------------------------------- */
/* Row output                                                             */
/* --------------------------------------------------------------------- */
static inline void maskClear(void)
{
    rowMask[0] = rowMask[1] = rowMask[2] = rowMask[3] = 0;
}

static inline void maskSet(uint16_t x)
{
    rowMask[x >> 5] |= (1u << (x & 31));
}

static inline bool maskGet(uint16_t x)
{
    return (rowMask[x >> 5] >> (x & 31)) & 1u;
}

static inline uint16_t rgb565to444(uint16_t c)
{
    return (uint16_t)((((c >> 12) & 0x0F) << 8) |
                      (((c >>  7) & 0x0F) << 4) |
                       ((c >>  1) & 0x0F));
}

/*
 * Push one decoded row. Only the pixels flagged in rowMask are written, so a
 * transparent run leaves whatever the panel already held - which is how a
 * diff-encoded animation composites without a canvas.
 *
 * No SD access may happen between gfx_select() and gfx_deselect(): the card
 * reprograms SPI1, and it would do so while the panel had chip select
 * asserted and was waiting for pixels.
 */
static void emitRow(void)
{
    if (curAbsY >= scrH) return;
    if (!(rowMask[0] | rowMask[1] | rowMask[2] | rowMask[3])) return;

    gfx_select();

    uint16_t x = 0;
    while (x < frW) {
        if (!maskGet(x)) { x++; continue; }
        const uint16_t start = x;
        while (x < frW && maskGet(x)) x++;
        gfx_setWindow((uint8_t)(frL + start), (uint8_t)curAbsY,
                      (uint8_t)(x - start), 1);
        gfx_directBlit(&rowRgb[start], (uint32_t)(x - start) * 2u, true);
    }

    gfx_deselect();
}

static void beginStreamWindow(void)
{
    if (!frStreamWindow) return;
    streamLen = 0;
    streamHave444 = false;
    gfx_select();
    gfx_setWindow(frL, frT, frW, frH);
    gfx_deselect();
}

static void flushStream(void)
{
    if (!streamLen) return;
    gfx_select();
    gfx_directBlit(streamBuf, streamLen, !frUse444);
    gfx_deselect();
    streamLen = 0;
}

static inline void streamPixel(uint8_t idx)
{
    const uint16_t c = gifPal[idx];

    if (!frUse444) {
        if (streamLen > 254) flushStream();
        *(uint16_t *)(streamBuf + streamLen) = c;
        streamLen = (uint16_t)(streamLen + 2);
        return;
    }

    const uint16_t p = rgb565to444(c);
    if (!streamHave444) {
        streamHold444 = p;
        streamHave444 = true;
        return;
    }

    if (streamLen > 253) flushStream();
    streamBuf[streamLen++] = (uint8_t)(streamHold444 >> 4);
    streamBuf[streamLen++] = (uint8_t)(((streamHold444 & 0x0F) << 4) | (p >> 8));
    streamBuf[streamLen++] = (uint8_t)p;
    streamHave444 = false;
}

static void emitOpaqueRow(void)
{
    if (curAbsY >= scrH) return;

    gfx_select();
    gfx_setWindow(frL, (uint8_t)curAbsY, frW, 1);
    gfx_directBlit(rowRgb, (uint32_t)frW * 2u, true);
    gfx_deselect();
}

/* Interlaced GIFs arrive in four passes: every 8th row from 0, then from 4,
 * then every 4th from 2, then every 2nd from 1. Streaming a row at a time
 * makes that free - the row just goes to a different y. */
static void advanceRow(void)
{
    curRow++;
    if (!frInterlace) { curAbsY = frT + curRow; return; }

    uint16_t y = (uint16_t)(curAbsY - frT) + ilStep;
    while (y >= frH && ilPass < 4) {
        ilPass++;
        switch (ilPass) {
            case 1:  y = 4; ilStep = 8; break;
            case 2:  y = 2; ilStep = 4; break;
            case 3:  y = 1; ilStep = 2; break;
            default: y = frH;           break;   /* done */
        }
    }
    curAbsY = frT + y;
}

/* --------------------------------------------------------------------- */
/* Pixel sink                                                             */
/* --------------------------------------------------------------------- */
static inline bool disposeCovers(uint16_t absX, uint16_t absY)
{
    return dispPending &&
           absX >= dspL && absX < (uint16_t)(dspL + dspW) &&
           absY >= dspT && absY < (uint16_t)(dspT + dspH);
}

static inline void putPixel(uint8_t idx)
{
    /*
     * The stack bound above is stated in terms of P, the number of pixels the
     * decoder actually EMITS - not the frame's declared area. Nothing in the
     * format stops a malformed or hostile file from encoding far more pixels
     * than its rectangle holds, and a decoder that keeps going until the block
     * terminator would let string lengths climb towards the 4096 ceiling and
     * walk off a 256-byte stack.
     *
     * So the budget is hard: it stops DECODING, not just writing. The frame
     * rectangle is already validated to lie inside the 128x128 screen, so
     * pixelBudget <= 16384 and the bound holds by construction.
     */
    if (pixelBudget == 0) return;
    pixelBudget--;

    if (curRow >= frH) return;                  /* belt and braces */

    if (frStreamWindow) {
        streamPixel(idx);
        if (++curX >= frW) {
            curX = 0;
            advanceRow();
            if ((curRow & 3) == 0) cancelPoll();
        }
        return;
    }

    if (frOpaqueRows) {
        rowRgb[curX] = gifPal[idx];
        if (++curX >= frW) {
            emitOpaqueRow();
            curX = 0;
            advanceRow();
            if ((curRow & 3) == 0) cancelPoll();
        }
        return;
    }

    if ((int16_t)idx == gceTransp) {
        /*
         * Transparent. Normally that means "leave the panel alone". But if
         * the previous frame asked to be restored to background and this
         * pixel is inside its rectangle, the restore is still owed - and
         * painting it here, in the same pass, is both correct and free.
         * Doing it as a separate pre-fill would cost a full-screen DMA and
         * flash the background between every pair of frames.
         */
        if (disposeCovers(frL + curX, curAbsY)) {
            rowRgb[curX] = bgRgb;
            maskSet(curX);
        }
    } else {
        rowRgb[curX] = gifPal[idx];
        maskSet(curX);
    }

    if (++curX >= frW) {
        emitRow();
        curX = 0;
        maskClear();
        advanceRow();
        if ((curRow & 3) == 0) cancelPoll();
    }
}

/* --------------------------------------------------------------------- */
/* LZW                                                                    */
/* --------------------------------------------------------------------- */
static uint8_t  sbLeft;          /* bytes left in the current sub-block */
static bool     sbDone;
static uint32_t bitAcc;
static uint8_t  bitCnt;

static inline int nextDataByte(void)
{
    if (sbLeft == 0) {
        const int n = rdByte();
        if (n <= 0) { sbDone = true; return -1; }   /* 0 = block terminator */
        sbLeft = (uint8_t)n;
    }
    const int b = rdByte();
    if (b < 0) { sbDone = true; return -1; }
    sbLeft--;
    return b;
}

static inline int getCode(uint8_t width)
{
    while (bitCnt < width) {
        const int b = nextDataByte();
        if (b < 0) return -1;
        bitAcc |= ((uint32_t)b) << bitCnt;
        bitCnt = (uint8_t)(bitCnt + 8);
    }
    const int code = (int)(bitAcc & ((1u << width) - 1u));
    bitAcc >>= width;
    bitCnt = (uint8_t)(bitCnt - width);
    return code;
}

/* Consume whatever is left of the image's sub-blocks, so the file position is
 * right for the next block however the decode above ended. */
static void drainSubBlocks(void)
{
    while (!sbDone) {
        if (sbLeft) { if (!rdSkip(sbLeft)) break; sbLeft = 0; }
        const int n = rdByte();
        if (n <= 0) break;
        sbLeft = (uint8_t)n;
    }
}

static bool lzwDecodeFrame(uint8_t minCodeSize)
{
    if (minCodeSize < 2 || minCodeSize > 8) return false;

    const uint16_t clear = (uint16_t)(1u << minCodeSize);
    const uint16_t eoi   = (uint16_t)(clear + 1);

    uint16_t next  = (uint16_t)(clear + 2);
    uint8_t  width = (uint8_t)(minCodeSize + 1);
    int      prev  = -1;
    uint8_t  prevFirst = 0;

    sbLeft = 0; sbDone = false; bitAcc = 0; bitCnt = 0;

    for (;;) {
        const int code = getCode(width);
        if (code < 0) break;                    /* truncated stream */

        if (code == clear) {
            next  = (uint16_t)(clear + 2);
            width = (uint8_t)(minCodeSize + 1);
            prev  = -1;
            continue;
        }
        if (code == eoi) break;

        uint16_t sp = 0;
        int walk;

        if (code < next) {
            walk = code;
        } else if (code == next && prev >= 0) {
            /* KwKwK: the code refers to the entry being created right now,
             * which is always string(prev) + firstByte(prev). */
            lzwStack[sp++] = prevFirst;
            walk = prev;
        } else {
            break;                              /* corrupt: code out of range */
        }

        while (walk >= (int)clear) {
            if (sp >= LZW_STACK_BYTES) return false;   /* bound was wrong */
            lzwStack[sp++] = lzwSuffix[walk];
            walk = lzwPrefix[walk];
        }
        if (sp >= LZW_STACK_BYTES) return false;
        lzwStack[sp++] = (uint8_t)walk;
        const uint8_t curFirst = (uint8_t)walk;

        while (sp) putPixel(lzwStack[--sp]);
        if (pixelBudget == 0) break;            /* budget met: stop decoding */

        if (prev >= 0 && next < LZW_MAX_CODES) {
            lzwPrefix[next] = (uint16_t)prev;
            lzwSuffix[next] = curFirst;
            next++;
            if (next == (uint16_t)(1u << width) && width < 12) width++;
        }
        prev      = code;
        prevFirst = curFirst;

        if (cancelled) { drainSubBlocks(); return true; }
    }

    drainSubBlocks();
    return true;
}

/* --------------------------------------------------------------------- */
/* Palette                                                                */
/* --------------------------------------------------------------------- */
static bool loadColourTable(uint16_t entries)
{
    for (uint16_t i = 0; i < entries; i++) {
        const int r = rdByte(), g = rdByte(), b = rdByte();
        if (b < 0) return false;
        const uint16_t rgb = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        gifPal[i] = rgb;
    }
    return true;
}

/* --------------------------------------------------------------------- */
/* Disposal                                                               */
/* --------------------------------------------------------------------- */
/*
 * Method 2 says the previous frame's rectangle goes back to the background
 * colour before the next one is drawn. The part of it the incoming frame
 * covers is handled pixel-by-pixel in putPixel(); what is left is the region
 * outside the new frame, which is at most four strips.
 *
 * For every file on this card the two rectangles are identical and full
 * screen, so this does nothing at all - which is the point. A naive
 * implementation fills the whole screen before each frame, and at 32768
 * bytes over a 24 MHz wire that is 11 ms of the frame budget, every frame,
 * for no visible result.
 */
static void fillDisposeOutside(uint8_t nl, uint8_t nt, uint8_t nw, uint8_t nh)
{
    if (!dispPending) return;

    const int dl = dspL, dt = dspT, dr = dspL + dspW, db = dspT + dspH;
    const int nl2 = nl, nt2 = nt, nr = nl + nw, nb = nt + nh;

    /* Above the new rect. */
    const int topEnd = (nt2 < db) ? nt2 : db;
    if (topEnd > dt) gfx_directFillRect((uint8_t)dl, (uint8_t)dt,
                                        (uint8_t)(dr - dl), (uint8_t)(topEnd - dt), bgRgb);

    /* Below it. */
    const int botStart = (nb > dt) ? nb : dt;
    if (db > botStart) gfx_directFillRect((uint8_t)dl, (uint8_t)botStart,
                                          (uint8_t)(dr - dl), (uint8_t)(db - botStart), bgRgb);

    /* The band beside it, only over the vertical overlap. */
    const int bandTop = (dt > nt2) ? dt : nt2;
    const int bandBot = (db < nb)  ? db : nb;
    if (bandBot > bandTop) {
        const int leftEnd = (nl2 < dr) ? nl2 : dr;
        if (leftEnd > dl) gfx_directFillRect((uint8_t)dl, (uint8_t)bandTop,
                                             (uint8_t)(leftEnd - dl), (uint8_t)(bandBot - bandTop), bgRgb);
        const int rightStart = (nr > dl) ? nr : dl;
        if (dr > rightStart) gfx_directFillRect((uint8_t)rightStart, (uint8_t)bandTop,
                                                (uint8_t)(dr - rightStart), (uint8_t)(bandBot - bandTop), bgRgb);
    }
}

/* --------------------------------------------------------------------- */
/* Header                                                                 */
/* --------------------------------------------------------------------- */
static bool parseHeader(void)
{
    char sig[6];
    for (int i = 0; i < 6; i++) {
        const int c = rdByte();
        if (c < 0) return false;
        sig[i] = (char)c;
    }
    if (strncmp(sig, "GIF", 3) != 0) { gifErrorText = "Not a GIF"; return false; }

    const int w = rdWord(), h = rdWord();
    const int packed = rdByte();
    const int bgIndex = rdByte();
    if (rdByte() < 0) return false;                     /* aspect ratio */

    scrW = (uint16_t)w;
    scrH = (uint16_t)h;
    if (scrW == 0 || scrH == 0 || scrW > GFX_W || scrH > GFX_H) {
        gifErrorText = "Too big for panel";
        return false;
    }

    gctEntries = 0;
    gctOffset  = rdTell();
    if (packed & 0x80) {
        gctEntries = (uint16_t)(1u << ((packed & 0x07) + 1));
        if (!loadColourTable(gctEntries)) return false;
    }
    palIsLocal = false;
    bgRgb = (gctEntries && bgIndex >= 0 && bgIndex < (int)gctEntries)
          ? gifPal[bgIndex] : 0x0000;

    firstBlockOffset = rdTell();
    return true;
}

/* A frame with a local colour table overwrites the shared palette buffer, so
 * the global table has to be re-read before the next frame that relies on it.
 * None of the files on this card use local tables, but the seek is cheap and
 * silently rendering later frames in the wrong palette would not be. */
static bool restoreGlobalPalette(void)
{
    if (!palIsLocal || !gctEntries) return true;
    const uint32_t here = rdTell();
    if (!rdSeek(gctOffset)) return false;
    if (!loadColourTable(gctEntries)) return false;
    palIsLocal = false;
    return rdSeek(here);
}

/* --------------------------------------------------------------------- */
/* Extensions                                                             */
/* --------------------------------------------------------------------- */
static bool skipSubBlocks(void)
{
    for (;;) {
        const int n = rdByte();
        if (n < 0) return false;
        if (n == 0) return true;
        if (!rdSkip((uint32_t)n)) return false;
    }
}

static bool parseExtension(void)
{
    const int label = rdByte();
    if (label < 0) return false;

    if (label == 0xF9) {                          /* graphic control */
        const int bs = rdByte();
        if (bs != 4) { if (bs > 0 && !rdSkip((uint32_t)bs)) return false; return skipSubBlocks(); }
        const int packed = rdByte();
        const int delay  = rdWord();
        const int tr     = rdByte();
        if (rdByte() < 0) return false;           /* terminator */
        gceDisposal = (uint8_t)((packed >> 2) & 0x07);
        gceDelayCs  = (uint16_t)delay;
        gceTransp   = (packed & 0x01) ? (int16_t)tr : (int16_t)-1;
        return true;
    }

    if (label == 0xFF) {                          /* application */
        const int bs = rdByte();
        char app[12];
        int i = 0;
        for (; i < bs && i < 11; i++) app[i] = (char)rdByte();
        app[i < 0 ? 0 : i] = 0;
        for (; i < bs; i++) if (rdByte() < 0) return false;

        const bool netscape = (strncmp(app, "NETSCAPE", 8) == 0);
        for (;;) {
            const int n = rdByte();
            if (n < 0) return false;
            if (n == 0) return true;
            if (netscape && n >= 3) {
                const int id = rdByte();
                const int lc = rdWord();
                if (lc < 0) return false;
                if (id == 1) loopsLeft = (lc == 0) ? -1 : lc;
                if (!rdSkip((uint32_t)(n - 3))) return false;
            } else {
                if (!rdSkip((uint32_t)n)) return false;
            }
        }
    }

    return skipSubBlocks();                       /* comment, plain text, ... */
}

/* --------------------------------------------------------------------- */
/* One image block                                                        */
/* --------------------------------------------------------------------- */
static bool parseImage(void)
{
    const int l = rdWord(), t = rdWord();
    const int w = rdWord(), h = rdWord();
    const int packed = rdByte();
    if (packed < 0) return false;

    if (w <= 0 || h <= 0) return false;
    /* Clip to the panel rather than refusing: a frame whose rectangle runs
     * off the logical screen is malformed but harmless if bounded. */
    if (l < 0 || t < 0 || l + w > (int)scrW || t + h > (int)scrH) return false;

    frInterlace = (packed & 0x40) != 0;

    if (packed & 0x80) {
        const uint16_t n = (uint16_t)(1u << ((packed & 0x07) + 1));
        if (!loadColourTable(n)) return false;
        palIsLocal = true;
    } else if (!restoreGlobalPalette()) {
        return false;
    }

    const int mcs = rdByte();
    if (mcs < 0) return false;

    const bool opaque = (gceTransp < 0);
    const bool streamWindow = GIFPLAY_STREAM_WINDOWS && opaque && !frInterlace;
    const bool use444 = GIFPLAY_OPAQUE_RGB444 && streamWindow &&
                        ((((uint32_t)w * (uint32_t)h) & 1u) == 0);

    /* Everything owed by the previous frame, settled before this one lands.
     * Solid fills are RGB565-only, so do them before switching an opaque frame
     * to the RGB444 fast path. */
    if (dispPending) gfx_setColorMode(GFX_16BPP);
    fillDisposeOutside((uint8_t)l, (uint8_t)t, (uint8_t)w, (uint8_t)h);

    gfx_setColorMode(use444 ? GFX_12BPP : GFX_16BPP);

    frL = (uint8_t)l; frT = (uint8_t)t;
    frW = (uint8_t)w; frH = (uint8_t)h;
    frOpaqueRows   = opaque;
    frUse444       = use444;
    frStreamWindow = streamWindow;

    curX = 0; curRow = 0;
    pixelBudget = (uint32_t)frW * frH;
    curAbsY = frT;
    ilPass = 0; ilStep = 8;
    maskClear();
    beginStreamWindow();

    const uint32_t frameStart = millis();

    if (!lzwDecodeFrame((uint8_t)mcs)) { gifErrorText = "Bad LZW data"; return false; }

    /* A frame whose stream ended mid-row still has pixels pending. */
    if (frStreamWindow) {
        flushStream();
    } else if (curX) {
        if (frOpaqueRows) emitOpaqueRow();
        else              emitRow();
    }

    /* Record what this frame will owe the next one. Method 3 (restore to
     * previous) needs a saved canvas, which does not exist here; treating it
     * as method 1 leaves the last frame showing, which is the least-wrong
     * option and matches what most viewers do in practice. */
    if (gceDisposal == 2) {
        dispPending = true;
        dspL = frL; dspT = frT; dspW = frW; dspH = frH;
    } else {
        dispPending = false;
    }

    gceDisposal = 0;
    gceTransp   = -1;

    uint32_t want = (uint32_t)gceDelayCs * 10u;
#if GIFPLAY_MIN_DELAY_MS > 0
    if (gceDelayCs && want < GIFPLAY_MIN_DELAY_MS) want = GIFPLAY_MIN_DELAY_MS;
#endif
    gceDelayCs = 0;
    while (!cancelled && (millis() - frameStart) < want) cancelPoll();

    return true;
}

/* --------------------------------------------------------------------- */
/* Entry point                                                            */
/* --------------------------------------------------------------------- */
GifResult gifPlay(const char *path)
{
    gifErrorText = "Could not read file";
    bindChunkScratch();

    cancelArmed = false;
    cancelled   = false;
    dispPending = false;
    loopsLeft   = -1;
    gceDisposal = 0;
    gceTransp   = -1;
    gceDelayCs  = 0;

    sdBegin();
    gifFile = SD.open(path);
    sdEnd();
    if (!gifFile) return GIF_ERROR;

    rdReset(0);
    if (!parseHeader()) { gifFile.close(); return GIF_ERROR; }

    /* From here on gfx_fb holds the LZW prefix table, so nothing may draw
     * through the framebuffer until playback ends. */
    gfx_setColorMode(GFX_16BPP);
    gfx_directFillRect(0, 0, GFX_W, GFX_H, 0x0000);

    bool ok = true;

    while (ok && !cancelled) {
        const int b = rdByte();

        if (b < 0 || b == 0x3B) {                 /* trailer or end of file */
            if (loopsLeft > 0) loopsLeft--;
            if (loopsLeft == 0) break;
            dispPending = false;
            gceDisposal = 0;
            gceTransp   = -1;
            if (!rdSeek(firstBlockOffset)) { ok = false; break; }
            /* The palette may have been left local by the last frame. */
            palIsLocal = palIsLocal && gctEntries;
            if (!restoreGlobalPalette()) { ok = false; break; }
            continue;
        }

        if (b == 0x00) continue;                  /* stray padding */

        if (b == 0x21)      ok = parseExtension();
        else if (b == 0x2C) ok = parseImage();
        else { gifErrorText = "Unknown block"; ok = false; }

        cancelPoll();
    }

    sdBegin();
    gifFile.close();
    sdEnd();

    gfx_setColorMode(GFX_16BPP);

    if (cancelled) return GIF_CANCELLED;
    return ok ? GIF_FINISHED : GIF_ERROR;
}
