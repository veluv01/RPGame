# CHRoulette wheel view and ball: implementation spec

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02. The game built from it is in `WheelArt.cpp`, `Ball.cpp`, `Wheel.cpp` and `tools/wheel.py`.*

I checked this against CHGfx 1.3.0, the Demoscene tunnel, both games' `Audio.cpp`, `Iso.cpp`, `Fx.cpp`, `Table.cpp` and `Layout.h`. I also ran two read-only Python checks:
- The geometry was rendered as ASCII using CHGfx's own `EllipseRows` arithmetic. That render is where the map size (515 B) comes from.
- The pocket colour rules were checked against both wheel orders. Both match.

All other cycle and ms figures are estimates; none were measured on hardware.

## 0. Decisions that change the draft

1. **The angle map is centred on a pixel centre, not a pixel corner.** CHGfx ellipses are always odd-sized around a pixel centre. With the map on the same centre, the ring, cone and bowl layers line up exactly with no half-pixel seam. The generator uses CHGfx's `EllipseRows` formula (`struct EllipseRows` in `CHGfx_extras.cpp`) to decide which ring each pixel is in.
2. **The four quadrant mirrorings move out of the pixel loop and into four lookup tables.** The 256-entry table (LUT) is built per quadrant, so each pixel costs one map byte read and one table read.
3. **The LUT lives in `gfx_chunkScratch()`, so it costs no static SRAM.** It is 1,024 B, rebuilt every frame between `gfx_wait()` and the ring draw.
4. **The landing solver shifts the rotor, not the ball.**
   - The ball's path before it reaches the rotor is fixed relative to the screen: the croupier's flick point and the deflectors don't move.
   - Rotating the rotor's start angle by whole pockets moves the result by exactly that many pockets.
   - Shifting the ball's start angle instead would move the flick away from the glove and change the deflector hits.
5. **The green zero uses the felt colours (FELT/FELT_DK), and the RED_FELT theme is replaced.** Details are in §1.3.

## 1. Geometry and colours

### 1.1 Placement
- Wheel centre is **CX=64, CY=86**. Viewed at 30°, so every circle draws with height = width/2, the same 2:1 as chess.
- Radii **R** are horizontal half-widths in px, always even, so the vertical half-height `ry = R/2` is a whole number.
- Each layer's centre is raised by **o** rows to show height. The rule I used: a lower, inner layer must not stick out past the outer layer's near (bottom) edge, i.e. `R_in ≤ R_out − 2·(o_in − o_out)`. That gives a real bowl: the far inside wall shows and the near one is hidden.
- The wheel fits between the rail and the bottom of the screen:
  - Far rim edge is at row 52.
  - The side face ends at row 119, and the shadow at row 121.
  - Rows 46–51 and 122–127 stay plain felt.
  - The wall band (rows 0–45, dealer at x=2) is untouched.

### 1.2 Layers, drawn in this order

| Layer | Centre | R (×ry) | Colour | Visible at far / near side |
|---|---|---|---|---|
| Felt band | rows 46–127 | – | FELT, with BJ's FELT_DK edge dither and line at y46 | |
| Shadow | (66, CY+5) | 60×30 | FELT_DK | |
| Bowl side face | (64, CY+3) | 60×30 | WINE fill, INK outline | – / 7 rows |
| Rim top | (64, CY−4) | 60×30 | WOOD fill, GOLD outline | 2 / 2 rows |
| Inner bowl wall | (64, CY−4) | 56×28 | INK | 2 / 0 |
| Ball track | (64, CY−3) | 54×27 | **NAVY** (the white ball stands out; matches the wallpaper) | 3 / 1; ball runs at R52 |
| Deflector ring | (64, CY−2) | 50×25 | WOOD, with 8 deflectors at R48 | 3 / 1 |
| Gap under the rotor | (64, CY−1) | 46×23 | INK | 2 / 0 |
| **Rotor, angle map** | (64, CY) | 44…26 | see §1.3 | |
| Cone | (64, CY) | 26×13 | WOOD fill, GOLD outline (inner lip), WINE inlay ring R18 at CY−1 | |
| Turret dome | (64, CY−2) | 8×4 | GOLD, WOOD outline | |
| Turret column | x 62–66, y CY−9…CY−3 | | GOLD, FX_B highlight column, WOOD shade column | |
| Cap and finial | (64, CY−9), 3×1; finial at CY−12 | | FX_B (shimmers for free); finial SILVER/WHITE | |
| Arms ×4 | `gfx_line` from (64, CY−9), length 12 | | GOLD, with 2×2 SILVER knob at each tip | |

