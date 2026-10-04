# CHRoulette animation, particles and "punch": reference for reusing CHChess and CHBlackjack

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

**Paths**: `BJ` = `../CHBlackjack`, `CH` = `../CHChess` (each game's code now sits beside its `.ino`). Functions are named rather than line numbers.

**Since then:** the two `fx` modules (section 2) and the palettes (section 3) are one now, in the CHGame library: easing, sine, `rnd()` and the shake in `chgame/Fx.h` (`Ease.cpp`, `Shake.cpp`), the particles, banner and floats in `chgame/Sizzle` (each game picks its kinds and extras with `SIZZLE_*` switches in its `Fx.h`), the palette in `chgame/Palette`, the frame timer in `chgame/Input`. The BJ/CH comparisons describe the copies as they were.

**CHGfx version**: both games need CHGfx **1.3.0**, which the board package brings (`platform/board/arduino/CHGame/libraries/CHGfx`). An older 1.1.0 copy, without `gfx_scroll`, `gfx_dither`, `gfx_sprite4`, `gfx_copyRow` or `gfx_fillEllipse`, is gone.

---

## 1. Frame pacing (copy as is)

**Fixed-rate timer**
- Then `src/CHGame.cpp` in both games, now the library's `chgame/Input.cpp`. `setFrameRate(fps)` sets `period = 1000000/fps` (16,667 us).
- `nextFrame()` uses a microsecond accumulator. If it falls more than 3 periods behind, it resyncs (`next = now + period`) instead of sprinting.
- `CHBJ_FPS` / `CHCH_FPS` = **60** (each game's `config.h`).

**Main loop** (`loop()` in `CHBlackjack.ino`; CH has the same in `frame::run()`, `Frame.cpp`):
```cpp
if (!chgame.nextFrame()) return;
uint8_t ticks = 0;
do { chgame.pollButtons(); pal::tick(); screens::update(); }   // logic at a fixed 60 Hz
while (++ticks < 3 && chgame.nextFrame());                     // catch up, at most 3 ticks per drawn frame
pal::commit(); gfx_wait(); screens::render(chgame.frameCount); gfx_flushAsync();
```
- Order differs slightly: CH calls `gfx_wait()` before `pal::commit()`; BJ commits first. Both are safe because CHGfx 1.3 stages the palette.
- **Advance anything that must keep real-time speed in `update()` (per tick)**, never in render.
- Exceptions that step per *drawn* frame:
  - BJ title, win and lose screens call `fx::update()` inside render (`titleRender()`, `winRender()`, `loseRender()`).
  - The CH whip-zoom steps once per drawn frame on purpose: the `zoomDrawn` gate (set in `stage::render()`, used in `stage::update()`).
- Sound: BJ calls `audio::update()` (the library's engine; LED patterns) inside `screens::update`; CH calls it in the frame tick.

**CHChess search bursts** (`CHChess/Frame.cpp`). These are chess-only (the engine runs synchronously and calls back every 8 nodes). Roulette has no search, so skip this. Constants:
- `FIRST_MS=300`, `SEARCH_MS=2000`, `BURST_MS=450`, `BOB_MS=133`, `TICK_MS=2000`.
- Frames drawn from the search run on a 1 KB `frameStack` (asm stack switch, `onFrameStack`).
- While idle it forces `frameCount |= 7` so the `frame>>3` bob still steps (`thinkFrame`).

**Screen transitions**
- `go(s)` sets `fadeOut = 8`.
- BJ: `setFade(fadeOut*2)`, then `enter(pending)` (`CHBlackjack/Screens.cpp`, `screens::update`).
- CH: `setFade((fadeOut-1)*2)` (`CHChess/Screens.cpp`, `screens::update`).
- Fade in: `fadeIn = 8`, then `setFade(16 - fadeIn*2)`.
- `enter()` always calls `fx::clear()`.

---

## 2. `fx` module (then `src/fx/Fx.{h,cpp}` in each game, 242 lines each, `#pragma GCC optimize("Os")`; now `chgame/Fx.h` and `chgame/Sizzle`)

Header rule (top of `chgame/Fx.h`): **integer maths only**. Soft-float trig once cost a demo 8.5 KB of flash and half its frame rate.

### 2.1 Easing and trig (identical in both, copy as is)
- `enum Ease : uint8_t { LINEAR, OUT_CUBIC, OUT_BACK, IN_OUT, OUT_BOUNCE };`
- `int ease(Ease e, int t, int n)`:
  - Returns Q8 0..256. Uses 17-point Q8 tables (`chgame/Ease.cpp`) with linear interpolation.
  - `t >= n` returns 256.
  - OUT_BACK peaks at 281. OUT_BOUNCE dips 248→193→249.
- `int isin(int a)`: `a` in 1/256 of a turn, returns −256..256. Quarter table `SIN[65]` (`chgame/Ease.cpp`). Cosine is `isin(a+64)`.
- `rnd()`: xorshift32, seed `0x1234567`, presentation only. `rndRange(lo, hi)` is exclusive of `hi`. `reseed()` is for debug determinism.
- Standard tween idiom:
  ```cpp
  x = x0 + (((x1-x0)*e) >> 8);
  y = ... - ((isin(t*128/T)*ARC) >> 8);   // half-sine hop
  ```

### 2.2 Particles
- `struct Particle { int16_t x, y; int8_t vx, vy; uint8_t life, colour, kind, age; };`
  - 10 B each. Pool `parts[48]` = **480 B SRAM**.
- Units: position is Q4 pixels. Velocity is Q4 px/frame, clamped to ±127 (about 8 px/frame).
- `spawn(k, x, y, vx16, vy16, life, colour)` takes the first free slot. If none, it **steals a random one** (`parts[rnd()%48]`).
- `burst(k, x, y, n, speed16, colour)`:
  - n particles at evenly spaced angles `i*256/n + rnd(0..11)`.
  - Speed is `speed/2 .. speed`; life is 20..39.
  - CH only: `DUST` has `vy/2`, so puffs spread along the floor.
- `fountain`:
  - BJ: `fountain(Kind k, x, y, n)`. CH: `fountain(x, y, n)`, confetti only.
  - x jitter ±4, `vx` −28..28, `vy` −60..−31, life 40..69.
  - Colours from `CONF[6] = {RED, GOLD, FELT_LT, CYAN, BLUE, WHITE}`. BJ's `COIN` kind is always `GOLD`.

Per-kind physics (`updateParticles`):

| Kind | Motion | Drawn as (`drawParticles`) |
|---|---|---|
| SPARK | `vy += 1` every 4 frames | pixel, plus a 5-px cross while `age < 8` |
| CONFETTI | `vy += 2` every other frame, capped at 24; `vx *= 15/16` | 2-px line, flips horizontal/vertical every 4 frames (flutter) |
| COIN (BJ only) | `vy += 3`/frame; bounces at y=122 with `vy = -vy/2` | 3×3 GOLD with a WOOD centre; 1 px wide when `((age>>2)&3)==2` (spin) |
| RAIN (BJ only) | constant velocity | 3-px vline |
| STAR | as SPARK | 3×3 plus |
| DUST | `vx, vy *= 7/8` (drag, no gravity) | BJ: pixel that blinks in the last 4 frames. CH: square of size `dust`, half size when life ≤ 10, blinking when ≤ 4 |

- `particlesAlive()` (BJ) is the same as `particles()` (CH).
- CH `drawParticles(uint8_t dust)` is called with `(2*tileH+2)/5`: 2 at zoom 5, 4 at zoom 10.
- CH exports `extern const uint8_t RAIN[5] = {RED, GOLD, FELT_LT, CYAN, BLUE}`, "the casino rainbow".

### 2.3 Banner (the signature "dancing gradient letters")

API: `banner(text, BannerStyle, cy, frames = 70)`, with `enum BannerStyle { B_RAINBOW, B_GOLD, B_RED, B_CYAN, B_WHITE }`. Text is at most 13 chars (`bannerText[14]`).

`drawBanner`:
- **Pop scale**: scale 2 for `t < 3`, 4 for `t < 7` (overshoot), then 3. It shrinks while width > 124.
- **Per-letter wave**: `dy[k] = (isin(t*10 + k*36)*2 >> 8) + 2`. That is ±2 px, phase 36/256 per letter, about 25.6 frames per cycle.
- Layout: centred at x=64, top at `cy - h/2 - 2`, with `h = 6*scale`.
- **Blink-out**: in the last 10 frames it is hidden whenever `bannerFrames & 2`.
- Row ramps:
  - RAINBOW: `RAIN[((r/2) + t/3) % 5]`, 2-row bands scrolling every 3 frames.
  - GOLD: `r < 3` FX_B (the pulsing highlight), then GOLD down to `h/2 + 6`, then WOOD.
  - RED: `r < 3` WHITE, then RED.
  - CYAN: `r < 3` WHITE, then CYAN.
  - WHITE: all white.
- Outline is `FX_A` (the palette rainbow, so it cycles for free) for RAINBOW, else INK. Shadow is INK for RAINBOW, else WINE.
- Mask-based rendering (now `chgame/Mask.*`, see 2.6).
- `activeRows` reserves `cy ± 18`.

**CH only**, `holdBanner(bool)` (now `SIZZLE_HOLD_BANNER`): while held, the banner freezes just before blink-out (`bannerFrames > 10` still decrements). `bannerT` wraps to 128 so the dance continues. Used for CHECK! and CHECKMATE! with "PRESS A".

### 2.4 Floating text (BJ only; now `SIZZLE_FLOATS`)
- `floatText(text, x, y, colour)`: 4 slots, `Float {int16 x, y; uint8 t, colour; char text[8]}` = 56 B.
- Lasts 50 frames, rises 1 px every 2 frames (25 px total), centred on x.
- INK drop shadow at (+1, +1). Blinks on odd frames when `t < 8`.

### 2.5 Screen shake
- `shake(frames, amp)`.
- Amplitude `a = (amp*shakeT + 9)/10`, minimum 1. It decays linearly. For example `shake(12,3)` starts at 4 px.
- `dy` alternates ±a every frame; `dx` is ±2 px, flipping every 2 frames.
- **Post-process on framebuffer rows after everything is drawn.**
  - BJ: `gfx_scroll(y0, y1-y0+1, (shakeT&2)?2:-2, dy)`.
  - CH: its own `RAMFUNC(shake) shiftRows`, word copies from SRAM, walking against the direction of travel.
- Perf notes:
  - memmove-based shake cost ~10 ms per frame (CH comment). CHGfx measured 6.3 ms against 1.0 ms for 118 rows (BJ comment).
  - `gfx_scroll` costs +164 B of flash compared with CH's own routine (`CHChess/docs/CHGfx-notes.md`, the table in "On CHGfx 1.3.0: shapes, sprites and row operations").
- Rows shaken: BJ `applyShake(0, TRIM_Y-1)` (the action bar stays still). CH `applyShake(10, 127)` (the HUD stays still).
- `activeRows`: a shake marks all rows 0..127 as active.

### 2.6 Supporting pieces
- `bool activeRows(int &lo, int &hi)`: the vertical extent of particles (y−2..y+3), floats and the banner. Drives partial redraws.
- `fx::clear()`, `fx::update()`: **one call per logic tick**.
- **Mask** (then `src/gfx/Mask.*`, now `chgame/Mask.*`):
  - A 1 bpp mask in `gfx_chunkScratch()` (1 KB, valid only between `gfx_wait()` and the flush; up to ~7000 px).
  - Rows are dilated for the outline and drawn as `gfx_hline` runs from SRAM (`RAMFUNC(maskruns)`).
  - Naive outlined text cost ~5 ms per line. Mask.cpp is 502 B of flash + 408 B of SRAM functions, against 1,014 + 462 for `gfx_textFx`.
  - **The signatures differed**: BJ `maskDraw(m, x, y, fill, outline=-1, shadow=-1, ramp=nullptr)` against CH `maskDraw(m, x, y, outline, shadow, ramp)` (ramp required). The library kept BJ's.

### 2.7 Reuse verdict
The fx module is fully game-agnostic. Build a merged version (done since: `chgame/Sizzle`):
- From CH: `holdBanner`, exported `RAIN`, `drawParticles(dust)`, DUST puffs, and the `shiftRows` shake (saves 164 B).
- From BJ: the `COIN`/`RAIN` kinds, `fountain(Kind, …)` and `floatText`.
- BJ's `bannerLen` is unused and can be dropped.
- The only game-specific line is COIN's floor at `122 << 4`.

---

## 3. Palette effects (free, with no redraw: every frame is re-flushed through the palette)

**Common to both** (then each game's `src/gfx/Palette.*`, now `chgame/Palette.*`):
- Palette enum: `INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE, GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B`.
- `pal::tick()`:
  - `FX_A` = `RAINBOW[12][(ticks/3)%12]`, a 36-frame cycle.
  - `FX_B` = a gold↔white triangle wave over 32 frames (`r=15, g=12+3t/15, b=2+13t/15`).
- Fades: `setFade(0..16)`.
- Themes: `setTheme(0..3)` rewrites FELT_DK, FELT and FELT_LT.

**BJ only**:
- `pal::flash(index, rgb444, frames)` overrides one index briefly. Used on Bust: `flash(WHITE, 0xFBB, 6)`, so every white pixel blushes pink for 6 frames.
- `setDesaturate(0..16)`. The Lose screen drains colour with `t/12`, up to 12.

**CH only**: `setMode(CASINO|TARGETS|HOVER)` (the library added `FIRE`).
- TARGETS: FX_A shimmers cyan→white (`SHIMMER[8]`) and FX_B pulses red→gold (`PULSE[8]`).
- HOVER: FX_A fades black→white→black over about a second.
- tick only marks the palette dirty when FX_A or FX_B actually moved (each commit costs a LUT rebuild).

**View-change dip** (CH `stage::update()`): `dipT = 8`, then `setFade(dipT > 4 ? 16-(8-dipT)*3 : 16-dipT*3)`. That is a V from 16 down to 4 and back over 8 frames.

**Other idioms**
- Highlight blink: `(frame & 16) ? FX_B : GOLD` borders on menus (BJ `titleRender()`, CH `menuItem()`).
- "PRESS A" blinks with `frame & 16` or `frame & 32`.

---

## 4. BJ Presenter: the event→motion layer (`CHBlackjack/Presenter.cpp`, 651 lines)

**Architecture** (copy the pattern; adapt the content):
- `Round` (pure logic) has `emit()` into a 16-slot ring of `Event {Ev type; uint8 a, b, c; int16 amount}` (`struct Event` in `Round.h`; `Round::emit`). Note: `amount` is **int16**.
- `present::onEvents(r)` drains the queue. `present::update(r)` runs per tick and calls `fx::update()` at the end.
- `Round::update(pressed, rep, present::busy())` (`playUpdate()` in `Screens.cpp`):
  - `if (wait) { wait--; return; }` first. `pace(frames)` is halved at FAST speed (`Round::pace`, `Round::update`).
  - Phases that must wait for the show do `if (fxBusy) return;`.
- `busy()` is true for: a card in flight or mid-flip, any `Fly` (including delayed ones with negative `t`), any `collectT`, or `shuffleT`. **Particles, banners and floats do NOT block.** They overlap the next step.
- Logic pacing constants (top of `Round.cpp`): `T_DEAL=14, T_PEEK=50, T_PEEK_RESULT=30, T_BUST=50, T_SETTLE=40, T_REVEAL=22, T_SHUFFLE=70, T_SPLIT=16, T_END=30`.

### 4.1 Chip flights (most reusable for roulette)
- `enum FlyKind { CHIP_IN, CHIP_OUT, STACK_TO_TRAY, STACK_TO_PLAYER }`.
- `struct Fly { int16 x0, y0, x1, y1; int16 t; uint8 T, denom, seat, kind; int32 value; }`: 20 B × `flies[16]` = 320 B.
- `fly(x0, y0, x1, y1, denom, seat, value, kind, delay=0, T=14)`:
  - `t` starts at `-delay`.
  - If the pool is full, the end effect is applied immediately.
- Path (`flyY`, `overlay`):
  - x is OUT_CUBIC.
  - y is OUT_CUBIC minus `isin(t*128/T)*10 >> 8`, a **10 px hop**, clamped below the rail.
  - `CHIP_IN`/`CHIP_OUT` draw `art::chip(x, y, denom, true)`. Stacks draw `art::chipStack(x, y, value, 4)`.
- `chipsIn(r, seat, amount, fromX, fromY, stagger)`:
  - Splits the amount greedily into denominations `{1, 5, 10, 25, 100}` (`art::chipDenom`), up to 8 flights.
  - Each flight launches from `fromX + k*3` with delay `k*stagger`. The 8th carries the remainder.
- On landing (`present::update`):
  - `CHIP_IN`: `dispBet[seat] += value`. **The stack visibly grows as each chip lands.**
  - `STACK_TO_PLAYER`: `pending -= value; purseFlash = 24; Sfx::Coin`.
- `sweep(r, seat, kind)`: the whole displayed stack flies as one object and `dispBet` is zeroed at once.
  - To the tray: target `(TRAY_X + TRAY_W/2, RAIL_Y + RAIL_H + 10)`, T=16.
  - To the player: target `(8, 140)` (off-screen, bottom left), T=18.
  - Plays `Sfx::Whoosh`.
- `collectT[seat]` (`present::update`): counts down **only while no CHIP_IN for that seat is in flight**, then sweeps to the player.
  - Win: 40. Push: 30. Insurance: 36.

### 4.2 Money roll-up (copy as is)
In `present::update`:
```cpp
int32_t target = r.purse - pending, d = target - shown;
if (d) { int32_t step = d/5; if (!step) step = d>0?1:-1; shown += step;
         if (d > 0 && (shown & 3) == 0) audio::blip(3000 + (uint16_t)((shown*7) & 511), 8); }
if (purseFlash) purseFlash--;
```
- The rules credit the purse immediately. `pending` keeps the display back until the chips physically arrive.
- The roll covers ~20% of the remaining gap per frame, with a rising tick sound.
- The plaque colour is `flash ? ((flash & 4) ? WHITE : GOLD) : GOLD`, drawn double-struck (`table::plaque` in `Table.cpp`).

### 4.3 Cards (CHRoulette has no cards; the patterns still apply)
- `FLIGHT=12`, `FLIP=8`. `CardView {int16 x, y, sx, sy; uint8 t, flip; bool live, up}` × `[3][12]`.
- Deal tween (`present::update`): OUT_CUBIC with an arc of `isin(t*128/12)*3 >> 4` Q4 (~3 px). On landing, `burst(DUST, tx+11, ty+27, 4, 20, FELT_LT)`.
- After landing: `x += (tx-x)/3` exponential retarget, snapping within 3 Q4.
- Flip (`present::update`, `drawHand`): width squashes to 2 px and back, face switches at `FLIP/2`, plays `Sfx::Flip`.
- Ghosts: cleared cards slide left 56 Q4 (3.5 px) per frame for 14 frames.

### 4.4 Ambient life
- Speech typewriter: 1 char/frame, `blip(1900 + (n*97)%700, 12)` on odd chars, hold 100 frames.
- Blink every 90..219 frames, lasting 6.
- Pupils track the moving card's x (`present::update`).
- Peek: `peekT = 48`, card lifted 3 px during 10..40, `Sfx::Peek` every 12 frames.
- Shuffle riffle: `shuffleT` 70 (6 decks) or 20.

### 4.5 Event recipes (where each effect fires, with parameters)

| Event (in `present::onEvents`) | Effects |
|---|---|
| Bust | `banner("BUST!", B_RED, PLAYER_CARDS_Y+8)` (70f); `shake(12,3)`; `burst(SPARK, cx, 95, 12, 48, RED)`; `pal::flash(WHITE, 0xFBB, 6)`; `sweep(STACK_TO_TRAY)`; `floatText("-$N", BET_CX, BET_CY-18, RED)`; `Sfx::Bust` |
| Natural | `banner("BLACKJACK!", B_RAINBOW, 64, 90)`; `fountain(CONFETTI, cx, 83, 24)`; `burst(STAR, cx, 93, 10, 50, FX_A)`; `Sfx::Blackjack`; `LED_TRIPLE` |
| Hand21 | `banner("21!", B_GOLD, 91, 50)`; `burst(SPARK, cx, 95, 10, 40, GOLD)` |
| Settle win | `pending += amt`; `chipsIn(win, from tray, stagger 3)`; `collectT = 40`; `floatText("+$N", x, y-18, GOLD)`; `banner("WIN!", B_GOLD, 91, 60)` or `"3 TO 2!"` B_RAINBOW 70; `fountain(CONFETTI, cx, 87, 14)`; `LED_BLINK` |
| Settle push | `collectT = 30`; `banner("PUSH", B_CYAN, 91, 50)` |
| Settle lose | `sweep(TRAY)`; `floatText("-$N", RED)`; `Sfx::Lose` |
| Dealer BJ | `banner("DEALER BJ", B_RED, 62, 70)`; `shake(10,2)` |
| DealerBust | `banner("BUST!", B_GOLD, 60, 60)`; `burst(SPARK, cx, 60, 12, 40, GOLD)` |
| Split | `burst(SPARK, 48, 93, 10, 40, FX_A)` |
| BetAdd / BetRemove | chip flies from bar slot `(9 + idx*17, 118)` to the stack; returns to `(20, 136)`; `Sfx::Chip` |

### 4.6 Partial redraw (essential for 60 fps)
- `Sig` FNV-1a hash (`struct Sig`) over everything each band shows: wall (0..45) and felt (46..110).
- `movingRows` = `fx::activeRows` ∪ cards in flight ∪ ghosts ∪ flies ∪ peek.
- A band redraws if its hash changed or a mover **touched it this frame or last** (`present::render`). This erases trails.
- `rowsMoving(a, b)` tells the action bar to redraw when a chip crosses it (`playRender()` in `Screens.cpp`).
- `overlay()` runs **only if something was drawn**. Each frame is still flushed, so the palette keeps animating.

**Draw order**
- `render`: wall, dealer, bubble/plaque, shoe, rail → felt fill, print, `FX_B` bet ring in InitBet, hands with badges, `chipStack(dispBet)`, gold trim → `bar::draw`.
- `overlay`: flying chips/stacks → ghosts → `drawParticles` → `drawFloats` → `drawBanner` → `applyShake(0, TRIM_Y-1)`.
- Then `Screens.cpp` adds the DEMO label, pause panel and toast. These are not shaken.

**Measured cost**: a bust frame with shake and banner took 25 ms to draw on CHGfx 1.2 and **11 ms** now (`CHBlackjack/NOTES.md`).

---

## 5. CHChess Stage (`CHChess/Stage.cpp`, 880 lines)

### 5.1 Busy and waiting gates
- `busy()`: `mv[0].on || mv[1].on || fly.on || holdT || picking || topT || handT || outWait || waitPress || fx::particles() || (tileH != zoomTo && !flat)`.
  - **Here particles DO block.** The next move waits for the dust.
- `stage::update` drains match events only while `!busy()`; `EV_START` cuts in.
- `match::update(stage::busy())`, and input only `if (humanToMove() && !busy())` (`playUpdate()` in `Screens.cpp`).
- `holdT` is a "keep the game waiting" frame counter. Landing gives ≥12 frames, promotion 30, check 40, mate 80, CPU turn start 24.
- `waiting()` / `acknowledge()`: CHECK! against you holds the banner until any button (`playUpdate()`). "PRESS A" plate blinks once `holdT < 20 && (frame & 32)` (`drawHud()`).

### 5.2 Piece movement
- `Mover {piece, from, to, t, T, arc, delay, on, sound}`. `launch` sets `T = (fast ? 10 + d*2 : 14 + d*3) * slowF`, with d the Chebyshev distance.
- `moverPos`: IN_OUT ease. Arc:
  - Knight: arc 12 sine hop, `z = arc*isin(t*128/T) >> 8`.
  - Others: flat-top lift of 3 px, ramping over 4 ticks at each end.
- A shadow ellipse stays on the board under the piece: `fillEllipse(sx, sy, zoomed(3), (tileH+2)/5, INK)`.
- Castling rook: delay 10, arc 10.

**Landing** (`land`):
- Capture:
  - `burst(SPARK, x, y-up/2, 12, 36, GOLD)` + `burst(STAR, …, 4, 24, WHITE)`.
  - `shake(10,2)`, `Sfx::Capture`; launches the Flyer.
- Normal: `shake(4,1)`, `Sfx::Land`.
- Always: dust `burst(DUST, x, y, 16, zoomed(30), squareColour)`, where `dust()` uses the square's own colour so it blends in.
- Promotion: `burst(STAR, x, y-up, 12, 36, GOLD)`, `holdT = 30`.
- Rook landing: dust 12 at speed 24.

### 5.3 Capture in half-speed slow motion and the tumbling flyer
- `CAPTURE_SLOW = 2` → `slowF` multiplies the mover's T. `fly.slow` divides time (`onMove`).
- `flyAt`, Q4:
  ```cpp
  t4 = fly.t*16/slow;  v = dir*zoomed(16);
  z = (zoomed(44)*t4/16 - zoomed(4)*t4*(t4-16)/512)/16;   // parabola up then down
  sx = x + v*t4/256;  sy = y + v*t4/768 - z;  ang = dir*11*t4/16;   // spins 11/256 turn per tick
  ```
- Drawn with `spriteRot`. For the first `4*slow` ticks it uses `RM_HIT` (an all-white silhouette flash).
- Removed after `90*slow` ticks or once off-screen (`stage::update`, `drawPieces`).
- Remap-table flashes (the `RM_*` tables): `RM_HIT` (white), `RM_PREY` (red), `RM_CPU`, `RM_ALERT`. One sprite gives many looks.

### 5.4 Whip-zoom camera
- `iso::tileH` ranges 5..10. `zscale() = tileH*256/5`, `zoomed(px)` (`Iso.h`).
- `onMove`: if not flat and not fast, `zoomTo = 10` + `Sfx::Whoosh`.
- `update`: `tileH` steps ±1 **once per drawn frame** (`zoomDrawn` is set in `render`).
- `setZoom(h)` rescales every world-space Q4 coordinate by `h/old`, so things keep their place.
- After landing, `outWait = zoomTo > 5 && !mate` (`land`). It clears once `!fx::particles()` → `zoomTo = 5` (`update`).
- Zooming out calls `snapCamera()` per step: "clean steps, no wobble" (`update`).
- Camera ease (`stepCamera`): `cx16 += dx >> aimShift`. Shift 1 = fast (hand-over), 2 = normal, 3 = slow drift. Snaps within 1 px.
- `aimAt` framing leans towards the board centre less the further zoomed in, clamped to content bounds.
- Finger glides at `>> 1` per tick (`update`).

### 5.5 Other punch in CHChess
- Mate topple (`drawPieces`): `topT` 1..60, `ease(OUT_BOUNCE, topT, 30)` × 60/256 of a turn, via `spriteRot`.
- Mate itself (`onOver`):
  - `banner("CHECKMATE!", B_RAINBOW, 34, 170)` held; `holdT = 80`; zoom in.
  - Human win: `LED_PARTY` + `fountain(40, 90, 20)` + `fountain(88, 90, 20)`.
  - Other results: B_CYAN or B_WHITE for 150 frames.
- CHECK! (`onCheck`): `banner B_RED cy 36` 70 frames, `LED_TRIPLE`, `holdT = 40`.
- 2-player hand-over (`update`):
  - Ticks 1..11: aim at the centre with shift 1.
  - Tick 12: flip the camera, `dipT = 8`, `banner("BLACK TO MOVE", B_GOLD, 40, 60)`.
  - Done at tick 41.
- Announcement plate (`announce`, `plate`, `drawHud`), `ANN_FRAMES = 120`:
  - The plate opens with `ease(OUT_BACK, t, 8)` and closes linearly over the last 8 frames (`(120 - t)*32`).
  - Word k drops in at frame `6 + 3k`, falling 3 px over 3 frames.
  - Rounded NAVY panel with GOLD border; words coloured per word, e.g. "ROOK" WHITE, "TAKES" RED, "A4" GOLD.
- Selected-piece bob: `y -= zoomed(3) + (zoomed(isin((frame>>3)*48)) >> 8)`. Outline `fx::RAIN[(frame>>3)%5]` (`drawPieces`).
- Check heartbeat: `!(((frame>>3)+5)&5)` gives a lub-dub. Glove bob is `isin((frame>>3)*40)*2>>8`.
- Tap: `tapT` 1..12, offset `(tapT < 6 ? tapT : 12 - tapT)/2`. `denyT = 24` flashes `RM_ALERT` on `denyT & 4`.

### 5.6 Redraw gating and draw order
- `signature()` returns `frame` (always new) while anything moves: `activeRows`, movers, flyer, topple or the camera easing.
- Otherwise it hashes the state including `frame >> 3`. An idle board redraws at 7.5 Hz; palette effects still run at 60.
- Draw order (`stage::render`): table → board → tile overlays → depth-sorted pieces (movers inserted at their interpolated depth `md[k]`, flyer last) → finger → HUD → `drawParticles(dust)` → `drawBanner` → `applyShake(10,127)`. `Screens.cpp` then adds the hold bar and pause/promo/result panels, not shaken.

**Measured costs** (`CHChess/docs/CHGfx-notes.md`, "On CHGfx 1.3.0: shapes, sprites and row operations"):
- Full redraw: 6.0 ms normal, 7.8 ms zoomed.
- Whip-zoom frames: 6.1 ms average, 8.6 ms worst.
- Async flush: **2.5-3.2 ms of CPU** per frame (CHGfx 1.3 header).

---

## 6. Screen-level showpieces (BJ `Screens.cpp`)
- **Title card deal** (`titleRender()`): card i starts at frame `i*10`; `ease(OUT_BACK, k, 18)` from y = −30; flips at k > 12 with `Sfx::Flip`. At `t == 60`: `burst(STAR, 64, 52, 14, 60, FX_A)`.
- **Win screen** (`winRender()`):
  - 16-ray sunburst from (64, 60), rotating `frame & 255`, colours `FX_A`/`WINE` (`ray()` is a clipped Bresenham).
  - Bitmap lettering with ramp `R[((i/3) + frame/4) % 5]`.
  - Every 6 frames `fountain(COIN, rnd(20..108), 120, 2)`; every 24 `burst(STAR, rnd, rnd, 16, 60, FX_A)`; every 30 `fountain(CONFETTI, rnd, 100, 10)`.
  - `LED_PARTY`.
- **Lose screen** (`loseRender()`): desaturate `t/12`, capped at 12. Lettering drops in (`drop = 40 - t`). Two RAIN particles per frame (`spawn(RAIN, rnd x, -4, -6, 64, 40, CYAN)`).
- **Still-screen gate**: `unchanged(sig)` returns false while `particlesAlive()`.
- **Toast**: 60 frames, panel at y = 2.
- **Accordion action bar** (`layout()` in `Bar.cpp`): `widthQ4 += (target - w)/3`; the selected button is 40 px wide against 26, lifted 1 px, with an `FX_B` border.

---

## 7. Mapping to CHRoulette needs

| Roulette moment | Reuse |
|---|---|
| **Ball spin** | No orbit code exists. Build it from `isin`/`ease`: angle `a(t) = a0 + total*ease(OUT_CUBIC, t, T) >> 8`; ball at `(cx + isin(a+64)*rx >> 8, cy + isin(a)*ry >> 8)`; radius shrinking with `ease(IN_OUT)` as the ball drops inward. A trail is `spawn(SPARK, x, y, 0, 0, 6, WHITE/SILVER)` per frame (watch the 48-particle pool). Optional CH-style whip: scaled `sprite4` / `zscale` on the wheel, stepped once per drawn frame. `Sfx::Whoosh` at launch; FX_A/FX_B on the wheel rim animate for free. |
| **Ball landing / pocket hops** | A Chess-flyer parabola (`flyAt`) per bounce, damped with `ease(OUT_BOUNCE)`. `shake(4,1)` per hop and `shake(10,2)` on the final settle; `burst(DUST, x, y, 4..16, ~20..30, pocketColour)` (the "own colour" trick); a white flash via a `RM_HIT`-style remap or `pal::flash` on the pocket index. Hold the camera with an `outWait` until `!particles()`. Call out the number with the chess `plate()` (word drop-in, e.g. "17" RED, "RED" WHITE, "ODD" SILVER) plus a `banner("17", B_RED or new B_BLACK/B_GREEN style, cy)`. |
| **Chips sliding** (betting, payout) | `Fly` + `chipsIn` + `art::chip`/`chipStack` as they are (14-frame OUT_CUBIC with a 10 px hop; set the hop to 0 for a felt slide). Stagger 2-3 frames; `Sfx::Chip`. |
| **Win payout** | BJ Settle win: `pending += amount`; `chipsIn` from the dealer rack with stagger 3; `collectT` (≈40) then `sweep(STACK_TO_PLAYER)` → `purseFlash = 24` + `Sfx::Coin` + rolling purse (`d/5` per frame with `blip` ticks); `floatText("+$N", GOLD)`; `banner("WIN!", B_GOLD, cy, 60)`; `fountain(CONFETTI, x, y, 14)`; `LED_BLINK`. |
| **Big win** (straight-up 35:1) | BLACKJACK! recipe: `banner(…, B_RAINBOW, 64, 90)` + `fountain(CONFETTI, 24)` + `burst(STAR, 10, 50, FX_A)` + `LED_TRIPLE`, or CHECKMATE!: two fountains of 20 + `LED_PARTY`, `holdBanner` + PRESS A. Add Win-screen `fountain(COIN, …)` rain and a sunburst for the biggest. |
| **Losing chips swept** | `sweep(STACK_TO_TRAY)` (T=16, Whoosh, `dispBet` zeroed at once) per losing spot, staggered with `delay`; `floatText("-$N", RED)`; `Sfx::Lose`. Heavy loss: Bust recipe (`shake(12,3)` + `pal::flash(WHITE, 0xFBB, 6)` + RED SPARK burst). A ghost-style slide (−3.5 px/frame for 14 frames) works for a rake. |
| **Gating** | BJ model: `Round::update(…, present::busy())` with `pace()` waits and `if (fxBusy) return;` at spin-start, landing and settle. `busy()` covers the ball, flies and `collectT`. Banners and particles are non-blocking unless you add CH's `fx::particles()` / `holdT`. |

**Budgets**
- SRAM:
  - Particles: 480 B.
  - Flies: 320 B.
  - Floats: 56 B.
  - Mask/rotation scratch: free (chunk buffers).
- Flash:
  - BJ uses ~45.6 KB of 50,944 B. CH release is 48,628 B flash and 17,872 B SRAM.
  - Keep `#pragma GCC optimize("Os")` on cold code. Hot per-pixel loops need `RAMFUNC(name)` (the CHGame library's `chgame/RamFunc.h`; section `.gnu.linkonce.r.app.<name>`) because flash runs with 3 wait states (2-3 us per call per pixel).
  - Avoid `memmove`/`memcpy` on framebuffer rows, `snprintf` (3.5 KB) and float.