# CHCraps — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHCraps at commit fa1fd77 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 49,579 of 50,944 B (1,365 spare), static RAM 15,568 of 18,416 B (2,848 spare).
- The image (the repository's `tools/check_size.py`, its `image:` line; 49,940 B on 2026-10-02) is only ~490 B under the 50,432 B that keeps both A/B save pages (0xF500 and 0xF600, the CHGame library's `chgame/Save.cpp`). Past 0xF500 saving drops to one page; past 0xF600 it switches off.
- Verification: simulator only. `chgame test` (every bet against an independent oracle, exact house edges, dice physics, zone reachability), `tools/tests/sim_save.py` (save mid-hand, debug `V` reboot and continue, the broke case), and the chdrive scripts in `tools/scripts`; `chgame check` runs all of it.
- As of 2026-10-01 it has never run on the device: frame times unmeasured, sound unheard.

## Design decisions

- Chosen: TABLE option CLASSIC / BEGINNER (Beginner keeps line, don't, odds, field, place 6/8 on a roomier layout).
- Chosen: hold A on ROLL to shake, release to throw.
- Chosen: CHBlackjack's split layout (wall, felt, bar) plus the 3D "dice cam".
- Chosen: CHBlackjack's dealer works the table as the stickman and calls every roll.
- Chosen: keep the lit ON puck - a white disc showing the point number in the 3x5 font, pinned to the number box.
- Chosen: the dice show pips whenever they are on screen. The result is repainted at the back-wall hit, choosing the labelling that changes the fewest faces.
- Chosen: music only if flash is left once the game is complete; there is none (`Sounds.h` says so).
- Architecture to keep: the rules settle the whole roll in `Craps::throwDice()` (a result per spot); the presenter (`Presenter.cpp`) only replays it - call, losers swept, pays, home, come moves. Money has already moved, so a save mid-show is always consistent.

## Open items

- First device run. Simulator estimates put the dice cam's shake and the result + banner at 9-15 ms a frame, over the ~8 ms drawing budget for 60 fps. Measure on the board (`chgame run --device tools/scripts/perf.txt OUTDIR`) before optimising.
- Listen to the sound effects on the piezo (`chgame audio out/audio` renders them to WAV meanwhile).
- Fixed 2026-10-01 (the repository's docs/status.md): CHYacht used this game's save magic `0x52434843` "CHCR" with the same
  version 1, so after playing one, the other accepted its save. CHYacht now has its own, "CHYD".
- Music: deferred until flash allows (it does not now).

## Gotchas

- Flash is effectively full while both save pages are kept. Tactics already in use: chips and pucks are span sprites (the CHGame library's `sprite4`) recoloured by remap tables (drawn from CHBlackjack's chip, made by `tools/assets.py`); all 24 die orientations come from one walk of quarter turns stored in a 24-bit constant (`labelDie`, `WALK` 0x288A28 in `Dice3D.cpp`); the dice share the CHGame library's sine table (`fx::isin`); no music.
- Device debug builds get `CHCR_LEAN` automatically (`config.h`): no saving, no Options or Stats. `-DCHCR_FULL` forces the whole game into a debug build (check it fits).
- Dice3D: integer Euler-angle cubes and a tilting pinhole camera; physics steps once per 60 Hz tick, so the dice keep their speed if a frame is slow. The throw is pre-simulated deterministically and the dice relabelled afterwards: any physics change moves where they land, so rerun `chgame test` after touching it.
- Drawing: the table redraws only the bands (wall, felt, bar) that changed or that something moving touched; the dice cam redraws everything each frame, so that is where frame time goes.
- Debug hooks (listed above the hook in `Screens.cpp`): `R` seed, `F` force rolls, `J` jump to a screen, `M` purse, `V` reboot (reload from flash), `E` set a bet, `X` point, `C` cursor zone, `Z` D-pad route, `H` state, `Q` (simulator) calibration. New hook letters must avoid the protocol's own: `? S K L N P T B`.
- chdrive extras: `goto ZONE` walks the cursor with real D-pad taps on the game's planned route; `idle` waits out the dice cam and the payout.
- Credits: the dealer's art and the 3x5 font are Press Play On Tape's (Simon Holmes / filmote, Stephane C / vampirics) by way of CHBlackjack. Credits are exactly those in NOTICE; add no others.
- Paths moved with the monorepo: the size report is the repository's `tools/check_size.py`, the simulator its `tools/chsim/chsim.py`; per-game tools stay in `tools/`. For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- Device debug builds leave out saving; the board may be in use, so announce a debug upload and put the release back afterwards.

## Development

Everything can be checked on a PC: Python 3 with `pip install -e .[sim]` in the repository root, and a C++ compiler for the simulator and the tests (zig, clang++ or g++ on the PATH, `pip install ziglang`, or `CHSIM_CXX="path/to/zig c++"`; root CLAUDE.md).

    chgame test          # rules, dice physics, layout reachability
    python tools/tests/sim_save.py           # save mid-hand, power-cycle, continue
    chgame sim
    chgame run tools/scripts/sc_show.txt out/sc_show
    chgame gif         # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py                   # dealer, logo, chips -> src/assets/
    chgame audio out/audio  # every sound effect to WAV
    chgame build|upload [--debug]
    chgame size

- The README GIF: `gameplay.txt` records five clips (`01_title`, `02_comeout`, `03_hardfour`, `04_hot`, `05_sevenout`). The come-out is played with the buttons alone; the later clips set bets with `say E` off camera, and every roll is forced with `say F`. The play clips use `rec start 4` to stay under 1 MB (the dice cam compresses badly).
- Other scripts are tests and look-dev, written to `out/`: `betting.txt` (the plaque, a refused bet, chips down and back, picking from the rack), `beginner.txt` (a Beginner-table hand: line and field, point 8, the 6 placed for $12, $20 odds, a hard six, winner eight), `showcase.txt`, `sc_show.txt`, `sc_screens.txt`, `look_table.txt`, `look_cam.txt`, `review_roll.txt`, `perf.txt`.
- The debug build (`--debug`) speaks the CHGame library's serial protocol (`chgame/Debug.h`). The game's commands are in `Screens.cpp`: reseed or force the dice, jump to a screen, set bets, the purse or the point, move the cursor, dump the table state (letters under Gotchas). The same scripts run on the board (`chgame run --device SCRIPT OUTDIR`); `goto` is simulator only.
- Installing by hand (Arduino IDE): board package 0.3.0 or later (it brings CHGfx and the CHGame library), *Tools > Optimize > Smallest + LTO* (the default; needed to fit) and *Tools > USB > Upload only*. `chgame build` does the same as `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly CHCraps`, with `--library` for the repository's copies of the libraries.
- `tools/tests/test_craps.cpp` checks every bet against every point and all 36 rolls with an independent oracle, and works out each bet's house edge exactly by enumerating the dice (pass 1.414%, don't 1.364%, field 2.778%, place 6 1.515%, odds 0). It also runs a long fuzz for money conservation and a chi-square test of the dice.
- Dice3D details: Euler-angle rotation matrices, faces back-face culled, scanline filled, shaded in three levels and outlined. Physics: gravity, felt bounces that turn speed into a tumble, the back wall's kick, side rails, and the two dice pushing off each other. The relabelled die is always a real die (opposites add to 7); of the four ways to do it the game picks the one that changes the fewest pips, and the new pips go on at the back-wall hit, in a shower of sparks.
- The point puck: the black OFF puck turns over and slides to the number's box, where it sits as a round white badge on the box's top right corner, so the box's own number stays clear.
- The board beside the plaque colours the roll history: winners gold, craps wine, a seven-out red, hard numbers marked.

Files:

    CHCraps.ino, config.h   the frame loop and build switches
    Craps.*                 the table: bets, payouts, the point, the dice
    Cam.*, Dice3D.*         the dice cam: 3D dice and physics, the scene
    Layout.h, Zones.*, Felt.*, Wall.*, Bar.*, Chips.*
                            the wall, the layout and its spots, chips, the bar
    Presenter.*, Fx.*       the presenter; the library's chgame/Sizzle configured in Fx.h
    Screens.*               title, play, options, stats, the two endings
    Sounds.*                the sound effects (the CHGame library plays them)
    Save.*                  what a save holds (the CHGame library keeps it in flash)
    src/assets/             the art, generated by tools/assets.py
    tools/                  tests, assets, the driver and its scripts, device helper