- The 8 deflectors sit at angles `(2k+1)/16` of a turn. They alternate between a 3×3 plus shape and a 3×1 bar, in GOLD with a SILVER centre. Each flashes WHITE for 4 frames when hit.
- The turret arms rotate with the rotor.

### 1.3 Rotor rings and pocket colours

| Ring (R) | Band in map | Colour |
|---|---|---|
| 44–42 | 3 | GOLD rotor lip |
| 42–36 | 1 | Number ring: RED / INK / FELT, with GOLD dividers 1 step wide |
| 40–38 (inside 42–36) | 2 | WHITE mark, 1 step wide, at each pocket centre; reads as "numerals" |
| 36–34 | 3 | GOLD separator |
| 34–26 | 0 | Pocket floors: WINE / INK / FELT_DK, with **SILVER frets** 2 steps wide; ball rests at R30 |

A "step" is one angle step (1/256 turn, §2.2).

- **Black is INK, not NAVY.** It is true black, and NAVY is now the track. The GOLD lip keeps black cells from merging with the INK gap.
- **Pocket colour is computed by parity, so no colour table is needed** (both rules verified):
  - European: index 0 is green, odd indexes are red, even ones black.
  - American: indexes 0 and 19 (the 00) are green, odd ones black, even ones red.
  - Number tables: ORDER_EU[37] + ORDER_US[38] = 75 B. The inverse lookup is a linear search.
- **The zero uses FELT/FELT_DK**: "the zero wears the felt". It costs nothing and is consistent across the wheel, the 0 cell on the layout, and the tote board.
  - RED_FELT has to go anyway, because red number cells on red felt are unreadable. Replace it with, for example, TEAL `{0x033, 0x166, 0x5BB}`, or ship three themes.
  - Fallback if a true green zero is required in every theme: override FELT_DK/FELT to the green pair only while the wheel fills the screen. Switch at the end of the swing in, restore at the start of the swing out. The cost is that the tote board's zero changes colour. I don't recommend it.
  - Repurposing CYAN breaks the shared look: rainbow banners, rain particles and the CONF colours all use it.

## 2. Polar angle map

### 2.1 Format (generated)
```cpp
// src/assets/WheelMap.h  (tools/wheel.py, do not edit)
constexpr int WHEEL_ROWS = 23, WHEEL_R_IN = 26, WHEEL_R_OUT = 44;
extern const uint8_t WHEEL_SPAN[23][2];  // row j: pixels dx = i for i in [a, b); a = 0 once j > 13
extern const uint8_t WHEEL_MAP[515];     // rows packed together, 1 byte per pixel: band<<6 | q
```
- The map stores one quadrant. Row j (dy = j) includes the axes.
- Mirroring:
  - The right half uses i ≥ 0; the left half uses i ≥ 1.
  - Rows below the centre use j ≥ 0; rows above use j ≥ 1.
- `q = min(63, floor(atan2(2j, i)·128/π))` is the quadrant angle in steps of 1/256 turn. The clamp only affects the i = 0 column, which lies exactly on a step boundary, so it is correct either way.
- The full-turn step `s` is:
  - right-down: **q**
  - left-down: **127−q**
  - left-up: **128+q**
  - right-up: **255−q**

  These are floor-based steps. Demoscene's `128−a` applies to angle values, not steps, and would be off by one here.
- **Size:** 515 + 46 = **561 B of flash.** About 2,000 ring pixels are drawn per frame.

