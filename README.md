# RPGame 0.3.1 — RP2350-PiZero RISC-V

This release ports the consolidated [CHGame](https://github.com/bateske/CHGame) snapshot `e876774c3a079b0362e1f0fc8494ade491bdbe29` (3 October 2026) to **RP2350 Hazard3 RISC-V**. The supplied firmware targets the **Waveshare RP2350-PiZero with its onboard microSD slot**. The external ST7735S 128×128 LCD, eight buttons and piezo keep their existing GPIO assignments.

It includes the shared game library, all **20 games**, the updated STL and USB-reader interfaces, four apps, HardwareCheck and a resident **SD launcher**. **Builds and host checks pass; physical-board testing is still required**, especially ROM handoff, flash writes, audio and LCD/SD/USB timing.

Use the [PiZero guide](docs/PIZERO.md) for the 40-pin header and native USB connector. The LCD uses **SPI0**; the onboard card uses **SPI1, GP30/31/40/43**, already connected on the PCB. Firmware uses the first 4 MB of the board's 16 MB flash. Replace older external-SD `.RPG` files with this release's `sdcard/` files: the package format identifies CPU/layout, but does not identify a board or change its pin map.

RPGame 0.3 builds target RISC-V only. The previous 0.2 release remains the baseline for RP2040 and RP2350 ARM.

## Try the supplied firmware

1. Connect the LCD, buttons and piezo using [the PiZero wiring table](docs/PIZERO.md). Insert a microSD card in the onboard slot.
2. Hold BOOT while connecting the native programming USB-C port (J4). Copy `dist/rp2350-pizero-riscv/standalone/HardwareCheck.uf2` to the RP2350 drive. Check the LCD, eight buttons, buzzer and onboard SD read. The activity blink needs an optional external LED; the board's red LED indicates power.
3. Format a microSD card as **FAT16 or FAT32**, then copy the **contents** of `sdcard/` to its root. The card should have `/GAMES/*.RPG`, `/WORDS.DIC`, `/PHRASES.BNK`, `/CHCW`, `/WALK.BIN` and `/MODELS`.
4. Enter ROM download mode again and copy `dist/rp2350-pizero-riscv/standalone/SDLauncher.uf2` to the board. The menu lists the games and apps by their package title.
5. Use UP/DOWN to select, LEFT/RIGHT to move a page, **A to install and play**, B to launch the already installed image, and START to rescan. SELECT+B enters BOOTSEL. Hold START for three seconds in a game to reset to the menu.

The launcher keeps one installed image. Switching titles rewrites the game slot; selecting the same unchanged image skips the rewrite. An interrupted install leaves the menu available to retry, but does not retain the previous game image. Card files can be fragmented, up to 64 extents per package; the menu lists up to 64 packages.

`standalone/*.uf2` runs a single title directly and replaces the menu. In a standalone title, a plain reset/START exit restarts that title. Restore `SDLauncher.uf2` to return to the SD menu. Use the `.RPG` files with the menu; `menu/*.uf2` are relocation build outputs, not the ordinary BOOTSEL installation path.

## Contents

| Folder | Contents |
|---|---|
| `libraries/RPGfx` | Original indexed graphics and RP SPI/DMA transport |
| `libraries/RPGame` | Input, timing, palettes, drawing, effects, PWM audio, saves, serial debug |
| `libraries/RPGameSD` | Existing CRC/DMA block driver and writable SD wrapper; new portable read-only FAT API |
| `games/` | Backgammon, Bingo, Blackjack, Boardwalk, Checkers, Chess, Craps, Crossword, Dominoes, Four, Mahjong, Poker, Roulette, Slots, Snakes, Solitaire, Tic-Tac-Toe, WordWheel, Words, Yacht |
| `apps/` | HardwareCheck, MultiSprite, StlView, FileBrowser, SDtoUSB, SDLauncher |
| `dist/rp2350-pizero-riscv/standalone` | 33 PiZero UF2s: games, apps, launcher and examples, all using onboard SD |
| `dist/rp2350-pizero-riscv/menu` | 25 PiZero images linked for the reserved game slot, with `.RPG` packages |
| `sdcard/` | Ready-to-copy packages and data files, with a catalog and checksums |
| `docs/` | Architecture, validation, build information and game test logs |

## Build from source

Install [Arduino-Pico 6.2.0](https://github.com/earlephilhower/arduino-pico/tree/6.2.0) and Arduino CLI. The Boards Manager index is `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`.

From this project folder:

```sh
python3 scripts/install_board.py
python3 scripts/build.py --board pizero --games --examples
python3 scripts/build.py --board pizero --games --profile menu
python3 scripts/sdcard.py --board pizero
python3 scripts/verify_release.py
```

The default board is `rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150`: **native RP2350B, 150 MHz, 4 MB logical window, no internal filesystem, Small (-Os), Pico SDK USB**. `architectures=rp2040` is the Arduino core family name, including RP2350. RPSDtoUSB requires its native Pico SDK USB stack.

`--games --profile menu` rebuilds all 25 SD packages; `SDLauncher` always uses the standalone profile. Build scripts accept `--cli`, `--config-file`, `--build-root`, `--output`, and global `--flags`. The libraries are found in the project automatically; an IDE installation can copy all three libraries to the sketchbook, but the supplied scripts are required for menu relocation and bounded flash regions.

RPGame's linker hook discards Arduino-Pico's fixed-address prebuilt OTA stub and enters the SDK CRT0 through its RISC-V IMAGE_DEF. Internal LittleFS/OTA, FreeRTOS and signed-image boot are not configured in these builds. Do not add an internal FS or change the flash size without changing the storage layout and package format.

## Optional Pico 2 source profile

The source retains the external shared-SPI card configuration for a non-wireless Pico 2. Select `--board pico2` for both build profiles and card staging, using a separate card/output folder if retaining the PiZero files. Pico 2 firmware is not prebuilt in this archive. **This physical-pin table does not apply to the PiZero**; use [PIZERO.md](docs/PIZERO.md) for that board.

| Peripheral signal | GPIO | Physical pin |
|---|---:|---:|
| LCD SCK + SD CLK | GP2 / SPI0 SCK | 4 |
| LCD SDA/MOSI + SD DI | GP3 / SPI0 TX | 5 |
| SD DO/MISO | GP4 / SPI0 RX | 6 |
| LCD CS | GP5 | 7 |
| LCD DC | GP6 | 9 |
| LCD RESET | GP7 | 10 |
| SD CS | GP8 | 11 |
| Piezo signal | GP9 | 12 |
| UP | GP10 | 14 |
| DOWN | GP11 | 15 |
| LEFT | GP12 | 16 |
| RIGHT | GP13 | 17 |
| A | GP14 | 19 |
| B | GP15 | 20 |
| SELECT | GP16 | 21 |
| START | GP17 | 22 |
| Status LED | GP25 | Onboard LED |

Buttons connect GPIO to GND and use internal pull-ups. Use a common ground and 3.3 V logic. Retain the appropriate display backlight and piezo drive circuitry. The backlight is not assigned a GPIO by this port. On a Pico, use its power-input arrangements; battery charging/power circuitry and custom MCU board design are outside this firmware package.

Set a different pin map in `libraries/RPGfx/src/RPGamePins.h`, or pass **global** build flags so the sketch and all three libraries use the same values. Defining pins only inside the `.ino` does not configure separately compiled libraries.

```sh
python3 scripts/build.py --board pico2 --flags="-DRPGAME_SPI_BUS=1 -DRPGAME_SPI_SCK=26 -DRPGAME_SPI_MOSI=27 -DRPGAME_SPI_MISO=28"
```

Pin checks reject invalid hardware-SPI assignments, GPIOs outside the selected chip package and control-signal collisions. RP2350B high GPIOs require its proper board/package selection. `RPGAME_SPI_*` selects the LCD bus; `RPGAME_SD_SPI_*` and `RPGAME_SD_CS` select the card bus independently. Shared-bus profiles must use the same SCK/MOSI/MISO pins. Both request up to 24 MHz by default; `gfx_spiHz()` and `SD.rawCard().sckHz()` report the achievable clock. Lower `RPGAME_LCD_MAX_HZ` or `RPGAME_SD_MAX_HZ` globally if needed.

The panel initialization retains the original 128×128 ST7735S offsets: MADCTL `0xC8`, column offset 2, row offset 3. A different module may need `gfx_setPanelOffsets()` or `gfx_setInverted()` after initialization.


## Saves, debug and validation

Saves alternate between two reserved 4 KiB sectors. They retain the upstream magic/version/sequence/CRC record format, with **244 bytes of user data**. The two slots are shared by all titles: switching games can replace another game's saves. Package installation preserves these sectors; a full flash erase does not.

`-DCHGAME_DEBUG=1` enables USB CDC input injection, lockstep, screenshots, state hooks and frame timing. Existing game debug IDs and configuration macro names are retained for tool compatibility. The WCH stack painter and crash-register capture are unavailable on RP; `stk=0` means unmeasured and `FAULT none` provides no crash-history diagnosis.

```sh
python3 scripts/test_host.py --sanitize
python3 scripts/test_sd_transport.py
python3 libraries/RPGameSD/tests/run_tests.py
python3 scripts/test_games.py --logic --quick
python3 scripts/test_games.py --smoke
```

Host tests use g++/clang++ and Pillow for screenshots. The game runner uses each game's quick arguments; Poker's optional exhaustive seven-card enumeration and Crossword's FAT32 game-specific fixtures are not part of the quick game suite. The separate FAT suite includes FAT32. ASan leak detection is disabled in the reference runtime because its process tracer cannot access `/proc`; address and undefined-behavior checks remain enabled for the platform tests.

See [validation](docs/VALIDATION.md), [flash/launcher design](docs/LAUNCHER.md), [onboard SD changes](docs/UPDATE_0.3.1.md), [upstream update changes](docs/UPDATE_0.3.md), and [source licenses](LICENSES.md). Keep each component's copyright/license notices when redistributing.
