# CHRoulette: architecture, reuse, budget and phased plan

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

I based this on the seven map reports. I then checked them against the source. Diffs were run with `--strip-trailing-cr` on both games, and `check_size.py` was run on the existing BJ and CC build maps. `BJ` means `CHBlackjack` and `CC` means `CHChess`.

## 0. Refinements to the draft (decisions this plan takes)

1. **Ball solver: offset the rotor, not the ball.**
   - The ball is always launched from the same stator angle, on the croupier's side. The rotor's start angle is then shifted by a whole number of pockets so the ball lands on the target.
   - Hits on the deflectors depend only on the ball's absolute angle, so the physics stays consistent. After the drop, the rattle is simulated in the rotor frame as integer pocket hops, so shifting the rotor shifts the landing pocket exactly.
   - A live correction at the drop frame removes boundary rounding: it allows at most ±1 hop (§2.7).
2. **Bets are a sparse list, and settlement is state, not events.**
   - `Bet bets[24]` with stable slot indices, plus `last[24]` for rebet.
   - The rules emit one `Settle` event. The presenter reads each bet's `won` flag and amount from `bets[]` and runs the payout spot by spot.
   - BJ's 16-entry queue silently drops events when full. 24 per-bet events would overflow it.
   - `Event.amount` becomes `int32`. BJ's `int16` overflows: a $1000 table returns up to 36×1000.
3. **Palette and the green zero.**
   - Keep the FELT option with **GREEN / BLUE / PURPLE**. Drop RED felt, which clashes with the RED number cells.
   - On the layout, 0 is printed on the felt colour, as on real blue tables.
   - The wheel scene uses no felt (WOOD bowl on a NAVY/INK surround). While the wheel fills the table band, call `pal::setTheme(GREEN)`; call `pal::setTheme(opt.felt)` when the whip back starts. The zero pocket is therefore always green.
   - Fallback if the user prefers simplicity: drop the FELT option (about 150 B less, no swap).