### 2.2 Angle resolution: 256 vs 512 per turn
- **Use 256.** The rotor's outermost moving ring is at R44, where one step is 1.08 px; at R30 it is 0.74 px. Steps therefore never exceed the pixel grid. In the slow final crawl (under 0.5 step per frame) the ring moves 1 px every 2+ frames, which is ordinary pixel motion.
- **R54 never uses the map.** The track is static, and the ball is placed from a 16-bit angle with sine interpolation.
- **The real artefact is pocket-width shimmer.** A pocket is 6.92 steps wide, so with 2-step frets the floor shows 4 or 5 steps as it turns.
- **What 512 costs:**
  - q needs 7 bits, leaving 1 band bit, so the WHITE marks go and the GOLD rings become `gfx_ellipse` outlines (aligned now that the map is on a pixel centre).
  - The map size is unchanged; the table is still 1 KB.
  - Building the table takes about twice as long (about +0.25 ms).
- Keep `--bins` as a generator option.

### 2.3 Generator: `tools/wheel.py`
- Pure Python using `math`. numpy 2.5.2 is installed but is not in the repository's `tools/requirements.txt`, so don't depend on it. Pillow is only used for previews.
- `ell_rows(rx, ry)` copies CHGfx `EllipseRows` exactly: `lim = rx²·(ry² − dy² + ry>>1)`, stepping x down.
- `inside(R, i, j) = j ≤ R/2 and i ≤ ell_rows(R, R/2)[j]`.
- A pixel is in the ring when `inside(44)` and not `inside(26)`. Its band:
  - 3 if not `inside(42)`
  - 2 if it is in the 40–38 strip
  - 1 if not `inside(36)`
  - 3 if not `inside(34)`
  - otherwise 0
- Output:
  - `WheelMap.{h,cpp}`, with `static_assert` checks on the sizes.
  - Previews at several rotor angles (as built: `out/wheel/`, `wheel_sheet.png` and others), drawing every layer from §1.2 in the real palette. Use these to check the near-side tucking and how it reads.

### 2.4 Per-frame LUT
- Layout in scratch: 4 quadrant blocks × 4 bands × 64 = 1,024 B. Block = `s>>6`: RD, LD, LU, RU.
- Angles use pocket units: 1 turn = `n<<16`.

```cpp
static const uint8_t RING_C[3] = {FELT, RED, INK}, FLOOR_C[3] = {FELT_DK, WINE, INK};
RAMFUNC(wheellut) void buildLut(uint8_t *lut, uint32_t rho, uint8_t n, const uint8_t *cls,
                                uint8_t hiP, uint8_t hiC) {          // hiP = 0xFF: no highlight
  const uint32_t T = (uint32_t)n << 16, step = (uint32_t)n << 8;   // one step = n*256 pocket-Q16
  const uint32_t FR = n << 8, DV = n << 7, MK = n << 7;            // half-widths: fret 2, divider 1, mark 1 step
  uint32_t u = ((uint32_t)n << 7) + T - rho; if (u >= T) u -= T;  // centre of step 0, wheel frame
  for (int s = 0; s < 256; s++) {
    uint32_t p = u >> 16, f = u & 0xFFFF; uint8_t k = cls[p];
    uint8_t ring = RING_C[k], flo = FLOOR_C[k];
    if (p == hiP) ring = flo = hiC;                                // win: WHITE 4 frames, then FX_A
    uint8_t c0 = (f < FR || f >= 0x10000 - FR) ? SILVER : flo;
    uint8_t c1 = (f < DV || f >= 0x10000 - DV) ? GOLD : ring;
    uint8_t c2 = (f - (0x8000 - MK) < 2 * MK) ? WHITE : c1;        // unsigned window test
    int q = s & 63; if (s & 64) q = 63 - q;                        // LD: 127-s, RU: 255-s
    uint8_t *d = lut + ((s >> 6) << 8);
    d[q] = c0; d[64 + q] = c1; d[128 + q] = c2; d[192 + q] = GOLD;
    u += step; if (u >= T) u -= T;
  }
}
```
- **Cost:** 256 iterations × about 22 instructions. About 0.27 ms as a RAMFUNC (≈200 B SRAM), or about 0.6 ms from flash.
- **Start in flash** and promote it only if the landing frame goes over budget (§5).
- **Frets are always exactly 2 steps wide**, because colour is sampled at step centres.
- **Motion blur can't use the palette** (RED/INK/FELT are shared slots). If the rotor ever exceeds about 3 steps per frame, make the table alternate colours step by step and drop the frets. Normal spins (≤2 steps per frame) never reach that.

