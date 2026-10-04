# CHBoardwalk — development notes

Agent-facing notes for continuing work on this game. Rules and controls are in [README.md](README.md), build steps and tools under Development below; the platform is covered by the repo-root [CLAUDE.md](../../../../../../../../../CLAUDE.md) and [docs/](../../../../../../../../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBoardwalk at commit a99a4f8 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 49,226 of 50,944 B (1,718 spare), static RAM 15,052 of 18,416 B (3,364 spare).
- On the CHGame library since 2026-10-02 (`platform/board/arduino/CHGame/libraries/CHGame`, `<CHGame.h>`): the input, palette, drawing, 3x5 font, masks, fx maths, shake and formatting that were then the game's `src/CHGame.*` and `src/gfx/` are the library's; `Fx.*` kept the game's particles, banners and floating texts until they became the library's `chgame/Sizzle` later that day (`Fx.h` holds the switches: coins last in the kind order, a floor at row 114; the image did not move). The same frames on every repeatable script; image 49,884 -> 49,712 B, static RAM 15,404 -> 15,100 B (the doubled 3x5 text runs from flash).
- Sound on the library's engine (`chgame/Audio.h`) since 2026-10-02: `Sounds.*` holds the effect tables (3-byte steps: 20 Hz / 2 ms units, so a pitch moves up to 10 Hz and an odd length gains 1 ms; LOSE's 700 ms sweep is two steps), Tick/Tock are `audio::SOFT`, and the bid blip keeps its rule (refused over priority 2+). The preview is the shared `chgame audio out/audio`. Image 49,676 -> 49,612 B, static RAM 15,100 -> 15,052 B; frames unchanged.
- The debug protocol (`chgame/Debug.h`, `CHGAME_DEBUG`), the flash save record (`chgame/Save.h`; `Save.cpp` says only what the record holds, byte for byte the old layout) and RAMFUNC are the library's too since 2026-10-02, and `tools/chsim/chdrive.py` is the shared `tools/chsim/chdrivelib.py` plus this game's `board`, `waitturn` and `cal`. Image 49,612 -> 49,664 B (the library's `audio::setOn()` out of line, about +14 B; its save code, about +30 B), static RAM 15,052 B unchanged; frames unchanged.
- Save pages: the repository's `tools/check_size.py` reports the image as 49,604 B (2026-10-02). Both A/B save pages need the image to stay at or below 50,432 B (0xF500 and 0xF600, the CHGame library's `chgame/Save.cpp`), so the margin is only about 828 B. Treat flash as full: any feature needs a cut first. LTO inlining makes small additions cost more than they look.
- Verification as of 2026-10-01: simulator only.
  - `chgame test` checks every rule, then plays 5,000 seeded games (CPUs and random "humans") checking that the books balance, houses stay even and every game ends, and prints a tuning table.
  - The scripts in `tools/scripts` play through in the simulator.
  - `chgame check` runs every script twice (the frames must be identical).
- Status: complete and played through in the simulator. It has never run on the board: frame timing (pace, render profile) and the sound are unchecked.

## Design decisions

- Name and IP:
  - The title is BOARDWALK, with the classic Atlantic City street names and colour groups.
  - No trademarked game name, characters or official card wording anywhere (code, art, README); the card texts are in the game's own words.
  - The README carries the "independent game" disclaimer.
- Built for one player against the CPU, or two on one handheld, quick and arcade style.
  - 2-4 seats, any human (hot-seat) and CPU mix.
  - The title offers 1 PLAYER / 2 PLAYERS; THE TABLE comes up preset with the cursor on BEGIN.
  - CPU turns always run at the quick pace. The hand-over banner between two humans does not wait for a press.
- A short game:
  - Random deeds are dealt free at the start: 4 each with two players, 2 each with three or four. Chosen over "head-start pairs".
  - The game ends at the first bankruptcy, or at closing time (10-60 rounds, default 20). The richest player wins.
- Rejected: trading. There are auctions instead: simple, automated, real time, tap to outbid, and a lot always sells.
  - Players can put their own deeds up. The bank's half-price opening bid is the floor, and only the bank's own unbid lot goes free to the next seat.
  - CPUs do not offer deeds of their own.
