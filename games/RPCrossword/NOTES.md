# CHCrossword — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, commands under Development below, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHCrossword at commit 68c482e (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 50,070 B, static RAM 17,276 of 18,416 B (1,140 spare).
- Flash is full in practice: the image (`../../../../../../../../../tools/check_size.py`'s `image:` line, 50,416 B) sits only 16 B under the 50,432 B that keeps both A/B save pages (0xF500/0xF600, the CHGame library's `chgame/Save.cpp`). Any new feature needs a cut first.
- Verification: simulator only. `chgame check` passes: puzzle check, host tests (decoder vs the Python reference, rules and score, saving, FAT16/FAT32 card images with a read failure at every point), every script twice with identical frames, device compile and size.
- As of 2026-10-01 it has never run on the device, and CHSd (its SD driver) has never read a real card in any game.

## Design decisions

- Chosen: full-size 13x13 puzzles; 20 built in (flash) plus packs on the SD card (`CHCW/*.CWD`, up to 15x15 at 7 px squares, SD only).
- Chosen: score attack - timer, score, combo streaks, best times, stars. No chips or betting.
- Chosen: a pop-up letter board for typing.
- Chosen, at the owner's direction, after the first build:
  - the series' zoom-in: a 2x close-up while the letter board is up or B is held (Options VIEW WHOLE/CLOSE);
  - bevelled 16 px tiles with clue numbers; letters anti-aliased from DejaVu Serif Bold, one half-tone per tile colour, also on the keys;
  - keys as green tiles with white letters; drop shadows;
  - a rainbow rounded cursor outline with CHChess's glove over the cursor (it flips below the cursor on the top rows);
  - a red triangle for rub-out;
  - a x3 JACKPOT word per puzzle, star ranks on the puzzle list, a deal-in;
  - the title rebuilt in close-up tiles with a serif menu and a side-on glove selector (no navy/gold box).
- The owner liked the look in this direction; keep it.
- Rejected (tried, failed): themed grid fills. Grids are unthemed.

## Open items

- Awaiting the owner's verdict (built without explicit sign-off):
  - the whole grid at 8 px cells: white tiles on dark felt, cyan active word, gold locked words;
  - no numbers in the small cells (the clue bar shows 14A); the side HUD column; wide M/W glyphs in cells;
  - the glove only pokes keys; auto-check locks words (CHECKING OFF as the option);
  - 15x15 at 7 px for SD packs only; light casino flavour in titles and clues; the COMBO / CROSS! / SOLVED! banners.
- First device run, which is also the first hardware test of CHSd: `chgame run --device tools/scripts/device/perf.txt OUTDIR` for render times, then `chgame check --compare` against the simulator's run of the same script. Try a FAT32 card, a FAT16 card, an exFAT card (should say FORMAT IT AS FAT32), no `CHCW` folder, and pulling the card mid-puzzle.
- The clues were written for the game and only spot-checked; a full proofread has not been done.

## Gotchas