### 2.5 Ring loop (RAMFUNC leaf, one call per frame)
```cpp
// cx must be even (pixel centre). Writes gfx_fb directly in [x0,x1)x[y0,y1); x0, x1 even.
RAMFUNC(wheelring) void ring(int cx, int cy, const uint8_t *lut, int x0, int x1, int y0, int y1) {
  const uint8_t *m = WHEEL_MAP;
  for (int j = 0; j < WHEEL_ROWS; j++) {
    int a = WHEEL_SPAN[j][0], b = WHEEL_SPAN[j][1];
    const uint8_t *rm = m - a; m += b - a;                           // rm[i], i in [a,b)
    for (int up = 0; up < 2 && (j || !up); up++) {
      int y = up ? cy - j : cy + j; if (y < y0 || y >= y1) continue;
      uint8_t *fb = gfx_fb + y * GFX_FB_STRIDE;
      const uint8_t *L = lut + (up ? 3 : 0) * 256;                    // right: RU / RD
      int i = a > x0 - cx ? a : x0 - cx, e = b < x1 - cx ? b : x1 - cx;
      if (i < e) { uint8_t *p = fb + ((cx + i) >> 1);
        if (i & 1) { *p = (*p & 0x0F) | (L[rm[i]] << 4); p++; i++; }  // odd x = high nibble
        for (; i + 1 < e; i += 2) *p++ = L[rm[i]] | (L[rm[i + 1]] << 4);
        if (i < e) *p = (*p & 0xF0) | L[rm[i]]; }
      L = lut + (up ? 2 : 1) * 256;                                   // left: LU / LD
      i = a ? a : 1; if (i < cx - x1 + 1) i = cx - x1 + 1;
      e = b < cx - x0 + 1 ? b : cx - x0 + 1;
      if (i < e) { uint8_t *p = fb + ((cx - i) >> 1);
        if (!(i & 1)) { *p = (*p & 0xF0) | L[rm[i]]; p--; i++; }      // even x = low nibble
        for (; i + 1 < e; i += 2) *p-- = L[rm[i + 1]] | (L[rm[i]] << 4);
        if (i < e) *p = (*p & 0x0F) | (L[rm[i]] << 4); }
    }
  }
}
```
- **Pixel packing:** even x is the low nibble and rows are 64 B (`gfx__row` in `CHGfx_internal.h`).
- **Right half:** pairs (i even, i+1) share a byte.
- **Left half:** pairs (i odd, i+1) share a byte, mirrored.
- **Cycles:** about 13 instructions per 2 px, plus 2 map reads from flash with wait states. That is about 10.5 cycles/px × 2,000 px plus row overhead, so **about 0.5 ms**.
- **About 260 B in SRAM.** These bytes also count against flash, because SRAM code is copied from flash at boot.
- **It needs about 18 live values,** so GCC will save s-registers. Under `-msave-restore` that is one call to the flash helper on entry and exit per frame, which is fine.
- **No calls inside the loop.** Check the objdump for any `jal` other than the save/restore helpers.
- **Section name:** `.gnu.linkonce.r.chrl.wheelring` as planned; the game's `RAMFUNC(wheelring)` from the CHGame library's `chgame/RamFunc.h` gives `.gnu.linkonce.r.app.wheelring`. Do not copy Demoscene's `.srodata.ramfunc`.