- Rejected: mortgages. Debts settle automatically: houses go back at half price, then deeds go to auction.
- Payday ($400 for landing on GO with the dice) was a change to the rules, and the owner kept it.
- Camera: an isometric close-up that follows the token, plus a whole-board map on SELECT.
- Tokens are all fruit: cherries P1 RED, banana P2 GOLD, apple P3 FELT_LT, strawberry P4 SKIN. Pink stands in for the strawberry so it doesn't match the cherries. Rejected: a die token, which was confusing next to the dice.
- The glove is CHChess's hand art, with a gold cuff for humans and a red cuff (`RM_CPU`) for CPUs.
- Lettering:
  - The title has its own slab-serif `LOGO`, 1 bpp, drawn through Mask like CHBlackjack's logo.
  - The same `LOGO` is printed on the board's plaque, in plain GOLD with no blink (the owner asked: no FX_B on board lettering). The title's logo still shimmers.
- The ink rule under a colour band is clipped to its own cell.
- Scaling: art is drawn at whole multiples only, because the owner found the in-between zoom steps "stretched".
  - `iso::zscale()` is 256 below tileH 8 and 512 from there. Use `iso::sized()` for sprite lengths and `iso::zoomed()` for board lengths.
  - Things painted on the board (the plaque) scale with `zoomed()` through every step; things standing on it go 1x/2x.
  - A new house drops in with a bounce rather than scaling up.
- Look-dev picks: 20x10 tiles; white/silver tiles; pink is a RED/WHITE dither and orange a RED/GOLD dither. Felt colour themes were dropped, because FELT_LT is the green group.

## Open items

- Device test: pace, render profile, sound by ear.
- Fixed 2026-10-02: `tools/scripts/save.txt` used to draw different frames from run to run and usually crash the simulator. `screens::newGame()` left `Setup::deal` uninitialised, so `game::start()` dealt a stack byte's worth of deeds per player and wrote past its 28-entry `deeds[]` (found with valgrind on a `-O0 -g -mcpu=baseline` simulator build). On the board the same garbage could deal the wrong number of deeds. `Setup s = {}` fixes it; every script now repeats exactly.
- The owner's art redraw through `tools/sheet.py`. The sheet has 20 sprites, including the fruit, CHIPS, CARD_DECK, the corner icons and LOGO.
- Arcade rules the implementer chose and reported, not explicitly confirmed by the owner (only payday was):
  - Building on most of a group (2 of 3, or both of a pair).
  - The bank's half-price opening bid.
  - The Free Parking jackpot (taxes, card fines, bail).
  - 4 deeds each with two players.
  - Default 20 rounds.
  - CPUs not offering deeds.
- Device debug builds (`CHGAME_DEBUG` on the board) are always `CHBW_LEAN` (no options screen, no saving). A full debug build overflowed by about 0.8 KB.

## Gotchas

