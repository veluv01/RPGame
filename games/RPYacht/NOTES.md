# CHYacht: development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the commands in Development at the end, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHYacht at commit 751b026 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 43,744 of 50,944 B (7,200 spare; the image is 44,104 B, so both save pages fit with 6,328 B to go), static RAM 15,260 of 18,416 B (3,156 spare).
- On the CHGame library since 2026-10-02 (`#include <CHGame.h>`): its input, palette, drawing (sprite4, dither, fillConvex, the 3x5 font), masks, fx maths, screen shake and formatting replace the game's copies; `Fx.h` keeps only the switches that configure the library's `chgame/Sizzle` (particles, banners and floating texts; since later that day, image unchanged). The debug protocol (`chgame/Debug.h`, `CHGAME_DEBUG`), the flash save record (`chgame/Save.h`; Save.cpp says only what the record holds, byte for byte the old layout) and RAMFUNC are the library's too, and tools/chsim/chdrive.py is the shared tools/chsim/chdrivelib.py plus this game's `idle` and `cal` (its copy's `goto ZONE`, a CHCraps leftover with no `Z` hook here, is gone). The score-zero shake (`fx::applyShake(0, TRIM_Y - 1, -1)` in Presenter.cpp) now also shifts the rows and the 2 px edge it uncovers instead of leaving them as they were.
- Verification: simulator and host tests only, all passing as of 2026-10-01 (not re-run since the import). `chgame check` runs these:
  - `chgame test`: the rules against an oracle over all 7,776 rolls, the house player's self-play and the paytable's return, the 3D dice physics at every power for every set of kept dice.
  - `tools/tests/sim_save.py`: save mid-turn, reboot, continue; a finished game leaves nothing to continue but keeps the purse.
  - The scripts in tools/scripts (look, modes, perf, showcase, gameplay) through `tools/chsim/chdrive.py --sim .`.
  - `chgame build` for the release image and its size.
- Never run on a CHGame. The dice cam is estimated at 10-14 ms a frame in the simulator (about 30 fps during the throw, like CHCraps); unmeasured.

## Design decisions

- Chosen (owner): name "Yacht Dice"; the trademarked name of the commercial game is never used. Five of a kind is a "YACHT".
- Chosen (owner): three modes: solo score attack, vs the dealer (CPU), pass-and-play.
- Chosen (owner): bankroll and ante; the final score pays on a paytable; YACHT and the upper bonus pay a bonus.
- Chosen (owner): CHCraps's 3D roll engine, then a close-up tray for holds. Rejected: CHCraps's backstop ("clearly a craps table"); now a padded leather back wall with brass studs, wooden rails and an inlaid line on the baize (`Cam.cpp` tray drawing).
- Chosen (owner, after seeing it): its own slab-style title lettering (tools/art/logo.txt) and 3D dice 25% smaller (`EDGE = 12` in Dice3D.h). The owner approved the game as built.
- Accepted without comment: pass-and-play is score-only (no purse); tray dice are 2D; one CPU strength (strong: mean ~238); kept dice wait on a plate in the cam's corner; each seat throws its own dice colour (Chips.cpp `dieColours`).
- Chosen: paytable 260/300/350/400/500 pays 1/2/3/5/10 antes; a YACHT pays the ante again, the upper bonus a fifth of it. Rejected: the plan's 5x / 1x bonuses (the return would have been far over 100%); it is now ~98.4% against the house player (tools/tests/test_yacht.cpp prints the figures).
- The roll never comes from the physics: Yacht.cpp rolls, then the cam simulates the throw ahead and repaints the pips so the dice land on the rolled numbers.

## Open items

- Fixed 2026-10-01 (with the SD game menu, which makes switching games routine): the save magic in Save.cpp was `0x52434843`, CHCraps's "CHCR"; it is now "CHYD" = `0x44594843`, as its comment always said. A save written by an older build is ignored once. The debug handshake was already CHYD.
- Device run: dice cam frame times, the feel of shaking and throwing, sounds, saving across a power cycle. Device debug builds (`CHGAME_DEBUG` on the board) are `CHYD_LEAN` (no saving, no Options/Stats pages; `-DCHYD_FULL` keeps them). Put the release build back afterwards.

## Gotchas

- Dice outcomes must stay in the rules: never let the cam (Cam.*, Dice3D.*) decide a value. For scripts, `say R seed` fixes the dice and `say F a b c d e` forces the next roll (up to 4 queued).
- The cam (Cam.*, Dice3D.*) is a fork of CHCraps's (../CHCraps), grown from two dice to five; fixes do not flow between the two automatically.
- Drawing is CHBlackjack's band redraw (seats and card, tray, bar repainted only when they change), but the dice cam repaints the whole screen every frame: it is the frame-time hot spot.
- The house player (`ai::` in Yacht.cpp) looks one roll ahead over all 32 hold masks and is spread over a few frames while it "shakes"; `say A 1` lets it play every seat for whole-game scripts.
- tools/assets.py clones Press Play On Tape's Blackjack into tools/.cache/ppot (gitignored) pinned to `PPOT_COMMIT`: needs git and network on first run. The shared tools/art/common/dealer.png (CHBlackjack's hand-painted dealer, repository root) replaces the recoloured PPOT bust; the chips are the shared chip_*.txt there too (CHBlackjack's chip, captured; CHCraps uses the same).
- Debug hooks (list at the end of Screens.cpp): `R`, `F`, `J <T|P|C|2|3|4|O|S|E|L>`, `M purse`, `V` reboot (reload from the save), `A 0|1`, `C focus index`, `G box`, `H` state line, `Q` (sim) timing calibration.
- tools/tests/sim_save.py drives the simulator through this game's tools/chsim/chdrive.py and the shared chsim; it builds the simulator itself.
- Simulator: `chgame sim` (the repository's `tools/chsim`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `chgame size` (`chgame build` runs it).

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the simulator and host tests: `CHSIM_CXX`, zig, clang++ or g++; root CLAUDE.md). Device builds need the CHGame board package 0.3.0, which brings CHGfx and the CHGame library (`chgame build` uses the repository's copies under `platform/board/arduino/CHGame/libraries/`).

    chgame test     # rules, house player, dice physics
    python tools/tests/sim_save.py      # save mid-turn, reboot, continue
    chgame sim
    chgame run tools/scripts/look.txt out/look
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame build        # release image + size report
    chgame upload       # ... and upload it

- Scripts in tools/scripts drive the game through its serial debug protocol (the CHGame library's `chgame/Debug.h`; the game's commands are listed at the end of Screens.cpp). `say R seed` fixes the dice, `say F a b c d e` forces the next roll, `say J P` jumps to a solo game, `idle [W]` runs until the dice cam and the payout are over. The same scripts run on the board with `--device` (a debug build).
- gameplay.txt records the README's clips (01_title, 02_turn, 03_yacht, 04_dealer, 05_result), each its own `rec start 3` / `rec stop`; readme_gif.py joins them. look.txt, modes.txt, showcase.txt and perf.txt are tests: every screen, the three modes, one recorded turn, estimated render cost.
- The README has one picture. The old sheet of screenshots (docs/screens.png) is gone; look.txt and modes.txt still snap every screen into their output folder.
- The game sits in the same casino as CHBlackjack and CHCraps: the same felt, chips and lettering, and CHCraps's 3D dice thrown down a wooden tray.