## 3. Ball physics and landing solver
Put the simulation in `BallSim.{h,cpp}` (as built: `Ball.{h,cpp}`): pure integer code with no gfx, so a host test (as built: `tools/tests/run_ball_tests.py`) can test it on the PC.

### 3.1 State and units
```cpp
struct Ball {                  // ~48 B; a dry-run copy lives on the stack
  int32_t a, w;                // world angle / speed, pocket units (1 turn = n<<16)
  int32_t rho, rw;             // rotor angle / speed
  int32_t psi, v;              // on the rotor: angle relative to the rotor, relative speed
  int32_t dec;                 // track deceleration per frame
  int16_t r, vr, z, vz;        // radius, radial speed, hop height, vertical speed (Q8 px)
  uint32_t rng;                // own xorshift; never fx::rnd() (particles use that)
  uint16_t t, tAll; uint8_t ph, n, hits, pocket, quick;
};
enum : uint8_t { EV_ROLL=1, EV_LEAVE=2, EV_DEFLECT=4, EV_FRET=8, EV_BOUNCE=16, EV_LAND=32, EV_FLICK=64 };
uint8_t step(Ball &b);         // one 60 Hz tick; returns events (presenter plays them, dry run ignores them)
```
Tuning values are written in turn-Q16 units (65,536 = one turn) and multiplied by n at init, which is exact. One pocket per frame = 65,536.

### 3.2 Phases (FUN / QUICK)

| Phase | What happens | Frames |
|---|---|---|
| WAIT | Rotor turning (rw: 0.45 rev/s = 492·n per frame FUN; 0.6 rev/s = 655·n QUICK; `rw −= rw>>10`). Covers the swing in plus the croupier's red glove dipping at σ=176 (top left of the wheel). Ball released at a = 176/256 turn, r = 52. **The timeline starts at the beginning of the swing**, so nothing needs extrapolating. | 28 / 18 |
| TRACK | `a += w` with w negative (counter-clockwise; rotor turns clockwise). `w += dec`, where `dec = (W0 − W_DROP)/T_track`. W0 = 2.1–2.4 rev/s (2294–2622 turn-Q16) FUN, 1.5–1.7 QUICK. W_DROP = 0.85 rev/s (928) FUN, 0.9 QUICK. T_track = 180 + rnd%40 FUN, 60 + rnd%20 QUICK. That is about 4.8 laps FUN. Below 1.3 rev/s the radius wobbles by up to 1.5 px. | 180–220 / 60–80 |
| DROP / APRON | Inward pull `vr −= 6` (Q8 px/frame²). Angular momentum makes the ball speed up as it falls: `w += 2·w·dr / r`. A deflector is crossed when `(a − T/16)·8/T` changes while r is 46–50. Each crossing hits with probability ½, at most 2 hits. A hit sets vz = 410–614 (1.6–2.4 px/frame), multiplies \|w\| by 3/8–5/8, and kicks inward (`vr −= 77`). | 20–35 / 12–20 |
| ROTOR (rattle) | At r ≤ 44 switch to the rotor frame: `psi = a − rho`, `v = w − rw`, \|v\| capped at 0xF000 (at most one fret per frame). `r += (R30 − r) >> 3`. Gravity `vz −= 90`. **Fret crossed** (`psi>>16` changes) while z < 320: if \|v\| > 19,661 and rng < 75%, hop over (`vz = 230 + |v|·5/1024`, capped at 640; `v = v·5/8`). Otherwise bounce back (`v = −v·3/8`, psi pulled back inside, `vz = 128`). Floor: `vz = −vz·3/8` if vz < −100, else 0. Friction: `v −= v>>3` on the floor, `v>>6` in the air. Ends when \|v\| < 2000 and z = vz = 0 for 4 frames, or forced at 150 / 90 frames. | 40–120 / 25–70 |
| SETTLE | `psi += (centre − psi) >> 2`, with centre = `(psi & ~0xFFFF) | 0x8000`. `pocket = (psi>>16) % n`. Emits EV_LAND. | 12 / 8 |
| DONE | Ball rides the rotor: `a = rho + psi`. | until the swing out |

