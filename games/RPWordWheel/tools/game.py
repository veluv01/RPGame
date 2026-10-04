"""CHWordWheel: what the shared tools need to know (the schema is in
the repository's tools/gamecfg.py; `rpgame test`, `rpgame check`,
`rpgame redraw` read this)."""

_GAME = ["Cpu.cpp", "Puzzle.cpp", "Show.cpp", "Spin.cpp", "Wedges.cpp", "lib/rpgame/Fmt.cpp"]
TESTS = {
    "test_show": dict(sources=["tools/tests/test_show.cpp", *_GAME], includes=["lib", "chsd", "chsd-host"],
                      cwd="game", optional=True),
    "test_bank": dict(sources=["tools/tests/test_bank.cpp", *_GAME, "FlashBank.cpp", "SdBank.cpp",
                               "src/bank/BankData.cpp", "chsd/Fat.cpp"], includes=["lib", "chsd", "chsd-host"],
                      cwd="game", optional=True),
}
QUICK_ARGS = []


def before_check(ctx):
    """The puzzle banks are rebuilt from tools/phrases/phrases.txt (every
    puzzle checked and wrapped) and must come out as committed."""
    data = ctx.game / "src" / "bank" / "BankData.cpp"
    before = data.read_bytes() if data.exists() else b""
    r = ctx.run(["tools/phrases/build_bank.py"])
    ctx.log((r.stdout + r.stderr).strip())
    if r.returncode:
        return False
    if data.read_bytes() != before:
        ctx.log("   note: src/bank/BankData.* changed (phrases.txt or the byte budget did)")
    return True


CARD = dict(scripts={"card"}, file="sdcard/PHRASES.BNK")
BUILD_REQUIRE = ["save pages free: 2"]      # the release build must leave both save pages


def before_tests(ctx):
    r = ctx.run(["tools/phrases/build_bank.py"])
    if r.returncode:
        raise SystemExit(r.stdout + r.stderr)
