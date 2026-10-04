> RPGame 0.3 uses RP2350 RISC-V. See [project setup](../../README.md) for the current firmware paths, GPIO map and launcher. Legacy measurements below are not RP hardware test results.

# RPFileBrowser

Build using the root RPGame instructions. Insert a FAT16/FAT32 microSD with folders, `.GIF` files or text files. Long names appear as their 8.3 aliases.

| Button | Action |
|---|---|
| UP / DOWN | Move selection; hold to repeat |
| LEFT / RIGHT | Page up/down |
| A | Enter folder, play GIF or open text |
| B | Parent folder / leave viewer |
| START | Rescan current directory or retry SD mount |

The original GIF/text streaming and scratch-memory reuse are retained. RPGameSD waits for outstanding graphics transfers before reusing memory. It supports the PiZero's dedicated onboard SPI1 card and shared-SPI external-card profiles; shared profiles restore the LCD's SPI format and clock. The original CH32 GPIO-shadow workaround is removed.

Derived from [bateske/FileBrowser](https://github.com/bateske/FileBrowser), at the commit in the root `upstream.json`. See root `LICENSES.md` for the upstream licensing status.
