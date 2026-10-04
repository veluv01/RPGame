"""Drive SNAKES & LADDERS - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
`say` sends a game command (Screens.cpp's debugHook); one the
stage is not ready for is answered HELD and run a few frames later, and
this game's `say` also takes the frame ack that comes after it.
CHSnakes adds:
    board               print the game's state line (BOARD ...)
    waitturn [W]        run until the game waits for you, then W frames more
--id names the game's handshake reply (default CHSN).
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


class SnakesDriver(Driver):
    def say(self, text):
        self.t.send(text)
        owed = 0                                    # frame acks still to come
        for _ in range(10000):
            line = self.t.readline()
            if line.startswith("HELD"):
                # The stage is still showing something: game commands wait
                # for it, and in lockstep it needs frames to finish.
                self.t.send("N 5")
                owed += 1
                continue
            if line.startswith("OK ") and line[3:].strip().isdigit():
                self.t.send("N 5")                  # a frame ack, still waiting
                continue
            if line.startswith("OK"):
                break
            if line.startswith("ERR"):
                raise SystemExit(f"game refused: say {text}")
            print(line)                             # the hook's own reply lines
        # The command's own OK comes before the ack of the frames that let
        # it run: take that too, or every later reply is read one line late.
        while owed:
            line = self.t.readline()
            if line.startswith("OK ") and line[3:].strip().isdigit():
                owed = 0

    def op(self, name, args, outdir):
        if name == "say":
            self.say(" ".join(args))
        elif name == "waitturn":
            # Until the game waits for your choice, then W more frames. A
            # card or a banner held for a press is answered with A after
            # 90 frames.
            waited = 0
            for _ in range(20000):
                state = self.query("H", "BOARD").split()
                if state[3] == "1":
                    break
                waited = waited + 1 if state[4] == "1" else 0
                if waited > 90:
                    self.buttons(mask_of("A"))
                    self.frames(3)
                    self.buttons(0)
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name == "board":
            print(self.query("H", "BOARD"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = SnakesDriver, "CHSN"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)