"""CHBlackjack: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_rules": dict(sources=["tools/tests/test_rules.cpp", "Round.cpp"], opt="-O1", defines=[],
                            includes=["lib"], args="none")}
QUICK_ARGS = []
SKIP_SCRIPTS = ("device_", "prof", "perf_free")    # prof*.txt, perf_free.txt: a CHGAME_PROFILE build on the board
