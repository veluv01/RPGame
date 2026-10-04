# CHBlackjack game and table layer: reference for CHRoulette

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

Root: `../CHBlackjack/`. Paths below are relative to it. CHGfx is 1.3.0, in `platform/board/arduino/CHGame/libraries/CHGfx` (the board package brings it). Much of what is listed here as CHBlackjack's is now the CHGame library's (`chgame/...`), shared by every game.

## 1. Architecture (reusable as is)

Every cold `.cpp` starts with `#pragma GCC optimize("Os")`. Hot loops run from SRAM through `RAMFUNC(name)` from the CHGame library's `chgame/RamFunc.h`, which uses section `.gnu.linkonce.r.app.<name>` (CHBlackjack's own copy used a `chbj` prefix).

**Frame loop** (`loop()` in `CHBlackjack.ino`):
- Logic runs at a fixed 60 Hz and catches up by up to 3 ticks: `chgame.pollButtons()`, `pal::tick`, `screens::update`.
- Then `pal::commit()`, `gfx_wait()`, `screens::render(chgame.frameCount)`, `gfx_flushAsync()`.
- Setup calls `chgame.boot()`, `gfx_begin(GFX_DIV2, GFX_12BPP)`, `pal::init()`, `screens::begin()`, `chgame.setFrameRate(CHBJ_FPS)` (60).

**Split between rules and presentation:**
- `Round` (`Round.*`) is pure logic with no graphics or sound, so it can be tested on the host.
- `present::` (`Presenter.*`) drains Round's event queue and turns it into motion.
- `Round::update(pressed, repeat, fxBusy)` refuses to advance while `present::busy()` is true (`playUpdate()` in `Screens.cpp`):
  ```cpp
  game.update(pressed & ~START_BUTTON, rep, present::busy());
  present::onEvents(game);
  present::update(game);
  ```

**Event queue** (`struct Event` in `Round.h`; `Round::emit`, `Round::popEvent`):
- `struct Event { Ev type; uint8_t a, b, c; int16_t amount; }` in a 16-entry ring (`q[16]`, masked with `& 15`). When full, events are silently dropped.
- `amount` is **int16**. A roulette straight-up at 35:1 on a large stake can overflow it: $200 × 36 = 7,200 is fine, but stakes above about $910 are not. Widen it to int32 if the table limit is high.

**Phase machine idiom** (`Round::go`, `Round::pace`):
- `go(Phase, Bar, sel)` sets the phase, sets the action bar, resets `step`, then calls `fixSel()`.
- `pace(frames)` sets `wait`. It halves when `opt.speed` is set. `update()` returns early while `wait--`.
- Pacing constants (the enum at the top of `Round.cpp`): `T_DEAL=14, T_PEEK=50, T_PEEK_RESULT=30, T_BUST=50, T_SETTLE=40, T_REVEAL=22, T_SHUFFLE=70, T_SPLIT=16, T_END=30` (frames).

## 2. Round: betting, bankroll, payouts, stats, RNG

**Chips and limits:**
- `static const uint8_t CHIPS[4] = {1, 5, 10, 25}`; `MAX_BET = 200` (top of `Round.cpp`).
- The renderer also knows a $100 chip (§4) that only appears in stacks and payouts. It is not a bet button.

**Bankroll:**
- `int32_t purse`. `newGame()` sets it to 500, clears `lastBet`, empties the shoe and says `L_WELCOME/F_SMILE`.
- `resume()` keeps the purse and says `L_GOOD_LUCK`.

**Goal** (`Round::goal()`): `GOAL_1000` → 1000, `GOAL_5000` → 5000, `GOAL_ENDLESS` → 0x7FFFFFFF.

**Bet bar and input:**
- Slot enum `B_1, B_5, B_10, B_25, B_DEAL, B_CLEAR, BET_SLOTS` (`Round.h`).
- `slotEnabled`: chip slot `s` is enabled when `initBet + CHIPS[s] <= MAX_BET && purse >= CHIPS[s]`; DEAL and CLR need `initBet > 0`.
- `betInput`:
  - LEFT/RIGHT on `repeat` call `moveSel`, which skips disabled slots and emits `Ev::Cursor`.
  - A or UP on `repeat` adds the selected chip: `purse -= v; initBet += v; emit(BetAdd, seat=1, b=sel, amount=v)`.
  - B or DOWN takes **that denomination** back with `BetRemove`.
  - A on CLR refunds everything as one `BetRemove` with `amount=initBet`.
  - A on DEAL, or START, deals. Pressing something unavailable emits `Ev::Deny`.
  - `fixSel()` moves the cursor to the nearest enabled slot, checking left first.
- **Money leaves the purse the moment a chip is placed.** Removing a chip refunds it immediately.

