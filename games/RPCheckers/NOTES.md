# CHCheckers — development notes

Agent-facing notes for continuing work on this game. Rules and controls are in [README.md](README.md), commands under *Development* below; the platform is covered by the repo-root [CLAUDE.md](../../../../../../../../../CLAUDE.md) and [docs/](../../../../../../../../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHCheckers at commit a9ec530 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 41,901 of 50,944 B (9,043 spare), static RAM 17,108 of 18,416 B (1,308 spare). RAM is the tight budget here. It includes the 1 KB think-frame stack.
- Save pages: the repository's `tools/check_size.py` puts the image at 42,240 B, so both A/B save pages fit with about 8.2 KB to spare.
- Verification as of 2026-10-01: simulator only. `chgame check` passes:
  - Host tests: perft from the opening (7 … 179,740), a second naive move generator across all 8 rule sets, both kinds of draw, and 2,000 random games with undo and save/load.
  - The CPU: legal moves, inside its budget, abortable, repeatable from a seed, stronger at a higher level.
  - Every script runs twice with identical frames, and the release build compiles.
- It has never run on the board.

## Design decisions

Made by the owner:
- This is "the same game, number 2": CHChess's iso board, camera, glove and called-out moves, with the flash freed from the chess engine spent on the show.
- The rules are options inside the game: the RULES screen, reached from setup in both modes, offers JUMPS FORCED/FREE, KINGS SHORT/FLYING and MEN AHEAD/ANY WAY. The default is American (FORCED/SHORT/AHEAD), and White moves first.
- The pieces are casino chips in the chess set's colours: white vs silver/blue/navy.
- All four polish areas are wanted: jump combos, the crowning ceremony, captured-piece flair (the trays), and a richer table and title.
- The trays are mats beyond the far edges of the board, built from whole squares so their edges follow the 2:1 diagonals.
  - Rejected: an inlay line around the table.
  - Each mat is in its owner's colours: White's is SILVER with WHITE trim, Black's BLUE with INK trim.
  - In view space, the left mat holds what the viewer took. Chips stack 3 piles of 4, with a HUD tally; the map shows a row of chips seen from above.
- The engine (`Engine.*`) is the project's own Apache-2.0 code. No third-party engine or licence file is involved.

## Open items

- Device run, never done (follow "The device" in the root CLAUDE.md).
  - Render profile with `say Y`. The simulator's estimate of about 14 ms max when zoomed is unreliable.
  - CPU speed with `say W` (ms, nodes). This sets the level node budgets and `SIM_US_PER_POLL` in `Frame.cpp`, which currently guesses about 6,000 nodes/s.
  - Stack high-water marks with `perf`, the frame stack included.
  - Sound and the title tune by ear (`chgame audio out/audio` renders them on the PC).
  - No `device_*.txt` scripts exist yet; `chgame check` already skips that name pattern. On the board only `G`, `M`, `W` and `Y` exist among the game's commands, so device scripts must start games with `say G` and play with `say M` or the pad.
- Choices approved only as plan assumptions, which the owner has not yet seen on screen:
  - Must-jump UI: the D-pad still visits every piece; pieces that can't jump say MUST JUMP and buzz; the ones that can are ringed. In the middle of a chain a single continuation plays itself and B is refused (KEEP JUMPING).
  - The opponents TOURIST {300, 60}, DEALER {4000, 12} and THE HOUSE {30000, 0} (node budget and margin, `LEVEL` in `Match.cpp`) are untimed guesses.
  - Slow motion only on a combo's last hop and on the winning capture. The banners are DOUBLE!/TRIPLE!/QUAD!/RAMPAGE!, KING ME! and SWEEP!.
  - Title:
    - Chips drop in, and CHECKERS slams down in the PPOT 3x5 font at scale 3.
    - An attract game plays: `src/states/DemoLine.h`, from `tools/tests/demo_line.cpp`, ending in a triple jump and a crowning.
    - A looping single-voice tune plays (MUSIC option).
  - Departures from the plan: no 1 bpp logo sprite, no plaque, and no decorative carpet chip stacks (just the tray mats).
- The owner's art redraw through `tools/sheet.py`, covering `tools/art/chip.txt`, `chiptop.txt` and the glove (the shared `tools/art/common/hand.png`; an import saves an edited glove back there, for every game).

## Gotchas

- The engine works in steps, not whole moves. A multiple jump is several steps, with the turn staying on the piece.
  - Undo and saves store each step as its index among the steps legal at that moment (`Match.cpp`, `Record::m`).
  - If the order of move generation changes, bump `VERSION` in `Save.cpp`, or old saves will replay the wrong moves.
- While the CPU thinks, frames are drawn from inside the search (a poll every `eng::POLL_NODES` = 32 nodes), on their own 1 KB stack (`frameStack` in `Frame.cpp`).
  - The search can run about 1.5 KB deep in the 2 KB main stack, and a frame needs about 800 B more.
  - With only about 1.3 KB of RAM free, find the RAM before adding buffers.
- In lockstep (scripts, simulator) a frame is drawn every three polls (96 nodes), so a scripted CPU move always takes the same frames. Keep the search deterministic for a given seed.
- Debug protocol:
  - The game's commands are documented above `debugHook()` in `Screens.cpp`.
  - `J Q R H A V X` are simulator-only. Positions are set up with `say X <32 cells> <w|b> <rules>`, and chdrive's `auto N` relies on `A`.
  - The protocol itself is the CHGame library's (`chgame/Debug.h`, on in `CHGAME_DEBUG` builds); `dbg::holdWhile(searching)` holds the game's commands while the CPU searches, and `dbg::frameStack()` reports the frame stack's high-water mark as `fstk=` in P (board only).
  - Device debug builds are `CHCK_LEAN` (no saving, no Options or Rules screen: Setup's RULES row steps through the rule sets instead); `-DCHCK_FULL` overrides that.
- Art scale goes in whole multiples: `iso::ascale()`/`sized()` for art (2x from tileH 8, 1x on the flat map) and `zoomed()` for things painted on the board.
- `tools/sheet.py`:
  - The MASTER row holds the art. The WHITE/BLACK rows are the palette swaps (`tools/art/sides.txt`).
  - Chips are anchored at the base centre (9, 17 in the cell). They can grow right and down, not left or up.
  - Keep the glove's fingertip where it is.
  - `src/assets/` is generated by `tools/assets.py`.
- There is about 9 KB of flash room. Only `#pragma GCC optimize("Os")` is used; CHBackgammon's further trims (`no-ipa-sra`, etc.) have not been applied here and are available if needed.
- Saves use magic "CHCK" in pages shared with every other CHGame game; saving here replaces another game's save, and vice versa (README).
- Shared tools: the simulator is the repository's `tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).

## How it fits

- `Engine`: the rules and the CPU. The board is the 32 dark squares in a padded row, moves are single steps, and the search is alpha-beta over those steps with iterative deepening inside a node budget, only stopping on positions with no jump pending. About 3 KB.
- `Match`: turns, the events the presentation shows, undo and saved games (a snapshot plus one byte per step since).
- `Stage`: the play screen - camera, glove, movers, the flying chips and trays, combos, the crowning, the HUD. `Iso`: the board and table.
- `Frame`: while the CPU thinks, frames are drawn from inside the search on a stack of their own, in bursts, with a soft clock ticking.
- `Sounds`: the effects and the title's tune, played by the CHGame library's piezo sequencer (`chgame/Audio`).
- The save goes through the library's `chgame/Save`, in the last two flash pages.
- On the play screen the piece under the glove fades its outline black to white, the one picked up gets a rainbow outline, and the piece a jump would take flashes. With nothing in the pressed direction the glove wraps round to the farthest spot the other way. Holding a piece and B, a bar under the top line fills before the inspection zoom: let go before it is full and the piece goes back.

## Development

Everything can be checked on a PC (Python 3 with `pip install -e .[sim]` in the repository root, and a C++ compiler for the host builds: root CLAUDE.md).

    chgame check                # everything below except the board, in one go
    chgame test      # host tests
    chgame sim
    chgame run tools/scripts/moments.txt out/moments
    chgame gif     # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    chgame build         # release build + size report
    chgame upload        # build and upload the release
    python tools/assets.py               # art (tools/art) -> src/assets
    python tools/sheet.py export         # the art as one sheet to edit; import reads it back
    chgame audio out/audio   # the effects and the title's tune to WAV

- Host tests (`tools/tests/test_checkers.cpp`): move counts from the opening against the published numbers (7, 49, 302, 1469, 7361, 36768, 179740); every rule combination against a second, naive move generator written in the test; hand-made positions for each rule; both kinds of draw; 2,000 random games through the same calls the pad makes, with undo and save/load on the way; the CPU.
- `chgame check` runs every script in `tools/scripts` twice and compares the frames. `--quick` runs each once, `--no-device` skips the device compile.
- Scripts: `say X <32 cells> <w|b> <rules>` sets a position up, `auto N` plays N of your moves with the pad (the game picks them, the script walks the glove), `goto SQ` walks the glove to a square, `waitturn` runs until it is your move, `board` prints the board; `snap` and `rec` take pictures. The header of `tools/chsim/chdrive.py` lists them.
- `gameplay.txt` records the README's clips (`01_title` ... `05_sweep`) at `rec start 4`; one opening move is all that fits beside the three showpieces under 1 MB, because the camera's dive changes every pixel. `showcase.txt` records the same showpieces (and a flying king and the CPU's turn) as separate GIFs, as a test only.
- On the board: `chgame run --device SCRIPT OUTDIR` (a debug build, uploaded and driven the same way). What is still to do there is under *Open items*.
- With the Arduino IDE: *Tools > Optimize > Smallest + LTO* and *Tools > USB > Upload only* (the game has no use for USB Serial).
