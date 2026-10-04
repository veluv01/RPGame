# CHBackgammon — development notes

Agent-facing notes for continuing work on this game. Rules and controls are in [README.md](README.md), commands under Development below; the platform is covered by the repo-root [CLAUDE.md](../../../../../../../../../CLAUDE.md) and [docs/](../../../../../../../../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBackgammon at commit 80cf126 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): image 49,880 of 50,944 B, static RAM 16,700 of 18,416 B (1,716 spare).
- 2026-10-02, from a player's feedback: beavers and raccoons (option), AUTO play (option, on by default) and the D-pad rule below. They cost about 860 B; paid for by cutting the coach (its verdict on each play, its line on the answer and result panels, its option: 952 B; the SELECT hint stays) and the screen shake (344 B, 184 B RAM). Image 50,328 -> 49,880 B; static RAM 16,892 -> 16,700 B.
- Save pages: both A/B save pages (0xF500/0xF600) need the image to stay at or below 50,432 B: 552 B left. The image was 49,524 B at import (about 900 B of margin); on the CHGame library's sound engine (2026-10-02) 50,152 B, on its debug protocol, saving and RAMFUNC (below) 50,232 B. The pages are the CHGame library's (`chgame/Save.cpp`). Treat flash as full.
- The debug protocol (`chgame/Debug.h`, `CHGAME_DEBUG`), the flash save record (`chgame/Save.h`; `Save.cpp` says only what the record holds, byte for byte the old layout) and RAMFUNC are the CHGame library's since 2026-10-02, and `tools/chsim/chdrive.py` is the shared `tools/chsim/chdrivelib.py` plus this game's `goto`, `move`, `auto`, `board`, `waitturn` and `cal`. Image 50,152 -> 50,232 B (the library's `audio::setOn()` out of line, about +18 B; the rest its save code and LTO's inlining), static RAM 16,892 B unchanged; debug image 49,728 -> 49,844 B; frames unchanged.
- Verification as of 2026-10-01: simulator only. `chgame check` passes. It runs the host tests (UBSan, rules against a naive reference, whole matches, the CPU), runs every script twice with identical frames, checks that the network's evaluation is bit-identical in the simulator and on the host, and compiles the release build.
- It has never run on the board. Frame times, CPU thinking time, sound and saving on the hardware are all unmeasured.

## Design decisions

