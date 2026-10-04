/* RP2040/RP2350 ST7735S transport. Drawing/conversion engine derived from CHGfx.
 * Copyright (c) 2026 bateske. Original MIT/third-party notices in LICENSE.
 * SPI may be shared with RPGameSD or dedicated to the panel on core 0.
 */
#include "RPGfx_internal.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/regs/spi.h"
#include "pico/stdlib.h"

static spi_inst_t *const panelSpi = RPGAME_SPI_BUS ? spi1 : spi0;
static int s_dmaChannel = -1;
static uint32_t s_spiHz;
static void gfx_dma_irq();
#define CS_LOW() gpio_put(PIN_LCD_CS, 0)
#define CS_HIGH() gpio_put(PIN_LCD_CS, 1)
#define DC_CMD() gpio_put(PIN_LCD_DC, 0)
#define DC_DATA() gpio_put(PIN_LCD_DC, 1)
static inline void rstDriveHigh() { gpio_put(PIN_LCD_RST, 1); }

#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_NORON   0x13
#define ST_INVOFF  0x20
#define ST_INVON   0x21
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_RASET   0x2B
#define ST_RAMWR   0x2C
#define ST_MADCTL  0x36
#define ST_COLMOD  0x3A
#define ST_FRMCTR1 0xB1
#define ST_FRMCTR2 0xB2
#define ST_FRMCTR3 0xB3
#define ST_INVCTR  0xB4
#define ST_PWCTR1  0xC0
#define ST_PWCTR2  0xC1
#define ST_PWCTR3  0xC2
#define ST_PWCTR4  0xC3
#define ST_PWCTR5  0xC4
#define ST_VMCTR1  0xC5
#define ST_GMCTRP1 0xE0
#define ST_GMCTRN1 0xE1

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
uint8_t  gfx_fb[GFX_FB_BYTES] __attribute__((aligned(4)));   /* word stores everywhere */

static uint8_t  s_div      = GFX_DIV2;
static uint8_t  s_mode     = GFX_16BPP;
static uint8_t  s_madctl   = 0xC8;   /* INITR_144GREENTAB, rotation 0 */
static uint8_t  s_colStart = 2;
static uint8_t  s_rowStart = 3;

/* Expansion LUT: one framebuffer byte (2 px) -> packed output.
 *   16 bpp: entry = pal565[lo] | pal565[hi] << 16   (one uint32 store)
 *   12 bpp: entry = 3 packed RGB444 bytes in the low 24 bits          */
static uint32_t s_lut[256];

/* Ping-pong chunk buffers. GFX_CHUNK_BYTES is sized for the 16 bpp
 * worst case; 12 bpp uses 3/4 of it. */
static uint8_t s_chunk[2][GFX_CHUNK_BYTES] __attribute__((aligned(4)));

static volatile int8_t   s_sending = -1;   /* buffer index in flight, -1 idle   */
static volatile int8_t   s_ready = -1;     /* converted buffer waiting, -1 none */
static volatile uint32_t s_readyLen;  /* bytes in the waiting buffer       */
static bool              s_dmaHalf;

/* The hot loops - the converters and the DMA interrupt that drives them -
 * live in SRAM. See GFX_RAMFUNC in RPGfx_internal.h. */

/* ================================================================== */
/* Low-level SPI                                                       */
/* ================================================================== */

