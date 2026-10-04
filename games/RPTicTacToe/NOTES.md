# CHTicTacToe — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHTicTacToe at commit db8274c (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 49,333 of 50,944 B (1,611 spare); the image is 49,712 B, 720 B under the 50,432 B line that keeps both save pages; static RAM 14,580 of 18,416 B (3,836 spare). (Earlier that day the library's sound engine saved 316 B over the game's own sequencer, and its shared core 688 B over the game's own copies, which had left 80 B.) Trust check_size over any older figure.
- Verification: simulator and host tests only, as of 2026-10-01 (not re-run since the import): `chgame test` (rules, dealer, match flow); scripts smoke, endings, save, iso, hover, perf, showcase, gameplay all deterministic with no BUG lines; `chgame redraw` on tools/scripts/diff/diff_iso.txt with 0 stale frames. `chgame check` now runs all of these plus the release build.
- Never run on a CHGame: frame times (simulator estimates: full iso frame ~7 ms, glove move ~5-6 ms), the dealer's thinking time and every sound are unchecked.

## Design decisions

- Chosen (owner): title "TIC TAC TOE: ROYALE"; over the top, funny, tongue in cheek, the casino vibe. Played for money: purse, stake, odds per table, a draw is a push, streaks, broke/goal.
- Chosen (owner): "99 squares" means both ULTIMATE (9x9 of boards) and THE 99 (a literal 11x9, five in a row). As many rule sets as possible, each explained in game (pause menu; SELECT on tables without an iso view).
- Built beyond the plan's 12 tables, not yet confirmed by the owner: ALL X (notakto), DARK (phantom), WRAP (5x5 torus), MINES, DROP 4 (7x6 gravity), and 2 PLAYERS hot-seat (score only; no BLITZ/DARK/AUCTION).
- Chosen (owner): depth and physicality in CHChess's isometric style, with a top-down map as the strategy view. Iso applies to square 3x3 and 5x5 boards without ULTIMATE/gravity (`iso::fits`, Iso.cpp); DROP 4, ULTIMATE and THE 99 stay flat. SELECT toggles iso/map and it stays as left.
- Chosen (owner): flat fields use dithering and more shades, tastefully. The flat map is the iso board seen from above: WOOD frame, GOLD trim, WINE edges, INK dither shadow, recessed FELT pads (FELT_DK top/left, FELT_LT bottom/right), gold inlay when cells are >= 14 px, spotlit felt.
- Chosen (owner): X/O art 2 px smaller each way (`SHRINK` in tools/pieces.py). A held piece over a placed one must not look like it clips through it: it rises to max(`HOLD` 14, the piece's height + 4) (Stage.cpp), the piece under it is drawn with `RM_SHADE`/`RM_BLUESHADE` and no felt shadow over it, and the drop starts from that height.
- Chosen (fourth pass, pushed): held X/O on 3x3 iso tables spin (X_L1/O_L1 at 45 degrees, X_L2/O_L2 at 90, the rest mirrored via sprite4's `SPR_FLIP_H`; a step every 4 ticks).
- Chosen, not yet reviewed by the owner: when a held piece would leave the top of the screen the board glides down (`camY`/`camT` in Stage.cpp) and back on the dealer's turn.
- Removed at the owner's request: the decorative poker chips (iso stake stacks, title stacks, tables-room stake chip). Kept: GOBBLE and AUCTION chips, which are game pieces.
- Removed for flash (the owner allowed it if space was needed): the TOWER table, leaving 16 tables; save `VERSION` 2 in Save.cpp.
- No music, only a title sting: there is no flash for a score.
- Sound: the CHGame library's engine (chgame/Audio.h); the effect tables are Sounds.cpp (Tick and Tock `audio::SOFT`). The SOUND option (`opt.sound`: 0 on, 1 off) maps to `audio::begin(SOUNDS, COUNT, !opt.sound)` / `audio::setOn`. `chgame audio out/audio` renders them to WAV. The old engine's 800 ms last step of BROKE is two 400 ms sweeps (a step holds at most 510 ms).

## Open items

- The owner's verdict on the iso look and on the extra tables.
- Device run: iso frame times (full and band redraws), dealer time on the big felts (12 cells a frame), every sound. Device debug builds are `CHTT_LEAN` (no saving, plain end-screen lettering, no particles: `SIZZLE_NO_PARTICLES` in `Fx.h`; their Stats page says SAVING UNAVAILABLE: it asks `!CHTT_LEAN && save::available()`). With board package 0.3.0 they were 340 B over the flash until the particles went (2026-10-02); now 49,964 B. `-DCHTT_FULL` does not fit either (52,512 B then). Put the release build back afterwards.
- Logo touch-up: tools/art/logo.txt and royale.txt (drafted by tools/make_logo.py from Arial Black and Georgia; the .txt files are the source).
- Optional music: impossible without cuts elsewhere.

## Gotchas

- Flash is effectively full. Earlier squeezes: `gfx_ellipse` dropped (fills drawn as pairs), only the bounce curve kept (`fx::bounce`, now the CHGame library's), unused banner styles cut. Nothing fails when the image passes 50,432 B: read check_size's "save pages free: N" line after every build (one page: saving loses its power-cut safety; none: saving switches off).
- LTO inlines almost everything into `stage::render`, so the symbol table does not show what a feature costs: measure by building a patched copy with and without it.
- Band redraw: when only the glove or cursor moved, Stage redraws just the rows they swept, inside CHGfx's clip rectangle (`gfx_setClip` in `stage::render`, Stage.cpp), which the CHGame library's primitives and CHGfx's both honour (the library's masks do not). New play-screen drawing must respect that clip. Check with `chgame redraw tools/scripts/diff/diff_iso.txt out/diff 1` (0 stale frames expected): its reference build defines `CHSIM_FORCE_FULL`, which turns every frame with motion into a full redraw (`stage::render`, Stage.cpp).
- Palette cycling (the `FX_A`/`FX_B` slots, the CHGame library's `pal::`, platform/board/arduino/CHGame/libraries/CHGame/src/chgame/Palette.cpp) animates the cursor, the fading VANISH mark and the winning line with no redraw; static art drawn in those slots will flicker with them.
- Iso pieces: tools/pieces.py ray-marches signed-distance models (CHChess's renderer) into tools/art/pieces/*.png + .anchor (L for 3x3, S for 5x5, L1/L2 spin frames). Re-running overwrites hand touch-ups. Then `python tools/assets.py`.
- tools/assets.py requires the dealer, faces and PPOT end lettering to come out byte-identical to ../CHBlackjack's src/assets/Assets.cpp and the glove to ../CHChess's; it stops with an error if they differ. Change shared art in the sibling first.
- Iso D-pad: the nearest cell in the pressed screen direction, scored `along + 3 * |perp|` (Match.cpp).
- Debug hooks (CHTicTacToe.ino): `R seed`, `J <T|G|P|W|L|O|S> [table]`, `C cell [arg]`, `H cell` (the dealer's next move), `M purse`, `D 0..2` dealer level, `V ticks` BLITZ clock, `E 1|0` (a non-lean device debug build writes saves only after `E 1`), `Q` simulator calibration.
- The README's one GIF, docs/gameplay.gif, is made by `chgame gif` from tools/scripts/gameplay.txt (clips 01_title, 02_classic, 03_vanish, 04_ultimate, 05_cat in out/gameplay). showcase.txt is kept as a test and records its clips into the folder it is given (use out/showcase, not docs/).
- Simulator: `chgame sim` (the repository's `tools/chsim`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `chgame size` (`chgame build` runs it).

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the host builds: root CLAUDE.md). `chgame check` runs it all.

    chgame test [table]   # the rules, the dealer, the match flow
    chgame sim
    chgame run tools/scripts/smoke.txt out/smoke
    chgame gif          # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    chgame redraw tools/scripts/diff/diff_iso.txt out/diff 1   # band redraws against full ones: 0 stale frames
    python tools/assets.py                    # tools/art -> src/assets
    python tools/make_logo.py                 # redrafts the title lettering
    python tools/pieces.py                    # re-renders the iso pieces (overwrites the PNGs)
    chgame build|upload [--debug]
    chgame size
    chgame audio out/audio   # the effects as WAV files

- `chgame test table` prints the dealer's results against a random player at every table and level.
- Scripts (tools/scripts): `smoke` (every screen and table), `endings`, `save`, `iso`, `hover`, `perf`, `showcase` (a clip per feature), `gameplay` (the README GIF), `diff/diff_iso` (for `chgame redraw`). They use the common commands (`wait`, `tap`, `snap`, `rec`, `gif`, `say`, `perf`, `cal`) and the debug hooks listed under Gotchas: `say J P 4` jumps to table 5's play screen, `say H 8` fixes the dealer's next move, `say C 4` plays a cell.
- In the Arduino IDE: *Tools > Optimize > Smallest + LTO* and *Tools > USB > Upload only* (the game has no use for USB Serial); board package 0.3.0 brings CHGfx and the CHGame library.
- Saving: options, statistics and the run (purse, table, stake, streak) in the last two flash pages, every five games and on leaving.
- The dealer: on 3x3 tables a depth-limited search of the real rules; on the big felts every empty cell is scored by the lines it could still make or break, 12 cells a frame.
- The iso view is about 6 KB of the image. One board type (up to 99 cells, a line length, rule flags) and one line scanner serve every table.
- The pieces are quantised to the palette by tools/pieces.py at the board's 30 degree camera, in two sizes; the PNGs in tools/art/pieces can be touched up by hand.

Files:

    CHTicTacToe.ino   setup, the frame loop, debug commands
    rules             Rules (every table), Cpu (the dealer), Match (turns, toss, bids, clock), Text
    drawing           Stage (the play screen), Iso (the isometric tables), Table (the dealer's wall), ChipArt
    Screens           title, tables room, play, options, stats, win, broke
    Remap, Fx         sprite remaps; the library's chgame/Sizzle (particles,
                      banners, floating text), configured in Fx.h
    Sounds            the sound effects (the CHGame library plays them)
    Save              what a save holds (the CHGame library keeps it in flash)
    src/assets/       the art, generated by tools/assets.py
    tools/            assets, simulator, scripts, tests
