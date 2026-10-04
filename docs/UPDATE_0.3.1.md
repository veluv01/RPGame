# RPGame 0.3.1 — PiZero onboard SD

The Waveshare RP2350-PiZero's onboard microSD socket is now the default card for its RPGame RISC-V board profile. The external LCD, buttons and piezo retain their earlier GPIO assignments. No external SD module is needed.

| Change | Result |
|---|---|
| Separate LCD and SD bus/pin settings | LCD stays on SPI0; onboard SD uses SPI1, GP30 SCK, GP31 MOSI, GP40 MISO, GP43 CS |
| Native PiZero alias selects onboard SD globally | Sketches and all separately compiled libraries use the same profile; high GPIOs require the RP2350B variant |
| Card driver owns dedicated SPI1 setup/CS/DREQs | Card operations leave LCD SPI0 registers and CS untouched; existing CRC, DMA timeout/abort and polled fallback remain |
| Shared-bus compatibility retained | Pico 2 external-SD source profile restores LCD format/clock; mismatched shared pin triples are rejected |
| Graphics flush barrier retained | Apps can still safely reuse framebuffer/scratch memory during card operations |
| Complete PiZero rebuild | 33 standalone UF2s, 25 menu-slot images/packages and the ready-to-copy card tree all use onboard SD |
| Build and staging default to PiZero | `--board pizero` and `--board pico2` select independent output directories; the card catalog records the board |
| HardwareCheck startup report | Native serial prints the selected LCD/SD peripherals and card pins |
| FileBrowser mount error | PiZero asks for reinsertion/retry at the onboard slot; other profiles show the configured SD pins |
| Actual-driver host tests | SDK/card mocks exercise both bus modes, DMA selection, reads/writes, corrupt CRC, absent card, timeout/abort/fallback and pin guards with ASan/UBSan |

The consolidated upstream snapshot, portable game logic/assets, first-4-MB flash/save layout, RISC-V family and version-1 RPG package format are unchanged. The supplied firmware set contains only the PiZero onboard-SD builds; the Pico 2 profile remains available from source.

**Replace the older external-SD `.RPG` files** with this release's `sdcard/` contents. Package v1 identifies CPU/layout but not board wiring. A previous package can pass validation and still initialize the old SD pins. Reinstall the updated game with A instead of running an old installed image with B.

Build and host validation do not establish physical-board operation. Start with the updated HardwareCheck, then test launcher installs, saves and native USB file round trips. See [PIZERO.md](PIZERO.md) and [VALIDATION.md](VALIDATION.md).
