# CHWordWheel — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the commands under Development below, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHWordWheel at commit 7c97419 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): the image (check_size's `image:` line) is 50,376 B, 56 B under the 50,432 B line that keeps both save pages; static RAM 14,956 of 18,416 B (3,460 spare).
- Puzzle banks: 111 built in (`FLASH_BYTES = 1376` in tools/phrases/build_bank.py), 606 on the card (sdcard/PHRASES.BNK, magic `WWPB`, version 1).
- Verification: `chgame check` (banks rebuilt, host tests incl. thousands of CPU episodes and both bank readers, every script twice with identical frames, card.txt with PHRASES.BNK in the simulator's slot, the redraw diff check over tools/scripts/diff, release build) passed as of 2026-10-01; not re-run since the import. The whole episode is playable in the simulator.
- Never run on a board: first run at all, render times (simulator estimate 3-6 ms at the busiest), the wheel's feel, sounds, saving across a power cycle, and the SD reader.

## Design decisions

- Chosen (owner): titled WORD WHEEL. The TV show's trademarked name never appears anywhere in the repo, puzzles included. tools/phrases/blocked.txt lists what must never appear and is kept in ROT13 (build_bank.py decodes it) so the repo never spells those names out; the old repo's history was rewritten so it is ROT13 in every commit. Keep it that way.
- Forked from CHRoulette: prefix `CHWW_`, debug handshake CHWW, save magic `0x57574843` "CHWW".
- Chosen (owner): three podiums, each HUMAN or CPU (default You, Dot, Ace), pass-and-play between humans.
- Chosen (owner): the wheel is shown only as the pointer close-up (wedges with printed values sweep past a flipper); no full wheel. The rules draw the stop and the spin is solved to land there.
- Chosen (owner): the series dealer hosts, with bubble patter; board panels reveal themselves (no letter-turner).
- Chosen (owner): a full episode (toss-up, three rounds, final spin, bonus round) plus Quick Play; career total and stats. As many of the show's tropes as possible.
- Chosen (owner): engine first with a built-in flash bank, then an SD bank using the shared SD reader (now CHSd).
- Chosen (owner liked the game, asked for depth): the shading/depth pass, with palette shades and 50% dithers lit from the top left.
- Cut for flash: screen shake, the win-screen sunburst, the victory/broke songs (only the title tune is left), the PPOT end lettering.

## Open items

- Asked of the owner, unanswered:
  - Panel lettering A (bold 5x7, double-struck text: `bold57` in BoardView.cpp; built) vs B (doubled 3x5).
  - Converging wedges (built, WheelStrip.cpp) vs flat wedges.
  - Built-in bank size trade: dropping the mystery/split/wild/prize wedges (~1.6 KB) or the 5x7 lettering via B (~0.85 KB) would each buy roughly 80-100 puzzles per KB.
- The 56 B now under the two-page line could raise `FLASH_BYTES` a little (about 4 more built-in puzzles); the owner's call, alongside the trade above.
- Device bring-up: first run, render times, wheel feel, sounds (audition the WAVs from `chgame audio out/audio`), save across a power cycle, the SD reader incl. pulling the card mid-game. Device debug builds are `CHWW_LEAN` (no Setup/Options/Stats screens; set podiums with `W`; saving and the SD bank stay) and write save pages only after `say E 1`; they have no particles either (`SIZZLE_NO_PARTICLES` in `Fx.h`): with board package 0.3.0 they were 40 B over the flash until those went (2026-10-02), now 50,188 B. Put the release build back afterwards.
- Grow tools/phrases/phrases.txt toward thousands of puzzles (the card bank has room; 64 B a puzzle).

## Gotchas

- Size pragma: every size-optimised file begins `#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")`, ~1 KB smaller than plain Os + LTO here. Measured dead ends: `no-ipa-cp` broke the build; making `Sig::add` (Presenter.cpp) noinline made it bigger. Hot pixel loops are `RAMFUNC` (SRAM) and unaffected. Probably worth trying in sibling games. The pragma also governs the library's `chgame/Sizzle` bodies, which `Fx.cpp` includes after it (coins only, no burst, no shake: `Fx.h`).
- The drawing primitives, 3x5 font, masks, palette, input, fx maths and formatting are the CHGame library's (`platform/board/arduino/CHGame/libraries/CHGame`), not CHGfx 1.3's versions (CHGfx's text would cost ~2 KB more). The game's own panel shape is `edgedRound()` in Shapes.*. The library's font has real lower case, but everything the game draws is in capitals (the bank, names and quips): keep it so, or upper-case new text, to keep the look.
- Nothing fails when the image passes 50,432 B: read check_size's "save pages free: N" after every build.
- Banks: edit tools/phrases/phrases.txt (`CATEGORY|PUZZLE` lines), then `python tools/phrases/build_bank.py` (checks every puzzle fits and wraps it; writes src/bank/BankData.* and sdcard/PHRASES.BNK; `--curve` prints bytes against puzzles kept). `chgame check` rebuilds both in place and only notes a change: commit the regenerated files with phrases.txt.
- In a fresh clone run `python tools/phrases/build_bank.py` (or `chgame check`) before `chgame test`: the bank test reads tools/phrases/build/bank_ref.txt, which build_bank.py writes and git ignores.
- No-repeat dealing is a Feistel permutation per section fixed by a seed, so the save holds a seed and three counters, not a list.
- SD: CHSd is used as a library (`#include <Fat.h>`, `<SdSpi.h>`; `../../../../CHSd`); the game holds no copy of it. `chgame build` passes it with `--library`, and the simulator builds CHSd's `Fat.cpp` with the pretend card in its `host/` folder (`$CHSD_CARD`). After changing CHSd run its tests (`python platform/board/arduino/CHGame/libraries/CHSd/tests/run_tests.py`) and this game's `chgame check`.
- Simulator card: `CHSD_CARD=sdcard/PHRASES.BNK` (a non-.img file goes on a pretend FAT16 card, so the FAT code runs); `say X 1|0` (sim only) puts the card in or pulls it out.
- Music: the title tune is generated by tools/make_music.py (the song; the composer is the repository's tools/music/composer.py) into src/audio/Music.cpp, and every `CHGAME_DEBUG` build, the simulator included, leaves the score out. Hear it with the shared preview, `chgame audio out/audio`. The effects are in Sounds.cpp, played by the CHGame library's engine (chgame/Audio.h).
- Redraw diff: `chgame redraw` builds the game a second time with `CHSIM_FORCE_FULL`, which `render()` in Presenter.cpp honours by redrawing everything; the switch stays in the source.
- Mock-ups: tools/mockup.py with the repository's tools/pixkit.py (shared with CHRoulette; `set_upper35(True)` here, as the game upper-cases its text), which reads the dealer and the glove from tools/art/common/ and the 3x5 font from the CHGame library's chgame/Draw.cpp. tools/assets.py checks the dealer and faces come out byte-identical to ../CHBlackjack's src/assets/Assets.cpp.
- Debug hooks (CHWordWheel.ino): `R seed`, `F stops` (0..71 = wedge*3 + peg slot), `U section i`, `C letter`, `V 1|0` solve right/wrong, `M player cash`, `G step`, `W k0 k1 k2`, `J <T|U|P|E|O|S>`, `H` state line, `E 1|0`, `X 1|0` (sim).
- The debug protocol is the CHGame library's (`chgame/Debug.h`, on with `CHGAME_DEBUG`); the save record's pages and CRC are the library's too (`chgame/Save.cpp`), the game's `Save.*` says what goes in it. The script driver is the repository's shared `tools/chsim/chdrivelib.py`; tools/chsim/chdrive.py adds only `rec pause`/`rec resume` and `cal`.
- The Arduino core defines `bit` and `DEFAULT` as macros: don't use them as identifiers.
- Simulator: `chgame sim` (the repository's shared `tools/chsim`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `chgame size` (the repository's `tools/check_size.py`; `chgame build` runs it).

## How it fits

- Flash, roughly: the rules 6 KB, screens and presentation 14 KB, drawing and effects 7 KB, sound 2.5 KB, the SD reader and bank 2.3 KB, the built-in puzzles 1.4 KB, the rest core, USB and CHGfx. The built-in bank's share is one number (`FLASH_BYTES` in tools/phrases/build_bank.py): every kilobyte freed elsewhere is about eighty more puzzles without a card.
- Puzzles in flash are Huffman-coded text (about 12 bytes a puzzle at this size, 10 at 600), grouped by section and category, already wrapped onto the board's four rows by the build script; the category names are coded the same way, after the puzzles. On the card they are plain 64-byte records, so a puzzle is one block read.
- Drawing: the play screen redraws only what changed - the wall, the board panel by panel, the podiums, the prompt bar - and whatever a banner or confetti passed over. The wheel's wedges are spans between edges stepped in fixed point toward a hub below the screen.
- Depth comes from the palette's own shades and 50% dithers, lit from the top left throughout: raised letter tiles with a shadowed edge, empty slots sunk into the board, a gold bezel with corner bulbs in the cycling colour, podiums with a lit header, a groove and a shaded foot, keycap letters in the picker, drop shadows under the rack, the bubble and the printed lettering, and a wheel shaded like a drum - dark at both sides and toward the hub, metal dividers, brass pegs, the rim's shadow across the face.

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the host builds: root CLAUDE.md).

    chgame check               # banks rebuilt, host tests, every script twice, the redraw check, device compile + size
    chgame test     # the rules (thousands of CPU episodes), the flash bank's decoder, the SD bank's reader
    chgame sim
    chgame run tools/scripts/round.txt out/round
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    chgame redraw tools/scripts/diff/diff_round.txt out/d 1    # against a build that redraws everything every frame
    python tools/phrases/build_bank.py [--bytes N] [--curve]                     # the banks
    chgame audio out/audio                              # every sound and the tune as WAV files
    python tools/mockup.py              # the layout mock-ups the screens were built from
    python tools/assets.py              # art -> src/assets
    chgame build|upload [--debug]
    chgame size --top 20

- Scripts: `say R 7` seeds the episode, `say U 1 0` picks the next puzzle, `say F 4` fixes the next spin's stop, `say C N` calls a letter, `say V 1` makes the solve come out right, `say G 5` jumps to a step (the full list is under Gotchas and at the top of CHWordWheel.ino); `snap`, `gif` and `rec` take pictures. `set CHSD_CARD=sdcard/PHRASES.BNK` puts the SD bank in the simulator's slot.
- The README's GIF: tools/scripts/gameplay.txt records five clips (`01_title`, `02_tossup`, `03_spin`, `04_bankrupt`, `05_bonus`) at `rec start 3`; readme_gif.py joins them. It is close to the 1 MB limit: the wheel's blurred frames are the expensive ones, so lengthen a clip only by shortening another. tools/scripts/showcase.txt records single moments with `gif` and is kept as a test.
- A debug build carries the test protocol (the CHGame library's `chgame/Debug.h`) and leaves out the Setup, Options and Stats screens to make room for it.
- Arduino IDE: *Tools > Optimize* must be **Smallest + LTO** (the default; the game does not fit without it), *Tools > USB* **Upload only**; board package 0.3.0 brings CHGfx, CHSd and the CHGame library.
- The card: `PHRASES.BNK` goes in the top level of a FAT32 or FAT16 card under exactly that name (exFAT, as cards of 64 GB and more come, cannot be read). The title screen says CARD 606 PUZZLES or BUILT-IN 111 PUZZLES; the game looks again each time the title comes up, and a card pulled out mid-game falls back to the built-in puzzles.
