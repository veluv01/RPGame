"""CHYacht: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {
    "test_yacht": dict(sources=["tools/tests/test_yacht.cpp", "Yacht.cpp"], opt="-O1", args="filter"),
    "test_dice": dict(sources=["tools/tests/test_dice.cpp", "Dice3D.cpp"], opt="-O1", args="filter"),
}
QUICK_ARGS = []
SIM_TESTS = ["tools/tests/sim_save.py"]
