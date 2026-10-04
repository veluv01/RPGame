# Waveshare RP2350-PiZero — onboard SD bring-up

RPGame 0.3.1 uses the **onboard microSD slot** and the same external ST7735S LCD, eight buttons and piezo as before. The display and controls retain their GPIO assignments. **The PiZero's 40-pin header has different physical pin numbers from Pico 2**.

Waveshare specifies an RP2350B, 16 MB onboard NOR flash and optional PSRAM solder pads. The RPGame PiZero profile selects the native RP2350B variant and RISC-V at 150 MHz, with no PSRAM or internal filesystem. It uses the first **4 MB flash window** for the existing app/save/metadata layout; the remaining flash is unused by this release.

Sources checked 3 October 2026: [Waveshare product](https://www.waveshare.com/product/rp2350-pizero.htm), [wiki](https://www.waveshare.com/wiki/RP2350-PiZero), and [schematic](https://files.waveshare.com/wiki/RP2350-PiZero/RP2350-PiZero.pdf). Board-specific firmware has compiled and passed image checks; physical testing is pending.

## Header wiring for this profile

Use the physical numbers printed for the 40-pin header, with pin 1 identified from the board marking. Do not use the Pico/Pico 2 physical-pin column in the root README. The GPIO column below is the RP2350 GPIO number used by firmware; Raspberry Pi SPI/I2C function labels do not select this MCU's alternate functions.

| Peripheral signal | RP2350 GPIO | PiZero physical header pin |
|---|---:|---:|
| LCD SCK | GP2 | 3 |
| LCD MOSI/SDA | GP3 | 5 |
| LCD CS | GP5 | 29 |
| LCD DC | GP6 | 31 |
| LCD RESET | GP7 | 26 |
| Piezo signal | GP9 | 21 |
| UP | GP10 | 19 |
| DOWN | GP11 | 23 |
| LEFT | GP12 | 32 |
| RIGHT | GP13 | 33 |
| A | GP14 | 8 |
| B | GP15 | 10 |
| SELECT | GP16 | 36 |
| START | GP17 | 11 |
| Optional external activity LED | GP25 | 22 |
| 3.3 V supply | 3V3 | 1 or 17 |
| Common ground | GND | 6, 9, 14, 20, 25, 30, 34 or 39 |

Buttons connect GPIO to GND with firmware pull-ups. Use 3.3 V logic and retain the display backlight/piezo drive circuitry appropriate to your peripherals. An optional activity LED uses a series resistor, such as 330 ohms, between GP25 and its anode, with the cathode to GND. The board's red LED is a power indicator and will not blink under software control. A missing external activity LED does not prevent testing.

Leave HDMI disconnected during this test: GP6 and GP7 also connect to its auxiliary signals. The display remains the external ST7735S.

SPI0 RX GP4 (header pin 7) is reserved by the LCD profile but needs no wire for this write-only display. GP8 (header pin 24) is no longer used for SD chip select. An external SD module is unnecessary.

## Onboard SD slot

The following connections already exist on the PCB. Insert the card in the board's slot; no SD jumpers or soldering are required.

| SD signal in SPI mode | RP2350 GPIO | Firmware setting |
|---|---:|---|
| CLK / SCK | GP30 | `RPGAME_SD_SPI_SCK` |
| CMD / MOSI | GP31 | `RPGAME_SD_SPI_MOSI` |
| DAT0 / MISO | GP40 | `RPGAME_SD_SPI_MISO` |
| DAT3 / CS | GP43 | `RPGAME_SD_CS` |
| DAT1 | GP41 | Unused in SPI mode |
| DAT2 | GP42 | Unused in SPI mode |

The card uses **SPI1** and the LCD uses **SPI0**. GP41 is not chip select. The card signals are internal board connections, not GPIOs on the ordinary 40-pin header. Use FAT16 or FAT32; exFAT is unsupported. The driver initializes the card at a low clock, then requests up to 24 MHz; lower `RPGAME_SD_MAX_HZ` globally if your hardware tests show errors.

## USB and first upload

Use the **native programming USB-C connector**, not the separate PIO-USB connector. The schematic names the native connector J4 and PIO-USB J2. Native USB is the port used for BOOT programming, CDC serial and RPSDtoUSB mass storage. PIO-USB is not configured in this release.

1. Hold BOOT while connecting native USB, or hold BOOT and press/release RUN, then release BOOT once the ROM drive appears.
2. Copy `dist/rp2350-pizero-riscv/standalone/HardwareCheck.uf2` to the drive. Check the LCD, every button, piezo and onboard SD read. The activity blink requires an external LED. Native CDC serial reports `LCD SPI0; SD SPI1 SCK=30 MOSI=31 MISO=40 CS=43` at startup.
3. Format the card FAT16 or FAT32 and copy the contents of `sdcard/` to its root. Insert it in the onboard slot. `/GAMES`, the dictionaries, puzzle banks, sprite file and `/MODELS` should be directly under the root.
4. Re-enter ROM download mode and copy `dist/rp2350-pizero-riscv/standalone/SDLauncher.uf2`. Install a game with A; hold START for three seconds to return to the launcher.
5. `dist/rp2350-pizero-riscv/standalone/RPSDtoUSB.uf2` is a board-specific standalone USB-card test. Uploading it replaces the resident menu; restore SDLauncher afterward. The same USB app is also available as an RPG package in the menu.

All **33 standalone UF2s and 25 menu packages** in this release are rebuilt for the native PiZero board and onboard SD. Replace old external-SD `.RPG` files when preparing the card. A version-1 package header checks CPU family/layout, not board wiring: earlier packages may pass those checks but still address the external SD pins. If the launcher offers B to run a previously installed image, install the updated title with A first.

Card operations retain a graphics flush barrier because some apps reuse the framebuffer for card data. Separate SPI peripherals remove the shared register/clock handoff; they do not promise simultaneous LCD/SD rendering or measured speed gains.

## Rebuilding the PiZero profile

Install Arduino-Pico 6.2.0 and run the root board installer. It adds a PiZero alias with the native variant, RISC-V only and a fixed 4 MB/no-FS logical window:

```sh
python3 scripts/install_board.py
python3 scripts/build.py --board pizero --games --examples
python3 scripts/build.py --board pizero --games --profile menu
python3 scripts/sdcard.py --board pizero
python3 scripts/test_sd_transport.py
python3 scripts/verify_release.py
```

The FQBN is `rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150`. The stock Waveshare core entry uses a full 16 MB flash layout; use the RPGame alias for this release's reserved save/launcher regions and automatic onboard-SD selection. SDLauncher itself always uses the standalone profile. For a smaller bring-up build, pass `HardwareCheck SDLauncher RPSDtoUSB` before `--board pizero`.

The alias defines `ARDUINO_RPGAME_RP2350_PIZERO`, which defaults `RPGAME_PIZERO_ONBOARD_SD` to 1. To deliberately build the older external-SD wiring for this board, use global `--flags="-DRPGAME_PIZERO_ONBOARD_SD=0"` and separate output/card folders; rebuild both the launcher and all menu images consistently. Do not mix those packages with the supplied onboard-SD files.

No physical PiZero was attached during preparation. Builds and host driver checks passed; start with HardwareCheck before exercising flash installs, saves, audio, SD stress or USB file round trips. See [VALIDATION.md](VALIDATION.md) for the exact scope and remaining hardware checks.
