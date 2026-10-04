"""Saving in the simulator: SAVE & QUIT in the middle of a round, power off
and on, CONTINUE from the title, and the round must come back exactly (the
purse, the jackpot, the cards and their daubs, the calls so far, the call a
rival wins on). Also: options survive, and going broke leaves no game to
continue.

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

FIELDS = ["purse", "jackpot", "phase", "cards", "called", "focus", "hall", "daubs", "hasGame", "rounds",
          "speed", "screen"]
CALLING, QUIT = 2, 6
LOSE = 4


def state(line):
    return dict(zip(FIELDS, map(int, line.split()[1:])))


def main():
    s = Session(ROOT)

    def say(line):
        return s.say(line, state)

    # A round in progress: four cards, a few calls, some daubed, the third card in play.
    say("R 11")
    say("J P")
    s.frames(20)
    say("C 4")
    s.frames(100 + 5 * 120)
    say("D")
    s.frames(2 * 120)                                  # two more calls left waiting
    s.tap("RIGHT")
    s.tap("RIGHT")
    s.frames(10)
    before = say("Y")
    # Pause -> SAVE & QUIT (the fourth item).
    s.tap("START")
    for _ in range(3):
        s.tap("DOWN")
    s.tap("A")
    s.frames(40)
    # Power off and on (the game in memory is gone), then CONTINUE.
    s.power_cycle()
    off = say("Y")
    s.check(off["hasGame"] == 1 and off["phase"] == QUIT, f"after a power cycle there is a game to continue: {off}")
    s.tap("A")
    s.frames(40)
    after = say("Y")
    same = all(before[k] == after[k] for k in FIELDS if k != "screen")
    s.check(same and before["phase"] == CALLING and before["called"] >= 6 and before["focus"] == 2,
            f"save mid-round: {before} -> {after}")
    # The round plays on from there.
    s.frames(400)
    later = say("Y")
    s.check(later["called"] > after["called"], f"the caller carries on ({after['called']} -> {later['called']} calls)")
    # Options are saved when the menu is left.
    say("J O")
    s.frames(10)
    s.tap("DOWN")
    s.tap("DOWN")
    s.tap("RIGHT")                                     # SPEED -> FAST
    s.tap("B")
    s.frames(30)
    s.power_cycle()
    st = say("Y")
    s.check(st["speed"] == 1, f"options survive a power cycle (speed {st['speed']})")
    # Going broke: the last $5 on one card, a rival wins at once.
    say("J P")
    s.frames(20)
    say("M 5")
    say("C 1")
    say("H 4")
    s.frames(1200)
    st = say("Y")
    s.check(st["screen"] == LOSE and st["purse"] == 0, f"broke: the broke screen ({st})")
    s.power_cycle()
    st = say("Y")
    s.check(st["hasGame"] == 0, f"broke: no game to continue ({st})")
    raise SystemExit(s.result())


if __name__ == "__main__":
    main()
