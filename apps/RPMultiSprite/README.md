> RPGame 0.3 uses RP2350 RISC-V. See [project setup](../../README.md) for the current firmware paths, GPIO map and launcher. Legacy measurements below are not RP hardware test results.

# RPMultiSprite

Build using the root RPGame instructions. Copy `sample/WALK.BIN` to `/WALK.BIN` on a FAT16/FAT32 card. Its frames and `src/WalkerData.h` are a matched asset set. The SD sheet path expects the file to be contiguous; copy it to a card with adequate contiguous free space.

| Button | Action |
|---|---|
| A / B | Add / remove one walker |
| UP / DOWN | Add / remove eight walkers |
| SELECT | Cycle SD-EACH, SD-BATCH, SD-SHEET and FLASH modes |
| START | Start/stop automatic performance sweep |

The HUD and USB serial report measured draw/LCD time and frame rate. Run the sweep on RP2040/RP2350 to establish its results; upstream CH32 benchmarks are not used as RP2040/RP2350 measurements.

The optional asset builder accepts `--src DIR` containing `MOOG.ZIP`, `MUSHBOOM.ZIP`, `Moog.h` and `Mushboom.h`, from the original SpriteSource collection. That collection is not included. Existing `WALK.BIN` and the flash frames are ready to use.

Derived from [bateske/CHMultiSprite](https://github.com/bateske/CHMultiSprite); source snapshot in the root `upstream.json`.
