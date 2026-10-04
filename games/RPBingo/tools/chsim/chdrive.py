"""Drive CHBingo - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHBingo adds:
The game's own protocol commands (say R/J/C/F/G/D/W/I/H/X/M/A/Y/V/E) are
listed at the top of CHBingo.ino.
--id names the game's handshake reply (default CHBN).
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


class BingoDriver(Driver):
    """No script commands of its own yet: they would go in op() (see Driver.op)."""


DRIVER, IDENT = BingoDriver, "CHBN"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)