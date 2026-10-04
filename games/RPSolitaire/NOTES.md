# CHSolitaire — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the commands under Development below, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHSolitaire at commit 38d0309 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 30,562 of 50,944 B (20,382 spare; the image is 30,904 B, so both save pages fit with ~19.5 KB to go), static RAM 16,188 of 18,416 B (2,228 spare). RAM, not flash, is the tight budget in this game.
- Verification: simulator and host tests only. `chgame check` (host tests, every script in tools/scripts run twice with identical frames and every `expect` holding, release build) passed as of 2026-10-01; not re-run since the import.
- Never run on a CHGame: pace, frame rate, sound and card legibility on the real LCD are unchecked. The ~5 ms full-table / <1 ms cascade frame figures are simulator estimates scaled by the CHGfx benchmark (chdrive `cal`, sim hook `Q`).

## Design decisions

- Chosen: copy the Solitaire that came with Windows as closely as possible: rules, Standard/Vegas/None scoring with its numbers, timed game and bonus, draw one/three, one-level undo. The win cascade (cards bounce off the foundations and leave their trail) is the most important feature.
- Chosen: keep the casino series' glove; card backs in the Windows spirit with a choice (12 on the DECK screen: 2 weaves in code, the rest art; robot, castle, island, fish, lucky 7 and chip have small animated gags).
- Chosen (owner, second pass): the glove points UP from under the pile's top card, fingertip 2 px onto its bottom edge; the carried run rides on the fingertip. Rejected: finger resting on the selected card from above (it read as pointing at the stock).
- Chosen (owner, second pass): white space above the rank (glyphs at y+2), face-up overlap 9 px (`UP_PITCH`, Layout.h).
- Chosen (owner, second pass): no cascade behind the title menu (it was there in the first build); instead cards drift down the felt and flip, with a meteor now and then, as CHMahjong's tiles do.
- Chosen (owner, third pass): empty glove bobs 1 px slowly; a card put down on a column gives a light grey/white dust ring (CHChess's puff, toned down); carried cards get the rainbow (FX_A) border; the title menu is one row of CHBlackjack-style accordion buttons in the small font (PLAY light green hovered / dark green not; the others gold hovered / grey not).
- Approved only as part of the plan, no verdict yet:
  - 17x23 card (seven columns force it; no big card anywhere).
  - A pick/put, double-tap A to send to the foundation, B put back / deal, SELECT undo, START pause.
  - The Vegas bank carries over between games and is saved.
  - No felt themes (FELT_LT is the only green, and the card-back art uses it).
  - Title lettering drawn from Georgia Bold Italic.

## Open items

- Device run: pace, sound by ear, card legibility, real frame times. A device debug build (`chgame upload --debug`) keeps saving unless built with `-DCHSO_LEAN=1`, so it writes the shared save pages like the release; put the release build back afterwards. (A LEAN build's Stats page says "SAVING UNAVAILABLE": it asks `!CHSO_LEAN && save::available()`.)
- The owner's art pass on the card backs (tools/art/backs/*.txt, 15x21 in palette letters; a palette-exact PNG of the same name overrides one).
- The owner's verdict on the plan-level choices listed above.
- Fixed 2026-10-01 (with the SD game menu, which makes switching games routine): the save magic, the debug handshake id and the macro prefix used to be CHSlots' (`0x4C534843` "CHSL", `CHSL_`). They are now `0x4F534843` "CHSO", handshake "CHSO" (tools/chsim/chdrive.py `--id` default) and `CHSO_` (CHSO_VERSION etc.; the debug protocol's switch is now the CHGame library's `CHGAME_DEBUG`). A save written by an older build is ignored once.

## Gotchas

- RAM: the title's top rail (the outlined lettering costs ~5 ms to draw) is cached as 31 framebuffer rows, 1,984 B of static RAM (`rail` in Screens.cpp). It is the first thing to give back if RAM runs short.
- The cascade depends on the table NOT being redrawn: each logic tick stamps the bouncing card into the framebuffer (up to three stamps when catching up). Anything that invalidates the table mid-cascade wipes the trail.
- The whole game state (`Klondike` in Klondike.h) plus options and stats must fit one 256 B flash page: `static_assert` in Save.cpp. Undo is a copy of that struct, so growing it costs RAM twice.
- Debug hooks beyond the ones under Development (G, W, O, C), in `debugHook` in Screens.cpp: `$ n` Vegas bank, `J <T|P|D|O|S>` jump to a screen, `H` the table as one line (what chdrive's `expect KEY=VALUE` and `waitstate` read); simulator only: `X` the tallest possible column, `Z` a power cycle (reloads the save), `Q` timing calibration.
- tools/scripts/gameplay.txt (the README's GIF) plays a real deal (`say G 1`) whose moves were worked out by the host tests' sensible player and written out as glove moves (`say C pile depth`); the one-off generator is not in the repo. Any change to dealing or the RNG invalidates the move list.
- The README's GIF is made by `chgame gif`, which runs gameplay.txt into out/gameplay and joins its clips. showcase.txt is a test like the other scripts now; run it into out/, not docs/.
- tools/make_logo.py renders the title once from a TrueType font (Georgia Bold Italic by default; pass another path as the first argument). tools/art/logo.txt is the source from then on; re-running overwrites hand edits. Then `python tools/assets.py`.
- The card face (ranks, pips, court busts, suit glyphs: the shared tools/art/common/ at the repository root, CHBlackjack's and CHPoker's files) and the 3x5 font come from CHBlackjack/CHPoker; assets.py does not cross-check them against those siblings.
- Simulator: `chgame sim` (the repository's `tools/chsim`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `chgame size` (`chgame build` runs it).

## Development

Everything can be checked on a PC (Python 3 with `pip install -r tools/requirements.txt` from the repository root, and a C++ compiler for the host builds: zig, clang++ or g++ on the PATH, `pip install ziglang`, or `CHSIM_CXX="path/to/zig c++"`; root CLAUDE.md).

    chgame check               # host tests, every script twice (identical frames), device compile + size
    chgame test     # the rules, both scorings, the stock and its passes, the clock and bonus, thousands of random and sensible games
    chgame sim
    chgame run tools/scripts/play.txt out/play
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art -> src/assets
    chgame audio out/audio    # the sound effects as WAV
    chgame upload [--debug]            # build and upload
    chgame size --top 20

- Scripts: `say G <seed>` deals a known game, `say W <n>` leaves n cards to play, `say O <i> <v>` sets an option, `say C <pile> <cards>` puts the glove on a pile; `expect KEY=VALUE` checks the table's numbers, `waitstate S` runs until the table is in a state; `snap` and `rec` take pictures. The host tests' thousands of games must leave all 52 cards in place.
- gameplay.txt records four clips: 01_title, 02_deal (the deal and first moves of `say G 1`), 03_runs (later in the same game, which plays on unrecorded in between) and 04_win (`say G 11`, `say W 16`: the game plays itself out and the cascade).
- `--debug` adds the CHGame library's serial protocol (`chgame/Debug.h`) for screenshots, injected input and lockstep; `chgame run --device SCRIPT OUTDIR` runs a script on the board.
- Build: `CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly` (in the IDE: *Optimize > Smallest + LTO*, *USB > Upload only*; the game has no use for USB Serial). `chgame build` does it and prints the size.
- Art: the cards (`ranks.txt`, `suits.txt`, `pip9.txt`, `court.txt`) and the glove (`hand.*`) are the shared ones in the repository's `tools/art/common/`; this game's `tools/art` holds the title lettering (`logo.txt`, as `#` and `.`) and the card backs (`backs/*.txt`, 15x21 in palette letters; a PNG of the same name in the game's 16 colours overrides one).
- The card: seven columns in 128 pixels leave 18 a column, so it is 17x23, with CHBlackjack's bold rank and Press Play On Tape's suit glyph side by side along the top and a pip or a court card's bust below. The tallest column there can be (six face down, king to ace on top) runs over the status line with the top of every rank still showing.
- The cascade: kings leave first, round the four foundations, as Windows did it; each card takes a random sideways speed and loses a fifth of its bounce every time it hits the floor.
- The whole game is 196 bytes (the stock and the waste share one 24-card array).

Files:

    CHSolitaire.ino        loop: logic ticks, then draw, then DMA flush
    config.h               build switches
    Klondike.*             the rules and the scoring (no graphics)
    Stage.*                the glove, cards in motion, the table, the cascade
    CardArt.*, Layout.h    the card and its backs, layout
    Screens.*              title, play, deck, options, stats
    Fx.*                   the library's chgame/Sizzle (particles, banners, floating texts), configured in Fx.h
    Sounds.*               the sound effects (the CHGame library plays them)
    Save.*                 what a save holds (the CHGame library keeps it in flash)
    src/assets/            the art (generated by tools/assets.py)
    tools/                 tests, asset pipeline, script driver, device tools
