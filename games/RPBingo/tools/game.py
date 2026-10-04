"""CHBingo: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_bingo": dict(sources=["tools/tests/test_bingo.cpp", "Bingo.cpp"], args="none",
                            reference=dict(module="ref_bingo", func="dump_lines"))}
QUICK_ARGS = []
SIM_TESTS = ["tools/tests/sim_save.py"]