- Flash: a built-in puzzle is ~700 B. The save page holds records for exactly the 20 built-in puzzles, so adding built-ins also needs a save-layout change (and a cut elsewhere).
- Device debug builds are `CHCW_LEAN` (config.h derives it from `CHGAME_DEBUG` on the board; the simulator is never lean; `-DCHCW_FULL` turns it off, and does not fit): only the first three built-in puzzles, no saving, no Options; start puzzles with `say G <i>`.
- SD (MIT, HypeRunner's clean-room driver): CHSd is used as a library (`#include <Fat.h>`, `<SdSpi.h>`; `../../../../CHSd`); the game holds no copy of it. `chgame build` passes it with `--library`, and the simulator builds `CHSd/src/Fat.cpp` with the pretend card in CHSd's `host/` folder (`$CHSD_CARD`). After changing CHSd run its tests (`python platform/board/arduino/CHGame/libraries/CHSd/tests/run_tests.py`) and this game's `chgame check`. `tools/puzzles/mkcard.py` uses `CHSd/tools/fatimg.py`.
- The card shares SPI1 with the LCD: it is read only between frames and only on the puzzle list; a card puzzle is copied into 2 KB of RAM at start. Keep card access out of play and out of an in-flight flush.
- Simulator card: `tools/chsim/chdrive.py --card IMG` (sets `CHSD_CARD`); images come from `tools/puzzles/mkcard.py`. `chgame check` runs the `card_*.txt` scripts with `out/card.img` in the slot (`CARD` in `tools/game.py`). `say X 0|1` (simulator) pulls / inserts the card.
- `chgame check` (the shared `tools/check.py`) runs every script twice and fails on any frame difference or a simulator BUG line (drawing into a frame still being sent): keep the game deterministic.
- Puzzle pipeline: `tools/puzzles/newgrid.py` fills a grid, clues are written into `tools/puzzles/src/*.txt`, `build_pack.py` checks them and regenerates `src/game/PuzzleData.cpp`. `tools/puzzles/cwformat.py` holds the reference decoder the host tests hold the game to: change the format in both.
- The grid maker needs `wordfreq` (`pip install wordfreq`, or `pip install --target tools/puzzles/data/pylib wordfreq`; that folder is gitignored). `tools/puzzles/avoid.txt` is the curated block list of junk words: add to it rather than hand-editing fills.
- Letters: `tools/tilefont.py` rasterizes DejaVu Serif Bold into `tools/art/tilefont.txt`; `tools/assets.py` packs the art into `src/assets/`.
- Sound: the CHGame library's engine (`chgame/Audio.h`); the effect tables are `Sounds.*` in `Sfx` order. A locking word's rising notes (and the deal, title and result ticks) are `audio::note(hz, ms, 2)`: they give way only to the fanfares. `frame::begin()` starts the engine (`audio::begin(..., false)`), `applyOptions()` switches it with `audio::setOn()` (calling `begin()` there instead would be 8 B smaller). WAVs: `chgame audio out/audio`.
- Debug hooks (above the hook in `Screens.cpp`): `G` start puzzle, `H` STATE line, `W` next word, `C` cursor, `Z` fill all but the last k words, `U` advance the clock, `J` jump, `X` and `Q` simulator only. chdrive extras: `state`, `expect`, `waitstate`, `type`, `solve [N]`, `wrong`, `mark`/`delta`, `solveto`, `solvemost`, `rec pause/resume`, `cal` and a calibrated `perf`, `--card`.
- The debug protocol is the CHGame library's (`chgame/Debug.h`, on with `CHGAME_DEBUG`); the save record's pages and CRC are the library's too (`chgame/Save.cpp`), the game's `Save.*` says what goes in it (unchanged layout: magic "CHCW", version 1, the puzzle-in-progress flag in the header). The script driver is the shared `../../../../../../../../../tools/chsim/chdrivelib.py`; tools/chsim/chdrive.py adds the commands above.
- The effects (`Fx.*`) have only what the game uses: SPARK, CONFETTI and STAR particles, RAINBOW and GOLD banners (the DUST puffs and the RED, CYAN and WHITE banners copied from the casino games were never used, and were cut for 140 B on 2026-10-02). Since the library's `chgame/Sizzle` took the code later that day, that choice is `SIZZLE_KIND_DUST 0` and `SIZZLE_STYLES` in `Fx.h`; the image did not move.
- `tools/scripts/gameplay.txt` records the clips of the README's one GIF (`docs/gameplay.gif`); `chgame gif` runs it and joins them (see Development).
- Credits are exactly those in NOTICE (Press Play On Tape's 3x5 font, DejaVu, CHSd/HypeRunner, ENABLE and wordfreq as build-time aids); add no others. CHSd (the library, with its `fatimg.py`) is MIT, the game Apache-2.0.
- The simulator is `../../../../../../../../../tools/chsim/chsim.py` (shared); per-game tools stay in `tools/`. For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- Device debug builds leave out saving and most puzzles; the board may be in use, so announce a debug upload and put the release back afterwards.

## Development

Everything can be checked on a PC (Python 3 with `pillow`, `tools/requirements.txt`, and a C++ compiler for the host builds: `CHSIM_CXX`, zig, clang++ or g++, root CLAUDE.md).

    chgame check                 # puzzles, host tests, every script twice, device build
    chgame test       # the host tests alone
    chgame sim
    chgame run tools/scripts/play.txt out/play
    chgame run --card out/card.img tools/scripts/card_packs.txt out/card
    chgame gif      # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py                # art -> src/assets
    chgame build [--debug] | upload | run --device SCRIPT OUTDIR
    chgame size --top 20

- The README's picture: `tools/scripts/gameplay.txt` records six clips into `out/gameplay` (`01_title` ... `06_solved`, one puzzle played and recorded at its highlights; the last clip at `rec start 4` to stay under 1 MB) and `readme_gif.py` joins them. `tools/scripts/showcase.txt` is kept as a test of the same screens (title, list, close-up, wrong word, the show); its GIFs stay in `out/`.
- The host tests hold the game's decoder to the Python reference on every puzzle, play whole puzzles through the rules, and read packs out of FAT16 and FAT32 images - fragmented files, long names, decoy entries, files that are not packs - with the card failing at every possible read.
- The scripts in `tools/scripts` drive the real game through its debug protocol (`solve` types each word on the letter board as a player would), in the simulator or on the board.
- Building in the Arduino IDE: *Tools > Optimize* must be **Smallest + LTO** (the game does not fit without it), *Tools > USB* **Upload only** (the game stops with a message otherwise); the board package (0.3.0 on) brings CHGfx, CHGame and CHSd.

### Making puzzles

    python tools/puzzles/newgrid.py --seed 7          # a filled 13x13 grid and its word list
    python tools/puzzles/build_pack.py                # check tools/puzzles/src/*.txt, pack them into the game
    python tools/puzzles/build_pack.py --cwd MINE.CWD --name MINE a.txt b.txt   # a pack for the card
    python tools/puzzles/puz2cwd.py MINE.CWD *.puz    # ... or from Across Lite files
    python tools/puzzles/mkcard.py out/card.img MINE.CWD   # a card image for the simulator

- A puzzle source is a text file: a title, a difficulty, the grid (`#` for black) and a clue for each word (see any file in `tools/puzzles/src`).
- The checker insists on what crosswords insist on - symmetry, every letter in two words, no two-letter words, one piece - and on what the game needs: clues that fit the clue box in the characters the font has.
- `newgrid.py` draws a blank grid, fills it with common words (ENABLE, ranked by the `wordfreq` package), never two forms of one word, and prints it ready for clues.

### The card, for a player

- Packs (`.CWD`, up to eight, up to 32 puzzles each) go in a folder `CHCW` at the top level of a FAT32 or FAT16 card; packs anywhere else are not found. Cards of 64 GB and more come as exFAT and must be reformatted as FAT32.
- The list's messages: **CARD: FORMAT IT AS FAT32** (an exFAT card), **CARD: NO PACKS IN CHCW**, **CARD: CANNOT READ IT**.
- `sdcard/CHCW/BONUS.CWD` is a pack of three puzzles.

### How it fits

- **The grid.** 13 squares of 8 pixels is 104: the whole puzzle fits with a 24-pixel column beside it for the clock and score and three lines below for the clue. (15x15 packs use 7-pixel squares.) A square is a 7x7 tile with a 3x5 letter - M and W are drawn five wide there, since in three columns they are an H with its bar out of place - and there is no room for numbers in the squares, so the clue line carries them.
- **The close-up** is not the small grid doubled: at 16 pixels a square each tile is drawn with a shaded edge and clipped corners, its number in the 3x5 font, and its letter in capitals nine pixels tall, with a layer of half-ink pixels on curves and diagonals in a tone between the letter's colour and the tile's - silver on white, brown on gold, blue on cyan, light green on the keys (1 KB for the 26; the keys of the letter board use them too). The camera steps through the sizes between in four ticks, with flat tiles, to get there.
- **A puzzle is about 700 bytes.** The black squares are one bit each (half of them: the other half is the first turned half a turn), the answers five bits a square, and the title and clues Huffman-coded with one fixed table (4.5 bits a character), so twenty puzzles are 14 KB. The grid and word list are unpacked into RAM when a puzzle starts; clues stay packed and are decoded one at a time. The same bytes are a puzzle in flash and in a pack on the card.
- **Saving** shares the two save pages with every other CHGame game: saving here replaces another game's save. Twenty built-in puzzles is what one page has room to keep records for.
