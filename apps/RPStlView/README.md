# RPStlView 2.0 for RP2350 RISC-V

Build and wiring instructions are in the [project README](../../README.md). Copy the supplied card tree, or put the binary STL files from `sample/` on a FAT16/FAT32 card. The new browser supports folders, fragmented reads and card errors. Export ASCII STL as binary first.

| Mode | Button | Action |
|---|---|---|
| File list | UP / DOWN | Move selection |
| File list | LEFT / RIGHT | Move a page |
| File list | A / B | Open / parent folder |
| File list | START | Rescan card |
| Viewer | D-pad | Rotate and stop spin |
| Viewer | A / B held | Zoom in/out |
| Viewer | A + B | Reset view |
| Viewer | SELECT | Switch X-RAY / FRONT mode |
| Viewer | START tap | Toggle spin |
| Viewer | START held half a second | Return to file list |
| Any screen | START held three seconds | Reset to resident menu; restart when standalone |

The viewer includes the updated fixed-point renderer, HUD, card checks, effects and sounds. It streams geometry into the 4 bpp framebuffer. The eight edge shades use palette entries 8–15.

`python3 tools/preview.py sample/TORUS.STL /tmp/torus.gif` makes a host-rendered preview using a C++ compiler and Pillow. Root `scripts/test_host.py` checks all six supplied models and seven generated edge cases. The shared simulator can run this app against a FAT card image; a model-open/render check passed. Host results do not measure RP2350 speed or physical SD/LCD behavior.

Sources: consolidated [CHGame StlView](https://github.com/bateske/CHGame), with the existing RPGame SD/graphics transport. MIT notices are retained in `LICENSE` and source files.
