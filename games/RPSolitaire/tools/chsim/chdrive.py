"""Drive CHSolitaire - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHSolitaire adds:
    waitstate S [W]     run until the table is in state S (1 play, 3 cascade, 4 done), then W frames more
    table               print the table in numbers
    expect KEY=VALUE..  check them against the table's numbers (score, moves, stock, waste, up...)
--id names the game's handshake reply (default CHSO).
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


class SolitaireDriver(Driver):
    def table(self):
        """The game's H line (TABLE key=value ...) as a dict."""
        return dict(kv.split("=") for kv in self.query("H", "TABLE").split()[1:])

    def op(self, name, args, outdir):
        if name == "waitstate":
            # Run until the table is in state S (Stage.h: 0
            # dealing, 1 play, 2 playing itself out, 3 cascade, 4 done),
            # then W frames more (default 0). Gives up after 20000 frames.
            for _ in range(20000):
                if self.table()["state"] == args[0]:
                    break
                self.frames(1)
            else:
                raise SystemExit(f"waitstate {args[0]}: never got there")
            self.frames(int(args[1]) if len(args) > 1 else 0)
        elif name == "expect":
            # expect KEY=VALUE ...: check the TABLE line.
            got = self.table()
            for kv in args:
                key, want = kv.split("=")
                if got.get(key) != want:
                    raise SystemExit(f"expect {kv}: the table says {key}={got.get(key)}")
        elif name == "table":
            print(self.query("H", "TABLE"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = SolitaireDriver, "CHSO"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)