# Source and license notices

This port retains Kevin Bates's upstream code, assets and copyright notices. Exact source snapshots are listed in `upstream.json`. The project has per-component terms rather than a single license that overrides its dependencies.

| Component | Terms and notice location |
|---|---|
| RPGfx graphics code and RP2040 transport | MIT; `libraries/RPGfx/LICENSE` |
| RPGfx bundled fonts/layouts | Original third-party notices in that LICENSE, `LICENSE.Apache-2.0` and `FONTS.md` |
| Consolidated RPGame library and twenty games | Apache-2.0; `libraries/RPGame/LICENSE`, NOTICE, and each game LICENSE/NOTICE |
| Prior RPGame helper contributions | MIT; `libraries/RPGame/LICENSE.legacy-MIT` |
| RPMultiSprite source | MIT; `apps/RPMultiSprite/LICENSE` |
| RPStlView source/tools | MIT; `apps/RPStlView/LICENSE` |
| Portable CHSd FAT reader | MIT; `libraries/RPGameSD/LICENSE.CHSD-MIT`, NOTICE.CHSD |
| RPGameSD FAT wrapper and block driver | GPL-3.0; `libraries/RPGameSD/LICENSE` and source headers (William Greiman, SparkFun, upstream contributors) |
| RPSDtoUSB and its adapter | GPL-3.0; `apps/RPSDtoUSB/LICENSE` |
| RPFileBrowser source | Upstream FileBrowser snapshot has no standalone LICENSE; original headers are preserved. This port does not add an upstream redistribution grant. |
| New RP backends, launcher, build/test scripts and HardwareCheck sketch | MIT; `LICENSE.port` |

Apps linking RPGameSD include GPL-licensed code; their upstream MIT files retain their own notices. Preserve applicable dependency terms when distributing a combined firmware. The walker sample assets are preserved from CHMultiSprite; retain their upstream attribution.

Arduino-Pico and its toolchain are external build dependencies, available at [arduino-pico 6.2.0](https://github.com/earlephilhower/arduino-pico/tree/6.2.0). Its core/dependency notices remain applicable to firmware binaries; see [the core license](https://github.com/earlephilhower/arduino-pico/blob/6.2.0/LICENSE) and included dependency license files. The RPGame board installer generates configuration only and does not redistribute or modify core implementation files.

The optional Adafruit graphics bridge requires the external [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) and [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO), under their upstream terms. Their sources are not vendored here.

Binary dependency license texts are copied into `licenses/`. The core and compiler source remain external dependencies; the tested versions and complete RPGame source are included in this project.
