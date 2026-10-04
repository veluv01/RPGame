"""CHMahjong: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_board": dict(sources=["tools/tests/test_board.cpp", "MahjongBoard.cpp?", "Nav.cpp?",
                                     "src/game/Layouts.cpp?"], args="none")}
QUICK_ARGS = []
