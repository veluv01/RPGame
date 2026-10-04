"""Awkward STLs for tools/sim/accuracy.cpp: models far from the origin,
tiny, huge, and with broken (NaN / infinite) triangles.

    python tools/sim/make_edge_cases.py OUTDIR
"""
import math, os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import make_samples as ms


def moved(tris, off, scale=1.0):
    return [tuple(tuple(p[i] * scale + off[i] for i in range(3)) for p in t) for t in tris]


def write(outdir, name, tris):
    ms.OUT = outdir
    ms.write_stl(name, tris)


if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    torus = ms.torus()
    write(out, 'OFFSET.STL', moved(torus, (1000.0, -2000.0, 500.0)))      # build-plate coords
    write(out, 'TINY.STL', moved(torus, (0, 0, 0), 1e-5))                 # micro-metres
    write(out, 'HUGE.STL', moved(torus, (0, 0, 0), 1e5))                  # kilometres
    write(out, 'TINYFAR.STL', moved(torus, (3000.0, 0, 0), 0.01))         # small, far out
    write(out, 'NEG.STL', moved(torus, (-50.0, -50.0, -50.0)))
    broken = list(torus)
    broken[5] = ((float('nan'), 0, 0), (1, 1, 1), (2, 2, 2))
    broken[9] = ((float('inf'), 0, 0), (1, 1, 1), (2, 2, 2))
    write(out, 'NANS.STL', broken)
    flat = [((0, 0, 0), (10, 0, 0), (10, 10, 0)), ((0, 0, 0), (10, 10, 0), (0, 10, 0))]
    write(out, 'FLAT.STL', flat)                                          # zero thickness, open
