#!/usr/bin/env python3
"""spin_preview: one ball spin, simulated by the real Ball.cpp and
drawn with tools/wheel.py's reference renderer, as an animated GIF.

    python tools/spin_preview.py                     # out/spin/spin_eu.gif + spin_us.gif
    python tools/spin_preview.py --seed 7 --target 17 --quick
    python tools/spin_preview.py --sheet 150 230 4   # also a contact sheet of ticks 150..230

It builds tools/tests/test_ball.cpp (as run_ball_tests.py does) and runs
it with --trace, which plans the spin the way the game does (begin, solve
in slices, launch) and prints one line per tick: t, phase, screen and rotor
angles, the raw pocket-unit angles, r, z, events, deflector, pocket, w, v.

Frames are 2x, every 2nd tick at 30/30/40 ms (33.3 ms average, 60 Hz
halved: GIF delays are in 10 ms units and most viewers slow 10 ms delays
to 100 ms). The wall band (rows 0..45) is NAVY with a status line.
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "tests"))
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))   # the repository's tools/: pixkit
import wheel  # noqa: E402
from pixkit import FB, NAVY, WHITE, SILVER, GOLD, FX_A  # noqa: E402
import run_ball_tests  # noqa: E402

OUT = HERE.parent / "out" / "spin"
PHASES = ["WAIT", "TRACK", "DROP", "ROTOR", "SETTLE", "DONE"]
EV_FLICK, EV_ROLL, EV_LEAVE, EV_DEFLECT, EV_FRET, EV_BOUNCE, EV_LAND = 1, 2, 4, 8, 16, 32, 64
EV_NAMES = ((EV_FLICK, "FLICK"), (EV_LEAVE, "LEAVE"), (EV_DEFLECT, "DEFL"),
            (EV_FRET, "FRET"), (EV_BOUNCE, "BOUNCE"), (EV_LAND, "LAND"))
HOLD = 60                                          # ticks shown after the landing


def trace(exe, seed, n, quick, target):
    out = subprocess.run([str(exe), "--trace", str(seed), str(n), str(int(quick)), str(target)],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        t, ph, sa, ra, a, rho, r, z, ev, defl, pocket, w, v, nn = map(int, line.split())
        rows.append(dict(t=t, ph=ph, sa=sa, ra=ra, a=a, rho=rho, r=r, z=z, ev=ev, defl=defl,
                         pocket=pocket, w=w, v=v, n=nn))
    return rows


def frames(rows, quick, target, n):
    """One FB per tick, with deflector flashes, the win highlight and a status line."""
    land = next((k for k, r in enumerate(rows) if r["ev"] & EV_LAND), None)
    end = len(rows) if land is None else min(len(rows), land + HOLD)
    flash_until, recent = {}, []
    out = []
    for k in range(end):
        r = rows[k]
        if r["ev"] & EV_DEFLECT:
            flash_until[r["defl"]] = k + 4
        flash = tuple(d for d, u in flash_until.items() if k < u)
        hi = col = None
        if land is not None and k >= land:
            hi, col = r["pocket"], (WHITE if k < land + 4 else FX_A)
        moving = r["ph"] in (1, 2)
        ball = (r["a"], r["r"], r["z"], r["ph"] == 5, r["w"] if moving else 0)
        fb = FB(NAVY)
        fb.fill_rect(0, 0, 128, wheel.WALL_H, NAVY)
        wheel.draw_wheel(fb, wheel.CX, wheel.CY, r["rho"], n, hi_pocket=hi, hi_colour=col,
                         ball=ball, flash=flash)
        for bit, name in EV_NAMES:
            if r["ev"] & bit:
                recent.append((k, name))
        recent = [(t, s) for t, s in recent if k - t < 20]
        tag = ("QUICK" if quick else "FUN") + (" US" if n == 38 else " EU")
        fb.text35(2, 2, f"{tag} T{r['t']} {PHASES[r['ph']]}", WHITE)
        fb.text35(2, 9, f"TARGET {wheel.number_name(wheel.wheel_order(n)[target])}", SILVER)
        if recent:
            fb.text35(2, 16, recent[-1][1], GOLD)
        if land is not None and k >= land:
            fb.text35(2, 23, f"LANDED {wheel.number_name(wheel.wheel_order(n)[r['pocket']])}", GOLD)
        out.append(fb)
    return out


def save_gif(fbs, path, scale=2, every=2):
    ims = [fb.image(scale) for fb in fbs[::every]]
    pattern = [30, 30, 40] if every == 2 else [17]
    durs = [pattern[i % len(pattern)] for i in range(len(ims))]
    durs[-1] = 1500
    path.parent.mkdir(parents=True, exist_ok=True)
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=durs, loop=0, optimize=False)
    return path


def save_sheet(fbs, rows, path, t0, t1, step, scale=3, cols=6, zoom=False):
    """A contact sheet of the table band, ticks t0..t1 every step; zoom: a
    48x32 window that follows the ball, at 6x."""
    from PIL import Image, ImageDraw
    pick = [k for k in range(len(fbs)) if t0 <= rows[k]["t"] <= t1][::step]
    if zoom:
        scale, cols = 6, 8
        zw, zh = 48, 32
        w, h = zw * scale, zh * scale
    else:
        w, h = 128 * scale, (wheel.BAND_Y1 - wheel.BAND_Y0) * scale
    rows_n = (len(pick) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows_n * (h + 14)), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for i, k in enumerate(pick):
        r = rows[k]
        if zoom:
            bx, by, _, _ = wheel.project_ball(wheel.CX, wheel.CY, r["a"], r["r"], r["z"], r["n"])
            x0 = max(0, min(128 - zw, bx - zw // 2))
            y0 = max(wheel.BAND_Y0, min(128 - zh, by - zh // 2))
            im = fbs[k].image(1).crop((x0, y0, x0 + zw, y0 + zh)).resize((w, h), Image.NEAREST)
        else:
            im = fbs[k].image(scale).crop((0, wheel.BAND_Y0 * scale, w, wheel.BAND_Y1 * scale))
        x, y = (i % cols) * w, (i // cols) * (h + 14)
        sheet.paste(im, (x, y + 14))
        d.text((x + 4, y + 1), f"t{r['t']} {PHASES[r['ph']]} r{r['r'] / 256:.1f} z{r['z'] / 256:.1f}"
               + (" " + "+".join(nm for bit, nm in EV_NAMES if r["ev"] & bit) if r["ev"] & ~EV_ROLL else ""),
               fill=(255, 255, 255))
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seed", type=lambda s: int(s, 0), default=7)
    ap.add_argument("--target", type=int, help="wheel index (default: the number 17 / 00)")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--only", choices=("eu", "us"))
    ap.add_argument("--sheet", nargs=3, type=int, metavar=("T0", "T1", "STEP"))
    ap.add_argument("--zoom", action="store_true", help="the sheet follows the ball at 6x")
    ap.add_argument("--every", type=int, default=2, help="ticks per GIF frame (1 or 2)")
    args = ap.parse_args()
    exe = run_ball_tests.build()
    for tag, n in (("eu", 37), ("us", 38)):
        if args.only and args.only != tag:
            continue
        target = args.target if args.target is not None else \
            wheel.wheel_order(n).index(17 if n == 37 else wheel.N00)
        rows = trace(exe, args.seed, n, args.quick, target)
        fbs = frames(rows, args.quick, target, n)
        suffix = "_quick" if args.quick else ""
        path = save_gif(fbs, OUT / f"spin_{tag}{suffix}.gif", every=args.every)
        land = next(r for r in rows if r["ev"] & EV_LAND)
        flick = next(r for r in rows if r["ev"] & EV_FLICK)
        print(f"wrote {path}: {len(fbs)} ticks, flick t{flick['t']}, land t{land['t']} "
              f"({land['t'] - flick['t']} ticks), pocket {land['pocket']} (target {target})")
        if args.sheet:
            sp = save_sheet(fbs, rows, OUT / f"sheet_{tag}{suffix}{'_zoom' if args.zoom else ''}.png",
                            *args.sheet, zoom=args.zoom)
            print("wrote", sp)


if __name__ == "__main__":
    main()
