# CHBlackjack — development notes

Agent-facing notes for continuing work on this game. Rules and controls are in [README.md](README.md), whose format is [docs/game-readme.md](../../../../../../../../../docs/game-readme.md); the platform is covered by the repo-root [CLAUDE.md](../../../../../../../../../CLAUDE.md) and [docs/](../../../../../../../../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBlackjack at commit 88d8fc7 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 45,292 B, static RAM 15,436 of 18,416 B (2,980 spare).
- Save pages: `../../../../../../../../../tools/check_size.py` reports the image as 45,668 B, so both A/B save pages fit with about 4.7 KB to spare.
  - A build with the IDE defaults (no LTO, USB Serial) still cleared both pages when last measured, by about 60 B (see "How it fits" below). Any growth breaks that, so re-measure when the game grows.
- Verification as of 2026-10-01: this is the most device-proven game here.
  - Host tests: `chgame test` covers the rules, every payout, PPOT bug regressions and a 16,000-hand fuzz.
  - Simulator scripts in `tools/scripts`.
  - On the board, `device.py run` scripts run in lockstep, and the screenshots match the simulator's.
  - `pace.txt` gave about 300 frames per 5 s with no late frames.
  - Render times measured on the board, before and after the CHGfx 1.3 migration: showcase average 9.8 → 5.8 ms, worst frame 28.2 → 14.0 ms, bust hand worst 25 → 11 ms.
  - `tools/probes/FlashProbe` proved that a flash page above the image survives a re-upload.
- The last commit (court portraits without the grey frame) changed art only and was re-recorded in the simulator. No device run is recorded for it.

## Design decisions

- Chosen: a faithful port of Press Play On Tape's game flow and screens. `Round.cpp` keeps PPOT's ViewState flow; the additions are presentation.
  - Casino rules are the default; the Classic preset keeps PPOT's rules.
  - PPOT's money bugs are fixed in both presets (listed in `NOTICE`).
- Chosen: the credits are a cozy casino "back room" page, reached with A from Stats. The dealer tells the credits in his speech bubble, under a neon sign and cigarette smoke. Rejected: a demoscene-style credits screen.
- Credits are exactly as in `NOTICE`, the README and the credits page (A on Stats; `say J C` jumps to it in a script): PPOT, filmote (code) and vampirics (art). Don't add or change names without the owner.
- Rejected: the original's "Mario" dealer (Nintendo fan art). It is not included; don't bring it back.
- Chosen by the owner: court-card portraits without the grey SILVER frame. CHPoker (`../CHPoker`) made the same change, so keep the two decks' court art in step.
- The release build is Smallest + LTO with USB "Upload only" (`tools/device.py` has the exact settings).

## Open items

- `CHBJ_LEAN` is no longer needed to fit: a full debug build fits (image about 47.5 KB). Whether to retire it is the owner's call.
- Known slow frames, pre-existing and not fixed:
  - The still screens (title, options, stats) redraw every frame for their first 90 frames, about 1.5 s (`step` in `render()`, `Screens.cpp`).
  - That measured about 13-15 ms a frame on CHGfx 1.2 and has not been re-measured since the migration.
- Not yet checked on the board: the frameless court art (88d8fc7).

## Gotchas

- Flash:
  - Every hand-written `.cpp` uses `#pragma GCC optimize("Os")`.
  - No `snprintf`: it is 3.5 KB with 64-bit division; use the CHGame library's `fmt*` (`chgame/Fmt.h`).
  - No `pinMode`: its pin tables are about 2 KB; write the registers.
  - Sound is the CHGame library's piezo sequencer (`chgame/Audio.h`), not CHGameSound; `Sounds.*` holds the effect tables, `playSong` and the two sound switches (the options' mode and SELECT's mute, `sound::`).
  - "How it fits" below has the budget breakdown. Measure with `chgame size` after every change.
- Big outlined lettering (Mask: a 1 bpp mask, grown for the outline, painted in up to three layers) costs about 5-10 ms per word on the board. Draw it once, on still screens or static layers, never every frame.
- The credits page draws its felt once and redraws only the wall band: 3.3 ms a frame measured on CHGfx 1.2.
- Libraries:
  - The CHGame library (`<CHGame.h>`, `platform/board/arduino/CHGame/libraries/CHGame`) gives the input core, the palette, the rounded rects and `panel()`, `remapRect`, `sprite4` spans (court and face art are converted in `tools/assets.py`), dither, the 3x5 font/`text35`, the Mask banners, `fx::` easing, sine, randomness and the shake, and the `fmt*` number formatting. The particles, banners and floating texts are the library's `chgame/Sizzle` too (since later on 2026-10-02): `Fx.h` sets its switches and `Fx.cpp` compiles it; the release image is byte for byte the same.
  - From CHGfx 1.3 directly: ellipses and `copyRow`.
  - The shake is the library's `fx::applyShake()` without a fill: the rows and columns the move uncovers shift in place. The earlier `gfx_scroll` shake left them as they were; that edge is the only pixel difference (`sc_double_bust`'s `h_bust`).
