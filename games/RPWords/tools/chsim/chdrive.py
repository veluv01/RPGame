"""Drive CHWords - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHWords adds:
    auto [TURNS]        (simulator) play on for the human (the A command: the best play found)
                        to the end of the game, or for TURNS of the human's
    board               print the game's state (the H command)
    waitturn [W]        run until the game waits for your move (answering a hand-over
                        with A), or is over; then W frames more
--id names the game's handshake reply (default CHWD).
"""
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


class WordsDriver(Driver):
    def press(self, spec, hold=3):
        self.buttons(mask_of(spec))
        self.frames(hold)
        self.buttons(0)
        self.frames(1)

    def op(self, name, args, outdir):
        if name == "waitturn":
            # Until the game waits for the player's move with nothing
            # moving (or is over); a hand-over is answered with A.
            for _ in range(20000):
                state = self.query("H", "STATE").split()
                if (state[2] == "M" and state[3] == "1") or state[2] == "O":
                    break
                if state[2] == "W":
                    self.press("A")
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name == "auto":
            # Play the human's moves (the best play found) for N turns,
            # or to the end of the game.
            turns = int(args[0]) if args else 10000
            for _ in range(200000):
                state = self.query("H", "STATE").split()
                if state[2] == "O" or turns <= 0:
                    break
                if state[2] == "W":
                    self.press("A")
                elif state[2] == "M" and state[3] == "1":
                    self.cmd("A")
                    turns -= 1
                self.frames(2)
        elif name == "board":
            print(self.query("H", "STATE"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = WordsDriver, "CHWD"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)