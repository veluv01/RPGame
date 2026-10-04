# Consolidated CHGame update → RPGame 0.3

Upstream: `bateske/CHGame`, pinned to `e876774c3a079b0362e1f0fc8494ade491bdbe29` on 3 October 2026. The six previously supplied standalone repositories remain the older app/transport snapshots. The consolidated repository is the source for the new game library, twenty games, CHSd FAT reader and current STL/USB app layers.

| Upstream change / dependency | RPGame implementation |
|---|---|
| Shared CHGame library | Imported portable palettes, draw/mask, formatting, easing, shake and Sizzle effects; RPGame class/global names |
| Snapshot input, repeat, lockstep, 60 Hz accumulator | Configurable RP GPIO; legacy helper methods retained; elapsed-time START exit |
| Piezo effects, melody, Playtune score, LED | RP PWM on the existing buzzer pin; 1 kHz SDK timer; bounded effect IDs and prior IRQ-state restoration |
| CRC flash saves | Two separate 4 KiB sectors, reserved by build maps; upstream 256-byte record format retained |
| CHSd FAT16/FAT32 reader and extent lists | Portable reader retained; sd:: API wraps existing RP CRC/DMA shared-SPI transport |
| Twenty casino/board/word games | Game logic, art and data imported; includes, names, RAM sections and tool paths ported |
| StlView 2.0 | Folder browser, richer viewer/HUD, card errors, shared agent-style UI and effects; new fixed-point renderer retained |
| SDtoUSB 2.1 | Agent-style UI, filesystem monitor, event/history/stat panels, read-only toggle, hot-card handling and retries; native RP TinyUSB transport retained |
| USB monitor command history | Whole SCSI command observers span packet callbacks; sector caches still issue complete 512-byte card operations |
| WCH bootloader/CHG files | Replaced by resident RP menu, relocated SDK images and family/layout-checked RPG packages |
| Host tools | Shared simulator and game tests use the RPGame tree; graphics/SD APIs execute their portable implementations |
| WCH crash capture/stack watermark | Unavailable; serial protocol keeps compatibility fields, documented as unmeasured |

Local defects found during testing: Boardwalk's optional initial deal is bounded by the available deeds, its fuzz setup is zero initialized, and Tic-Tac-Toe's iso/score/RNG fields have safe defaults. Poker's rare stud split is covered by a deterministic showdown fixture rather than requiring one in a finite random sample. WordWheel builds its puzzle-bank references before testing. These changes are included in the source.

No PCB redesign is included. Peripheral parts and the RPGame 0.2 GPIO map are retained. RP2040/ARM binaries are not refreshed in this RISC-V release. WCH hardware-only probes and the WCH bootloader/updater are not bundled as executable RP tools.

The initial PiZero bring-up added a native RP2350B board alias, three board-specific test images and a 40-pin wiring guide. [RPGame 0.3.1](UPDATE_0.3.1.md) adds onboard SPI1 SD support and rebuilds every game/app for that board. Physical flash is 16 MB; the existing 4 MB logical storage layout and version-1 package format remain in use.
