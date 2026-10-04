# CHDominoes — development notes

Agent-facing notes for continuing work here; rules and controls are in README.md, the commands under Development below, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHDominoes at commit ddace41 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, board package 0.3.0, 2026-10-02): flash 42,543 of 50,944 B (8,401 spare), static RAM 16,864 of 18,416 B (1,552 spare). The image (42,888 B) leaves both A/B save pages free with ~7.5 KB to go: RAM is the tight budget here, not flash.
- On the CHGame library's debug protocol (`chgame/Debug.h`, `CHGAME_DEBUG`), flash save record (`chgame/Save.h`; `Save.cpp` says only what the record holds, byte for byte the old layout, magic "CHDM" version 1) and RAMFUNC since 2026-10-02; `tools/chsim/chdrive.py` is the shared `tools/chsim/chdrivelib.py` plus this game's `auto`, `round`, `board`, `waitturn`, `cal`, its `perf` and its GIF writer (`fbimage.save_gif`). Image 42,596 -> 42,736 B (the library's `audio::setOn()` out of line, about +18 B; its save code, which GCC splits so the `available()` check is inlined into both callers, about +120 B), static RAM 16,864 B unchanged; debug build 45,944 -> 46,196 B, RAM 17,076 -> 17,084 B. Frames unchanged.
- Verification: simulator only. `chgame check` passes: host tests (rules vs a naive reference, 20,000 matches laid out with no overlap, save/reload mid-round, CPU levels against each other), every script twice with identical frames, device compile and size.
- As of 2026-10-01 it has never run on the device: frame times unmeasured, sound unheard.

## Design decisions

- Rules: ALL FIVES and DRAW, chosen on the setup screen; double-six, two seats, play to a target.
- Players: 1 vs the CPU (three levels) or 2 hot-seat (the rack turns face down until the next player presses A).
- Top-down only. Rejected: an isometric view and a 3D falling-domino win effect - keep it top down, save memory, and put the effort into the end-of-round effect.
- The finale matters most: a chain of firecrackers using the sprite rotation (CHChess's capture), tile by tile along the line with particles. Built as: fuse from the last tile played back to the first and out along the other arms (arms burn in parallel), gaps shortening, each tile flung spinning (`rotRaw`) with sparks, smoke and a crack rising in pitch, a boom on the last; a match win adds fireworks until PRESS A; a CPU-won round is just swept away.
- View: on this 128 px screen, favour big readable art and a following camera over fitting everything in view. The first build (whole table at 1x, 7x13 tiles) was unreadable; play is now close up at 2x (13x25 tiles) with a camera gliding to the play, and hold B shows the whole table. No whip zoom.
- Tile style follows a reference image from the owner: bevelled BONE face (WHITE top/left, SILVER bottom/right), engraved bar, 2x2 SLATE pips with a 2-px shadow (no corner pixel, or diagonals merge), a pixel of face between pips and bevel. Per-number pip colours were dropped to match it.
- Depth pass: 3 px near edge plus drop shadow, drawn back to front; brass pin on the bar; felt vignette and printed double line; wooden racks; score plaques with a pulsing edge for the side to play. Rejected in look-dev: pips with silver corners (read as plus signs), a dithered red rack channel (noisy).
- Tile sets: Options TILES = WHITE/BLACK/IVORY/RED/BLUE/JADE/GRAPE/PINK (GRAPE because PURPLE overflowed the row). One set for everything in play; the title always uses white + black.
- Title: falling tiles as in CHMahjong/CHSolitaire; all fallers tumble end over end (planar `rotRaw`, 1:1) with a slow spin at the original fall pace; the name hand-drawn at full size in `tools/art/logo.txt`, straight on black (no plaque, no shadow); menu selector radius 2.
- Title rejections: any scaled art (it reads as crunched / pixel-doubled; keep art 1:1), turning about the vertical axis and tiles lying on their side (uncomfortable to watch), half-speed falling, grey depth-shaded tiles, a radius-3 selector (jaggies).
- Lettering: CHCrossword's anti-aliased DejaVu Serif Bold at its 12 px size, everywhere. Rejected: 14 px (looks horizontally stretched) and any rescaling - adapt the layout to the font, not the font to the layout.
- The 3x5 font is the CHGame library's (`platform/board/arduino/CHGame/libraries/CHGame/src/chgame/Draw.cpp`), so its 'M' is PPOT's (a two-row middle), as at the other tables. This game's own copy, from CHBackgammon, had an 'M' with a lighter middle (one row).
- The display font (`Font.*`: `maskFont`, `fontHalf`, `fontText`) is this game's, built on the library's masks and `glyph16`.

## Open items

- Not yet confirmed by the owner (README describes them as built):
  - whether 2x is big enough (3x would leave only ~3 tiles across);
  - spinner = only a double set as the first tile (keeps four arms at the centre so the pinwheel never dead-ends);
  - round 1's heaviest double set automatically, later the round's winner leads freely; the whole boneyard can be drawn; the match is decided at a round's end;
  - levels ROOKIE / REGULAR / SHARK, targets 100/150/200 (fives) and 50/100/150 (draw), default ALL FIVES to 100 vs REGULAR;
  - the "YOU/CPU: LAST TILE!" call; SELECT hint = the SHARK's play.
- Deferred: per-number pip colours as an option, if the owner misses them.
- First device run: `chgame run --device tools/scripts/perf.txt OUTDIR` for frame times, `say Y` for the cost by section (felt, line, HUD, rack, rest), and `chgame check --compare` against the simulator's run. Earlier simulator estimates: 6-8 ms in play, ~7-10 ms on the title. Listen to the sound.
- Flash is spare: more sizzle in the finale is affordable if asked.

## Gotchas

- RAM: the title's fallers are cached as images (`fallerImg`, 9 x 175 B, ~1.6 KB); new RAM has to come from somewhere like that.
- `tilePx` (`Table.cpp`) uses `CLEAR` = 0xFF for "no pixel", because colour 15 (`FX_B`) is a real face colour.
- `fx::applyShake(10, GFX_H - 1)` shakes everything under the scoreboard. Shaking only the felt rows split anything crossing them (the revealed CPU hand, a raised rack tile), which looked like partial renders.
- Palette: two slots were given to the tiles (SKIN to BONE 0xEEE, CYAN to SLATE 0x445), so the rainbow and confetti have no cyan. The game passes its own table (`COLOURS`, `Colours.*`, which also names BONE and SLATE) to the CHGame library's `pal::init()`. A tile set is a swap of BONE/SLATE (`table::useSet`, `SETS`).
- `Options.tiles` took the old pad byte (no save `VERSION` bump); one `pad` byte is left. Another new option needs it or a version bump.
- `fontText` picks the half-ink tone from the ink (WHITE to SILVER, GOLD/FX_B to WOOD, FELT_LT to FELT; any other ink gets no half ink): a new ink colour needs a mapping. Selection boxes are 15 px tall (y - 3) for the 9-px caps.
- Speed: close-up tiles use an SRAM row-pattern renderer (`tileFast`). `text35x2` (the floating score, the plates of ends out of view) is now the CHGame library's, which runs from flash and draws each pixel as a 2x2 `gfx_fillRect`; this game's own wrote the framebuffer directly from SRAM. The same pixels; the cost on the board is unmeasured. The simulator cannot show gains like these (host calls are cheap; the device pays flash wait states): measure on the board.
- Simulator perf estimates swing with host load (about 2x seen): compare against a clean copy of HEAD run at the same time before blaming a change.
- Sound is the CHGame library's engine (`chgame/Audio.h`); this game's effects are `Sounds.*`, and the crackers, the deal and the counting scores are `audio::blip()`s in `Stage.cpp`. `chgame audio out/audio` renders the effects to WAV.
- `CHDM_LEAN` exists but is off: debug builds (`CHGAME_DEBUG` on the board) carry the whole game, saving included. Turn it on only if the game outgrows the debug build.
- Debug protocol: the CHGame library's (`chgame/Debug.h`; it owns `? S K L N P B`, and `T` in a `CHGAME_PROFILE=1` build). The game's hooks (above the hook in `Screens.cpp`): `G` start a match, `D` stack the deal, `C` score, `W` end the round, `Y` render profile, `J` jump, `A` play for the human, `H` state, `Q` (simulator) calibration. chdrive extras (`tools/chsim/chdrive.py`): `waitturn`, `auto`, `round`, `board`, `cal`.
- Look-dev: `tools/tilemock.py` draws the pip-treatment sheet; `tools/aafont.py` regenerates `tools/art/aafont.txt`.
- Credits are exactly those in NOTICE (Press Play On Tape's 3x5 font, DejaVu for the serif); add no others.
- The simulator is the repository's `tools/chsim/chsim.py` (shared); per-game tools stay in `tools/`. For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- The board may be in use: announce a debug upload and put the release back afterwards.

## How it fits

- **The rules** (`Dominoes.*`) keep a round in 56 bytes: each hand and the boneyard as a bit per tile, the line as the tiles in the order played with the arm each went on, and the pips open at each arm's end. A second, naive implementation in the tests, which keeps the line as lists of tiles and works everything out from them, agrees with it on 40,000 random rounds, step by step.
- **The table** (`Layout.*`) is a grid of units, 42 by 26 (six pixels each close up, three seen whole). Each arm walks outward from the middle: straight on while there is room, else round a corner - clockwise by choice, so the arms turn about the middle like a pinwheel and keep out of each other's way - leaving a unit clear between tiles that are not neighbours. A tile once down never moves, so the layout of a saved game is simply played again. The tests lay out 1.5 million tiles in 20,000 matches: every one on the felt, none over another.
- **The tiles** (`Table.cpp`) are drawn in five colours lit from the top left: the face, its bevel, the engraved bar, pips from a 9-bit pattern per number, the near edge in shade and a shadow on the felt. The line is drawn back to front, so a tile's edge goes under the face of the tile in front of it, and a tile in the air leaves a shadow that shrinks and softens as it climbs. Sizes: 13 x 25 close up and on the rack, 10 x 19 for a hand shown at the round's end, 7 x 13 with the whole table in view. For a tile in the air the same pattern is written into the scratch buffer as a small image and rotated and scaled from there, CHChess's way of sending a captured piece flying.
- **The firecrackers** (`Stage.*`): each tile's place on the fuse is its distance along the line from the last tile played - back down that arm to the first tile, then out along the others, so the arms of a spinner burn together. A tile about to go spits sparks and glows; then it is gone from the line, leaving a scorch, and a copy of it is thrown up and away with the camera running along the fuse.
- **The CPU** (`Ai.*`) is a page of rules of thumb, no search: over 2,000 matches each, REGULAR beats ROOKIE 69.8% of the time at ALL FIVES (58.5% at DRAW) and SHARK beats REGULAR 62.0% (70.3% at DRAW).
- **The title**: each falling tile is built once into a 175-byte image and only turned each frame; the lettering on the top rail, the costliest drawing on the screen, is drawn once and left in the framebuffer, everything else below it.
- **The lettering**: capitals, figures and a few stops at 12 px (capitals 9 pixels tall), each glyph its ink - the typeface's own one-bit rendering, so stems stay crisp - and a layer of half ink on its curves and diagonals, drawn in a tone between the ink and what is under it (1.7 KB for 43 glyphs). It is the menus, the panels, and through CHBlackjack's mask code the outlined, gradient-filled headings and the dancing banners.
- **Sound**: short step lists, three bytes a step (`Sounds.cpp`): bone on wood, knuckles for a pass, a rising run for points, the crackers' climbing cracks and the boom, fanfares.
- **Saved games** hold the round as it stands, the score and the generators' state, so reloading can never change a tile to come. The tiles are shuffled by one generator, seeded from the moment you press the button.

## Development

Everything can be checked on a PC (Python 3 with Pillow, and a C++ compiler for the host builds: root CLAUDE.md).

    chgame check               # host tests, every script twice, device compile + size (--quick, --no-device)
    chgame check --compare A B # two runs' images (the simulator's against the board's)
    chgame test     # the rules against the reference, whole matches, the layout, save/reload, the CPU's levels
    chgame sim
    chgame run tools/scripts/showcase.txt out/showcase
    chgame gif    # tools/scripts/gameplay.txt -> docs/gameplay.gif (the README's one GIF, <= 1 MB)
    python tools/assets.py              # tools/art -> src/assets
    chgame upload       # build and upload the release (--debug adds the serial protocol)
    chgame audio out/audio
    chgame size --top 20

- With the Arduino IDE: *Tools > Optimize > Smallest + LTO* and *Tools > USB > Upload only* (the game has no use for USB Serial).
- Scripts: `say G <mode> <level> <seed> <game> <target>` starts a match, `say D 63 55 50 ..` stacks the next deal (seven tiles for you, seven for the other side, then the boneyard in order), `say C <you> <them>` sets the score, `say W <side>` ends the round as if that side had gone out, `waitturn` waits for your turn, `auto` and `round` play on for you, `rec` records, `cal` and `perf` estimate the device's render time, `say Y` reports the drawing time by section (on the board in microseconds; in the simulator in host nanoseconds).
- GIFs from `gif` and `rec` are written with every frame whole on one shared palette (the repository's `tools/chsim/fbimage.py`, `save_gif`), so no viewer can show a frame half-updated.
- The README's GIF: `tools/scripts/gameplay.txt` records the title and four clips (`01_title` .. `05_firecrackers`) and `readme_gif.py` joins them. `showcase.txt` records the same moments and the match's end as a test.
- Art: `tools/assets.py` packs `tools/art/` and the shared `tools/art/common/` into `src/assets/`: the glove (`hand.png`, CHChess's, from common), the serif lettering (`aafont.txt`, `#` ink, `+` half ink; `python tools/aafont.py` starts it again from the typeface, `--preview` compares sizes) and the title's name (`logo.txt`, 116 x 16, 1-bit). The tiles have no art: their look is in `Table.cpp` (`tilePx` for the close-up drawing, `SETS` for the sets). `python tools/tilemock.py` draws the look-dev sheet of pip treatments (`out/tilemock.png`).
