"""CHCrossword: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

import cwtests          # tools/tests/cwtests.py: the reference decoder's expectations, the card tests

TESTS = {
    "test_crossword": dict(sources=["tools/tests/test_crossword.cpp", "tools/tests/nocard.cpp", "Puzzle.cpp",
                                    "Game.cpp", "src/game/PuzzleData.cpp", "Pack.cpp", "chsd/Fat.cpp"],
                           includes=["lib", "chsd"], args="none"),
    "test_card": dict(sources=["tools/tests/test_card.cpp", "Puzzle.cpp", "src/game/PuzzleData.cpp",
                               "Pack.cpp", "chsd/Fat.cpp"], includes=["lib", "chsd"], run=False),
}
QUICK_ARGS = ["--quick"]        # the card tests: FAT16 images only (a FAT32 image is 34 MB)


def before_tests(ctx):
    cwtests.write_expect(ctx.game / "tools" / "tests" / "build" / "expect.h")


def after_tests(ctx, exes):
    return cwtests.card_tests(exes["test_card"], ctx.quick)


PRE_STEPS = [("puzzles", ["tools/puzzles/build_pack.py", "--check"])]
CARD = dict(scripts="card_", file=cwtests.make_card)     # out/card.img: BONUS and EXTRA packs
