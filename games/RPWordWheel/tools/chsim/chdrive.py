"""Drive CHWordWheel - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHWordWheel adds:
    rec pause / rec resume   leave what is between these out of the `rec`
                        GIF (highlights)
The game's own protocol commands (say R/F/U/C/V/M/G/W/J/H/E/X) are listed
at the top of CHWordWheel.ino.
--id names the game's handshake reply (default CHWW).
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


class WordWheelDriver(Driver):
    def op(self, name, args, outdir):
        if name == "rec" and args and args[0] == "pause":
            self.rec_held, self.rec = self.rec, None
        elif name == "rec" and args and args[0] == "resume":
            self.rec = self.rec_held
        else:
            return False
        return True


DRIVER, IDENT = WordWheelDriver, "CHWW"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)