- Generated files, don't hand-edit:
  - `src/assets/Assets.cpp` comes from `python tools/assets.py`. The first run clones PPOT's repository into `tools/.cache/ppot` (gitignored), pinned to a commit, so it needs git and network. `tools/art/common/dealer.png` (the shared copy, at the repository root) must use palette colours only.
  - `src/audio/Music.cpp` comes from `python tools/make_music.py` (the songs; the composer is the repository's `tools/music/composer.py`).
- Debug builds:
  - Every `CHGAME_DEBUG` build, the simulator included, has no music scores (`Music.cpp` is under `#if !CHGAME_DEBUG`). Listen with `chgame audio out/audio` or a release build.
  - Device debug builds (`CHBJ_LEAN`) also drop the credits page; `-DCHBJ_FULL` forces it back in.
  - Announce device uploads, and put a release build back afterwards: a debug build looks like a game without its music.
- Profiling:
  - chdrive's `prof` sends the protocol's `T`, which answers only in a build made with `CHGAME_PROFILE=1` (the CHGame library's protocol, `chgame/Debug.h`; without it `T` gets `ERR` and `prof` waits for ever). In the simulator, use `chdrive.py --sim . -D CHGAME_PROFILE=1 ...`; `device.py` has no switch for it.
  - The simulator's PERF line reports `pcrnd`, the host-measured render time.
  - Scripts: `prof.txt`, `prof_hand.txt`, `perf_free.txt` and `pace.txt` (device).
- Saves use magic "CHBJ" in pages shared with every other CHGame game, kept by the CHGame library (`chgame/Save.cpp`). The record (`Save.cpp`) is byte for byte the one from before the library: the old header's u16 version 1 reads as the library's version 1 with flag 0, and "a game in progress" stays in the data. A build that grows into the pages falls back to one page, then to none (Stats says SAVING UNAVAILABLE).
- Shared tools: the simulator is `../../../../../../../../../tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).

## How it fits

- **Flash is the wall.** Without LTO the game is about 50 KB: roughly core + USB 5.1 KB, CHGfx 8.7 KB, rules and saving 5.8 KB, presentation (table, cards, button bar, effects, palette and the game's own drawing) 17.1 KB, screens 7.4 KB, art 3 KB, sound and music 2.9 KB. Getting there meant building everything at -Os, dropping `snprintf`, replacing `pinMode` with register writes, writing a 1.8 KB sound sequencer (now the CHGame library's `chgame/Audio`) instead of the 6.5 KB CHGameSound library, and storing dealer expressions as pixel edits.
- **Build settings and the save pages.** The release is *Smallest + LTO* with USB *Upload only* (the game never uses Serial, and Upload still works without touching the board). With both IDE menus left at their defaults (no LTO, USB Serial) the image still ends before the two save pages, but by only 60 bytes; *Upload only* alone leaves about 0.7 KB and *Smallest + LTO* alone about 3.9 KB. A build that grows into the pages saves to the one page left, so a power cut during a save can lose it, and one that leaves neither saves nothing (Stats says SAVING UNAVAILABLE). The board package (0.3.0 on) carries CHGfx and the CHGame library; `tools/device.py` passes the repository's copies itself.
- **Code runs from flash with 3 wait states**, so a function call per pixel costs 2-3 us. Hot loops (glyphs, spans, remapped sprites) run from SRAM, and the play screen redraws only the bands (wall, felt, button bar) whose content changed or that something moving touched. Every frame is still flushed, so palette effects (the rainbow BLACKJACK!, pulsing highlights, fades) cost nothing. Gameplay holds 60 fps.
- **Logic runs at a fixed 60 Hz.** If drawing falls behind, the loop catches up with extra logic ticks, so the game never slows down.
- **Saving without EEPROM.** The bootloader erases only the pages a new sketch occupies, so the two pages below its metadata page (0xF500, 0xF600) survive re-uploads. Records carry a sequence number and CRC and alternate between the two pages (the CHGame library's `chgame/Save.h`).
- **Music on one pin.** A piezo plays one note at a time, so the scores have two renderings: Lead (the melody; other voices only fill its rests) and Arpeggio (voices take 6 ms turns). Notes change pitch at the end of a wave cycle rather than restarting the timer, which would click.

Files:

    CHBlackjack.ino         loop: logic ticks, then draw, then DMA flush; the debug hook (R, D, J)
    config.h                build switches
    Round.*                 the rules and PPOT's ViewState flow (no graphics)
    Presenter.*             events -> motion; band-level redraw
    Fx.*                    the library's chgame/Sizzle (particles, banners, floating text), configured here
    Table.*, CardArt.*, Bar.*, Layout.h   table, cards and chips, action bar, layout
    Screens.*               splash, title, play, options, stats, credits, win, lose
    Sounds.*                sound effects and the music player
    Save.*                  what a save holds (the CHGame library keeps it in flash)
    src/audio/Music.*       generated music scores (tools/make_music.py)
    src/assets/*            generated art (tools/assets.py)

## Development

Everything except timing and sound can be checked on a PC (Python 3 with `pip install -r ../../../../../../../../../tools/requirements.txt`, and a C++ compiler for the host builds: root CLAUDE.md). `CHSIM_CHGFX` can point the simulator at another CHGfx `src/` folder.

    chgame test     # the rules: hand values, dealer policies, every payout, PPOT's bug regressions, a 16,000-hand fuzz
    chgame sim
    chgame run tools/scripts/sc_split.txt out/sc_split
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    python tools/make_music.py          # the scores -> src/audio/Music.cpp
    chgame audio out/audio    # every tune and effect to WAV, from the real sequencer code
    chgame upload [--debug]            # build and upload
    chgame run --device tools/scripts/sc_split.txt out/   # the same script on the board, in lockstep
    chgame size --top 20

- `chgame check` runs the host tests, every script twice (identical pictures) and the device build.
- Scripts: `say J <T|P|W|L|O|S|C>` jumps to a screen (P is a new game on a fresh table), `say D 26,8,25,46` stacks the deck with the next cards to be dealt, `say R 7` seeds the shoe; `snap`, `gif` and `rec` take pictures. `chdrive.py` flags drawing into the framebuffer while a flush is still converting it.
- `gameplay.txt` records the README's clips (title, a blackjack, split and double, a bust, the win screen). `showcase.txt` and the `sc_*.txt` scripts are tests of the same scenes; `pace.txt` checks real-time frame pacing on the device (about 300 frames per 5 s, no late frames).
- A `--debug` device build keeps USB Serial and adds the library's serial protocol (`chgame/Debug.h`); it leaves out the music and the credits page (see Gotchas).
- `tools/assets.py` rebuilds `src/assets/` from Press Play On Tape's art (cloned into `tools/.cache/`, pinned to a commit) and the hand-drawn pieces, `dealer.png` and the card art (`court`, `pip9`, `pip13`, `ranks`), all in the shared `tools/art/common/` at the repository root (a file of the same name in this game's `tools/art/` would override it).