**Automatic rebet** (`Round::update`, `Phase::StartHand`): at StartHand, if `lastBet <= MAX_BET && purse >= lastBet`, the game re-places `lastBet` and emits `BetAdd` with `b=0xFF`. The presenter flies those chips in from off screen. The cursor then defaults to DEAL, or to `B_5` when there is no rebet.

**Settlement** (`settleHand`):
- `ret` = stake plus winnings, added straight to `purse`. A blackjack returns `bet + bet*3/2` (integer). A win returns `2*bet`, a push returns `bet`, a loss returns 0.
- Stats updated: `won`, `pushed`, `lost`, `blackjacks`, and `biggestWin = max(ret - bet)`.
- Emits `Ev::Settle(a=seat, b=Result, amount=ret)`, then `say(LINES[r], FACES[r])`.
- Line and face per result:
  - lose → `L_DEALER_WINS` / `F_SMILE`
  - push → `L_PUSH` / `F_RAISED`
  - win → `L_YOU_WIN` / `F_ANGRY`
  - blackjack → `L_PLAYER_BJ` / `F_SURPRISED`
- The dealer smiles when you lose and scowls when you win. Keep that personality.
- Insurance pays 2:1, so `ret = insurance*3` (in `Round::update`).

**End of round** (`Phase::OverallWinOrLose`):
- Updates `bestPurse`.
- `purse >= goal()` → `gamesWon++`, `Ev::GameOver(1)`, `Phase::GameWon`.
- `purse < 1` → `gamesBroke++`, `Ev::GameOver(0)`, `Phase::GameLost`.
- Otherwise `Bar::End` with NEXT HAND / QUIT. START also continues.
- `playUpdate()` in `Screens.cpp` watches phase changes: GameWon/GameLost → `persist(false)` and go to the Win or Lose screen; Quit → `persist(true)`; every 5th hand at EndOfGame it autosaves.

**Stats** (`struct Stats`, `Round.h`):
```cpp
uint32_t hands, won, lost, pushed, blackjacks;
int32_t bestPurse, biggestWin;
uint16_t gamesWon, gamesBroke;
```
The Stats screen labels `gamesWon` "BANKS BROKEN" and `gamesBroke` "TIMES BROKE" (`statsRender()` in `Screens.cpp`).

**Options** (`struct Options`, `Round.h`):
- 8 bytes, all-zero default: `rules, goal, speed, sound, theme, fourColour, totals, dealer`.
- The options menu indexes the struct as raw bytes (`static_assert(sizeof(Options)==8)` in `Screens.cpp`).
- Each menu row is a string `"LABEL|v1|v2|..."` (`OPT_TEXT`), parsed by `optField()`. This is copy-paste reusable.

**RNG:**
- `Round::rand32()` is xorshift32 (`<<13, >>17, <<5`); a zero state becomes `0x9E3779B9`. `seed(s)` is in `Round.h`.
- Seeded once, on the first title-menu selection: `game.seed(micros() * 2654435761u ^ chgame.frameCount)` (`seedOnce()` in `Screens.cpp`).
- The shuffle is Fisher–Yates with `rand32() % (i+1)` (`Round::shuffle`). The demo seeds with `fx::rnd()`.
- A separate presentation-only xorshift `fx::rnd()` (then in CHBlackjack's `Fx.cpp`, now the CHGame library's `chgame/Fx.h`) never touches outcomes. Roulette should draw its pocket from `Round::rand32() % 37`.
- Test hook: `stackDeck()` forces the next cards. The roulette equivalent would be "force next pocket".

**Speech lines:** a `Line` enum plus `lineText()` with a `TEXT[LINE_COUNT]` table. Up to 12 characters per line, `'\n'` separates lines, 4 lines at most (the bubble is 35 px tall at 7 px per line). `say(line, face)` emits `Ev::Say(a=line, b=face)`. Faces: `F_NORMAL, F_ANGRY, F_RAISED, F_SMILE, F_SURPRISED` (`enum Face`, `Round.h`).

**Money-conservation test idiom** (`testFuzz()` in `tools/tests/test_rules.cpp`): a fuzz test over 40 seeds × 400 hands asserts `purse + onTable == money` across BetAdd/BetRemove/Split/Double events. Port this for roulette.

## 3. Table layout (`Layout.h`, namespace `lay`)