- `docs/` holds one picture, `gameplay.gif`, made by `chgame gif`. Run the other scripts into `out/`, never into `docs/`: they write snaps and a contact sheet (`result.png`, `sheet.png`) beside their GIFs.
- The simulator's `cal`/`perf` render estimate is host time and swings by ±50% from run to run under load. It is useless for small differences.
- Debug protocol (the CHGame library's `chgame/Debug.h`):
  - The game's commands are listed above `debugHook()` in `Screens.cpp`.
  - `G D A $ E T H` work on the board; `J V X F Q` are simulator-only.
  - The protocol owns `? S K L N P B`, and also `T` in a `CHGAME_PROFILE=1` build, where it shadows the game's token command `T`.
- `CHBW_LEAN` is `#ifndef`-guarded: `-DCHBW_LEAN=0` forces a full device debug build, which won't fit without a temporary cut.
- Art:
  - `tools/art/sprites.txt` holds the sprites as palette letters. A `tools/art/<name>.png` replaces its sprite, and `chips.png`, `icon_chest.png`, `icon_jail.png` and `token_banana.png` (shared with CHSnakes: `tools/art/common/` at the repository root) already do, so editing those four in `sprites.txt` has no effect.
  - `python tools/sheet.py export` / `import [SHEET]` round-trips an indexed PNG. Colours are matched by value, and FX_B cannot be used in sprites because it is the transparent colour. An edited sheet saved elsewhere imports with `python tools/sheet.py import <path>`. Wide sprites (LOGO) get a row of their own.
  - Go To Jail is `ICON_JAIL` with SILVER turned RED, and the Community Chest deck is `CARD_DECK` with GOLD turned BLUE.
  - `python tools/lookdev.py` renders a contact sheet of the board at each zoom and tile treatment.
- Board geometry: a 13x13 iso lattice with a ring 2 cells deep (tiles 1x2 cells, corners 2x2). The art is native at tileH 5 and the whip zoom goes to 10.
- The rules (`Tiles`, `Game`, `Cpu`, `Text`) are pure logic and host-tested. A rule change needs the tests updated.
  - Tuning table at import (all CPUs, two seats): bust by closing time about 21% at cap 20 and about 42% at cap 30, with 16-19 houses on the board.
- Saves use magic "CHBW", `VERSION` 1, in `Save.cpp` (the record and the pages are the CHGame library's, `chgame/Save.h`; the header's flag byte is "a game is saved"). CONTINUE resumes as the turn began. The pages are shared with every other CHGame game; bump `VERSION` on any change to the `Data` layout.
- Shared tools: the simulator is the repository's `tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).

## Development

Everything can be checked on a PC: Python 3 with `pip install -e .[sim]` in the repository root, and a C++ compiler for the host builds (zig, clang++ or g++ on the PATH, `pip install ziglang`, or `CHSIM_CXX="path/to/zig c++"`; root CLAUDE.md).

    chgame test     # every rule, then 5,000 seeded games, and the tuning table
    chgame sim
    chgame run tools/scripts/showcase.txt out/showcase
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame audio out/audio    # the sound effects as WAV
    chgame build        # release build + size
    chgame upload [--debug]
    chgame size --top 20

- In the Arduino IDE: *Tools > Optimize > Smallest + LTO* (the default) and *Tools > USB > Upload only*; the board package brings CHGfx and the CHGame library. From the command line, `chgame build` runs `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly` with `--library` for the repository's copies of the libraries.
- `upload --debug` adds the serial protocol for screenshots, injected input and lockstep; to fit, it leaves out the options screen and saving (`CHBW_LEAN`).
- Scripts tap buttons, `waitturn` until the game wants you, `snap` a screenshot, `rec` a GIF, and `say` debug commands: `G` a new game, `D` the next dice, `A` the next card, `$`, `E` and `T` to set cash, deeds and tokens (the list is above `debugHook()` in `Screens.cpp`). `cal` and `perf` estimate the device's render time.
- What the scripts play:
  - `gameplay.txt`: the README's clips (the title, a buy that makes most of a set, BUILD, an auction, the CPU on your houses).
  - `showcase.txt`: a turn, an auction, building, jail and a bankruptcy, as a test.
  - `pop.txt`: the big moments (a set made, BUILD, a hotel, payday, the jackpot, a hotel's rent).
  - `info.txt`: the round, THEY PAY and the landing plate.
  - `story.txt`: an all-CPU game to its result graph.
  - `auction.txt`, `hotseat.txt`, `manage.txt`, `endings.txt`, `save.txt`, `play1.txt`, `views.txt`: what their headers say.
  - `look.txt` is run by `tools/lookdev.py`; `perf.txt` and `pace.txt` are for timing.
- Editing the art: `python tools/sheet.py export` writes `tools/art/sheet.png`, an indexed PNG on the game's palette with every sprite in a labelled cell. Edit it (Photoshop keeps it indexed), then `python tools/sheet.py import` writes what changed to `tools/art/` and rebuilds the assets. Details at the top of `tools/sheet.py`.
- How the picture is made:
  - The name lies across the felt on the board's long diagonal, level on screen, between the two decks, in the title's own lettering; stacks of chips stand about on the carpet.
  - A tile is one cell along its side and two into the board, with its colour band on the inside edge and its owner's colour along the rim.
- With four at the table the top bar has no room for the round, so it floats up as each round begins.
- Building on most of a group exists because, with no trading and only two at the table, full sets would be rare; this way the houses come.
