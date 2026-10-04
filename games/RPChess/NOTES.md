# CHChess — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the commands under Development below, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHChess at commit 34e4382 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 48,611 B, static RAM 17,528 of 18,416 B (888 spare). The image (`../../../../../../../../../tools/check_size.py`'s `image:` line) is 48,992 B, 1,440 B under the 50,432 B that keeps both A/B save pages.
- Without LTO it overflows (~350 B over). Device debug build (`-DCHGAME_DEBUG=1`, which turns on `CHCH_LEAN`: no saving, no Options screen) was 49,484 B / 17,904 B.
- 2026-10-02: the debug protocol, saving and RAMFUNC are the CHGame library's (`chgame/Debug.h`, `chgame/Save.h`, `chgame/RamFunc.h`), no longer game copies. The save record (magic "CHCS", version 2, pages 0xF500/0xF600 in `chgame/Save.cpp`) is byte for byte the old one, so existing saves load. Release image 48,656 -> 48,776 B (RAM 17,544 unchanged); device debug 49,452 -> 49,544 B (RAM 17,560 -> 17,580). P's `fstk=` (the frame stack, `dbg::frameStack` in `Frame.cpp`) is on the board only now; the simulator, which has no second stack, leaves it out.
- Verification: simulator - `chgame test` (perft, draw rules, book, snapshots, every CPU level, fuzzed games with undo and save/load) and the chdrive scripts in `tools/scripts`; `chgame check` runs both and the release build (`tools/game.py`). Device: the CHGfx 1.3 release has run on the board, and the render times, think times and stack peaks under Gotchas were measured there.

## Design decisions

Board, glove, highlights:
- Chosen: 20x10 iso tiles, half-size pieces; SELECT switches board / MAP. Rejected: the 2x CLOSE view toggle (removed).
- The D-pad visits all own pieces, blocked ones included (useful for navigation); nearest in the screen direction, wrapping to the farthest the other way. With a piece up it visits the targets.
- A blocked piece shows NO MOVES; A on it (`stage::deny`) buzzes and flashes NO MOVES and the glove red (`RM_ALERT`) 3x over 24 frames. That is the only use of the red glove for the player.
- Hovered piece: outline fades black/white (palette mode `HOVER` animates `FX_A` as a grey ramp). Rejected: pick-up sparkles (distracting).
- Picked-up piece: outline cycles `fx::RAIN`. Prey under the glove flashes red (`RM_PREY`); other capturable pieces flash white.
- Chosen square blinks dithered/solid: CYAN for a move, RED for a capture. No ghosting. Last move is lit only during the reply to it.
- A plate at the foot names the piece or target and calls out moves in words. HUD is the name only. Rejected: chips, material count, trays.
- Hints and coordinates are always on (both options removed; `Save.h` keeps their bytes as `unused`/`unused2` so saves keep their layout, the menu maps past them with `optByte`). Options: SOUND, BOARD, PACE (FUN/QUICK). Setup button reads BEGIN. No title subtitle.
- Knights are never mirrored (mirroring broke the shading); `sprite4`/`spriteRot` have no mirror.

Check and mate:
- Vs a human, CHECK! stays up (`fx::holdBanner`, a blinking PRESS A plate, call-out frozen) until any button (`stage::waiting`/`acknowledge`, flag `waitPress`); `busy()` holds the turn. The CPU in check never waits. CHECKMATE! always waits the same way before the result panel.
- The checked king's body beats `RM_PREY` (`((frame>>3)+5)&5 == 0`, phased to land as the HOVER outline is light), under the glove too, but not once picked up. `spots()` offers only pieces that can move (cached per position).
- Rejected: a red glove in check, and gold-to-red trim/UI in check (wrong vibe).

Inspect (B held, `stage::inspect`):
- Zoom 10; a straight push goes to a diamond corner, a diagonal to an edge middle. A, START and SELECT are ignored while B is held. Rejected: spring-back (removed).
- Holding a piece, a tap is < `HOLD_B` (32 frames) and a 2 px cyan bar grows at y 10-11 meanwhile (`holdBar`); otherwise the threshold is 8 frames.

CPU turn (it should act like a player):
- `onTurn` glides the red glove to the CPU's own king (`holdT` 24) before the search starts (it used to start over the player's piece).
- Search in bursts (`Frame.cpp`): every `SEARCH_MS` 2000 a `BURST_MS` 450 full-rate burst (`stage::thinkPick` moves the glove); in between a frame every `BOB_MS` 133 with `frameCount |= 7` so the glove keeps bobbing. Buttons or a menu force frames (`screens::holdFrames`).
- The pick: glove rests on the piece (24/8 frames), taps, the piece lifts (its target lit, prey flashing), glove glides to the square, stays 60/16, taps, moves. The CPU's piece gets the HOVER outline too; `stage::selected()` returns the player's selection only.
- A soft clock (`Sfx::Tick`/`Tock`, every 2 s) plays while it searches: slows perceived time, hides hiccups. They are `audio::SOFT` in `Sounds.cpp` (1/8 duty).
- Vs the CPU the camera stays on the human's side; the hand-over spin is for 2P only.

