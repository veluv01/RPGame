# CHGame casino visual style: palette, text effects and drawing primitives in CHChess and CHBlackjack

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

Paths: **CC** = `../CHChess`, **BJ** = `../CHBlackjack`.

**Since then:** the two games' copies of Palette, Mask, Draw, Fmt and Fx compared in sections 1-5 are one now, the CHGame library's `chgame/` (`Palette`, `Mask`, `Draw`, `Fmt`, `Fx.h` with `Ease.cpp` and `Shake.cpp`, and `Sizzle` for the particles, banner and floats), merged much as section 7 recommends. The CC/BJ comparisons describe the copies as they were; line references into them are dropped.

**CHGfx versions:**
- CHGfx is 1.3.0, in `platform/board/arduino/CHGame/libraries/CHGfx`; the board package brings it. (When this was written, a stale 1.1.0 copy also lay about.)

**Setup and frame loop:**
- Both games call `gfx_begin(GFX_DIV2, GFX_12BPP)`. Output is RGB444 (12 bpp), so palettes are written on the RGB444 grid.
- BJ loop (`loop()` in `CHBlackjack.ino`): up to 3 logic ticks of `chgame.pollButtons(); pal::tick(); screens::update();`, then `pal::commit()`, `gfx_wait()`, `render`, `gfx_flushAsync()`.
- CC loop (`Frame.cpp`): commits *after* `gfx_wait()`. CHGfx 1.3 stages the palette itself, so either order is safe.

---

## 1. Palette (then each game's `src/gfx/Palette.h/.cpp`, now `chgame/Palette.h/.cpp`)

### 1.1 Indices
The enum is identical in both games. It is a global unnamed enum outside `namespace pal`.

| idx | name | RGB444 | RGB565 (after commit) | ~RGB888 | Typical use |
|---|---|---|---|---|---|
| 0 | INK | 0x000 | 0x0000 | #000000 | outlines, shadows, HUD bar, sprite outline slot |
| 1 | WHITE | 0xFFF | 0xFFFF | #FFFFFF | text, card face |
| 2 | FELT_DK | 0x042 | 0x0224 | #004422 | felt edge dither, bet circle, card drop shadow (themed) |
| 3 | FELT | 0x173 | 0x13A6 | #117733 | felt fill, chess dark squares (themed) |
| 4 | FELT_LT | 0x4B5 | 0x45CA | #44BB55 | felt printing, labels, $25 chip (themed) |
| 5 | SILVER | 0xBBC | 0xBDD9 | #BBBBCC | secondary text, disabled labels |
| 6 | RED | 0xE12 | 0xE884 | #EE1122 | red suits, $5 chip, B_RED, neon |
| 7 | WINE | 0x702 | 0x7004 | #770022 | banner/logo shadow, card back, slab left face |
| 8 | GOLD | 0xFC2 | 0xFE64 | #FFCC22 | trim lines, panel borders, money text |
| 9 | WOOD | 0x741 | 0x7222 | #774411 | rail, shoe, bottom rows of gold gradient |
| 10 | BLUE | 0x26E | 0x233D | #2266EE | $10 chip, gradient low |
| 11 | NAVY | 0x125 | 0x110A | #112255 | panels and menu highlight fill, wallpaper, action bar, chess carpet |
| 12 | SKIN | 0xFB8 | 0xFDD1 | #FFBB88 | dealer skin, chess light squares, neon text |
| 13 | CYAN | 0x6EF | 0x677F | #66EEFF | subtitle, B_CYAN, move targets |
| 14 | FX_A | 0xF0F | 0xF81F | #FF00FF (init only) | animated: rainbow, grey hover fade or shimmer |
| 15 | FX_B | 0xFC2 | 0xFE64 | #FFCC22 | animated: gold-to-white pulse, or red-to-gold |

