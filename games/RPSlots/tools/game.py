"""CHSlots: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_slots": dict(sources=["tools/tests/test_slots.cpp", "Slots.cpp"], opt="-O1",
                            includes=["lib"], args="filter")}
QUICK_ARGS = []
