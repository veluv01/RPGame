# RPSDtoUSB 2.1 for RP2350 RISC-V

Build and wiring instructions are in the [project README](../../README.md). The app uses the **Pico SDK USB** stack and exposes the card as native USB mass storage beside the serial port. The new interface shows commands, filesystem events, statistics and card identity. Its monitor observes host block traffic; the host owns filesystem writes.

| Button | Action |
|---|---|
| A | Rescan card / restore media after eject |
| START tap | Toggle read-only policy and signal media change |
| LEFT / RIGHT | Switch events, statistics and card panels |
| UP / DOWN | Scroll the event log |
| B held about one second | Detach USB and reset to the resident menu |
| START held about three seconds | Detach USB and reset to the resident menu |
| B held at power-up | Safe mode: remain a plain USB serial device |

The hold actions are serviced between card operations. With standalone firmware, a reset restarts this app. BOOTSEL remains available for uploading firmware; the serial interface retains the core's 1200-baud upload reset.

Eject the drive in the OS before removing the card, rescanning, changing write policy or switching firmware. Any serial byte requests sector/retry/error/card status; `U` requests drawing-time statistics. Those times become hardware measurements only when obtained on a board.

The RP transport uses TinyUSB callbacks, a 512-byte read cache and write accumulator. Complete SD transactions occur inside callbacks, while monitor command boundaries span SCSI READ/WRITE(10). Main-loop display/card/serial work holds the core USB mutex. Reads and writes retain the CRC/retry driver. Host tests cover the adapter and fault cleanup; physical enumeration, hot-plug and file round trips still need testing.

Optional global `-DCHSD_TEST` enables serial fault injection. `-DCHSD_AUTOBOOT_MS=30000` enables timed BOOTSEL recovery. Neither is enabled in supplied firmware. The prior WCH hardware harness is not an RP validation tool.

Sources: consolidated [CHGame SDtoUSB](https://github.com/bateske/CHGame) app layers with the existing RPGame native USB adapter. GPL-3.0 notices are retained in `LICENSE` and source files.
