#!/usr/bin/env python3
"""Run drawing, USB sector-adapter and STL projection checks on a host PC."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(cmd, env=None):
    subprocess.run([str(x) for x in cmd], check=True, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--sanitize", action="store_true", help="enable address/undefined checks for USB and STL")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")  # leak tracer cannot inspect /proc in this runtime
    cxx = shlex.split(args.cxx)
    flags = ["-std=c++17", "-O2"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = dict(os.environ, CHSIM_CXX=args.cxx, PYTHONDONTWRITEBYTECODE="1")
    run([sys.executable, ROOT / "libraries/RPGfx/extras/sim/chsim.py", "test"], env)
    with tempfile.TemporaryDirectory(prefix="rpgame-tests-") as temporary:
        out = Path(temporary)
        usb = out / "usb-tests"
        run(cxx + flags + ["-I", ROOT / "tests/usb_host", ROOT / "tests/test_usb.cpp",
                          ROOT / "apps/RPSDtoUSB/src/usb/UsbMsc.cpp", "-o", usb])
        run([usb])
        input_test = out / "input-tests"
        run(cxx + flags + ["-DCHSIM", "-I", ROOT / "tools/chsim/host", "-I", ROOT / "libraries/RPGfx/src", "-I", ROOT / "libraries/RPGame/src", ROOT / "tests/test_input.cpp", ROOT / "libraries/RPGame/src/rpgame/Input.cpp", "-o", input_test])
        run([input_test])
        storage = out / "storage-tests"
        run(cxx + flags + ["-DCHSIM", "-I", ROOT / "tools/chsim/host", "-I", ROOT / "libraries/RPGfx/src", "-I", ROOT / "libraries/RPGame/src", ROOT / "tests/test_storage.cpp", ROOT / "libraries/RPGame/src/rpgame/Flash.cpp", ROOT / "libraries/RPGame/src/rpgame/Package.cpp", ROOT / "libraries/RPGame/src/rpgame/Save.cpp", "-o", storage])
        run([storage])
        accuracy = out / "stl-accuracy"
        run(cxx + flags + ["-DSTL_TEST_HOOK", "-DCHTEST", "-I", ROOT / "libraries/RPGame/src", ROOT / "apps/RPStlView/tools/sim/accuracy.cpp",
                          ROOT / "apps/RPStlView/StlRender.cpp", "-o", accuracy])
        edges = out / "edgecases"
        run([sys.executable, ROOT / "apps/RPStlView/tools/sim/make_edge_cases.py", edges], env)
        for model in sorted((ROOT / "apps/RPStlView/sample").glob("*.STL")) + sorted(edges.glob("*.STL")):
            print(f"Checking {model.name}", flush=True)
            run([accuracy, model])
    print("All host checks passed. Hardware SPI/USB behavior requires a board.")


if __name__ == "__main__":
    main()
