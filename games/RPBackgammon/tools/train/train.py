"""Build and run the CPU's trainer (tools/train/train.cpp).

    python tools/train/train.py [-H 16] train out/net16.bin --games 300000
    python tools/train/train.py [-H 16] bench int:out/net16.bin heur --games 10000
    python tools/train/train.py [-H 16] export out/net16.bin src/ai/NetData.cpp
    python tools/train/train.py race src/ai/RaceData.cpp      (fit the race table; tools/train/race.cpp)

-H is the network's hidden size (NET_H), -S the first table's scale (NET_W1_SCALE, 32);
everything after them goes to the tool.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools") / "chsim"))  # the repository's tools/chsim (find_cxx)
from chsim import find_cxx  # noqa: E402

SOURCES = [HERE / "train.cpp", ROOT / "Rules.cpp", ROOT / "Net.cpp", ROOT / "Race.cpp",
           ROOT / "src" / "ai" / "RaceData.cpp", ROOT / "Ai.cpp"]
RACE = [HERE / "race.cpp", ROOT / "Rules.cpp", ROOT / "Race.cpp"]
LIB = ROOT.parents[2] / "src"     # the RPGame library (rpgame/RamFunc.h)


def compile_(exe, sources, flags):
    exe.parent.mkdir(exist_ok=True)
    newest = max(p.stat().st_mtime for p in list(ROOT.glob("*.h")) + sources)
    if exe.exists() and exe.stat().st_mtime > newest:
        return exe
    cmd = find_cxx() + ["-std=gnu++17", "-O3", "-march=native", "-ffast-math", "-Wall", "-Wno-unknown-pragmas",
                        "-I", str(LIB), *flags, *[str(s) for s in sources], "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    return exe


def build(hidden, scale, extra=()):
    tag = "".join("_" + e[2:].replace("=", "") for e in extra)
    return compile_(HERE / "build" / f"train{hidden}_{scale}{tag}.exe", SOURCES,
                    ["-DNET_HOST", f"-DNET_H={hidden}", f"-DNET_W1_SCALE={scale}", *extra])


def main():
    args = sys.argv[1:]
    hidden, scale = 16, 32
    extra = []
    while args[:1] in (["-H"], ["-S"], ["-D"]):
        if args[0] == "-H":
            hidden = int(args[1])
        elif args[0] == "-S":
            scale = int(args[1])
        else:
            extra.append("-D" + args[1])            # e.g. -D AI_SLIPS=6: a constant to try out
        args = args[2:]
    if args[:1] == ["race"]:
        exe = compile_(HERE / "build" / "race.exe", RACE, ["-DRACE_HOST"])
        raise SystemExit(subprocess.run([str(exe), *args[1:]]).returncode)
    raise SystemExit(subprocess.run([str(build(hidden, scale, extra)), *args]).returncode)


if __name__ == "__main__":
    main()
