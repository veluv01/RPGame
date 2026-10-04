# CHBingo — development notes

Agent-facing notes for continuing work on this game. Rules and controls are in [README.md](README.md), whose format is [docs/game-readme.md](../../../../../../../../../docs/game-readme.md); the platform is covered by the repo-root [CLAUDE.md](../../../../../../../../../CLAUDE.md) and [docs/](../../../../../../../../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBingo at commit dd6295b (2026-10-01). The public history was squashed to that single commit. Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 36,047 of 50,944 B (14,897 spare), static RAM 14,912 of 18,416 B (3,504 spare).
- Save pages: the repository's `tools/check_size.py` reports the image as 36,388 B, so both A/B save pages fit with about 14 KB to spare. This game has real flash room.
- Verification as of 2026-10-01: simulator only. These all passed:
  - `chgame test`: rules, races against the hall, money, power-ups, jackpot odds, round set-up against `tools/tests/ref_bingo.py`; about 1.24M checks.
  - `tools/tests/sim_save.py`: save mid-round, power-cycle, continue.
  - `chgame redraw` on `tools/scripts/diff/diff_soak.txt`: 0 stale pixels.
- `chgame check` runs the host tests, every script twice, the redraw check, the save test and the device build.
- It has never run on the board. Render times, pacing, sound and saving on hardware are unmeasured.

## Design decisions

Made by the owner:
- The dealer (CHBlackjack's PPOT dealer) announces every ball in his speech bubble.
- The win banner is a rainbow "BINGO!". Rarely it says "IT'S A BINGO!" instead, and the dealer's bubble corrects it: "You just say bingo....".
- Up to 9 cards, each bought separately. Every card needs its own daub: move to it and press A.
- Only one and a half cards are visible, so play is swiping. A card holding a called, undaubed number gets a rainbow outline.
- The player races a hall of rivals; the twists are power-ups and a progressive jackpot.
- Depth everywhere: shaded panels, cards, daubs and pips, plus drop shadows.
  - Panels are layers of one rounded shape: the shadow follows the corners, the outline is one colour and the rims follow the curve (`panelLit()` in the CHGame library, `platform/board/arduino/CHGame/libraries/CHGame/src/chgame/Draw.cpp`).
  - The owner dislikes jagged edges and shadows that ignore the contour.
  - Ball shadows are a single shrinking line.
- Title:
  - "Bingo" in CHBlackjack-style lettering (`tools/art/logo.txt`: B from Blackjack, o from Roulette).
  - Rejected: chips on the title, and a spotlight at the top.
  - The menu sits on a FELT_DK dither band at a pitch of 12.
  - The menu glove is CHChess's glove pointing right (`HAND_R`), thumb on top, cuffed in the dauber colour.
- Title balls (the owner loves them; tuned over four rounds, see `titleBalls()` in `Screens.cpp`):
  - A continuous stream spelling BINGO travels right to left with B leading. "Left to right" was a slip in the owner's earlier request.
  - All balls are the same size and bounce at one rate and height (`BOUNCE` 48 ticks), each a beat behind the ball ahead.
  - Each ball has its own slow ±3 px swing, which can never make one overtake another (`PITCH` 27).
- The DAUBER option (red/blue/green/cyan/peach) colours the daubs, their splat and the glove's cuff.
- A daub is CHChess's DUST puff, plus GOO "gack" particles (a Nickelodeon feel), plus a small shake.
- After a daub the glove holds on the cell with a press-and-kick `RECOIL` curve (22 ticks, `Presenter.cpp`), then slides on. A swipe cancels the hold.

## Open items

- Device run: render profile, pacing, sound by ear, saving.
- Owner feedback on the overall look and feel.
- Defaults the implementer chose, not yet confirmed by the owner:
  - The view shows the card in play plus the first three columns of the next card, as a wrapping carousel, with a pip per card (rainbow when a number is waiting).
  - A daubs every waiting number on the card in play. Completing a line is the call. A press with nothing waiting resets the streak.
  - The hall has 8/20/40 rival cards. The pot is 90% of the hall's buy-in. A tie goes to the player.
  - A call every 2 s (1.3 s on FAST, 3 s on SLOW).
  - The rare banner comes 1 win in 10 (`RARE_ONE_IN`). The jackpot pays within 10 calls (`JACKPOT_CALLS`).
  - The power-ups are WILD, FREEZE and 2X POT.
  - The caller has nicknames for 13 numbers (`LINGO` in `Presenter.cpp`).
  - No music, and no win screen: play is endless until broke.

## Gotchas

- The skeleton was forked from CHRoulette (wheel, glove and music removed): macro prefix `CHBN_`, save magic "CHBN", and the protocol handshake is `CHBN`.
- A saved round is just its seed plus the daubs; the draw, the cards and the hall are dealt again from the seed. Any change to dealing order or random-number use breaks saved rounds, so bump `VERSION` in `Save.cpp`. The save pages are shared with every other CHGame game.
- The hall costs no RAM: each rival card is reduced to the call on which it completes, and only the earliest call is kept.
- Redraws are incremental, by band (wall, plaque, felt, bar).
  - `-DCHSIM_FORCE_FULL` (`Presenter.cpp`; `chgame redraw` sets it) forces full redraws.
  - After touching render or presenter code, run `chgame redraw tools/scripts/diff/diff_soak.txt out/diff`.
  - The title draws its felt and logo once (`titleReady`) and redraws only the ball band and the menu.
- Rainbow outlines, pips and the winning line all use one cycling palette entry. Animate through the palette, not with redraws.
- Debug protocol:
  - The game's commands are listed at the top of `CHBingo.ino`; `Q` (calibration) is simulator-only.
  - Device debug builds do not write the save pages unless a script sends `say E 1`, because the pages are shared with the release and other games.
  - `CHBN_LEAN` (device debug) drops only the broke screen's lettering; saving stays in.
  - The protocol itself is the CHGame library's (`chgame/Debug.h`, on with `CHGAME_DEBUG`); the save record's pages and CRC are the library's too (`chgame/Save.cpp`), the game's `Save.*` says what goes in it.
- `python tools/chsim/autoplay.py out/autoplay.gif` is a buttons-only bot: it plays a round as a person would (reaction times, swiping toward the rainbow frames, power-ups in the quiet moments), searches for a seed the player wins, then records that seed. It made the README's GIF before the README had one reel of clips; that is `tools/scripts/gameplay.txt` now.
- Sound is the CHGame library's sequencer (`chgame/Audio.h`); the game's effect tables are `Sounds.*`. There is no music. Adding some costs flash (the library links its music code only for a game that calls `audio::music()`), but there is room: give the game generated scores and a `playSong()` in `Sounds.*`, which the shared preview (`chgame audio out/audio`) uses to render them. The SOUND option already asks for the lead rendering.
- `src/assets/Assets.cpp` is generated by `python tools/assets.py` from `tools/art/` (the logo) and the repository's shared `tools/art/common/` (dealer, faces, hand, broke lettering). Don't hand-edit it.
- Shared tools: the simulator is the repository's `tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).

## Development

Everything can be checked on a PC (Python 3 with `pip install -e .[sim]` in the repository root, and a C++ compiler for the simulator and tests: root CLAUDE.md).

    chgame test     # the rules (below)
    python tools/tests/sim_save.py      # in the simulator: save mid-round, power off and on, continue; options; going broke
    chgame sim
    chgame run tools/scripts/smoke.txt out/smoke
    chgame redraw tools/scripts/diff/diff_soak.txt out/diff   # incremental redraw against a full redraw, frame by frame
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # art in tools/art and tools/art/common -> src/assets/Assets.*
    chgame audio out/audio    # the sound effects as WAVs
    chgame build [--debug]                            # also upload, run --device SCRIPT OUTDIR, shot OUT.png
    chgame size     # flash and RAM from the map

- `chgame test` covers the lines, the cards, the race against the hall over thousands of rounds (a player who daubs everything wins exactly when one of their cards is first or level), the money, the power-ups, the buttons, saving a round, the jackpot's odds, and the round set-up against an independent Python model.
- Scripts (`tools/scripts`): `smoke.txt` is every screen and a round as screenshots, `perf.txt` estimates render times, `showcase.txt` records the buy-in, swiping, both banners and a rival's win as separate GIFs (a test now, written to its output folder, not `docs/`), `gameplay.txt` records the README's clips (`01_title` ... `06_rival`). `say R 21` seeds the round, `say J P` jumps to the buy-in, `say H n` sets the call a rival wins on, `say W` makes a bingo, `say I 1` forces the rare banner; the full list is at the top of `CHBingo.ino`. The same scripts run on the device with a debug build.
- The device build wants *Optimize: Smallest + LTO* and *USB: Upload only* (the game has no use for USB Serial); `chgame build` sets both.
- Render times estimated from the simulator: a slide across the cards about 7 ms a frame, a daub with its splat about 8 ms, the title about 5 ms. None measured on the board.
- Saving: the two flash pages below the bootloader's metadata survive re-uploads; records alternate between them with a sequence number and a CRC.

Files:

    CHBingo.ino          the frame loop and the debug commands
    config.h             build switches
    Bingo.*              the rules: no graphics, no sound, host-tested
    Presenter.*          events to motion: the caller, the carousel, the wins
    Fx.*                 the library's chgame/Sizzle configured (64 particles, GOO) plus gack()
    Table.*, Cards.*     the wall; the cards, the buy-in and the bar
    Layout.h             every coordinate of the play screen
    Screens.*            title, play, pause, options, stats, broke
    Sounds.*             the sound effects (the CHGame library's sequencer plays them)
    Save.*               what a save holds (the CHGame library keeps it in flash)
    src/assets/          generated art (tools/assets.py)
    tools/               simulator, tests, scripts and art tools
