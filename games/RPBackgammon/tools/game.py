"""CHBackgammon: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

import re

# The pure-logic sources, compiled beside the tests as they are. With the
# match sources present (the trained network and its tables), the whole
# match is tested too.
_MATCH = ["Net.cpp", "src/ai/NetData.cpp", "Race.cpp", "src/ai/RaceData.cpp", "Ai.cpp",
          "Cube.cpp", "src/ai/MetData.cpp", "Match.cpp", "Notation.cpp", "lib/rpgame/Fmt.cpp"]


def TESTS():
    from pathlib import Path
    here = Path(__file__).resolve().parent
    game = here.parent
    sources = ["tools/tests/test_backgammon.cpp", "Rules.cpp"]
    defines = ["CHTEST"]
    if (here / "tests" / "test_match.h").exists() and all((game / m).exists() for m in _MATCH if not m.startswith("lib/")):
        sources += _MATCH
        defines.append("WITH_MATCH")
    return {"test_backgammon": dict(sources=sources, defines=defines, includes=["lib"])}


SKIP_SCRIPTS = ()           # the device_* scripts run in the simulator too
ECHO = ("THINK",)


def after_host_tests(ctx, stdout):
    """The network's evaluation of the opening position: the host tests and
    the simulator must agree."""
    host = re.search(r"worth (-?\d+) to", stdout)
    probe = ctx.out / "net.txt"
    probe.write_text("say E\nwait 1\n")
    good, text = ctx.drive(probe, ctx.out / "net")
    sim = re.search(r"NET (-?\d+) ", text)
    if not (good and host and sim and host.group(1) == sim.group(1)):
        ctx.log(f"FAIL: the network says {host and host.group(1)} on the host, {sim and sim.group(1)} in the simulator")
        return False
    ctx.log(f"opening position: {sim.group(1)} in both")
    return True
