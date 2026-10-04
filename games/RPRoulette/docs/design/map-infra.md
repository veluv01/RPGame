# CHRoulette infrastructure map: what to copy from CHChess and CHBlackjack

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

Paths below are relative to the games' folder, `examples/Games/` (`BJ/` = CHBlackjack, `CH/` = CHChess). Most of what this map found in both games is now the CHGame library's (`chgame/...`, in `platform/board/arduino/CHGame/libraries/CHGame/src/`) or the repository's shared `tools/`, and a game's own code that was under `src/<dir>/` sits beside its `.ino`; the notes below say where. I read the files fully, diffed both games with `--strip-trailing-cr`, and ran `check_size.py` against the existing build maps. That command is read-only.

## 0. Environment facts that mattered
- **CHGfx version.** The working CHGfx was **1.3.0**, installed by hand in the sketchbook (`libraries\CHGfx`), where `arduino-cli config get directories.user` pointed the simulator. (Now board package 0.3.0 brings CHGfx 1.3.0, CHGame and CHSd, and the simulator uses those copies.)
  - The project's own `CHGfx` copy was **v1.1.0 and stale**.
  - The project's own `tools\chsim` was an older copy of the simulator that differed from BJ's. (Now the repository's `tools/chsim` is the one simulator.)
- **Board package.** `arduino-cli core list` showed `CHGame:ch32v 0.2.4`. Python has Pillow 12.1.1 and pyserial.
- **C++ compiler.** None is on the PATH. Zig exists at `<zig>`.
  - Set `CHSIM_CXX="<zig> c++"`.
  - `tools/chsim/chsim.py` splits `CHSIM_CXX` on spaces, so the path must contain no spaces. This one has none.
- **Line endings.** BJ files are CRLF and CH files are LF. `.gitattributes` normalises them.

## 1. Budgets and measured sizes (from `check_size.py` on the existing `build/` maps)
| Build | Image (flash) | Static RAM |
|---|---|---|
| BJ release | 45,832 B | 15,736 B |
| BJ debug | 46,532 B | 15,856 B |
| CH release | 48,884 B | 17,880 B |
| CH debug | 49,744 B | 17,912 B |

- **Limits** (`tools/check_size.py`, `FLASH_LIMIT` and `RAM_LIMIT`):
  - `FLASH_LIMIT = 50944`. That is 0xF700 − 0x3000: the app starts at 0x3000 and the metadata page is 0xF700.
  - `RAM_LIMIT = 18416`. The 2,048 B stack is separate.
  - Two save pages need image ≤ **50,432** B (`tools/check_size.py`).
- **Fixed SRAM**, from the map:
  - `gfx_fb` 8192
  - CHGfx `s_chunk` 1024 and `s_lut` 1024
  - `wch_usbcdc_EP2_buffer` 128
  - `chgame` 40
  - fx `parts` 480
  - BJ's save `buf` 256 (CH avoids this, see §3)
- **Flash costs documented in comments and READMEs:**
  - LTO saves ~3.8–3.9 KB (BJ's own `device.py` then; `CHChess/docs/CH32SerialBoot-notes.md`).
  - `periph=game` saves ~3.4–4 KB (both games' `config.h`).
  - `usb=uploadonly` saves ~0.6 KB (616 B on CH).
  - The debug protocol costs ~1.8 KB (BJ's `config.h` then) to ~2 KB (CH's `config.h`).
  - Saving costs ~1 KB (`CHChess/docs/CH32SerialBoot-notes.md`).
  - BJ's sequencer is 1.8 KB, against 6.5 KB for CHGameSound. Sound plus music is 2.9 KB (BJ's README then).
  - `snprintf` would cost 3.5 KB; `Fmt.*` replaces it and was identical in both games (now the library's `chgame/Fmt.*`).
  - `pinMode` would cost 2 KB of pin tables.
  - BJ's no-LTO breakdown: core+USB 5.1 KB, CHGfx 8.7 KB.
- **Performance:**
  - Flash has 3 wait states: ~5 cycles per instruction from flash against ~2 from SRAM.
  - An async full flush costs ~2.6 ms of CPU on CHGfx 1.3 (~5 ms on 1.2) (`CHChess/Frame.cpp`).
  - A full frame takes 8.37 ms on the wire at 12 bpp (the simulator's panel model, `tools/chsim/host/chgfx_host.cpp`: 8,384 µs).
  - A frame counts as "late" above 17,500 µs (now the library's `chgame/Debug.cpp`).

## 2. Audio (then each game's `src/audio/`; now the library's `chgame/Audio.*`, with a game's effects in its `Sounds.*`)
**Common engine (copy as is).**
- TIM1 CH2 drives the piezo on PB10, partial remap.
  - `hwInit()`: `PSC=47` gives a 1 MHz clock, `CHCTLR1=0x6800` (PWM1 + preload), `BDTR=0x8000` (MOE), `CCER=0x10`.
  - GPIOB CFGHR is write-only, so it goes through `extern "C" volatile uint32_t CFGHR_tmpB`.
  - The status LED is on PB9.
- Effects are stepped from the core's 1 kHz SysTick weak hook, `extern "C" void osSystickHandler(void)`.
  - Step format: `struct Step { uint16_t hz, endHz, ms; }` (6 B). BJ's `Audio.h` comment saying "8 bytes" was wrong. (The library's `Step` is now 3 B: 20 Hz and 2 ms units.)
  - Macros: `#define S(hz,end,ms)` and `REST(ms)` (now `AUDIO_STEP`, `AUDIO_REST`).
  - Sweeps are linear: `hz + (endHz-hz)*fxT/ms`.
  - Table: `struct SfxDef { const Step *steps; uint8_t n, prio; }` with `DEF(a,p)`. `DEFS[(int)Sfx::COUNT]` must follow the enum order. (Now `audio::Effect` and `AUDIO_EFFECT`, the table handed to `audio::begin()`.)
- **Priority.** A new effect is dropped if its `prio < fxPrio`; equal priority interrupts (BJ `play()`, CH `sfx()`).
- **Tone.** `period=(1e6+hz/2)/hz` with 50% duty (`tone()`). Keep effects in the piezo's 1–4 kHz sweet spot.
- **Simulator stub.** `#ifdef CHSIM` gives a silent stub. CH's stub also records `uint8_t audio::simLast` so scripts and tests can check which sound fired.
- **LED.** `audio::led(Led)` with `LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY`. `update()` runs once per logic tick: BLINK lasts 12 ticks, TRIPLE 48 (period 16), PARTY 240 (period 8).
- `#pragma GCC optimize("Os")` at the top of the file.

**BJ-only features worth taking.**
- `void blip(uint16_t hz, uint16_t ms)`: a prio-0 dynamic tone, refused while an effect with prio > 1 plays. It is used for typewriter text and the rolling-purse ticks (BJ `Presenter.cpp`, `update()`). Use it for the ball clicking over frets.
- Music: `music(Song, bool loop)`, `stopMusic()`, `loopMusic(bool)`, `musicPlaying()`, `mute(bool)`/`muted()` (the SELECT toggle, not saved), and `setMode(0 off / 1 arpeggio / 2 lead)`.
  - Score interpreter: `scoreTick()`. Playtune bytes: `0x9c n` note on, `0x8c` note off, two bytes big-endian wait in ms, `0xE0` restart, `0xF0` stop.
  - `musicHz()`: Lead mode holds `LEAD_HOLD_MS=20`; Arpeggio rotates voices every `ARP_MS=6`.
  - `noteHz()` shifts the top octave (C8..B8) down.
  - `tone(hz, smooth=true)` for music changes only `ATRLR`/`CH2CVR`, which avoids clicks.
- `Audio.h` (the enums are now in each game's `Sounds.h`):
  - `enum class Sfx : uint8_t { Deal, Flip, Chip, Cursor, Select, Deny, Win, Blackjack, Bust, Push, Lose, Peek, Shuffle, Coin, Split, Double, Insurance, Broke, Reveal, Whoosh, COUNT }`
  - `enum class Song : uint8_t { Title, Victory, Broke }`
  - `bool begin(uint8_t mode)`
- Option mapping: `soundMode(opt)` maps opt 0/1/2 to lead/arpeggio/off (BJ `Screens.cpp`).

**CH-only feature worth taking.**
- The `soft` flag gives a narrow pulse, duty `period/8`. It was set by `soft = s >= Sfx::Tick` (now each effect's `audio::SOFT` flag).
- The soft effects are `TICK {1100,0,3}` and `TOCK {850,0,3}`, kept last in the enum (CH `Sounds.h`). This fits a quiet, ratcheting wheel or ball tick.
- CH has no music; its fanfares are effects. API: `begin(bool)`, `setOn(bool)`, `sfx`, `playing()`, `update`, `led`.

**Reusable step tables.** Several are casino-flavoured and apply directly to roulette:
- `CHIP {3100,0,12}{REST 9}{3700,0,26}`
- `COIN {2800,0,10}{3700,0,28}`
- `WIN` (C7-E7-G7-C8)
- BJ `BLACKJACK` = CH `MATE` fanfare
- `LOSE` and `BROKE` descending sweeps
- `WHOOSH {1200,3800,90}`
- `SHUFFLE`, a click train with a sweep
- `CURSOR {2100,0,10}`, `SELECT`, `DENY {900,650,70}`

**Music generator** (`CHBlackjack/tools/make_music.py`, on the repository's `tools/music/composer.py`).
- `SONGS` dict: `eighth_ms` and up to 4 voice strings of `"NOTE:len"` tokens (`C6`, `F#5`, `Bb6`, `R`), lengths in eighths.
- Each note-off is placed 12 ms early (now in `tools/music/composer.py`), which is why `LEAD_HOLD_MS=20`.
- The `loops` dict chooses `0xE0` or `0xF0`.
- It writes `src/audio/Music.cpp` with the scores inside `#if !CHBJ_DEBUG` (now `#if !CHGAME_DEBUG`), exposed through `music::get(song, loop, data, n)` (`Music.h`). Every debug build, the simulator included, has no music.
- Score sizes: TITLE 453 B, VICTORY 169 B, BROKE 164 B, 786 B in all.
- For a new game, replace `SONGS` (the guard is now the library's `CHGAME_DEBUG`).

**Preview tool** (`tools/audio/preview.py` + `tools/audio/host/harness.cpp` + `tools/audio/host/Arduino.h`, now the repository's, for every game: `chgame audio`).
- Compiles the real `Audio.cpp` (now the library's `chgame/Audio.cpp`) against a TIM1 register model.
- Writes 48 kHz WAVs and prints `restarts=` (audible clicks) and an FNV hash for comparing versions.
- Each game had its own copy then: `Arduino.h` identical, BJ's harness taking `MODE song|sfx INDEX MS` and CH's `INDEX MS`, and the `SFX` name list kept in step with the enum by hand. The shared tool reads the names from the game's enums.

## 3. Save (then each game's `src/save/`; the common part is now the library's `chgame/Save.*`)
**Common (copy as is).** Flash routines mirrored from the bootloader's flash code (now `platform/bootloader/src/flash.c`):
- `PAGE=256`, `PAGE_A=0xF500`, `PAGE_B=0xF600`, metadata page 0xF700. The bootloader erases only the pages a new image occupies, so these survive re-uploads (proved by `CHBlackjack/tools/probes/FlashProbe/FlashProbe.ino`).
- `RAMFUNC(save) static void pageWrite(uint32_t addr, const uint32_t *w)`:
  - masks interrupts through CSR 0x800 (`irq & ~0x88`);
  - unlocks with `KEYR` and `MODEKEYR` = 0x45670123/0xCDEF89AB;
  - erases the page, resets the buffer, loads 64 words (`CR_BUF_LOAD`), then programs and relocks;
  - addresses are `PROG(a)=a+0x08000000`.
- `imageEnd() = &_data_lma + (&_edata - &_data_vma)`.
  - `twoPages()` requires `imageEnd() <= PAGE_A`.
  - `available()` requires `!broken && imageEnd() <= PAGE_B`.
  - If a write verifies wrong (`memcmp`), `broken = true`.
- `crc32` (poly 0xEDB88320) covers `sizeof(Record)-4`.
  - `valid()` checks magic, version and CRC.
  - `static_assert(sizeof(Record) <= PAGE)`.
- **A/B selection.** Odd `seq` goes to PAGE_B, even to PAGE_A. The newest is chosen with `(int16_t)(a->seq - b->seq) > 0`.
- **Simulator.** `static uint8_t simFlash[2][PAGE]` makes save/continue flows scriptable.
- **All CHGame games share these two pages.** Only `MAGIC` separates them: BJ `0x4A424843` "CHBJ", CH `0x53434843` "CHCS". A foreign record reads as invalid, so the game starts from defaults. CHRoulette needs its own magic; for example "CHRL" would be `0x4C524843`.

**BJ variant (`Save.cpp`, 154 lines).**
- `VERSION=1` (`uint16_t`).
- `Record { u32 magic; u16 version, seq; i32 purse; u8 hasGame, pad[3]; Options opt; Stats stats; u32 crc; }`.
- API: `bool load(Round&, bool &hasGame)` (purse restored only if `hasGame && purse > 0`) and `bool store(const Round&, bool hasGame)`.
- `writePage` uses a static 256 B buffer, costing 256 B of SRAM.
- BJ `Options` (8 B, `Round.h`): rules, goal, speed, sound, theme, fourColour, totals, dealer.
- BJ `Stats` (`Round.h`): `u32 hands, won, lost, pushed, blackjacks; i32 bestPurse, biggestWin; u16 gamesWon, gamesBroke`.
- Caller (`CHBlackjack/Screens.cpp`): `persist()` returns early in demo mode, then calls `gfx_wait()` and `save::store`.

**CH variant (`Save.cpp`, 167 lines).**
- `VERSION=2` (`uint8_t`, "three opponents").
- `Record { magic; u8 version, hasGame; u16 seq; Options; Stats; match::Record game; crc }`.
- The page is built in **`gfx_chunkScratch()`**, so it uses no SRAM. It must be called after `gfx_wait()`.
- `best()` helper. With `CHCH_LEAN` everything is stubbed, because device debug builds do not fit with saving.
- `Options` and `Stats` live in `Save.h`. Spare fields are kept as `unused` so old saves keep their layout.

**Recommendation for CHRoulette:** BJ's record shape (purse/bankroll, `hasGame`, `Options`, `Stats`) combined with CH's chunk-scratch write. (The library's `chgame/Save.*` now does the chunk-scratch write for every game: `save::load`/`save::store` of the game's struct, `hasGame` as the header's flag byte, the magic from `save::magic("CHRL")`.)

## 4. Debug protocol (then `src/debug/`; now the library's `chgame/Debug.*`), CHSIM glue and `config.h`
**`config.h` pattern** (identical structure; prefix CHBJ_/CHCH_):
- `X_VERSION` string.
- `X_DEBUG`: 1 under `CHSIM`, else 0. It can be overridden with `-DX_DEBUG=1`. (Now the library's `CHGAME_DEBUG`, from `chgame/Config.h`.)
- `X_LEAN = X_DEBUG && !CHSIM && !X_FULL`. BJ drops credits and music; CH drops saving, options and credits.
- `X_PROFILE` 0 (now `CHGAME_PROFILE`); `X_FPS 60`.

**Line-based ASCII over USB CDC** (`Debug.h` headers document it). Unknown input is never answered, so it cannot confuse `chgame-upload`.

| Cmd | Reply | Notes |
|---|---|---|
| `?` | BJ `CHBJ 1.0 frame=N lock=N`; CH `CHCS 0.1` | handshake prefix |
| `S` | `FB <frame> 8224\n` + 8192 B fb (4 bpp, even x in low nibble, 64 B/row) + 32 B RGB565 palette from `gfx_paletteOut(i)` | fade included; calls `gfx_wait()` first |
| `K <hex>` | `OK` | `chgame.injected`, ORed into the buttons |
| `L1`/`L0` | `OK [frame]` | lockstep on (0) / off (−1) |
| `N <k>` | `OK <frame>` | sent when lockstep reaches 0 again |
| `P` | `PERF ...` | BJ: upd, wait, rnd, max, late, frames. CH: rnd, max, late, frames, stk, fstk (painted-stack high-water mark). Simulator adds `pcrnd`/`pcmax` (host ns) |
| `T` | `PROF i=us ...` | only with `X_PROFILE`; 12 slots via `dbg::profStart()` / `dbg::prof(slot)` |
| `B` | none | device only: `chgame_enter_bootloader()` |
| other | hook | BJ: `OK` if `hook` returns true, otherwise silence. CH: `OK`/`ERR`, plus `HELD` via `holdGame()` while the CPU searches |

- **Reserved letters.** Game hooks must not use `? S K L N P T B`.
- **Line buffer.** BJ `line[48]`; CH `line[100]` (FEN strings).
- **Output.** `out()` writes straight to the CDC endpoint with `CDC_write_nb`, a 25 ms timeout per byte, then `CDC_flush`, because `Serial.write` flushes every byte. In the simulator it is `sim_out` to stdout.
- **Helpers.** `dbg::parseNum(const char *&p, uint8_t base)` skips spaces and commas; `dbg::print`. The protocol uses `fmtStr`/`fmtInt` from `Fmt.h` (now `chgame/Fmt.h`), so it depends on Fmt.
- **Timing marks.** BJ: `markUpdateStart`, `markWaitStart`, `markRenderStart`, `markRenderEnd`. CH drops `markWaitStart`. Without `X_DEBUG` everything is inline no-ops.
- **Hook examples.**
  - BJ `.ino`'s `debugHook()`: `R <seed>`, `D c1,c2,..` (stack the deck), `J <T|P|W|L|O|S|C>` (jump to a screen).
  - BJ `debugJump` resets `frameCount=0`, `pal::resetClock()` and `fx::reseed()` so device and simulator frames match.
  - CH `Screens.cpp`'s `debugHook()`: `G M W Y`, and in the simulator only `J Q R H V X`. `Q` returns `CAL` host-ns timings for the cost model (now the protocol's own, in `chgame/Debug.cpp`).
  - **A roulette equivalent would be `R <seed>`, a "force next pocket" command, and `J`.**

**`CHGame.*` (input and pacing; now the library's `chgame/Input.*`).** Byte-identical between the games except one comment on line 1. Copy verbatim.
- Button masks `A=1, B=2, UP=4, DOWN=8, LEFT=16, RIGHT=32, START=64, SELECT=128` (`chgame/Input.h`).
- `chgame_readButtons()` reads INDR directly: PB1 A, PB6 B, PB4 UP, PC14 DOWN, PB3 LEFT, PC15 RIGHT, PB8 START, PB7 SELECT.
- `boot()` sets pull-ups through registers, not `pinMode`.
- `nextFrame()` uses a µs accumulator, resyncs when more than 3 periods behind, and honours lockstep.
- `repeat(b, delay=18, rate=5)`, `justPressedMask()`.
- Global instance `CHGame chgame`.

**`RamFunc.h`.** `RAMFUNC(name)` expands to `__attribute__((section(".gnu.linkonce.r.chbj." #name), noinline))` (CH uses `chch`; use a new prefix). In the simulator it is just `noinline`. The section name saved 192–260 B. (Now the library's `chgame/RamFunc.h`: `chg` for the library's own, `app` for a game's `RAMFUNC`.)

**Main loop template** (`CHBlackjack/CHBlackjack.ino`'s `setup()`/`loop()`; use this, not CH's `Frame.cpp`, which exists for the engine callback and side stack).
- `setup()`: `chgame.boot(); gfx_begin(GFX_DIV2, GFX_12BPP); pal::init(); screens::begin(); setFrameRate(FPS); dbg::hook = ...`
- `loop()`:
  1. `dbg::poll(); if (!nextFrame()) return;`
  2. do { `pollButtons; pal::tick; screens::update` } while `++ticks < 3 && nextFrame()` (up to 3 catch-up logic ticks per drawn frame)
  3. `pal::commit(); gfx_wait(); render(frameCount); gfx_flushAsync();`
- `audio::update()` is called inside `screens::update` (BJ `Screens.cpp`, `update()`).

## 5. `tools/`: generic vs game-specific
*Now:* the generic tools are the repository's `tools/` (`tools/chsim`, `tools/audio`, `tools/check_size.py`, `tools/serialcap.py`, `tools/device.py` and `tools/hosttests.py` behind the `chgame` command), and a game keeps only its `tools/game.py`, `tools/chsim/chdrive.py`, `assets.py`, `make_music.py`, tests and scripts. What follows is how the two games' copies compared.

**Identical in both games (pure template):**
- `chsim/chsim.py`, `chsim/fbimage.py`, `chsim/host/Arduino.h`, `chsim/host/chgfx_host.cpp`
- `serialcap.py`, `requirements.txt`, `audio/host/Arduino.h`
- `.gitignore`, `.gitattributes`, `LICENSE`

**Near-identical (rename the game, prefix or handshake):**
- `chsim/host/main.cpp`: CH adds `sim_waitInput()`; take CH's.
- `chsim/host/sim.h`: same change as `main.cpp`.
- `check_size.py`: docstring only.
- `device.py`: the `-D<PFX>_DEBUG=1` flag and the FQBN variable names.
- `tests/run_tests.py` (now `tools/hosttests.py`, the sources in each game's `tools/game.py`): the source list.

**Diverged (take CH's superset):**
- `chdrive.py`: 145 differing lines. CH adds `--id` (default `CHCS`; BJ hard-coded `CHBJ`), `-v`, `freegif`, `rec start/stop`, `step`, `cal`, the `pcrnd`-to-device-ms perf estimate, HELD/ERR handling, and chess-only `goto`/`board`/`waitturn`.
- `chsim/gifsheet.py`: CH only.
- `audio/preview.py` and `audio/host/harness.cpp`: BJ's has music; CH's is effects only.

**Fully game-specific:** `assets.py` (551 differing lines), `make_music.py`, `tests/test_*.cpp`, `scripts/*.txt`, and CH's `sheet.py`, `pieces.py` (needs numpy, which is not in `requirements.txt`) and `book.py`.

**`tools/chsim/chsim.py build <sketch> [-D N=V]`** (as it was then; CLAUDE.md describes it now).
- Writes `build/<Name>/sketch_ino.cpp`, concatenating the `.ino` files with `#line`. The folder name must equal the `.ino` name.
- Compiles `sketch/src/**/*.cpp|.c` (now also the files beside the `.ino`, and the CHGame library), all of CHGfx's `src/*.cpp` except `CHGfx.cpp`, and `host/*.cpp`.
- Flags: `-std=gnu++17 -O1 -g0 -w -DCHSIM -DCH32X035 -DARDUINO=10800`.
- Output: `tools/chsim/build/<Name>/sim.exe`.
- CHGfx lookup order: `$CHSIM_CHGFX`, then the sketchbook copy (`libraries/CHGfx/src`), then `CHGfx*` (now the board package's copy in `platform/board/arduino/CHGame/libraries/` comes before the sketchbook).
- Compiler lookup order: `$CHSIM_CXX`, zig, the `ziglang` pip package, clang++, g++.

**The simulator.**
- Time is virtual; each `loop()` adds 100 µs (`LOOP_US` in `tools/chsim/host/main.cpp`).
- It starts in lockstep 0 and blocks on stdin after 2 idle loops.
- The simulator modelled flush time at 40 µs + 0.511 µs/px (12 bpp); `tools/chsim/host/chgfx_host.cpp` now models the panel row by row.
- It reports `BUG:` and exits with code 3 when the framebuffer changes during an async flush, or when `gfx_chunkScratch` is written mid-flush. `chdrive` then fails.

**Script format** (then each game's `chdrive.py` docstring, BJ's plus CH extras; now the header of `tools/chsim/chdrivelib.py`).
- One command per line; `#` starts a comment.
- Commands: `wait N`, `tap BTN[+BTN] [H=3]`, `hold`, `release`, `snap NAME`, `gif NAME N [EVERY]`, `say TEXT`, `free SECONDS`, `perf`, `prof`. CH adds `rec start [E]`, `rec stop NAME`, `freegif NAME S E`, `step N`, `cal`.
- `tap` is `K mask` → `N H` → `K 0` → `N 1`.
- `snap` saves at 3x and builds a 4-column `sheet.png`. `gif` saves at 2x with `duration=1000*every/60`.
- Example: `CHBlackjack/tools/scripts/showcase.txt` made the README GIFs, using `say J P` for a fresh table and `say D` to stack the deck.
- `save1.txt`/`save2.txt` test persistence across an upload. `pace.txt` checks ~300 frames per 5 s with `late=0`.

**`assets.py` idiom** (palette identical in both games).
- `PALETTE = [0x000,0xFFF,0x042,0x173,0x4B5,0xBBC,0xE12,0x702,0xFC2,0x741,0x26E,0x125,0xFB8,0x6EF,0xF0F,0xFC2]`
- `NAMES = INK WHITE FELT_DK FELT FELT_LT SILVER RED WINE GOLD WOOD BLUE NAVY SKIN CYAN FX_A FX_B`
- Letters `k w d f g s r m y b u n p c x z`; `' '` and `'.'` are transparent (index 16).
- Loaders:
  - `load_art`: text art in `tools/art/*.txt` (the art several games share is now in the repository's `tools/art/common/`, which `tools/artlib.py` searches after the game's folder). CH's version allows several images separated by blank lines.
  - `load_png`: a palette-exact PNG that rejects any off-palette pixel. FX_B is excluded because it shares GOLD's RGB.
- Packers:
  - `pack_span4` produces `w,h`, then per row `n` and n bytes of `(len-1)<<4|colour` (colour 15 = skip, runs up to 16, trailing transparency implicit). This is the sprite4 format.
  - Also `pack_rows1` (MSB-first 1 bpp), `pack_span1`, `pack_cols`.
- Output: `src/assets/Assets.{h,cpp}` ("GENERATED, do not edit") plus 6x previews in `build/assets/`.
- BJ has an `Out` class (`array`/`const`/`write`); CH has `c_array`.

**Build and run commands** (as they were; today `chgame build`, `upload`, `run`, `test` and `audio` do each, see CLAUDE.md).
- Compile: `arduino-cli compile -b CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly <Sketch>`
- Upload: `arduino-cli upload -b CHGame:ch32v:rev0 -p COMx <Sketch>`
- `python tools/device.py build|upload [--debug] [--port]`:
  - debug builds drop `usb=uploadonly` and add `--build-property build.extra_flags=-D<PFX>_DEBUG=1` (now `-DCHGAME_DEBUG=1`);
  - outputs go to `build/release` or `build/debug`;
  - it then runs `check_size.py --top 0`;
  - port lookup uses VID:PID `16C0:27DD` at 115200 with DTR.
- `python tools/device.py run SCRIPT OUTDIR` builds and uploads a debug build, then runs `chdrive --device`. `device.py shot OUT.png` takes one screenshot.
- `python tools/chsim/chdrive.py --sim . tools/scripts/showcase.txt docs/` (CH: add `--id CHCS`); now `chgame run tools/scripts/showcase.txt docs/`.
- The game's `tests/run_tests.py` (now `chgame test`): zig with `-std=gnu++17 -O1|-O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=undefined`, compiling `test_*.cpp` plus the game's rules folder only (CH adds `-DCHTEST`; now the sources listed in `tools/game.py`). The harness is two macros, `CHECK(c)` and `CHECK_EQ(a,b)`, ending with "N checks, M failures".
- `python tools/check_size.py build/release [--symbols] [--top N]`. With LTO the per-file table shows `ltrans` partitions.
- `python tools/audio/preview.py out/audio` (now `chgame audio out/audio`); `python tools/make_music.py`; `python tools/assets.py`.

## 6. Repo hygiene template
- **`.gitignore`** (identical in both):
  - `build/`, `tools/chsim/build/`, `tools/tests/build/`, `out/`
  - `tools/.cache/` (BJ's PPOT clone; the comment is stale in CH)
  - `__pycache__/`, `*.pyc`, `docs/mockups/`, `.vscode/`, `.DS_Store`, `Thumbs.db`
- **`.gitattributes`:** `* text=auto`, `*.gif binary`, `*.png binary`.
- **`LICENSE`:** Apache-2.0, identical. CH also ships `LICENSE.MPL-2.0` for the engine.
- **`NOTICE`** pattern from CH:
  - "Copyright 2026 bateske";
  - third-party code with its licence;
  - "From CHBlackjack (Apache-2.0): input/frame pacing, palette, drawing, outlined lettering, effects, sound sequencer, flash saving, debug protocol, simulator and tools", plus credit that the 3x5 font in `Draw.cpp` (now the library's `chgame/Draw.cpp`) is Press Play On Tape's;
  - then what is new.
- **`README.md`** sections:
  1. Title and a pitch paragraph.
  2. A GIF table from `docs/*.gif` (2x3), with the note "Captured from the PC simulator in `tools/chsim`". (Now one GIF, `docs/gameplay.gif`; the repository's `docs/game-readme.md` has the format.)
  3. Credits.
  4. Installing: board package 0.2.4+ via the URL `https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json`; CHGfx 1.3.0 installed by hand; folder name must match the `.ino`; Tools menu settings; the arduino-cli lines. (Now board package 0.3.0 brings CHGfx, CHGame and CHSd.)
  5. Playing: a button table.
  6. Rules and options.
  7. "How it fits": flash, performance, saving, sound.
  8. Development: one bullet per tool command.
  9. Files tree (BJ).
  10. License.

## 7. Stale copies and comments to fix while copying
(These were in the games' own copies, then under `src/`; the library's files replaced them.)
- BJ's `Audio.h` said a step is 8 bytes; it was 6.
- CH's `Debug.h` described the `?`/`P` replies BJ's way; the actual replies were `CHCS 0.1` and `rnd/max/late/frames/stk/fstk`.
- CH's `Debug.cpp` referred to a perf script in `tools/chsim` that never existed; the calibration lives in `chdrive.py`'s `cal`.
- `CHChess/tools/scripts/pace.txt` was BJ's file verbatim (`J P` and `R 7` mean nothing in chess).
- Debug hook replies differ: BJ is silent on an unhandled command; CH answers `ERR`. CH's behaviour plus its `chdrive` is the better template.

**Proposed copy set for CHRoulette** (now the library and the repository's tools provide all of it; nothing is copied):
- `CHGame.*` and `Fmt.*` verbatim; `RamFunc.h` with a new prefix.
- BJ's `.ino` loop and `config.h` with a new prefix.
- BJ's `Audio.cpp`/`Music.*`/`make_music.py`, plus CH's `soft` flag.
- BJ's save record written into CH's chunk-scratch page, with a new MAGIC.
- CH's `Debug.*` (ERR/HELD and stack high-water mark); drop `holdGame`/`waitInput`/`frameStack` if unused.
- The whole `tools/chsim` from CH, including `tools/chsim/gifsheet.py`, with the `--id` default changed.
- `serialcap.py`, `check_size.py`, `device.py`, `run_tests.py`, `audio/` and `requirements.txt` with names changed.
- `assets.py` rebuilt on CH's skeleton with the same palette, letters and `pack_span4`.