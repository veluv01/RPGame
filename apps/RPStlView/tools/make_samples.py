"""Write a few test models as binary STL into ../sample/.

    python tools/make_samples.py

Names are 8.3 on purpose: the SD library only sees short names, so a long
name like "torus_knot.stl" would show up on the device as "TORUS_~1.STL".

  CUBE.STL      12 triangles. The smallest closed mesh: sanity check.
  ICOSPH.STL    320. Geodesic sphere, every edge the same length.
  TORUS.STL     1152. Classic donut.
  GEAR.STL      ~450. Extruded 16-tooth spur gear with a bore: flat faces
                triangulated as a CAD exporter would.
  BOWL.STL      512. An open hemisphere: exercises the open-mesh fallback
                (edges are drawn from both sides, no dedup).
  KNOT.STL      12800. (3,2) torus knot tube, 640 KB: big enough that the
                device drops to draft rendering while it spins.
"""
import math, os, struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sample')


def write_stl(name, tris, header=b'CHStlView sample'):
    """tris: list of (a, b, c), each a 3-tuple, counter-clockwise from outside."""
    path = os.path.join(OUT, name)
    with open(path, 'wb') as f:
        f.write(header.ljust(80, b'\0'))
        f.write(struct.pack('<I', len(tris)))
        for a, b, c in tris:
            ux, uy, uz = (b[i] - a[i] for i in range(3))
            vx, vy, vz = (c[i] - a[i] for i in range(3))
            n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
            l = math.sqrt(sum(x * x for x in n)) or 1.0
            f.write(struct.pack('<12fH', *(x / l for x in n), *a, *b, *c, 0))
    print(f'{name:12s} {len(tris):6d} triangles  {os.path.getsize(path):8d} bytes')


def quad(tris, a, b, c, d):
    """a b c d counter-clockwise from outside."""
    tris.append((a, b, c))
    tris.append((a, c, d))


def grid_surface(f, nu, nv, wrap_u=True, wrap_v=True):
    """Parametric surface f(u, v) in [0,1)^2, outward-facing for (u, v) CCW."""
    tris = []
    cu = nu if wrap_u else nu - 1
    cv = nv if wrap_v else nv - 1
    pts = [[f(i / nu if wrap_u else i / (nu - 1), j / nv if wrap_v else j / (nv - 1))
            for j in range(nv)] for i in range(nu)]
    for i in range(cu):
        for j in range(cv):
            i2, j2 = (i + 1) % nu, (j + 1) % nv
            quad(tris, pts[i][j], pts[i2][j], pts[i2][j2], pts[i][j2])
    return tris


def cube():
    s = 10.0
    v = [(x, y, z) for x in (-s, s) for y in (-s, s) for z in (-s, s)]
    def p(x, y, z): return v[x * 4 + y * 2 + z]
    t = []
    quad(t, p(0, 0, 0), p(0, 1, 0), p(1, 1, 0), p(1, 0, 0))  # bottom (-z)
    quad(t, p(0, 0, 1), p(1, 0, 1), p(1, 1, 1), p(0, 1, 1))  # top
    quad(t, p(0, 0, 0), p(1, 0, 0), p(1, 0, 1), p(0, 0, 1))  # -y
    quad(t, p(0, 1, 0), p(0, 1, 1), p(1, 1, 1), p(1, 1, 0))  # +y
    quad(t, p(0, 0, 0), p(0, 0, 1), p(0, 1, 1), p(0, 1, 0))  # -x
    quad(t, p(1, 0, 0), p(1, 1, 0), p(1, 1, 1), p(1, 0, 1))  # +x
    return t