Camera and effects:
- Whip zoom starts at once, `iso::tileH` 5 to 10, one step per drawn frame (`zoomDrawn`). After landing it holds until all particles are gone (`outWait`), then zooms out snapping to the framing. Stays in on mate. QUICK pace or the map disables it.
- Captures play at half speed (`CAPTURE_SLOW` 2); the fly is parametric in its tick. Capture sound: impact plus ~0.5 s falling swoops.
- Dust puffs are sized to the zoom and exactly the landing square's colour (they must blend in). Particles are screen space and do not scroll (`fx::scroll` was removed: it broke the map); `busy()` waits for `fx::particles()` so the camera never moves under them.
- The carpet is plain (the dot lattice was dropped).

Opponents and flash priorities:
- Three opponents (cut from five for flash; save `VERSION` 2): BEGINNER {400,150}, EXPERT {3000,25}, GRANDMASTER {12000,0} in `Match.cpp`.
- When cutting flash: sound effects over tunes, no CPU-vs-CPU mode, a minimal opening book is fine, gameplay first.

## Open items

- Awaiting the owner: building with `-DCHGFX_ISR_IN_SRAM` (its SRAM is there now: the CHGame library draws `text35x2` from flash, which freed ~300 B); the pacing constants (`SEARCH_MS`, `BURST_MS`, `BOB_MS`) were deliberately left unchanged after the faster CHGfx 1.3 build.
- Twice a scripted device move was refused because the game had gone to the title mid-script. Not reproducible; possibly buttons pressed on the board during the run.

## Gotchas