4. **Fonts.** Keep BJ's 5×7 `gfx_text` for the plaque purse, menus and selected bar labels, for the BJ look. It is cut candidate #2 (−0.74 KB flash, −264 B SRAM).
5. **No splash.** BJ's splash is "PPOT presents", and Roulette is not a PPOT port. This drops `PPOT_LOGO` (288 B) and the splash code.
6. **Pure split of `fx::ease/isin/rnd`** into `Ease.*` (now the CHGame library's `chgame/Ease.cpp`). The ball simulation and the host tests can then link it without CHGfx. LTO makes this free.
7. **The rules own the cursor** (as BJ's `Round` does). Navigation is pure (`Nav.*`, CC's `nearest()` over a spot-position table). The host fuzz can then drive the real input path with buttons.
8. **Use CC's own `sprite4`, `dither` and `shiftRows` shake** instead of BJ's `gfx_sprite4` / `gfx_dither` / `gfx_scroll`. CC measured +244/+8/+164 B flash and +160/+176 B SRAM for the library versions.
9. **Debug builds on the device do not write flash** unless a script enables it with the hook `E 1` (planned as `Y 1`; see §1.2). Debug builds use the same save pages, and the pages are shared by every CHGame game (§5, etiquette).
10. **SELECT cycles the chip denomination** (user decision), so it is no longer a mute toggle. Mute moves to Options (SOUND OFF only).

---

## 1. Module map: `CHRoulette` (its own git repo)

Legend:
- **[V-BJ]** / **[V-CC]**: copied verbatim, prefix renames only (`CHBJ_`/`CHCH_` → `CHRL_`, `chbj`/`chch` → `chrl`, handshake → `CHRL`).
- **[M]**: merged.
- **[P]**: pattern taken from an existing file, content new.
- **[N]**: new.
- **[G]**: generated.

The left column says where each piece is now. What the plan copied into the game (input and pacing, Fmt, Palette, Draw, Mask, Ease, the sound engine, the flash code of Save, Debug, RamFunc) has been the CHGame library's `chgame/` since 2026-10-02; the game's own files sit beside the `.ino`, and the tools every game shares are the repository's `tools/`.

```
CHRoulette.ino        [P BJ CHBlackjack.ino loop()] loop unchanged; debugHook R/F/J/G/W/M/E
config.h              [P BJ config.h] CHRL_VERSION "0.1", CHRL_LEAN (= CHGAME_DEBUG && !CHSIM && !CHRL_FULL:
                      drops music scores + credits page ONLY; saving stays), CHRL_FPS 60
                      (the debug and profile switches are now the library's CHGAME_DEBUG, CHGAME_PROFILE)
LICENSE .gitattributes .gitignore   [V-BJ] (identical in both)
NOTICE README.md      [N] (§3.6)
chgame/Input.*        [V-BJ]  was the games' own CHGame.h/.cpp (diff vs CC: 2 comment lines)
chgame/RamFunc.h      [V-BJ comment] was a game copy tagged "chrl"; a game's RAMFUNC now gets the library's "app" tag
chgame/Fmt.*          [V]  (0 diff lines)
chgame/Palette.*      [M]  (§1.1)
chgame/Draw.*         [M]  CC Draw.cpp/.h + BJ panel()
chgame/Mask.*         [M]  (§1.1)
chgame/Ease.cpp       [N split] ease/CURVES, isin/SIN, rnd/rndRange/reseed (moved out of Fx; identical in both games)
Fx.*                  [M]  (§1.1); the body is now the library's chgame/Sizzle, Fx.h keeps the switches
Presenter.*           [P BJ Presenter] event drain, Fly pool, chipsIn/sweep/collectT/pending, rolling purse,
                      speechBubble, face/blink/look, band signatures/movingRows/overlay + NEW whip camera,
                      payout script, dolly, croupier glove, tote updates
chgame/Audio.*        [M]  BJ Audio.cpp + CC soft pulse (§1.1); the game's effects are Sounds.*
src/audio/Music.*     [G]  tools/make_music.py (guard #if !CHGAME_DEBUG)
Save.*, chgame/Save.* [M]  (§1.1)
chgame/Debug.*        [M]  (§1.1)
Roulette.*            [N, P BJ Round] rules (§2)
Spots.*               [N]  pure spot model: kinds, coverage, payout, names, positions (+ SpotData tables)
Nav.*                 [N, P CC Screens.cpp nearest()] pure, over Spots positions
Wheel.*               [N]  pure: EU_ORDER[37], US_ORDER[38] (37 = 00), isRed, colour class
Ball.*                [N]  pure ball sim + solver (Ease only; host-tested)
WheelArt.*            [N]  ring RAMFUNC, pocket LUT, body/turret/diamonds, ball/shadow draw
src/assets/WheelMap.* [G] tools/wheel.py: per-row spans + quadrant angle bytes + geometry consts
Layout.h              [P BJ Layout.h] lay:: wall/rail from BJ; table band; grid; plate; bar
Table.*               [M BJ subset] wall(), dealer(), rail(), plaque(), arcText/arcLine + NEW tote()
Felt.*                [N]  betting layout: grid, numbers, outside labels, coverage glow, bet stacks, dolly
ChipArt.*             [P BJ CardArt.cpp chip()/chipStack()] chip, chipStack, chipDenom, CHIP_* + NEW miniChip/miniStack
Bar.*                 [P BJ Bar.cpp] button()+layout() accordion kept; NEW bet bar ($1 $5 $10 $25 $100 CLR SPIN), ox param
Presenter.cpp, Remap.* (the glove; planned as Glove.*)
                      [P CC Stage.cpp update()/drawFinger()] Q4 glide, bob, tap, deny; RM_ID/RM_CPU/RM_ALERT/RM_HIT
Screens.*             [P BJ Screens.cpp] go/enter/fade (CC's (fadeOut-1)*2 to full black), still-screen sig,
                      optField options, stats + hold-SELECT reset, pause, toast, Win/Lose, demo, credits; new Title
src/assets/Assets.*   [G]  tools/assets.py
tools/chsim/chdrive.py [P CC] the game's script commands ("CHRL"; 'goto SPOT' -> "G <id>") on the repository's
                      tools/chsim (its gifsheet tool, and host/ with CC's sim_waitInput), which every game shares
tools/check_size.py tools/serialcap.py tools/requirements.txt tools/device.py   [V] now the repository's, shared
tools/game.py         `chgame test`'s sources (was tools/tests/run_tests.py [V-CC]): test_rules.cpp + Nav, Roulette,
                      Spots, Wheel + chgame/Ease.cpp, -DCHTEST; tools/tests/run_ball_tests.py for Ball.cpp
tools/tests/test_rules.cpp [N, P BJ test_rules.cpp idioms]   tools/tests/ref_roulette.py [N]
tools/audio/{preview.py,host/harness.cpp,host/Arduino.h} [V-BJ] (music-capable): now the repository's, for every game
tools/make_music.py   [P BJ] new TITLE, VICTORY+BROKE kept (BJ's own originals: "All tunes here are original")
tools/assets.py       [P BJ] palette/letters/load_png/pack_span4/pack_rows1 + BJ dealer pipeline (the PPOT lettering
                      is now tools/art/common/{youwon,broke}*.txt) + tools/art/common/{dealer.png (from BJ), hand.png
                      (from CC)} + tools/art/logo.txt (new)
tools/pixkit.py       [N] shared Python, now the repository's: PALETTE/NAMES, FONT35 (parsed from chgame/Draw.cpp), chip(), sprite loader
tools/wheel.py        [N] generator + preview PNG (planned as wheelmap.py)     tools/mockup.py [N] Phase-0 PNGs
tools/scripts/*.txt   [N] smoke, bet_tour, spin_forced, american, sc_*, save1/2, pace, showcase
```

### 1.1 Merges, function by function (verified by diff)

The merges were made in the game's copies. Since 2026-10-02 the merged Palette, Mask, Draw, Audio, Save and Debug are the CHGame library's `chgame/` files, and the body of Fx is `chgame/Sizzle`.

**`Palette.*`**: `Palette.cpp` differs by 67 lines and `Palette.h` by 23.

| Source | What is taken |
|---|---|
| Both, identical | `BASE[16]`, `RAINBOW[12]`, `init`, `setTheme`/`theme`, `setFade`/`fade`, `setFx`, `setCycling`, `resetClock`, `rgb444` |
| CC | `setMode()` with `Mode { CASINO, HOVER }`. Drop `TARGETS` and the `SHIMMER`/`PULSE` tables. |
| CC | `tri()`, and the dirty-only `tick()`, which marks dirty only when FX_A/FX_B move |
| CC | `memcpy` init |
| BJ | `setDesaturate()` (Lose screen) and `flash(idx, rgb, frames)` with its countdown in `tick()` |
| BJ | the `commit()` pipeline: flash → desaturate → fade → `to565`. Drop BJ's `gfx_pal[]` compare: CC's dirty tracking replaces it (−44 B flash, −32 B SRAM). |
| Edited | `THEMES` becomes {green, blue, purple} |

**`Mask.*`**: `Mask.cpp` differs by 151 lines.

| Function | Source | Notes |
|---|---|---|
| `maskBegin` | CC | memset |
| `maskText35` | CC | ORs whole scaled bit patterns per row |
| `runs` (RAMFUNC `maskruns`) | CC | writes nibbles directly |
| `dilateRow` (RAMFUNC `maskdilate`) | CC | |
| `maskBlit1` | BJ | for the logo and YOUWON/BROKE |
| `maskDraw` | BJ signature on CC's two-pass body | Signature `(m, x, y, fill, outline=-1, shadow=-1, ramp=nullptr)`. Body: shadow+outline per row, then fills. Layers are optional (no dilate if `outline < 0`). |
| Header | BJ | |

**`Fx.*`**: `Fx.cpp` differs by 120 lines and `Fx.h` by 17. Pure helpers move to `Ease.*`.

| Source | What is taken |
|---|---|
| Both | `Particle`, `parts[48]`, `spawn`, `banner`/`bannerActive`, the `drawBanner` core, `activeRows`, `clear`, the `update` core |
| BJ | `Kind { SPARK, CONFETTI, COIN, RAIN, STAR, DUST }` |
| BJ | `fountain(Kind, x, y, n)` (COIN → GOLD) |
| BJ | COIN bounce at y = 122 and RAIN, in both update and draw |
| BJ | `floatText`/`drawFloats` + `floats[4]`, including their lines in `activeRows`/`clear`/`update` |
| CC | `holdBanner()` plus the `bannerHeld` logic (`bannerT` wraps to 128) |
| CC | exported `const uint8_t RAIN[5]` (replaces BJ's local static in `drawBanner`) |
| CC | `particles()` (BJ's `particlesAlive` renamed) |
| CC | DUST `vy/2` in `burst` |
| CC | `drawParticles(uint8_t dust)` puff |
| CC | RAMFUNC `shake` `shiftRows` used by `applyShake` (now `chgame/Shake.cpp`) |
| Dropped | BJ `bannerLen` |
| New | `B_BLACK`: ramp r<3 SILVER, then INK; outline WHITE; shadow NAVY |
| New | `B_GREEN`: ramp r<3 WHITE, then FELT_LT, low rows FELT; outline INK; shadow FELT_DK |
| New | outline/shadow chosen per style from a 7-entry table |

**`Draw.*`**: take CC's file (`fillRound`/`roundRect`/`INSET`, RAMFUNC `sprite4` (remap required, so pass `RM_ID`), `dither`, `FONT35`/`IDX35`/`glyph35`, RAMFUNC `glyph`/`text35`, `text35Width`, `text35x2`) plus BJ's `panel()`.
- `spriteRot` and BJ's `remapRect` are not needed. Both would be GC'd anyway.
- BJ call sites change: `gfx_sprite4(x, …, nullptr)` → `sprite4(…, RM_ID)` and `gfx_dither` → `dither`.

**`Audio.*`**: take all of BJ's `Audio.cpp`: sequencer, music, `blip`, `setMode`, `mute`, LED, `tone(hz, smooth)`, hwInit. Add from CC:
- the `soft` narrow-pulse duty (`period/8`, CC's `tone()`);
- `soft = s >= Sfx::Tick` (now an effect's `audio::SOFT` flag);
- `blip(hz, ms, bool soft = false)` for ball ticks.

The new enum:

```
Cursor, Select, Deny, Chip, Coin, Whoosh, Win, BigWin(=BJ BLACKJACK), Lose, Broke,
Spin(launch sweep), Rattle(CC CAPTURE's first 8 steps), Thunk(CC LAND), Tick, Tock (soft, last)
```

Songs are `Title` (new), `Victory` and `Broke`.

**`Save.*`**: `Save.cpp` differs by 91 lines.

| Source | What is taken |
|---|---|
| BJ | the Record shape and the `load(Roulette&, bool &hasGame)` / `store(const Roulette&, bool)` API |
| CC | the page built in `gfx_chunkScratch()` (no static 256 B buffer: −256 B SRAM), and `best()` |
| Both (common) | RAMFUNC `pageWrite`, `crc32`, `imageEnd`/`twoPages`/`available`, sim `simFlash` |
| New | `MAGIC 0x4C524843` ("CHRL"), `VERSION 1`, the extended Record (§2.9) |
| New | `#if CHGAME_DEBUG && !CHSIM`: `store()` is a no-op unless the hook allows it (`save::allowWrites`) |

**`Debug.*`**: `Debug.cpp` differs by 90 lines.

| Source | What is taken |
|---|---|
| CC | `OK`/`ERR` hook replies and stack paint / `stk=` (drop `fstk`/`frameStack`) |
| BJ | the `?` reply `CHRL 0.1 frame=N lock=N`, `OK <frame>` on L/N, PERF `upd`/`wait` (`markWaitStart`), `line[48]` |
| Dropped | CC's `holdGame`/`held[100]`/`waitInput` (chess-search only) |

### 1.2 Debug hook letters

The reserved protocol letters are `? S K L N P T B` (the library's `chgame/Debug.h` now also takes `!` and `Q`). The game hook uses:

| Cmd | Meaning |
|---|---|
| `R <seed>` | reseed the rules RNG |
| `F <n[,n..]>` | force the next pockets, queue of up to 8 (37 = 00) |
| `J <T\|P\|W\|L\|O\|S\|C>` | jump to a screen |
| `G <spot>` | teleport the glove |
| `W <spot> <amount>` | place a bet through `Roulette::place` (money conserved) |
| `M <amount>` | set the purse |
| `E 1/0` | allow device flash writes (planned as `Y`, which CC uses for its render profile) |

`J` resets `frameCount`, `pal::resetClock()` and `fx::reseed()`, as BJ's `debugJump` does.

---

## 2. Rules: `Roulette.*` (+ Spots, Nav, Wheel)

### 2.1 Spot model (pure, shared ids)

Ids, in this order:

| Range | Spots |
|---|---|
| 0..36 | straight n |
| 37 | straight 00 (US only) |
| next 102 | regular inside: horizontal splits n/n+3 (33), vertical splits n/n+1 (24), streets (12), corners (22), six lines (11) |
| next 12 | outside: COL1-3, DOZ1-3, 1-18, EVEN, RED, BLACK, ODD, 19-36 |
| next 7 | bar (nav only): $1 $5 $10 $25 $100 CLR SPIN |
| last | the zero block. EU (6): 0/1, 0/2, 0/3, 0/1/2, 0/2/3, first four 0/1/2/3. US (9): 0/00, 0/1, 0/2, 00/2, 00/3, 0/1/2, 0/00/2, 00/2/3, top line 0/00/1/2/3. |

- Bet spots: **EU 157, US 161**; nav targets 164 / 168 (fits a `uint8`).
- All ids except the zero block are the same on both wheels. `last[]` is still cleared on a wheel change.
- Coverage is computed from (kind, anchor) by formula. The zero block uses an explicit ≤5-number table.
- `size(id)` is the number of pockets covered.
- **Payout multiplier = `36 / size`**, which returns stake + win: 1→36 (35:1), 2→18, 3→12, 4→9, 5→7 (top line 6:1), 6→6, 12→3, 18→2.
- On 0/00, outside bets simply aren't covered, so they lose. No La Partage.
- `name(id, buf)` builds the plate text, for example:
  - "SPLIT 17/20" + "17 TO 1"
  - "CORNER 17/18/20/21" + "8 TO 1" (26 chars, fits 124 px)
  - "TOP LINE 0/00/1/2/3" + "6 TO 1"
  - "2ND DOZEN 13-24" + "2 TO 1"

  Every character used is in FONT35, `/` and `-` included.
- Positions: `spotXY(id)` by lattice formula, plus small tables for the zero block, outside and bar. Exact cell geometry is fixed in P0; provisionally 8×10 cells, zero column 11 px, column bets 15 px, grid rows 48..95.

### 2.2 API

```cpp
enum : uint8_t { WHEEL_EURO, WHEEL_AMERICAN };   enum : uint8_t { GOAL_1000, GOAL_5000, GOAL_ENDLESS };
struct Options { uint8_t wheel, goal, pace, sound, felt, dealer, unused, pad; };   // 8 B, all-zero default
struct Stats { uint32_t spins, spinsWon, wagered; int32_t bestPurse, biggestWin;
               uint16_t gamesWon, gamesBroke, straightHits, pad; uint16_t hits[38]; };  // 104 B
struct Bet   { uint8_t spot, won; uint16_t amount; };                                  // amount 0 = free slot
struct Event { Ev type; uint8_t a, b, c; int32_t amount; };                            // 8 B
class Roulette {
public: Options opt; Stats stats; int32_t purse;
  Bet bets[24], last[24]; uint8_t history[8], pocket, cursor, chip; Phase phase;
  void newGame(); void resume(); void seed(uint32_t s);            // seed: BJ Round::seed() idiom
  void update(uint8_t pressed, uint8_t repeat, bool fxBusy);
  bool popEvent(Event&); int32_t onTable() const; int32_t goal() const; uint8_t pockets() const;
  int32_t returnFor(const Bet&, uint8_t pocket) const; const char *lineText(uint8_t line, char *buf) const;
  bool place(uint8_t spot, uint16_t v); void force(uint8_t p); void setWheel(uint8_t w);   // refunds bets, clears last
private: uint32_t rng; uint16_t wait; uint8_t step, forced[8], nForced; Event q[16]; uint8_t qHead, qLen;
  uint32_t rand32();   // BJ Round::rand32() xorshift32, zero -> 0x9E3779B9
};
```

- RNG: seeded at the first title selection with `micros()*2654435761u ^ frameCount`, as BJ's `seedOnce()` does.
- Pocket: `forced` if any, else `rand32() % pockets()`. The bias of 2^32 mod 37 is negligible; tests check it.
- Demo mode seeds with `fx::rnd()`. Presentation randomness (ball variation) uses `fx::rnd` only.

### 2.3 Phases

Paces are in frames and halve on QUICK through `pace()`, the BJ idiom.

| Phase | Behaviour | Exit |
|---|---|---|
| Welcome | `say(L_WELCOME` or `L_GOOD_LUCK)`, pace 30 | Betting |
| Betting | input (§2.5) | A on SPIN with ≥1 bet → NoMoreBets |
| NoMoreBets | emit `NoMoreBets`, `say(L_NO_MORE, F_RAISED)`, pace 40 | Spin |
| Spin | pick the pocket, emit `Spin(a = pocket)` | `!fxBusy` (the presenter whips, launches and lands) → Result |
| Result | `spins++`, `hits[p]++`, push to history, emit `Result(p)`, `say(L_RESULT, F_NORMAL, c = p)` ("17 RED"), pace 60 | Settle |
| Settle | per bet: `ret = returnFor`, set `won`; `purse += Σret`; update stats (`wagered`, `spinsWon` if Σret > stake, `biggestWin` = max net, `straightHits`); emit `Settle(amount = Σret)`; `say(L_WINNER, F_ANGRY` / `L_BIG, F_SURPRISED` / `L_HOUSE, F_SMILE)` | `!fxBusy` (payout script done) → EndOfSpin |
| EndOfSpin | copy bets to `last`, clear `bets`, update `bestPurse`; `purse ≥ goal` → `gamesWon++`, `GameOver(1)` → GameWon; `purse < 1` → `gamesBroke++`, `GameOver(0)` → GameLost; else rebet if `Σlast ≤ purse` (emit `Rebet(Σ)`), `say(L_PLACE)` | Betting |
| GameWon / GameLost / Quit | terminal; `Screens` persists and changes screen (BJ `playUpdate()`) | |

The croupier keeps BJ's personality: he smiles when you lose and scowls when you win.

### 2.4 Events

| Event | Fields |
|---|---|
| `BetAdd` | a = slot, b = spot, c = denom, amount |
| `BetRemove` | a = slot, b = spot, amount |
| `Clear` | amount |
| `Rebet` | amount (the presenter flies each `bets[i]` in from the player) |
| `ChipSel` | a = denom |
| `Cursor` | a = spot |
| `Deny` | a = reason: purse / spot limit / table limit / too many spots / no bet |
| `NoMoreBets` | |
| `Spin` | a = pocket |
| `Result` | a = pocket |
| `Settle` | amount |
| `Say` | a = line, b = face, c = pocket |
| `GameOver` | a = 1 won / 0 broke |

### 2.5 Input in Betting

- **D-pad.** A direction in `pressed` (a tap) runs `Nav::nearest` over **all** spots, including lines, corners and the bar. A direction only in `repeat` (auto-repeat) runs it over **whole-cell** spots only: straights, outside and bar.
- **Diagonals.** Pressing two directions gives a diagonal; CC's scoring supports it.
- **Edges.** No wrap at edges: emit `Deny`, and the glove shows `RM_ALERT`. Confirm in P0.
- **A.**
  - On a bet spot: place `CHIPS[chip]`. It repeats while held, at `repeat(18, 5)`.
  - On a chip button: select that denomination.
  - On CLR: clear all bets.
  - On SPIN: spin, or `Deny` if there are no bets.
- **B.** On a spot with a stake, remove `min(chip value, stake)`; it repeats. Otherwise `Deny`.
- **SELECT.** Cycle the denomination; emits `ChipSel`.
- **START.** Not passed to the rules (`pressed & ~START_BUTTON`); `Screens` opens the pause menu.
- **Limits.** Chips {1, 5, 10, 25, 100}. Max $200 per spot, $1,000 on the table, 24 distinct spots. Money leaves the purse when a chip is placed, as in BJ.
- **Changing the wheel in Options** refunds every bet, clears `last` and emits `Clear`.

### 2.6 Options and lines

Options rows use the BJ `optField` idiom with `static_assert(sizeof(Options) == 8)`:

```
"WHEEL|EUROPEAN|AMERICAN" "GOAL|$1000|$5000|ENDLESS" "PACE|FUN|QUICK"
"SOUND|LEAD|ARPEGGIO|OFF" "FELT|GREEN|BLUE|PURPLE" "DEALER|CLASSIC|NIGHT" "BACK"
```

Lines (at most 12 characters × 4 lines):
- WELCOME!\nPLACE YOUR\nBETS
- PLACE YOUR\nBETS
- NO MORE\nBETS!
- 17 RED / 0 GREEN / 00 GREEN (built)
- WINNER!
- INCREDIBLE!
- THE HOUSE\nTHANKS YOU
- GOOD LUCK

### 2.7 Ball (`Ball.*`, pure)

**State:**
- Ball: `θb` (u16 turn), `ωb` (Q16/tick), radius `ρ` (Q8: rim to pocket ring).
- Rotor: `θr`, `ωr` (opposite sign, slow decay).
- Phases: ORBIT → DESCEND (diamond hits: `ωb *= 3/4`, bounce up, emit Rattle) → RATTLE (rotor frame: `idx` plus parabolic hops of ±1 pocket with decaying height; Tick/Tock each landing) → SEATED (rides the rotor; emit Thunk).
- Variation, chosen up front from `fx::rnd` seeded per spin: `ωb ±10%`, `ωr ±10%`, 1–4 hops.
- Duration: FUN about 360 ticks, QUICK about 180.

**`plan(target, wheel, pace, seed)`:**
1. Run headless with rotor offset 0 to get entry index `e0` and hop sum `H`.
2. Choose `m` so that `(e0 − m + H) mod N == targetIdx`.
3. Set `θr0 = m × 65536 / N`.
4. At the live drop frame, recompute `e` from `θb − θr`. If `e + H ≠ targetIdx`, adjust the last hop by ±1.

The `step()` used by the solver and by the presenter is the same code, so landing is exact by construction and checked in tests.

**Quantisation.** The map generator (`tools/wheel.py`) stores quadrant angles as 0..127, which is 512 per turn and free in flash.
- Default: a 256-entry LUT indexed with `>>1`, at 256 B SRAM.
- Upgrade: a 512-entry LUT (+256 B SRAM) if the slow rotor looks steppy.
- A seated ball is drawn at its quantised pocket centre, so it never jitters against its pocket.

### 2.8 Stats

| Shown as | Field |
|---|---|
| SPINS | `spins` |
| SPINS WON | `spinsWon` |
| WAGERED | `wagered` |
| BIGGEST WIN | `biggestWin` |
| BEST PURSE | `bestPurse` |
| STRAIGHT UPS HIT | `straightHits` |
| BANKS BROKEN | `gamesWon` |
| TIMES BROKE | `gamesBroke` |
| HOT n (xk) / COLD n (xk) | from `hits[38]` |

Hot/cold costs 76 B; it is cut candidate #4.

### 2.9 Save record (static_assert ≤ 256)

| Offset | Field |
|---|---|
| 0 | `magic` u32 = 0x4C524843 |
| 4 | `version` u16 = 1 |
| 6 | `seq` u16 |
| 8 | `purse` i32 |
| 12 | `hasGame`, `nLast`, `nHist`, `pad` (u8 each) |
| 16 | `Options` (8) |
| 24 | `Stats` (104) |
| 128 | `Bet last[24]` (96) |
| 224 | `history[8]` |
| 232 | `crc` |
| **236 B** | total |

- CONTINUE restores the purse, the tote and your last layout as the rebet. The purse is restored only if `hasGame && purse > 0`, as in BJ.
- `persist()` returns early in demo mode, then calls `gfx_wait()` before `save::store` (chunk scratch).
- It autosaves every 5th spin, on SAVE + QUIT, and on GameWon/GameLost (`hasGame = false`).

### 2.10 Demo (attract mode)

As in BJ (`titleUpdate()`, `demoInput()`), the demo starts after 600 idle frames on the title once the tune has ended.
- `demoInput()` builds a plan of 2–4 showy spots (a straight, a split, RED, a dozen).
- It walks the glove there with synthetic D-pad taps every 12 frames, chosen with `Nav` (the `toward()` idiom), and taps A 1–3 times.
- It then walks to SPIN and presses A. After 3 spins it returns to the title.
- A blinking "DEMO" label is shown. Any real press runs `save::load` and returns to the title.

---

## 3. Screens

1. **Splash: dropped** (§0.5).
2. **Title.**
   - The live wheel is the same renderer, slowly spinning, with an occasional ball orbit.
   - Logo: a new 1 bpp **"Roulette"** in BJ's chunky mixed-case style (tools/art/logo.txt, about 110×16, ~220 B), drawn with `maskBlit1` and BJ's ramp: FX_B rows < 3, GOLD, WOOD, INK outline, WINE shadow. P0 shows it next to `title35("ROULETTE")` at scale 4 (124 px, CC's zero-cost style); the user picks one.
   - Menu: PLAY | CONTINUE $n / NEW GAME | OPTIONS | STATS. 5×7 text; selected items in a NAVY panel with a FX_B/GOLD border.
   - The title redraws every frame because the wheel moves (about 5–6 ms).
3. **Play:**

   | Rows | Content |
   |---|---|
   | 0..41 | Wall: pinstripe, dealer at (2,0), plaque/bubble at x 50..101, **tote board** at about (104,3) 22×38 in NAVY/GOLD |
   | 42..45 | Rail and chip rack |
   | 46..127 | Table band: either the layout scene (grid 46..99, plate at y 100, trim 111, bar 112..127) or the wheel scene (46..127, centre about (64,88), rim rx 62 / ry 31) |

   - The tote board shows the last 5 results like a real marquee: black numbers left-aligned, red right-aligned, zero centred.
   - Whip: 12 frames `IN_OUT`, each scene drawn with `ox`, plus speed streaks and `Whoosh`.
   - Pause: RESUME / OPTIONS / SAVE & QUIT (5×7 has `&`).
4. **Options and Stats:** BJ layouts. Stats has the hold-SELECT 90-frame reset bar.
5. **Credits, the "back room":** keep (BJ `creditsUpdate()`/`creditsRender()`), gated `#if !CHRL_LEAN`. Credit lines:
   - "Thanks for\nplaying!"
   - "Croupier and\nlettering by\nvampirics"
   - "Press Play\nOn Tape"
   - "Font by\nPress Play\nOn Tape"
   - "Another\nspin?"

   It is cut candidate #1.
6. **Win and Lose:** BJ's sunburst + YOUWON1/2 and desaturate + BROKE1/2, unchanged. Win plays `Song::Victory`; Lose plays `Song::Broke` and shows the croupier `E_SMILE`.

### 3.6 Attribution (checked against BJ's `NOTICE` and its README's credits, and the PPOT clone)

- PPOT Blackjack: "Simon Holmes (filmote), code, and Stephane C (vampirics), art". The PPOT repo's own README names no artist (its commits are by filmote), so **mirror BJ's NOTICE wording exactly**.
- The croupier is PPOT's dealer, recoloured and retouched by hand in `dealer.png` (then BJ's `tools/art/`, now the shared `tools/art/common/`). FACE_EDITS come from PPOT's expression PNGs.
- YOUWON / BROKE lettering: PPOT `YouWon_01/02.png` and `YouAreBroke_01/02.png` (now `tools/art/common/youwon1.txt`, `youwon2.txt`, `broke1.txt`, `broke2.txt`).
- 3×5 font: PPOT (`Font3x5.cpp`). The font file carries no Pharap header; PPOT's `Game.*` and `GameContext.*` do, but none of that code is used. **Pharap appears nowhere.**
- The glove (`hand.png`) and everything else are new (bateske).
- NOTICE text:
  - "Copyright 2026 bateske".
  - From CHBlackjack (Apache-2.0): input and pacing, palette, drawing, lettering, effects, sequencer, saving, debug, simulator, croupier art and lettering.
  - From CHChess (Apache-2.0): glove art, nearest-in-direction navigation, word plate, hover palette mode, soft ticks, shake routine.
  - The PPOT paragraph above.

**Music.** BJ's sequencer + `make_music.py`.
- A new TITLE: a Parisian musette waltz in 3/4, 8 bars, loops, about 450 B, written to sit in the 1–4 kHz band like BJ's.
- Keep VICTORY (169 B) and BROKE (164 B); they are BJ originals.
- The user auditions the WAVs from `chgame audio` (`tools/audio/preview.py`) in P6.

---

## 4. Budget

### 4.1 Flash

Measured baseline: **BJ release image 45,832 B** and static RAM 15,736 B. Other measurements:
- BJ debug: 46,532 B (+700 with LEAN).
- CC release: 48,884 B; CC debug: 49,744 B.
- The LTO/no-LTO ratio is about 0.90 (from BJ `build/os`). That build looks stale, so it is used only as a ratio.

| Line | Bytes | Basis |
|---|---:|---|
| BJ release image | 45,832 | measured |
| − Round (shoe, hands, insurance, split, peek, BJ bars) | −4,400 | Round.cpp 4,815 no-LTO ×0.9 |
| − Presenter card parts (drawHand 836, badgeFor 482, handGeom 302, views/flip/ghost/peek/shuffle) | −3,000 | symbols + estimate |
| − CardArt card drawing (card 590, span1/span1Rot/glyphRot 252, shadow 98, …) | −1,150 | symbols |
| − card and PPOT assets (SUIT, RANK, PIP9/13, COURT×3, LOGO 182, PPOT_LOGO 288) | −1,240 | exact |
| − Table shoe + felt print | −500 | est. |
| − Bar Play/Insurance/End/status bars | −600 | est. |
| − Splash + title card deal-in | −600 | est. |
| − BJ TITLE score (replaced below) | −453 | exact |
| − `gfx_sprite4` + `gfx_dither` + `gfx_scroll` → CC's own | −416 | CC notes 244+8+164 |
| **Kit after removals** | **≈33,470** | |
| + Roulette rules (phases, input, limits, rebet, stats) | +1,900 | est. |
| + Spots (coverage, payout, names, position tables) | +700 | est. |
| + Nav + Wheel orders | +320 | |
| + Presenter roulette (events, payout script, whip, dolly, croupier glove, tote) | +2,400 | est. |
| + Felt layout + mini stacks + coverage glow + plate | +1,500 | est. |
| + Glove (HAND 111 + logic) | +350 | CC Assets + Stage |
| + WheelArt (ring RAMFUNC ~250 counted once in the image, LUT ~150, body/turret/diamonds ~550) | +950 | est. |
| + WheelMap data ≈ π/4·(Rxo·Ryo − Rxi·Ryi) + spans | +1,000 | for 58×29 / 40×20 |
| + Ball sim/solver + draw | +800 | est. |
| + merges (holdBanner, B_BLACK/B_GREEN, DUST puff, HOVER mode, soft audio) | +250 | |
| + Screens deltas (live-wheel title, option rows, hot/cold) | +500 | |
| + new logo + new TITLE tune + new Sfx tables | +770 | |
| **Projected release** | **≈44,900** | uncertainty ±25% of new code: **42.0–47.8 KB** |
| Debug (protocol ~1.9 KB + USB serial 0.6 − music scores 0.79 − credits ~0.9) | **≈+800** | BJ measured +700 |

- **Gates:** release ≤ 48,500 at the end of P6; debug ≤ **50,432** (both save pages).
- Both builds run `check_size.py` at the end of every phase. The CC precedent (48.9 / 49.7 KB) shows there is room even at the top of the range.

### 4.2 SRAM (limit 18,416)

| Line | Bytes |
|---|---:|
| BJ release static | 15,736 |
| − Round object (incl. shoe 312) / card views / ghosts / save buf | −564 −432 −72 −256 |
| − CHGfx sprite4/ditherspan/shiftrow RAMFUNCs net of CC's own | ≈ −396 |
| + Roulette object (opt 8, stats 104, purse, bets 96, last 96, history 8, q 16×8 = 128, misc ~40) | +484 |
| + presenter state (disp[24] 48, payout cursor, glove, dolly, whip, wheel, ball ≈ 76) | +124 |
| + pocket LUT | +256 |
| + ring RAMFUNC (+ LUT builder) | +300 |
| **Projected** | **≈15,180 (headroom ≈3.2 KB)**; debug +~120 |

The stack is checked with CC's `stk=` in `P`. The deepest path is render → `maskDraw` (two 32 B row buffers) and the headless ball plan.

### 4.3 Frame cost (device estimates; draw budget about 8.3 ms)

| View | Cost |
|---|---|
| Betting, idle | glove bob redraws at 7.5 Hz only (signature skip, CC idiom) |
| Betting, glove moving | table band ~2.5–3 ms + bar 0.4 ms |
| Whip | layout @ox ~2.7 + wheel ~2.0 + streaks ≈ 4.8 ms |
| Spin | wheel ring 0.45–0.65 + bodies ~0.6 + LUT 0.05 + ball 0.05 ≈ 2 ms; + talking wall ~1.2; + result banner ~2.5 ≈ 5.7 ms worst |
| Payout | band + flights + particles + banner + shake ≈ 6–7 ms |

### 4.4 Cut list (priority order, only if a gate is missed)

1. Credits back room (−0.9 KB).
2. 5×7 `gfx_text` → `text35`/`text35x2` (−0.74 KB, −264 B SRAM).
3. Attract demo (−0.4 KB).
4. Hot/cold `hits[38]` (−0.25 KB, −76 B SRAM).
5. New TITLE tune (−0.45 KB), or all music (−1.6 KB; fanfares as effects, the CC way).
6. FELT option and the forced-green swap (−0.15 KB).
7. Smaller wheel ring radii (map data scales with ring area).
8. American wheel (−0.4 KB).
9. YOUWON/BROKE bitmaps → `title35` (−0.8 KB, loses PPOT lettering).

---

## 5. Phased plan

**Host setup:**
- Every phase uses `CHSIM_CXX="<zig> c++"`. There is no native compiler, and the path has no spaces, as `tools/chsim/chsim.py` needs.
- The sim builds with `chgame sim`, run in CHRoulette (then `python tools/chsim/chsim.py build .`).
- Scripts run with `chgame run tools/scripts/<s>.txt out/<s>` (then `chdrive.py --sim . ... --id CHRL`). Any `BUG:` line (drawing during a flush, or scratch use mid-flush) fails the run.
- Device compiles use `chgame build [--debug]` (then `python tools/device.py build`), which runs `tools/check_size.py`.

**Device etiquette (every phase that touches the board):**
1. Announce before each upload: "uploading a DEBUG build to COMx: the board reboots".
2. Batch device work into short sessions.
3. Finish with `chgame upload` (release) and say that it is restored.
4. Debug builds don't write flash unless a script sends `E 1`. The save tests (`save1`/`save2`) overwrite the board's save; the two pages are shared with any other CHGame game. Run them only with the user's explicit OK.

| Phase | Deliverable | Verification / what the user sees |
|---|---|---|
| **P0 Mockups** | `tools/pixkit.py` + `tools/mockup.py` + `tools/wheel.py` (preview mode). They use the real palette, FONT35, `dealer.png`, `hand.png`, the `art::chip` algorithm and the real wheel geometry. Output `docs/mockups/*.png` at 3× (gitignored): title (logo A/B), betting (glove on SPLIT 17/20 with plate), corner hover with coverage glow, US layout, NO MORE BETS bubble, 3-frame whip strip, wheel mid-spin, result "17 RED" (B_RED/B_BLACK/B_GREEN), payout (dolly + flying chips + rolling purse), big win, felt themes strip. Also reports the wheel-map byte count. | **Approval gate:** the user picks cell geometry, wheel radii, logo, edge wrap, palette decision. Budget line 4.1 is updated with the real map size. |
| **P1 Skeleton + infra** | `git init`; verbatim copies, merges (§1.1), renames; `config.h`, `.ino`; `assets.py` (DEALER/FACE_*/ALT/YOUWON/BROKE/HAND/logo); Save (CHRL), Debug; Screens: title stub, options, stats, pause, toast, Win/Lose; Play shows wall/dealer/plaque/tote/rail on empty felt. | `assets.py` output **byte-identical** to BJ's `Assets.cpp` for the dealer arrays and CC's HAND. Sim builds; `smoke.txt` makes a screen sheet with no BUG. Release and debug compile; `check_size` baseline (expect about 34 KB). The user sees the sheet and the size report. |
| **P2 Rules + host tests** | `Ease`, `Wheel`, `Spots`, `Nav`, `Roulette`; `test_rules.cpp`; `ref_roulette.py` | `chgame test` (then `run_tests.py`) → "N checks, 0 failures" (UBSan); reference cross-check "157/161 spots × 37/38 pockets match". The user sees the summary and a sample spot/plate list. |
| **P3 Betting view** | Felt, ChipArt mini stacks, Glove (HOVER palette mode), coverage glow, plate, Bar (5 chips, CLR, SPIN), presenter betting flights (drop from the glove, remove, clear, rebet from the player), plaque total, tote | `bet_tour.txt` (taps through every spot class, repeat-skips-lines, bar) → snaps + GIF; sim perf estimate (`perf`/`cal`) ≤ 4 ms; size check |
| **P4 Wheel + ball** | `tools/wheel.py` → `src/assets/WheelMap.cpp`; WheelArt (ring RAMFUNC leaf, word reads from flash, x-clip); LUT (pocket colour pairs as nibbles, frets, win → FX_A); body/turret/diamonds; Ball + solver; croupier glove launch; whip; forced-green swap; Spin/Rattle/Tick/Tock/Thunk | Host ball tests (§6.4). `spin_forced.txt` (`F 17`, `F 0`, `F 37`) → EU/US spin GIFs. **First device session:** debug upload, whip and spin frames, `P` max < 8.3 ms and `late=0`, then restore release. |
| **P5 Payout** | Result banner + pocket rainbow + tote; whip back; croupier glove places the dolly; staggered losing sweeps (STACK_TO_TRAY); per-spot `chipsIn` from the rack → `collectT` → STACK_TO_PLAYER; rolling purse with `pending`; big win (B_RAINBOW, confetti + coin fountains, LED_PARTY, BigWin fanfare); faces and lines | `sc_win_straight`, `sc_lose_all`, `sc_mixed` (10 bets), `sc_zero` (even money loses), `sc_bigwin` → GIFs. A script checks the plaque settles at exactly `rules.purse` (`M`/snap). No BUG lines. |
| **P6 Screens / save / options / music / demo / credits** | Final title, options, stats + hot/cold + reset, CONTINUE, demo, credits, Win/Lose, new TITLE tune | `sc_screens.txt`; `save1` → rebuild sim → `save2`; `chgame audio out/audio` WAVs for audition; release ≤ 48,500 and **debug ≤ 50,432** |
| **P7 Device bring-up** | Debug upload (announced): `pace.txt` (~300 frames / 5 s, `late=0`), worst-frame script, `stk`; save persistence across re-upload only with permission; then the release upload | PERF table to the user; the user plays the release build |
| **P8 README + GIFs** | `showcase.txt` → `docs/{title,betting,spin,payout,win,broke}.gif`; README in series format (pitch, GIF table, credits, install, controls, rules/options, "How it fits" with final sizes and perf, development); NOTICE | The user reviews the README; commits only when the user asks |

---

## 6. Test plan (`tools/tests`, BJ `CHECK`/`CHECK_EQ` harness)

1. **Payouts and coverage against an independent Python reference.**
   - `ref_roulette.py` derives bets from a grid model: edge-adjacent pairs as splits, 2×2 blocks as corners, columns of 3 as streets, adjacent street pairs as six lines, and casino rule lists for the zero bets.
   - The test exe's `--dump` prints, per wheel: spot id, name, coverage set, and multiplier for every pocket. Python diffs the two.
   - **Invariants:**
     - Σ over all pockets of `returnFor(spot, stake 1)` = **36** for every spot, except the US top line = **35** (7.89% edge).
     - Spot counts by kind: EU 37/60/14/23/11/3/3/6 = 157; US 38/62/15/23/11/3/3/6 = 161.
     - Each n in 1..36 is covered by exactly 1 straight, 2–4 splits, 1 street, 1–4 corners, 1–2 six lines, 1 column, 1 dozen and 3 even-money bets.
     - 0 and 00 lose every outside bet.
     - Plate names fit 124 px.
2. **Wheel tables.** Each pocket appears once. Colours alternate around the wheel, zeros excepted. Known neighbours: EU 0 lies between 26 and 32; US 0 is opposite 00.
3. **Navigation.**
   - From every spot and all 8 directions a tap gives a spot or a deny.
   - A BFS over taps reaches every spot, bar included.
   - Repeat-only moves never land on a line spot.
   - RIGHT-repeat from 1 visits 4, 7, …, 34.
4. **Ball solver.** For EU and US, FUN and QUICK, every pocket, and seeds 1..200:
   - seated pocket == target;
   - |H′ − H| ≤ 1;
   - duration within its pace window;
   - deterministic per seed;
   - no UB.
5. **Rules flow.**
   - Limits: spot $200, table $1,000, 24 spots, purse.
   - B removes min(chip, stake); CLR refunds all; SELECT cycles the chip.
   - SPIN with no bet → Deny.
   - Forced pocket honoured; stats increments.
   - Goal → GameWon; broke → GameLost.
   - Rebet is exact, skipped if unaffordable, and cleared on a wheel change.
6. **Money conservation fuzz** (port of BJ `test_rules.cpp`'s `testFuzz()`): 40 seeds × 400 spins, random buttons, both wheels.
   - `purse + Σbets == money` from Betting to Settle.
   - After Settle, `money' = money − stake + Σret`.
   - `purse ≥ 0`, no stall (>20,000 frames), and the event queue never drops (a test hook counts drops).
7. **RNG.**
   - `rand32() % 37` and `% 38` over 3.7 M draws: chi-square below the 0.999 critical value (df 36 ≈ 68, df 37 ≈ 69); each count within 5σ.
   - Seed 0 → `0x9E3779B9`.
8. **Save:** `static_assert(sizeof(Record) ≤ 256)`. The sim `save1`/`save2` scripts cover CRC, alternating seq and the CONTINUE round trip.

### Critical files for implementation
- CHBlackjack\Presenter.cpp (Fly pool, chipsIn/sweep/collectT, rolling purse, band signatures, speech bubble)
- CHBlackjack\Round.cpp (and Round.h: phase machine, `pace()`/`go()`, event queue, RNG, bet input; the template for `Roulette`)
- CHChess\Stage.cpp (glove glide/bob/tap/deny, `RM_*` remaps, `plate()`) and CHChess\Screens.cpp (`nearest()`)
- CHBlackjack\Screens.cpp (screen skeleton, options, stats, pause, demo, credits, Win/Lose, `persist`)
- CHChess\Save.cpp, CHBlackjack\Fx.cpp and CHChess's Mask.cpp (the merge sources; the merged Save, Fx body and Mask are now the library's `chgame/Save.cpp`, `chgame/Sizzle.inl`, `chgame/Mask.cpp`), plus CHBlackjack\tools\assets.py and CHChess\tools\chsim\chdrive.py