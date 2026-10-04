"""Drive CHCrossword - in the simulator or on the device - with a script.

    rpgame run [--card IMG] <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHCrossword adds:
    state               print the game's STATE line (the H command)
    expect KEY=VALUE .. fail unless the STATE line has these
    waitstate KEY=VALUE [MAX]   run until the STATE line has it (at most MAX frames, 2000)
    type LETTERS        on the open letter board: walk the glove to each letter and press A
    solve [N]           fill in the next N words not done yet (all of them by default), each
                        through the letter board as a player would: cursor to the word, A,
                        the letters still missing
    solveto             solve up to the jackpot word
    solvemost           solve until two words are left
    wrong               fill in the next word not done yet with its last missing letter wrong
    mark                remember the STATE line
    delta KEY=N ..      fail unless these have changed by N since `mark`
    rec pause / rec resume      leave what is between these out of the `rec` GIF (highlights)
--card FILE (simulator): a FAT image to stand in for the SD card (CHSD_CARD).
--id names the game's handshake reply (default CHCW). The game's own
protocol commands (say G, H, W, C, Z, U, J, X, Q) are listed above its hook
in Screens.cpp.
"""
import argparse
import os
import sys
from pathlib import Path

def _tools():
    """The repository's tools/ above this game; a copy in a sketchbook has none."""
    for up in Path(__file__).resolve().parents:
        if (up / "tools" / "chsim" / "chsim.py").exists() and (up / "libraries" / "RPGame").is_dir():
            return up / "tools"
    raise SystemExit(f"{Path(__file__).name}: the RPGame repository's tools/ was not found above this sketch "
                     "(this file needs tools/chsim/chdrivelib.py); run it from a checkout")


sys.path.insert(0, str(_tools() / "chsim"))
from chdrivelib import Driver, SerialTransport, SimTransport, main, mask_of  # noqa: E402,F401


class CrosswordDriver(Driver):
    def press(self, spec, hold=3):
        self.buttons(mask_of(spec))
        self.frames(hold)
        self.buttons(0)
        self.frames(1)

    def state(self):
        """The game's STATE line as a dict."""
        line = self.query("H", "STATE")
        return dict(f.split("=") for f in line.split()[1:])

    def type(self, letters):
        """Walk the letter board's glove to each letter (9 keys a row) and press A."""
        for ch in letters.upper():
            k = int(self.state()["key"])
            want = ord(ch) - 65
            dx, dy = want % 9 - k % 9, want // 9 - k // 9
            for _ in range(abs(dx)):
                self.press("RIGHT" if dx > 0 else "LEFT", 2)
            for _ in range(abs(dy)):
                self.press("DOWN" if dy > 0 else "UP", 2)
            self.press("A", 2)

    def wrong(self):
        """The next word not done, typed with its last missing letter one on in the alphabet."""
        _, w, cell, down, answer, have = self.query("W", "WORD").split()
        k = have.index(".")
        step = int(self.state()["n"]) if down == "1" else 1
        self.cmd(f"C {int(cell) + k * step} {down}")
        self.frames(1)
        self.press("A")
        self.frames(10)
        missing = [a for a, h in zip(answer, have) if h == "."]
        missing[-1] = chr((ord(missing[-1]) - 65 + 1) % 26 + 65)
        self.type("".join(missing))

    def solve(self, count, until=None):
        while count:
            word = self.query("W", "WORD").split()
            if word[1] == "none" or word[1] == until:
                break
            st = self.state()
            if until == "last" and int(st["words"]) - int(st["locked"]) <= 2:
                break
            _, w, cell, down, answer, have = word
            # The cursor to the word's first empty cell, then type what is missing.
            k = have.index(".") if "." in have else next(i for i in range(len(answer)) if have[i] != answer[i])
            step = int(self.state()["n"]) if down == "1" else 1
            self.cmd(f"C {int(cell) + k * step} {down}")
            self.frames(1)
            if "." not in have:
                # A wrong letter in a full word: rub it out (the board's B), type it again.
                self.press("A")
                self.frames(10)
                self.press("B")
                self.frames(2)
                self.type(answer[k])
            else:
                self.press("A")
                self.frames(10)
                self.type("".join(a for a, h in zip(answer, have) if h == "."))
            # Let the word's show run, and the board shut.
            for _ in range(400):
                st = self.state()
                if st["board"] == "0" and (st["busy"] == "0" or st["solved"] == "1"):
                    break
                if st["board"] == "1" and st["busy"] == "0":
                    self.press("START")
                self.frames(2)
            count -= 1

    def op(self, name, args, outdir):
        line = " ".join([name] + args)
        if name == "state":
            print(self.query("H", "STATE"), flush=True)
        elif name == "expect":
            st = self.state()
            for kv in args:
                k, v = kv.split("=")
                if st.get(k) != v:
                    raise SystemExit(f"expect {kv}: the game has {k}={st.get(k)} ({line})")
        elif name == "waitstate":
            k, v = args[0].split("=")
            for _ in range(int(args[1]) if len(args) > 1 else 2000):
                if self.state().get(k) == v:
                    break
                self.frames(1)
            else:
                raise SystemExit(f"waitstate {args[0]}: never happened")
        elif name == "type":
            self.type(args[0])
        elif name == "wrong":
            self.wrong()
        elif name == "mark":
            self.mark = self.state()
        elif name == "delta":
            st = self.state()
            for kv in args:
                k, v = kv.split("=")
                if int(st[k]) - int(self.mark[k]) != int(v):
                    raise SystemExit(f"delta {kv}: {k} went from {self.mark[k]} to {st[k]} ({line})")
        elif name == "solve":
            self.solve(int(args[0]) if args else 100000)
        elif name == "solvemost":
            # ... until two words are left.
            self.solve(100000, "last")
        elif name == "solveto":
            # ... up to (not including) the jackpot word.
            self.solve(100000, self.state()["jp"])
        elif name == "rec" and args and args[0] == "pause":
            self.rec_held, self.rec = self.rec, None
        elif name == "rec" and args and args[0] == "resume":
            self.rec = self.rec_held
        else:
            return False
        return True


DRIVER, IDENT = CrosswordDriver, "CHCW"       # what the shared tools load from this file

if __name__ == "__main__":
    # --card FILE (simulator): the pretend SD card's image, through $CHSD_CARD.
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--card")
    a, rest = pre.parse_known_args()
    if a.card:
        os.environ["CHSD_CARD"] = str(Path(a.card).resolve())
    sys.argv[1:] = rest
    main(DRIVER, ident=IDENT)