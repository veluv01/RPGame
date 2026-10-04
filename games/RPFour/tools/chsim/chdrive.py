"""Drive CHFour - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHFour adds:
    col N               move your disc over column N (1..7) with D-pad taps and drop it
    auto [TURNS]        (simulator) play on for the human (the A command: the SHARK's choices)
                        to the end of the game, or for TURNS of the human's
    board               print the game's state (the H command)
    waitturn [W]        run until the game waits for your move, or is over; then W frames more
    waitover [W]        run until the result is up (hurrying the ending along); then W frames more
--id names the game's handshake reply (default CHF4).
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


class FourDriver(Driver):
    def press(self, spec, hold=3):
        self.buttons(mask_of(spec))
        self.frames(hold)
        self.buttons(0)
        self.frames(1)

    def op(self, name, args, outdir):
        if name == "col":
            # The cursor starts wherever it was left: the game says where
            # by taking the disc there (D-pad taps, as a player would).
            want = int(args[0]) - 1
            for _ in range(7):
                self.press("LEFT", 2)
            for _ in range(want):
                self.press("RIGHT", 2)
            self.frames(4)
            self.press("A")
        elif name == "waitturn":
            # Until the game waits for the player (state M and nothing
            # moving) or the game is over (W: its ending is playing).
            for _ in range(20000):
                state = self.query("H", "BOARD").split()
                if (state[4] == "M" and state[5] == "1") or state[4] in "OW":
                    break
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name == "waitover":
            waited = 0
            for _ in range(20000):
                state = self.query("H", "BOARD").split()
                if state[4] == "O":
                    break
                waited = waited + 1 if state[4] == "W" else 0
                if waited > 400:
                    self.press("A")
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name == "auto":
            turns = int(args[0]) if args else 10000
            for _ in range(200000):
                state = self.query("H", "BOARD").split()
                if state[4] in "OW" or turns <= 0:
                    break
                if state[4] == "M" and state[5] == "1":
                    self.cmd("A")
                    turns -= 1
                self.frames(2)
        elif name == "board":
            print(self.query("H", "BOARD"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = FourDriver, "CHF4"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)