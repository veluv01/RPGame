# Validation of RPGame 0.3.1

## Configuration and builds

Validated on 3 October 2026 against consolidated CHGame commit `e876774c3a079b0362e1f0fc8494ade491bdbe29`.

- Arduino CLI 1.5.1; Arduino-Pico 6.2.0.
- RISC-V GCC 16.1.0, `pqt-gcc-riscv` 5.0.0-9576866.
- Target `rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150`: native Waveshare RP2350B variant, Hazard3 RISC-V, 150 MHz, first-4-MB logical flash window, no PSRAM/internal filesystem, Small (-Os), Pico SDK USB.
- LCD SPI0 GP2/3/4; onboard SD SPI1 GP30/31/40, CS GP43; existing button/piezo GPIOs, normal core-0 loop, no FreeRTOS or OTA image.
- **All 33 standalone and 25 menu-slot PiZero builds passed**. Actual compilation commands for every image select the RPGame PiZero macro, native Waveshare variant and RISC-V toolchain. Every image includes SDK startup metadata and a bounded linker region.
- All 25 staged card packages match their PiZero menu builds byte for byte; catalog metadata, package CRCs and every staged asset checksum passed.
- The optional Pico 2 shared-SPI source profile was also compiled for HardwareCheck and FileBrowser. Those compatibility UF2s are not included in this archive.
- Compiler logs contain 18 upstream cross-enum comparison/arithmetic warnings across standalone/menu builds of Dominoes, WordWheel, Roulette and StlView. They did not prevent successful compilation; no driver warning was reported.

The largest binary is WordWheel at **141,788 bytes**; the largest compiler-reported static RAM usage is SDLauncher at **53,204 bytes**. These reports do not measure peak stack/heap usage. Arduino's displayed program limit is the core flash setting; `scripts/linker.py` and release checks enforce the smaller per-profile region. The resident launcher is below its 512 KiB region; every menu image ends below the save area.

| PiZero sketch | Compiler program bytes | Static RAM bytes | Standalone binary bytes |
|---|---:|---:|---:|
| Benchmark | 80,128 | 37,312 | 103,292 |
| Demoscene | 70,824 | 37,488 | 92,644 |
| Fonts | 73,380 | 34,204 | 93,668 |
| GameHello | 70,344 | 39,396 | 91,172 |
| GameKit | 72,404 | 36,412 | 94,628 |
| HardwareCheck | 80,124 | 35,948 | 100,716 |
| HelloGraphics | 66,012 | 34,292 | 86,428 |
| PartialUpdate | 65,040 | 34,312 | 85,340 |
| RPBackgammon | 109,844 | 44,652 | 134,228 |
| RPBingo | 97,508 | 43,372 | 120,836 |
| RPBlackjack | 108,096 | 44,004 | 131,460 |
| RPBoardwalk | 111,912 | 43,388 | 134,932 |
| RPCheckers | 102,956 | 45,532 | 126,636 |
| RPChess | 109,756 | 45,876 | 133,436 |
| RPCraps | 111,736 | 43,868 | 135,084 |
| RPCrossword | 116,588 | 47,164 | 139,748 |
| RPDominoes | 103,152 | 45,500 | 127,212 |
| RPFileBrowser | 84,864 | 40,396 | 105,572 |
| RPFour | 95,156 | 44,524 | 120,028 |
| RPMahjong | 108,168 | 45,852 | 132,540 |
| RPMultiSprite | 92,912 | 38,948 | 114,324 |
| RPPoker | 110,344 | 43,932 | 133,164 |
| RPRoulette | 111,608 | 44,524 | 135,516 |
| RPSDtoUSB | 98,780 | 40,612 | 120,076 |
| RPSlots | 108,740 | 43,308 | 132,092 |
| RPSnakes | 97,980 | 43,828 | 121,636 |
| RPSolitaire | 91,612 | 44,052 | 113,596 |
| RPStlView | 94,080 | 41,284 | 118,252 |
| RPTicTacToe | 111,568 | 42,980 | 134,892 |
| RPWordWheel | 118,624 | 44,476 | 141,788 |
| RPWords | 116,008 | 45,628 | 139,420 |
| RPYacht | 105,732 | 43,340 | 129,084 |
| SDLauncher | 78,492 | 53,204 | 99,460 |

Exact board, FQBN, flags, image lengths, hashes, memory reports and compiler output are in `dist/rp2350-pizero-riscv/{standalone,menu}/`. All prebuilt files in this archive use the onboard slot. No RP2040 or RP2350 ARM firmware was rebuilt for 0.3.1.

## Host and image checks

