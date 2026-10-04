"""Saving in the simulator: SAVE & QUIT in the middle of a turn, CONTINUE
from the title, and the game must come back exactly (purse, whose turn, the
dice on the table, the held ones, every card). Also: a finished game leaves
nothing to continue but keeps the purse.

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
from simsave import Session  # noqa: E402


def main():
    s = Session(ROOT)
    say = s.say

    def idle():
        for _ in range(4000):
            st = say("H").split("|")[0].split()
            if st[5] == "0" and st[6] == "0":
                return
            s.frames(4)
        raise SystemExit("the game never came to rest")

    def roll():
        s.press("A", 30)
        s.frames(10)
        idle()
        s.frames(30)

    # Versus the dealer: one full round each, then mid-turn with two dice held.
    say("R 99")
    say("J C")
    s.frames(20)
    say("F 2 2 2 5 6")
    roll()
    say("G 1")                                        # twos: 6
    idle()                                            # the dealer's whole turn
    s.frames(40)
    say("F 6 6 1 3 4")
    roll()
    s.tap("A")                                        # hold the first die
    s.tap("RIGHT")
    s.tap("A")                                        # and the second
    s.frames(10)
    before = say("H")
    held = before.split("|")[1].split()[5]
    # Pause -> SAVE & QUIT (the fourth item).
    s.tap("START")
    for _ in range(3):
        s.tap("DOWN")
    s.tap("A")
    s.frames(40)
    # Power off and on (the game in memory is gone), then CONTINUE.
    s.power_cycle()
    s.tap("A")
    s.frames(40)
    after = say("H")

    def game(st):                                     # all but the cursor
        a, b, c = st.split("|")
        return a.split()[1:5], b, c

    s.check(game(before) == game(after) and held == "3", f"save mid-turn:\n     {before}\n     {after}")
    # The dice kept their holds: rolling again changes only the other three.
    say("F 1 1 5 5 5")
    s.tap("DOWN")
    roll()
    dice = say("H").split("|")[1].split()[:5]
    s.check(dice == ["6", "6", "5", "5", "5"], f"held dice stay after continue: {dice}")
    # The house plays the game out; then there is nothing to continue.
    say("A 1")
    for _ in range(150):
        st = say("H")
        if st.split("|")[2].split()[-1] == "1":
            break
        s.frames(400)
    purse = st.split()[1]
    s.frames(60)
    say("A 0")
    s.power_cycle()
    s.tap("A")                                        # no CONTINUE: this starts a new game (ante $5)
    s.frames(40)
    st = say("H")
    head = st.split("|")[0].split()
    s.check(st.split("|")[2].split()[-1] == "0" and head[3] == "0" and int(head[1]) == int(purse) - 5,
            f"finished game: purse {purse} kept, a new game antes $5 ({head[1]})")
    raise SystemExit(s.result())


if __name__ == "__main__":
    main()