Total wheel time, including the banner hold and the swing out:
- FUN: about 6.5–8.5 s.
- QUICK: about 3–4 s.

Holding A runs `step()` 4× per tick to fast-forward. The result is identical because the simulation is deterministic.

### 3.3 Guaranteed landing
```cpp
void spinStart(Ball &live, uint8_t target /*wheel index*/, uint32_t seed, uint8_t n, bool quick) {
  Ball d; init(d, seed, n, quick, 0);
  while (d.ph != DONE) step(d);                  // ~400 steps, ~2-3 ms from flash: run on the static "NO MORE BETS" tick
  uint32_t k = (d.pocket + n - target) % n;      // rotor +k pockets => pocket under the ball -k
  init(live, seed, n, quick, (uint32_t)k << 16); // rho0 = seedBase + k<<16 (mod n<<16)
}
```
- **Why it is exact:**
  - Before the rotor phase, the ball's path depends on nothing tied to the rotor.
  - On the rotor it depends only on the fret phase (psi's low 16 bits), the rotor speed and its own RNG.
  - A shift of `k<<16` in pocket units keeps the fret phase bit-exact. That is why angles use pocket units rather than turn-Q16: 65,536/37 is not a whole number.
- **The rules RNG (`rand32()%n`) only picks the result.** The `seed` comes from `fx::rnd()`.
- **Rules for `step()`:**
  - It may read only `Ball`.
  - Pace and n are copied into `Ball` at spin start, so changing options while paused can't affect a spin in progress.
- The dry run also gives `t_leave`, useful for timing "NO MORE BETS!" when the ball leaves the track.
- **Debug check:** assert `live.pocket == target` at settle.
- **Host test (`tools/tests/test_ball.cpp`):** 10k seeds × all targets × {37, 38} × {FUN, QUICK}. Check exact landing, duration limits, and \|v\| below one pocket per frame.

### 3.4 Projection and hops
```cpp
uint32_t a16 = b.a / b.n;  int a8 = a16 >> 8, f = a16 & 255;           // screen angle, clockwise from +x
int s = isin(a8) + (((isin(a8+1) - isin(a8)) * f) >> 8), c = (same with +64);  // ±256, interpolated
int o  = b.r >= 52<<8 ? -768 : b.r > 46<<8 ? -256 - (b.r - (46<<8)) * 2 / 6 : 0;  // surface rows, Q8
x  = cx + ((b.r * c / 256 + 128) >> 8);
ys = cy + (((b.r * s >> 9) + o + 128) >> 8);   // shadow: on the surface
y  = ys - ((b.z * 7) >> 11);                   // hop: z · 7/8 (cos 30°) rows up
```
- **Ball sprite:** 4×4 sprite4 with transparent corners, WHITE body and SILVER lower-right, plus an INK contact pixel at (+2, +2).
- **Shadow:** a 3×1 INK line at `ys` while z > 0.
- **Trail:** above 1.2 rev/s, 3 ghosts at `a − k·w/4`: 2×2 WHITE, 2×1 SILVER, 1×1 SILVER.
- **After settling,** snap the drawn angle to the step grid (`((rho+psi)/n & ~255) + 128`) so ball and pocket move together with no ±1 px wobble.

### 3.5 Sounds (merged BJ blip with chess's soft pulse)
- Add `blip(hz, ms, bool soft=false)`, which sets chess's `soft` flag inside the IRQ-off section. Soft means duty = period/8 (then CHChess's `src/audio/Audio.cpp`; the CHGame library's engine, `chgame/Audio.h`, now has `blip(hz, ms, soft)`).

| Event | Sound |
|---|---|
| Swing | `Sfx::Whoosh` (existing) |
| Flick | `Sfx::Hop` (chess: 1500→3300 Hz, 80 ms) |
| Rolling | `blip(650 + |w_turnQ16|/5, 2, soft)` per 1/16 turn travelled. About 35/s at 1130 Hz, falling to 14/s at 840 Hz; speeds up again as the ball spirals in. |
| Leaves track | `blip(1400, 4, soft)` |
| Deflector | New `Sfx::Clack {S(3800,0,3), REST(4), S(2900,0,5), REST(3), S(3300,0,3)}`, prio 1, plus 4 WHITE sparks |
| Fret hop or bounce | `blip(2300 + (rng & 511), 4)`, normal duty |
| Floor bounce | `blip(1500, 3, soft)` |
| Landing | New `Sfx::Thunk {S(1300,700,26), REST(18), S(2800,0,6)}`, prio 2, plus `LED_BLINK` |

Blips are prio 0, so they are dropped automatically under Whoosh or Thunk.

## 4. Composition, depth and draw order
1. Wall band: BJ's band-signature redraw.
   - The dealer's eyes follow the ball (`look` from the ball's x).
   - While the glove reaches over the rail (rows 42–45), mark those wall rows dirty.
