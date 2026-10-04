"""CHWords: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

TESTS = {"test_words": dict(sources=["tools/tests/test_words.cpp", "Words.cpp", "Game.cpp",
                                     "Ai.cpp", "FlashDict.cpp", "src/dict/DictData.cpp"],
                            includes=["lib"], cwd="game")}
ECHO = ("THINK", "DICT", "WORD")
CARD = dict(scripts="card", file="sdcard/WORDS.DIC", build=["tools/dict/build_sd.py"])