| Area | Result and scope |
|---|---|
| Actual SD transport (new) | Real Sd2Card.cpp with SDK/card mocks: both shared SPI0 and dedicated PiZero SPI1 passed initialization, correct pins/DREQs, whole-sector DMA/stream reads, writes, bad-CRC rejection, absent-card cleanup, timeout, E5-safe DMA abort and subsequent polled fallback |
| LCD/SD isolation (new) | Dedicated card operations leave LCD SPI0 registers/CS untouched; shared mode restores LCD format/clock; both retain the graphics flush barrier |
| Pin guards (new) | Reject high GPIOs on RP2350A, control collisions, different pad triples on a shared peripheral and GP41 as SD MISO |
| Graphics (rerun) | 19,962 checks, zero failures: portable drawing, clipping, palettes/fades, fonts and simulated flushing |
| USB adapter (rerun) | Packet reads, accumulated sector writes, whole-command observers, abort/restart, bounds, read-only mode, error cleanup, media/eject and SCSI checks passed |
| Input/timing (rerun) | Actual portable Input implementation: snapshots, injection, repeat, lockstep, timer wrap and resynchronization passed |
| Package/flash/save logic (rerun) | Format/CRC/image metadata/range checks, 28 interrupted-install points, 100 saves and corrupt-record fallback passed against a host flash model |
| SD-to-flash path (rerun with new PiZero package) | Staged Four package read by the actual FAT reader, validated and installed from contiguous and seven-extent FAT16 images; flash bounds and installed CRC passed |
| STL fixed point (rerun) | All 13 models × 7 views passed against double precision, including extreme scale, offset, NaN and flat inputs |
| Release images (new builds) | All 58 UF2s passed family, SHA-256, flash bounds and ROM IMAGE_DEF loop checks; all 25 RPG packages passed CRC/image checks; wrong-family, truncated, duplicate and damaged files were rejected |
| FAT reader (retained 0.3 results; code unchanged) | 23,588 checks, zero failures: FAT16/32, partition/superfloppy cases, directories, fragmentation, short/looping/overlong chains and unsupported-format rejection |
| Twenty games (retained 0.3 results; portable logic unchanged) | All 20 logic suites passed with UBSan and quick arguments; all 20 simulator smoke runs passed |
| Card assets (retained 0.3 results; data/portable app code unchanged) | Words, WordWheel, Crossword and StlView started against a populated FAT image; StlView opened/rendered the 12-triangle CUBE.STL |
| Serial debug (retained 0.3 result) | GameHello compiled for the Pico 2 RISC-V target with CHGAME_DEBUG=1; this is an earlier compile record, not a PiZero hardware check |

New SD-driver, platform and SD-package host checks used address and undefined-behavior sanitizers. Leak detection was disabled because the reference runtime's process tracer cannot access `/proc`; this is not a leak-test result. Game logic tests use UBSan. The quick game suite excludes Poker's optional exhaustive seven-card enumeration and Crossword's game-specific FAT32 fixtures; the separate FAT suite tests FAT32.

Current results are in `SD_TRANSPORT_TESTS.txt`, `HOST_TESTS.txt`, `PACKAGE_FAT_TESTS.txt`, `RELEASE_CHECKS.txt`, `PIZERO_BUILD.txt`, `BUILD_PROFILE_CHECK.txt` and `SHARED_SPI_BUILD.txt`. Unchanged portable-code results from 0.3 are retained in `FAT_TESTS.txt`, `DEBUG_BUILD.txt`, `STL_CARD_TEST.txt`, `SD_ASSET_SMOKES.txt` and `game-tests/`. Simulated frame/SD/renderer timings are not RP2350 performance measurements. Host mocks substitute physical flash, SPI, DMA, USB and PWM behavior.

Commands from the project root:

```sh
python3 scripts/install_board.py
python3 scripts/build.py --board pizero --games --examples
python3 scripts/build.py --board pizero --games --profile menu
python3 scripts/sdcard.py --board pizero
python3 scripts/test_sd_transport.py
python3 scripts/test_host.py --sanitize
python3 scripts/test_package_fat.py
python3 scripts/verify_release.py
```

Additional portable suites:

```sh
python3 libraries/RPGameSD/tests/run_tests.py
python3 scripts/test_games.py --logic --quick
python3 scripts/test_games.py --smoke
```

`SOURCE_SHA256SUMS` covers all delivered source, tools, documentation, licenses and sample/game assets outside `dist/` and `sdcard/`; the firmware and card folders have their own checksums. Build/cache outputs are omitted from the archive. The ZIP's entries and embedded source/card/UF2 hashes are checked after packaging.

## Physical board testing still required

No RP2350 board was connected. Compilation and host logic passed, with these physical checks pending:

1. Flash PiZero standalone `HardwareCheck.uf2` first. Confirm ST7735S colors/offsets, full/partial/async flushing, all buttons, buzzer and onboard SD reads. The activity blink needs an external GP25 LED; the onboard red LED indicates power.
2. Exercise onboard SPI1 initialization, reads/writes, DMA/CRC, timeouts/retries, abort recovery and sustained LCD/SD traffic. Separate peripherals do not establish parallel rendering or measured speed gains; callers can still reuse graphics storage.
3. Replace old external-SD packages with the supplied card tree, flash PiZero `SDLauncher.uf2`, install a game with A, and confirm ROM handoff and a three-second START return. Exercise ROM error recovery and controlled interrupted installs after basic bring-up works. Version-1 package headers do not enforce the board's pin map.
4. Check actual PWM effects/music, input timing, save/load, CRC fallback and save preservation across installs. The two save slots are shared by all titles; they are not a per-game archive.
5. Check native J4 CDC/MSC enumeration, file round trips, read-only toggle, OS eject/reinsert, card hot-plug and ROM-download/reset recovery. Eject the host drive before removing the card or switching firmware.
6. Measure frame rates, USB speed, interrupt latency, peak memory and power behavior on the actual board.

Use `dist/rp2350-pizero-riscv/standalone/` for initial ROM-download uploads. See [PIZERO.md](PIZERO.md) for board wiring and [LAUNCHER.md](LAUNCHER.md) for reserved flash layout/recovery. The physical SD, flash and ROM handoff remain unverified.
