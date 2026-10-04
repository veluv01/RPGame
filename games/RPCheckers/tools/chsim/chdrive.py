"""Drive CHCheckers - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...);
its `say` already waits out a HELD reply (the CPU searching) by running
frames. CHCheckers adds:
    goto SQ [W]         (simulator) walk the glove to square SQ with D-pad taps
    board               (simulator) print the board
    waitturn [W]        (simulator) run until it is your move, then W frames more
    auto [N] [GAP]      (simulator) play N of your moves with the pad, the game
                        choosing them
--id names the game's handshake reply (default CHCK).
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


class CheckersDriver(Driver):
    def press(self, button, gap):
        self.buttons(mask_of(button))
        self.frames(3)
        self.buttons(0)
        self.frames(gap)

    def walk(self, sq, gap):
        route = self.query(f"R {sq}", "ROUTE").split()[1:]
        steps = route[0] if route else ""
        if "?" in steps:
            raise SystemExit(f"no route to {sq}")
        for ch in steps:
            self.press({"U": "UP", "D": "DOWN", "L": "LEFT", "R": "RIGHT"}[ch], gap)

    def wait_turn(self):
        """Until the game waits for your move (True) or is over (False). A
        banner waiting on a button is answered with A after 90 frames."""
        waited = 0
        for _ in range(8000):
            state = self.query("H", "BOARD").split()
            if state[3] == "1":
                return True
            if len(state) > 5 and state[5] == "1" and state[4] == "0":
                return False
            waited = waited + 1 if state[4] == "1" else 0
            if waited > 90:
                self.press("A", 0)
            self.frames(1)
        return False

    def op(self, name, args, outdir):
        if name == "goto":
            # Walk the glove to square N with D-pad presses (simulator: the
            # game plans the route), W frames apart (default 8).
            self.walk(args[0], int(args[1]) if len(args) > 1 else 8)
        elif name == "waitturn":
            # (simulator) until the game waits for your move (or is over),
            # then W more frames.
            self.wait_turn()
            self.frames(int(args[0]) if args else 0)
        elif name == "auto":
            # (simulator) play N of your moves (default 1) with the pad, the
            # game choosing them: the glove walks to the piece, A, to the
            # square, A - and on through a multiple jump. GAP frames
            # between presses (default 8). Stops when the game is over.
            gap = int(args[1]) if len(args) > 1 else 8
            moves = int(args[0]) if args else 1
            while moves > 0:
                if not self.wait_turn():
                    break
                hop = self.query("A", "AUTO").split()
                if hop[1] == "none":
                    break
                if hop[3] == "2":
                    self.frames(12)                 # the game jumps on by itself
                    continue
                if hop[3] == "0":
                    self.walk(hop[1], gap)
                    self.press("A", gap)
                self.walk(hop[2], gap)
                before = self.query("H", "BOARD").split()[2]
                self.press("A", gap)
                self.frames(4)
                state = self.query("H", "BOARD").split()
                if state[2] != before or state[5] == "1" or state[3] == "0":
                    moves -= 1                      # the turn passed (else: more to jump)
        elif name == "board":
            print(self.query("H", "BOARD"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = CheckersDriver, "CHCK"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)