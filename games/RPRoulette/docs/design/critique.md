# CHRoulette spec review: verdict, issues with fixes, decisions for the user

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

Path shorthand: `BJ` = `CHBlackjack`, `CC` = `CHChess`, `GFX` = the board package's CHGfx 1.3.0 (`platform\board\arduino\CHGame\libraries\CHGfx`).

## 0. Verdict

The three specs are strong but don't agree with each other.
- **Adopt Spec A whole** for the layout pixels, spot lattice, navigation and plate.
- **Adopt Spec B whole** for the wheel map, LUT, ring loop, ball sim and solver.
- **Adopt Spec C** for the module map, the merges, the phases and the test plan.
- Then fix the conflicts in §1 and the logic holes in §3.
- Flash and SRAM should fit. The real risks are:
  - landing and big-win frames going past 8.3 ms;
  - SAVE+QUIT or OPTIONS during a spin;
  - the "0 GREEN" wording against the felt themes.

## 1. Contradictions between the specs (adopt the resolution)

1. **Layout geometry.**
   - A: 9 px pitch, 0-column 9, 2:1 column 11, number grid at rows 48..78.
   - C: "8×10 cells, zero column 11, column bets 15, grid 48..95".
   - **Use A.** C's 8 px pitch leaves 7 px inside a cell, and a two-digit number is 7 px, so it can't fit with any margin.
2. **Spot set and ids.**
   - A: US 159, dropping the 0/2 and 00/2 splits; lattice ids `v*24+u`.
   - C: US 161 with those two splits; ids grouped by kind.
   - **Use A's ids and spot set** (but ask the user, §8.6). Fix C's test invariant to 38/60/15/22+1/11/12 = 159.
3. **Bet storage.**
   - A: dense `uint8_t bet[159]`, kept after settlement as the rebet; 40 B of presenter bitsets.
   - C: sparse `Bet bets[24], last[24]` plus a "24 distinct spots" limit.
   - **Use A's dense array, with C's "settlement is state" idea:** one `Settle` event, and the presenter walks `bet[]` with `covers(id, pocket)`.
   - This drops the arbitrary 24-spot limit and the 192 B of C's arrays.
   - Save the rebet layout as up to 48 `(spot, amount)` byte pairs (96 B, same record size as C).
4. **Limits.**
   - A: $100 inside, $250 outside, $1,000 table; this is what keeps `uint8_t` valid.
   - C: $200 on every spot.
   - Pick one set (§8.3) and `static_assert` the cap ≤ 255.
5. **Wrap.** A wraps on taps; C never wraps and plays Deny. CC wraps (`CC\Screens.cpp`, `nearest()`). **Use A** (§8.2).
6. **Bar order.**
   - A: `CLR | $1 $5 $10 $25 $100 | SPIN`.
   - C: chips, then CLR, then SPIN.
   - **Use A.** Keeping CLR far from SPIN avoids clearing by accident.
7. **CLR.** A asks for a second press to confirm; C clears at once. **Use A.** A 20-spot layout is costly to lose.
8. **Zero and felt.**
   - A: zero is FELT_LT, and the RED felt becomes teal.
   - B: zero is FELT/FELT_DK, teal, and the wheel sits on a felt surround.
   - C: GREEN/BLUE/PURPLE, forcing GREEN while the wheel is up, with a NAVY/INK surround.
   - C's swap makes the felt jump colour at the end of the whip, and it breaks the continuous-table look B gives.
   - **Use B's felt surround.** The theme question goes to the user (§8.1).
9. **Wheel map and LUT.**
   - B: 256 steps per turn, band bits in the map byte, four quadrant LUTs in `gfx_chunkScratch()` (0 static SRAM).
   - C: 512 steps per turn, a static 256 B LUT, mirroring in the pixel loop.
   - **Use B.** That frees the 256 B SRAM line in C's budget.
