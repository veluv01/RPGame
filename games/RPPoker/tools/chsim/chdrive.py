"""Drive CHPoker - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHPoker adds:
    waitturn [W]        run until the table waits for you, then W frames more
    table               print the table's phase, your turn and the stacks
    playto P [W]        check/call on your turns until the table reaches phase P
--id names the game's handshake reply (default CHPK).
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


class PokerDriver(Driver):
    def op(self, name, args, outdir):
        if name == "waitturn":
            # Run until the table waits for you (a bet or the draw), then
            # W frames more (default 0). Gives up after 6000 frames.
            for _ in range(6000):
                state = self.query("H", "TABLE").split()
                if state[2] == "1":
                    break
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name == "playto":
            # Check or call (A) whenever it is your turn, until the table
            # reaches phase P (Table.h: 11 showdown, 12 award,
            # 13 hand over), then W frames more.
            want = int(args[0])
            for _ in range(20000):
                state = self.query("H", "TABLE").split()
                if int(state[1]) == want:
                    break
                if state[2] == "1":
                    self.buttons(mask_of("A"))
                    self.frames(3)
                    self.buttons(0)
                self.frames(1)
            self.frames(int(args[1]) if len(args) > 1 else 0)
        elif name == "table":
            print(self.query("H", "TABLE"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = PokerDriver, "CHPK"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)