| Rows | Content | Constants |
|---|---|---|
| 0..41 | back wall: dealer, HUD plaque, shoe | `WALL_H=42`, `DEALER_X=2, DEALER_Y=0` (even x for fast blits), `FACE_X=14, FACE_Y=14` |
| | plaque | `PLAQUE 52,3,48x34` |
| | speech bubble (covers the plaque) | `BUBBLE 50,2,51x35` |
| | shoe | `SHOE 104,20,22x26`, `SHOE_MOUTH 106,36` |
| 42..45 | rail | `RAIL_Y=42, RAIL_H=4`; chip rack `TRAY_X=54, TRAY_W=44` |
| 48..75 | dealer cards | `DEALER_CARDS_Y=48, DEALER_CX=60` |
| ~75..84 | arced felt print | `PRINT_Y=77` |
| 83..110 | player cards | `PLAYER_CARDS_Y=83, PLAYER_CX=48, PLAYER_MAX_X=94`, split centres 25 / 72 |
| | bet circle | `BET_CX=112, BET_CY=100, BET_RX=14, BET_RY=8` |
| | insurance stack | `INS_CX=112, INS_CY=62` |
| 111 | gold trim | `TRIM_Y=111` |
| 112..127 | action bar | `BAR_Y=112, BAR_H=16` |

Cards are 22×28 with a 12 px pitch.

Rows 0..45 (wall, dealer, plaque, rail) can be kept almost unchanged for roulette. The felt band (46..110) becomes wheel plus betting grid.

## 4. Chip rendering (`CardArt.cpp`, `CHIP_BODY` to `chipStack`): reusable as is

Chips are procedural; there are no chip bitmaps.
```cpp
static const uint8_t CHIP_BODY[5]  = {WHITE, RED,   BLUE,  FELT_LT, INK};   // $1 $5 $10 $25 $100
static const uint8_t CHIP_EDGE[5]  = {BLUE,  WHITE, WHITE, WHITE,   GOLD};
static const uint8_t CHIP_SHADE[5] = {SILVER,WINE,  NAVY,  FELT_DK, INK};
static const int32_t CHIP_VALUE[5] = {1, 5, 10, 25, 100};
int chipDenom(int32_t amount);   // largest denom <= amount (index 0..4)
```

**`art::chip(cx, y, d, top)`** draws a chip seen at an angle, 15 px wide (cx−7..cx+7) and 6 rows tall (y−1..y+4):
- Edge band: rows y+2 and y+3, `hline(cx-6, 13, shade)`.
- INK side pixels at cx±7 on rows y+1 and y+2.
- Three stripes: `vline(cx-4|cx|cx+4, y+2, 2, edge)`.
- Bottom INK line: `hline(cx-5, y+4, 11)`.
- Only when `top` is true: `fillEllipse(cx, y+1, 6, 2, body)`, an `ellipse(cx, y+1, 7, 2, INK)` rim, and 4 edge-colour pixels at (cx±4, y+1), (cx, y) and (cx, y+2).
- Gotcha: the $25 chip uses FELT_LT/FELT_DK, so it **changes colour with the felt theme**, because themes repalette indices 2..4.

**`art::chipStack(cx, baseY, amount, maxChips=10)`:**
- Greedy split from $100 down, at most 24 chips. The largest denomination is at the bottom.
- Each chip sits 2 px above the last: `chip(cx, baseY - 2*(i-first), d, i==n-1)`, so only the top chip gets a face.
- Tall stacks show only their top `maxChips`.
- `maxChips` used: 9 on the table, 4 for chips in flight, 6 on the title and credits screens.

**Chip rack on the rail** (`table::rail()` in `Table.cpp`): an INK box `(53,42,46,4)`, then 11 chips seen edge-on as 3×3 squares at `x = 54 + i*4`. Colours cycle `{WHITE, RED, BLUE, FELT_LT, INK}` with a centre highlight pixel.

**Bet-bar chip buttons** (`bar::draw()` in `Bar.cpp`):
- Slot widths `{16,16,16,16,30,22}`.
- Each chip button is `art::chip(cx, y+1, d, true)` with a 3×5 label "$1"…"$25" at y+7.
- Selected: an FX_B rounded fill behind it and a 1 px lift. Disabled: a NAVY dither over it.

## 5. Chip animation (`Presenter.cpp`)

**Flight pool** (`FlyKind`, `Fly`, `flies`):
```cpp
enum FlyKind : uint8_t { CHIP_IN, CHIP_OUT, STACK_TO_TRAY, STACK_TO_PLAYER };
struct Fly { int16_t x0, y0, x1, y1; int16_t t; uint8_t T, denom, seat, kind; int32_t value; };
static Fly flies[16];                       // T==0 means free; t<0 means a delay
```

**Spawning**: `fly(x0, y0, x1, y1, denom, seat, value, kind, delay=0, T=14)` takes a free slot. If the pool is full, the effect is applied instantly (`dispBet += value` or `pending -= value`).

