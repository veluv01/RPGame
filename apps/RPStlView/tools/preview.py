"""Render what the device would show, using the real renderer.

    python tools/preview.py model.stl [out.gif] [--frames 36] [--mode xray|front]
                            [--pitch 25] [--zoom 1.0] [--draft] [--scale 3]

Builds tools/sim/stlsim.cpp together with src/StlRender.cpp (the same file
the sketch compiles) using any C++ compiler it can find (g++, clang++, or
`zig c++` - set CXX to override), runs it, and writes an animated GIF or a
PNG (--frames 1) with the device palette at 12 bpp. Prints the per-frame
edge and pixel counts that set the frame time on the device.
"""
import argparse, os, shutil, subprocess, sys, tempfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SIM_SRC = [os.path.join(HERE, 'sim', 'stlsim.cpp'), os.path.join(HERE, '..', 'src', 'StlRender.cpp')]
BUILD = os.path.join(tempfile.gettempdir(), 'rpstlview-sim')
SIM_EXE = os.path.join(BUILD, 'stlsim.exe' if os.name == 'nt' else 'stlsim')

# Must match PALETTE in RPStlView.ino (RGB565 there; RGB888 here, then
# reduced to 4 bits per channel as the panel does in 12 bpp mode).
PALETTE = [
    (6, 8, 20),                                     # 0 background
    (240, 240, 240), (130, 140, 160), (255, 190, 40),   # 1-3 text, dim, accent
    (235, 70, 70), (80, 210, 90), (80, 130, 255),   # 4-6 X Y Z axes
]
for i in range(9):                                  # 7-15 wires, far -> near
    t = i / 8
    PALETTE.append((int(20 + 200 * t * t), int(55 + 200 * t), int(85 + 170 * t)))


def find_cxx():
    if os.environ.get('CXX'):
        return os.environ['CXX'].split()
    for c in ('g++', 'clang++'):
        if shutil.which(c):
            return [c]
    if shutil.which('zig'):
        return ['zig', 'c++']
    try:
        import ziglang  # pip install ziglang
        return [sys.executable, '-m', 'ziglang', 'c++']
    except ImportError:
        sys.exit('no C++ compiler found: install g++/clang, or `pip install ziglang`, or set CXX')


def build():
    newest = max(os.path.getmtime(p) for p in SIM_SRC + [os.path.join(HERE, '..', 'src', 'StlRender.h')])
    if os.path.exists(SIM_EXE) and os.path.getmtime(SIM_EXE) >= newest:
        return
    os.makedirs(BUILD, exist_ok=True)
    cmd = find_cxx() + ['-O2', '-std=c++17', '-o', SIM_EXE] + SIM_SRC
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)


def to_image(fb, scale):
    pal = [(r >> 4) * 17 for rgb in PALETTE for r in rgb]   # 12 bpp, as the panel shows it
    px = bytearray(128 * 128)
    for i, b in enumerate(fb):
        px[2 * i] = b & 15
        px[2 * i + 1] = b >> 4
    img = Image.frombytes('P', (128, 128), bytes(px))
    img.putpalette(pal + [0] * (768 - len(pal)))
    return img.resize((128 * scale, 128 * scale), Image.NEAREST) if scale > 1 else img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stl')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--frames', type=int, default=36)
    ap.add_argument('--mode', choices=['xray', 'front'], default='xray')
    ap.add_argument('--pitch', type=int, default=25)
    ap.add_argument('--zoom', type=float, default=1.0)
    ap.add_argument('--draft', action='store_true',
                    help='show the draft (box + sampled points) used while turning big models')
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--fps', type=int, default=30)
    a = ap.parse_args()

    build()
    out = a.out or os.path.splitext(os.path.basename(a.stl))[0] + ('.png' if a.frames == 1 else '.gif')
    fbfile = out + '.fb'
    r = subprocess.run([SIM_EXE, a.stl, fbfile, str(a.frames), '1' if a.mode == 'front' else '0',
                        str(a.pitch), str(int(a.zoom * 256)), '1' if a.draft else '0'],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.exit(r.stderr)
    data = open(fbfile, 'rb').read()
    os.remove(fbfile)
    frames = [to_image(data[i:i + 8192], a.scale) for i in range(0, len(data), 8192)]
    if len(frames) == 1:
        frames[0].save(out)
    else:
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=1000 // a.fps, loop=0)
    print('wrote', out)


if __name__ == '__main__':
    main()
