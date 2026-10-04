# Screens, UI flow, input and app skeleton in CHChess and CHBlackjack (for building CHRoulette)

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

Paths are relative to `examples/Games/` (the games sit side by side there); functions are named rather than line numbers. What both games carried copies of (input and pacing, palette, drawing, Mask, Fmt, the effects, sound, saving, the debug protocol, `RAMFUNC`) is now the CHGame library's `chgame/`. Both games use one palette (index names `INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE, GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B`), and their `Palette.h` files are identical apart from Chess's `setMode`.

**Main recommendation:** copy CHBlackjack's skeleton, not Chess's. That means its `.ino` loop, `CHGame.*`, `Screens.cpp` shape (`go`/`enter`/fade, still-screen signature, options string table, stats with hold-to-reset, toast, pause panel, win/lose), and `Bar.cpp` as the bottom action bar. Chess's `Frame.cpp` and `holdFrames()` only exist because its CPU search blocks the loop; roulette does not need them.

---

## 1. App skeleton

### 1.1 `.ino` and frame loop

**`setup()` and `loop()` in CHBlackjack/CHBlackjack.ino** (the template to copy):
```cpp
void setup() { chgame.boot(); gfx_begin(GFX_DIV2, GFX_12BPP); pal::init(); screens::begin(); chgame.setFrameRate(CHBJ_FPS); }
void loop() {
    dbg::poll();
    if (!chgame.nextFrame()) return;
    dbg::markUpdateStart();
    uint8_t ticks = 0;
    do { chgame.pollButtons(); pal::tick(); screens::update(); }
    while (++ticks < 3 && chgame.nextFrame());   // fixed 60 Hz logic, up to 3 catch-up ticks
    pal::commit();                  // CHGfx stages it: lands with the next flush
    dbg::markWaitStart(); gfx_wait();
    dbg::markRenderStart(); screens::render(chgame.frameCount); dbg::markRenderEnd();
    gfx_flushAsync();
}
```
- Logic and input run while the previous frame is still going out over DMA. Drawing happens only after `gfx_wait()`, because there is one framebuffer.
- In debug builds `debugHook` adds three commands: `R <seed>`, `D <cards>` and `J <T|P|W|L|O|S|C>` (jump to a screen). It is installed with `dbg::hook = debugHook`.

**CHChess/CHChess.ino** hands the loop to `frame::run(false)` in **CHChess/Frame.cpp**. Two differences from Blackjack:
- `audio::update()` runs inside the tick loop.
- It calls `gfx_wait(); pal::commit();` in that order.

Frame.cpp also re-enters the frame from inside the engine search (`thinkPoll`) on a separate 1 KB stack. Constants there: `FIRST_MS=300, SEARCH_MS=2000, BURST_MS=450, BOB_MS=133, TICK_MS=2000`. This is Chess-only.

### 1.2 `config.h` (copy and rename the prefix)

**CHBlackjack/config.h**:
- `CHBJ_VERSION "1.0"`
- The debug switch was `CHBJ_DEBUG` (1 under `CHSIM`); it is now the CHGame library's `CHGAME_DEBUG` (and `CHGAME_PROFILE`), from `chgame/Config.h`.
- `CHBJ_LEAN`: `CHGAME_DEBUG && !CHSIM && !CHBJ_FULL`. Device debug builds drop the credits page; every debug build has no music scores.
- `CHBJ_FPS 60`
- Comments give the costs: the debug protocol is about 1.8 KB; Peripherals "Game" saves about 3.4 KB.

**CHChess/config.h** has the same structure with the `CHCH_` prefix. Its debug protocol is about 2 KB, and its LEAN build drops saving and the options screen. A roulette game would use `CHRL_`.

### 1.3 `CHGame.h/.cpp`: input and timing

The two copies were identical except for one comment; they are now the CHGame library's `chgame/Input.h/.cpp`, included through `<CHGame.h>`.

Button masks, parenthesised so `~X` and `A|B` behave:

| Mask | Value |
|---|---|
| `A_BUTTON` | `1u<<0` |
| `B_BUTTON` | `1u<<1` |
| `UP_BUTTON` | `1u<<2` |
| `DOWN_BUTTON` | `1u<<3` |
| `LEFT_BUTTON` | `1u<<4` |
| `RIGHT_BUTTON` | `1u<<5` |
| `START_BUTTON` | `1u<<6` |
| `SELECT_BUTTON` | `1u<<7` |

API on `class CHGame`, with a global `CHGame chgame`:
- `pressed(b)`: all of `b` held. `anyPressed(b)`.
- `justPressed(b)`: an edge on any bit of `b`. `justReleased(b)`. `justPressedMask()`.
- `repeat(b, delay=18, rate=5)`: true on the press frame (`held==1`), then every 5 frames once held past 18 frames. Used for menus and bet adjustment.
- `buttons()`, `clearButtonState()`, `everyXFrames(n)`.
- Public fields: `frameCount` (uint32), `injected` (ORed into the real buttons), `lockstep` (-1 runs free; the simulator starts at 0).

Implementation, the library's **`chgame/Input.cpp`**:
- Buttons are read straight from the GPIO input registers (`GPIOB/GPIOC->INDR`). Pins: A=PB1, B=PB6, UP=PB4, DOWN=PC14, LEFT=PB3, RIGHT=PC15, START=PB8, SELECT=PB7.
- Pull-ups are set by register writes. The comment says `pinMode` would drag in about 2 KB of pin-map tables.
- `nextFrame()` uses a microsecond accumulator (`period = 1000000/fps`, so a true 60.0 fps). If it falls more than 3 periods behind it resyncs instead of sprinting.
- `pollButtons()` keeps a held-frame counter per button (`uint16_t held[8]`).

