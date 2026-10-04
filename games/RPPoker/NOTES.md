# CHPoker — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHPoker at commit 0840afd (2026-10-01); develop here now, not in the old repo.
- Release build (board package 0.3.0, 2026-10-02; `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): image 48,540 of 50,944 B, static RAM 15,500 of 18,416 B (2,916 spare).
- On the CHGame library (`platform/board/arduino/CHGame/libraries/CHGame`, `<CHGame.h>`) since 2026-10-02: the input, palette, drawing, 3x5 font, masks, fx maths and shake, formatting, the debug protocol (`chgame/Debug.h`), saving (`chgame/Save.h`) and `RAMFUNC` are the library's; `Fx.h` configures the library's `chgame/Sizzle` (particles, banners and floating texts; since later that day, image unchanged).
- Sound: on the library's engine (`chgame/Audio.h`) since 2026-10-02; `Sounds.*` holds the effect tables (3-byte steps, every sweep GLIDEs as the old sequencer did). `chgame audio out/audio` renders them to WAV.
- Save pages: the image (as the repository's `tools/check_size.py` reports it) is 48,540 B. Both A/B pages (0xF500, 0xF600) fit while it is at most 50,432 B, so the real headroom is 1,892 B. Past that, saving (the CHGame library's `chgame/Save.cpp`; `Save.cpp` says what the record holds) uses page B only.
- Simulator-verified (as of 2026-10-01):
  - `chgame test` passes. It builds under UBSan; `--long` adds the exhaustive 7-card enumeration.
  - It covers exact hand-category counts, betting spots, side pots, stud order, a fuzz of thousands of hands, CPU equity and honesty, and stats.
  - The scripts in `tools/scripts/` run in the simulator, and `gameplay.txt` makes the README's `docs/gameplay.gif` (`chgame gif`).
- Device: never run on the board.

## Design decisions

- Chosen: all four games in ONE image: NL Hold'em, PL Omaha, FL Five Card Draw and FL Seven Card Stud.
  - Hold'em and Draw were the first priority.
  - Separate editions built from one source (a `CHPK_EDITION` switch) were the fallback. They were removed once all four games fit; don't reintroduce them unless flash forces it.
- Chosen: a cash game with a saved purse and a Blackjack-style goal / broke ending. You play against 3 CPUs.
- Chosen: difficulty is tied to the stakes: ROOKIE $1/$2, PRO $5/$10, SHARK $25/$50 (`UNIT` and the variant table in `Variants.cpp`).
- Chosen: CPU seats are anonymous plates named by colour, with no portraits.
- Chosen: the CPUs judge their hands by Monte Carlo play-outs over the cards they can't see, and never peek at hidden cards. The tests check that changing hidden cards never changes a decision; keep that property.
- Defaults the owner has not yet confirmed or vetoed:
  - the draw takes up to 3 cards (4 when keeping an ace);
  - stud: ante = bring-in = one unit, bets of 2 and 4 units;
  - every live hand is shown at the showdown;
  - a $500 starting purse, with goals of $10K, $50K or endless;
  - a NEXT HAND / LEAVE bar after each hand;
  - sound effects only, with no music player (the fanfares are effects too, see `Sounds.cpp`).

## Open items

- Device checks: pace, CPU think time, mini-card legibility and sound by ear. Then leave the release build on the board.
  - `tools/scripts/device_perf.txt` runs each game at SHARK, free-running, and prints the frame `perf` and the CPUs' think time (`say W`).
- The owner's verdict on the unconfirmed defaults above.

## Gotchas

- Flash: 1,892 B before the image reaches save page A. Measure with `chgame build`, which runs the repository's shared `tools/check_size.py`.
- Device debug builds (`CHPK_LEAN`, set in `config.h`) have no particles (`SIZZLE_NO_PARTICLES` in `Fx.h`: no coins or confetti) and leave saving out entirely (stubs in `Save.cpp`), so a debug run on the board never touches its save pages. The simulator and release builds keep saving. `-DCHPK_FULL` forces a full device debug build, which may not fit. On such a build the Stats page says "SAVING UNAVAILABLE" (it asks `!CHPK_LEAN && save::available()`). With board package 0.3.0 the device debug build was 156 B over the flash until the particles went (2026-10-02); it is now 49,512 B.
- Debug hooks are sent with `say` in chdrive scripts; the list is in `Screens.cpp`:
  - `G` sit down (game, table, buy-in, seed), `D` stack the deck (card = rank*4 + suit), `$` set the purse;
  - `J` jump to a screen (T L O S W B), `H` table state, `W` CPU think time (last/max);
  - simulator only: `Z` "power cycle" (reload the save, back to the title) and `Q` (calibration for `cal`).
- The debug protocol is the CHGame library's (`chgame/Debug.h`, on with `CHGAME_DEBUG`; its hello is `CHPK <version>`). This game's `tools/chsim/chdrive.py` (on the shared `tools/chsim/chdrivelib.py`) adds the ops `waitturn`, `playto P`, `table` and `cal` (with `cal` first, `perf` prints estimated device render times).
- CPU tuning is the `Level` table at the top of `Ai.cpp`: samples, noise, slack, bet/raise thresholds, first-street fold floor, bluff, slow-play, fear, position.
  - Tuned targets: ROOKIE calls about 60%, PRO folds about 2/3, SHARK raises about as often as it calls.
  - Rerun `chgame test` after any change.
- CPU thinking runs a fixed number of play-outs, only on a frame's first logic tick. Keep it that way: it keeps the table animating and lockstep scripts deterministic.
- The royal flush and the four sevens in `tools/scripts/gameplay.txt` and `showcase.txt` come from stacked decks. Everything else in them is the CPUs playing, so a CPU tuning change alters the README GIF: record it again.
- Art: `tools/assets.py` packs `logo.txt` from `tools/art/` ("Poker" in the letters of PPOT's BlackJack logo) and CHBlackjack's card art and CHChess's glove from the shared `tools/art/common/` at the repository root; it does not read the sibling games. Credit Press Play On Tape as NOTICE does.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).

## How it fits

- Flash: all four games, every screen and the attract demo fit with LTO (`opt=oslto`, with `usb=uploadonly`: the game has no use for USB Serial). The presentation is the biggest part (cards, chips, plates, bar, animation: ~12 KB without LTO), then the rules (~7.5 KB), screens (~6.3 KB), CHGfx and the core. CHGfx's circle, ellipse and line drawing were replaced by the rounded-rect corner table and a 31-byte ellipse quadrant, which saved 736 bytes.
- The hand evaluator has no tables: a rank mask per suit, straights by four shifts and ANDs, flushes by counting bits, pairs, trips and quads by counting ranks, for 1 to 7 cards at once. The tests check the exact category counts over all 2,598,960 five-card and 133,784,560 seven-card hands, and the 7,462 distinct five-card scores.
- CPU thinking: 16 play-outs a frame in Hold'em, Stud and Draw, an estimated 2 ms on the board; one Omaha play-out is 60 five-card evaluations per player.
- A still table isn't redrawn: the frame is flushed again. A full redraw is estimated (from the simulator, calibrated against the board) at about 7.4 ms.

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the host builds: root CLAUDE.md).

    chgame test [--long]   # the evaluator, betting, pots, the CPUs, a fuzz of every game
    chgame sim
    chgame run tools/scripts/showcase.txt out/showcase
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame audio out/audio   # the sound effects as WAV
    chgame upload [--debug]   # build and upload (--debug adds the serial protocol)
    chgame size --top 20

- The tests cover: the evaluator (exhaustive), betting spots (no-limit minimum raises, short all-ins, pot-limit maximums, fixed-limit caps, the stud bring-in and order), side pots, odd chips, uncalled bets, the draw heuristic, CPU equity and honesty, statistics, and a fuzz of thousands of hands of every game at every table with random input, checking that no chip or card is ever lost or doubled and that every pot is paid.
- Scripts: `say G <game> <table> <buy-in> <seed>` sits down, `say D <cards>` stacks the deck (card = rank*4 + suit), `waitturn` runs until the table waits for you, `playto P` checks or calls until the table reaches phase P, `snap` and `rec` take pictures. The same scripts run on the device with a debug build (`chgame run --device SCRIPT OUTDIR`).
- `tools/scripts/gameplay.txt` records the README's clips: the title, a Hold'em royal flush and Five Card Draw's four sevens from stacked decks, and a Seven Card Stud hand that is the CPUs playing, so a CPU tuning change alters that clip. `showcase.txt` is the longer tour (lobby, Omaha, all in and busted, breaking the bank), kept as a test.
- Art: `tools/art/logo.txt` is the title's lettering as `#` and `.`; the cards and the glove are the shared ones in the repository's `tools/art/common/`.
- With plain `arduino-cli`: `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly .` (board package 0.3.0 brings CHGfx and the CHGame library; `chgame build` does the same with the repository's copies).

## Files

    CHPoker.ino            loop: logic ticks, then draw, then DMA flush
    config.h               build switches
    Cards.h, Hand.*        the cards; the hand evaluator
    Table.*                the rules and the flow of a hand (no graphics)
    Ai.*                   the CPU players
    Variants.*             the four games and three tables as data
    Stage.*                events -> motion; drawing the table
    CardArt.*, Bar.*,      cards and chips, the action bar, every coordinate
      Layout.h
    Screens.*              title, lobby, play, options, stats, the endings
    Fx.*                   the library's chgame/Sizzle (particles, banners,
                           floating texts), configured in Fx.h
    Sounds.*               the sound effects (the CHGame library's engine)
    Save.*                 what a save holds (the CHGame library keeps it in flash)
    src/assets/            the art, generated by tools/assets.py
    tools/                 simulator driver, tests, asset pipeline, device tools