- Chosen (a player's request, 2026-10-02): the D-pad moves the glove LEFT/RIGHT to the next spot along the same half of the board (top or bottom, by the spot's screen position), wrapping to the far end of that half, and UP/DOWN across to the other half's spot nearest in x (`nearest()` in `Screens.cpp`). It used to score distance along the press plus a sideways penalty, which skipped tall stacks.
- Chosen (a player's request): AUTO (on by default): a human's dice are thrown when they could not double (the opening roll, a single game, the Crawford game, the other side owning the cube), and a roll with only one distinct play is played for them and the dice picked up (`onlyPlay()` in `Match.cpp`, through the CPU's play path). The opening roll is always thrown.
- Chosen (a player's request): BEAVERS (off by default): doubled, TAKE / PASS / BEAVER; beavered, the doubler TAKE / RACCOON (no pass). Each turns the floating cube over to twice its value; the cube cannot pass 64. The options live in the old `pad` byte of `Options` as `rules` bits (`RULE_MANUAL`, `RULE_BEAVERS`), so old saves load unchanged and default to AUTO on, beavers off.

- Chosen: a top-down view with the whole board always visible. A 2x punch-in whip zoom is kept for the big moments only.
- Chosen: the default is a single game without the cube. Match play (MATCH TO 3/5/7) with the doubling cube, Crawford and the game's own match equity table (`tools/train/met.py`) is opt-in on the setup screen.
- Chosen: at the end of a turn the glove goes to the dice. A picks them up and passes the turn; B takes the last move back. Nothing is final until the dice are up.
- Chosen: the game has its own slab-serif display font (drawn here; now the shared `tools/art/common/font.txt`, used by CHCrossword, CHFour and CHWords too) for the logo, banners and headings, like Blackjack's title lettering.
  - The logo uses a plain lower-case 'o'.
  - Rejected: a checker as the logo's 'o'.
  - Rejected: a dark red (WINE) drop shadow on display-font lettering.
  - Rejected: marquee chasing bulbs on the title.
- Chosen: the title/BEGIN menus use the 3x5 font at 2x (`text35x2`), as at the other tables, with at least 2 px of padding inside the gold selector.
- Chosen: the cube is odd-sized so its value sits exactly in the centre, with no stray corner pixels.
- Chosen: the rules, the network, its trainer and weights, and the match equity table are this project's own code (Apache-2.0). Keep credits exactly as `NOTICE` has them: Press Play On Tape for the 3x5 font, and nobody else added.

## Open items

- Device run (kit ready, never run; follow "The device" in the root CLAUDE.md).
  - `chgame run --device tools/scripts/device_render.txt out/dev_render`: render cost per section (`say Y`) and whole-frame perf.
  - `chgame run --device tools/scripts/device_think.txt out/dev_think`: `say W` prints positions weighed, ms and `slice_us`. The longest slice should stay under about 8 ms; tune `QUANTUM` (64 positions a tick) in `Match.cpp`.
  - Then run `chgame check --compare out/<sim run> out/<device run>`.
  - Simulator estimates (host-time, unreliable): play averages about 5-7 ms and peaks at about 11-13 ms; the title takes about 12 ms.
- Device checks still to do: sound by ear, saving and CONTINUE on hardware, and `say E` on the board, which should match the host network value.
- Choices the owner has not reviewed yet:
  - The glove is drawn turned over (a vertical flip) when it works from below: the top-half points, and the bar/tray on Red's turn (`fromBelow()` in `Stage.cpp`). The owner has objected to mirrored art elsewhere because flipped shading reads wrong, so this may need a separate drawing, which costs flash.
  - Red vs ivory chips, points printed on the felt, and a wood frame with a gold inlay.
  - The centred cube shows 64.
  - The 3x5 font is now the CHGame library's (`platform/board/arduino/CHGame/libraries/CHGame/src/chgame/Draw.cpp`), so its 'M' is PPOT's, as at the other tables. This game's own copy had an 'M' with a lighter middle (one row, not two), because two of PPOT's side by side, as in BACKGAMMON, read as HH at title size.
- Cut earlier to fit flash, and not reviewed by the owner: party rays, the bear-off chip flip, and dice on the title. Re-adding any of them needs a flash cut first.
- Cut 2026-10-02 (the owner chose them) to pay for the player's requests: the coach (the owner would rather have the effects) and the screen shake. `Options::coach` stays in the save record, unused, for its layout. The options screen also lost its line THE CPU TAUGHT ITSELF. Bringing the shake back would cost 344 B and still leave both save pages (about 200 B to spare).
- Measured release-build savings of other features, if room is needed again (each removed alone, 2026-10-02, before the cuts above): saving 1,792 B; the cube and match play 1,668+; coach and hint 1,276 (the coach alone, measured later: 952); the dice's spin 640 (152 RAM); the felt dust 232; banners 1,272; the whole particle system 1,240 (484 RAM); the setup screen 1,164; the options screen 876; the prime / closed-out / match-party moments 404; fountains 192; the CPU's thinking glove and clock 120; the last-move plate 112; the whip zoom in play 96. A smaller particle pool (24) saves 240 B RAM and no flash.
- The CPU's beaver and raccoon: when its chance on the other side's roll is at least 55% (`cube::BEAVER_AT`). Gammons and the match score are not weighed, unlike its takes and doubles; a better rule would use the match equity table.
- Beavers and raccoons in a match are a house rule (standard match play has none); the cube exists only in matches here, so that is where the option applies.

## Gotchas

- Flash tactics already in use:
  - Every hand-written `.cpp` starts with `#pragma GCC optimize("Os", "no-ipa-sra")`; no-ipa-sra saved about 256 B under LTO. Keep it in new files.
  - Avoid 64-bit division: it pulls in `__divdi3` (about 1.2 KB). See the 32-bit maths in `Cube.cpp`.
  - The glove is one `HAND` sprite, turned over with `SPR_FLIP_V` in `sprite4`.
- Size levers measured earlier:
  - `-flto-partition=one` would save about 260 B, but it needs link flags in the board package.
  - The biggest remaining items are features: match/cube about 2 KB, display font + mask about 1.8 KB, tumbling-dice rotation about 0.7 KB.
- Arduino's `binary.h` defines `B0`, `B1`, ... as macros. Don't use those names as identifiers.
- `bg::Board` is `alignas(4)` (the network compares boards a word at a time) and has padding. Compare, hash or copy `.n` only, never the whole struct.
- CPU:
  - It thinks a slice per frame, with no second stack. Its choice must not depend on how the work is sliced, and the tests check this.
  - The incremental evaluation keeps the last hidden sums. That, plus the evaluator and move generator running from SRAM, makes a position about 3x cheaper on the host.
  - Races use a 25-entry table fitted to the exact bear-off database.
- Generated files, don't hand-edit:
  - `src/ai/NetData.cpp` comes from `tools/train/train.py export`, and `tools/train/net.bin` is the network that ships.
  - `src/ai/RaceData.cpp` comes from `train.py race`.
  - `src/ai/MetData.cpp` comes from `tools/train/met.py`.
  - `src/assets/` comes from `tools/assets.py`.
- The hand-written sources sit in the sketch's top folder, so the Arduino IDE shows them as tabs; only the generated files above stay in `src/`. The rules are `Rules.*`, not `Board.*`: the sketch folder is on the include path, and on a case-insensitive file system (Windows, macOS) a `Board.h` there is what the core's `#include <board.h>` (in `wiring.h`) finds.
- Saves:
  - `VERSION` 2 in `Save.cpp`. Bump it on any change to the `Data` layout (the header, whose flag byte says a game is saved, and the CRC are the library's, `chgame/Save.h`).
  - A save holds the position as the turn began, its roll and the dice generator's state, so a reload can never change a roll.
  - The save pages are shared with every other CHGame game; records are told apart by magic "CHBG".
  - CHFour used the same magic value (`0x47424843`, "CHBG") by mistake until 2026-10-01; it is "CHF4" now (../../../../../../../../../docs/status.md).
- Device debug builds (`CHGAME_DEBUG` on the board) are `CHBG_LEAN`: no saving, no setup/options screens, no hint, and games start with `say G` (or at once from the title's menu). `-DCHBG_FULL` forces those parts in, but don't expect it to fit.
- Debug protocol (the CHGame library's `chgame/Debug.h`):
  - The letters `? S K L N P B` belong to the protocol, and `T` too in a `CHGAME_PROFILE=1` build.
  - The game's commands are documented above `debugHook()` in `Screens.cpp`.
  - `A` (play for the human, used by chdrive's `auto`) and `Q` (calibration) are simulator-only. `G C D U V X R W Y E H J` also work on the board.
- The simulator's `cal`/`perf` render estimates are host time. Don't trust them for small differences.
- Art: `python tools/assets.py` writes each drawing to `tools/art/gen/` as a PNG. To redraw one, copy it up to `tools/art/` (see Development). The glove and the display font are the shared ones in `tools/art/common/` at the repository root; a file of the same name in this game's `tools/art/` would override them.
  - `tools/art/sides.txt` is the per-side palette swap.
  - `python tools/font_preview.py` draws the display font.
  - `tools/lookdev.py` is a Python mock-up of the board for choosing colours, written to `docs/mockups/` (gitignored). It is not the renderer.
- Shared tools: the simulator is `../../../../../../../../../tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).

## How it fits

- **The CPU** (`Net.*`, `Ai.*`, `Race.*`) evaluates a position just after a side has moved:
  how likely is that side to win? The answer is a neural network with 196
  inputs (for each side and point: a first checker, a second, a third, and
  each one more; the bar; the tray), 16 hidden units and one output - 3,188
  bytes of 8-bit weights, evaluated with integer adds, a 65-entry sigmoid
  table and no floating point. It learned by temporal-difference self-play
  (`tools/train`): 1.6 million games against itself on a PC, about a quarter
  of an hour, starting from random weights and the rules alone. The trainer
  compiles the game's own rules and its integer evaluator, so the network
  that was measured is bit for bit the one in the cartridge: it wins 99.7%
  against random play and 77% against a hand-written player that runs,
  hits and covers its blots sensibly; the integer version plays the float
  one dead even.
  * EXPERT tries every play the roll allows (about 17 on average, up to
    several hundred for a double) and takes the one the network likes best.
  * GRANDMASTER then takes its best ten and, for each, every roll the other
    side could throw and that side's best answer: a few thousand positions.
    It beats EXPERT 55% of the time - in backgammon, where the dice decide
    so much, that is a real edge - and the hand-written player 80%.
  * BEGINNER picks at random among its three best plays that are not much
    worse than the best: it loses to EXPERT nearly three games in four
    (73%).
  * A pure race (no contact left) is played, at every level, by a
    25-number table fitted to the exact answer for all 54,264 home-board
    positions, which the handheld has no room for: it bears off 0.013 rolls
    slower than perfect play.
  * **The cube** (`Cube.*`): the network's chance, and a match
    equity table worked out from first principles in `tools/train/met.py`
    (a 7x7 table and the post-Crawford column, 112 bytes): take when taking
    is worth more than passing, double near the point where the other side
    should pass.
  * **Speed.** Consecutive plays differ by a checker or two, so the network
    keeps the hidden sums of the last position it saw and adds or takes
    away only the rows of the points that changed (an incremental
    evaluation; the tests check it against a fresh one half a million
    times). That, and the evaluation and the move generator running from
    SRAM, makes a position about three times cheaper. The thinking is done
    a slice per frame, so the frames never stop and nothing needs a second
    stack; the red glove wanders over the checkers being weighed, and a soft
    clock ticks if it takes long.
* **The rules** (`Rules.*`) are written once, for "the side to move",
  each side counting the points its own way. Every play of a roll is
  enumerated without storing any (the list for a double can run to
  hundreds), each final position exactly once. A second, naive
  implementation in the tests agrees with it on 2.1 million rolls.
* **The board** (`Table.*`) is drawn top-down in world units that the
  camera scales by fifths, 1x to 2x, a row at a time: each row's wood,
  felt and tray in one pass of word stores, then the points' tapering spans,
  from SRAM. The checkers, dice and glove are span-encoded sprites
  recoloured per side by a palette swap, with a second, detailed drawing of
  the checker for the close-ups. A still board is not redrawn: the frame is
  sent again, so the palette effects (the shimmering targets, the outlines,
  the lettering's shimmer) keep moving for free.
* **The lettering** is a slab serif drawn for the game (the shared `tools/art/common/font.txt`,
  capitals and figures 11 pixels high, a few lower-case letters for the
  logo): 600 bytes of packed bits, drawn through the CHGame library's `Mask` (once CHBlackjack's)
  for the outlined, gradient-filled logo, banners and headings, and plain
  for the setup and options choices. Menus use the 3x5 font at twice the
  size, as in the other tables.
* **Sound** is the CHGame library's piezo engine (once CHBlackjack's
  sequencer) playing short step lists, three bytes a step: the dice rattling to rest, a knock for each checker set
  down, a smash and falling swoops for a hit, a chip dropped in the tray,
  fanfares.
* **Saved games** hold the position as the turn began, its roll, the match
  and the cube, and the dice generator's state, so reloading can never
  change a roll.
* **Room.** The game fills the flash; the last few kilobytes came from
  replacing a 64-bit division with a 32-bit one (1.2 KB of library code),
  table-driven saving, a font found by walking its glyphs rather than an
  index, and the shadows drawn as rounded boxes rather than ellipses.

## Development

Everything can be checked on a PC (Python 3 with `pip install -r ../../../../../../../../../tools/requirements.txt`, and a C++ compiler for the host builds: root CLAUDE.md).

    chgame check               # host tests, every script twice, net check, device compile + size
    chgame test     # the rules, the dice, whole matches, the cube, the CPU
    chgame sim
    chgame run tools/scripts/showcase.txt out/showcase
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets (previews in tools/art/gen)
    chgame upload       # build and upload the release
    chgame audio out/audio     # the sound effects as WAV

- `chgame check`: the host tests; each script in `tools/scripts` run in the simulator twice (the frames must be identical; `--quick` runs each once); the network's evaluation the same in the simulator as on the host; the device build compiled and sized (`--no-device` skips it). `--compare A B` compares two runs' images (the simulator's against the board's).
- `chgame test`: the rules against the reference implementation, the step-by-step validator (every way of playing a turn ends on a legal position, and can never get stuck), the dice (chi-square), hundreds of whole matches through the game's own calls with take-backs, doubles, takes, passes, the Crawford rule, save and reload, the notation, the cube's judgement, and the CPU (always legal, the same choice however its thinking is sliced; its incremental evaluation the same as a fresh one).
- Scripts: every script but `beaver.txt` starts with `say U 1` (AUTO off), as they were written for thrown dice; `beaver.txt` covers beavers, raccoons, AUTO and the D-pad. `say X <side> <position>` sets up a position (`w 6:5 8:3 r 24:2 ..`: each side's points and counts; `say V <side> <opponent> <position>` against the CPU), `say G <mode> <level> <seed> [length]` starts a game (mode 0 against the CPU, 1 two players), `say C <length> <white> <red> <cube> <owner> <crawford>` sets the match, `say D 6431` stacks the next rolls, `move 13 7` walks the glove with D-pad presses and picks up and sets down, `waitturn` waits for your turn, `auto` plays on for you, `snap` and `rec` take pictures, `cal` and `perf` estimate the device's render time. `showcase.txt` and the others are tests now: only `gameplay.txt` makes a README picture.
- Training: `python tools/train/train.py train out/net.bin --games 600000` trains a network from nothing; `bench int:tools/train/net.bin heur` plays two players against each other (`random`, `pips`, `heur`, `float:<file>`, `int:<file>`, and the game's own opponents `ai0:` `ai1:` `ai2:`); `export tools/train/net.bin src/ai/NetData.cpp` writes the tables; `race src/ai/RaceData.cpp` fits the race table. `python tools/train/met.py src/ai/MetData.cpp` works out the match equity table.
- `chgame upload --debug` adds the serial protocol (screenshots, injected input, lockstep); the debug build is lean (Gotchas). `tools/scripts/device_render.txt` and `device_think.txt` measure the render time and the CPU on the board.
- Art: to redraw a drawing, copy it from `tools/art/gen/` up to `tools/art/` (`checker.png`, `checker_big.png`, `dice.png`), edit it with the palette's 16 colours, and run `tools/assets.py` again. The glove (`hand.png`) and the display font (`font.txt`, `#` and `.` per glyph) are the shared copies in the repository's `tools/art/common/`, used by other games too.
- Arduino IDE: *Tools > Optimize > Smallest + LTO* (the game needs link-time optimisation to fit) and *Tools > USB > Upload only*.