- RAM is the scarcer budget (888 B free on 2026-10-02, on the CHGame library). Every RAMFUNC costs SRAM as well as flash. Game RAMFUNCs are the CHGame library's `RAMFUNC(name)` (`chgame/RamFunc.h`, `.gnu.linkonce.r.app.<name>`); each needs its own name within the game.
- CHGfx 1.3: only `gfx_fillEllipse` (and the staged palette) were adopted. Rounded rects, dither, `sprite4`, `spriteRot`, shake, `copyRow` (`Iso.cpp`), the 3x5 font / `text35` / `text35x2` / masks stay off CHGfx because its versions measured bigger or slower. All but `copyRow` (and the palette, the `fx::` maths and the input) are the CHGame library's now (`platform/board/arduino/CHGame/libraries/CHGame`, `<CHGame.h>`), not game code. Per-item table in `docs/CHGfx-notes.md`; measure before swapping any of them.
- Measured on the board (CHGfx 1.3 debug builds): full redraw 6.0 ms normal, 5.6 map, 7.8 zoomed; whip-zoom frames 6.1 ms average, 8.6 worst; GRANDMASTER 9.2-9.3 s a move with bursts and bobs (raw search ~7 s, ~1,700 nodes/s). Scripts: `tools/scripts/device_render.txt`, `device_think.txt`.
- Stack: main peaks ~1,552 of 2,048 B. Frames drawn from inside the search run on `Frame.cpp`'s own 1 KB stack (peak ~640 B).
- Chip facts found here (more in `../../../../../../../../../docs/performance.md`): flash code ~5 cycles an instruction, SRAM code ~2; newlib's `memmove` is a byte loop in flash; an async full flush costs ~5 ms of CPU; pieces take ~5 ms (~1.2 us per sprite run); `sprite4` needs separate 1:1 and scaled loops (register pressure); a packed-nibble sprite format was tried and reverted.
- Engine: `ch2k.hpp` is ch2k from ArduChess, MPL-2.0, patched. Keep it under MPL-2.0 with its change list at the top current; everything else is Apache-2.0. Credits are exactly those in NOTICE (Peter Brown / tiberiusbrown for the engine, Press Play On Tape for the 3x5 font); add no others.
- `Engine.cpp` sets `CH2K_POLL_NODES` 8 (~5 ms between callbacks) and `CH2K_MAX_PLY` 10 (stack). The opening book is 4 plies; `tools/book.py` can only cut it, so a deeper book has to come from upstream ch2k or the old repo's history.
- Debug protocol: the CHGame library's (`chgame/Debug.h`; `CHGAME_DEBUG`), hello `CHCS <version>`; the game's commands are `debugHook` in `Screens.cpp`, held while the CPU searches (`dbg::holdWhile(searching)`). `Y` prints the render profile per section, `W` the last think's time and nodes. `Y`, `W`, `R`, `H` answer at once even while the CPU searches; any other scripted game command acknowledges a pending CHECK! (scripts `tap A` through mate). Simulator only: `X <fen>` (2P), `V <fen>` (vs the CPU, you to move), `R <sq>` (D-pad route), `H` (board, your-turn and waiting flags), `J`, `Q`.
- chdrive extras: `tools/chsim/chdrive.py` extends the shared `../../../../../../../../../tools/chsim/chdrivelib.py` with `goto SQ`, `waitturn` (answers CHECK! with A after 90 frames), `board` and `cal`; `rec start/stop` and `freegif` (device only: the simulator's free mode is not real time) are the shared driver's.
- `tools/scripts/gameplay.txt` (the README's GIF) records the title and four clips from set positions (`say G`, `say V <fen>`), each move made with `goto` and A. After a clip that ends on the CPU's turn it runs `waitturn` before the next `say V`: a position set while the CPU is still moving leaves `goto` without a route. The CPU's replies follow the game's seed (`say G`'s last number; from the menus, the timing of the title and setup presses). To plan a whole game vs the CPU: replay it in the simulator (deterministic), read `H`, and pick moves with a host build of the engine (`Engine.cpp` + `load_fen` + `benchThink` at ~200k nodes).
- Art: `tools/sheet.py export/import` is the owner's Photoshop workflow; keep it working. Import reads colours by RGB value (Photoshop reorders the colour table on save). If MASTER's pieces are missing or the side rows are reshaped, the art is rebuilt from the White/Black colour pairs (up to 16, INK kept for the outline). The source of truth is `tools/art/pieces/*.png` and the glove, the shared `tools/art/common/hand.png` at the repository root (hand-finished; `sheet.py import` saves an edited glove back there, so every game that draws it gets the change: then run `tools/assets.py` in each of them); `tools/pieces.py` only writes `tools/art/gen/`. `tools/art/sides.txt` generates `SIDE_REMAP`; `HAND_TIP` is generated from the art. Black is silver/blue/navy.
- Don't name things `sq`, `map` or `word`: Arduino macros.
- Check any drawing change pixel-for-pixel against a baseline simulator run of the same scripts.
- Paths moved with the monorepo: the simulator is `../../../../../../../../../tools/chsim/chsim.py`, the size report `../../../../../../../../../tools/check_size.py` (per-game tools stay in `tools/`). For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- Device debug builds leave out saving; the board may be in use, so announce a debug upload and put the release back afterwards.

## Development

Everything can be checked on a PC (Python 3 with `pip install -r ../../../../../../../../../tools/requirements.txt`, and a C++ compiler for the host builds: zig, clang++ or g++ on the PATH, `pip install ziglang`, or `CHSIM_CXX="path/to/zig c++"`; root CLAUDE.md).

    chgame test     # perft on five positions, draw rules, book, snapshots, every CPU level, ~100,000 fuzzed moves with undo and save/load
    chgame sim
    chgame run tools/scripts/showcase.txt out/showcase
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    chgame build        # release build and size (the Arduino IDE: Tools > Optimize > Smallest + LTO, Tools > USB > Upload only)
    chgame upload [--debug]
    python tools/sheet.py export        # the art as one indexed PNG; `import` takes the edits back
    python tools/pieces.py              # render the pieces from the 3D models (tools/art/gen)
    python tools/assets.py              # art -> src/assets
    python tools/book.py N              # cut the opening book to N plies
    chgame audio out/audio    # the sound effects as WAV

- The build needs link-time optimisation to fit, and `usb=uploadonly` saves 0.6 KB (the game has no use for USB Serial). By hand: `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly --library ../../../../CHGfx --library ../../../../CHGame .`. `--debug` adds the CHGame library's serial protocol (`chgame/Debug.h`) for screenshots, injected input and lockstep.
- Scripts: `goto SQ` walks the glove to a square with D-pad presses, `waitturn` waits for your move, `rec start N` / `rec stop NAME` record across a script, `gif` and `snap` take pictures. `say G <mode> <black> <level> <seed>` starts a game, `say X <fen>` sets up a position for two players and `say V <fen>` against the CPU, `say M <from> <to> [promo]` plays a move (squares 0 = a1 .. 63 = h8). `cal` and `perf` estimate the device's render time. A move to the last rank made with the buttons opens the PROMOTE TO panel: `tap A` takes the queen.
- `showcase.txt`, `moments.txt`, `check.txt`, `ui.txt`, `views.txt` and the rest are tests of the screens and the big moments (pictures to `out/`); the `device_*.txt` scripts are for a debug build on the board.
- Editing the art: `python tools/sheet.py export` writes `tools/art/sheet.png`, an indexed PNG on the game's palette (transparent background, swatch included): the pieces and glove as drawn (MASTER), the pieces as White and as Black, and the palette swap between them as a key. Edit it (Photoshop keeps it indexed), then `python tools/sheet.py import` turns MASTER edits into `tools/art/pieces/` and the glove, recolouring on the White/Black rows or the key into `tools/art/sides.txt`, and rebuilds the assets. Details at the top of `tools/sheet.py`.
- How the CPU's turn looks (the pacing is in `Frame.cpp`): its glove first goes over to its own king, then the search runs, drawing a frame only every 133 ms and stopping every two seconds for a burst of full-rate frames in which the glove glides to the piece it is weighing. Then it plays as you do: taps its piece, carries it to the square, rests a moment and taps again.
- The engine (ch2k, ~12 KB): a 0x88 board with fully legal move generation, alpha-beta with quiescence search, a Texel-tuned evaluation and an opening book, cut here to four plies.
- Sound: the CHGame library's piezo sequencer (`chgame/Audio.h`) playing short step lists: a knock for each landing, a smash and spinning swoops for a capture, fanfares for mate.