### 1.4 Shared module shape

Every cold-code `.cpp` file starts with `#pragma GCC optimize("Os")`. Each module is a `namespace` with `begin/update/render`. `Screens.h` exposes the following.

**Blackjack** (`CHBlackjack/Screens.h`):
```cpp
void begin(); void update(); void render(uint32_t frame);
void debugSeed(uint32_t); void debugStack(const uint8_t*, uint8_t); void debugJump(char);
```

**Chess** (`CHChess/Screens.h`):
```cpp
void begin(); void update(bool thinking); void render(uint32_t frame);
bool holdFrames();   // Chess-only: keep frame bursts going mid-search
```
`holdFrames()` is defined in Chess's `Screens.cpp`: `cur==Scr::Play && (overlay!=NONE || bHeld)`. Roulette does not need it.

**Other shared modules:**
- `Fmt.h` (now `chgame/Fmt.h`), with no printf (snprintf costs about 3.5 KB): `fmtInt`, `fmtMoney` (gives `"$1234"` and `"-$5"`), `fmtStr`. They chain: `p = fmtStr(p,"BET "); p = fmtMoney(p,bet);`. Fmt.cpp is identical in both games.
- `RamFunc.h` (now `chgame/RamFunc.h`): `RAMFUNC(name)` puts a function in section `.gnu.linkonce.r.app.<name>` with `noinline`, so it runs from SRAM (the games' copies used `chbj`/`chch` in place of `app`).

---

## 2. Screen state machine

### 2.1 Common idiom: `go()` → fade out → `enter()` → fade in

**CHBlackjack/Screens.cpp, `go()`, `enter()` and `screens::update()`:**
```cpp
static void go(Scr s) { if (fadeOut) return; pending = s; fadeOut = 8; }
// update():
t++;
if (toastT && !--toastT) redrawAll();
if (paused != wasPaused) { wasPaused = paused; redrawAll(); }
audio::update();
if (fadeOut) { pal::setFade(fadeOut*2); if (--fadeOut == 0) enter(pending); return; }  // input frozen while fading
if (fadeIn)  { fadeIn--; pal::setFade(16 - fadeIn*2); }
switch (cur) { ...Update(); }
```
- `enter(s)`:
  - Sets `cur=s; t=0; fadeIn=8; creditsReady=false; fx::clear(); pal::setDesaturate(0); pal::setCycling(true);`.
  - Then runs per-screen setup: music start/stop, `present::reset`, `bar::reset`, `redrawAll`, or `audio::led(LED_PARTY)` on Win.
- A transition takes 8 frames out and 8 in, about 267 ms. During the fade-out the old screen keeps rendering; only the palette dims.
- Chess (`go()`, `enter()`, `screens::update()` in its `Screens.cpp`) is the same, except:
  - It uses `pal::setFade((fadeOut-1)*2)`, so the fade reaches full black. Blackjack stops at level 2. Copy Chess's version.
  - `enter()` also resets the shared `sel=0`, calls `stage::invalidate()` and `pal::setMode(pal::CASINO)`, and Setup starts with `sel=2` on BEGIN.
- `t` (uint16) counts frames on the current screen. It drives deal-in animations, the "PRESS A" delay (`t>60`) and splash timing.

### 2.2 CHBlackjack screens and transitions

`enum class Scr : uint8_t { Splash, Title, Play, Options, Stats, Win, Lose, Credits };`

```
Splash --(any press | t>360)--> Title
Title  [menu: PLAY | (CONTINUE $purse, NEW GAME) | OPTIONS | STATS]
   A/START on PLAY/NEW ------> Play (game.newGame())
   A/START on CONTINUE ------> Play (game.resume())
   A/START on OPTIONS -------> Options (optBack=Title)
   A/START on STATS ---------> Stats
   10 s idle (600 frames) and the tune has ended --> Play in demo mode (enter() directly, no fade)
Play
   START --------------------> pause overlay [RESUME | OPTIONS | SAVE & QUIT]
        RESUME / B / START -> close the overlay
        OPTIONS ------------> Options (optBack=Play, resumePlay=true: the table is not reset)
        SAVE & QUIT --------> persist(purse>0), Title
   phase GameWon ------------> Win   (persist(false), hasGame=false)
   phase GameLost -----------> Lose  (persist(false))
   phase Quit (QUIT button) -> Title (persist(true))
   demo: any press ----------> save::load (undo the demo), Title
Options -- B, or A on BACK --> persist(hasGame), go(optBack)
Stats   -- A --> Credits;  B --> Title;  hold SELECT 90 frames -> wipe stats
Credits -- A or B --> Stats
Win/Lose -- after t>60, A/START --> stopMusic, Title
```

### 2.3 CHChess screens and transitions

`enum class Scr : uint8_t { Title, Setup, Play, Options };`. The header comment (`Screens.h`) still mentions stats and credits screens, but the code has none. The win/loss record lives on the Setup screen.

```
Title (live board behind, camera drifting) [ (CONTINUE) | 1 PLAYER | 2 PLAYERS | OPTIONS ]
   A on 1 PLAYER -> Setup;  2 PLAYERS -> Play;  CONTINUE -> Play (save::loadGame, else Deny sfx);  OPTIONS -> Options
Setup ("OPPONENT") rows: [level <> | side <> | BEGIN]
   A on a row: moves to the next row;  A on BEGIN: persist, then Play;  B -> Title;  hold SELECT 90 frames: wipe this level's record
Play  overlays: enum Overlay { NONE, PAUSE, PROMO, RESULT }
   START -> PAUSE [RESUME | UNDO | RESIGN | SAVE + QUIT]   (START = resume, B = close)
   PROMO (pawn promotion reel): LEFT/UP previous, RIGHT/DOWN next (wraps &3), A confirm, B cancel
   RESULT: A -> rematch (sides swap) / play again;  B -> Title  (both persist(false))
   stage::waiting() (CHECK!/CHECKMATE!): any button acknowledges
Options -- B, or A on BACK --> persist, go(optBack)
```
Chess overlays are state within the Play screen, not separate screens. The result panel appears only once `stage::overShown()` is true (`playUpdate()`).

---

## 3. Button conventions

| Button | Menus | In play (BJ) | In play (Chess) |
|---|---|---|---|
| D-pad UP/DOWN | move the cursor (`repeat`) | bet bar: UP adds the chip, DOWN removes it; play bar: UP=HIT, DOWN=STAND; insurance: amount | move the glove |
| LEFT/RIGHT | change an option's value (`justPressed`, wraps) | move along the bar (`repeat` in betting, skips disabled slots, no wrap) | move the glove |
| A | confirm; on an option row it cycles the value +1 | press the selected button; on a chip, add it (repeat) | pick up / place |
| B | back (persist first); also closes overlays | betting: take the chip back; otherwise dismiss the dealer's speech bubble | tap: put the piece back; hold 32 frames: inspect zoom |
| START | also confirms on BJ title and end screens | pause; also DEAL in the bet bar and NEXT HAND on the end bar | pause, and resume from pause |
| SELECT | hold 90 frames (1.5 s) = reset stats | mute toggle plus toast "SOUND OFF"/"SOUND ON" (`playUpdate()`) | cycle view |

Sounds are consistent across both games:
- Cursor move: `Sfx::Cursor`.
- Confirm, back, pause open/close: `Sfx::Select`.
- Changing an option value: `Sfx::Chip` (BJ) / `Sfx::Coin` (Chess).
- Not allowed: `Sfx::Deny`.
- Promo reel step: `Sfx::Flip`.
- Splash: `audio::blip(2600,60)` at `t==1`; `blip(1800,40)` when skipped.

The rules layer (`Round`) never calls audio. It emits `Ev::Cursor` and `Ev::Deny`, and the presenter turns those into sounds (`present::onEvents`).

Play-input plumbing in BJ (`playUpdate()`):
```cpp
uint8_t pressed = chgame.justPressedMask(), rep = 0;
static const uint8_t RB[6] = {LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, A_BUTTON, B_BUTTON};
for (i..6) if (chgame.repeat(RB[i])) rep |= RB[i];
...
game.update(pressed & ~START_BUTTON, rep, present::busy());   // the rules wait on the animation
present::onEvents(game); present::update(game);
if (game.phase != lastPhase) { ...persist / go(Win|Lose|Title)... }
```
The rules get raw `pressed`/`repeat` masks plus `fxBusy`. `Round::update` returns early while `wait` (pacing) is running, and phases such as `Shuffle`, `InitDeal` and `OverallWinOrLose` do `if (fxBusy) return;` (`Round::update`). This rules → events → presenter split is the pattern to reuse for the wheel.

**Attract/demo mode** (BJ `titleUpdate()`, `startDemo()`, `demoInput()`):
- Title: `idle = buttons() ? 0 : idle+1; audio::loopMusic(idle <= 600);`. When `idle > 600` and the music has stopped, `startDemo()` runs.
- `demoInput()` produces one synthetic press every 30 frames (`cool=29`). A lambda `toward(target)` returns LEFT, RIGHT or A to walk `game.sel` to the wanted slot.
- In the demo, any real press reloads the save and returns to Title. The demo blinks a "DEMO" label: `fillRound(44,50,40,11,3,INK)`, then `centred57(52,"DEMO",FX_A)` when `frame&32`.

---

## 4. Menu and panel visuals (exact geometry)

### 4.1 Shared drawing helpers (copy them)

**BJ and Chess `Screens.cpp`, `feltBackdrop()` and the helpers after it:**
```cpp
static void feltBackdrop() {           // every menu screen
    gfx_clear(FELT);
    dither(0,0,128,6,FELT_DK,0); dither(0,122,128,6,FELT_DK,1);
    dither(0,0,6,128,FELT_DK,0); dither(122,0,6,128,FELT_DK,1);
    gfx_rect(2,2,124,124,GOLD);
}
```
- BJ uses `gfx_dither` from CHGfx. Chess used its own `dither` from its `Draw.h` (now the library's `chgame/Draw.h`).
- The flat felt area is 6..121.

`title35`, big gradient lettering:
- BJ: `title35(text, y, top, mid, low, shadow, lowFrom=9)`, always scale 3, mask 124x18, ramp: rows 0-2 `top`, then `mid` until `lowFrom`, then `low`. Draw call: `maskDraw(m, 64-w/2, y, mid, INK, shadow, ramp)`.
- Chess: `title35(text, y, scale, top, mid, low, shadow, lowFrom)`. The mask is sized to the lettering. The `maskDraw(m,x,y,outline,shadow,ramp)` signature differed from BJ's (the games' own `Mask.h` then; the library's `chgame/Mask.h` now has BJ's).
- Standard calls:
  - `title35("OPTIONS", 6, FX_B, GOLD, WOOD, WINE, 13)` (BJ)
  - `title35("STATISTICS", 6, WHITE, CYAN, BLUE, NAVY)` (BJ)
  - `title35("CHESS", 4, 4, FX_B, GOLD, WOOD, WINE, 17)` (Chess)
  - `title35("OPPONENT", 10, 3, FX_B, GOLD, WOOD, WINE, 13)` (Chess)
- Because the top rows are `FX_B`, the palette makes the lettering shimmer with no redraw.

Centring helpers:
- `centred35(y,s,c)`: 3x5 font.
- `centred57(y,s,c)`: BJ, the CHGfx 5x7 `gfx_text`.
- `centred2(y,s,c)`: Chess, `text35x2`, the doubled 3x5 font (8 px advance, 12 rows).

From `Draw.h` (now `chgame/Draw.h`):
- `fillRound(x,y,w,h,r<=4,c)`, `roundRect(...)`, `panel(x,y,w,h,r,fill,edge)`.
- `text35(x,y,s,c)` returns the advance; 4 px per character, `'~'` is a 2 px space, `'\n'` moves down 7 px. `text35Width(s)`.

### 4.2 Title menus

**BJ (`titleRender()`):**
- Items at `y = 76 + i*11`, drawn in the 5x7 `gfx_text` font.
- Selected item: `panel(64-w/2-6, y-2, w+12, 11, 3, NAVY, (frame&16)?FX_B:GOLD)`, text GOLD. Others are WHITE.
- No wrap: `repeat(UP)` stops at `sel>0`.
- CONTINUE shows the money: `fmtMoney(fmtStr(buf,"CONTINUE "), game.purse)`.
- Credits sit in the bottom corners at y=116 in FELT_LT: `"PPOT 2018"` at x=7, `"CHGAME 2026"` right-aligned to 121. They are drawn before the menu.
- The title's opening animation:
  - A fanned 5-card hand drops in. Card `i` starts at `t = i*10` with `fx::ease(OUT_BACK, k, 18)` and plays `Sfx::Flip` at `k==12`.
  - `fx::burst(STAR,64,52,14,60,FX_A)` fires at `t==60`.
  - `art::chipStack` sits on both sides.
  - A spotlight is drawn with `gfx_dither(24,30,80,44,FELT_LT,0)`.

**Chess `menuItem` and `menuNav`:**
- `text35x2` text, highlight `fillRound(64-w/2-6, y-3, w+12, 15, 3, NAVY)` plus a `roundRect` in `(frame&16)?FX_B:GOLD`.
- Pitch is 14 px. The block is bottom-anchored with `y0 = 128 - n*14 - 1`, over `dither(0, y0-5, 128, …, INK, 1)`. The top band `dither(0,0,128,34,INK,0)` sits behind the logo.
- `menuNav(n)` does `repeat` UP/DOWN with no wrap and returns `justPressed(A)`.

### 4.3 Setup "choice" rows (Chess `arrows()`, `choice()`, `setupRender()`), useful for a table-limit or wheel-type picker

- `choice(y, s, on, frame)`: boxed in `fillRound(64-w/2-5, y-3, w+10, 15, 3, NAVY)` when chosen.
- Arrows bob 1 px every 8 frames: `bob=(frame>>3)&1`. `"<"` is drawn at `64-w/2-9-bob` and `">"` at `64+w/2+6+bob`, both GOLD.
- Difficulty stars: `"*"` in FX_B, spaced 8 px apart.
- Description line at y=66 in FELT_LT. Record line `"WON n LOST n DRAWN n"` at y=72 in GOLD.
- BEGIN uses `menuItem` at y=104.

### 4.4 Options

The table-driven string idiom is reusable as is.

**BJ (`OPT_TEXT`, `optionsUpdate()`, `optionsRender()`):**
```cpp
enum Opt : uint8_t { O_RULES, O_GOAL, O_SPEED, O_SOUND, O_FELT, O_DECK, O_TOTALS, O_DEALER, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = { "RULES|CASINO|CLASSIC", "GOAL|$1000|$5000|ENDLESS", "SPEED|NORMAL|FAST",
   "SOUND|LEAD|ARPEGGIO|OFF", "FELT|GREEN|BLUE|RED|PURPLE", "CARDS|2 COLOUR|4 COLOUR", "TOTALS|SHOW|HIDE", "DEALER|CLASSIC|NIGHT", "BACK" };
static_assert(sizeof(Options) == 8, "options menu indexes Options as bytes");
static uint8_t optField(const char *s, uint8_t k, char *buf);   // copies field k, returns the field count
```
- Update: UP/DOWN `repeat` wrap (`% OPT_COUNT`). LEFT/RIGHT `justPressed` gives `d=±1`; A on a value gives `d=1`.
- `*f = (*f + n + d) % n` over the `Options` bytes, then a side-effect switch: shoe reset, `audio::setMode`, `pal::setTheme`, the 4-colour deck flag. Then `Sfx::Chip`.
- B, or A on BACK: `persist(hasGame); go(optBack)`.
- Render:
  - Rows at `y = 26 + i*10`. Selected row: `fillRound(6, y-2, 116, 10, 3, NAVY)`.
  - Label: `text35` at x=10, WHITE when selected, otherwise FELT_LT.
  - Value: right-aligned to 118, FX_B when selected, otherwise GOLD.
  - Arrows on the selected row: `"<"` at `112-w`, `">"` at 120, SILVER.
  - Help line: `centred35(118, help, SILVER)`. It shows a per-option explanation for RULES, otherwise `"B: BACK"`.
- All-zero `Options` is the default. The `Round` object lives in `.bss`, so no constructor code runs (the comment above `struct Options` in `Round.h`).

**Chess (`optionsUpdate()`, `optionsRender()`):**
- 4 rows: `"SOUND|OFF|ON", "BOARD|GREEN|BLUE|RED|PURPLE", "PACE|FUN|QUICK", "BACK"`.
- `optByte(i)` skips two retired bytes so old saves keep their layout.
- Rows at `y = 29 + i*13`, in `text35x2`. Selected: `fillRound(8,y-3,112,15,3,NAVY)` plus a blinking `roundRect`. Label at x=15, value right-aligned to 114. BACK is centred.
- Footer credits: `centred35(110,"ARDUCHESS ENGINE: PETER BROWN",SILVER)` and `centred35(117,"FONT: PRESS PLAY ON TAPE",SILVER)`.
- `applyOptions()` updates `audio::setOn`, `pal::setTheme` and `stage::setFast`.

### 4.5 Stats (BJ `statsUpdate()`, `statsRender()`)

- 9 rows at `y = 26 + i*9`: label at x=10 in FELT_LT, value right-aligned to 118 (GOLD for money rows, using `fmtMoney`; otherwise `fmtInt` in WHITE).
- Footer:
  - y=106: `"PRESS A FOR CREDITS"` in SILVER.
  - y=112: one of three things:
    - `"HOLD SELECT TO RESET"` in FELT_LT.
    - A filling bar while SELECT is held: `gfx_rect(24,112,80,5,SILVER)` with `gfx_fillRect(25,113,78*statHold/90,3,RED)`.
    - `"STATS RESET"` in GOLD once done.
  - y=119: `"SAVED IN FLASH"` / `"SAVING UNAVAILABLE"`.
- `STAT_RESET_FRAMES = 90`. On reset: `memset(&stats,0)`, `persist`, `Sfx::Bust`.

### 4.6 Pause overlays

**BJ (pause, in `playRender()`):**
- Full-screen `gfx_dither(0,0,128,128,INK,0)`, then `panel(24,34,80,56,4,NAVY,GOLD)`.
- `"PAUSED"` via `centred57(39,…,GOLD)`.
- Items `{"RESUME","OPTIONS","SAVE & QUIT"}` at `y=53+i*11`. Selected: `fillRound(30,y-2,68,11,3,INK)` with text FX_B; others WHITE.
- Wraps with `(pauseSel + (UP?2:1)) % 3` on `justPressed` (no repeat).

**Chess (`panel()`, `playRender()`):**
- `panel(y,h)` is `fillRound(14,y,100,h,3,NAVY)` plus a GOLD `roundRect`.
- Pause is `panel(32,64)`, items at `38+i*14`. Selected: `fillRound(18,y-3,92,15,3,INK)`, text FX_B.
- Unavailable items (UNDO) are drawn in SILVER.

### 4.7 Result panels and PRESS A prompts

- **Chess RESULT** (`playRender()`):
  - `panel(84,40)`.
  - Heading `centred2(87, "YOU WIN!"|"YOU LOSE"|"WHITE WINS"|"STALEMATE"|…, FX_B)`.
  - Reason `centred35(99, "BY CHECKMATE"…, SILVER)`.
  - Hint `centred35(111, "A REMATCH   B MENU" / "A AGAIN   B MENU", WHITE)`. Three spaces separate the two hints.
- **Chess word plate** (`plate()` in `Stage.cpp`):
  - `plate(words[], colours[], n, y, grow, t)`: `fillRound(64-pw/2, y, pw, 11, 2, NAVY)` plus a GOLD `roundRect`.
  - The plate width grows with `fx::ease(OUT_BACK, t, 8)`. Word `k` appears at frame `6+3k`, dropping 3 px into place.
  - It shrinks over the last 8 of `ANN_FRAMES=120`.
  - Bottom plate at y=116, e.g. `"KNIGHT"`(WHITE) `" TAKES "`(RED) `"PAWN"`(WHITE), `" TO "`(SILVER), square name (GOLD), `" NO MOVES"`(SILVER, flashing RED on deny).
  - Blinking `"PRESS A"` plate at y=100 when `waitPress && holdT<20 && (frame&32)`.
  - This is the "calls out actions in words" plate. For roulette it could read "RED 7", "STRAIGHT UP 17 PAYS 35", "SPLIT 8/11".
- **BJ Win/Lose:** `if (t > 60 && (frame & 16)) centred35(108 or 116, "PRESS A", WHITE)`. A press only counts after `t>60` (`endUpdate`).
- **BJ toast** (`toast()`; drawn at the end of `screens::render`): `toast(s)` sets `toastT=60`. It draws `panel(64-w/2, 2, w, 11, 3, INK, GOLD)` plus `centred57(4, s, WHITE)` on top of any screen.

### 4.8 Win and Lose screens (BJ `winRender()`, `loseRender()`)

These are full redraws every frame.

**Win:**
- `gfx_clear(NAVY)`, then 16 rotating rays via a clipped Bresenham `ray()` (stopping at the screen edge), alternating `FX_A` and `WINE`.
- Bitmap lettering `YOUWON1/2` with a ramp cycling through `{RED,GOLD,FELT_LT,CYAN,BLUE}`, shifted every 4 frames.
- Purse in `title35`.
- Particles: `fx::fountain(COIN,…,2)` every 6 frames, `burst(STAR…)` every 24, `fountain(CONFETTI…,10)` every 30.

**Lose:**
- `pal::setDesaturate(min(t/12,12))`.
- NAVY vertical lines every 8 px.
- `BROKE1/2` lettering drops in (`drop = 40-t`).
- 2 rain particles a frame.
- The dealer smiles (`table::dealer(E_SMILE,…,40,70)`).
- Bottom band `gfx_fillRect(0,112,128,16,INK)`.

**Splash:** `SPLASH_PRESENTS=60, SPLASH_LOGO=120, SPLASH_TAG=180, SPLASH_END=360`. Each element appears once `t` passes its threshold.

---

## 5. HUD and the bottom action bar (BJ `Bar.cpp`, `Layout.h`)

### 5.1 `Layout.h`: every coordinate in one namespace (`lay::`)

Pattern to copy for a `RouletteLayout`. The play screen is split into bands:

| Rows | Content |
|---|---|
| 0..41 | Back wall: dealer, HUD plaque, shoe |
| 42..45 | Rail |
| 48..75 | Dealer's cards |
| 76..82 | Felt print |
| 83..110 | Player's cards and bet circle |
| 111 | Gold trim |
| 112..127 | Action bar |

Constants:
- `CARD_W=22, CARD_H=28, PITCH=12`
- `WALL_H=42`, `PLAQUE_X=52, PLAQUE_Y=3, PLAQUE_W=48, PLAQUE_H=34`
- `RAIL_Y=42, RAIL_H=4`, `TRAY_X=54, TRAY_W=44`
- `BET_CX=112, BET_CY=100, BET_RX=14, BET_RY=8`
- `TRIM_Y=111`, `BAR_Y=112, BAR_H=16`
- `BUBBLE_X=50, BUBBLE_Y=2, BUBBLE_W=51, BUBBLE_H=35`
- Comment: "even x: fast blits" for the dealer sprite.

### 5.2 The HUD plaque (`table::plaque` in `Table.cpp`)

- `panel(52,3,48,34,3,INK,GOLD)`.
- `"PURSE"` in text35 FELT_LT. The value is double-struck `gfx_text` (drawn at x and x+1 to look bold) in GOLD, flashing WHITE on payout with `(flash&4)`.
- NAVY divider line, then `"BET"` in SILVER with the value in WHITE.
- The purse number rolls toward its target: `step = d/5` (at least ±1), with a rising blip `3000+((shown*7)&511)` Hz every 4 units on gains (`present::update`).

### 5.3 `bar::draw(const Round&, frame)` (`Bar.cpp`)

API in Bar.h: `bool draw(r, frame)` (returns true if it drew), `reset()`, `invalidate()`.

**Skip-if-unchanged** (the start of `bar::draw`): an FNV-1a hash of `r.bar, r.sel, r.insureAmt, r.phase`, each slot's enabled bit plus its animated width, and `(frame>>3)%4` only for `Bar::None` (the dots). It also redraws while any accordion width is still easing (`widthQ4[i] & 15`).

**Background:** `gfx_fillRect(0,112,128,16,NAVY)` plus `gfx_hline(0,112,128,INK)`.

**`button(x, w, face, label, sel, on, frame)`**:
- The selected button sits 1 px higher: `y = BAR_Y+2-sel`, `h=12`.
- Fill: `fillRound(r=3)` in the face colour, or NAVY when disabled.
- Bottom shade line at `y+h-2`: INK on dark faces, WOOD on light ones (`darkFace` = RED, BLUE, NAVY, WINE, INK).
- Border: `roundRect` in FX_B when selected, otherwise INK.
- Label: the big 5x7 font when selected and it fits, otherwise text35. Text is WHITE on dark faces, INK on light ones, SILVER when disabled.
- Disabled buttons get `gfx_dither(INK)` over them.

**Accordion layout** (`layout()`):
- Target widths in Q4 ease by `/3` per frame and snap within 8 (half a pixel).
- Buttons are centred with a 2 px gap.
- On a bar change, widths reset to `128/n - 2`.

**Bars** (`enum class Bar { None, Bet, Insurance, Play, End }`, `Round.h`):
- **Bet:**
  - Widths `{16,16,16,16,30,22}`: four chips, then DEAL in GOLD, CLR in SILVER.
  - Each chip slot is `art::chip(cx, y+1, denom, true)` plus the value in text35 (`"$1","$5","$10","$25"`).
  - The selected chip sits on `fillRound(...,13,3,FX_B)` with INK text. Disabled chips are dithered NAVY.
  - **This maps directly to a roulette betting bar: chips, SPIN, CLR.**
- **Play:** `{"HIT","STAND","DOUBLE","SPLIT"}`, short label `"DBL"` when not selected. The selected button is 40 wide, the others 26. Faces: `{FELT_LT, RED, GOLD, BLUE}`.
- **Insurance:** `{74,44}`: `"INSURE $n"` in CYAN with tiny up/down arrow pixels for amount adjustment, and `"NO"` in SILVER.
- **End:** `{70,48}`: `"NEXT HAND"` in FELT_LT and `"QUIT"` in RED.
- **None (status):**
  - Text by phase: `"DEALING"`, `"SHUFFLING THE SHOE"`, `"DEALER PEEKS"`, `"DEALER PLAYS"`, `"PAYING OUT"`.
  - Drawn with text35 in SILVER at BAR_Y+6, followed by 3 animated dots (`(frame>>3)%4`).
  - For roulette: "NO MORE BETS", "SPINNING", "PAYING OUT".

**Selection rules** (`Round.cpp`, `slotCount()` to `moveSel()`):
- `slotCount()` and `slotEnabled(s)` define the bar.
- `fixSel()` moves to the nearest enabled slot, checking left first.
- `moveSel(dir)` skips disabled slots, does not wrap, and emits `Ev::Cursor`.
- Phase changes go through `go(Phase, Bar, defaultSel)`. On a new hand the last bet is placed again automatically and the cursor starts on DEAL (`go(InitBet, Bar::Bet, initBet ? B_DEAL : B_5)`).
- Constants: `CHIPS[4]={1,5,10,25}`, `MAX_BET=200`, starting purse 500.

**Chess HUD** (`drawHud()` in `Stage.cpp`):
- Top bar: `gfx_fillRect(0,0,128,9,INK)` plus a GOLD line at y=9.
- `text35(3,2, who, thinking?FX_B:WHITE)`, where `who` is `"YOUR MOVE"`, `"WHITE"`/`"BLACK"`, `"GAME OVER"`, or the CPU's name followed by 0-3 dots `((frame>>4)&3)`.
- B-hold progress bar: `gfx_fillRect(0,10,bar,2,CYAN)`, with `bar = bHeld*(128/HOLD_B)` and `HOLD_B=32` (`holdBar()`, `playRender()` in `Screens.cpp`).
- Shake is applied only to rows 10..127, below the HUD.

---

## 6. Drawing strategy: static layers vs. partial redraws

These are the main performance idioms.

### Premise (both READMEs)

- The framebuffer persists between frames. Every frame is flushed regardless, so palette animation is free: FX_A is a rainbow stepping every 3 ticks over 12 colours; FX_B is a GOLD↔WHITE triangle wave over 32 frames (`pal::tick`, then BJ's `Palette.cpp`, now the library's `chgame/Palette.cpp`). The same goes for fades, `pal::flash(index, rgb444, frames)` and desaturation.
- So: **draw only when the inputs change, and animate highlights through FX_A/FX_B instead of redrawing.**
- An async flush costs about **5 ms of CPU time** per frame (CHChess/docs/CHGfx-notes.md, suggestion 1).
- Code runs from flash with 3 wait states, so a function call per pixel costs about 2-3 µs.

### 1. Still screens are signature-skipped (BJ `screens::render()` and `unchanged()`)

```cpp
uint32_t step = cur==Scr::Splash ? (t>60)+(t>120)+(t>180) : (t<90 ? t : 90);
uint32_t sig = (uint32_t)cur*2654435761u ^ step ^ (menuSel<<8) ^ (optSel<<12) ^ (hasGame<<16) ^ (purse<<17) ^ (statHold<<24);
for (i..8) sig = sig*31u + ((uint8_t*)&game.opt)[i];
sig |= 1;                       // 0 means "never drawn"
bool still = cur==Title || cur==Options || cur==Stats || cur==Splash;
if (still && unchanged(sig)) return;    // unchanged() is also false while particles or a toast are alive
```
- The title redraws every frame for its first 90 frames (the deal-in) and while particles fly, then stops.
- **Caveat:** frame-based blinks such as `(frame&16)?FX_B:GOLD` on the title highlight freeze once the screen goes still, because `frame` is not in the signature. Rely on FX_B palette pulsing for anything that should keep animating on a still screen.
- Chess does not skip on its menu screens. Title, Setup and Options redraw fully every frame (the title has a moving 3D board behind it).

### 2. BJ play screen: band-level redraw (`struct Sig` and `present::render` in `Presenter.cpp`)

- Three bands are each FNV-hashed:
  - Wall, rows 0..45: expression, look, bubble, shown purse, bet, purse flash, shoe, shuffle.
  - Felt, rows 46..111: phase, hands, card views and positions, bets.
  - Bar, 112..127: §5.3.
- A band redraws if its hash changed, `forceAll` is set, or anything moving (`fx::activeRows`, flying cards and chips, ghosts, peek) touched its rows this frame or last.
- `present::rowsMoving(BAR_Y,127)` invalidates the bar when a chip flies over it.
- Order: `present::render`, then `bar::draw`, then, if anything drew, `present::overlay()`: flying chips and ghosts, particles, floating text, banner, `fx::applyShake(0, TRIM_Y-1)`.
- `redrawAll()` (`Screens.cpp`) calls `present::invalidate(); bar::invalidate(); staticSig=0; creditsReady=false`. Anything drawn on top of play (pause, toast, demo label) forces `redrawAll()` every frame while visible, plus once when it closes (`playRender()`, `screens::update()`).

### 3. Chess play: one signature for the whole scene (`signature()` and `stage::render()` in `Stage.cpp`)

- `signature(frame, ui)` returns `frame` itself (forcing a redraw) whenever anything is moving: particles, movers, flyer, toppling king, or the camera not yet at its aim.
- Otherwise it hashes about 24 state values, including `frame>>3`. The glove bob and marching borders therefore step at 7.5 Hz: "an idle board costs an eighth of the frames".
- The caller passes `ui` = overlay, sel, promoSel and hold bar (plus `(frame>>3)<<12` while an overlay is up), so overlays are part of the hash: `if (!stage::render(frame, ui)) return;` and then the overlay is drawn on top (`playRender()`).

### 4. BJ credits: static layer once, animated band per frame (`creditsRender()`)

- The felt (rows below RAIL_Y+RAIL_H), printed PPOT logo, chip stacks and credit text are drawn once, guarded by `creditsReady`.
- Each frame redraws only the wall band: `table::wall`, the dealer with a typewriter speech bubble (`CREDIT_FRAMES=180` per line, a letter every 2 frames, talk mouth alternating every 4 frames, blink when `t%150<6`), a flickering neon "OPEN 24H" (lit unless `frame%97<3` or `frame%211<2`), ashtray smoke pixels, and the rail.
- Cost: 3.3 ms per frame on CHGfx 1.2, about 2 ms now (BJ's README at the time).

### 5. Wall wallpaper via one built row (`table::wall` in `Table.cpp`)

- One `row[GFX_FB_STRIDE]` aligned to 4 bytes is built, then `gfx_copyRow(y,row,0,GFX_W)` copies it down 42 rows.
- Drawing the vertical lines pixel by pixel would cost more than 1 ms.

### 6. Big outlined lettering is expensive

- Naive drawing costs about 5 ms per line. The Mask path renders the shape into a 1 bpp mask in `gfx_chunkScratch` (1 KB, valid only between `gfx_wait()` and the flush), grows it for the outline, and paints it in runs (the library's `chgame/Mask.h`).
- Animated screens draw lettering once. Never keep a `Mask` across frames.

### 7. Performance figures

- Chess full redraw: 6.0 ms normal view, 7.8 ms zoomed (CHChess/docs/CHGfx-notes.md, "On CHGfx 1.3.0: shapes, sprites and row operations").
- BJ worst frame (bust + shake + banner): 11 ms on CHGfx 1.3.
- Chess title dithers about 5 KB per frame.

### 8. Saving must follow `gfx_wait()`

`persist()` calls `gfx_wait()` first (in both games' `Screens.cpp`). Chess builds the flash page in the chunk scratch, and flash programming runs from SRAM.

---

## 7. Flash and RAM budget notes relevant to screens

- App region: 50,944 B.
  - BJ is about 45.6 KB with LTO. Without LTO it is about 50 KB: screens 7.4 KB, presentation (table, cards, bar, fx, palette, drawing) 17.1 KB, CHGfx 8.7 KB, rules and saving 5.8 KB, art 3 KB, sound and music 2.9 KB, core and USB 5.1 KB.
  - Chess is 48.9 KB with LTO and 51.3 KB without, so LTO is mandatory there.
- **Fonts:**
  - BJ uses CHGfx `gfx_text` (5x7) for menu items, pause, toast, plaque and selected bar labels, and so pays for the built-in font (475 B plus a 264 B renderer).
  - Chess avoids it entirely and uses `text35x2`.
  - Switching everything to CHGfx's `CHGfx_Tiny3x5`/`gfx_textFx` would cost about +2.0 to 2.3 KB (CHChess/docs/CHGfx-notes.md, "On CHGfx 1.3.0: text, banners and the palette"). The games' own `FONT35` (now in `chgame/Draw.cpp`) is 331 B and `Mask.cpp` (now `chgame/Mask.cpp`) is 502 B plus 408 B of SRAM.
  - Choosing one menu font is a real flash decision. Blackjack's look (5x7 menu text) costs about 0.7 KB more than Chess's.
- `pal::commit()` only pushes when a colour actually changed, because each push makes CHGfx rebuild its conversion table. Using `gfx_setFade` instead of the game's own fade would cost about 150 B more.
- SRAM is the scarcer budget: Chess used about 17.9-18.1 KB of 20 KB. Saving puts the page in the chunk scratch rather than a stack buffer.
- Debug and LEAN builds drop the credits/music (BJ) or options/saving (Chess) to make room for the roughly 2 KB serial protocol. Gate the roulette credits page with `#if !CHRL_LEAN`.

---

## 8. What to reuse and what is game-specific

**Copy verbatim, renaming prefixes only** (the first three are now the CHGame library, so they are included, not copied):
- `CHGame.h/.cpp` (`chgame/Input`)
- `RamFunc.h` (`chgame/RamFunc.h`)
- `Fmt.*` (`chgame/Fmt`)
- `config.h` structure
- the `.ino` loop (BJ version)
- `go`/`enter`/fade in `update`
- `unchanged(sig)` still-screen skip
- `feltBackdrop`, `title35`, `centred35/57`
- the `optField` plus `OPT_TEXT "LABEL|v|v"` options screen (with `static_assert(sizeof(Options)==N)`)
- the stats hold-SELECT-90 reset with progress bar
- the toast
- the BJ pause panel
- Win/Lose (sunburst/`ray()`, desaturate, rain)
- the BJ `bar::button` and accordion `layout`
- the Chess word `plate()`
- `redrawAll()` invalidation discipline
- the `demoInput`/`toward()` attract mode
- the `debugJump(char)` hook

**Adapt for roulette:**
- `Layout.h` (new bands: wheel or racetrack on top, betting grid/felt in the middle, bar at the bottom).
- `Bar` enums and slots (Bet: chip denominations, SPIN, CLR; End: NEXT SPIN, QUIT).
- The `Round`-style rules class: pure logic, an event queue, an `update(pressed, repeat, fxBusy)` signature, and `pace()` frame timers halved at FAST speed.
- The Presenter-style band redraw.

**Not needed:** Chess's `Frame.cpp` search-interleaving, `holdFrames()`, `update(bool thinking)`, `pal::setMode`, the stage camera code, and BJ's dealer/shoe/card art (unless you want a croupier on the wall: `table::dealer(expr, look, alt, x, y)` and `present::speechBubble(text, typed)` can be reused with their assets).