10. **Ball solver.**
    - C's u16 turn angles can't shift by whole pockets exactly (65,536/37 isn't an integer). That is why it needs a "±1 last hop" live correction, which contradicts its own "exact by construction".
    - **Use B:** pocket units and a rotor shift of `k<<16`.
    - Put the sim beside the rules (then `src\game\`) so the test runner's glob of that folder picks it up. B and C named different places. (It is now `Ball.cpp` beside the `.ino`, tested by `tools/tests/run_ball_tests.py`.)
11. **Wheel size and cost.**
    - B: CX 64, CY 86, rim R60, ring R26..44, 561 B map, about 3.5–3.9 ms a frame.
    - C: (64, 88), rim 62/31, about 1,000 B map, about 2 ms.
    - **Use B's numbers.** C's 2 ms leaves out the ellipse overdraw (about 8 stacked fills).
12. **Whip length.** B: 14/10 frames; C and the draft: 12. **Use 12 FUN / 8 QUICK**, stepped per drawn frame, with offsets kept even.
13. **Menu font.**
    - B's title uses `text35x2` (298 B of SRAM code in CC's map); C keeps BJ's 5×7 `gfx_text` (264 B SRAM + 475 B font).
    - Linking both wastes about 0.6 KB of flash and 300 B of SRAM. **Use BJ's 5×7 everywhere**, since the plaque already needs it.
14. **Plate width.** C says plate names "fit 124 px". The real limit is 120: CC `plate()` makes the plate `tw + 8` wide (`CC\Stage.cpp`). **Use A's rule:** if the text is over 120 px, drop the number list.
15. **Palette modes.** C's merge drops TARGETS, which A names as its fallback; B needs CASINO for the FX_A win pocket.
    - **Simplify to CASINO only:**
      - hover rings in **FX_B**: the gold-to-white pulse never goes dark on INK or RED cells, where HOVER's grey ramp turns black for half its 64-frame cycle;
      - the hovered stack's outline in `fx::RAIN[(frame>>3)%5]`, the CC picked-up idiom.
    - Delete `setMode` and the HOVER mode.
16. **Payout events.** A emits a rules event per spot (Clear/Lose/Win/Collect); C emits one Settle. **Use C.** BJ's queue drops events silently once it holds 16 (`BJ\Round.cpp`, `Round::emit()`).
17. **Sound names.** B uses Clack, Thunk and Hop; C uses Spin, Rattle, Thunk, Tick and Tock. Merge into one enum with soft effects last (`soft = s >= Tick`), and add `blip(hz, ms, soft)`.
18. **Wording.**
    - "1st DOZEN" (A) against "2ND DOZEN" (C): keep the lowercase ordinals on the felt and use uppercase "1ST DOZEN 1-12" in the plate.
    - "SAVE + QUIT" (A) against "SAVE & QUIT" (C): use "SAVE & QUIT", since 5×7 has `&` and `text35` doesn't.
19. **Two demo systems.** B has the title ball doing demo spins; C has BJ's attract demo. Keep C's attract demo. On the title, just turn the rotor and let the ball lap; there's no need for solver-driven title spins.
20. **Pocket encoding.** C's F command uses 37 = 00, while B's `spinStart(target /*wheel index*/)` takes a wheel index.
    - Make "number 0..36, 37 = 00" canonical in rules, tote, save, `hits[38]` and debug.
    - Convert to a wheel index only inside `Wheel` and `Ball`. Add a test for the round trip.

## 2. Fit and budget

**Screen**
- **Title doesn't fit as drafted.**
  - B's menu is `text35x2` at an 11–12 px pitch inside 34 rows.
  - The title has up to 4 items (CONTINUE, NEW GAME, OPTIONS, STATS); CC's 15 px highlight boxes need a 14 px pitch, so that's 56 px.
  - **Fix:**
    - Move the title wheel up: `cy` ≈ 60, rows ≈ 26..93. The renderer already takes `cx` and `cy`.
    - Put the logo in rows 2..22.
    - Use the 5×7 menu at a 9 px pitch over `dither(0,94,…)`, or a single LEFT/RIGHT-cycled item row.
    - Mock it in P0 before committing.
- **Plaque overflow.** The 5×7 bold purse fits up to "$100000" (41+1 px in a 48 px plaque). ENDLESS can pass that. Show "$1.2M"-style or switch to text35 at 7 digits or more.
- **Stacks to the rack.**
  - BJ `flyY` clamps stacks to y ≥ `RAIL_Y+RAIL_H+9` = 55 (`BJ\Presenter.cpp`).
  - A's STACK_TO_TRAY target (76, 47) therefore stops on the top number row and vanishes.
  - **Fix:** for TRAY and WIN_IN, clamp at `RAIL_Y`, and mark rows 42..47 as moving so the wall band redraws.

**Palette**
- **Green wording under other themes.**
  - Every slot is used. With any theme kept, B_GREEN (WHITE, then FELT_LT) and the bubbles and banners "0 GREEN" and "00 GREEN" turn blue or purple on those felts.
  - **Fix:** either drop the themes (§8.1), or make the zero call "ZERO" / "00" and give B_GREEN the name B_ZERO, using the felt ramp.
- **$100 mini chip on a black cell.**
  - The chip body is INK, its outline is INK and its shade is INK. On an INK cell only 2–3 GOLD pixels show.
  - **Fix:** give the $100 mini chip a GOLD outline instead of INK.
- **Zero label contrast.** A WHITE "0" on FELT_LT (0x4B5) is weak. Use an INK digit, or put WHITE on FELT. Check in P0.

**Flash (gate: release ≤ 48.5 KB, debug ≤ 50,432 B)**
- C's per-module removal figures can't be checked against the LTO map.
  - BJ's release map has `screens::render` at 7,800 B and `screens::update` at 6,940 B, with Round and the presenter inlined into them.
  - Treat C's 44.9 KB as **45–48.5 KB**, with debug +0.9–1.2 KB (the extra hooks below).
- **Hard checkpoints:**
  - P1 skeleton ≤ 35 KB release.
  - End of P4 ≤ 45.5 KB release and ≤ 47 KB debug.
  - Otherwise apply cuts before P5.
- **Remove the American wheel from C's cut list (#8).** It is a fixed user decision.
- Line items C's budget misses:
  - the debug `Q` calibration and route hooks (+0.25 KB);
  - dolly and ball art (≈60 B);
  - text35x2 if it creeps in (+0.3 KB plus 298 B SRAM).

**SRAM (18,416 B)**
- C's "−396 B" from swapping in CC's own primitives is overstated.
  - CC's `chch.sprite4` is **458 B** of SRAM, against `chgfx.sprite4` at 238 B (+ `copypixels` 156) in BJ's map.
  - The true change is about −120 to +60 B.
- With B's scratch LUT (−256 against C), the projection is about **15.4–15.6 KB**. There is still about 2.8 KB of headroom.

**Frame time (draw ≈ 8.3 ms target; BJ already ships an 11 ms worst frame)**
- **Wheel frame:** about 3.5–4 ms.
  - Ellipse fills ≈ 1.5 ms, the LUT 0.6 ms from flash, the ring about 0.6 ms (map bytes are flash data reads), plus the cone, turret and felt.
- **Landing frame: about 8.6–9.1 ms (over).** It adds the banner at its scale-4 pop (2.9 ms), the croupier typing (wall redraw 1.2 ms), particles, and shake.
  - B's 7.5–8 ms leaves out its own `shake(6,2)`: `shiftRows` over 82 rows ≈ 0.7 ms.
  - **Fixes:**
    1. Start the "17 RED" bubble after the banner pop (t ≥ 8).
    2. Delay the shake to after the pop, or drop it.
    3. Move `buildLut` into SRAM, about 200 B (−0.3 ms).
- **Big-win payout on the layout:** about 8.1–8.6 ms.
  - Layout 2.5 + wall/plaque for the rolling purse 1.2 + flights + particles + banner 2.9 + shake 0.9.
  - **Fixes:** the same two as above, plus redraw **only the plaque rectangle** while the purse rolls (≈0.2 ms against a 1.2 ms full wall band).
- **Whip:** worst case ≈ 6.5 ms. Fine.
- **Ball dry run.** B's "2–3 ms on one tick" is optimistic: 400 steps of flash code is about 3–8 ms. One late tick would fail `pace.txt` (`late=0`).
  - **Fix:** choose the pocket when NoMoreBets starts. Keep the dry-run `Ball` in a static (48 B) and run about 25 steps per tick across the 40-frame NoMoreBets pace.

## 3. Logic holes (correctness)

1. **SAVE & QUIT mid-spin.**
   - BJ's quit saves the purse with stakes already deducted, so it forfeits them (the pause menu in `BJ\Screens.cpp`'s `playUpdate()`; the `mid` flag there is unused).
   - In roulette the result is decided at NoMoreBets and is visible before Settle. A refund policy would let a player quit to cancel a loss; a forfeit policy robs a winner.
   - **Fix:**
     - During Betting, refund the bets and save them as the rebet layout.
     - From NoMoreBets onward, the rules apply the decided settlement (`finishSpin()`, no events) before `persist`.
2. **Changing options while paused mid-spin.**
   - C's `setWheel` refunds committed bets.
   - **Fix:** a wheel change takes effect at the next Betting phase. Store it in `opt`, and keep the live `n` in the rules and the Ball. Grey out the WHEEL row while a spin is running.
3. **Fast-forward problems.**
   - Holding A from the SPIN press fast-forwards at once.
   - B's ×4 makes the rotor turn about 1.8 rev/s, which is about 1.1 pockets per frame: exactly the red/black strobe B warns about.
   - **Fix:** require a fresh A press after the whip; fast-forward ×2 at most (or ×4 only before the rotor phase).
4. **Plaque hidden by the bubble.**
   - BJ draws the bubble *instead of* the plaque (`Presenter.cpp`, `render()`) and holds it 100 frames (the `Say` event in `onEvents()`).
   - So "PLACE YOUR BETS" hides PURSE/BET while you bet, and "WINNER!" hides the rolling purse at the climax.
   - **Fix:**
     - Any A or B betting input dismisses the bubble.
     - Hold the betting line at most 40 frames.
     - Say "17 RED" during the wheel view and close it when the whip back starts.
     - Settle lines start only after the purse roll ends, or are skipped when the roll is long.
5. **The banner can hide the ball.**
   - The banner sits at cy ≈ 84 with rows ±12 and x 18..110. Pockets at the sides of the wheel (y ≈ 80–92) are under it.
   - **Fix:** put the banner at cy ≈ 58 (over the far rim), or flip to cy ≈ 112 when the ball's y < 80. Flash the pocket (WHITE for 4 frames, then FX_A) for 12 frames before the banner pops.
6. **Debug hook letters.**
   - C's `Y` (allow flash writes) collides with CC's `Y` (RPROF device render profile), which P4/P7 need for measurements (`CC\Screens.cpp`, `debugHook()`).
   - C also drops `Q`, which chdrive's `cal` and C's own P3 "perf/cal ≤ 4 ms" check depend on (then a CC hook; now the protocol's own, in the CHGame library's `chgame/Debug.cpp`).
   - **Fix:** keep `Y` = RPROF and `Q` = cal, and use `E 1/0` for "enable flash writes".
   - Add a route command (a BFS over `Nav`, as CC's `R`) under a free letter such as `U`, so showcase scripts walk the glove instead of teleporting it.
7. **Event amounts.** Widen to int32 in all three specs (agreed). Also widen `Fly.value` and `pending`.
8. **"Big win" is never defined.** Use: net ≥ 17× total stake, or any straight-up hit, gives B_RAINBOW + fountains + LED_PARTY. Otherwise WIN = B_GOLD and losses = BJ's lose recipe.

## 4. UX (glove, controls, readability)

- **Diagonal taps rarely fire.**
  - CC moves diagonally only when two `repeat()`s fire on the same frame (`CC\Screens.cpp`, `playInput()`).
  - Treat corners as "right, then up" (two taps) and don't advertise diagonals.
  - Optional: if a second direction arrives within 3 frames while the first is held, merge them.
- **Half-step cost.** A tap is half a cell, so reaching the next number takes two taps, and runs start at frame 23.
  - Keep CHGame's `repeat(18,5)` for feel parity.
  - Make the run snap A describes the *first* run step (as simulated), so holding a direction never visibly pauses on a line.
- **Hover visibility.** Use FX_B rings (§1.15). Draw the ghost chip ring at empty line spots in FX_B as well.
- **Two-digit numbers.** They touch a GOLD line on one side. Accept, but check in the P0 3× mockup. If they read poorly, drop the vertical gold lines *inside a row* only where both neighbours differ in colour. That isn't always the case: 9 and 12 are both red.
- **The bar is packed to the pixel** (1 px gaps; BJ uses 2).
  - Option: move CLR to "hold B 45 frames anywhere", with CC's hold bar under the plate. That frees 18 px for a wider SPIN.
- **Plate colours.** BJ's money colour is GOLD. Make the amount GOLD, the numbers WHITE, the bet name WHITE and the payout SILVER. A has numbers GOLD and the amount CYAN, two shades that are hard to tell apart at 3×5.
- **Rebet then accidental spin.** The cursor lands on SPIN after a rebet. That's fine, but start the bubble and the cursor glide *after* the rebet chips land, so an early A doesn't spin on a half-drawn layout.
- **Optional: winners' stakes "stay up"** (§8.5). Only the winnings come home and swept spots fly back in, which halves payout chip traffic.

## 5. Drift from the CHBlackjack/CHChess style or the user's decisions

- Two title-font and logo approaches; settle on one (§1.13, §8.7).
- C's NAVY wheel surround breaks decision 4's whip "along the table". Use felt (B).
- C's forced-GREEN swap flashes the palette at the whip boundary. Drop it.
- B's title demo spins duplicate C's attract demo (§1.19).
- No splash is right, since this isn't a PPOT port.
- Decision 3 needs the "17 RED" bubble: keep it, but time it (§3.4).
- The "0 GREEN" wording breaks the moment a theme is kept (§2, Palette).

## 6. Missing pieces

- **Dolly art.** A 5×9 sprite: WHITE/SILVER cylinder, GOLD cap, INK outline, about 40 B; no FX_B (slot 15 is transparent in span4). Its drop: the croupier's glove (RM_CPU) taps, then `burst(DUST)`.
- **Ball art** (B's 4×4) is in neither asset list.
- **American specifics.** Most are covered: 00 cell, 0/00 split, top line plus its plate text, trios, tote "00", banner "00", `hits[38]`. Still missing:
  - a README note on the omitted 0/2 and 00/2 splits (if omitted);
  - the deferred wheel change (§3.2);
  - a stats note on the house edge (optional).
- **Pause.** Freezes the ball sim because it steps in `present::update`. Pause and the toast force `redrawAll`, which costs about 8 ms on the wheel view; acceptable.
- **Demo.**
  - Keep the purse topped up the way BJ does (`Screens.cpp`, `demoInput()`).
  - Seed the rules with `fx::rnd()`, and `save::load` on any press.
- **Sound option.** SOUND LEAD/ARPEGGIO/OFF in Options, since SELECT is now the chip cycle.
- **Tests (beyond C):**
  - Plate width ≤ 120 for every spot × max amount, on both wheels.
  - The number ↔ wheel-index round trip.
  - Mid-spin quit conserves money.
  - Ball solver: shift invariance makes testing all targets redundant. Run 200 seeds × all targets plus 10k seeds × one random target, not 10k × all (about 1.5 M simulations under UBSan).
  - A sim script that crosses a whip, to catch `BUG:` scratch use.
- **Tools.** `tools/pixkit.py` should **parse** `FONT35`/`IDX35` out of the game's `Draw.cpp` (regex), not port it by hand, so they can't drift. Keep CC's `gifsheet.py`. (Both are now the repository's: `tools/pixkit.py` parses `FONT35` from the CHGame library's `chgame/Draw.cpp`, and `tools/chsim/gifsheet.py` serves every game.)
- **README and NOTICE.**
  - C's text is correct: the croupier and lettering credited to PPOT (vampirics, filmote), Apache-2.0, plus the BJ and CC lines.
  - I checked the PPOT clone: "Pharap" appears only in `Game.*` and `GameContext.*`, which aren't used, and BJ and CC mention him nowhere. Keep it that way.

## 7. Unverified assumptions and how to check them

| Claim | How to check |
|---|---|
| Full table-band redraw 1.5–2.7 ms (A, C) | sim `cal`/`perf` (needs the `Q` hook), then device `Y` RPROF in P3 |
| Ring 0.5 ms, LUT 0.6 ms | `Y` on device in P4. Promote `buildLut` to SRAM if the landing frame is > 8.3 ms |
| Near-side tucking of the stacked ellipses | B's `wheel.py` preview, which uses the `EllipseRows` arithmetic from `GFX\src\CHGfx_extras.cpp` |
| HOVER grey visible on INK/RED | moot if FX_B rings are used |
| `1ull<<n` pulls in `__ashldi3` | moot: use `covers()` |
| Flash removal figures | P1 skeleton build with `check_size.py` |
| PPOT `Font3x5.cpp` has no author header | verified: no Pharap |
| Ball durations (6.5–8.5 s FUN) | host test prints a histogram; tune to §8.4 |

## 8. Decisions that need the user

1. **Green zero and felt themes.**
   - (a) **Drop the FELT option**: green only, the zero always green, "17 RED / 0 GREEN" and B_GREEN work, about −150 B. **Recommended.**
   - (b) Keep GREEN/BLUE/PURPLE (RED goes); the zero wears the felt and calls say "ZERO".
   - (c) Keep the themes and force green while the wheel is up: a visible felt colour jump at the whip.
2. **Edge wrap on taps.**
   - (a) **Chess-style wrap on taps** (UP from the top row reaches the bar and SPIN), never on held runs. **Recommended.**
   - (b) No wrap; deny at the edges.
3. **Limits and goals.**
   - (a) **$100 inside / $250 outside / $1,000 table, goals $1000/$5000/ENDLESS.** **Recommended.** One straight hit can't finish $5000 on its own.
   - (b) A flat $200 per spot (like BJ).
   - (c) (a) with goals $2500/$10000.
4. **Spin length.**
   - (a) FUN 7–8 s (Spec B as written).
   - (b) **FUN ≈ 5 s and QUICK ≈ 3 s of wheel time, fresh-press hold-A ×2.** **Recommended.**
   - (c) QUICK as the default.
5. **Payout choreography.**
   - (a) BJ style: everything goes home, then the rebet flies back.
   - (b) **Winners' stakes stay up**; only the winnings travel. **Recommended.**
6. **American 0/2 and 00/2 splits.**
   - (a) **Omit** (anchors only 2.5 px apart; documented). **Recommended.**
   - (b) Include them and accept overlapping chips.
7. **Title.**
   - (a) **Live wheel + `title35` "ROULETTE" + 5×7 menu.** **Recommended.** It needs no new art.
   - (b) A new 1 bpp logo in BJ's chunky style (new art, about 220 B).
   - (c) A static BJ-style felt title with chip stacks.
8. **Music.**
   - (a) A new title tune, about 450 B (a musette waltz).
   - (b) **Reuse BJ's title tune** (0 B new, already auditioned). **Recommended.**
   - (c) No title music (CC style).
9. **Scope if flash gets tight.** Confirm the cut order: credits back room, then hot/cold, then the attract demo, then the title tune, then the 5×7 font. The American wheel and the chip art are never cut.

### Critical Files for Implementation
- CHBlackjack\Presenter.cpp
- CHBlackjack\Screens.cpp
- CHChess\Stage.cpp
- the CHGame library's chgame\Draw.cpp (then CHChess's own)
- GFX\src\CHGfx_extras.cpp