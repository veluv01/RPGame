# CHGfx library and platform performance: reference for building CHRoulette

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

## 0. Which copy and version to use

| Copy | Path | Version | Status |
|---|---|---|---|
| The one copy (the board package 0.3.0 brings it) | `platform/board/arduino/CHGame/libraries/CHGfx` | **1.3.0** | Has `CHGfx_extras.cpp`, `CHGfx_palette.cpp`, `CHGfx_textfx.cpp`, `CHGfx_internal.h`, `CHGfx_gfxfont.h`, `src/fonts/*`, `extras/sprite4.py`, `extras/fontconvert.py`, `FONTS.md`, and the `examples/GameKit` and `examples/Fonts` examples. |

- When this was written there was also a stale 1.1.0 project copy (no clip, sprite4, ellipse, palette staging or textFx) and the 1.3.0 installed in the sketchbook. Both are gone: `chgame build` passes the repository's copy with `--library`, and nothing is installed in the sketchbook.
- The simulator (the repository's `tools/chsim/chsim.py`) uses the same copy. It compiles every `src/*.cpp` except `CHGfx.cpp`, with `tools/chsim/host/chgfx_host.cpp` standing in for it.

All line references below are to CHGfx **1.3.0** (`I:` = `platform/board/arduino/CHGame/libraries/CHGfx/src/`), checked against it on 2026-10-02.

## 1. Board package, FQBN and toolchain

**Package:** board package 0.3.0 (`platform/board` in the repository). It contains one board, *CHGame Rev0* (`rev0`). Toolchain is `riscv-none-embed-gcc 8.2.0`; other tools are `chgame-upload` and `wchisp 0.3.0`.

**FQBN used by the games** (`tools/device.py`, `FQBN_RELEASE`):
```
arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly <Sketch>
arduino-cli upload  -b CHGame:ch32v:rev0 -p COMx <Sketch>
```

**Menus** (`boards.txt`):

- **opt**
  - `oslto` = `-Os -flto`, *Smallest + LTO*, the default since 0.3.0; CHChess only fits with it
  - `osstd` = `-Os` (the default in 0.2.4)
  - `o1std` = `-O1`; `o2std` = `-O2`; CHGfx's README recommends it for speed, since `-Os` costs 10–50% on primitives
  - `o3std` = `-O3`; about 8 KB more flash than `-O2` (`docs/performance.md`, "Build and run")
  - `ogstd`
- **rtlib**: `nano` (default), `nanofp`, `full`.
- **periph**
  - `game` (default): `-DUART_MODULE_ONLY -DTIM_MODULE_ONLY`, about 4 KB smaller. It removes Serial1, `tone()`, PWM and HardwareTimer.
  - `full`
- **usb**
  - `serial` (default): `-DUSE_CHGAME_USB_CDC`
  - `uploadonly`: `-DUSE_CHGAME_USB_BOOTONLY`, about 0.6–0.7 KB smaller. Using `Serial` in this mode is a compile error.

**Limits and flags:**

- `upload.maximum_size=50944`, `upload.maximum_data_size=18416`, `build.warn_data_percentage=95`.
- `platform.txt` `compiler.extra_flags`: `-march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fsigned-char -ffunction-sections -fdata-sections -fno-common`.
- Linker script `system/CH32X035/SRC/Ld/link_chgame_app.ld`:
  - FLASH starts at `0x3000`, length 50944 (the first 12 KB are the bootloader's).
  - RAM starts at `0x20000010`, length 20464.
  - `__stack_size = 2048`, fixed.
  - `*(.gnu.linkonce.r.*)` is placed **first** in `.data`, ahead of `.srodata*`. This is why RAM functions use that section name.
- **Save pages:** the last two 256 B flash pages are save slots, so an app that saves must end at **≤ 50,432 B** (the repository's `tools/check_size.py` prints "save pages free").
- **CHGfx `-D` macros must be passed as build properties, not `#define`d in the sketch**, because they compile into the library's own files. Example: `--build-property build.extra_flags=-DCHGFX_ISR_IN_SRAM` (CHGfx README, "Configuration").

## 2. Framebuffer model and fixed RAM cost

- **There is one full 128×128 framebuffer at 4 bpp (16 colours), 8,192 B.**
  - `extern uint8_t gfx_fb[GFX_FB_BYTES]` (`I:CHGfx.h:159`), defined `aligned(4)` at `CHGfx.cpp:168`.
  - `GFX_FB_STRIDE = 64`, `GFX_FB_BYTES = 8192` (`CHGfx.h:76-77`).
  - Packing: the **even x pixel is the LOW nibble**, odd x the high nibble.
  - Colours are palette indices 0–15, never RGB565.
- **No 8 bpp, no double buffer, no strips.**
  - RGB565 would be 32 KB.
  - 8 bpp (16 KB) was rejected (`docs/performance.md`, "Dead ends I checked so you don't have to").
  - A second 4 bpp buffer (8 KB) does not fit.
  - **Drawing must not touch the framebuffer while a flush is reading it.** Call `gfx_wait()` or `gfx_waitRow(y)` first.
- **Expansion LUT:** `static uint32_t s_lut[256]` = 1,024 B (`CHGfx.cpp:179`). Each entry turns one framebuffer byte (2 px) into one 32-bit store at 16 bpp, or 3 packed bytes at 12 bpp (`buildLut`, `CHGfx.cpp:500`).
- **Chunk ping-pong:** `s_chunk[2][GFX_CHUNK_BYTES]` = 2 × 512 B = 1,024 B (`CHGfx.cpp:183`). `GFX_CHUNK_ROWS = 2` (`CHGfx.h:91-94`).
  - This 1 KB is also `gfx_chunkScratch()` (`CHGfx.h:546-551`). It is only free between `gfx_wait()` and the next flush.
  - `gfx_sprite4Rot` and `gfx_textFx` clobber it, and both call `gfx_wait()` internally.
- **Palette:** `gfx_pal[16]` = 32 B (`CHGfx_palette.cpp:17`).
- **Totals:** CHGfx fixed RAM is about 10.3 KB before any code. `HelloGraphics` uses 11.9 KB of SRAM including the core; `GameKit` uses 13.9 KB (CHGfx README, "What it costs in SRAM").

### SRAM code sizes
Measured from CHBlackjack's release link map (oslto), at design time. Each function sits in its own `.gnu.linkonce.r.chgfx.<name>` section and is GC-able.

| Function | Size (B) |
|---|---:|
| convspan | 314 |
| charbuiltin | 264 |
| sprite4 | 238 |
| glyph1 | 214 |
| hline | 202 |
| shiftrow | 186 |
| ditherspan | 182 |
| copypixels | 156 |
| convjob | 110 |
| clearall | 56 |
| pixel | 52 |
| copywords | 26 |

`-DCHGFX_ISR_IN_SRAM` adds about 330 B of SRAM and saves 4–9% of flush CPU (`CHGfx.cpp`, the comment above `DMA1_Channel3_IRQHandler`).

### Real budgets from the shipped games
Measured with `riscv-none-embed-size` on the release ELFs.

| Game | .text | .data | Flash | SRAM static (.data + .bss) | SRAM free (of 18,416) |
|---|---:|---:|---:|---:|---:|
| CHBlackjack | 42,712 | 2,864 | **45,576** | 2,864 + 12,872 = **15,736** | **2,680 B** |
| CHChess | 44,940 | 3,688 | **48,628** | 3,688 + 14,184 = **17,872** | **544 B** |

Both also have the 2 KB stack. CHGfx costs about **8.7 KB flash** in Blackjack without LTO (CHBlackjack's `NOTES.md`, "Flash is the wall").

**Flash is the wall; SRAM is the second wall.** A roulette game sized like Blackjack has about 4.8 KB of flash (keeping save pages) and about 2.7 KB of SRAM to spend on new things.

## 3. Presenting a frame

| Call | Signature / location | Notes |
|---|---|---|
| `gfx_begin` | `(uint8_t spiDiv=GFX_DIV2, uint8_t colorMode=GFX_16BPP)`, `CHGfx.h:165` | **Both games call `gfx_begin(GFX_DIV2, GFX_12BPP)`** (in `CHBlackjack.ino` and `CHChess.ino`). |
| `gfx_flush` / `gfx_flushAsync` | `CHGfx.h:208-209` | The async version returns immediately; the DMA TC ISR converts the next 2-row chunk. |
| `gfx_busy` / `gfx_wait` | `CHGfx.h:210-211` | |
| `gfx_flushRect` / `gfx_flushRectAsync` | `(int x,int y,int w,int h)`, `CHGfx.h:309-310` | x/w are rounded **outward to a multiple of 8 at 12 bpp**, 2 at 16 bpp, 4 at 18 bpp (`CHGfx.cpp:837`). |
| `gfx_flushRow` / `gfx_waitRow` | `CHGfx.h:250-251` | "Racing the beam": rows above `flushRow()` are already converted and free to draw into. Measured 10–14% faster frames (CHGfx README, "Racing the beam"). |
| `gfx_stream` | `(gfx_streamFn fn, void *user)`, `typedef void (*gfx_streamFn)(uint8_t *dst,int y0,int rows,void*)`, `CHGfx.h:283-284` | Direct mode with no framebuffer; switches 12 bpp to 16 bpp. Budget is **32 cycles/px** at 16 bpp, 48 at 18 bpp. |
| `gfx_directFillRect` | `CHGfx.h:528` | DMA with `MINC=0`; 16 bpp only. |

**Palette changes are staged.** `syncLut()` (`CHGfx.cpp:551`) runs inside `setupJob()` (`CHGfx.cpp:826`) at the start of the next flush, after `gfx_wait()`. So `gfx_setPalette` and `gfx_setPaletteEntry` (`CHGfx.h:189-190`) are safe to call at any time, and each frame shows exactly one palette.

`gfx_setFade(uint8_t amount, uint16_t rgb565=0)` (`CHGfx.h:196`) fades toward the target while the LUT is built (`fadeOne`, `CHGfx_palette.cpp:45-55`). Linking it costs about 150 B; the games fade their RGB444 colours themselves (now in the CHGame library's `chgame/Palette.cpp`) (`CHChess/docs/CHGfx-notes.md`, "On CHGfx 1.3.0: text, banners and the palette").

**Animating the palette costs one LUT rebuild per frame and no drawing.** The rebuild is 16 fades plus 256 entries, estimated at about 0.05–0.1 ms (not measured).

**Panel:**

- The panel scan rate defaults to FRMCTR `{0x05,0x3A,0x3A}` (`CHGfx.cpp:322`).
- The TE pin is not broken out, so there is no vsync (`docs/performance.md`, "Dead ends I checked so you don't have to").
- The SPI clock is 24 MHz, which is out of the ST7735 spec (15.1 MHz); `GFX_DIV4` is the fallback.

### Measured transport (board, -O2, 24 MHz; `docs/performance.md`, "Measured results", and the CHGfx README)

| | 16 bpp | 12 bpp |
|---|---:|---:|
| Full-frame `gfx_flush` | 11.1 ms / 89 fps | **8.37 ms / 119 fps** |
| flushRect 96×96 | 6.32 ms | 4.78 ms |
| flushRect 64×64 | 2.82 ms | 2.2 ms |
| flushRect 32×32 | 0.75 ms | 0.64 ms |
| flushRect 16×16 | 0.29 ms | 0.20 ms |
| **CPU taken by async full flush (ISR conversion)** | 2.6–3.2 ms | **2.5–2.9 ms** |
| … half frame | 1.4–1.6 ms | 1.3–1.5 ms |
| Convert only, SRAM vs flash code | 1.52–1.62 vs 3.41–3.51 ms (**2.2× flash penalty**) | 1.58 vs 3.87 ms |

**Frame budget at 60 Hz with the games' loop** (`wait → draw → flushAsync → logic`): draw time + 8.37 ms ≤ 16.7 ms, so **draw ≤ about 8.3 ms per frame at 12 bpp**. The ISR's 2.5–2.9 ms comes out of the logic time during the flush.

For comparison, CHBlackjack's heaviest frame (a bust with shake and a banner) takes 11 ms to draw on 1.3 (CHBlackjack's `NOTES.md`).

## 4. Drawing API (1.3.0) and measured costs

All primitives clip to `gfx__clip` (`I:CHGfx_internal.h:43-44`; `gfx_setClip/resetClip/getClip`, `CHGfx.h:324-326`). `gfx_getPixel`, `gfx_scroll` and the flush ignore the clip.

| Function (`CHGfx.h` line) | Implementation | Measured cost |
|---|---|---|
| `gfx_clear(c)` :328 | `clearAll` in SRAM, 8 words per iteration | **93 µs** whole buffer |
| `gfx_pixel` :329 | SRAM (`CHGfx_draw.cpp:52`), clipped nibble RMW | |
| `gfx_hline` :331 | SRAM, ragged nibble ends, word stores in between | **4 µs** for 128 px |
| `gfx_vline` :332 | flash | |
| `gfx_fillRect` :333 | an hline per row | **515 µs** for 120×120 |
| `gfx_line` :335 | Bresenham calling `gfx_pixel` per pixel (`CHGfx_draw.cpp:173-189`) | **270 µs** for a 128 px diagonal (about 2.1 µs/px) |
| `gfx_circle` / `gfx_fillCircle` :336-337 | midpoint; fill uses hline | fillCircle r=30: **263 µs** |
| `gfx_blit(spr,x,y,w,h,transparent)` :343 | 4 bpp bitmap; opaque even-x copies bytes | 16×16 opaque **87 µs**, transparent **100 µs** |
| `gfx_roundRect` / `gfx_fillRoundRect(x,y,w,h,r,c)` :350-351 | corner insets via isqrt each call | CHChess kept its own 16 B inset table (−124 B flash) |
| **`gfx_ellipse` / `gfx_fillEllipse(cx,cy,rx,ry,c)`** :355-356 | `CHGfx_extras.cpp:99-158`; integer step-down, **axis-aligned only, rx/ry ≤ 255**, 2·ry+1 hlines | a sprite shadow: **23 µs** |
| `gfx_dither(x,y,w,h,c,phase)` :361 | `maskedSpan` in SRAM (182 B), word path | |
| `gfx_remapRect(x,y,w,h,remap16)` :366 | builds a 256 B byte table on the stack when w·h ≥ 512 | |
| `gfx_sprite4(spr,x,y,remap=nullptr,scale=256)` :390 | runs format `w,h,{n,(len-1)<<4\|col}`; colour 15 is transparent; scale is Q8 | 16×12 remapped **81 µs**; at 2× **224 µs** |
| `gfx_sprite4Rot(spr,ax,ay,px,py,uint8_t angle,scale=256,remap)` :400 | decodes into the chunk scratch (max 1 KB: 32×64 or 45×45), **uniform scale only**, per-pixel `rotSpan`, visits a square of side 2r (`CHGfx_extras.cpp:319-373`), **calls `gfx_wait()`** | 16×12: **616 µs** |
| `gfx_scroll(y,h,dx,dy,fill=-1)` :416 | word shifts in SRAM; ignores clip | 118 rows: **1.0 ms** vs memmove 6.3 ms |
| `gfx_copyRow(y,src64B,x0,x1)` :422 | word copy | |
| `gfx_text/textScaled/char/charScaled` :465-468 | built-in 5×7 (475 B), advance 6 px; GFXfonts use the **baseline as y** | 24 chars: **246 µs** |
| `gfx_textWidth/WidthScaled/textBounds` :472-481 | | |
| `gfx_textFx(x,y,s,scale,fill,outline=-1,shadow=-1,ramp=nullptr,dy=nullptr)` :501 | 1 bpp mask in scratch, dilate, paint spans; max 126×62 px; **calls `gfx_wait()`** | 3×, outlined and shadowed, 9 letters: **about 2.9 ms** |
| `gfx_nearest(rgb565)` :562 | 16-entry search; use at setup only | |

**Fonts** (`src/fonts/`, FONTS.md):

| Font | Flash | Notes |
|---|---:|---|
| `CHGfx_Tiny3x5` | 857 B stated, **952 B actual** | `GFXglyph` is padded to 8 B |
| `CHGfx_Digits24` | 523 B | only `+,-./0-9:`; 17 px digit advance; good for the winning-number banner |
| Sans12 | 1,344 B | |
| SansBold12 | 1,444 B | |
| SansBold16 | 1,866 B | |
| Mono11 | 1,233 B | |

The games keep their own `text35` and `Mask.cpp` (now the CHGame library's `chgame/Draw.cpp` and `chgame/Mask.cpp`). Using CHGfx's text instead costs **+2,260 B flash**, because `gfx_textScaled`/`gfx_textFx` force-link the 5×7 font and renderer (739 B), and `textFx` is 1,014 B of flash plus 462 B of SRAM code (`CHChess/docs/CHGfx-notes.md`, "On CHGfx 1.3.0: text, banners and the palette").

**Missing from CHGfx:**

- No polygon or triangle fill. CHGfx's `examples/Demoscene/Demoscene.ino` has a copyable flat-shaded `triangle()` made of hlines.
- No rotated or tilted ellipse.
- No arc or wedge.
- No public sine. `gfx__sin14(uint8_t)` (Q14, 256 steps per turn, 130 B quarter table, `CHGfx_palette.cpp:89-103`) has external linkage but is declared only in `CHGfx_internal.h:81`.

## 5. Reusable idioms (copy as-is)

**RAM function macro.** The games' copies (CHChess's and CHBlackjack's `src/RamFunc.h` at the time) are now one, the CHGame library's `chgame/RamFunc.h`; a sketch's `RAMFUNC(name)` gets its own prefix:
```cpp
#if defined(__riscv) && !defined(CHSIM)
#define CHGAME_APP_RAMFUNC(name) __attribute__((section(".gnu.linkonce.r.app." #name), noinline))
#else
#define CHGAME_APP_RAMFUNC(name) __attribute__((noinline))
#endif
#define RAMFUNC(name) CHGAME_APP_RAMFUNC(name)
```
- **Give every function its own name**, so linkonce does not merge them and `--gc-sections` can drop unused ones.
- Do **not** use `.srodata.ramfunc`, which `Demoscene.ino`'s `FX` macro and the CHGfx README ("Getting more frame rate") still show. It lands after `.sdata`, pushes variables out of the 4 KB global-pointer window, and cost CHChess 192 B (`CHChess/docs/CH32SerialBoot-notes.md`, "4. A proper RAM-function section").
- **Keep hot SRAM functions as leaves.** `-msave-restore` routes register save/restore through libgcc helpers in **flash**, so an SRAM function that calls anything or spills registers detours to flash (the note on SRAM functions at the top of `CHGfx_draw.cpp`). CHGfx uses `GFX_INLINE` (always_inline, `CHGfx_internal.h:38`) for exactly this reason.
- **Cost of flash code:** about 5 cycles per instruction from flash against about 2 from SRAM (`CHChess/docs/CH32SerialBoot-notes.md`, "4. A proper RAM-function section"). A function call per pixel from flash costs 2–3 µs (CHBlackjack's `NOTES.md`).

**Direct framebuffer writes** (from `CHGfx_internal.h:46-71`; the `gfx__` helpers are internal but linkable):
```cpp
static inline uint8_t *row(int y){ return gfx_fb + y*GFX_FB_STRIDE; }
static inline void plot(uint8_t *r,int x,uint8_t c){ uint8_t*p=r+(x>>1);
  if(x&1)*p=(*p&0x0F)|(c<<4); else *p=(*p&0xF0)|c; }
```

**Other patterns:**

- **Integer trig only.** libm `sinf`/`cosf` cost **8.5 KB of flash** and capped the demo at 29 fps; a 64-entry table restored 68 fps (`docs/performance.md`, "Optimisations the benchmark itself found").
- **Quadrant-symmetric polar tables** with branch-free mirroring, as in `Demoscene.ino`'s tunnel (part 2): two runs of 64 per row, with angle fix-ups right-down `a`, left-down `128-a`, right-up `-a`, left-up `128+a`. The brute-force `iatan2_q1` costs 65 candidates per pixel and is for boot time only.
- **Skip unchanged frames.** If nothing changed, skip drawing and just flush again; palette animation still moves (CHGfx README, "What a flush costs the CPU").
- **Raw pin writes.** GPIOB `CFGHR` (PB8..15: buttons, LED, buzzer, SD_CS, LCD_RST) is **write-only**. Raw pin configuration must go through the core's `CFGHR_tmpB` shadow (CHGfx README, "A note about GPIOB pins 8–15"; `CHGfx.cpp` `cfgPin`).

## 6. Roulette wheel: candidate techniques evaluated

The working numbers below come from the measurements above. Lines marked "est." are cycle-count estimates, not board measurements. The games build at `-Os`, so treat `-O2` benchmark figures as 10–50% optimistic for flash-resident code.

### A. Polar angle map plus a per-frame 256-entry pocket LUT (recommended)
This is the Demoscene tunnel technique applied to a 4 bpp ring.

- **Boot:** for one quadrant, store the wheel-plane angle (0..64 per quadrant; 256 per turn) for each ring pixel.
  - For a perspective wheel use `θ = atan2(dy·rx/ry, dx)`. The quadrant symmetry of an axis-aligned ellipse is preserved.
  - Store per-row ring spans `[xin, xout]` for outer and inner ellipses, as 2–4 B per row.
- **Each frame:** rebuild `uint8_t pocketLut[256]` from the 16-bit wheel angle:
  ```cpp
  for (int a=0;a<256;a++){ uint16_t u=(a<<8)+128-wheelAng;          // bin centre, wheel frame
    uint32_t t=(uint32_t)u*37; uint8_t p=t>>16; uint16_t f=t&0xFFFF;
    pocketLut[a] = (f < 256*37) ? FRET : POCKET_COL[p]; }             // frets fall out for free
  ```
  About 256 × 8–10 cycles ≈ **45–55 µs** (est.). Rotation precision is limited only by `wheelAng` (16-bit); boundaries step in 1/256 turn, which is about 1.5 px at r = 60. Use a 9- or 10-bit `uint16_t` table and LUT if the final slow-down looks steppy.
- **Ring loop (RAMFUNC, leaf):** load the angle byte, apply the mirrored fix-up hoisted per run, look up the LUT, pack two pixels per byte.
  - Estimated **8–12 cycles/px**. For reference, the Demoscene rotozoomer (part 3) is about 10 cycles/px, and the tunnel fits its 32-cycle budget at 82 fps.

| Wheel geometry | Ring pixels | Draw/frame (est.) | Table (ring-only quadrant) | Table (full quadrant box) |
|---|---:|---:|---:|---:|
| Perspective ellipse, outer 60×32, inner 45×24 | about 2,640 | **0.45–0.65 ms** | about 660 B | 61×33 = 2,013 B |
| Top-down circle, r 60/44 | about 5,230 | 0.9–1.3 ms | about 1,310 B | 60×60 = 3,600 B |
| Whole disc, r = 60 | about 11,300 | 1.9–2.8 ms | — | 3,600 B |

- **RAM:**
  - pocketLut: 256 B.
  - Table: 0.66–1.3 KB if generated at boot into RAM. This fits the about 2.7 KB headroom of a Blackjack-sized game.
  - Spans: about 130 B.
  - Ring renderer code in SRAM: about 150–250 B (est.; compare hline 202 B, sprite4 238 B).
- **Flash:**
  - About 0.4–0.8 KB of code.
  - Or put the table in flash as a `const` (+0.66–3.6 KB), generated by a Python `tools/assets.py` step. Reading data from flash adds wait states, estimated at about 3 cycles per load, about 0.15 ms per frame.
- **Boot time with Demoscene's `iatan2_q1`:** about 650 cycles/px, so about 9 ms (660 px) to 50 ms (3,600 px). It gives only 64 steps per quadrant. For finer angles use octant reduction plus `atan(t) ≈ t·π/4 + 0.273·t·(1−t)` (max error 0.0038 rad) in integer form.

### B. Palette rotation / colour cycling of a static wheel (supplement only)

- The palette has 16 slots. Blackjack's is fully allocated (now the CHGame library's `chgame/Palette.h`, shared by the games: `INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE, GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B`). Only `FX_A`/`FX_B` are animatable.
- A K-phase cycle needs K dedicated slots and gives a step of `2/(37K)` turn: K = 2 gives a 1-pocket (≈ 10 px) jump; K = 8 eats half the palette.
- It cannot move the green 0, the numbers, the frets or the ball.
- **Use the palette for things that cost nothing instead:**
  1. **Motion blur.** Draw pockets with two dedicated wheel-red and wheel-black slots and lerp them toward each other with angular speed. The cost is one LUT rebuild.
  2. **Winning-pocket pulse.** Point that pocket's LUT bins at `FX_A` and cycle `FX_A`.
  3. Chasing marquee lights and diamond glints.
  4. `gfx_setFade` or the CHGame library's palette fade (`chgame/Palette.h`) for transitions.

### C. Rejected or limited options

- **`gfx_sprite4Rot` of a wheel image.** The source is capped at 1 KB (45×45). Scaled ×2.6 to fill the screen, it visits about 16 K px at roughly 20+ cycles each (≈ 7 ms+) and looks blocky. It has uniform scale only, so no perspective ellipse, and it calls `gfx_wait()`. Fine for the ball, a tumbling chip or the turret cross: 16×12 costs 616 µs.
- **Per-pixel `atan2f`.** No FPU, hundreds of cycles per call, and +8.5 KB of libm flash. Not viable.
- **37 wedge quads from `triangle()` and hlines.** About 74 triangles, roughly 1.2 ms (est.), with seam and gap artefacts, and more code than option A.
- **37 `gfx_line` frets per frame.** About 16 px × 2.1 µs × 37 ≈ 1.25 ms. Option A gives frets for free.
- **Pre-rendered rotation frames.** A 128×64 ring region at 4 bpp is 4 KB per frame. Impossible.
- **`gfx_stream` true-colour wheel.** Feasible at about 82 fps, but it drops framebuffer composition and the palette look the two games share. Reserve it for a title flourish, as in Demoscene's hybrid part.

### D. Ball, turret and the rest of the frame

- **Ball position:** `x = cx + (ρ·cos φ >> 14)`, `y = cy + (ρ·(ry/rx)·sin φ >> 14)`, using `gfx__sin14` or a game-owned table.
  - 256 steps per turn is 1.4 px per step at ρ = 56. Linearly interpolate with φ's low byte for a smooth slow ball; this costs about 20 cycles.
  - Keep φ, ω and ρ in 16-bit fixed point. Logic runs at a fixed 60 Hz, as in Blackjack.
- **Ball drawing:**
  - A 5×5 or 6×6 `gfx_sprite4`, about 20 µs (est.; 16×12 is 81 µs).
  - Shadow with `gfx_fillEllipse` (23 µs).
  - Trail: 2–3 ghost dots in a darker slot.
- **Static wheel body:** stacked `gfx_fillEllipse` layers (rim WOOD, ball track, cone, GOLD turret), estimated at about 0.15 ms each for ry ≈ 34 (fillCircle r = 30 is 263 µs).
- **Turret arms:** 4 `gfx_line`s (about 0.2 ms) or one `sprite4Rot` (about 0.6 ms).
- **Numbers on a spinning 10 px pocket are illegible at 128×128.**
  - Show the result with `textFx` or `Digits24`.
  - Or add a horizontal "number reel" strip: about 10 visible cells, each a fillRect plus 3×5 text, about 0.2 ms.

**Estimated whole spin frame:**

| Item | Time |
|---|---:|
| clear | 0.09 ms |
| body ellipses | 0.6 ms |
| ring | 0.5–1.3 ms |
| LUT | 0.05 ms |
| ball and trail | 0.1 ms |
| HUD text | 0.25 ms |
| **Total** | **about 1.6–2.4 ms** |

That is well under the 8.3 ms draw budget at 12 bpp full-frame, leaving room for particles and a banner (about 2.9 ms).

- **No partial flush is needed.** If wanted, a 128×72 wheel band at 12 bpp is about 4.7 ms on the wire; x is aligned to 8.
- **Idle betting screen:** do not redraw; flush the old buffer and let the palette animate.

## 7. Gotchas to carry over

- **Always `gfx_wait()` before drawing.** The simulator flags drawing into a frame still being sent and use of the scratch during a flush; it exits with status 3 and prints a `BUG` line.
- The chunk scratch does not survive a flush, `sprite4Rot` or `textFx`.
- Ellipses are axis-aligned with rx/ry ≤ 255. `scale` arguments are Q8; `sprite4Rot` angles are 256 per turn, clockwise.
- At 12 bpp, partial-flush x/w are aligned to 8 px. `RGBSET` is programmed automatically at 12 bpp.
- **Build at oslto for size.** Hot code lives in RAMFUNC leaves, so `-Os` barely affects it. Do not pull in `snprintf` (Blackjack: 3.5 KB with 64-bit division) or libm.