2. Felt band: 82 × `gfx_copyRow` from a prebuilt row.
3. Static layers from §1.2, then the deflectors.
4. `buildLut` into scratch, then `ring()`.
5. Cone.
6. **Ball, if it is in the far half (sin < 0):** a ball in a far pocket can sit behind the turret arms (both around y 72–78).
7. Turret and arms.
8. **Ball, if it is in the near half.** The ball always goes after the ring, because on the near track it overlaps the rotor lip by a row and should be on top.
9. Glove: chess `HAND` with `RM_CPU`.
10. Particles, banner, then shake of rows 46–127 only (CC `shiftRows`).

**Frame cost (ms, est.):**

| Item | ms |
|---|---|
| Felt | 0.25 |
| 7 ellipse fills + 3 outlines | 1.5 |
| Deflectors | 0.08 |
| LUT (flash / SRAM) | 0.6 / 0.27 |
| Ring | 0.5 |
| Cone | 0.3 |
| Turret | 0.2 |
| Ball | 0.06 |
| **Total** | **≈ 3.5–3.9** |

- The landing frame adds:
  - banner: 2.0–2.9 ms
  - dealer typing, which redraws the wall: about 1.2 ms
  - particles: about 0.3 ms
- **That is about 7.5–8 ms against the 8.3 ms limit** (§7).

## 5. The swing between layout and wheel
- **Timing:** 14 frames FUN / 10 QUICK, with `ease(IN_OUT)`. Step once per *drawn* frame, as in chess's `zoomDrawn` latch.
- **Offsets:** `off = (128·e >> 8) & ~1`. Offsets must be even, so the ring's centre stays on an even x.
- **Layout and wheel sit side by side** (layout left, wheel right):
  - Swing in: layout `gx = −off`, wheel `gx = 128 − off`.
  - Swing out: the reverse.
- **Drawing:** each scene is drawn under `gfx_setClip(visible x-range, 46, w, 82)`, and `ring()` gets the same x0/x1.
  - Skip the ring, cone or turret entirely when they are clipped out.
  - Worst case is about 5–6.5 ms (layout plus wheel, each partly clipped).
  - Fallback: `gfx_scroll` the previous frame and draw only the newly exposed strip, about 1 ms.
- **Speed streaks:** when the frame-to-frame move is ≥ 8 px, draw 6 `gfx_hline`s per frame at random rows 46–127. Length is 2·speed + rnd%16, alternating WHITE / SILVER / FELT_LT. About 40 µs.
- **Sound and fade:** Whoosh on frame 0. Optionally a mild fade dip, 16 → 12 → 16.
- **Zoom on the landing pocket is not affordable:** it needs a second map at 2× (about 2 KB of flash plus about 2 ms). The landing punch instead:
  - `shake(6, 2)` on the table band;
  - the winning pocket's table entries go WHITE for 4 frames, then FX_A (rainbow, so the palette mode must be CASINO);
  - the ball drawn at sprite4 scale 512 → 384 → 256 over 3 frames;
  - `burst(SPARK, 10, GOLD)` plus `burst(DUST, pocket colour)`;
  - the banner `"17 RED"` / `"00 GREEN"` at cy ≈ 84;
  - optionally, a "17" tag in text35 drawn above the ball.

