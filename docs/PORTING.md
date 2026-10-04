# Port architecture

RPGame 0.3.1 targets RP2350 RISC-V using Arduino-Pico 6.2.0. Portable CHGame logic and art sit above RP-specific GPIO input, PWM/timer audio, flash saves and watchdog/ROM reset handling. RPGfx's RP SPI/DMA backend and the RPGameSD CRC/DMA driver are reused by the apps and games.

On the PiZero, LCD uses SPI0 (GP2/3/4) and the onboard SD socket uses SPI1 (SCK GP30, MOSI GP31, MISO GP40, CS GP43). The RPGame PiZero board macro selects this profile for every compiled library and sketch. SD pin setup, registers and DMA DREQs use the card's selected peripheral. Dedicated SD transactions leave the LCD's registers and CS untouched. The SD driver owns its CS initialization.

The Pico 2 source profile still shares SPI0 between LCD and external SD. Shared transactions capture and restore the bus registers, including the LCD's 16-bit format and clock. A shared peripheral cannot use different pin triples. Both paths retain `gfx_wait()` before card operations: legacy apps reuse graphics scratch/framebuffer storage for SD data, so separate peripherals do not by themselves make overlapping access to that memory safe.

RPSDtoUSB holds the native core USB mutex for main-loop peripheral/CDC work; USB callbacks use full-sector caches and complete their SD transactions before returning. SCSI command observers feed the updated monitor without counting each 64-byte USB packet as a command. The onboard driver keeps CRC checking, DMA timeouts, the RP2350 E5-safe channel-abort sequence and polled fallback.

The portable Fat reader returns extent lists and supports FAT16/FAT32 short names, folders and fragmented files. The writable legacy SD wrapper remains available for FileBrowser/USB and sprite apps. exFAT is rejected with an app/menu message.

Time-critical drawing sections use `.time_critical.rpgame.*` and `.time_critical.rpgame_app.*`. The ISA match with CH32 does not authorize WCH register addresses or linker sections; those paths have been replaced. Debug serial transport uses Arduino CDC; WCH crash/stack painting is unavailable.

See LAUNCHER.md for the bounded flash maps and package format. APIs preserve CHGAME_* and per-game config names where useful to upstream scripts. Source snapshots and component notices are in upstream.json and LICENSES.md.
