#!/usr/bin/env python3
"""Add Pico 2 and PiZero RPGame RISC-V aliases to Arduino-Pico 6.2.0."""
import argparse
from pathlib import Path
import re
import subprocess

BEGIN = "# BEGIN RPGAME GENERATED BOARD"
END = "# END RPGAME GENERATED BOARD"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, help="Arduino-Pico version directory")
    parser.add_argument("--cli", default="arduino-cli")
    parser.add_argument("--config-file")
    args = parser.parse_args()
    if args.core:
        core = args.core.resolve()
    else:
        cmd = [args.cli, "config", "get", "directories.data"]
        if args.config_file:
            cmd += ["--config-file", args.config_file]
        data = subprocess.check_output(cmd, text=True).strip()
        if not data:
            parser.error("Cannot find Arduino data directory; provide --core VERSION_DIRECTORY")
        versions = Path(data) / "packages/rp2040/hardware/rp2040"
        core = versions / "6.2.0"
        if not core.is_dir():
            parser.error("Install Arduino-Pico 6.2.0, or provide --core VERSION_DIRECTORY")
    source = core / "boards.txt"
    if not source.is_file():
        parser.error(f"Missing {source}")
    definitions = source.read_text(encoding="utf-8").splitlines()
    lines = []
    boards = [
        ("rpipico2", "rpgame2350", "RPGame (RP2350 RISC-V / Pico 2)", "RPGAME_RP2350"),
        ("waveshare_rp2350_pizero", "rpgame2350_pizero",
         "RPGame (RP2350 PiZero RISC-V / 4MB window)", "RPGAME_RP2350_PIZERO"),
    ]
    for original, alias, label, board_macro in boards:
        entries = [line.replace(original + ".", alias + ".", 1)
                   for line in definitions if line.startswith(original + ".") and not line.startswith(original + ".menu.arch.arm")]
        if not entries:
            parser.error(f"The selected core has no {original} definition; use Arduino-Pico 6.2.0")
        if original == "waveshare_rp2350_pizero":
            # Keep the existing package/save format inside the first 4 MB of
            # the PiZero's physical 16 MB flash. The native variant selects
            # the RP2350B package; the rest of the flash is unused by RPGame.
            entries = [line for line in entries if not line.startswith(alias + ".menu.flash.")]
            entries += [line.replace("rpipico2.", alias + ".", 1) for line in definitions
                        if line.startswith("rpipico2.menu.flash.4194304_0")]
        replacements = {
            alias + ".name": label,
            alias + ".build.board": board_macro,
            alias + ".build.usb_manufacturer": '"RPGame"',
            alias + ".build.usb_product": '"RPGame RP2350"' if alias == "rpgame2350" else '"RPGame"',
        }
        if original == "waveshare_rp2350_pizero":
            # This is the optional external activity LED on the header,
            # not the PiZero's permanently powered red LED.
            replacements[alias + ".build.led"] = "-DPIN_LED=25"
        lines += [line.split("=", 1)[0] + "=" + replacements[line.split("=", 1)[0]]
                  if line.split("=", 1)[0] in replacements else line for line in entries]
        lines.append("")
    target = core / "boards.local.txt"
    previous = target.read_text(encoding="utf-8") if target.exists() else ""
    previous = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?",
                      "", previous, flags=re.S)
    if any(line.startswith(("rpgame.", "rpgame2350.", "rpgame2350_pizero.")) for line in previous.splitlines()):
        parser.error("An unrelated RPGame definition already exists in boards.local.txt")
    target.write_text(previous.rstrip() + "\n\n" + BEGIN + "\n" + "\n".join(lines) + "\n" + END + "\n", encoding="utf-8")
    print(f"Installed {target}")
    print("Restart Arduino IDE; select the RPGame Pico 2 or PiZero board.")
    print("RP2350 RISC-V FQBN: rp2040:rp2040:rpgame2350:arch=riscv,freq=150")
    print("PiZero RISC-V FQBN: rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150")


if __name__ == "__main__":
    main()
