"""CHFour: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_four": dict(sources=["tools/tests/test_four.cpp", "Rules.cpp", "Ai.cpp",
                                    "Game.cpp", "Taunt.cpp"],
                           defines=["CHTEST", "CHSIM"], includes=["lib"])}
ECHO = ("THINK", "BOARD")
