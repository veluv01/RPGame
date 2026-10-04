# CHRoulette — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHRoulette at commit d8c2f67 (2026-10-01); develop here now, not in the old repo.
- Release build (board package 0.3.0, 2026-10-02; CHGfx 1.3.0, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 49,465 of 50,944 B (1,479 spare), static RAM 16,072 of 18,416 B (2,344 spare).
- Save pages: the repository's `tools/check_size.py` reports the image as 49,836 B, 371 B more than the sections' flash figure. Both A/B pages (0xF500, 0xF600) fit while the image is at most 50,432 B, so only 596 B of headroom remain. Past that, the CHGame library's `chgame/Save.cpp` saves to page B only. Flash is the wall.
- Simulator-verified (as of 2026-10-01):
  - The game plays end to end: betting, whip, spin, payout, save/continue.
  - `chgame test` passes: rules against the independent `ref_roulette.py` model, navigation, limits, the spin flow, a money-conservation fuzz.
  - `python tools/tests/run_ball_tests.py` passes: about 1.2M cases, every pocket, wheel and pace.
  - An adversarial review found 13 integration bugs, and all are fixed.
  - `chgame redraw` reports 0 stale pixels.
- Device: never run on the board. Phase P7 (device bring-up) is pending.

## Design decisions

The design specs are in `docs/design/*.md` (written before the game; their paths and names were brought up to date on 2026-10-02, and they mark where something has since moved into the CHGame library). Where they conflict, `critique.md` decides; `architecture.md` has the phases P0-P8, the budget and the cut list. The owner overrode some spec recommendations, and the choices below win over the docs.

- Chosen: a European wheel by default, American in Options.
- Chosen: CHChess's glove on the felt layout:
  - a tap moves it half a cell (onto lines and corners), a held direction moves it whole cells;
  - taps wrap round the edges as in Chess, held runs stop at them.
- Chosen: CHBlackjack's dealer is the croupier.
- Chosen: the camera whips to a tilted wheel for the spin.
- Chosen: green felt only, with no felt themes. The zero, "0 GREEN" and the green banner rely on it.
- Chosen: winning stakes stay up, and only the winnings travel.
- Chosen: a new musette waltz as the title tune (over reusing CHBlackjack's). It was cut to 8 bars, about 304 B, to save flash; `tools/make_music.py` (the songs; the composer is the repository's `tools/music/composer.py`) writes `src/audio/Music.cpp`.
- Chosen: about 4 s of wheel time on FUN (shorter than the spec's 5 s). The tuning is the `PACE` table in `Ball.cpp`.
- Chosen: logo A, the chunky 1 bpp "Roulette" in `tools/art/logo.txt`, over the spec's recommended `title35` lettering.
- Plan defaults, never contested:
  - limits of $100 inside, $250 outside and $1,000 on the table;
  - no American 0/2 or 00/2 splits (their spots would sit 2.5 px apart).
- Cut to fit: the credits back room (`CHRL_CREDITS=0`; the credits are in the Options footer) and the attract demo (`CHRL_DEMO=0`).
- If more must go, follow `critique.md` §8.9 / `architecture.md` §4.4. Never cut the American wheel or the chip art.

## Open items

- P7 device bring-up, as `architecture.md` §5 describes:
  1. Announce the debug upload.
  2. Check pacing (`late=0`; the `P` max under 8.3 ms) on the worst frames: whip, landing, big win. Check stack use.
  3. Test save persistence across a re-upload only with the owner's permission.
  4. Upload the release build for the owner to play.
- Simulator render estimates after the incremental wheel and band-split redraws: 6-7.5 ms. They are unmeasured on the device; `tools/scripts/perf.txt` prints them.
- Deferred features: by the estimates in `config.h`, neither `CHRL_DEMO` (about 0.8 KB) nor `CHRL_CREDITS` (about 1.2 KB) fits in the 596 B margin while both save pages are kept.

## Gotchas

- Every `CHGAME_DEBUG` build has no music scores (`Music.cpp`), and that includes the simulator; its `playSong()` is empty, so the library's score player is left out too (about 0.5 KB). Audition the tunes with `chgame audio out/audio` (the shared preview: the CHGame library's engine with `Sounds.cpp` and `src/audio/Music.cpp`).
- Device debug builds (`CHRL_LEAN`) also drop the credits page, use `title35` lettering on the win/broke screens instead of PPOT's bitmaps, and have no particles (`SIZZLE_NO_PARTICLES` in `Fx.h`; with board package 0.3.0 the build was 128 B over until those went, 2026-10-02: now 49,692 B). `-DCHRL_FULL` forces a full device debug build.
- Device debug builds write flash only after a script sends `say E 1`, because the save pages are shared with the release build and the other games.
  - `architecture.md` §1.2 calls this hook `Y`; the code uses `E`.
  - The other hooks are in `CHRoulette.ino`: R seed, F force pockets (37 = 00), J screen, G glove, W bet, M purse. `Q` (calibration, simulator only) is the CHGame library's debug protocol command, not a hook.
- The redraw check (`chgame redraw`) takes its scripts from `tools/scripts/diff/`; `chgame check` runs them all. Rerun it after touching any band redraw.
- Tools that read sibling games in `../` (`examples/Games/`):
  - `tools/assets.py` checks that the dealer, faces, end-screen lettering and glove come out byte-identical to `../CHBlackjack` and `../CHChess` `src/assets/Assets.cpp`. If a sibling is missing it prints "unchecked" instead of failing.
  - `tools/logo_preview.py` reads `../CHBlackjack`'s `src/assets/Assets.cpp`. The mock-up kit is the repository's `tools/pixkit.py` (shared with CHWordWheel); it takes the dealer and the glove from the shared `tools/art/common/`.
- The ball:
  - The rules pick the number at SPIN.
  - The solver dry-runs the spin and turns the rotor by whole pockets so the ball lands there.
  - After any change to `Ball.cpp` or `PACE`, rerun `run_ball_tests.py`, which checks every pocket lands within its time window. `tools/spin_preview.py` traces one spin to a GIF.
- The wheel ring's map comes from `tools/wheel.py` (`src/assets/WheelMap.*`).
- `gfx_chunkScratch()` (1 KB) is shared by three users: the wheel's per-frame colour table (`WheelArt.cpp`), Mask lettering (the CHGame library's `chgame/Mask.cpp`) and `save::store()`. Only one may hold it at a time, and only between `gfx_wait()` and the next flush.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).

