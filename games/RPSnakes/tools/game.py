"""CHSnakes: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_rules": dict(sources=["tools/tests/test_rules.cpp", "Game.cpp?", "Layout.cpp?",
                                     "Cpu.cpp?"], includes=["lib"])}
QUICK_ARGS = ["quick"]
ECHO = ("THINK",)
