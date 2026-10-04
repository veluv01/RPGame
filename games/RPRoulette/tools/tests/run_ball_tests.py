"""Build and run the ball simulation tests (Ball.cpp).

    python tools/tests/run_ball_tests.py            all checks + duration histograms
    python tools/tests/run_ball_tests.py --stats    tuning tables only (4,000 spins)
    python tools/tests/run_ball_tests.py --trace SEED N QUICK TARGET

Built with zig c++ under UBSan (stop at the first error); clang does not know
#pragma GCC optimize, hence -Wno-unknown-pragmas. The exe is
build/test_ball.exe; tools/spin_preview.py reuses it for --trace.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LIB = (next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "libraries") / "RPGame" / "src"   # the RPGame library
SOURCES = [HERE / "test_ball.cpp", ROOT / "Ball.cpp", LIB / "rpgame" / "Ease.cpp"]
EXE = HERE / "build" / "test_ball.exe"


def cxx():
    """$CHSIM_CXX, else zig on the PATH, else a zig kept beside the workspace
    (CH32Sound/.work/zig), else what tools/chsim's find_cxx() finds."""
    if not os.environ.get("CHSIM_CXX") and not shutil.which("zig"):
        work = ROOT.parents[2] / "CH32Sound" / ".work" / "zig"
        for z in sorted(work.glob("zig-*/zig.exe")) + sorted(work.glob("zig-*/zig")):
            return [str(z), "c++"]
    sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools") / "chsim"))  # the repository's tools/chsim (find_cxx)
    from chsim import find_cxx  # noqa: E402
    return find_cxx()


def build():
    EXE.parent.mkdir(exist_ok=True)
    cmd = cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unknown-pragmas",
                   "-fsanitize=undefined",
                   "-fno-sanitize-recover=undefined", "-DCHTEST", f"-I{LIB}",
                   *[str(s) for s in SOURCES], "-o", str(EXE)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
        raise SystemExit("warnings: fix them")
    return EXE


def main():
    exe = build()
    raise SystemExit(subprocess.run([str(exe), *sys.argv[1:]]).returncode)


if __name__ == "__main__":
    main()
