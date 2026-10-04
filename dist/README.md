# RPGame 0.3.1 PiZero RISC-V builds

`rp2350-pizero-riscv/standalone`: 33 ordinary ROM-download UF2s for Waveshare RP2350-PiZero, including all 20 games, six apps and seven examples. Begin with HardwareCheck, then SDLauncher for the SD menu.

`rp2350-pizero-riscv/menu`: 25 relocated app/game images at flash offset 0x80000, with matching `.RPG` packages. Install packages using the menu; copy the ready-to-use `sdcard/` contents to the card in the onboard slot. The relocated UF2s are build outputs, not the ordinary ROM-download installation path.

Every directory has builds.json, SHA256SUMS and compiler logs. Family ID is 0xE48BFF5A; the additional absolute-family block is Picotool's RP2350-E10 guard. All builds use the native RP2350B variant, RISC-V at 150 MHz, a 4 MB logical window within the physical 16 MB, no PSRAM/internal FS and Pico SDK USB. Physical testing is pending.

All firmware uses LCD SPI0 and onboard SD SPI1 (GP30/31/40/43). Follow `docs/PIZERO.md`. Replace earlier external-SD card packages; package format v1 does not encode the board. The optional Pico 2 shared-SPI profile remains in source and can be built with `--board pico2`, but is not prebuilt here.