**Path** (`flyY`; `overlay`):
- `e = fx::ease(OUT_CUBIC, t, T)`; `x = x0 + ((x1-x0)*e >> 8)`.
- `y` uses the same lerp minus `(isin(t*128/T)*10) >> 8`, a half-sine hop of up to 10 px.
- `y` is clamped to at least `RAIL_Y+RAIL_H+1+(stack ? 8 : 1)` so chips never cross the rail.
- `CHIP_IN`/`CHIP_OUT` draw `art::chip(x, y, denom, true)`; stacks draw `art::chipStack(x, y, value, 4)`.
- Flights are drawn in `overlay()` after the bands, together with particles, floating text, banner and shake.

**Landing** (`present::update`):
- `CHIP_IN`: `dispBet[seat] += value`.
- `STACK_TO_PLAYER`: `pending -= value; purseFlash = 24; Sfx::Coin`.

**Displayed bets:** `dispBet[3]` (0 = insurance, 1..2 = hands) is what the felt shows. It grows only when chips land. Drawn as `art::chipStack(stackPos, dispBet[s], 9)` in `present::render`. Stack positions (`stackPos`): hand stacks at (112, 98), or ±7 px when split; insurance at (112, 62). Roulette needs `dispBet[N]`, one per bet spot.

**`chipsIn(r, seat, amount, fromX, fromY, stagger)`** splits an amount into at most 8 single-chip flights. Flight k uses denomination `chipDenom(remaining)`, starts at `(fromX + k*3, fromY)` and is delayed by `k*stagger`. The 8th flight carries the whole remainder.

**`sweep(r, seat, kind)`** flies the whole `dispBet[seat]` as one stack and zeroes it:
- `STACK_TO_TRAY` goes to `(76, 56)` with T=16.
- `STACK_TO_PLAYER` goes to `(8, 140)` (off screen, bottom left) with T=18.
- Plays `Sfx::Whoosh`.

**Event-to-flight mapping** (`onEvents`):

