# CHMahjong — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHMahjong at commit 481dfdf (2026-10-01); develop here now, not in the old repo.
- Release build (board package 0.3.0, 2026-10-02, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 48,139 of 50,944 B (2,805 spare), static RAM 17,812 of 18,416 B (604 spare). RAM is the tight budget, not flash.
- Save pages: the repository's `tools/check_size.py` reports the image as 48,592 B, 453 B more than the compile's flash figure. Both A/B pages (0xF500, 0xF600) fit while the image is at most 50,432 B, so the margin is 1,840 B. Past that, the CHGame library's saving (`chgame/Save.cpp`) uses page B only.
- Simulator-verified (as of 2026-10-01):
  - `chgame test` passes: 10,000 deals per layout cleared, golden deal hashes, every free tile reachable by the cursor.
  - Scripts `ui`, `match`, `clear`, `stuck`, `save` and `showcase` run clean in `tools/chsim/chdrive.py --sim`. They also ran clean once with the simulator built under UBSan.
- Device: the owner ran an early build on the board (before the close-up, classic faces, new title and sparrow) and reported it worked well. No run of the current build on hardware is recorded. Render time, pacing and sound by ear are unmeasured.

## Design decisions

- Chosen: mahjong solitaire, not 4-player mahjong.
- Chosen: the D-pad hops between free tiles only (`Nav.cpp`):
  - UP/DOWN use CHChess's nearest-in-direction rule.
  - LEFT/RIGHT step through the free tiles in reading order.
  - Reason: the pure directional rule left some tiles unreachable. A host test proves every free tile can be reached.
- Chosen: the cursor is both the lifted tile with an FX_B outline and CHChess's glove (the shared `tools/art/common/hand.png`).
- Chosen: chips with streak scoring. The constants are in `MahjongBoard.h` (`PAIR_PAYS`, `STREAK_FRAMES`, `HINT_COST`, `SHUFFLE_COST`, `CLEAR_BONUS`, `PAR_SECS`, `MAX_SHUFFLES`).
- Rejected: a true isometric view (the genre uses the oblique view, and iso hides tiles at 128 px). Built instead: the hold-B 2x close-up, plus Options VIEW FULL/CLOSE.
- Rejected: grey (SILVER) blocked tiles. Every tile is white with no blocked cue, and the glove shows which tiles are free. The reference look is GNOME Mahjongg.
- Chosen: CLASSIC traditional faces by default. The old numbered faces stay as Options TILES EASY.
- Chosen: tile bodies have an ivory (SKIN) side and a WOOD backing.
- Rejected: gold sides (they looked gilded and drowned the gold cursor outline). `-DCHMJ_BODY_SIDE=GOLD` (`Stage.cpp`) brings them back for comparison.
- Title feel: "calm, like koi swimming". Keep it calm when changing the title (`Screens.cpp`):
  - Tiles mostly do coin-flip spins. A tumbler is only 1 in 12 (tumbling looked funny but off-vibe).
  - A turn every 5-10 s, a gentle sway and a slow sink.
  - The meteor glides across in about 1 s.
- Chosen: the win banner says MAHJONG! (not JACKPOT!). Then the sparrow visits: any button shoos it, and the results screen waits for it.

## Open items

- First device run: `chgame run --device tools/scripts/device_render.txt out/device` measures the draw cost of a frame on the board.
  - Do it with the owner watching the screen. An early debug upload got no serial answer, and the board then dropped off USB.
  - Port contention is the likely cause, but a hardware-only crash was not ruled out.
- After that: check pacing and sound by ear, then leave the release build on the board.
- Simulator render estimates (unreliable, taken on a loaded host):
  - about 7-9 ms at 1x and about 11 ms at 2x;
  - in-between frames of the close-up whip up to about 30 ms (the whip steps one zoom level per tick, so it still takes 4 ticks).
- The owner has not yet given a verdict on the glove drawing back to the table's bottom-right corner after 50 idle frames (`IDLE_FRAMES` in `Stage.cpp`).
- The sparrow art (`tools/art/bird.txt`) is credited in NOTICE to an uncredited artist (the owner's wording, 2026-10-02).
- The weakest close-up drawings in `tools/art/classic2x.txt` are the 15x23 green dragon (發) and the 1-bamboo bird.

## Gotchas

- RAM has 604 B spare (release, since the move to the CHGame library and its sound engine: 17,812 of 18,416 B). The hot blitters run from SRAM (`RAMFUNC` in `Tile.cpp`; the CHGame library's sprite, 3x5 text, mask and shake loops), so their code counts against static RAM; the tile blitters alone take about 1.26 KB. Device debug builds have less spare RAM still (17,996 B, 420 B spare, since the debug protocol, saving and RAMFUNC moved to the CHGame library on 2026-10-02).
- Device debug builds don't fit with everything:
  - `config.h` turns on `CHMJ_LEAN`, which drops the EASY faces and the particles (`SIZZLE_NO_PARTICLES` in `Fx.h`; with board package 0.3.0 the build was 8 B over until those went, 2026-10-02: now 49,528 B); `-DCHMJ_FULL` overrides it.
  - The simulator and release builds keep everything.
- No debug write guard: a device debug run saves to the board's flash pages, the same pages every game uses.
- `save::store()` builds the page in `gfx_chunkScratch()`, so call it only between `gfx_wait()` and the next flush.
- A saved game is the seed, the layout and the pairs taken (plus shuffle markers), replayed.
  - The `GOLDEN` hashes in `tools/tests/test_board.cpp` guard the deal.
  - If a layout or the deal generator changes them, bump `VERSION` in `Save.cpp` (magic "CHMJ").
  - Deal generation runs a few pairs per frame and must give the same result however the work is split.
- Palette: felt themes swap only `FELT_DK` and `FELT`. `FELT_LT` is the bamboo ink and stays green in every theme (the felt table in `Frame.cpp`, given to the CHGame library's `pal::setThemes`).
- Some names clash with Arduino macros, and only on the device build: `bit` and `FLASH` here, `sq`, `map` and `word` in other games. Compile for the device early, not just the simulator.
- Simulator `perf`/`cal` numbers are host time scaled by a calibration, so they are noisy on a busy host.
- UBSan: the UBSan run above used a per-object build with `-fsanitize=undefined`. The one-shot build in the shared `tools/chsim/chsim.py` (repository root) has no sanitizer option and would not link with it.
- Faces: `python tools/faces.py` rewrites `tools/art/classic.txt` and `classic2x.txt`, overwriting any hand finishing in them. Diff before re-running. `tools/assets.py` computes the emboss shade.
- Sparrow pipeline: `tools/bird.py` → `tools/art/bird.txt` → `tools/assets.py`.
  - The source sheet is not in the repo (default path `build/assets/Bird.gif`, gitignored), so edit `bird.txt` directly.
  - Its acts (fly in, land, hop, peck, ... fly out) are scripted in `Stage.cpp`.
  - It is about 4.8 KB of flash.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).

## How it fits

- **The pile** sits on a grid of half tiles, so tiles can straddle the ones below (the turtle's top tile, its side tiles). A layout is a list of rows of tiles (about 100 bytes each, from the text maps in `tools/layouts/`), sorted at load into drawing order: a layer at a time and along the diagonals within it, so each tile's side falls only on tiles drawn before it. Whether a tile is free is a few shifts on one word per row.
- **Every deal can be cleared** because it is made by playing a full table backwards: take two free tiles at random, give them a pair of faces, set them aside, until none are left; if the last tiles end up on top of one another, start again (about one deal in fifteen on the turtle). It is worked out a few pairs a frame under the shuffle rattle.
- **Tiles** are drawn from faces at 2 bits a pixel: the face, its emboss (the art's shade, a pixel down and right, which `tools/assets.py` works out from the art) and two inks, through colours chosen at draw time, so one set of art is a tile, a white flash or a gold shimmer.
  - The classic set has two sizes: 8x12 (24 bytes a face) for the whole table, left flat because at 7 px an emboss muddies the strokes, and 16x24 (96 bytes) for the close-up, drawn pixel for pixel rather than doubled, with the dots and bamboo in their traditional patterns.
  - Each tile stands on a body drawn as two bands, ivory then wood, and the bottom layer casts a shadow on the felt. A tile lying squarely on another hides all of it but its body, so only that is drawn.
- **The close-up** draws the pile through a camera: tiles at 1x and 2x are byte-wide copies from SRAM (at 2x a source pixel is a byte, a row two rows), and the whip's in-between sizes are drawn a pixel at a time. When nothing moves the pile is not redrawn at all: the outlines are palette colours that animate for free.
- **Sound** is short step lists (`Sounds.cpp`) played by the CHGame library's piezo engine: a clack for each tile dealt, two clacks and a chime that climbs with the streak for a pair, a rattle for the shuffle, CHBlackjack's fanfare for a cleared table, and the sparrow's chirp.
- **The sparrow** is nine of its animations (39 frames). Its visit is a list of acts - an animation, the order of its frames, how fast, which way it moves - and the game mirrors it to face either way.

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the host builds: root CLAUDE.md). `chgame check` runs it all.

    chgame test     # the board: layouts, deals, matching, saves, the cursor
    chgame sim
    chgame run tools/scripts/clear.txt out/clear
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame audio out/audio   # the sound effects as WAV
    chgame upload       # build and upload the release
    chgame size --top 20

- The host tests cover: the layouts, the free rule against a slow reference, 10,000 deals of each layout cleared by their own order, the same deal however it is stepped (and against known hashes), matching, the streak, undo, shuffles (including tiles that cannot be dealt), saved games, the cursor reaching every free tile, and random calls in any order.
- Script commands (besides the shared `wait`, `tap`, `snap`, `gif`, `rec`): `solve N [W]` takes the deal's own next N pairs with the D-pad and A as a player would, `takehint` the pair a hint is showing, `auto N` any N pairs with no glove work, `goto TILE` walks the glove, `say G <layout> <seed>` deals a table, `say M <a> <b>` takes a pair, `hold`/`release` keep buttons down under the taps, `state` prints the game's state, `cal` and `perf` estimate the device's render time.
- Scripts: `gameplay.txt` (the README's GIF: five clips), `showcase.txt` (the set pieces as separate pictures), `ui.txt` (every screen), `layouts.txt` (each layout dealt), `match.txt` (a pair, frame by frame), `clear.txt` (a whole table to MAHJONG!), `stuck.txt` (no moves, undo, shuffle, hint), `save.txt` (save, continue), `zoom.txt` (the close-up, and a pair taken in it), `play.txt` (a few pairs), `perf.txt` (render cost at both sizes), `device_render.txt` (the same, on the board).
- The README GIF is within about 1.5 KB of the 1 MB limit. The close-up clip is the dear one (every pixel changes during the whip), which is why it records with `rec start 4` and takes two pairs only.
- `chgame upload --debug` adds the CHGame library's serial protocol (`chgame/Debug.h`) for screenshots, injected input and lockstep. The Arduino IDE settings for a release are *Tools > Optimize > Smallest + LTO* and *Tools > USB > Upload only*.
- **The classic faces:** `python tools/faces.py` writes `tools/art/classic.txt` (7 x 11) and `classic2x.txt` (15 x 23): the dots and bamboo laid out from their patterns, the characters, winds, dragons and the bird as text in the script. Edit either file afterwards (or the script), then `python tools/assets.py`.
- **The EASY faces:** `python tools/sheet.py export` writes `tools/art/sheet.png`, an indexed PNG of every face on the game's palette; edit it, then `python tools/sheet.py import` turns it back into `tools/art/tiles.txt` and rebuilds the assets. The text file can be edited directly too: a letter is a colour. A face may use two colours besides the tile's white.
- **Layouts:** edit or add a map in `tools/layouts/` (an X for each tile's corner, a grid per layer), then `python tools/layouts.py` checks it (it fits the screen, nothing hangs in the air, a deal can be found) and writes `src/game/Layouts.cpp`.
- **The sparrow:** `python tools/bird.py [Bird.gif]` finds each pixel's 5x5 block in the GIF, maps the colours onto the game's palette and writes `tools/art/bird.txt` (palette letters, each animation's frames).
- **The title:** `tools/art/logo.txt`, a `#` for each pixel of the lettering; the title screen tints it as CHBlackjack's logo.