## Development

Everything can be checked on a PC (Python 3 with `pip install -e .[sim]` in the repository root, and a C++ compiler for the simulator and tests: root CLAUDE.md).

    chgame test          # the rules, navigation, limits, spin flow, saving, a 16,000-spin money fuzz
    python tools/tests/run_ball_tests.py     # the ball lands on the chosen pocket: every pocket, wheel, pace, many seeds
    chgame sim
    chgame run tools/scripts/smoke.txt out/smoke
    chgame gif         # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    chgame redraw tools/scripts/diff/diff_soak.txt out/d 3
    python tools/assets.py                   # art in tools/art -> src/assets
    python tools/wheel.py                    # the wheel's map (src/assets/WheelMap.*) and previews
    python tools/make_music.py               # the tunes -> src/audio/Music.cpp
    chgame audio out/audio
    chgame upload [--debug]  # build and upload (--debug adds the CHGame library's serial protocol, chgame/Debug.h)

- The build needs link-time optimisation to fit: `opt=oslto` and `usb=uploadonly` (in the IDE, *Tools > Optimize > Smallest + LTO* and *Tools > USB > Upload only*). `chgame build` does it; by hand, `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly` with CHGfx and the CHGame library from `platform/board/arduino/CHGame/libraries/`.
- Scripts (`tools/scripts/*.txt`): `say F <n>` forces the next number (37 = 00), `say W <spot> <amount>` places a bet, `say G <spot>` moves the glove (165 is SPIN), `say J <T|P|W|L|O|S|C>` jumps to a screen, `say M <amount>` sets the purse, `say R <seed>` reseeds. `gameplay.txt` records the README's clips; `showcase.txt`, `smoke.txt`, `spin_forced.txt`, `american.txt`, `bet_tour.txt` and `save_continue.txt` are tests to look at; `perf.txt` estimates the device's render times.
- `chgame redraw` runs a script on the game and on a build that redraws everything every frame, and reports any pixel the incremental redraws left stale.
- `tools/mockup.py` drew the design mockups (`docs/design/` has the specs).

How it fits:

- The wheel is a tilted bowl of stacked ellipses with the rotor painted from a polar angle map: one quadrant, a byte a pixel (515 bytes), mirrored four ways through a 1 KB colour table built each frame for the rotor's angle. The pockets, the frets and the lit winning pocket all come out of that table, so the ring costs a map read and a table read a pixel, from SRAM. The bowl, which never moves, is repainted only in the rows the ball and the sparks passed through.
- The wall, the plaque, the layout and the action bar each redraw only when what they show changes or something moving touches them; every frame is still sent to the panel, so the palette effects (the rainbow banners, the pulsing highlights) run for free.
- Saving: the two flash pages below the bootloader's metadata survive re-uploads; records alternate between them with a sequence number and a CRC.

Files:

    CHRoulette.ino          loop: logic ticks, then draw, then DMA flush
    config.h                build switches
    Roulette.*, Spots.*,    the rules (Roulette), the betting spots, the glove's
    Nav.*, Wheel.*          navigation, the wheels' orders - no graphics, host-tested
    Ball.*, WheelArt.*      the ball and its solver; the wheel's drawing
    Presenter.*, Fx.*       the presenter (events -> motion); the library's chgame/Sizzle configured in Fx.h
    Table.*, Felt.*,        the wall and croupier, the felt layout, chips, the action bar;
    ChipArt.*, Bar.*,       every coordinate in Layout.h
    Layout.h
    Remap.*                 the glove's colour remaps
    Screens.*               title, play, options, stats, won, broke
    Sounds.*                the effects and the music player
    Save.*                  what a save holds (the CHGame library keeps it in flash)
    src/audio/Music.*       the music's scores (generated by tools/make_music.py)
    src/assets/             generated art and the wheel's map
