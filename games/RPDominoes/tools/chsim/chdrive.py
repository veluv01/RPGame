"""Drive CHDominoes - in the simulator or on the device - with a script.

    rpgame run <script> <outdir>
    python tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

The repository's tools/chsim/chdrivelib.py does the driving and has the
common script commands (wait, tap, hold, snap, gif, rec, say, perf, cal ...).
CHDominoes adds:
    auto [PLAYS]        play on for the human (the A command: the SHARK's choices, draws when
                        there is nothing to play, PRESS A answered) to the end of the match,
                        or for PLAYS of the human's tiles
    round               the same, to the end of the round (the result panel)
    board               print the game's state (the H command)
    waitturn [W]        run until the game waits for you (a tile to play, a draw), answering
                        "PRESS A"; then W frames more
and writes its GIFs (gif, rec, freegif) with fbimage.save_gif: whole frames
on one shared palette, which every viewer shows right.
--id names the game's handshake reply (default CHDM).
"""
import sys
import time
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
from fbimage import save_gif, to_image  # noqa: E402


class DominoesDriver(Driver):
    def press(self, spec, hold=3):
        self.buttons(mask_of(spec))
        self.frames(hold)
        self.buttons(0)
        self.frames(1)

    def op(self, name, args, outdir):
        if name == "waitturn":
            # Until the game waits for the player (state M or R and
            # nothing moving); a "PRESS A" (W) is answered after a while.
            waited = 0
            for _ in range(20000):
                state = self.query("H", "BOARD").split()
                if state[4] in "RM" and state[5] == "1":
                    break
                if state[4] in "OB":
                    break
                waited = waited + 1 if state[4] == "W" else 0
                if waited > 90:
                    self.press("A")
                self.frames(1)
            self.frames(int(args[0]) if args else 0)
        elif name in ("auto", "round"):
            plays = int(args[0]) if args else 10000
            waited = 0
            for _ in range(200000):
                state = self.query("H", "BOARD").split()
                if state[4] == "O" or plays <= 0 or (state[4] == "B" and name == "round"):
                    break
                waited = waited + 1 if state[4] in "WB" else 0
                if waited > 45:
                    self.press("A")
                    waited = 0
                elif state[4] in "RM" and state[5] == "1":
                    self.cmd("A")
                    if state[4] == "M":
                        plays -= 1
                self.frames(2)
        elif name == "board":
            print(self.query("H", "BOARD"), flush=True)
        elif name == "gif":
            gif, count = args[0], int(args[1])
            every = int(args[2]) if len(args) > 2 else 1
            frames = []
            for _ in range(count):
                frames.append(to_image(self.shot(), 2))
                self.frames(every)
            save_gif(frames, outdir / f"{gif}.gif", int(1000 * every / 60))
        elif name == "rec":
            # rec start [EVERY]: record from here (every EVERY-th frame, 3);
            # rec stop NAME: write NAME.gif.
            if args[0] == "start":
                self.rec, self.rec_count = [], 0
                self.rec_every = int(args[1]) if len(args) > 1 else 3
            else:
                shots, self.rec = self.rec, None
                frames = [to_image(d, 2) for d in shots]
                save_gif(frames, outdir / f"{args[1]}.gif", int(1000 * self.rec_every / 60))
                print(f"{args[1]}.gif: {len(frames)} frames", flush=True)
        elif name == "freegif":
            # Free-running (real time, as on the board: the CPU thinks in
            # bursts) for SECONDS, a shot every EVERY seconds into a GIF.
            # A shot waits for the game's next frame.
            gif, secs, every = args[0], float(args[1]), float(args[2])
            self.cmd("L0")
            frames, t0 = [], time.time()
            while time.time() - t0 < secs:
                frames.append(to_image(self.shot(), 2))
                time.sleep(every)
            self.cmd("L1")
            save_gif(frames, outdir / f"{gif}.gif", int(1000 * every))
        else:
            return False
        return True


DRIVER, IDENT = DominoesDriver, "CHDM"       # what the shared tools load from this file

if __name__ == "__main__":
    main(DRIVER, ident=IDENT)