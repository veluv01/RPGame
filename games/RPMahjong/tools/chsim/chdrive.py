"""Drive CHMahjong - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, snap, gif, rec, say, perf, cal ...).
CHMahjong adds:
    goto TILE [W]       (simulator) walk the glove to tile TILE with D-pad taps
    solve N [W]         (simulator) take the deal's own next N pairs, as a player would
    takehint [W]        (simulator) take the pair the hint is showing, the same way
    auto N              take N pairs (the first there is each time), no glove work
    state               print the game's state
    idle                run until the game takes input again
    hold BTN[+BTN]      keep held until `release` (taps and walks press theirs as well)
--id names the game's handshake reply (default CHMJ).
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


class MahjongDriver(Driver):
    def buttons(self, mask):
        # Buttons held with `hold` stay down under taps and walks.
        self.cmd(f"K {mask | getattr(self, 'base', 0):x}")

    def walk(self, tile, gap):
        """D-pad presses that take the glove to a tile (the game plans the route)."""
        route = self.query(f"R {tile}", "ROUTE").split()[1:]
        steps = route[0] if route else ""
        if "?" in steps:
            raise SystemExit(f"no route to {tile}")
        for ch in steps:
            self.buttons(mask_of({"U": "UP", "D": "DOWN", "L": "LEFT", "R": "RIGHT"}[ch]))
            self.frames(3)
            self.buttons(0)
            self.frames(gap)

    def idle(self, limit=600):
        """Run frames until the game is taking input again."""
        for _ in range(limit):
            if "busy=0" in self.query("H", "STATE"):
                return
            self.frames(1)

    def op(self, name, args, outdir):
        if name == "hold":
            self.base = mask_of(args[0])
            self.buttons(0)
        elif name == "release":
            self.base = 0
            self.buttons(0)
        elif name == "goto":
            # Walk the glove to tile N with D-pad presses (simulator: the
            # game plans the route), W frames apart (default 8).
            self.walk(args[0], int(args[1]) if len(args) > 1 else 8)
        elif name == "idle":
            self.idle()
        elif name in ("solve", "takehint"):
            # (simulator) the deal's own way of clearing the table, played
            # with the D-pad and A: W frames between presses (default 6).
            gap = int(args[1]) if len(args) > 1 else 6
            hint = name == "takehint"
            for _ in range(1 if hint else int(args[0])):
                self.idle()
                a, b = self.query("W" if hint else "O", "NEXT").split()[1:3]
                for tile in (a, b):
                    self.walk(tile, gap)
                    self.buttons(mask_of("A"))
                    self.frames(3)
                    self.buttons(0)
                    self.frames(gap)
                self.idle()
        elif name == "auto":
            for _ in range(int(args[0])):
                self.t.send("A")
                if self.expect(("OK", "ERR")).startswith("ERR"):
                    break
                self.idle()
        elif name == "state":
            print(self.query("H", "STATE"), flush=True)
        else:
            return False
        return True


DRIVER, IDENT = MahjongDriver, "CHMJ"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)