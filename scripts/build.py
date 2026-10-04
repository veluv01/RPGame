#!/usr/bin/env python3
"""Compile RPGame 0.3.1 for RP2350 RISC-V, standalone or menu-slot images."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from uf2 import FAMILIES, inspect_uf2

ROOT = Path(__file__).resolve().parents[1]
APPS = ("HardwareCheck", "RPMultiSprite", "RPStlView", "RPFileBrowser", "RPSDtoUSB", "SDLauncher")
EXAMPLES = ("HelloGraphics", "GameKit", "Fonts", "PartialUpdate", "Benchmark", "Demoscene")
PRESETS = {
    "pico2": "rp2040:rp2040:rpgame2350:arch=riscv,freq=150",
    "pizero": "rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150",
}
BOARD_DIRS = {"pico2": "rp2350-riscv", "pizero": "rp2350-pizero-riscv"}


def write_manifest(output, entries):
    (output / "builds.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS").write_text("".join(
        f'{entry["sha256"]}  {entry["name"]}.uf2\n' for entry in entries), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="*", help="app/example names; default all apps + GameHello (standalone)")
    parser.add_argument("--cli", default="arduino-cli")
    parser.add_argument("--config-file")
    parser.add_argument("--target", choices=("rp2350-riscv",), default="rp2350-riscv")
    parser.add_argument("--board", choices=tuple(PRESETS), default="pizero",
                        help="default PiZero onboard SD; Pico 2 uses the external shared-SPI module")
    parser.add_argument("--fqbn", help="override the target's board preset; UF2 family must still match --target")
    parser.add_argument("--flags", default="", help="global -D flags, e.g. -DRPGAME_LCD_CS=20")
    parser.add_argument("--games", action="store_true", help="compile all twenty games")
    parser.add_argument("--profile", choices=("standalone", "menu"), default="standalone")
    parser.add_argument("--examples", action="store_true", help="also compile the bundled graphics examples")
    parser.add_argument("--build-root", type=Path, default=Path(tempfile.gettempdir()) / "rpgame-build")
    parser.add_argument("--output", type=Path, help="default: dist/BOARD_DIRECTORY/PROFILE")
    args = parser.parse_args()
    args.fqbn = args.fqbn or PRESETS[args.board]
    args.output = args.output or ROOT / "dist" / BOARD_DIRS[args.board] / args.profile
    default_names = [name for name in APPS if args.profile == "standalone" or name != "SDLauncher"]
    if args.profile == "standalone": default_names += ["GameHello"]
    if args.examples: default_names += list(EXAMPLES)
    if args.games: default_names += sorted(p.name for p in (ROOT / "games").iterdir() if p.is_dir())
    names = args.names or default_names
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = args.output / "builds.json"
    entries = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else []
    for name in names:
        if name in APPS:
            sketch = ROOT / "apps" / name
        elif name == "GameHello":
            sketch = ROOT / "libraries/RPGame/examples/GameHello"
        elif (ROOT / "games" / name / (name + ".ino")).exists():
            sketch = ROOT / "games" / name
        elif name in EXAMPLES or name == "AdafruitGFXCompat":
            sketch = ROOT / "libraries/RPGfx/examples" / name
        else:
            parser.error(f"Unknown app/example: {name}")
        build = args.build_root.resolve() / BOARD_DIRS[args.board] / args.profile / name
        # The CLI does not track external linker-hook script content.
        # Force a relink while retaining expensive object/core caches.
        for suffix in ("elf", "bin", "uf2"):
            (build / f"{name}.ino.{suffix}").unlink(missing_ok=True)
        cmd = [args.cli, "compile", "--fqbn", args.fqbn, "--libraries", str(ROOT / "libraries"),
               "--build-path", str(build), "--warnings", "default"]
        if args.config_file:
            cmd += ["--config-file", args.config_file]
        if args.flags:
            cmd += ["--build-property", "build.extra_flags=" + args.flags]
        if args.profile == "menu" and name == "SDLauncher":
            parser.error("SDLauncher must use --profile standalone")
        offset = 0x80000 if args.profile == "menu" else 0
        length = 0x80000 if name == "SDLauncher" else 0x3f0000 - offset
        hook = ('"{runtime.tools.pqt-python3.path}/python3" -I "' + str(ROOT / "scripts/linker.py") + '"'
                ' --input "{runtime.platform.path}/lib/{build.chip}/memmap_default.ld"'
                ' --out "{build.path}/memmap_default.ld"'
                f' --offset {offset:#x} --length {length:#x}'
                ' --sub __FLASH_LENGTH__ {build.flash_length}'
                ' --sub __EEPROM_START__ {build.eeprom_start}'
                ' --sub __FS_START__ {build.fs_start} --sub __FS_END__ {build.fs_end}'
                ' --sub __RAM_LENGTH__ {build.ram_length} --sub __PSRAM_LENGTH__ {build.psram_length}')
        cmd += ["--build-property", "recipe.hooks.linking.prelink.1.pattern=" + hook, "--build-property", "build.uf2family=--family rp2350-riscv --platform rp2350 --abs-block"]
        cmd.append(str(sketch))
        print(f"Building {name} ({args.board}, {args.target}, {args.profile}) ...", flush=True)
        result = subprocess.run(cmd, text=True, encoding="utf-8", errors="replace", capture_output=True)
        log = result.stdout + result.stderr
        (args.output / f"{name}.build.log").write_text(log, encoding="utf-8")
        if result.returncode:
            raise SystemExit(log)
        firmware = build / f"{name}.ino.uf2"
        if not firmware.is_file():
            raise SystemExit(f"Compile succeeded but no UF2 found: {firmware}")
        data = firmware.read_bytes()
        try:
            inspect_uf2(data, args.target)
        except ValueError as error:
            raise SystemExit(f"Refusing to package {firmware}: {error}; check --target and --fqbn")
        target = args.output / f"{name}.uf2"
        shutil.copy2(firmware, target)
        binary = build / f"{name}.ino.bin"
        payload = binary.read_bytes()
        if len(payload) > length:
            raise SystemExit(f"{name} exceeds its reserved flash region")
        shutil.copy2(binary, args.output / f"{name}.bin")
        if args.profile == "menu":
            from package import make_package
            package = make_package(payload, name[2:] if name.startswith("RP") else name)
            (args.output / f"{name}.rpg").write_bytes(package)
        digest = hashlib.sha256(data).hexdigest()
        summary = {"name": name, "target": args.target, "uf2_family": hex(FAMILIES[args.target]),
                   "board": args.board,
                   "fqbn": args.fqbn, "profile": args.profile, "flash_origin": hex(0x10000000 + offset), "image_bytes": len(payload), "flags": args.flags, "sha256": digest,
                   "memory": [line for line in log.splitlines()
                              if line.startswith(("Sketch uses", "Global variables use"))]}
        # Keep earlier successes valid if a later app fails to compile.
        entries = [entry for entry in entries if entry["name"] != name] + [summary]
        write_manifest(args.output, entries)
        print("\n".join(summary["memory"]), flush=True)


if __name__ == "__main__":
    main()