static GFX_INLINE void spiWait() {
    while (spi_is_busy(panelSpi)) tight_loop_contents();
    // LCD-only traffic can overrun RX; drain it before the SD card takes over.
    while (spi_is_readable(panelSpi)) (void)spi_get_hw(panelSpi)->dr;
    spi_get_hw(panelSpi)->icr = SPI_SSPICR_RORIC_BITS;
}
static GFX_INLINE void spiSet8() {
    if ((spi_get_hw(panelSpi)->cr0 & SPI_SSPCR0_DSS_BITS) != 7) {
        spiWait();
        spi_set_format(panelSpi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }
}
static GFX_INLINE void spiSet16() {
    if ((spi_get_hw(panelSpi)->cr0 & SPI_SSPCR0_DSS_BITS) != 15) {
        spiWait();
        spi_set_format(panelSpi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }
}
static inline void spiByte(uint8_t b) { spi_write_blocking(panelSpi, &b, 1); }

/* Chip select is exposed rather than wrapped around every command,
 * because the whole point of this driver is to assert it once and stream
 * a frame. The panel latches nothing while CS is high. */
void gfx_select(void)   { CS_LOW(); }
void gfx_deselect(void) { spiWait(); CS_HIGH(); }

void gfx_cmd(uint8_t c) {
    spiWait();
    spiSet8();
    DC_CMD();
    spiByte(c);
    spiWait();
    DC_DATA();
}

void gfx_data8(uint8_t d) {
    spiSet8();
    spiByte(d);
}

/* ================================================================== */
/* Bring-up                                                            */
/* ================================================================== */

static void outputHigh(uint pin) {
    gpio_init(pin);
    gpio_put(pin, 1);
    gpio_set_dir(pin, GPIO_OUT);
}
static void gpioInit() {
    gpio_set_function(RPGAME_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(RPGAME_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(RPGAME_SPI_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(RPGAME_SPI_MISO);
    outputHigh(PIN_LCD_CS);
    outputHigh(PIN_LCD_DC);
    outputHigh(PIN_LCD_RST);
#if RPGAME_SHARED_SPI
    outputHigh(PIN_SD_CS);
#endif
}
static uint32_t requestedHz(uint8_t div) {
    return RPGAME_LCD_MAX_HZ * 2u / (div >= 2 ? div : 2);
}
static void spiInit(uint8_t div) {
    s_spiHz = spi_init(panelSpi, requestedHz(div));
    spi_set_format(panelSpi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    s_div = div >= 2 ? div : 2;
}
static void dmaInit() {
    if (s_dmaChannel >= 0) return;
    s_dmaChannel = dma_claim_unused_channel(true);
    dma_hw->ints1 = 1u << s_dmaChannel;
    irq_add_shared_handler(DMA_IRQ_1, gfx_dma_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
}
static void panelReset() {
    rstDriveHigh(); delay(5);
    gpio_put(PIN_LCD_RST, 0); delay(20);
    rstDriveHigh(); delay(150);
}

/* FRMCTR values. The ST7735 frame rate is
 *   fps = 850kHz / ((RTNA*2+40) * (LINE + FPA + BPA))
 * The stock Adafruit table uses 0x01,0x2C,0x2D which is a lazy ~60 Hz.
 * 0x05,0x3A,0x3A scans the glass considerably faster, which shortens the
 * gap between a GRAM write landing and the pixel actually changing. */
static uint8_t s_frm[3] = { 0x05, 0x3A, 0x3A };

void gfx_setPanelFrameRate(uint8_t rtna, uint8_t fpa, uint8_t bpa) {
    s_frm[0] = rtna; s_frm[1] = fpa; s_frm[2] = bpa;
    gfx_wait();
    CS_LOW();
    gfx_cmd(ST_FRMCTR1);
    gfx_data8(rtna); gfx_data8(fpa); gfx_data8(bpa);
    spiWait();
    CS_HIGH();
}

static void panelInit(void) {
    CS_LOW();

    gfx_cmd(ST_SWRESET); spiWait(); delay(150);
    gfx_cmd(ST_SLPOUT);  spiWait(); delay(255);

    gfx_cmd(ST_FRMCTR1); gfx_data8(s_frm[0]); gfx_data8(s_frm[1]); gfx_data8(s_frm[2]);
    gfx_cmd(ST_FRMCTR2); gfx_data8(s_frm[0]); gfx_data8(s_frm[1]); gfx_data8(s_frm[2]);
    gfx_cmd(ST_FRMCTR3);
    gfx_data8(s_frm[0]); gfx_data8(s_frm[1]); gfx_data8(s_frm[2]);
    gfx_data8(s_frm[0]); gfx_data8(s_frm[1]); gfx_data8(s_frm[2]);

    gfx_cmd(ST_INVCTR);  gfx_data8(0x07);

    gfx_cmd(ST_PWCTR1);  gfx_data8(0xA2); gfx_data8(0x02); gfx_data8(0x84);
    gfx_cmd(ST_PWCTR2);  gfx_data8(0xC5);
    gfx_cmd(ST_PWCTR3);  gfx_data8(0x0A); gfx_data8(0x00);
    gfx_cmd(ST_PWCTR4);  gfx_data8(0x8A); gfx_data8(0x2A);
    gfx_cmd(ST_PWCTR5);  gfx_data8(0x8A); gfx_data8(0xEE);
    gfx_cmd(ST_VMCTR1);  gfx_data8(0x0E);

    gfx_cmd(ST_INVOFF);
    gfx_cmd(ST_MADCTL);  gfx_data8(s_madctl);

    /* Gamma. Purely cosmetic, but the panel looks washed out without it. */
    static const uint8_t gp[16] = { 0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,
                                    0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10 };
    static const uint8_t gn[16] = { 0x03,0x1d,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                                    0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10 };
    gfx_cmd(ST_GMCTRP1); for (uint8_t i = 0; i < 16; i++) gfx_data8(gp[i]);
    gfx_cmd(ST_GMCTRN1); for (uint8_t i = 0; i < 16; i++) gfx_data8(gn[i]);

    gfx_cmd(ST_NORON);  spiWait(); delay(10);
    gfx_cmd(ST_DISPON); spiWait(); delay(100);

    CS_HIGH();
}

/*
 * RGBSET (2Dh) - the colour-depth conversion LUT.
 *
 * The ST7735S datasheet (10.1.23) lists the default contents of this
 * table as "Random" after both power-on and hardware reset, and says
 * "128-Bytes must be written to the LUT regardless of the color mode".
 * Every ST7735 driver in the wild ignores that and 16 bpp works anyway,
 * so the silicon evidently ships a sane 5-6-5 default - but 12 bpp is
 * exercised by almost nobody, so we do not assume the 4-bit half of the
 * table is populated. Writing it costs 129 bytes once.
 *
 * Layout (datasheet 9.18.2, 128 parameters):
 *   params   1..32  RED,   indexed by the input red value
 *   params  33..96  GREEN
 *   params  97..128 BLUE
 * Each parameter is a 6-bit frame-memory value. In 4 bpp-input (12 bit)
 * mode only the first 16 entries of each colour are consulted; in 5-6-5
 * mode red/blue use 32 and green uses 64.
 */
static bool s_writeLut12 = true;

void gfx_writeColorLut(void) {
    CS_LOW();
    gfx_cmd(0x2D);
    if (s_mode == GFX_12BPP) {
        /* 4-bit input -> 6-bit output: replicate the top 2 bits down. */
        for (uint8_t i = 0; i < 32; i++)
            gfx_data8(i < 16 ? (uint8_t)((i << 2) | (i >> 2)) : 0x3F);   /* R */
        for (uint8_t i = 0; i < 64; i++)
            gfx_data8(i < 16 ? (uint8_t)((i << 2) | (i >> 2)) : 0x3F);   /* G */
        for (uint8_t i = 0; i < 32; i++)
            gfx_data8(i < 16 ? (uint8_t)((i << 2) | (i >> 2)) : 0x3F);   /* B */
    } else {
        for (uint8_t i = 0; i < 32; i++)
            gfx_data8((uint8_t)((i << 1) | (i >> 4)));                   /* R, 5-bit */
        for (uint8_t i = 0; i < 64; i++)
            gfx_data8(i);                                                /* G, 6-bit */
        for (uint8_t i = 0; i < 32; i++)
            gfx_data8((uint8_t)((i << 1) | (i >> 4)));                   /* B, 5-bit */
    }
    spiWait();
    CS_HIGH();
}

void gfx_setWriteColorLut(bool on) { s_writeLut12 = on; }

static void applyColorMode(void) {
    CS_LOW();
    gfx_cmd(ST_COLMOD);
    gfx_data8(s_mode == GFX_12BPP ? 0x03 : (s_mode == GFX_18BPP ? 0x06 : 0x05));
    spiWait();
    CS_HIGH();
    /* Only for 12 bpp: 16 bpp demonstrably works on the factory default
     * and there is no reason to poke a table that is already right. */
    if (s_mode == GFX_12BPP && s_writeLut12) gfx_writeColorLut();
}

void gfx_begin(uint8_t spiDiv, uint8_t colorMode) {
    gpioInit();
    spiInit(spiDiv);
    dmaInit();
    panelReset();
    s_mode = colorMode;
    panelInit();
    applyColorMode();

    /* A sane default 16-colour ramp so a fresh sketch shows something. */
    static const uint16_t defpal[16] = {
        0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F,
        0x8410, 0xC618, 0x7800, 0x03E0, 0x000F, 0x8400, 0x0410, 0x4208
    };
    gfx_setPalette(defpal, 16);

    s_sending = -1;
    s_ready   = -1;
}

void gfx_setSpiDiv(uint8_t div) {
    gfx_wait();
    spiWait();
    s_div = div >= 2 ? div : 2;
    s_spiHz = spi_set_baudrate(panelSpi, requestedHz(s_div));
}

void gfx_setColorMode(uint8_t mode) {
    if (mode == s_mode) return;
    gfx_wait();
    s_mode = mode;
    applyColorMode();
    gfx__paletteTouch();           /* LUT layout depends on the mode */
}

uint8_t  gfx_colorMode(void) { return s_mode; }
uint8_t  gfx_spiDiv(void)    { return s_div; }
uint32_t gfx_spiHz(void)     { return s_spiHz; }

uint32_t gfx_frameBytes(void) {
    if (s_mode == GFX_12BPP) return (GFX_W * GFX_H * 3u) / 2u;
    if (s_mode == GFX_18BPP) return  GFX_W * GFX_H * 3u;
    return GFX_W * GFX_H * 2u;
}

uint8_t gfx_bytesPerPixel(void) { return s_mode == GFX_18BPP ? 3 : 2; }

uint8_t *gfx_chunkScratch(void) { return &s_chunk[0][0]; }

void gfx_setPanelOffsets(uint8_t madctl, uint8_t colStart, uint8_t rowStart) {
    gfx_wait();
    s_madctl = madctl; s_colStart = colStart; s_rowStart = rowStart;
    CS_LOW();
    gfx_cmd(ST_MADCTL); gfx_data8(s_madctl); spiWait();
    CS_HIGH();
}

void gfx_setInverted(bool on) {
    gfx_wait();
    CS_LOW();
    gfx_cmd(on ? ST_INVON : ST_INVOFF);
    spiWait();
    CS_HIGH();
}

/* ================================================================== */
/* Expansion LUT                                                       */
/* ================================================================== */
/* The palette itself lives in RPGfx_palette.cpp. Changes there only mark
 * it dirty; the LUT is rebuilt here, at the start of the next flush, once
 * the previous flush has stopped reading it. */

static void buildLut(const uint16_t *pal) {
    if (s_mode == GFX_16BPP) {
        /* Entry holds two RGB565 pixels. A single 32-bit store writes
         * both; on this little-endian core the low halfword lands first,
         * which is the even-x pixel, which is what the panel wants. */
        for (uint32_t b = 0; b < 256; b++) {
            uint32_t lo = pal[b & 0x0F];
            uint32_t hi = pal[b >> 4];
            s_lut[b] = lo | (hi << 16);
        }
    } else if (s_mode == GFX_18BPP) {
        /* RGB666 is three bytes per PIXEL, so a 256-entry byte->2px
         * table would need six bytes an entry. Use a 16-entry palette
         * indexed by nibble instead and do two lookups per source byte;
         * the budget at 18 bpp is 48 cycles/px, so it costs nothing. */
        for (uint8_t i = 0; i < 16; i++) {
            uint16_t c = pal[i];
            uint32_t r = (c >> 11) & 0x1F;          /* 5 bits -> 6 */
            uint32_t g = (c >> 5)  & 0x3F;          /* already 6   */
            uint32_t b =  c        & 0x1F;
            r = (r << 1) | (r >> 4);
            b = (b << 1) | (b >> 4);
            /* Component in bits 7:2 of its byte, byte 0 in the LSB. */
            s_lut[i] = ((r << 2) & 0xFF) | (((g << 2) & 0xFF) << 8)
                                         | (((b << 2) & 0xFF) << 16);
        }
    } else {
        /* RGB444: two pixels pack into exactly three bytes.
         *   byte0 = R0G0        byte1 = B0R1        byte2 = G1B1
         * Store them in the low 24 bits, byte 0 in the LSB. */
        uint16_t p12[16];
        for (uint8_t i = 0; i < 16; i++) {
            uint16_t c = pal[i];
            uint8_t r = (c >> 12) & 0x0F;            /* RGB565 R[4:1] */
            uint8_t g = (c >> 7)  & 0x0F;            /* RGB565 G[5:2] */
            uint8_t bl = (c >> 1) & 0x0F;            /* RGB565 B[4:1] */
            p12[i] = (uint16_t)((r << 8) | (g << 4) | bl);
        }
        for (uint32_t b = 0; b < 256; b++) {
            uint32_t p0 = p12[b & 0x0F];
            uint32_t p1 = p12[b >> 4];
            uint32_t b0 = p0 >> 4;
            uint32_t b1 = ((p0 & 0x0F) << 4) | (p1 >> 8);
            uint32_t b2 = p1 & 0xFF;
            s_lut[b] = b0 | (b1 << 8) | (b2 << 16);
        }
    }
}

/* Rebuild the LUT if the palette, the fade or the colour mode changed.
 * Only ever called with no flush in flight. */
static void syncLut(void) {
    uint16_t out[16];
    if (gfx__paletteTake(out)) buildLut(out);
}

/* The single global instance behind the class API. */
RPGfx Gfx;

/* ================================================================== */
/* The conversion inner loops                                          */
/* ================================================================== */
/*
 * These run out of SRAM. Flash on the CH32X035 costs 3 wait states at
 * 48 MHz (RM 20.3.1), and while the prefetch buffer hides most of that
 * for straight-line code, a 4-instruction loop body pays for every
 * refill. The benchmark reports both variants so you can see the gap on
 * your own silicon rather than taking my word for it.
 */

/* 16 bpp: one LUT load + one word store per two pixels. */
static GFX_INLINE uint32_t conv565(uint8_t *dst, const uint8_t *src, uint32_t srcBytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *lut = s_lut;
    while (srcBytes >= 4) {
        d[0] = lut[src[0]];
        d[1] = lut[src[1]];
        d[2] = lut[src[2]];
        d[3] = lut[src[3]];
        d += 4; src += 4; srcBytes -= 4;
    }
    while (srcBytes--) *d++ = lut[*src++];
    return (uint32_t)((uint8_t *)d - dst);
}

/* 12 bpp: four source bytes (8 px) become twelve output bytes, which is
 * three naturally aligned word stores. Doing it in groups of four keeps
 * every store word-aligned, which matters because the QingKe core does
 * not guarantee cheap unaligned access. */
static GFX_INLINE uint32_t conv444(uint8_t *dst, const uint8_t *src, uint32_t srcBytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *lut = s_lut;
    uint32_t groups = srcBytes >> 2;
    while (groups--) {
        uint32_t e0 = lut[src[0]];
        uint32_t e1 = lut[src[1]];
        uint32_t e2 = lut[src[2]];
        uint32_t e3 = lut[src[3]];
        src += 4;
        d[0] = e0 | (e1 << 24);
        d[1] = (e1 >> 8) | (e2 << 16);
        d[2] = (e2 >> 16) | (e3 << 8);
        d += 3;
    }
    return (uint32_t)((uint8_t *)d - dst);
}

/* 18 bpp: two source bytes (4 px) become twelve output bytes, i.e.
 * three aligned word stores - the same packing shuffle as conv444, just
 * fed one pixel per nibble instead of one pixel per half-nibble-pair. */
static GFX_INLINE uint32_t conv666(uint8_t *dst, const uint8_t *src, uint32_t srcBytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *pal = s_lut;          /* only [0..15] used here */
    uint32_t pairs = srcBytes >> 1;
    while (pairs--) {
        uint32_t s0 = src[0], s1 = src[1];
        src += 2;
        uint32_t e0 = pal[s0 & 0x0F];
        uint32_t e1 = pal[s0 >> 4];
        uint32_t e2 = pal[s1 & 0x0F];
        uint32_t e3 = pal[s1 >> 4];
        d[0] = e0 | (e1 << 24);
        d[1] = (e1 >> 8) | (e2 << 16);
        d[2] = (e2 >> 16) | (e3 << 8);
        d += 3;
    }
    return (uint32_t)((uint8_t *)d - dst);
}

/* 18 bpp from the framebuffer is rare, and its budget is 48 cycles a
 * pixel against 32, so its converter stays in flash and leaves the SRAM
 * to the two that matter. */
__attribute__((noinline)) static uint32_t convSpan666(uint8_t *dst, const uint8_t *src, uint32_t srcBytes)
{
    return conv666(dst, src, srcBytes);
}

/* Arbitrary run of framebuffer bytes -> panel format. This is the one
 * copy of the converters in SRAM: whole rows, partial rectangles (whose
 * rows are not contiguous in the framebuffer) and the ISR all come
 * through here. */
GFX_RAMFUNC(convspan) uint32_t gfx_convertSpan_ram(uint8_t *dst, const uint8_t *src, uint32_t srcBytes)
{
    if (s_mode == GFX_12BPP) return conv444(dst, src, srcBytes);
    if (s_mode == GFX_18BPP) return convSpan666(dst, src, srcBytes);
    return conv565(dst, src, srcBytes);
}

GFX_RAMFUNC(convrows) uint32_t gfx_convertRows_ram(uint8_t *dst, uint16_t row, uint16_t rows)
{
    return gfx_convertSpan_ram(dst, gfx_fb + (uint32_t)row * GFX_FB_STRIDE,
                               (uint32_t)rows * GFX_FB_STRIDE);
}

__attribute__((noinline))
uint32_t gfx_convertRows_flash(uint8_t *dst, uint16_t row, uint16_t rows)
{
    const uint8_t *src = gfx_fb + (uint32_t)row * GFX_FB_STRIDE;
    uint32_t n = (uint32_t)rows * GFX_FB_STRIDE;
    if (s_mode == GFX_12BPP) return conv444(dst, src, n);
    if (s_mode == GFX_18BPP) return conv666(dst, src, n);
    return conv565(dst, src, n);
}

/* ================================================================== */
/* DMA transmit                                                        */
/* ================================================================== */

/* Kick a transfer. `halfword` selects 16-bit SPI frames, which halves
 * the number of DMA bus cycles for 16 bpp data. */
static GFX_INLINE void dmaStart(const void *src, uint32_t bytes, bool halfword,
                                bool memInc, bool irq) {
    dma_channel_set_irq1_enabled(s_dmaChannel, false);
    dma_hw->ints1 = 1u << s_dmaChannel;
    if (halfword) spiSet16(); else spiSet8();
    dma_channel_config cfg = dma_channel_get_default_config(s_dmaChannel);
    channel_config_set_transfer_data_size(&cfg, halfword ? DMA_SIZE_16 : DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, memInc);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(panelSpi, true));
    channel_config_set_high_priority(&cfg, true);
    dma_channel_set_irq1_enabled(s_dmaChannel, irq);
    dma_channel_configure(s_dmaChannel, &cfg, &spi_get_hw(panelSpi)->dr, src,
                          halfword ? bytes / 2 : bytes, true);
}
static GFX_INLINE void dmaRearm(const uint8_t *src, uint32_t bytes) {
    dmaStart(src, bytes, s_dmaHalf, true, true);
}
static GFX_INLINE void dmaStopAndDrain() {
    dma_channel_wait_for_finish_blocking(s_dmaChannel);
    spiWait();
}
void gfx_directBlit(const void *data, uint32_t bytes, bool halfword) {
    if (!bytes) return;
    dmaStart(data, bytes, halfword, true, false);
    dmaStopAndDrain();
}
void gfx_blockingWrite(const uint8_t *data, uint32_t bytes) {
    spiSet8();
    spi_write_blocking(panelSpi, data, bytes);
    spiWait();
}

/* ================================================================== */
/* Window addressing                                                   */
/* ================================================================== */

void gfx_setWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    /* Every path that talks to the panel funnels through here, so this is
     * where RST gets re-asserted. Four instructions against the eleven SPI
     * frames below, and it means a stray pinMode() on another GPIOB pin
     * can leave RST floating for at most one draw call rather than until
     * the panel decides it has seen a reset. */
    rstDriveHigh();

    uint16_t x0 = x + s_colStart, x1 = x0 + w - 1;
    uint16_t y0 = y + s_rowStart, y1 = y0 + h - 1;

    gfx_cmd(ST_CASET);
    gfx_data8(x0 >> 8); gfx_data8(x0 & 0xFF);
    gfx_data8(x1 >> 8); gfx_data8(x1 & 0xFF);

    gfx_cmd(ST_RASET);
    gfx_data8(y0 >> 8); gfx_data8(y0 & 0xFF);
    gfx_data8(y1 >> 8); gfx_data8(y1 & 0xFF);

    gfx_cmd(ST_RAMWR);
    spiWait();
}

void gfx_directFillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t rgb565) {
    /* The whole point of this path: MINC off, so DMA re-reads the same
     * halfword for every pixel. No buffer, no CPU, full wire speed. */
    if (!w || !h) return;
    static volatile uint16_t colour;
    colour = rgb565;
    gfx_wait();
    CS_LOW();
    gfx_setWindow(x, y, w, h);
    dmaStart((const void *)&colour, (uint32_t)w * h * 2u, true, false, false);
    dmaStopAndDrain();
    CS_HIGH();
}

/* ================================================================== */
/* Frame presentation                                                  */
/* ================================================================== */
/*
 * One engine handles both the full frame and a sub-rectangle, because a
 * full frame IS the rectangle (0,0,128,128). Partial updates are the
 * single biggest lever left after DMA: the wire is the bottleneck, so
 * pushing a quarter of the screen takes a quarter of the time. A game
 * with a static background and a few moving sprites can run 4-8x the
 * full-frame rate by declaring what actually changed.
 *
 * Constraint: the source is 4 bpp, so a partial row has to start and end
 * on a byte. x and w are rounded outward to a multiple of 2 (16 bpp) or
 * 8 (12 bpp, where the converter works in groups of 8 pixels). Rounding
 * out is always safe - you send slightly more than you had to.
 */
namespace {
struct FlushJob {
    uint16_t x0;        /* left edge, already aligned            */
    uint16_t wpx;       /* width in pixels, already aligned      */
    uint16_t rowsPer;   /* rows per DMA chunk                    */
    uint16_t row;       /* next row to convert                   */
    uint16_t endRow;    /* one past the last row                 */
    uint32_t chunkLen;  /* bytes a full chunk produces           */
};
}  // namespace
static volatile FlushJob s_job;

/* Convert `rows` rows of the job's column span into dst. */
static GFX_RAMFUNC(convjob) uint32_t convertJobRows(uint8_t *dst, uint16_t row, uint16_t rows)
{
    uint16_t x0  = s_job.x0;
    uint16_t wpx = s_job.wpx;
    uint32_t srcBytesPerRow = wpx >> 1;

    if (x0 == 0 && wpx == GFX_W)                      /* contiguous fast path */
        return gfx_convertSpan_ram(dst, gfx_fb + (uint32_t)row * GFX_FB_STRIDE,
                                   (uint32_t)rows * GFX_FB_STRIDE);

    const uint8_t *src = gfx_fb + (uint32_t)row * GFX_FB_STRIDE + (x0 >> 1);
    uint8_t *d = dst;
    for (uint16_t r = 0; r < rows; r++) {
        d += gfx_convertSpan_ram(d, src, srcBytesPerRow);
        src += GFX_FB_STRIDE;
    }
    return (uint32_t)(d - dst);
}

static uint32_t setupJob(int x, int y, int w, int h)
{
    /* Wait FIRST. An async flush still in flight has the DMA ISR reading
     * s_job and the LUT every chunk; changing either underneath would
     * corrupt the transfer in progress. */
    gfx_wait();
    syncLut();

    /* Clip, then align outward. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return 0;

    /* 12 bpp packs 8 px into 12 bytes, 18 bpp packs 4 px into 12 bytes,
     * and 16 bpp only needs whole source bytes. */
    int align = (s_mode == GFX_12BPP) ? 8 : (s_mode == GFX_18BPP ? 4 : 2);
    int x1 = x + w;
    x  = x & ~(align - 1);
    x1 = (x1 + align - 1) & ~(align - 1);
    if (x1 > GFX_W) x1 = GFX_W;
    w = x1 - x;

    uint32_t bytesPerRow = (s_mode == GFX_12BPP) ? (uint32_t)w * 3u / 2u
                         : (s_mode == GFX_18BPP) ? (uint32_t)w * 3u
                                                 : (uint32_t)w * 2u;
    uint16_t rowsPer = (uint16_t)(GFX_CHUNK_BYTES / bytesPerRow);
    if (rowsPer == 0) rowsPer = 1;
    if (rowsPer > h)  rowsPer = (uint16_t)h;

    s_job.x0       = (uint16_t)x;
    s_job.wpx      = (uint16_t)w;
    s_job.rowsPer  = rowsPer;
    s_job.row      = (uint16_t)y;
    s_job.endRow   = (uint16_t)(y + h);
    s_job.chunkLen = bytesPerRow * rowsPer;

    CS_LOW();
    gfx_setWindow((uint8_t)x, (uint8_t)y, (uint8_t)w, (uint8_t)h);
    return 1;
}

/* How many rows the next chunk should carry (the last one may be short). */
static GFX_INLINE uint16_t rowsThisChunk(void) {
    uint16_t left = s_job.endRow - s_job.row;
    return left < s_job.rowsPer ? left : s_job.rowsPer;
}

void gfx_flushRect(int x, int y, int w, int h)
{
    if (!setupJob(x, y, w, h)) return;

    uint8_t cur = 0;
    uint16_t r = rowsThisChunk();
    uint32_t n = convertJobRows(s_chunk[cur], s_job.row, r);
    s_job.row += r;

    for (;;) {
        dmaStart(s_chunk[cur], n, s_mode == GFX_16BPP, true, false);
        if (s_job.row < s_job.endRow) {
            /* Convert the next chunk while this one is on the wire.
             * Conversion is ~10x faster than the transfer, so the SPI
             * bus never goes idle and the frame costs exactly its bytes. */
            uint8_t nxt = cur ^ 1;
            uint16_t r2 = rowsThisChunk();
            uint32_t n2 = convertJobRows(s_chunk[nxt], s_job.row, r2);
            s_job.row += r2;
            dmaStopAndDrain();
            cur = nxt; n = n2;
        } else {
            dmaStopAndDrain();
            break;
        }
    }
    CS_HIGH();
}

void gfx_flushRectAsync(int x, int y, int w, int h)
{
    if (!setupJob(x, y, w, h)) return;

    /* Both buffers are converted before the DMA is armed. Publishing
     * s_ready after starting the transfer would leave a window where the
     * completion ISR fires first and the frame stalls after one chunk. */
    uint16_t r = rowsThisChunk();
    uint32_t n = convertJobRows(s_chunk[0], s_job.row, r);
    s_job.row += r;

    if (s_job.row < s_job.endRow) {
        uint16_t r2 = rowsThisChunk();
        s_readyLen = convertJobRows(s_chunk[1], s_job.row, r2);
        s_job.row += r2;
        s_ready = 1;
    } else {
        s_ready = -1;
    }

    bool halfword = (s_mode == GFX_16BPP);
    s_dmaHalf = halfword;
    s_sending = 0;
    dmaStart(s_chunk[0], n, halfword, true, true);
}

/* ------------------------------------------------------------------ */
/* Direct streaming                                                    */
/* ------------------------------------------------------------------ */
/*
 * Same ping-pong as gfx_flushRect, with the framebuffer conversion
 * swapped for a caller-supplied generator. The generator fills the
 * chunk that is NOT currently being transmitted, so as long as it
 * stays inside 32 cycles/pixel (16 bpp) or 48 (18 bpp) the SPI bus
 * never goes idle and a full-colour procedural frame costs exactly its
 * bytes on the wire.
 */
void gfx_stream(gfx_streamFn fn, void *user)
{
    /* 12 bpp straddles byte boundaries between pixels, which is useless
     * to a per-pixel generator. Move to 16 bpp rather than hand the
     * callback a packing it cannot write. */
    if (s_mode == GFX_12BPP) gfx_setColorMode(GFX_16BPP);

    const uint32_t bpp         = (s_mode == GFX_18BPP) ? 3u : 2u;
    const uint32_t bytesPerRow = GFX_W * bpp;
    uint16_t rowsPer = (uint16_t)(GFX_CHUNK_BYTES / bytesPerRow);
    if (rowsPer == 0) rowsPer = 1;

    gfx_wait();
    CS_LOW();
    gfx_setWindow(0, 0, GFX_W, GFX_H);

    uint8_t  cur = 0;
    uint16_t y   = 0;
    uint16_t r   = (uint16_t)((GFX_H - y) < rowsPer ? (GFX_H - y) : rowsPer);
    fn(s_chunk[cur], y, r, user);
    y = (uint16_t)(y + r);
    uint32_t n = r * bytesPerRow;

    for (;;) {
        dmaStart(s_chunk[cur], n, s_mode == GFX_16BPP, true, false);
        if (y < GFX_H) {
            uint8_t  nxt = cur ^ 1;
            uint16_t r2  = (uint16_t)((GFX_H - y) < rowsPer ? (GFX_H - y) : rowsPer);
            fn(s_chunk[nxt], y, r2, user);      /* runs during the DMA */
            y = (uint16_t)(y + r2);
            dmaStopAndDrain();
            cur = nxt;
            n = r2 * bytesPerRow;
        } else {
            dmaStopAndDrain();
            break;
        }
    }
    CS_HIGH();
}

void gfx_flush(void)      { gfx_flushRect(0, 0, GFX_W, GFX_H); }
void gfx_flushAsync(void) { gfx_flushRectAsync(0, 0, GFX_W, GFX_H); }

bool gfx_busy(void) { return s_sending >= 0; }
void gfx_wait(void) { while (s_sending >= 0) { } }

/* Flush progress. The ISR advances s_job.row only after a chunk's rows
 * are fully converted, so every row above it has been read for the last
 * time and is free to draw into, even while the DMA is still sending it.
 * Once the last chunk is converted, every row is free. */
int gfx_flushRow(void) {
    if (s_sending < 0) return GFX_H;
    uint16_t row = s_job.row;
    return row >= s_job.endRow ? GFX_H : (int)row;
}

void gfx_waitRow(int y) {
    while (gfx_flushRow() < y) { }
}

// Shared IRQ handler only acknowledges this library's channel.
static GFX_RAMFUNC(dmaisr) void gfx_dma_irq() {
    if (!(dma_hw->ints1 & (1u << s_dmaChannel))) return;
    dma_hw->ints1 = 1u << s_dmaChannel;
    int8_t ready = s_ready;
    if (ready >= 0) {
        int8_t justDone = s_sending;
        uint32_t n = s_readyLen;
        s_sending = ready;
        s_ready = -1;
        dmaRearm(s_chunk[ready], n);
        if (s_job.row < s_job.endRow) {
            uint16_t r = rowsThisChunk();
            s_readyLen = convertJobRows(s_chunk[justDone], s_job.row, r);
            s_job.row += r;
            s_ready = justDone;
        }
    } else {
        dma_channel_set_irq1_enabled(s_dmaChannel, false);
        spiWait();
        CS_HIGH();
        s_sending = -1;
    }
}