Conversion to RGB565 is bit replication: `((r<<1)|(r>>3))<<11 | ((g<<2)|(g>>2))<<5 | ((b<<1)|(b>>3))` (BJ's `to565`).

### 1.2 Felt themes
`THEMES[4][3]` is identical in both (the library's `FELTS`). `setTheme` overwrites only FELT_DK, FELT and FELT_LT.

| Theme | FELT_DK | FELT | FELT_LT |
|---|---|---|---|
| GREEN | 0x042 | 0x173 | 0x4B5 |
| BLUE_FELT | 0x024 | 0x149 | 0x48D (#4488DD) |
| RED_FELT | 0x401 | 0x812 | 0xC44 (#CC4444) |
| PURPLE | 0x203 | 0x517 | 0x95B |

- The enum is `pal::Theme : uint8_t { GREEN, BLUE_FELT, RED_FELT, PURPLE, THEME_COUNT }`.
- **Roulette caveat:** if the green zero pocket or the felt is drawn with FELT_*, the RED_FELT theme makes it red, clashing with RED (0xE12) numbers. A roulette build would want a fixed green, or a different set of theme triples.

### 1.3 Animated slots (identical tables)
- **`RAINBOW[12]`**: `0xF22,0xF82,0xFE2,0x8F2,0x2F4,0x2FC,0x2EF,0x28F,0x42F,0xA2F,0xF2E,0xF28`.
  - FX_A = `RAINBOW[(ticks/3)%12]`: a 36-frame cycle (0.6 s).
- **FX_B gold-to-white pulse**: triangle wave over 32 frames, `t=0..15`, with `g = 12 + t*3/15` and `b = 2 + t*13/15`. It runs 0xFC2 → 0xFFF.
  - Used by banner top rows, logo top rows, menu borders and selected buttons, so they shimmer with no redraw.
- **CC only**, `enum Mode : uint8_t { CASINO, TARGETS, HOVER }` and `setMode()` (the library added a fourth mode, `FIRE`):
  - **TARGETS**: FX_A = `SHIMMER[tri(ticks*2)>>1]`, a 16-frame shimmer through `{0x6EF,0x7EF,0x9EF,0xAFF,0xBFF,0xCFF,0xEFF,0xFFF}` (cyan to white). FX_B = `PULSE[tri(ticks*2+16)>>1]`, half a period out of phase, through `{0xE12,0xE32,0xE52,0xF72,0xF82,0xF92,0xFB2,0xFC2}` (red to gold).
  - **HOVER**: FX_A = `tri(ticks>>1)*0x111`, a grey ramp from black to white and back over 64 frames. FX_B is the normal pulse.
  - `tri(t)`: `t&=31; t>15 ? 31-t : t`.
  - Mode is chosen per frame in CC's `stage::update()` (`Stage.cpp`): `pal::setMode(sel != 0xFF ? TARGETS : gloveOn() ? HOVER : CASINO)`.

### 1.4 API and staging differences

| | CC | BJ |
|---|---|---|
| Extras | `setMode`, `SHIMMER`/`PULSE` tables | `setDesaturate(0..16)`; `flash(index, rgb444, frames)` overrides one index for N frames |
| Dirty tracking | `tick()` marks dirty only if FX_A/FX_B actually changed (saves 44 B flash and 32 B SRAM, per `CHChess/docs/CHGfx-notes.md`) | `tick()` marks dirty every cycling frame; `commit()` compares `out[]` against `gfx_pal[]` and calls `gfx_setPalette` only on a real change |
| `init` | `memcpy` | loop |
| `commit` pipeline | fade only: `c * fadeLevel >> 4` per channel | flash override → desaturate (luma `y=(5r+9g+2b)>>4`, `c += (y-c)*desat>>4`) → fade → 565 |

- Common API: `init`, `setTheme`/`theme`, `setFade(0..16)`/`fade`, `setFx`, `setCycling`, `tick`, `resetClock`, `commit`, `rgb444`.
- **Costs:** every committed change makes CHGfx rebuild its conversion LUT at the next flush. The game keeps its own fade: `gfx_setFade` would cost about 150 B more (`CHChess/docs/CHGfx-notes.md`).

### 1.5 Palette-driven effects in use
- **Screen fades:** `go()` sets `fadeOut=8`.
  - CC: `pal::setFade((fadeOut-1)*2)`.
  - BJ: `pal::setFade(fadeOut*2)`.
  - Then `enter()` sets `fadeIn=8`, ramping with `setFade(16 - fadeIn*2)`.
  - `go()` and `screens::update()` in each game's `Screens.cpp`.
- **View dip (CC)**: `dipT` 6 or 8 frames, `setFade(dipT>4 ? 16-(8-dipT)*3 : 16-dipT*3)` (`stage::update()`).
- **Bust flash (BJ)**: `pal::flash(WHITE, 0xFBB, 6)` turns every WHITE pixel pink for 6 frames (`present::onEvents`).
- **Lose screen (BJ)**: `pal::setDesaturate(min(t/12, 12))`, colour draining away (`loseRender()`). `enter()` resets it with `setDesaturate(0); setCycling(true)`.

### 1.6 Sprite remap tables (game code, through a 16-entry remap)
- **CC**, the `RM_*` tables at the top of `Stage.cpp`:
  - `RM_ID`.
  - `RM_CPU`: GOLD→RED, WOOD→WINE (the CPU's red glove).
  - `RM_HIT`: all→WHITE except INK (white hit flash).
  - `RM_PREY`: all→RED except INK.
  - `RM_ALERT`: WHITE→RED, SILVER→WINE, GOLD→RED, WOOD→WINE (denied glove, toggled on `denyT & 4`).
  - `SIDE_REMAP[2][16]` (`src/assets/Assets.cpp`): White `{..,13→1}`; Black `{1→5, 5→10, 10→11, 13→1}`.
- **Outline trick:** copy the piece remap and set `hl[INK] = edge`. The sprite's INK outline then becomes `fx::RAIN[(frame>>3)%5]` (picked up) or FX_A (hover fade) (`drawPieces()` in `Stage.cpp`).
- **BJ**: `DEALER_ALT_REMAP` (assets); `remap[RED] = suit colour` for court-card robes (`card()` in `CardArt.cpp`); `DIM[16] = {INK,SILVER,INK,FELT_DK,FELT,SILVER,WINE,WINE,WOOD,WOOD,NAVY,INK,WOOD,SILVER,FX_A,FX_B}` through `remapRect` for the inactive split hand (`dimCard()` in `CardArt.cpp`).

---

## 2. Mask: big outlined, shadowed, gradient lettering (now `chgame/Mask.h/.cpp`)

### 2.1 Shared design
- `struct Mask { uint8_t *bits; uint8_t stride; uint8_t w, h; }` holds MSB-first rows with a 1 px margin on every side.
- `maskBegin(w,h)`: `stride = (w+2+7)>>3`, `bits = gfx_chunkScratch()` (1 KB), zeroed. Limits:
  - w×h ≤ about 7000 px.
  - The stride must fit 32 B (`d[32]` / `tmp[32]` buffers), i.e. w ≤ 254.
  - Only usable between `gfx_wait()` and the next flush, and never kept across frames.
- `maskText35(m, x, y, s, scale=1, const int8_t *dy=nullptr)`: the 3x5 font at an integer scale, 4×scale advance, `'~'` = 2×scale. `dy[k]` is a per-character vertical offset (wavy text).
- `text35WidthScaled(s, scale) = text35Width(s)*scale`.
- `maskDraw` works in one pass, row by row: dilate the row in 8 directions (`dilateRow`), paint it as the shadow at (+1,+1), as the outline at (0,0), then paint the fill of the original row with `ramp[r-1]`.
- Cost rationale (header): naive outlined text is about 5 ms a line. `Mask.cpp` is 502 B flash + 408 B SRAM functions, against `gfx_textFx` at 1,014 B + 462 B (`CHChess/docs/CHGfx-notes.md`).

### 2.2 Divergence (the files are NOT identical)

| | CC Mask | BJ Mask |
|---|---|---|
| `maskDraw` signature | `(m, x, y, uint8_t outline, uint8_t shadow, const uint8_t *ramp)`: outline, shadow and ramp are always used; ramp must be non-null | `(m, x, y, uint8_t fill, int outline=-1, int shadow=-1, const uint8_t *ramp=nullptr)`: optional layers, flat fill if no ramp |
| `maskBlit1(m, bits, w, h)` | absent | present: puts 1 bpp MSB-first bitmap logos into the mask (needs zero padding bits) |
| `maskText35` | ORs whole scaled bit patterns per font row (fast) | `plot()` per font pixel: masked OR into ≤ 2 bytes |
| `runs()` (RAMFUNC `maskruns`) | writes nibbles straight into `gfx_fb`, 2 px at a time | scans runs and calls `gfx_hline`, skipping 0x00/0xFF bytes whole |
| `dilateRow` | RAMFUNC `maskdilate`, two carry loops | plain flash, one combined OR expression |
| clear | `memset` (word stores) | byte loop |

**Recommendation:** merge them. Take CC's fast `maskText35`, `runs` and `dilateRow`, and BJ's optional-layer `maskDraw` signature plus `maskBlit1`.

### 2.3 How each text effect is built

**`fx::banner` (identical logic in both):** `fx::banner(text, BannerStyle, cy, frames=70)`.
- Code: then each game's `Fx.cpp`, now `chgame/Sizzle.inl`. Text is at most 13 characters (`bannerText[14]`).
- `drawBanner()` steps:
  1. **Pop-in scale:** 2 (t<3), then 4 (t<7), then 3 settled. While `w > 124 && scale > 2`, scale drops.
  2. **Wave:** `dy[k] = ((isin(t*10 + k*36)*2) >> 8) + 2`, i.e. 0..4 px. Each letter bobs on a sine, phase-shifted 36/256 of a turn per character.
  3. **Mask:** `maskBegin(w+1, h+5)` with `h = 6*scale`.
  4. **Blink-out:** when `bannerFrames < 10 && (bannerFrames & 2)`, nothing is drawn.
- **Row ramps per style:** `enum BannerStyle { B_RAINBOW, B_GOLD, B_RED, B_CYAN, B_WHITE }`.
  - B_RAINBOW: `RAIN[((r/2) + t/3) % 5]`, with `RAIN = {RED, GOLD, FELT_LT, CYAN, BLUE}`. 2-row bands scroll down every 3 frames.
  - B_GOLD: rows 0-2 FX_B (pulsing), rows below `h/2+6` GOLD, then WOOD.
  - B_RED: rows 0-2 WHITE, then RED.
  - B_CYAN: rows 0-2 WHITE, then CYAN.
  - B_WHITE: WHITE throughout.
- **Outline and shadow:** B_RAINBOW uses outline FX_A (cycling rainbow) with an INK shadow. All other styles use an INK outline with a WINE shadow.
- **Position:** `x = 64 - w/2`, `y = cy - h/2 - 2`. `activeRows` reserves cy±18.
- **CC extra:** `holdBanner(bool)` keeps the banner up (frames stop counting down above 10) until released. `bannerT` wraps `0 → 128` to keep the dance phase.

**`title35`: static screen titles, a gradient with INK outline.**
- CC (`Screens.cpp`): `title35(text, y, scale, top, mid, low, shadow, lowFrom)`. The mask is exactly the text width. `ramp[i] = i<scale ? top : i<lowFrom ? mid : low`.
- BJ (`Screens.cpp`): fixed scale 3, mask 124×18, `ramp[i] = i<3 ? top : i<lowFrom ? mid : low`.
- Uses:
  - CC: `"CHESS"` scale 4, y=4 (FX_B, GOLD, WOOD, shadow WINE, lowFrom 17); `"OPPONENT"` scale 3, y=10 (lowFrom 13); `"OPTIONS"` scale 3, y=7.
  - BJ: `"OPTIONS"` y=6 (FX_B/GOLD/WOOD/WINE, 13); `"STATISTICS"` (WHITE/CYAN/BLUE, shadow NAVY, 9); the win-screen purse `"$1234"` at y=64 (FX_B/GOLD/WOOD/WINE, 13).

**Bitmap logo lettering (BJ, `maskBlit1`).**
- `LOGO` is PPOT's "BlackJack", 104×14 (`LOGO` in BJ's `tools/assets.py`). It is drawn at (12,8) with ramp `i<3 FX_B, i<12 GOLD, else WOOD`, INK outline, WINE shadow (`titleRender()`). The splash uses `i<13` (`splashRender()`).
- `YOUWON1/2` (119 and 107 wide, 16 tall) go through `lettering()`. Scrolling rainbow ramp `R[((i/3)+frame/4)%5]`, INK outline, WINE shadow.
- `BROKE1/2` use ramp `i<4 WHITE, i<10 RED, else WINE`, INK outline, NAVY shadow. They drop in with `drop = 40-t`, the second line arriving at t>20.
- `PPOT_LOGO` (65×32): ramp WHITE(<11)/CYAN(<22)/BLUE, NAVY outline, no shadow. In "printed on the felt" mode it is drawn flat as `maskDraw(m,x,y,FELT_LT)`.

**Other text effects:**

| Effect | Where | Recipe |
|---|---|---|
| Floating numbers ("+$15", "-$5", "INSURED") | BJ `Fx.cpp` (now `chgame/Sizzle`, `floatText`) | 4 slots, ≤ 7 chars, 50 frames, rising 1 px per 2 frames (`y = f.y - (50-t)/2`), blinks in the last 8 frames (`t<8 && t&1`), INK drop shadow at +1,+1 then colour. CC lacks it. |
| Typewriter speech bubble | BJ `present::speechBubble`, typed in `present::update` | `panel(x,y,w,h,4,WHITE,INK)`, 5-row tail. Each line is centred on its full width and only `typed` chars are drawn (`text35`, INK). One char per frame with `audio::blip(1900+(n*97)%700,12)` on odd chars, then a 100-frame hold. Credits type at 1 char per 2 frames (`creditsRender()`). |
| Word-by-word plate | CC `plate()` and `drawHud()` in `Stage.cpp` | NAVY fill / GOLD edge, r2, h=11. Width grows with `ease(OUT_BACK, t, 8)` and shrinks over the last 8 of 120 frames. Word k appears at frame 6+3k, dropping 3 px into place. Words are colour-coded (piece WHITE, " TAKES " RED, " TO " SILVER, square GOLD, promo FX_B). |
| Blinking prompt | both | `(frame & 16)` or `(frame & 32)`, e.g. "PRESS A", "DEMO". |
| Menu highlight | CC `menuItem`; BJ `titleRender()` | NAVY rounded r3 box, border `(frame & 16) ? FX_B : GOLD` (alternates every 16 frames on top of the FX_B pulse); text GOLD when selected, WHITE otherwise. |
| Bold numbers | BJ `table::plaque` | double-strike `gfx_text(x)` + `gfx_text(x+1)`. |
| Purse flash and roll | BJ `table::plaque`, `present::update` | `flash ? ((flash&4) ? WHITE : GOLD) : GOLD`. Counter `shown += d/5` (min ±1) with a 3 kHz blip every 4 units. |
| Arc-printed felt text | BJ `arcDy`, `arcText`, `arcLine` in `Table.cpp` | per char `y - (d*d)/900` with `d = x-64`. "BLACKJACK PAYS 3 TO 2" in GOLD between two FELT_LT arc lines. Directly reusable for "0 PAYS 35 TO 1". |
| Neon sign | BJ `creditsRender()` | `lit = frame%97 >= 3 && frame%211 >= 2`; roundRect RED or WINE, text SKIN or WINE. |
| Animated dots | BJ `bar::draw`; CC `drawHud()` | `(frame>>3)%4` dots; CC "thinking" HUD text in FX_B. |
| Rainbow tagline | BJ `splashRender()` | `"NOW IN COLOUR"` in FX_A (palette-cycled). `"~COLOUR~EDITION~"` in CYAN, using `~` half-spaces. |
| Hold-to-reset bar | BJ `statsRender()` | SILVER rect with a RED fill growing. |

### 2.4 Banner strings in use

| Game | Banner | Style | cy | Frames | Paired effects |
|---|---|---|---|---|---|
| BJ | "BUST!" (player) | B_RED | 91 | 70 | `shake(12,3)`, red SPARK burst, `pal::flash(WHITE,0xFBB,6)`, float "-$n" |
| BJ | "BLACKJACK!" | B_RAINBOW | 64 | 90 | CONFETTI fountain 24, STAR burst FX_A |
| BJ | "21!" | B_GOLD | 91 | 50 | |
| BJ | "DEALER BJ" | B_RED | 62 | 70 | `shake(10,2)` |
| BJ | "3 TO 2!" | B_RAINBOW | | 70 | |
| BJ | "WIN!" | B_GOLD | | 60 | CONFETTI 14 |
| BJ | "PUSH" | B_CYAN | | 50 | |
| BJ | "BUST!" (dealer) | B_GOLD | 60 | 60 | |
| CC | "CHECK!" | B_RED | 36 | 70 | held until a button is pressed |
| CC | "CHECKMATE!" | B_RAINBOW | 34 | 170 | held; two confetti fountains `fountain(40,90,20)` / `fountain(88,90,20)` when the human wins |
| CC | "BLACK/WHITE RESIGNS" | B_WHITE | | 150 | |
| CC | "STALEMATE" / "DRAW" | B_CYAN | | 150 | |
| CC | "BLACK/WHITE TO MOVE" | B_GOLD | 40 | 60 | |

Sources: BJ `present::onEvents`; CC `onCheck()`, `onOver()` and `stage::update()` in `Stage.cpp`.

---

## 3. Font, text and formatting (now `chgame/Draw.cpp`, `chgame/Fmt.cpp`)

**FONT35 (PPOT 3x5, Apache-2.0):**
- `FONT35[][3]` and `IDX35[91]` are byte-identical in both games (verified by diff).
- Each glyph is 3 column bytes, bit 0 at the top, bit 5 the descender.
- Glyph set: A-Z, a-z, 0-9, `! . - + ? : $ , / ' * ( ) < > = % #`. There is **no `&`, `"`, `;` or `@`**. CC's pause menu says "SAVE + QUIT" for this reason; BJ's "SAVE & QUIT" works only because it uses `gfx_text`.
- `glyph35(ch)` returns an index or -1.
- Cost: 331 B, against `CHGfx_Tiny3x5` at 952 B.

**Text functions:**
- `text35(x,y,str,c)` (RAMFUNC, identical in both): 4 px advance, `'~'` = 2 px, `'\n'` = 7 px down. Returns the advance.
- `text35Width(s)` returns the widest line minus 1.
- `glyph(x,y,cols,ncols,c)` (RAMFUNC): column-major, ≤ 8 rows. Also used for card rank and suit glyphs.
- **CC only:** `text35x2(x,y,str,c)` (RAMFUNC `text35x2`, then CC's `Draw.cpp`) and `text35x2Width`. 2×2 pixel blocks, 8 px advance, `'~'` 4 px, newline 14 px. CC uses it for every menu, option row, pause item and result title.
- **BJ instead uses CHGfx's built-in 5x7 font** (`gfx_text` / `gfx_textWidth`) for menus, pause, toasts, the plaque purse, badges and selected button labels. That links CHGfx's 5x7 font (475 B) and renderer (264 B in SRAM, `CHChess/docs/CHGfx-notes.md`). CC never calls `gfx_text` outside its simulator calibration. For a tight flash budget, follow CC: text35 + text35x2 only.

**`Fmt` (identical in both):**
- `char *fmtInt(char *p, int32_t v)`, `fmtMoney(p, v)` producing "$1234" / "-$5", and `fmtStr(p, s)`.
- Each returns the new end pointer so calls chain.
- This replaces snprintf, which costs about 3.5 KB. Copy it verbatim.

---

## 4. Draw primitives (then `src/gfx/Draw.h/.cpp`, now `chgame/Draw.h/.cpp`)

The rule (both headers): code runs from flash with 3 wait states, so a function call per pixel costs about 2-3 µs. Everything is built from `gfx_hline` spans, tight byte or word loops, or RAMFUNCs.

### Identical in both
- `fillRound(x,y,w,h,r,c)` and `roundRect(...)` with `r ≤ 4`, using the corner table `INSET[4][4] = {{1},{2,1},{3,1,1},{4,2,1,1}}` (pixel-art circles).
  - 390 B, against the library's 524 B (BJ's `Draw.cpp` comment at the time).
  - The library version also caps r at half the *width*.
- Static `plot(p,x,c)` nibble helper.

### BJ only
- `panel(x,y,w,h,r,fill,edge)` = `fillRound` + `roundRect`. Every card, panel, bubble and badge uses it.
- `remapRect(x,y,w,h,remap)`: flash version. `gfx_remapRect` would cost +304 B SRAM and +182 B flash.
- Everything else comes from CHGfx 1.3: `gfx_dither`, `gfx_sprite4(spr,x,y,remap=nullptr,scale=256)`, `gfx_fillEllipse`/`gfx_ellipse`, `gfx_copyRow`, `gfx_scroll`.

### CC only (kept because the library versions cost more; table in `CHChess/docs/CHGfx-notes.md`, "On CHGfx 1.3.0: shapes, sprites and row operations")
- `sprite4(data,x,y,remap,scale=256)`: RAMFUNC, separate 1:1 fast path, pre-doubles 15 remapped colours. The library version costs +244 B flash and +160 B SRAM.
- `spriteRot(data,ax,ay,px,py,angle,scale,remap)`: decodes into chunk scratch, art ≤ 32×60, visits only the rotated bounding box. The library version costs +220 B.
- `dither(x,y,w,h,c,phase)`: 50% checker, `(px+yy+phase)` even gets the colour, word-at-a-time. The library version costs +8 B flash and +176 B SRAM. The title dithers about 5 KB a frame.
- CC has no `panel()`. Its local `panel(y,h)` in `Screens.cpp` is NAVY/GOLD r3 at x=14, w=100.

### CC Fx shake
- CC's shake is its own RAMFUNC `shiftRows`: a word-copy row shift, dy plus 2 px sideways (then CC's `Fx.cpp`).
- BJ calls `gfx_scroll(y0, y1-y0+1, ±2, dy)`; per CC's notes the library shake is +164 B.
- newlib `memmove` would cost about 10 ms a frame.

### Sprite format (span4)
- `w, h`, then per row `n` followed by n bytes of `(len-1)<<4 | colour`. Colour 15 is transparent.
- Produced by the `tools/assets.py` `pack_span4` packers.
- Identical to CHGfx's `gfx_sprite4` format.

### RAMFUNC macro
- `__attribute__((section(".gnu.linkonce.r.<tag>." #name), noinline))`, with tags `chch` (CC) and `chbj` (BJ) at the time. The simulator build keeps only `noinline`.
- Now one macro, the CHGame library's `chgame/RamFunc.h`: a sketch's `RAMFUNC(name)` gets the tag `app`, the library's own `chg`; every function in a sketch needs a unique name.
- The old `.srodata` placement cost 260 B of flash (BJ's `src/RamFunc.h` at the time).

---

## 5. Fx module (then `src/fx/Fx.h/.cpp`, now `chgame/Fx.h` and `chgame/Sizzle`): shared core, diverged edges

### Identical in both
- `CURVES[5][17]` (Q8) and `ease(Ease e, int t, int n)` returning 0..256, with `enum Ease { LINEAR, OUT_CUBIC, OUT_BACK, IN_OUT, OUT_BOUNCE }`. OUT_BACK peaks at 281 and OUT_BOUNCE dips (overshoot).
- `isin(a)`: a is in 1/256 turns, returns ±255, from a 65-entry quarter table.
- xorshift `rnd()` (seed 0x1234567), `rndRange(lo,hi)`, `reseed()`.
- Particle pool: `struct Particle { int16_t x,y; int8_t vx,vy; uint8_t life,colour,kind,age; }`, `parts[48]` = 384 B SRAM, positions Q4. A full pool steals a random slot.
- `spawn`, and `burst(k,x,y,n,speed,colour)`: radial, life 20-40.
- `banner` / `drawBanner` (section 2.4), `shake(frames,amp)` with amplitude `(amp*shakeT+9)/10`, alternating up and down.
- `activeRows(lo,hi)`, `clear()`, `update()`.

### Diverged

| | CC | BJ |
|---|---|---|
| `Kind` | `{SPARK, CONFETTI, STAR, DUST}` | `{SPARK, CONFETTI, COIN, RAIN, STAR, DUST}` |
| `fountain` | `fountain(x,y,n)`, confetti only | `fountain(Kind,x,y,n)`; COIN is GOLD |
| `fountain` motion | both: vx ±28, vy -60..-30, life 40-70, colours `CONF={RED,GOLD,FELT_LT,CYAN,BLUE,WHITE}` | same |
| COIN | — | vy += 3 per frame, bounces at y=122 with half velocity. Drawn as a 3×3 GOLD block with a WOOD centre, narrowing to 1 px every 4th quarter of `age>>2` (spinning coin). |
| RAIN | — | straight 3 px vline (lose screen) |
| DUST | square puff of size `dust`, shrinking; `burst` halves vy for DUST | 1 px |
| `drawParticles` | `drawParticles(uint8_t dust)` | `drawParticles()` |
| Alive check | `particles()` | `particlesAlive()` |
| Extras | `holdBanner`; `extern RAIN[5]` | `floatText`/`drawFloats` |

- Particle rendering is the same in both: SPARK is a plus shape while `age < 8`, then a dot; CONFETTI alternates 2 px horizontal and vertical lines every 4 frames; STAR is a 3×3 plus.

For roulette, BJ's Fx is the better base (COIN, floats). Add CC's `holdBanner` and the exported `RAIN[]`.

---

## 6. Casino look and scene composition

**`feltBackdrop()`** is the same recipe in both (CC's `Screens.cpp` with its own `dither`; BJ's with `gfx_dither`):
```cpp
gfx_clear(FELT);
dither(0,0,128,6,FELT_DK,0); dither(0,122,128,6,FELT_DK,1);
dither(0,0,6,128,FELT_DK,0); dither(122,0,6,128,FELT_DK,1);
gfx_rect(2,2,124,124,GOLD);            // gold trim line inside the dark edge
```
- The flat felt area is 6..121.
- BJ adds a spotlight on the title: `gfx_dither(24,30,80,44,FELT_LT,0)`.

**BJ table layout** (`Layout.h`, `Table.cpp`):

| Rows | Band | How it is drawn |
|---|---|---|
| 0-41 | Back wall | NAVY pinstripe wallpaper: one row built with INK every 8 px from x=3, stamped with `gfx_copyRow` (vlines cost > 1 ms). INK dither ceiling 3 rows. WOOD dither spotlight behind the dealer. |
| 42-45 | Rail | GOLD hline, WOOD ×2 rows, INK hline. Chip rack of 11 edge-on chips `{WHITE,RED,BLUE,FELT_LT,INK}`. |
| 46-110 | Felt | FELT fill; FELT_DK dither 3 px side edges; FELT_DK line under the rail; arc-printed GOLD rules; FELT_LT rule text. Bet circle: `fillEllipse` FELT_DK, `ellipse` FELT_LT, inner `ellipse` FELT; in the betting phase it gets an FX_B ring at +1. |
| 111 | Trim | GOLD trim line (`TRIM_Y = 111`) |
| 112-127 | Action bar | NAVY with an INK top line |

- **Bar buttons** (`button()` in `Bar.cpp`):
  - Shape: `fillRound` r3, h12, face colour.
  - Shading line: an INK or WOOD line at h-2, depending on `darkFace()`, which is true for {RED, BLUE, NAVY, WINE, INK}.
  - Border: FX_B when selected, INK otherwise; the selected button lifts 1 px.
  - Disabled: NAVY face, SILVER text, INK dither.
  - Labels: the selected button uses the 5x7 font if it fits, otherwise text35.
  - Accordion widths ease with `w += (target-w)/3` in Q4 (`layout()`).
  - Redraw is skipped via an FNV hash of the bar state (`bar::draw`).
- **Chips** (`chip`, `chipStack` in `CardArt.cpp`, reusable for roulette):
  - `CHIP_BODY = {WHITE,RED,BLUE,FELT_LT,INK}`, `EDGE = {BLUE,WHITE,WHITE,WHITE,GOLD}`, `SHADE = {SILVER,WINE,NAVY,FELT_DK,INK}`, values {1,5,10,25,100}.
  - `chip(cx,y,denom,top)` is 13-15 px wide: edge band with stripes, plus an ellipse top.
  - `chipStack(cx,baseY,amount,maxChips)` stacks 2 px per chip.
  - Flying chips: `fly(x0,y0,x1,y1,...)` with OUT_CUBIC easing plus an `isin` arc of 10 px (`fly`, `flyY` in `Presenter.cpp`).
- **Card back:** WINE fill, RED dither, WHITE inner rect, GOLD diamond.
- **Drop shadow:** FELT_DK, 1 px right and below, following the rounded corner (`back()`, `shadow()` in `CardArt.cpp`).

**CC table** (`tableCol`, `drawTable()`, `drawBoard()` in `Iso.cpp`):
- Carpet `tableCol = NAVY`, `gfx_clear`. Shadow INK.
- Board slab: left face WINE, right face INK, GOLD trim on the edges.
- Squares: dark FELT (themed), light SKIN.
- Coordinate labels in GOLD text35.
- HUD (`drawHud()` in `Stage.cpp`): INK rows 0-8 with a GOLD line at y=9.

**Title screens:**
- **BJ** (`titleRender()`): felt backdrop → spotlight → LOGO mask with its FX_B top rows (shimmer without redraw) → tagline → 5-card fan dealt from above. Each card arrives with `ease(OUT_BACK, k, 18)`, a 10-frame stagger, a flip at k=12 with sound, and gentle y offsets {6,2,0,2,6}. Then a STAR burst at t=60 (14 particles, FX_A), chip stacks at (14,110) and (114,110), FELT_LT credits at y=116, and a menu from y=76 every 11 px.
- **CC** (`titleRender()`): live scene (board, camera drifting with `isin`, zoom 10) → INK dither over the top 34 rows → `"CHESS"` title35 scale 4 → INK dither (phase 1) behind the menu → `text35x2` menu items every 14 px, anchored at the bottom.
- **Other BJ screens:**
  - Win: NAVY plus a 16-ray sunburst rotating with the frame, alternating FX_A/WINE, using a clipped Bresenham `ray()` (`winRender()`). COIN fountains every 6 frames, STAR bursts every 24, CONFETTI every 30.
  - Lose: INK, NAVY vertical stripes, desaturate, RAIN particles, BROKE lettering dropping in, the dealer smiling.
  - Pause: full-screen INK dither, `panel(24,34,80,56,4,NAVY,GOLD)`, selected row `fillRound` INK with FX_B text.
  - Toast: `panel` INK/GOLD at the top, 60 frames.

**Redraw economy (copy it):**
- Every frame is flushed, but drawing is skipped when an FNV signature of the visible state is unchanged. Palette cycling (FX_A/FX_B, fades) keeps animating anyway.
- BJ: `screens::render`, plus band-level wall and felt signatures and `rowsMoving` in `Presenter.cpp` (`struct Sig`, `present::render`).
- CC: `signature()` and `stage::render()` in `Stage.cpp`. Its bob and marching borders step at 7.5 Hz (`frame>>3`).

---

## 7. Reuse summary for a roulette game

**Copy verbatim** (all now the CHGame library, so nothing is copied):
- `Fmt.*`, `CHGame.*` (identical apart from a comment and line endings; now `chgame/Input`), `RamFunc.h` (change the tag), and the FONT35/IDX35/`text35`/`glyph`/`fillRound`/`roundRect` part of `Draw.cpp`.
- The Palette core: BASE, THEMES, RAINBOW, FX_B pulse, fade.
- Fx core: ease, isin, rnd, particles, banner, shake, activeRows.

**Pick or merge:**
- Palette: BJ's `flash` + `setDesaturate` with CC's dirty-only tick and `setMode` (a roulette "winning number" mode could reuse TARGETS: cyan shimmer and red/gold pulse).
- Mask: CC's fast internals with BJ's `maskDraw(fill, outline=-1, shadow=-1, ramp)` + `maskBlit1`.
- Fx: BJ's `Kind` set (COIN, RAIN), `fountain(Kind,…)` and floats, plus CC's `holdBanner` and `extern RAIN[5]`.
- Draw: `text35x2` (CC) instead of `gfx_text` (BJ) to save the 5x7 font; BJ's `panel()` helper. Use CC's own `dither`/`sprite4`/shake for the smallest SRAM, or the CHGfx calls for the least code.

**Game-specific, use as patterns only:** `Table.cpp`, `Bar.cpp`, `CardArt.cpp` (but `chip`/`chipStack` transfer directly), `Presenter.cpp` (event → motion/sound/banner mapping, band-level redraw), `Stage.cpp`/`Iso.cpp`, and `Screens.cpp`. Two pieces from `Screens.cpp` are reusable as-is: the `"LABEL|v1|v2"` options pattern with `optField` (identical in both) and the go/enter/fade flow.

**Budgets:**
- Application flash is 50,944 B. BJ uses about 45.6 KB with LTO; CC about 48.9 KB (51.3 KB without LTO). Both need *Smallest + LTO* and *USB: Upload only*.
- SRAM is 20 KB. CC's release build was 17,872 B; the framebuffer alone is 8 KB.
- BJ saves in the last two 256 B pages of the app region (0xF500, 0xF600). A bigger build eats into them.
- Timing: async flush CPU is about 2.6 ms on CHGfx 1.3 (about 5 ms on 1.2). CC redraws in about 6-8 ms. BJ's worst frame (bust + shake + banner) is 11 ms.
- A soft-float trig demo once cost 8.5 KB of flash and half its frame rate, so keep the maths integer-only.