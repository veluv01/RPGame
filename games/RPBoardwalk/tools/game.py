"""CHBoardwalk: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_rules": dict(sources=["tools/tests/test_rules.cpp", "Game.cpp?", "Tiles.cpp?",
                                     "Cpu.cpp?"], includes=["lib"])}
QUICK_ARGS = ["quick"]
SKIP_SCRIPTS = ("device_", "pace")        # pace.txt: frame pacing on the board (the debug build)
