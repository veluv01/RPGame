"""CHCheckers: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_checkers": dict(sources=["tools/tests/test_checkers.cpp", "Match.cpp?"], args="none")}
QUICK_ARGS = []
ECHO = ("THINK", "RPROF")