def icosphere(subdiv=2, r=20.0):
    t = (1 + 5 ** 0.5) / 2
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
             (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
             (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
             (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    def norm(p):
        l = math.sqrt(sum(x * x for x in p))
        return tuple(x / l for x in p)
    verts = [norm(p) for p in verts]
    for _ in range(subdiv):
        cache, nf = {}, []
        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                cache[key] = len(verts)
                verts.append(norm(tuple((verts[a][i] + verts[b][i]) / 2 for i in range(3))))
            return cache[key]
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = nf
    v = [tuple(x * r for x in p) for p in verts]
    return [(v[a], v[b], v[c]) for a, b, c in faces]


def torus(R=18.0, r=7.0, nu=48, nv=12):
    def f(u, v):
        a, b = u * 2 * math.pi, v * 2 * math.pi
        return ((R + r * math.cos(b)) * math.cos(a), (R + r * math.cos(b)) * math.sin(a),
                r * math.sin(b))
    return grid_surface(f, nu, nv)


def gear(teeth=16, r_root=16.0, r_tip=19.5, r_bore=5.0, h=6.0, bore_n=16):
    """Spur gear: outline from alternating root/tip arcs, extruded, with a bore.
    Faces are triangulated between the outer outline and the bore, which is
    what a CAD STL export looks like."""
    outline = []
    per = 2 * math.pi / teeth
    for i in range(teeth):
        a0 = i * per
        for fr, rad in ((0.0, r_root), (0.18, r_root), (0.30, r_tip), (0.55, r_tip),
                        (0.67, r_root)):
            a = a0 + fr * per
            outline.append((rad * math.cos(a), rad * math.sin(a)))
    n = len(outline)
    bore = [(r_bore * math.cos(2 * math.pi * k / bore_n), r_bore * math.sin(2 * math.pi * k / bore_n))
            for k in range(bore_n)]
    t = []
    z0, z1 = 0.0, h
    # side walls
    for i in range(n):
        a, b = outline[i], outline[(i + 1) % n]
        quad(t, (a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1))
    for i in range(bore_n):
        a, b = bore[i], bore[(i + 1) % bore_n]
        quad(t, (b[0], b[1], z0), (a[0], a[1], z0), (a[0], a[1], z1), (b[0], b[1], z1))
    # caps: the ring between the outline and the bore, stitched by walking
    # both loops in angle order and always advancing the one that is behind
    def ang(p): return math.atan2(p[1], p[0]) % (2 * math.pi)
    outer = sorted(outline, key=ang)
    i = j = 0
    while i < n or j < bore_n:
        o0, o1 = outer[i % n], outer[(i + 1) % n]
        b0, b1 = bore[j % bore_n], bore[(j + 1) % bore_n]
        adv_o = j >= bore_n or (i < n and ang(o1) + (2 * math.pi if i + 1 >= n else 0)
                                <= ang(b1) + (2 * math.pi if j + 1 >= bore_n else 0))
        if adv_o:
            tri2 = (o0, o1, b0); i += 1
        else:
            tri2 = (o0, b1, b0); j += 1
        a, b, c = tri2
        t.append(((a[0], a[1], z1), (b[0], b[1], z1), (c[0], c[1], z1)))   # top, CCW from +z
        t.append(((a[0], a[1], z0), (c[0], c[1], z0), (b[0], b[1], z0)))   # bottom, reversed
    return t


def bowl(r=20.0, nu=32, nv=8):
    """Open hemisphere, normals outward. Its rim edges belong to one triangle."""
    def f(u, v):
        a = u * 2 * math.pi
        b = -math.pi / 2 + v * math.pi / 2           # south pole .. equator
        b = max(b, -math.pi / 2 + 1e-4)
        return (r * math.cos(b) * math.cos(a), r * math.cos(b) * math.sin(a), r * math.sin(b))
    return grid_surface(f, nu, nv + 1, wrap_u=True, wrap_v=False)


def torus_knot(p=3, q=2, R=16.0, r=6.0, tube=3.0, nu=400, nv=16):
    def c(t):
        a = t * 2 * math.pi
        rr = R + r * math.cos(q * a)
        return (rr * math.cos(p * a), rr * math.sin(p * a), r * math.sin(q * a))
    def f(u, v):
        e = 1e-4
        p0, p1 = c(u), c(u + e)
        T = [p1[i] - p0[i] for i in range(3)]
        l = math.sqrt(sum(x * x for x in T)); T = [x / l for x in T]
        N = [-p0[0], -p0[1], 0.0]
        d = sum(N[i] * T[i] for i in range(3)); N = [N[i] - d * T[i] for i in range(3)]
        l = math.sqrt(sum(x * x for x in N)); N = [x / l for x in N]
        B = [T[1] * N[2] - T[2] * N[1], T[2] * N[0] - T[0] * N[2], T[0] * N[1] - T[1] * N[0]]
        a = v * 2 * math.pi
        return tuple(p0[i] + tube * (math.cos(a) * N[i] + math.sin(a) * B[i]) for i in range(3))
    return grid_surface(f, nu, nv)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    write_stl('CUBE.STL', cube())
    write_stl('ICOSPH.STL', icosphere())
    write_stl('TORUS.STL', torus())
    write_stl('GEAR.STL', gear())
    write_stl('BOWL.STL', bowl())
    write_stl('KNOT.STL', torus_knot())
