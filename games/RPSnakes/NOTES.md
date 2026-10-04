# CHSnakes — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHSnakes at commit 89bba02 (2026-10-01); develop here now, not in the old repo.
- Release build (board package 0.3.0, 2026-10-02; `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 36,956 of 50,944 B (13,988 spare), static RAM 15,360 of 18,416 B (3,056 spare).
- The debug protocol (`chgame/Debug.h`, `CHGAME_DEBUG`), the flash save record (`chgame/Save.h`; `Save.cpp` says only what the record holds, byte for byte the old layout) and RAMFUNC are the CHGame library's since 2026-10-02, and `tools/chsim/chdrive.py` is the shared `tools/chsim/chdrivelib.py` plus this game's `board`, `waitturn`, `cal`, the calibrated `perf` and a `say` that also takes the frame ack owed after a HELD command. Image 37,136 -> 37,208 B (the library's `audio::setOn()` out of line, about +12 B; its save code, about +30 B), static RAM 15,360 B unchanged; the same frames on every script.
- Save pages: the repository's `tools/check_size.py` reports the image as 37,320 B (board package 0.3.0, 2026-10-02). Both A/B pages (0xF500, 0xF600, the CHGame library's `chgame/Save.cpp`) fit with 13,112 B to spare (the limit is an image of at most 50,432 B).
- Simulator-verified (as of 2026-10-01): `chgame check` passes. It runs:
  - the host tests (100,000 seeded games, saves, CPUs, the SHARK table against the layout);
  - every script in `tools/scripts/` twice, with identical frames and no drawing into a frame still being sent;
  - the device compile and size check.
- Device (2026-10-02): the debug build runs the title on the board. Over its first 138 frames the title rendered in 17.8 ms on average (21.9 ms worst; the simulator estimates 22.2 ms), and the stack reached 736 of 2,048 B. The release build is installed but not yet played through. Pace in play and the sound by ear are unmeasured.
  - The first debug upload faulted on its first frame: the CHGame library's `dbg::begin()` painted the stack over `main()`'s live values once LTO had inlined it there. Fixed in the library (paint up to `sp`); every game's debug build had the same code.

## Design decisions

- Owner's choices:
  - A top-down flat board with a zoom camera: the close-up follows the token and whips out to the whole board.
  - Two rule sets at setup: CLASSIC (one die, 6 rolls again, exact roll or bounce back) and ARCADE (two dice and pick one, doubles roll again, landing on a rival bumps it).
  - The tokens are CHBoardwalk's fruit.
- Second pass, which the owner called fantastic:
  - The ARCADE pick shows a marching arc of dots from the token to the target square, red and flashing when a snake waits there.
  - The camera stays close and rides down a snake, zooming out only after the spit.
  - The title is slab-serif lettering after CHBlackjack's logo. `LOGO_SNAKES` is drawn at half size and doubled by `tools/assets.py`; `LOGO_LADDERS` is 1x.
- Third pass: a ladder climb also stays close with the camera following, zooming out only at the top. The chomp sets off a big blast (two rings of chunks plus the particle bursts).
- Agent choices reported to the owner but not yet confirmed. Keep them unless the owner says otherwise:
  - 2-4 seats in any mix of humans and CPUs; everyone starts on square 1, which is safe.
  - A bumped token drops straight down one row, not back to the start.
  - One hand-placed board: 8 ladders and 8 snakes in `Layout.cpp`, with the big snakes at 96 (to 46) and 99 (to 61).
  - A small bobbing arrow marks whose turn it is. The glove was too big on 11 px squares, so it only points at the die in ARCADE's pick bar.
  - CPU turns, and the QUICK pace, play at the overview.
  - The title is a 4-CPU ARCADE game playing itself.

## Open items

- The rest of the device run (pace in play, sound by ear, `tools/scripts/perf.txt`). It needs the owner's go-ahead.
  - `chgame audio out/audio` renders the effects to WAV on the PC meanwhile.
  - The simulator estimates 5-14 ms a frame (the title about 20 ms), over the 8.3 ms budget during motion, so expect 30-60 fps.
  - Snakes and ladders are the main cost.
  - `tools/scripts/perf.txt` prints the per-moment estimates.
  - After a device run, `chgame check --compare out/<sim> out/<device>` compares the frames pixel for pixel.
- The owner's feedback on the look and on the unconfirmed choices above.
- An art redraw through `tools/sheet.py`, if the owner wants one.

## Gotchas

- The board is one table: `LINK[]` in `Layout.cpp`. After changing it:
  1. Run `python tools/turns.py`.
  2. Paste the printed table into `TURNS[]` in `Cpu.cpp` (the SHARK's turns-to-go, 101 bytes). The host tests recompute it and fail if they disagree.
  3. Use `python tools/lookdev.py` to preview the board at each size.
- ADL trap: a stage helper must not share its name with an `fx::` function that takes an `fx` enum. A local `banner()` called with an `fx::BannerStyle` resolved to `fx::banner` and put banners off-screen. The helper is now `call()`; see the comment in `Stage.cpp`. (`fx::banner` is the library's `chgame/Sizzle` since 2026-10-02, configured in `Fx.h`; the trap is the same.)
- Snakes are procedural bead chains pushed by a travelling sine wave. Only the head (`SNAKE_HEAD`, `SNAKE_HEAD_OPEN`) is art, and its `FELT_LT`/`FELT` pixels become each snake's two colours.
- Art round trip:
  - `python tools/sheet.py export`, edit `tools/art/sheet.png` (indexed palette), then `python tools/sheet.py import`.
  - The import writes each changed sprite to `tools/art/<name>.png`, which from then on overrides that sprite's letters in `tools/art/sprites.txt`. `token_banana.png` already does (it is the shared one in the repository's `tools/art/common/`; a copy in `tools/art/` would take precedence).
- `CHSN_LEAN` is on for every device debug build (`CHGAME_DEBUG` on the board): no saving and no options screen.
  - It is the same switch as in CHBoardwalk, whose framework this game started from, although this game has room to spare: a full debug build is 39,940 B (2026-10-02).
  - To test saving on the board, build debug with `-DCHSN_LEAN=0` too (the macro is `#ifndef`-guarded): `tools/device.py` has no option for it, so use `arduino-cli compile` with `--build-property "build.extra_flags=-DCHGAME_DEBUG=1 -DCHSN_LEAN=0"` (plus the FQBN and `--library` paths in the root CLAUDE.md).
- Debug hooks (the CHGame library's protocol, `chgame/Debug.h`) are sent with `say`; the list is in `Screens.cpp`. `M` and `O` wait (HELD) while the stage is busy (`dbg::holdWhile`):
  - `G` new game (seat kinds, mode, seed), `D` next dice, `M` place a token, `O` overview, `H` state;
  - simulator only: `J` screen, `V` look at a square, `X` square tones, `Q` calibration.
- The determinism check in `chgame check` runs every script twice. New animation code must draw from seeded state only, never from host time.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).

## Development

Everything can be checked on a PC (Python 3 with `pip install -r tools/requirements.txt` from the repository root, and a C++ compiler for the host builds: zig, clang++ or g++ on the PATH, `pip install ziglang`, or `CHSIM_CXX="path/to/zig c++"`; root CLAUDE.md).

    chgame check               # host tests, every script twice, device compile + size (--quick, --no-device)
    chgame test     # the board, both games turn by turn, saves, the CPUs, 100,000 seeded games
    chgame sim
    chgame run tools/scripts/snake.txt out/snake
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame audio out/audio   # the sound effects as WAV
    chgame upload [--debug]           # build and upload
    chgame size --top 20

- Status: complete and played through in the simulator. Frame timing and the sound on the handheld itself are still to be checked.
- The host tests check that the board is a fair one (no chains, no wall of snakes) and that 100,000 seeded games all end.
- Scripts tap buttons, `waitturn` until the game wants you, `snap` and `rec` take pictures, and `say` sends debug commands: `say G 1 3 0 0 1 11` a new game, `say D 2 5` the next dice, `say M 0 91` puts a token on a square. `cal` and `perf` estimate the device's render time. The same scripts run on the device with a debug build (`chgame run --device SCRIPT OUTDIR`).
- `gameplay.txt` records the README's clips (`01_title` ... `05_home`) at `rec start 5` to stay under 1 MB; `showcase.txt` (title, a snake's meal, a ladder) is kept as a test.
- A device build with the Arduino IDE or plain `arduino-cli`: *Tools > Optimize > Smallest + LTO*, *Tools > USB > Upload only*, or the release FQBN in the root CLAUDE.md (`chgame build` does the same).
- Ladders are two rails and a rung every four pixels, a span to each row they cross. Each row of the board is a copy of one of five 64-byte patterns (`BoardView.cpp`).
- The art is in `tools/art`: `sprites.txt`, plus a `<name>.png` for each sprite edited through `tools/sheet.py` (see Gotchas).