## 6. Title screen
- The same `wheel::draw(view, gx=0)` with the title's own `Ball`:
  - Rotor at 0.15 rev/s.
  - Demo spins aimed at `fx::rnd()` targets every ~8 s, with sound muted and a small banner. Between spins the ball laps at about 1 rev/s.
- **Wall band:** pinstripe, spotlight, "ROULETTE" in title35 scale 3 (96 px) and the rail. It is static, so it is skipped by the signature.
- **Menu:** `dither(0, 94, 128, 34, INK, 1)` with text35x2 items at 11–12 px pitch, chess style. The far rim, rotor and turret stay visible.
- **Cost:** about 4.3 ms per frame.
- The renderer takes `cx` (even) and `cy` as parameters, so the title can move the wheel if it wants.

## 7. Size and risks

**Flash:**
- map + spans: 561 B
- wheel orders: 75 B
- render code: about 1.2 KB
- RAMFUNC images: about 0.26–0.46 KB
- simulation + solver: about 0.9 KB
- swing: about 0.15 KB
- sound steps: about 0.1 KB
- **Total: about 3.0–3.4 KB**

**SRAM:**
- ring code: about 260 B
- optional LUT code: about 200 B
- live `Ball`: about 50 B
- LUT and dry run use no static memory (scratch and stack)

**Risks:**
1. **Landing-frame budget is about 8 ms.**
   - Move the LUT build to SRAM.
   - Start typing "17 RED" after the banner pops.
   - Last resort: bake the static bowl into a span4 sprite generated by `wheel.py`. That saves about 1.1 ms per frame for about +0.7 KB of flash.
2. **Scratch lifetime.** Nothing may touch the scratch between `buildLut` and `ring()`: no Mask, save, `textFx` or `sprite4Rot`. chsim exits with BUG if it is used during a flush.
3. **Determinism.** Keep a single `step()`, its own RNG, no frame-count or `fx::rnd` reads, and constants captured at spin start. The host test guards this.
4. **Even offsets.** An odd cx or gx breaks the nibble pairing. Assert in debug builds.
5. **The zero follows the felt theme** (§1.3). This needs the user's sign-off.
6. **Shimmer and strobing.**
   - Fret width flickers between 4 and 5 steps (512 steps halves it).
   - Keep the rotor under about 0.9 rev/s, or red and black start to strobe (about 1 pocket per frame).
7. **Near-side tucking** depends on CHGfx's integer ellipse rounding. Confirm it in the generated preview, which uses the same arithmetic.
8. **`wheelring` must stay a leaf** (`-msave-restore`). Verify with the map file or objdump.
9. **Event amounts.** BJ's `Event.amount` is int16. Widen it for 35:1 payouts (also noted in the blackjack report).

### Critical Files for Implementation
- `platform/board/arduino/CHGame/libraries/CHGfx/src/CHGfx_extras.cpp` (EllipseRows to copy in `wheel.py`; `gfx_fillEllipse`/`gfx_ellipse`)
- `platform/board/arduino/CHGame/libraries/CHGfx/examples/Demoscene/Demoscene.ino` (tunnel quadrant runs, part 2; avoid its `.srodata.ramfunc` FX macro)
- The sound engine, now the CHGame library's `chgame/Audio.cpp` (then CHChess's soft pulse flag and CHBlackjack's `blip` and priorities, each in its `src/audio/Audio.cpp`)
- `../CHChess/Iso.cpp` and the shake, now the CHGame library's `chgame/Shake.cpp` (RAMFUNC nibble-span conventions; `shiftRows`, once CHChess's)
- `../CHBlackjack/Table.cpp` / `Layout.h` and `../CHChess/Stage.cpp` (wall/rail band, glove with `RM_CPU`, `zoomDrawn` per-drawn-frame stepping, fade dip)