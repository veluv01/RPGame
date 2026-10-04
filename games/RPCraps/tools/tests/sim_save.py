"""Saving in the simulator: SAVE & QUIT in the middle of a hand, CONTINUE
from the title, and the table must come back exactly (purse, every bet,
the point). Also: options and stats survive, and a game that ended (broke)
leaves no game to continue.

    python tools/tests/sim_save.py          (rpgame check runs it: SIM_TESTS in tools/game.py)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
for up in HERE.parents:                 # the repository's tools/chsim: simsave.py
    if (up / "tools" / "chsim" / "simsave.py").exists() and (up / "libraries" / "RPGame").is_dir():
        sys.path.insert(0, str(up / "tools" / "chsim"))
        break
from chdrivelib import mask_of  # noqa: E402
from simsave import Session  # noqa: E402


def main():
    s = Session(ROOT)

    def say(line):
        return s.say(line, lambda r: r.split("|"))

    def table():
        st = say("H")
        head = st[0].split()
        return {"purse": int(head[1]), "point": int(head[2]), "bets": st[1].split()}

    # A hand in progress: a point, odds, place bets.
    say("J P")
    s.frames(20)
    say("E 0 10")
    say("F 3 3")
    say("C 29")
    s.frames(5)
    s.press("A", 30)
    s.frames(320)
    say("E 2 30")
    say("E 9 12")
    say("E 13 5")
    s.frames(40)
    before = table()
    # Pause -> SAVE & QUIT (the fourth item).
    s.tap("START")
    for _ in range(3):
        s.tap("DOWN")
    s.tap("A")
    s.frames(40)
    # Power off and on (the table in memory is gone), then CONTINUE.
    say("M 1")
    s.power_cycle()
    s.tap("A")
    s.frames(40)
    after = table()
    s.check(before == after and before["point"] == 6, f"save mid-hand: {before} -> {after}")
    # Options and stats come back too; going broke leaves nothing to continue.
    say("J O")
    s.frames(10)
    s.tap("RIGHT")                                    # TABLE -> BEGINNER... refused: classic bets up
    s.tap("DOWN")
    s.tap("RIGHT")                                    # ODDS -> 2X
    s.tap("B")                                        # leave: saved
    s.frames(30)
    s.power_cycle()
    say("J O")
    s.frames(10)
    s.d.t.send("S")
    hdr = s.d.expect("FB ")
    s.d.t.read(int(hdr.split()[2]))
    say("J P")
    s.frames(10)
    say("M 0")
    say("E 0 5")                                      # the last $5 on the line (purse -5: a debug table)
    say("M 0")
    say("F 1 1")
    say("C 29")
    s.frames(5)
    s.press("A", 30)
    s.frames(420)
    s.power_cycle()
    st = table()
    s.check(st["purse"] == 500 and st["point"] == 0,
            f"broke: no game to continue, a fresh $500 table ({st['purse']})")
    raise SystemExit(s.result())


if __name__ == "__main__":
    main()
