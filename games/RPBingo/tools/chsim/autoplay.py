"""A round of CHBingo played with the buttons, as a person would, recorded
to a GIF: buy the cards, then swipe toward the cards with a rainbow frame
and daub, with a reaction time before every press, and use each power-up
in the next quiet moment.

    python tools/chsim/autoplay.py OUT.gif [--cards 6] [--seed N] [--every 3]

Without --seed it first tries seeds (unrecorded) until the player wins the
round, then plays that one again and records it. Needs CHSIM_CXX (see
chsim.py).
"""
import argparse
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(1, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools") / "chsim"))  # the repository's tools/chsim: chsim.py, fbimage.py
from chsim import build  # noqa: E402
from chdrive import Driver, SimTransport, mask_of  # noqa: E402
from fbimage import to_image  # noqa: E402

FIELDS = ["purse", "jackpot", "phase", "cards", "called", "focus", "hall", "daubs", "hasGame", "rounds",
          "speed", "screen", "waiting", "power"]
BUY, CALLING, WON, LOST = 1, 2, 3, 4


class Player:
    def __init__(self, exe, seed, cards, every=0):
        self.d = Driver(SimTransport(exe), "CHBN")
        self.d.handshake()
        self.d.cmd("L1")
        self.rng = random.Random(seed)
        self.seed, self.cards, self.every = seed, cards, every
        self.frames = []
        self.n = 0

    def state(self):
        self.d.t.send("Y")
        got = None
        for _ in range(100):
            r = self.d.t.readline()
            if r.startswith("STATE"):
                got = dict(zip(FIELDS, map(int, r.split()[1:])))
            elif r.startswith("OK"):
                return got
        raise RuntimeError("no state")

    def say(self, line):
        self.d.cmd(line)

    def step(self, k=1):
        for _ in range(k):
            self.d.cmd("N 1")
            self.n += 1
            if self.every and self.n % self.every == 0:
                self.frames.append(to_image(self.d.shot(), 2))

    def tap(self, btn):
        self.d.buttons(mask_of(btn))
        self.step(2)
        self.d.buttons(0)
        self.step(1)

    def play(self, tail=150, limit=6000):
        """Returns 'won' or 'lost'."""
        self.say(f"R {self.seed}")
        self.say("J P")
        self.step(70)
        st = self.state()
        while st["phase"] == BUY:                       # the buy-in: count up, then A
            have = self.state()
            # The buy-in starts at 3 cards (or the last count).
            want = self.cards
            cur = self._buyN
            if cur < want:
                self.tap("RIGHT"); self._buyN += 1; self.step(self.rng.randint(6, 10))
            elif cur > want:
                self.tap("LEFT"); self._buyN -= 1; self.step(self.rng.randint(6, 10))
            else:
                self.step(12)
                self.tap("A")
            st = self.state() if have else st
        result = None
        while self.n < limit:
            st = self.state()
            if st["phase"] in (WON, LOST):
                result = "won" if st["phase"] == WON else "lost"
                break
            waiting, focus, n = st["waiting"], st["focus"], st["cards"]
            if st["power"] and not waiting:                 # a quiet moment: use the power-up
                self.step(self.rng.randint(10, 20))
                self.tap("B")
            elif waiting & (1 << focus):
                self.step(self.rng.randint(5, 11))      # spot it, then daub
                self.tap("A")
            elif waiting:
                right = next(k for k in range(1, n) if waiting & (1 << ((focus + k) % n)))
                left = next(k for k in range(1, n) if waiting & (1 << ((focus - k) % n)))
                self.step(self.rng.randint(3, 7))
                self.tap("RIGHT" if right <= left else "LEFT")
            else:
                self.step(2)
        self.step(tail)
        return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--cards", type=int, default=6)
    ap.add_argument("--seed", type=int)
    ap.add_argument("--every", type=int, default=3)
    ap.add_argument("--max-calls", type=int, default=14, help="seed search: a win within this many calls")
    a = ap.parse_args()
    exe = build(str(HERE.parent.parent))
    Player._buyN = 3
    seed = a.seed
    if seed is None:
        for s in range(1, 400):
            Player._buyN = 3
            p = Player(exe, s, a.cards)
            res = p.play(tail=0, limit=2000)
            calls = p.state()["called"]
            p.d.t.close()
            if res == "won" and calls <= a.max_calls:
                seed = s
                print(f"seed {s}: won on call {calls}")
                break
        else:
            raise SystemExit("no winning seed found")
    Player._buyN = 3
    p = Player(exe, seed, a.cards, a.every)
    res = p.play()
    p.d.t.close()
    p.frames[0].save(a.out, save_all=True, append_images=p.frames[1:], duration=int(1000 * a.every / 60), loop=0)
    print(f"{a.out}: {len(p.frames)} frames, {res}")


if __name__ == "__main__":
    main()