| Event | Motion |
|---|---|
| BetAdd from a bar button | `fly(9 + b*17, 118, stack, denom=b, CHIP_IN)` + `Sfx::Chip` |
| BetAdd rebet (`b==0xFF`) | `chipsIn(..., 20, 132, stagger 2)`, from off screen |
| BetRemove | `dispBet -= amount` immediately, then `fly(stack → 20, 136, chipDenom(amount), CHIP_OUT)` |
| Insure / Split / Double | `chipsIn(..., 30, 132, 3)` |
| Settle, win | `pending += ret`; `chipsIn(win, from TRAY_X+6=60, 48, 3)` (out of the dealer's rack); `collectT[seat]=40`; `floatText("+$N", GOLD)`; banner "WIN!" `B_GOLD` (or "3 TO 2!" `B_RAINBOW`); confetti fountain 14; `LED_BLINK` |
| Settle, push | `pending += ret; collectT=30`; banner "PUSH" `B_CYAN` |
| Settle, lose | `sweep(STACK_TO_TRAY)`; `floatText("-$N", RED)`; `Sfx::Lose` |

The rebet start position `9 + b*17` only approximates the bar layout: the real chip centres are x = 9, 27, 45, 63 (pitch 18), so the start is off by 1–3 px.

**Collection** (`present::update`): `collectT[s]` counts down only while no `CHIP_IN` flight for that seat is still in the air. When it reaches 0, `sweep(STACK_TO_PLAYER)` sends the stake plus winnings off screen to the player.

**Rolling purse** (`present::update`):
- The display targets `r.purse - pending`, so money already credited by the rules appears only when its chips arrive.
- Each frame: `step = d/5`, at least ±1.
- While rising, `audio::blip(3000 + ((shown*7) & 511), 8)` plays every 4th value.

**Busy** (`present::busy()`): true while any card is in flight or flipping, any `flies[].T` is set, any `collectT` is running, or `shuffleT` is set.

**Sounds** (`Sounds.cpp`, played by the CHGame library's engine, `chgame/Audio.h`):
- CHIP is 3100 Hz 12 ms, rest 9 ms, 3700 Hz 26 ms.
- COIN is 2800 Hz 10 ms, then 3700 Hz 28 ms.
- WHOOSH sweeps 1200→3800 Hz over 90 ms.
- Sfx enum: `Deal, Flip, Chip, Cursor, Select, Deny, Win, Blackjack, Bust, Push, Lose, Peek, Shuffle, Coin, Split, Double, Insurance, Broke, Reveal, Whoosh`.

## 6. Table drawing (`Table.cpp`, namespace `table`)

**`wall(frame)`**:
- Navy pinstripe: one row buffer `row[GFX_FB_STRIDE]` aligned 4, NAVY with an INK pixel every 8 px from x=3, stamped into rows 0..41 with `gfx_copyRow`. The comment says drawing the vertical lines pixel by pixel cost over 1 ms.
- `gfx_dither(0,0,128,3,INK,0)` darkens the ceiling.
- `gfx_dither(8,2,36,30,WOOD,1)` is the spotlight behind the dealer.

**`dealer(expr, look, alt, x=2, y=0)`**:
- `gfx_sprite4(DEALER, x, y, alt ? DEALER_ALT_REMAP : nullptr)`.
- Then the face patch `FACE_NORMAL` at (x+12, y+14).
- If `expr != 0`, it plots the pixel edits `FACE_EDITS[FACE_EDIT_AT[expr-1] .. FACE_EDIT_AT[expr])`. Each word is `idx = w>>4` (y*24+x in the 24×18 patch) and `colour = w&15`, remapped for the alt dealer.
- Pupils follow the action: for each eye at `fx+5` and `fx+15`, a WHITE 4×2 sclera at `fy+6` and a 2×2 INK pupil shifted by `dx = look==0 ? -1 : +1`. Skipped when blinking or when `look == 1` (centre).
- `enum Expr { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK }` (`Table.h`).

**`rail()`**: GOLD line at y42, WOOD on 43–44, INK on 45, then the chip rack.

**`felt(r)`**:
- FELT_DK dither on 3 px at each edge, and a FELT_DK line at y46.
- Arced print: `arcText(77, "BLACKJACK PAYS 3 TO 2", GOLD)` with `arcDy(x) = (x-64)^2/900`, between two `arcLine` rules in FELT_LT at y75 and y84.
- Rule text centred in FELT_LT at y59 and y66.
- Bet circle: `fillEllipse(112,100,14,8,FELT_DK)`, an `ellipse` in FELT_LT, and an inner ellipse (12,6) in FELT.
- During betting, the presenter adds an FX_B ellipse at rx+1, ry+1, which pulses for free through the palette (`present::render`).
- `arcText`/`arcLine` are good for "SINGLE ZERO / STRAIGHT UP PAYS 35 TO 1".

**`plaque(purse, bet, flash)`**, the HUD money display:
- `panel(52,3,48,34,r3,INK,GOLD)`.
- "PURSE" in 3×5 FELT_LT at (56,6).
- The amount in the 5×7 font, right-aligned at y13, double-struck at x and x+1 for bold. GOLD, flashing WHITE/GOLD on `(flash & 4)`.
- A NAVY divider at y22.
- "BET" in SILVER at (56,25); the bet value in WHITE 5×7, right-aligned.
- Fully reusable; just pass the total of all roulette bets.

**`shoe()`** is blackjack-only.

## 7. Speech bubble, face and dealer animation (`Presenter.cpp`)

**`present::speechBubble(text, typed)`** is public:
- `panel(50,2,51,35,r4,WHITE,INK)`.
- A tail of 5 stepped rows toward the mouth at y+22..26, x−6..x.
- Lines are centred on their full width, then only the first `typed` characters are drawn, in 3×5 INK, 7 px apart, vertically centred.

**State** (the statics at the top of `Presenter.cpp`): `bubLine, bubChars, bubLen, bubHold`, plus `face`, `blinkT=90`, `blinking`, `look`.

**Per frame** (`present::update`):
- The typewriter adds 1 character per frame. Each odd character blips at `1900 + (n*97) % 700` Hz for 12 ms.
- After typing, it holds for 100 frames, then closes the bubble and resets the face to normal.
- Blink: `blinking = 6` frames, next blink after `rndRange(90, 220)` frames.
- `look` = 0 if the moving card's x < 30, 2 if > 40, else 1. While peeking, look = 2.

**Expression choice** (`render`):
1. Start from `exprFor(face)`, which maps F_ to E_.
2. While typing, alternate `E_TALK` every 4 frames (`(frame>>2)&1`).
3. Blinking overrides everything.

**Band redraw** (`render`):
- Two FNV-1a signatures (`struct Sig`):
  - wall: expression, look, bubble, shown purse, bets, flash, shoe, theme
  - felt: phase, hands, card views, `dispBet`
- A band is redrawn only if its signature changed, or if something moving touched its rows this frame or the last (`movingRows`/`fx::activeRows`).
- The wall band draws `wall → dealer → (bubble or plaque) → shoe → rail`.
- Every frame is still flushed, so FX_A/FX_B palette animation keeps running.
- `bar::draw` has its own signature hash (in `bar::draw`) and is invalidated when `present::rowsMoving(BAR_Y, 127)`.

## 8. Effects (then CHBlackjack's `src/fx/Fx.*`, now the CHGame library's): reusable as is

Easing, sine, `rnd()` and the shake are the library's `chgame/Fx.h`; the particles, the banner and the floating texts are its `chgame/Sizzle`, which each game configures in its `Fx.h`.

- **Easing:** `ease(Ease, t, n)` returns 0..256 from 17-point Q8 tables `LINEAR, OUT_CUBIC, OUT_BACK, IN_OUT, OUT_BOUNCE` (`chgame/Ease.cpp`). `isin(a)` takes 1/256 turns and returns −256..256 from a 65-entry table. No floats: the comment says soft-float trig once cost 8.5 KB of flash and half the frame rate.
- **Particles:** `parts[48]`, 10 B each. Kinds `SPARK, CONFETTI, COIN, RAIN, STAR, DUST`.
  - `burst(k, x, y, n, speed, colour)` is radial.
  - `fountain(k, x, y, n)` throws upward; confetti colours `{RED, GOLD, FELT_LT, CYAN, BLUE, WHITE}`.
  - COIN particles bounce at y122.
- **Banner:** `banner(text ≤13, style, cy, frames=70)`. Styles `B_RAINBOW, B_GOLD, B_RED, B_CYAN, B_WHITE`.
  - Pops in at scale 2, then 4, then settles at 3. Each letter waves with `isin`.
  - Drawn through a Mask with an outline, a shadow and a per-row colour ramp.
  - Blinks out over its last 10 frames.
- **Floating text:** `floatText(text ≤7, x, y, colour)`, 4 slots, 50 frames, rises 25 px, 1 px INK shadow.
- **Shake:** `shake(frames, amp)` applies `gfx_scroll` (±2 px sideways, decaying dy) to rows 0..110.
- **Palette** (the CHGame library's `chgame/Palette.cpp`), RGB444:

| Index | Name | RGB444 |
|---|---|---|
| 0 | INK | 000 |
| 1 | WHITE | FFF |
| 2 | FELT_DK | 042 |
| 3 | FELT | 173 |
| 4 | FELT_LT | 4B5 |
| 5 | SILVER | BBC |
| 6 | RED | E12 |
| 7 | WINE | 702 |
| 8 | GOLD | FC2 |
| 9 | WOOD | 741 |
| 10 | BLUE | 26E |
| 11 | NAVY | 125 |
| 12 | SKIN | FB8 |
| 13 | CYAN | 6EF |
| 14 | FX_A | rainbow cycle, 12 steps every 3 ticks |
| 15 | FX_B | GOLD↔WHITE triangle over 32 frames |

  - Themes recolour indices 2..4 only: green, blue `024/149/48D`, red `401/812/C44`, purple `203/517/95B`.
  - Also: `pal::flash(idx, rgb, frames)`, `setFade` (0..16), `setDesaturate`.
  - Roulette red/black/green pockets map onto RED, INK, and FELT or FELT_LT. Green pockets would follow the felt theme unless given a fixed index.
- **Drawing helpers** (the CHGame library's `chgame/Draw.*`):
  - `fillRound`/`roundRect`/`panel` (r ≤ 4)
  - `remapRect`
  - `text35`/`text35Width` (PPOT 3×5 font: 4 px advance, `~` = 2 px space, `\n` = 7 px)
  - `glyph`
  - `chgame/Fmt.*`: `fmtInt`/`fmtMoney` (negative prints "-$5") /`fmtStr`, used instead of snprintf
  - `chgame/Mask.*`: `maskBegin/maskText35/maskBlit1/maskDraw` for outlined gradient lettering in `gfx_chunkScratch` (1 KB, w*h ≤ ~7000 px, render-time only)
- **CHGfx 1.3 has `gfx_sprite4Rot(spr, ax, ay, px, py, angle, scale, remap)`** (`CHGfx.h`). It rotates and scales a sprite and would suit a spinning wheel or ball, but Blackjack does not use it.
  - It decodes into `gfx_chunkScratch`, which caps the art at 1 KB at 4 bpp (32×64 or 45×45).
  - It waits for any flush in flight and works per pixel.
  - It shares the scratch buffer with Mask, so do not hold a Mask across a call.

## 9. Screens (`Screens.cpp`)

- **Screens:** `enum class Scr { Splash, Title, Play, Options, Stats, Win, Lose, Credits }`. Fade-out and fade-in take 8 frames each via `pal::setFade` (`screens::update`).
- **Still screens** (Title/Options/Stats/Splash) redraw only when a signature changes (`unchanged()`, `screens::render`).
- **`feltBackdrop()`**: FELT fill, 6 px FELT_DK dither on every edge, GOLD rect (2,2,124,124).
- **`title35(text, y, top, mid, low, shadow, lowFrom)`**: scale-3 masked lettering with a gradient.
- **Title** (`titleRender()`):
  - Spotlight dither, then the LOGO mask with ramp FX_B / GOLD / WOOD, INK outline and WINE shadow.
  - Five cards dealt in, each with `ease(OUT_BACK, k, 18)`, staggered 10 frames.
  - Chip stacks at (14,110) $95 and (114,110) $155.
  - Menu highlight: `panel` in NAVY with a border alternating FX_B/GOLD on `frame & 16`.
  - Attract demo after 600 idle frames, once the tune has finished.
- **Pause and toast:** pause menu RESUME / OPTIONS / SAVE & QUIT; toast "SOUND ON/OFF" on SELECT.
- **Win** (`winRender()`):
  - NAVY background with a rotating 16-ray sunburst alternating FX_A/WINE.
  - YOUWON1 and YOUWON2 lettering with a rainbow ramp cycling by `frame/4`.
  - The purse via `title35`.
  - Every 6 frames a COIN fountain; every 24 a STAR burst; every 30 a CONFETTI fountain.
  - "PRESS A" blinks after 60 frames.
- **Lose (broke)** (`loseRender()`):
  - Desaturates up to 12 over `t/12` frames.
  - NAVY vertical stripes.
  - BROKE1 and BROKE2 drop in with a WHITE/RED/WINE ramp.
  - 2 CYAN RAIN particles per frame.
  - The dealer at (40,70) with `E_SMILE`, "delighted".
- **Credits, the "back room"** (`creditsRender()`, compiled out when `CHBJ_LEAN`):
  - **Drawn once** (flag `creditsReady`): the felt below the rail, the PPOT logo printed flat in FELT_LT at y51, chip stacks at (16,80) and (112,80) with max 6, "CODE / ART" credits and "ORIGINAL FOR ARDUBOY 2018".
  - **Each frame, wall band only:**
    - `table::wall` → `table::dealer(expr, 1, alt)` → `speechBubble(CREDIT_LINES[line], typed)`.
    - 6 lines, 180 frames each, 1 character per 2 frames.
    - Expression: TALK/NORMAL alternating every 4 frames while typing; BLINK when `(t%150) < 6`; SMILE on the first and last lines.
  - **Neon "OPEN 24H":** `roundRect(103,5,23,21,r3)` in RED, or WINE when unlit; text in SKIN when lit. Lit when `(frame%97) >= 3 && (frame%211) >= 2`.
  - **Ashtray:** `fillEllipse(111,40,6,2,SILVER)`; cigarette is `hline(113,38,5,WHITE)`; the ember pixel at (118,38) flickers RED/GOLD every 8 frames.
  - **Smoke:** for i in 2..29, a SILVER pixel at `x = 118 + (isin(i*10 - frame*2) * (i/5+1)) >> 8`, `y = 38 - i`. Gaps where `(i - frame>>2) % 5 == 0`; odd i > 18 are skipped. Rail is drawn last.
  - Cost: 3.3 ms per frame measured on CHGfx 1.2, about 2 ms estimated now.

## 10. Assets (`src/assets/Assets.*`, generated by `tools/assets.py`)

| Symbol | Bytes | Format / size | Reuse for roulette |
|---|---|---|---|
| `DEALER` | 487 | sprite4 48×42 | **yes (the croupier)** |
| `DEALER_ALT_REMAP` | 16 | `{0,1,2,3,4,5,8,11,8,0,10,11,...}`: RED tie→GOLD, WINE vest→NAVY, WOOD hair→INK ("NIGHT" dealer) | yes |
| `FACE_NORMAL` | 119 | sprite4 24×18 at (12,14) | yes |
| `FACE_EDITS` / `FACE_EDIT_AT` | 276 / 14 | 138 u16 pixel edits; order ANGRY RAISED BLINK SMILE SURPRISED TALK; starts `{0,22,49,73,87,128,138}` | yes |
| `SUIT_SMALL`, `RANK_GLYPH`/`RANK_WIDTH`, `PIP9`/`_AT`, `PIP13`/`_AT` | 20, 91 + 13, 132 + 8, 178 + 8 | column glyphs and span1 pips | no (cards only) |
| `COURT_JACK`/`QUEEN`/`KING` | 98 / 112 / 110 | sprite4 14×18 | no |
| `LOGO` | 182 | 1 bpp 104×14 "BlackJack" | no, needs a new "CHRoulette" logo |
| `YOUWON1`/`YOUWON2` | 240 / 224 | 1 bpp 119×16 / 107×16 | possible (PPOT art, needs attribution) |
| `BROKE1`/`BROKE2` | 192 / 160 | 1 bpp 91×16 / 73×16 | possible |
| `PPOT_LOGO` | 288 | 1 bpp 65×32 | no (PPOT credit only) |

- Total art is about 2.97 KB, matching the README's "art 3 KB".
- Chips, the felt, the plaque and the bubble are all procedural and cost no asset bytes.

**Pipeline:**
- Art source files: `tools/art/*.txt` use palette letters `k w d f g s r m y b u n p c x z` (= indices 0..15); `.` or space is transparent. The dealer, `tools/art/common/dealer.png` at the repository root (shared art), must use exact palette colours (`load_png` in `tools/assets.py`).
- `pack_span4`: `w, h`, then per row `n` followed by `n` bytes of `(len-1)<<4 | colour`. Runs are at most 16 px, colour 15 is transparent, trailing transparency is dropped.
- `pack_rows1` produces MSB-first 1 bpp rows; `pack_span1` produces hline spans; `pack_cols` produces column glyphs.
- Previews are written to `build/assets/*.png`.

## 11. Save (`Save.*`, on the CHGame library's `chgame/Save`): change MAGIC

- **Record:** the library's header (magic `save::magic("CHBJ")`, version 1, sequence), the game's `Data { purse, hasGame, pad[3], Options, Stats }`, then a CRC32.
- **Pages:** alternates between flash pages `0xF500` and `0xF600`; the metadata page is `0xF700`. Writes use the library's RAM function `pageWrite` with interrupts off.
- **Space check:** `available()` checks `imageEnd() <= PAGE_B`; two pages are used only if the image is at most `0xF500`. A larger image saves nothing and Stats says "SAVING UNAVAILABLE".
- **New game:** use a distinct magic so a left-over Blackjack record is rejected.
- **Simulator:** a `simFlash[2][256]` stand-in under `CHSIM`.

## 12. Documented flash, RAM and performance costs

- **Budget:** the release build (opt `oslto`, USB upload-only, periph `game`) uses about **45.6 KB of 50,944 B**.
- **Without LTO (~50 KB):** core+USB 5.1 KB, CHGfx 8.7, rules+save 5.8, presentation 17.1, screens 7.4, art 3, sound+music 2.9 (CHBlackjack's `NOTES.md`).
- **RAM:** statics limit is 18,416 B plus a fixed 2 KB stack (the repository's `tools/check_size.py`).
- **Savings already taken:**

| Change | Saved |
|---|---|
| no `snprintf` | 3.5 KB |
| register writes instead of `pinMode` | 2 KB |
| own sound sequencer (1.8 KB, now the CHGame library's `chgame/Audio`) instead of CHGameSound | 6.5 KB library avoided |
| periph=game | 3.4 KB |
| debug protocol left out of release | 1.8 KB |

- **Drawing costs** (from CHBlackjack's comments at the time):
  - `fillRound`/`roundRect` are 390 B against CHGfx's 524 B (then `src/gfx/Draw.cpp`, now `chgame/Draw.cpp`).
  - The local `remapRect` saves 304 B SRAM and 182 B flash.
  - The `text35` font is 331 B against 952 B for CHGfx_Tiny3x5. Moving to `gfx_textFx` would cost 1.5 KB more flash and 384 B more SRAM.
  - Naive outlined text costs about 5 ms per line, which is why masks are used.
  - `gfx_scroll` takes 1.0 ms for 118 rows against 6.3 ms with `memmove`.
  - Code runs from flash with 3 wait states, so a call per pixel costs 2–3 µs.
  - The heaviest frame (bust with shake and banner) is 11 ms on CHGfx 1.3.
- **Estimated SRAM in the presenter** (my arithmetic, not documented): `flies[16]` about 320 B, `parts[48]` 480 B, `views[3][12]` about 430 B. Round's 312 B `shoe` is blackjack-only.

## 13. Reusable vs game-specific

**Copy as is** (what is now the CHGame library is included, not copied):
- Palette, Draw, Fmt, Mask (`chgame/`)
- the effects (`chgame/Fx.h`, `chgame/Sizzle`)
- `RamFunc.h`, input and pacing (`chgame/Input.h`); the `config.h` pattern
- saving (`chgame/Save`; the game's `Save.*` changes MAGIC and the data)
- the sound engine (`chgame/Audio`; the game's `Sounds.*` extends the Sfx enum for ball and wheel sounds)
- From `table::`: `wall`, `dealer`, `rail`, `plaque`, `arcText`/`arcLine`
- From `art::`: `chip`, `chipStack`, `chipDenom`, `badge`
- From `present::`: `speechBubble`, plus the Fly / chipsIn / sweep / collectT / pending / rolling-purse machinery
- `bar::` `button`/`layout` accordion
- From `Screens.cpp`: options, stats, credits, win, lose and fade framework
- The dealer assets with their face edits and alt remap

**Game-specific:**
- `Hand`, the shoe and card logic, `CardArt` card drawing, `table::shoe`/`felt`, card views and ghosts, `handGeom`, `badgeFor`, the insurance and peek flow
- The `Layout.h` felt rows, which must be redone for the wheel and betting grid
- `LOGO`, `PPOT_LOGO` and the credit lines