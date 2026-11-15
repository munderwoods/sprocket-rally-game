#!/usr/bin/env python3
"""Bakes Sprocket Rally's sprites into src/art.h.

Racers and set pieces are low-poly 3D models (boxes, wedges, cylinders, ellipsoids, prisms)
rasterized with a z-buffer at N headings from a fixed camera pitch, the way pre-rendered
SNES-era sprites were made, then reduced (supersampled, majority material per pixel) to
palette-indexed pixels with a dark outline. Every racer and set piece has its own palette.
Small props (item crystal, bolts, bomb, oil, trees, FX, HUD icons) are drawn directly with
PIL, or rendered once from a tiny 3D model. The eight racer portraits are hand-drawn pixel
grids kept in tools/portraits.py.

Run:  python3 -I tools/bake_art.py   (from the module directory; writes src/art.h and
      tools/preview.png). Needs numpy; if plain python3 lacks it the script re-runs itself
      through `uv run --with numpy --with pillow`.

Angle convention: frame k of N shows the object with heading rel = k*2pi/N relative to the
camera's view direction; k=0 is seen from directly behind, k=N/4 has its nose pointing to
screen-right, k=N/2 faces the camera. Models use x forward, y left, z up.
"""
import math, os, sys
from contextlib import contextmanager

try:
    import numpy as np
except ImportError:
    import shutil
    uv = shutil.which("uv")
    if uv and not os.environ.get("SR_BAKE_REEXEC"):
        os.environ["SR_BAKE_REEXEC"] = "1"
        os.execv(uv, [uv, "run", "--quiet", "--with", "numpy", "--with", "pillow", "python", "-I",
                      os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit("bake_art.py needs numpy: uv run --with numpy --with pillow python tools/bake_art.py")
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # `python3 -I` leaves the script directory off sys.path
import portraits as portraits_data  # hand-drawn racer portraits (tools/portraits.py)
OUT_H = os.path.join(HERE, "..", "src", "art.h")
OUT_BIN = os.path.join(HERE, "..", "assets", "art.bin")   # the sprites; the header has only their names
PREVIEW = os.path.join(HERE, "preview.png")

N_ANGLES = 24
SPR = 32
PITCH = math.radians(24)   # camera elevation
KART_PPU = 11.5            # sprite px per kart model unit (1 unit ~ 4 world texels)
ANCHOR = (16, 25)          # ground center in the kart sprite
NO_OUTLINE_SCALE = 1.09    # about a pixel more on every side of a 22px kart, where the ink ring was
T = (0, 0, 0, 0)
INK = (16, 12, 24, 255)

# ================================================================================================
# 3D model building
# ================================================================================================
def _rot(rx, ry, rz):
    rx, ry, rz = (math.radians(a) for a in (rx, ry, rz))
    X = np.array([[1, 0, 0], [0, math.cos(rx), -math.sin(rx)], [0, math.sin(rx), math.cos(rx)]])
    Y = np.array([[math.cos(ry), 0, math.sin(ry)], [0, 1, 0], [-math.sin(ry), 0, math.cos(ry)]])
    Z = np.array([[math.cos(rz), -math.sin(rz), 0], [math.sin(rz), math.cos(rz), 0], [0, 0, 1]])
    return Z @ Y @ X

def mat(cols, glow=False, w=1.0):
    """A material: a shade ramp (bright -> dark). glow = unlit (always brightest shade).
    w = vote weight when downsampling, so tiny details (eyes, visors) survive."""
    return {"cols": [tuple(c[:3]) + (255,) for c in cols], "glow": glow, "w": w}

class Model:
    def __init__(self, mats):
        self.mats = mats
        self.names = list(mats)
        self.tris = []      # np (3,3) arrays
        self.mids = []
        self.tags = []
        self.stack = [(np.eye(3), np.zeros(3))]
        self.tag = "vehicle"

    @contextmanager
    def at(self, tr=(0, 0, 0), rx=0, ry=0, rz=0, s=None):
        R0, t0 = self.stack[-1]
        Rl = _rot(rx, ry, rz)
        if s is not None:
            Rl = Rl @ np.diag(s if isinstance(s, (tuple, list)) else (s, s, s))
        self.stack.append((R0 @ Rl, R0 @ np.array(tr, float) + t0))
        try: yield
        finally: self.stack.pop()

    @contextmanager
    def tagged(self, tag):
        old = self.tag; self.tag = tag
        try: yield
        finally: self.tag = old

    def emit(self, tris):
        """tris: list of ((a, b, c), material) in local coords, forming one convex primitive;
        windings are fixed so normals point away from the primitive's centroid."""
        if not tris: return
        R, t = self.stack[-1]
        P = np.array([tr for tr, _ in tris], float) @ R.T + t
        cen = P.reshape(-1, 3).mean(0)
        n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
        flip = np.einsum("ij,ij->i", n, P.mean(1) - cen) < 0
        P[flip] = P[flip][:, [0, 2, 1]]
        for i, (_, m) in enumerate(tris):
            if m not in self.mats: raise KeyError(m)
            self.tris.append(P[i]); self.mids.append(self.names.index(m)); self.tags.append(self.tag)

    # ---- primitives ----
    def hexa(self, p, m, mats=None):
        f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        out = []
        for i, (a, b, c, d) in enumerate(f):
            mm = mats[i] if mats else m
            out += [((p[a], p[b], p[c]), mm), ((p[a], p[c], p[d]), mm)]
        self.emit(out)

    def box(self, x0, x1, y0, y1, z0, z1, m, tx=0.0, ty=0.0, mats=None):
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0+tx, y0+ty, z1), (x1-tx, y0+ty, z1), (x1-tx, y1-ty, z1), (x0+tx, y1-ty, z1)]
        self.hexa(p, m, mats)

    def wedge(self, x0, x1, y0, y1, z0, zb, zf, m, ty=0.0, tyf=None):
        tyf = ty if tyf is None else tyf
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0+ty, zb), (x1, y0+tyf, zf), (x1, y1-tyf, zf), (x0, y1-ty, zb)]
        self.hexa(p, m)

    def cyl(self, p0, p1, r0, r1=None, m="", n=10, cap=None, cap1=None, phase=0.0, ring=None, inner=0.6):
        r1 = r0 if r1 is None else r1
        p0 = np.array(p0, float); p1 = np.array(p1, float)
        a = p1 - p0; a /= np.linalg.norm(a)
        ref = np.array([1.0, 0, 0]) if abs(a[2]) > 0.95 else np.array([0, 0, 1.0])
        u = ref - a*np.dot(a, ref); u /= np.linalg.norm(u)
        v = np.cross(a, u)
        ang = [phase + 2*math.pi*i/n for i in range(n)]
        R0 = [tuple(p0 + r0*(math.cos(t)*u + math.sin(t)*v)) for t in ang]
        R1 = [tuple(p1 + r1*(math.cos(t)*u + math.sin(t)*v)) for t in ang]
        out = []
        for i in range(n):
            j = (i+1) % n
            out += [((R0[i], R0[j], R1[j]), m), ((R0[i], R1[j], R1[i]), m)]
        for (c, rr, R, cm) in ((p0, r0, R0, cap or m), (p1, r1, R1, cap1 or cap or m)):
            if rr <= 1e-6: continue
            if ring:
                Ri = [tuple(c + (np.array(q) - c)*inner) for q in R]
                for i in range(n):
                    j = (i+1) % n
                    out += [((R[i], R[j], Ri[j]), ring), ((R[i], Ri[j], Ri[i]), ring)]
                    out.append(((tuple(c), Ri[i], Ri[j]), cm))
            else:
                for i in range(n):
                    out.append(((tuple(c), R[i], R[(i+1) % n]), cm))
        self.emit(out)

    def wheel(self, x, y, z, r, w, tire="tire", hub="hub", rim=None, n=12):
        self.cyl((x, y - w/2, z), (x, y + w/2, z), r, m=tire, n=n, cap=hub, ring=rim or tire, inner=0.62)

    def ellip(self, c, r, m, rows=6, cols=10, fn=None):
        if not isinstance(r, (tuple, list)): r = (r, r, r)
        cx, cy, cz = c
        def P(t, a): return (cx + r[0]*math.sin(t)*math.cos(a), cy + r[1]*math.sin(t)*math.sin(a), cz + r[2]*math.cos(t))
        out = []
        for i in range(rows):
            t0, t1 = math.pi*i/rows, math.pi*(i+1)/rows
            for j in range(cols):
                a0, a1 = 2*math.pi*j/cols, 2*math.pi*(j+1)/cols
                mm = m
                if fn:
                    tm, am = (t0+t1)/2, (a0+a1)/2
                    mm = fn(math.sin(tm)*math.cos(am), math.sin(tm)*math.sin(am), math.cos(tm)) or m
                q = [P(t0, a0), P(t1, a0), P(t1, a1), P(t0, a1)]
                if i == 0: out.append(((q[0], q[1], q[2]), mm))
                elif i == rows-1: out.append(((q[0], q[1], q[3]), mm))
                else: out += [((q[0], q[1], q[2]), mm), ((q[0], q[2], q[3]), mm)]
        self.emit(out)

    def prism(self, poly, t0, t1, m, plane="xy", side=None):
        """Extrude a convex 2D polygon. plane 'xy': poly in (x,y), extruded along z from t0..t1;
        'yz': poly in (y,z) along x; 'xz': poly in (x,z) along y."""
        def P(u, v, t):
            return {"xy": (u, v, t), "yz": (t, u, v), "xz": (u, t, v)}[plane]
        A = [P(u, v, t0) for u, v in poly]; B = [P(u, v, t1) for u, v in poly]
        n = len(poly); out = []
        for i in range(1, n-1):
            out += [((A[0], A[i], A[i+1]), m), ((B[0], B[i], B[i+1]), m)]
        for i in range(n):
            j = (i+1) % n
            out += [((A[i], A[j], B[j]), side or m), ((A[i], B[j], B[i]), side or m)]
        self.emit(out)

    def arrays(self, tags=None):
        idx = [i for i, t in enumerate(self.tags) if tags is None or t in tags]
        return np.array([self.tris[i] for i in idx]), np.array([self.mids[i] for i in idx])

    def palette(self):
        pal = [T, INK]; base = []
        for nm in self.names:
            base.append(len(pal)); pal += self.mats[nm]["cols"]
        return pal, base

# ================================================================================================
# rasterizer
# ================================================================================================
_LC = np.array([-0.5, 0.75, 0.55]); _LC /= np.linalg.norm(_LC)   # toward light: right, up, toward viewer

def _cam(rel, pitch):
    yaw = -rel
    c, s = math.cos(yaw), math.sin(yaw)
    Rz = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    right = np.array([0, -1.0, 0])
    up = np.array([math.sin(pitch), 0, math.cos(pitch)])
    d = np.array([math.cos(pitch), 0, -math.sin(pitch)])
    return Rz, right, up, d

def project(pts, rel, pitch=PITCH):
    """model points -> (screen x right, screen y up) in model units, relative to the origin."""
    Rz, right, up, d = _cam(rel, pitch)
    V = np.asarray(pts, float) @ Rz.T
    return V @ right, V @ up

def render(model, W, H, ppu, anchor, rel, ss=4, pitch=PITCH, tags=None, cov=0.45, edge=None, ring=True):
    """Returns an HxW uint8 array of palette indices (0 transparent, 1 outline)."""
    P, M = model.arrays(tags)
    Rz, right, up, d = _cam(rel, pitch)
    V = P @ Rz.T                                        # (N,3,3)
    n = np.cross(V[:, 1] - V[:, 0], V[:, 2] - V[:, 0])
    nl = np.linalg.norm(n, axis=1); ok = nl > 1e-12
    n[ok] /= nl[ok, None]
    facing = (n @ d < -1e-6) & ok
    Lw = _LC[0]*right + _LC[1]*up - _LC[2]*d
    lit = n @ Lw
    inten = np.where(lit > 0, 0.15 + 0.85*lit, 0.15*(1 + lit))
    sxs = (anchor[0] + (V @ right)*ppu)*ss
    sys_ = (anchor[1] - (V @ up)*ppu)*ss
    zs = V @ d
    WW, HH = W*ss, H*ss
    zb = np.full((HH, WW), np.inf); mb = np.full((HH, WW), -1, np.int16); ib = np.zeros((HH, WW))
    for t in np.nonzero(facing)[0]:
        x0, x1, x2 = sxs[t]; y0, y1, y2 = sys_[t]
        minx = max(0, int(math.floor(min(x0, x1, x2)))); maxx = min(WW-1, int(math.ceil(max(x0, x1, x2))))
        miny = max(0, int(math.floor(min(y0, y1, y2)))); maxy = min(HH-1, int(math.ceil(max(y0, y1, y2))))
        if minx > maxx or miny > maxy: continue
        den = (y1-y2)*(x0-x2) + (x2-x1)*(y0-y2)
        if abs(den) < 1e-9: continue
        fx = np.arange(minx, maxx+1) + 0.5; fy = (np.arange(miny, maxy+1) + 0.5)[:, None]
        w0 = ((y1-y2)*(fx-x2) + (x2-x1)*(fy-y2)) / den
        w1 = ((y2-y0)*(fx-x2) + (x0-x2)*(fy-y2)) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any(): continue
        z0, z1, z2 = zs[t]
        z = w0*z0 + w1*z1 + w2*z2
        sub = zb[miny:maxy+1, minx:maxx+1]
        upd = inside & (z < sub)
        sub[upd] = z[upd]
        mb[miny:maxy+1, minx:maxx+1][upd] = M[t]
        ib[miny:maxy+1, minx:maxx+1][upd] = inten[t]
    return _resolve(model, mb, ib, zb, W, H, ss, cov, edge, ring)

def _resolve(model, mb, ib, zb, W, H, ss, cov, edge, ring=True):
    B = mb.reshape(H, ss, W, ss).transpose(0, 2, 1, 3).reshape(H, W, ss*ss)
    I = ib.reshape(H, ss, W, ss).transpose(0, 2, 1, 3).reshape(H, W, ss*ss)
    Z = zb.reshape(H, ss, W, ss).transpose(0, 2, 1, 3).reshape(H, W, ss*ss)
    pal, base = model.palette()
    best = np.full((H, W), -1); bestc = np.zeros((H, W)); bi = np.zeros((H, W)); bz = np.zeros((H, W))
    for mid in np.unique(B[B >= 0]):
        msk = B == mid
        cnt = msk.sum(-1)
        c = cnt * model.mats[model.names[mid]]["w"]
        better = c > bestc
        safe = np.maximum(cnt, 1)
        best[better] = mid; bestc[better] = c[better]
        bi[better] = (np.where(msk, I, 0).sum(-1) / safe)[better]
        bz[better] = (np.where(msk, Z, 0).sum(-1) / safe)[better]
    covered = (B >= 0).sum(-1) >= cov*ss*ss
    out = np.zeros((H, W), np.uint8)
    for mid in range(len(model.names)):
        sel = covered & (best == mid)
        if not sel.any(): continue
        mm = model.mats[model.names[mid]]; nsh = len(mm["cols"])
        if mm["glow"]: k = np.zeros(sel.sum(), int)
        else: k = np.clip(((1.0 - bi[sel]) * nsh).astype(int), 0, nsh-1)
        out[sel] = base[mid] + k
    if edge is not None:   # inner dark line where a surface sits far behind its neighbor above/left
        filled = out > 1
        zz = np.where(filled, bz, np.inf)
        ln = np.zeros_like(filled)
        for dy, dx in ((-1, 0), (0, -1), (1, 0), (0, 1)):
            nb = np.full_like(zz, np.inf)
            ys = slice(max(0, -dy), H - max(0, dy)); yd = slice(max(0, dy), H - max(0, -dy))
            xs = slice(max(0, -dx), W - max(0, dx)); xd = slice(max(0, dx), W - max(0, -dx))
            nb[ys, xs] = zz[yd, xd]
            ln |= filled & (zz - nb > edge)
        out[ln] = 1
    return _outline(out) if ring else out   # ring: the 1px ink line round the silhouette

def _outline(out):
    H, W = out.shape
    solid = out > 1
    nb = np.zeros_like(solid)
    nb[1:, :] |= solid[:-1, :]; nb[:-1, :] |= solid[1:, :]
    nb[:, 1:] |= solid[:, :-1]; nb[:, :-1] |= solid[:, 1:]
    res = out.copy()
    res[(out == 0) & nb] = 1
    return res

def bounds(models_tags, ppu, rels, pitch=PITCH):
    xs, ys = [], []
    for model, tags in models_tags:
        P, _ = model.arrays(tags)
        pts = P.reshape(-1, 3)
        for r in rels:
            sx, sy = project(pts, r, pitch)
            xs += [sx.min(), sx.max()]; ys += [sy.min(), sy.max()]
    return min(xs)*ppu, max(xs)*ppu, min(ys)*ppu, max(ys)*ppu

def render_set(frames, ppu, rels, ss, pitch=PITCH, edge=None):
    """frames: list of models; renders every model at every rel on a shared, auto-sized canvas,
    then crops to the union of all pixels. Returns (list of HxW arrays, W, H, ax, ay)."""
    x0, x1, y0, y1 = bounds([(m, None) for m in frames], ppu, rels, pitch)
    W = int(math.ceil(x1 - x0)) + 4; H = int(math.ceil(y1 - y0)) + 4
    ax = -x0 + 2; ay = y1 + 2
    ax = round(ax); ay = round(ay)
    imgs = []
    for m in frames:
        for r in rels:
            imgs.append(render(m, W, H, ppu, (ax, ay), r, ss=ss, pitch=pitch, edge=edge))
            sys.stdout.write("."); sys.stdout.flush()
    U = np.zeros((H, W), bool)
    for im in imgs: U |= im > 0
    yy, xx = np.nonzero(U)
    cx0, cx1, cy0, cy1 = xx.min(), xx.max()+1, yy.min(), yy.max()+1
    imgs = [im[cy0:cy1, cx0:cx1] for im in imgs]
    return imgs, cx1 - cx0, cy1 - cy0, ax - cx0, ay - cy0

def render_prop3d(model, ppu, rel, pitch=PITCH, ss=4, edge=None):
    imgs, W, H, ax, ay = render_set([model], ppu, [rel], ss, pitch, edge)
    pal, _ = model.palette()
    return {"w": W, "h": H, "px": imgs[0].tolist(), "pal": pal}

# ================================================================================================
# racers (kart units: ~2.4 long, ground at z=0)
# ================================================================================================
TIRE = mat([(84, 84, 96), (50, 50, 60), (28, 28, 36)])
HUB = mat([(230, 230, 240), (165, 165, 180), (100, 100, 118)])
DARK = mat([(70, 60, 72), (34, 28, 38)], w=4)
EYEW = mat([(255, 255, 255)], w=4)

def driver_arms(m, sx, sy, z, mat_, wheel_x, wheel_mat="tire", wheel_z=None):
    wz = z if wheel_z is None else wheel_z
    with m.tagged("arms"):
        _arms(m, sx, sy, z, mat_, wheel_x, wheel_mat, wz)

def _arms(m, sx, sy, z, mat_, wheel_x, wheel_mat, wz):
    m.box(sx, wheel_x, sy, sy+0.13, z-0.07, z+0.07, mat_)
    m.box(sx, wheel_x, -sy-0.13, -sy, z-0.07, z+0.07, mat_)
    m.box(wheel_x, wheel_x+0.08, -0.22, 0.22, wz-0.12, wz+0.12, wheel_mat)

def racer_bruno():
    m = Model(dict(
        body=mat([(255, 176, 92), (236, 122, 44), (184, 76, 30), (112, 46, 26)]),
        crane=mat([(255, 222, 96), (224, 164, 40), (150, 100, 30)]),
        tire=TIRE, hub=HUB,
        fur=mat([(246, 218, 170), (216, 170, 114), (160, 112, 70)]),
        furd=mat([(176, 116, 74), (118, 72, 46)]),
        cap=mat([(100, 156, 244), (56, 94, 196), (30, 50, 124)]),
        dark=DARK, eye=EYEW,
        lamp=mat([(255, 250, 200), (255, 200, 80)], glow=True, w=3),
    ))
    with m.at(s=1.02):
        m.box(-0.95, 0.55, -0.55, 0.55, 0.20, 0.48, "body")               # tub
        m.box(0.50, 1.15, -0.52, 0.52, 0.20, 0.66, "body", tx=0.06)        # truck hood
        m.box(1.12, 1.19, -0.40, 0.40, 0.28, 0.58, "hub")                  # grille
        m.box(1.10, 1.20, 0.30, 0.46, 0.50, 0.62, "lamp"); m.box(1.10, 1.20, -0.46, -0.30, 0.50, 0.62, "lamp")
        m.box(1.15, 1.28, -0.62, 0.62, 0.14, 0.30, "hub")                  # bumper
        m.box(-1.18, -0.55, -0.58, 0.58, 0.20, 0.62, "body")               # tow bed
        m.box(-1.22, -1.12, -0.45, 0.45, 0.16, 0.30, "hub")                # rear bumper
        m.box(-1.0, -0.62, -0.22, 0.22, 0.62, 0.82, "crane")               # crane base
        m.cyl((-0.85, 0, 0.75), (-1.22, 0, 1.55), 0.08, m="crane", n=6)    # boom
        m.cyl((-0.80, 0.0, 0.66), (-1.05, 0, 1.15), 0.05, m="crane", n=6)  # strut
        m.cyl((-1.24, 0, 1.55), (-1.24, 0, 1.05), 0.02, m="dark", n=4)     # cable
        m.box(-1.30, -1.18, -0.05, 0.05, 0.92, 1.06, "hub")                # hook
        m.box(-1.36, -1.30, -0.05, 0.05, 0.92, 1.0, "hub")
        m.ellip((-0.80, 0, 0.90), 0.09, "lamp", rows=3, cols=6)            # beacon
        m.wheel(0.78, 0.66, 0.30, 0.30, 0.30)
        m.wheel(0.78, -0.66, 0.30, 0.30, 0.30)
        m.wheel(-0.72, 0.68, 0.40, 0.40, 0.36)
        m.wheel(-0.72, -0.68, 0.40, 0.40, 0.36)
        with m.tagged("driver"):
            m.box(-0.50, -0.02, -0.34, 0.34, 0.40, 0.98, "cap", tx=0.04, ty=0.04)    # overalls
            driver_arms(m, -0.25, 0.28, 0.72, "fur", 0.40)
            m.ellip((-0.20, 0, 1.20), (0.32, 0.40, 0.29), "fur")                       # wide head
            m.ellip((0.06, 0.19, 1.07), (0.16, 0.17, 0.15), "fur")                     # jowls
            m.ellip((0.06, -0.19, 1.07), (0.16, 0.17, 0.15), "fur")
            m.ellip((0.12, 0, 1.15), (0.12, 0.17, 0.10), "fur")                        # muzzle
            m.ellip((0.22, 0, 1.19), (0.06, 0.08, 0.05), "dark", rows=3, cols=6)       # nose
            m.box(0.05, 0.10, 0.10, 0.18, 1.28, 1.34, "dark"); m.box(0.05, 0.10, -0.18, -0.10, 1.28, 1.34, "dark")
            m.box(-0.32, -0.08, 0.36, 0.48, 0.92, 1.30, "furd", tx=0.02)               # droopy ears
            m.box(-0.32, -0.08, -0.48, -0.36, 0.92, 1.30, "furd", tx=0.02)
            m.cyl((-0.22, 0, 1.34), (-0.22, 0, 1.52), 0.30, 0.26, "cap", n=10)         # cap
            m.box(0.02, 0.36, -0.22, 0.22, 1.36, 1.41, "cap")                          # brim
    return m, dict(focus=(-0.12, 0, 1.24), ppu=30)

def racer_pip():
    m = Model(dict(
        pod=mat([(206, 222, 240), (148, 168, 196), (98, 114, 146), (60, 70, 100)]),
        trim=mat([(80, 200, 230), (40, 130, 180), (24, 76, 120)]),
        glow=mat([(170, 255, 255), (60, 220, 240)], glow=True, w=2),
        white=mat([(255, 252, 240), (226, 220, 200), (160, 152, 140)]),
        orange=mat([(255, 170, 70), (230, 110, 40), (150, 60, 30)]),
        dark=mat([(60, 66, 92), (32, 34, 52)], w=2),
        eye=mat([(200, 255, 255), (80, 230, 255)], glow=True, w=5),
        red=mat([(255, 90, 90), (200, 40, 50)], glow=True, w=3),
        jet=mat([(70, 226, 255), (30, 150, 230)], glow=True, w=2),
    ))
    m.ellip((0.0, 0, 0.46), (0.98, 0.62, 0.27), "pod", rows=6, cols=14)        # pod hull
    m.ellip((0.0, 0, 0.40), (1.04, 0.68, 0.09), "trim", rows=4, cols=14)       # rim band
    m.ellip((0.05, 0, 0.25), (0.62, 0.42, 0.08), "glow", rows=3, cols=12)      # underside glow
    m.cyl((-0.80, 0, 0.50), (-1.10, 0, 0.52), 0.24, 0.30, "trim", n=10, cap1="eye", ring="jet", inner=0.5)   # thruster
    m.box(-0.98, -0.55, -0.05, 0.05, 0.55, 0.94, "orange", tx=0.15)            # tail fin
    m.box(-0.98, -0.72, -0.36, 0.36, 0.88, 0.95, "orange", tx=0.04)            # T-tail stabilizer
    m.box(0.62, 0.95, -0.38, 0.38, 0.44, 0.52, "trim", tx=0.08)                # nose stripe
    with m.tagged("driver"):
        m.box(-0.36, 0.02, -0.22, 0.22, 0.58, 0.88, "white")                   # body
        driver_arms(m, -0.18, 0.20, 0.72, "dark", 0.30, "orange")
        m.box(-0.36, 0.14, -0.30, 0.30, 0.86, 1.32, "white", tx=0.02, ty=0.02)   # boxy head
        m.box(0.12, 0.18, -0.25, 0.25, 0.96, 1.24, "dark")                     # visor
        m.box(0.16, 0.21, -0.11, 0.11, 1.01, 1.19, "eye")                      # big eye
        m.cyl((-0.11, 0.30, 1.10), (-0.11, 0.36, 1.10), 0.09, m="orange", n=6) # ear bolts
        m.cyl((-0.11, -0.30, 1.10), (-0.11, -0.36, 1.10), 0.09, m="orange", n=6)
        m.box(-0.40, -0.36, -0.20, 0.20, 0.96, 1.24, "dark")                   # back panel
        m.box(-0.43, -0.40, -0.13, 0.13, 1.05, 1.15, "eye")                    # status light bar
        m.cyl((-0.14, 0, 1.32), (-0.14, 0, 1.64), 0.035, m="dark", n=4)        # antenna
        m.ellip((-0.14, 0, 1.74), 0.15, "red", rows=4, cols=8)
    return m, dict(focus=(-0.1, 0, 1.30), ppu=27)

def racer_hex():
    m = Model(dict(
        iron=mat([(124, 120, 144), (78, 74, 96), (48, 44, 64), (30, 28, 42)]),
        brew=mat([(190, 255, 120), (100, 225, 70)], glow=True, w=2),
        tire=TIRE, hub=HUB,
        straw=mat([(248, 220, 128), (206, 164, 72), (140, 104, 44)]),
        wood=mat([(170, 110, 64), (110, 68, 40)]),
        skin=mat([(176, 228, 124), (124, 184, 82), (74, 124, 52)]),
        robe=mat([(196, 124, 236), (144, 74, 196), (88, 42, 134)]),
        hat=mat([(110, 76, 168), (72, 46, 118), (42, 26, 74)]),
        band=mat([(255, 214, 90), (200, 150, 40)], w=2),
        dark=DARK,
    ))
    m.ellip((0, 0, 0.56), (0.74, 0.70, 0.44), "iron", rows=6, cols=14)         # cauldron belly
    m.cyl((0, 0, 0.80), (0, 0, 0.96), 0.62, 0.66, "iron", n=14, cap1="brew")   # rim + brew
    m.cyl((0, 0, 0.94), (0, 0, 1.0), 0.70, 0.70, "iron", n=14, cap1="brew", ring="iron", inner=0.86)
    for (bx, by, br) in ((0.32, 0.25, 0.08), (0.30, -0.30, 0.06), (0.42, 0.0, 0.05)):
        m.ellip((bx, by, 1.02 + br), br, "brew", rows=3, cols=6)               # bubbles
    for (lx, ly) in ((0.42, 0.42), (0.42, -0.42), (-0.42, 0.42), (-0.42, -0.42)):
        m.box(lx-0.06, lx+0.06, ly-0.06, ly+0.06, 0.12, 0.3, "iron")           # stubby legs
    m.wheel(0.48, 0.62, 0.18, 0.18, 0.16); m.wheel(0.48, -0.62, 0.18, 0.18, 0.16)
    m.wheel(-0.48, 0.62, 0.20, 0.20, 0.18); m.wheel(-0.48, -0.62, 0.20, 0.20, 0.18)
    m.cyl((-0.70, 0, 0.78), (0.98, 0, 0.90), 0.045, m="wood", n=6)             # broomstick
    m.cyl((-0.62, 0, 0.77), (-1.26, 0, 0.63), 0.10, 0.30, "straw", n=10)       # bristles exhaust
    m.cyl((-0.66, 0, 0.775), (-0.74, 0, 0.76), 0.12, m="band", n=8)            # binding
    with m.tagged("driver"):
        m.cyl((-0.16, 0, 0.75), (-0.16, 0, 1.18), 0.34, 0.18, "robe", n=10)    # robe
        driver_arms(m, -0.12, 0.17, 1.02, "robe", 0.40, "wood", 0.95)
        m.ellip((-0.12, 0, 1.28), (0.21, 0.22, 0.22), "skin")                  # head
        m.cyl((0.05, 0, 1.30), (0.26, 0, 1.22), 0.06, 0.01, "skin", n=6)       # pointy nose
        m.box(0.05, 0.09, 0.07, 0.13, 1.33, 1.38, "dark"); m.box(0.05, 0.09, -0.13, -0.07, 1.33, 1.38, "dark")
        m.box(-0.34, -0.14, -0.20, 0.20, 1.02, 1.32, "dark")                   # hair
        m.cyl((-0.13, 0, 1.40), (-0.13, 0, 1.45), 0.46, m="hat", n=14)         # brim
        m.cyl((-0.13, 0, 1.45), (-0.13, 0, 1.52), 0.25, 0.23, "band", n=10)
        m.cyl((-0.13, 0, 1.52), (-0.22, 0, 1.86), 0.23, 0.11, "hat", n=10)     # crooked cone
        m.cyl((-0.22, 0, 1.86), (-0.42, 0, 2.06), 0.11, 0.0, "hat", n=8)
    return m, dict(focus=(-0.12, 0, 1.42), ppu=26, pitch=0)

def racer_gull():
    m = Model(dict(
        wood=mat([(222, 152, 92), (172, 106, 60), (116, 70, 40), (72, 42, 26)]),
        trim=mat([(110, 220, 210), (50, 150, 156)]),
        tire=TIRE,
        sail=mat([(255, 248, 224), (226, 214, 180), (176, 160, 124)]),
        flag=mat([(64, 60, 80), (36, 34, 48)], w=1.5),
        feather=mat([(255, 255, 255), (220, 224, 238), (162, 168, 194)]),
        beak=mat([(255, 224, 70), (242, 162, 32)], w=2),
        coat=mat([(244, 84, 72), (194, 42, 48), (122, 26, 36)]),
        hat=mat([(84, 84, 126), (52, 52, 88), (30, 30, 56)]),
        gold=mat([(255, 220, 100), (206, 154, 40)], w=1.5),
        dark=DARK,
    ))
    # boat hull: midship hexahedron + pointed bow
    m.hexa([(-1.0, -0.36, 0.20), (0.62, -0.36, 0.20), (0.62, 0.36, 0.20), (-1.0, 0.36, 0.20),
            (-1.10, -0.58, 0.66), (0.66, -0.58, 0.66), (0.66, 0.58, 0.66), (-1.10, 0.58, 0.66)], "wood")
    m.hexa([(0.62, -0.36, 0.20), (1.12, 0.0, 0.28), (1.12, 0.0, 0.28), (0.62, 0.36, 0.20),
            (0.66, -0.58, 0.66), (1.34, 0.0, 0.80), (1.34, 0.0, 0.80), (0.66, 0.58, 0.66)], "wood")
    m.box(-1.12, 0.66, 0.56, 0.62, 0.62, 0.72, "trim"); m.box(-1.12, 0.66, -0.62, -0.56, 0.62, 0.72, "trim")
    m.box(-1.14, -1.06, -0.56, 0.56, 0.58, 0.72, "trim")
    m.box(-0.95, 0.55, 0.585, 0.60, 0.34, 0.40, "wood"); m.box(-0.95, 0.55, -0.60, -0.585, 0.34, 0.40, "wood")
    m.wheel(0.62, 0.62, 0.24, 0.24, 0.22, hub="sail"); m.wheel(0.62, -0.62, 0.24, 0.24, 0.22, hub="sail")
    m.wheel(-0.70, 0.64, 0.28, 0.28, 0.26, hub="sail"); m.wheel(-0.70, -0.64, 0.28, 0.28, 0.26, hub="sail")
    m.cyl((-0.92, 0.42, 0.6), (-0.92, 0.42, 1.96), 0.055, m="wood", n=6)      # stern flagpole
    with m.at((-0.92, 0.42, 0), rz=-50):                                        # flag streams back-outward
        m.box(-0.50, -0.02, -0.02, 0.02, 1.56, 1.94, "flag")
        m.box(-0.34, -0.16, -0.035, 0.035, 1.66, 1.84, "sail")
    m.box(0.95, 1.30, -0.03, 0.03, 0.72, 0.80, "coat")                         # bow pennant stripe
    with m.tagged("driver"):
        m.box(-0.50, -0.08, -0.28, 0.28, 0.56, 1.02, "coat", tx=0.04, ty=0.04)
        driver_arms(m, -0.25, 0.22, 0.82, "coat", 0.30, "wood")
        m.ellip((-0.26, 0, 1.24), 0.29, "feather")                             # round white head
        m.cyl((-0.02, 0, 1.22), (0.36, 0, 1.15), 0.12, 0.03, "beak", n=8)      # big beak
        m.box(-0.02, 0.06, 0.10, 0.17, 1.30, 1.37, "dark")                     # eye
        m.box(-0.01, 0.07, -0.20, -0.08, 1.27, 1.39, "dark")                   # eyepatch
        m.cyl((-0.26, 0, 1.42), (-0.26, 0, 1.49), 0.56, 0.56, "gold", n=3)     # tricorn trim
        m.ellip((-0.42, -0.20, 1.80), (0.10, 0.08, 0.20), "coat", rows=4, cols=6)   # red plume
        m.cyl((-0.26, 0, 1.46), (-0.26, 0, 1.62), 0.45, 0.36, "hat", n=3)      # tricorn
        m.ellip((-0.26, 0, 1.56), (0.24, 0.24, 0.15), "hat")
    return m, dict(focus=(-0.16, 0, 1.30), ppu=30)

def racer_rex():
    m = Model(dict(
        rex=mat([(156, 234, 96), (94, 188, 62), (52, 132, 46), (30, 86, 36)]),
        belly=mat([(252, 240, 190), (216, 200, 140)]),
        bone=mat([(255, 255, 244), (222, 216, 196), (160, 150, 130)], w=1.5),
        log=mat([(196, 134, 78), (144, 92, 52), (92, 58, 32)]),
        logend=mat([(240, 200, 140), (196, 152, 96)]),
        stone=mat([(196, 190, 180), (142, 138, 132), (92, 90, 94)]),
        dark=DARK, eye=EYEW,
        spike=mat([(255, 170, 70), (210, 110, 40)]),
    ))
    for y in (-0.40, 0.0, 0.40):                                              # log raft
        m.cyl((-1.0, y, 0.40), (0.95, y, 0.40), 0.21, m="log", n=8, cap="logend")
    m.box(-0.75, -0.25, -0.40, 0.40, 0.55, 0.70, "stone")                      # stone seat slab
    for x, r in ((0.70, 0.30), (-0.68, 0.36)):
        m.cyl((x, -0.80, r), (x, 0.80, r), 0.06, m="bone", n=6)               # bone axles
        m.wheel(x, 0.66, r, r, 0.26, "stone", "stone", n=9)
        m.wheel(x, -0.66, r, r, 0.26, "stone", "stone", n=9)
    m.cyl((1.05, -0.52, 0.42), (1.05, 0.52, 0.42), 0.07, m="bone", n=6)       # bone bumper
    for y in (-0.56, 0.56):
        m.ellip((1.05, y + 0.0, 0.50), 0.09, "bone", rows=3, cols=6)
        m.ellip((1.05, y, 0.34), 0.09, "bone", rows=3, cols=6)
    with m.tagged("driver"):
        m.ellip((-0.36, 0, 0.86), (0.32, 0.30, 0.34), "rex")                   # body
        m.ellip((-0.22, 0, 0.84), (0.20, 0.22, 0.26), "belly")
        m.cyl((-0.56, 0, 0.80), (-1.0, 0, 0.84), 0.17, 0.10, "rex", n=8)       # tail
        m.cyl((-1.0, 0, 0.84), (-1.36, 0, 1.02), 0.10, 0.02, "rex", n=8)
        with m.tagged("arms"):
            m.box(-0.10, 0.20, 0.16, 0.22, 0.88, 0.94, "rex"); m.box(-0.10, 0.20, -0.22, -0.16, 0.88, 0.94, "rex")  # tiny arms
            m.box(0.20, 0.27, -0.20, 0.20, 0.82, 1.0, "log")                   # wheel
        m.ellip((-0.10, 0, 1.30), (0.33, 0.30, 0.27), "rex")                   # big head
        m.ellip((0.24, 0, 1.24), (0.27, 0.22, 0.15), "rex")                    # snout
        m.ellip((0.20, 0, 1.10), (0.22, 0.18, 0.08), "belly")                  # jaw
        m.box(0.10, 0.48, -0.17, 0.17, 1.12, 1.17, "bone")                     # teeth row
        m.box(0.02, 0.10, 0.18, 0.27, 1.38, 1.47, "eye"); m.box(0.02, 0.10, -0.27, -0.18, 1.38, 1.47, "eye")
        m.box(0.08, 0.12, 0.20, 0.27, 1.39, 1.45, "dark"); m.box(0.08, 0.12, -0.27, -0.20, 1.39, 1.45, "dark")
        for (sx, sz) in ((-0.22, 1.56), (-0.42, 1.38), (-0.62, 1.14), (-0.86, 0.98)):
            m.cyl((sx, 0, sz - 0.10), (sx - 0.06, 0, sz + 0.06), 0.07, 0.0, "spike", n=5)
    return m, dict(focus=(0.0, 0, 1.22), ppu=30)

def racer_volt():
    m = Model(dict(
        bike=mat([(88, 88, 112), (50, 50, 70), (28, 28, 42)]),
        neon=mat([(170, 255, 255), (0, 224, 255)], glow=True, w=2.5),
        tire=mat([(60, 60, 74), (34, 34, 46), (20, 20, 30)]),
        hub=mat([(110, 110, 140), (60, 60, 84)]),
        suit=mat([(132, 100, 214), (86, 60, 160), (50, 34, 100)]),
        helmet=mat([(248, 248, 255), (196, 200, 220), (124, 128, 156)]),
        visor=mat([(255, 140, 240), (240, 60, 210)], glow=True, w=4),
        hair=mat([(255, 128, 232), (228, 56, 194), (150, 26, 134)], w=1.5),
        skin=mat([(255, 214, 180), (220, 160, 124)]),
    ))
    # long low light-cycle: nose wedge + tail
    m.hexa([(-1.10, -0.30, 0.18), (1.25, -0.16, 0.16), (1.25, 0.16, 0.16), (-1.10, 0.30, 0.18),
            (-1.05, -0.26, 0.62), (0.85, -0.18, 0.46), (0.85, 0.18, 0.46), (-1.05, 0.26, 0.62)], "bike")
    m.box(-1.12, -0.40, -0.62, 0.62, 0.40, 0.62, "bike", tx=0.08, ty=0.06)    # rear fender pod
    m.box(-0.95, 1.20, 0.29, 0.33, 0.30, 0.36, "neon"); m.box(-0.95, 1.20, -0.33, -0.29, 0.30, 0.36, "neon")
    m.box(-1.16, -1.11, -0.56, 0.56, 0.46, 0.52, "neon")                       # tail light strip
    m.box(-1.0, -0.45, 0.60, 0.64, 0.46, 0.50, "neon"); m.box(-1.0, -0.45, -0.64, -0.60, 0.46, 0.50, "neon")
    m.wheel(0.86, 0.0, 0.30, 0.30, 0.20, rim="neon")                            # front wheel
    m.wheel(-0.72, 0.56, 0.34, 0.34, 0.22, rim="neon")
    m.wheel(-0.72, -0.56, 0.34, 0.34, 0.22, rim="neon")
    with m.tagged("driver"):
        with m.at((-0.30, 0, 0.60), ry=35):
            m.box(-0.18, 0.18, -0.24, 0.24, 0.0, 0.55, "suit", tx=0.02)          # leaning torso
        driver_arms(m, 0.0, 0.20, 0.86, "suit", 0.48, "bike", 0.84)
        def vis(x, y, z): return "visor" if (-0.2 < z < 0.25 and x > 0.25) else None
        m.ellip((0.05, 0, 1.14), (0.30, 0.24, 0.23), "helmet", rows=7, cols=12, fn=vis)
        m.cyl((0.0, 0, 1.30), (-0.20, 0, 1.40), 0.12, 0.02, "helmet", n=6)        # fin
        for (y, z, ex, ez) in ((0.12, 1.22, -0.62, 1.34), (-0.12, 1.22, -0.62, 1.34), (0.0, 1.32, -0.56, 1.58),
                               (0.17, 1.10, -0.55, 1.08), (-0.17, 1.10, -0.55, 1.08)):
            m.cyl((-0.14, y, z), (ex, y*1.6, ez), 0.10, 0.0, "hair", n=5)          # spiky hair
    return m, dict(focus=(0.0, 0, 1.12), ppu=32)

def racer_clanks():
    m = Model(dict(
        steel=mat([(236, 242, 252), (182, 192, 208), (122, 132, 152), (72, 80, 98)]),
        gold=mat([(255, 232, 112), (222, 172, 40), (150, 106, 22)], w=1.3),
        red=mat([(255, 104, 92), (222, 42, 52), (140, 20, 36)]),
        blue=mat([(96, 146, 255), (52, 84, 204), (30, 46, 124)]),
        tire=TIRE, hub=HUB, dark=DARK,
    ))
    m.box(-0.95, 0.70, -0.56, 0.56, 0.20, 0.62, "steel", tx=0.05, ty=0.05)    # armored tub
    m.wedge(0.70, 1.05, -0.48, 0.48, 0.20, 0.56, 0.30, "steel")                # nose
    m.box(-0.98, 0.72, 0.50, 0.56, 0.58, 0.66, "gold"); m.box(-0.98, 0.72, -0.56, -0.50, 0.58, 0.66, "gold")
    m.box(-1.0, -0.94, -0.56, 0.56, 0.58, 0.66, "gold")
    shield = [(-0.44, 0.92), (0.44, 0.92), (0.44, 0.52), (0.0, 0.14), (-0.44, 0.52)]
    m.prism(shield, 1.04, 1.10, "gold", plane="yz")                              # kite shield
    m.prism([(y*0.84, 0.14 + (z-0.14)*0.88 + 0.04) for y, z in shield], 1.10, 1.13, "blue", plane="yz")
    m.box(1.10, 1.15, -0.05, 0.05, 0.24, 0.86, "gold"); m.box(1.10, 1.15, -0.34, 0.34, 0.62, 0.70, "gold")
    for x, r in ((0.66, 0.27), (-0.66, 0.32)):
        for y in (0.66, -0.66):
            m.wheel(x, y, r, r, 0.26)
    for y in (0.80, -0.80):                                                      # scythe hub spikes
        m.cyl((-0.66, y, 0.32), (-0.66, y*1.32, 0.32), 0.07, 0.0, "gold", n=6)
    m.cyl((-0.90, 0.46, 0.5), (-0.90, 0.46, 1.94), 0.03, m="gold", n=5)          # banner pole
    m.ellip((-0.90, 0.46, 1.96), 0.06, "gold", rows=3, cols=6)
    m.box(-0.93, -0.88, 0.46, 0.86, 1.48, 1.94, "red")                           # banner
    m.box(-0.94, -0.87, 0.60, 0.72, 1.56, 1.86, "gold")
    with m.tagged("driver"):
        m.box(-0.50, -0.04, -0.30, 0.30, 0.56, 1.0, "steel", tx=0.04, ty=0.04)
        m.ellip((-0.26, 0.30, 0.98), (0.14, 0.14, 0.10), "steel"); m.ellip((-0.26, -0.30, 0.98), (0.14, 0.14, 0.10), "steel")
        driver_arms(m, -0.22, 0.24, 0.82, "steel", 0.36)
        m.cyl((-0.24, 0, 1.00), (-0.24, 0, 1.46), 0.25, 0.23, "steel", n=12)      # bucket helm
        m.box(-0.04, 0.03, -0.17, 0.17, 1.24, 1.29, "dark")                        # visor slit
        m.box(-0.03, 0.03, -0.03, 0.03, 1.08, 1.24, "dark")
        m.cyl((-0.24, 0, 1.36), (-0.24, 0, 1.41), 0.255, m="gold", n=12)
        m.ellip((-0.36, 0, 1.58), (0.30, 0.08, 0.14), "red")                       # plume
        m.ellip((-0.58, 0, 1.48), (0.16, 0.07, 0.12), "red")
    return m, dict(focus=(-0.16, 0, 1.24), ppu=31)

def racer_granny():
    m = Model(dict(
        cream=mat([(255, 248, 222), (226, 208, 166), (160, 136, 104)]),
        red=mat([(255, 94, 84), (218, 42, 48), (140, 22, 36)]),
        rocket=mat([(236, 240, 248), (150, 156, 176)]),
        flame=mat([(255, 240, 140), (255, 150, 40)], glow=True, w=2),
        wicker=mat([(214, 160, 88), (140, 96, 46)]),
        hair=mat([(244, 244, 250), (198, 198, 214), (142, 142, 164)]),
        skin=mat([(255, 216, 182), (230, 170, 136), (170, 116, 90)]),
        brass=mat([(255, 214, 100), (196, 146, 50)], w=2),
        lens=mat([(200, 255, 255), (100, 200, 240)], glow=True, w=4),
        cardi=mat([(194, 162, 234), (142, 112, 194), (90, 70, 142)]),
        tire=TIRE, dark=DARK,
    ))
    m.box(-0.80, 0.82, -0.40, 0.40, 0.16, 0.34, "cream")                       # deck
    m.box(-0.90, -0.20, -0.44, 0.44, 0.18, 0.60, "cream", tx=0.06, ty=0.04)   # rear housing
    m.box(0.55, 0.90, -0.40, 0.40, 0.18, 0.40, "cream", tx=0.05)              # front cowl
    m.box(-0.62, -0.12, -0.34, 0.34, 0.58, 0.72, "red")                        # seat
    m.box(-0.70, -0.56, -0.32, 0.32, 0.60, 1.08, "red")                        # seat back
    m.cyl((0.74, 0, 0.36), (0.52, 0, 1.04), 0.06, m="cream", n=6)              # tiller
    m.box(0.48, 0.58, -0.34, 0.34, 1.00, 1.07, "dark")                         # handlebar
    m.box(0.62, 1.06, -0.30, 0.30, 0.52, 0.86, "wicker")                       # basket
    m.box(0.66, 1.02, -0.26, 0.26, 0.86, 0.88, "wicker")
    m.ellip((0.80, 0.10, 0.92), 0.07, "red", rows=3, cols=6); m.ellip((0.90, -0.08, 0.92), 0.06, "brass", rows=3, cols=6)
    m.wheel(0.74, 0.44, 0.19, 0.19, 0.16, hub="cream"); m.wheel(0.74, -0.44, 0.19, 0.19, 0.16, hub="cream")
    m.wheel(-0.64, 0.48, 0.22, 0.22, 0.18, hub="cream"); m.wheel(-0.64, -0.48, 0.22, 0.22, 0.18, hub="cream")
    m.cyl((-1.30, 0, 0.84), (-0.74, 0, 0.84), 0.17, m="rocket", n=10)          # strapped rocket
    m.cyl((-0.74, 0, 0.84), (-0.66, 0, 0.84), 0.17, 0.10, "red", n=10)
    m.box(-1.30, -1.08, -0.02, 0.02, 0.84, 1.10, "red"); m.box(-1.30, -1.08, -0.02, 0.02, 0.58, 0.84, "red")   # fins
    m.box(-1.30, -1.08, 0.0, 0.27, 0.82, 0.86, "red"); m.box(-1.30, -1.08, -0.27, 0.0, 0.82, 0.86, "red")
    m.box(-1.0, -0.94, -0.19, 0.19, 0.64, 1.04, "dark")                        # strap
    m.cyl((-1.30, 0, 0.84), (-1.58, 0, 0.84), 0.12, 0.0, "flame", n=8)        # exhaust flame
    with m.tagged("driver"):
        m.box(-0.50, -0.10, -0.27, 0.27, 0.62, 1.02, "cardi", tx=0.04, ty=0.04)
        driver_arms(m, -0.24, 0.21, 0.86, "cardi", 0.42, "dark", 1.04)
        m.ellip((-0.26, 0, 1.24), 0.23, "skin")                                # head
        m.ellip((-0.30, 0, 1.31), (0.24, 0.25, 0.19), "hair")                  # hair
        m.ellip((-0.48, 0, 1.48), 0.13, "hair", rows=4, cols=8)                # bun
        for y in (0.10, -0.10):                                                # goggles
            m.cyl((-0.06, y, 1.27), (0.04, y, 1.27), 0.10, m="brass", n=8, cap1="lens")
        m.box(-0.40, -0.06, -0.25, 0.25, 1.25, 1.30, "dark")                   # strap
        m.cyl((-0.26, 0, 1.0), (-0.26, 0, 1.10), 0.22, m="red", n=10)          # scarf
        for i, (x0, x1, z, y) in enumerate(((-0.40, -0.70, 1.10, 0.12), (-0.66, -0.96, 1.22, 0.22), (-0.92, -1.20, 1.36, 0.32))):
            m.box(x1, x0, y - 0.08, y + 0.08, z - 0.09, z + 0.09, "red")   # scarf tail streams up and out
    return m, dict(focus=(-0.16, 0, 1.24), ppu=34)

RACERS = [racer_bruno, racer_pip, racer_hex, racer_gull, racer_rex, racer_volt, racer_clanks, racer_granny]
RACER_NAMES = ["BRUNO", "PIP-8", "MADAME HEX", "CAP'N GULLBEARD", "TINY REX", "VOLT VEGA", "SIR CLANKS", "GRANNY GOGGLES"]

def render_racer(model):
    frames = []
    for k in range(N_ANGLES):
        # no ink ring round a kart: the model is drawn a little larger instead, so its own
        # surfaces fill the pixels the ring used to take (SR_KART_OUTLINE=1 bakes the old look)
        if os.environ.get("SR_KART_OUTLINE"):
            frames.append(render(model, SPR, SPR, KART_PPU, ANCHOR, 2*math.pi*k/N_ANGLES, ss=4, edge=None))
        else:
            frames.append(render(model, SPR, SPR, KART_PPU * NO_OUTLINE_SCALE, ANCHOR, 2*math.pi*k/N_ANGLES, ss=4, edge=None, ring=False))
    return frames

def hand_portrait(i):
    """Racer i's character-select portrait. The portraits are hand-drawn pixel art kept in
    tools/portraits.py (explicit 32x32 grids, no ink outline, at most 16 colours each); the
    3D driver models are not used for them."""
    px, pal = portraits_data.indexed(i)
    assert len(px) == SPR and all(len(row) == SPR for row in px) and len(pal) <= 32
    return {"w": SPR, "h": SPR, "px": px, "pal": pal}

# ================================================================================================
# set pieces (modeled in world texels, ground at z=0, facing +x)
# ================================================================================================
def sp_train_loco():
    m = Model(dict(
        green=mat([(110, 196, 130), (64, 140, 92), (38, 94, 64), (22, 60, 42)]),
        black=mat([(112, 112, 134), (70, 70, 90), (42, 42, 58), (26, 26, 38)]),
        red=mat([(246, 90, 66), (196, 44, 42), (124, 26, 32)]),
        brass=mat([(255, 226, 116), (214, 164, 54), (142, 102, 30)]),
        silver=mat([(232, 236, 244), (164, 170, 186)]),
        glass=mat([(150, 210, 240), (70, 120, 170)]),
        lamp=mat([(255, 252, 210), (255, 220, 110)], glow=True, w=2),
    ))
    m.box(-19, 18, -4.0, 4.0, 2.5, 4.2, "black")                                 # frame
    m.cyl((-7, 0, 8.4), (13, 0, 8.4), 4.3, m="green", n=14)                      # boiler
    m.cyl((13, 0, 8.4), (17.6, 0, 8.4), 4.5, m="black", n=14)                    # smokebox
    for x in (-2, 5, 12):
        m.cyl((x, 0, 8.4), (x + 0.8, 0, 8.4), 4.45, m="brass", n=14)             # bands
    m.cyl((14.8, 0, 12), (14.8, 0, 14.6), 1.2, m="black", n=10)                  # balloon stack
    m.cyl((14.8, 0, 14.6), (14.8, 0, 17.4), 1.2, 2.5, "black", n=10)
    m.ellip((5, 0, 12.4), (1.9, 1.9, 1.6), "brass")                               # dome
    m.ellip((-1, 0, 12.4), (1.3, 1.3, 1.0), "brass")
    m.box(16.0, 18.4, -1.2, 1.2, 12.4, 14.8, "black")                            # headlamp
    m.box(18.4, 18.9, -0.9, 0.9, 12.7, 14.5, "lamp")
    m.box(-19, -7, -4.6, 4.6, 4.2, 14.0, "green")                                # cab
    m.box(-20.2, -6.0, -5.2, 5.2, 14.0, 15.4, "red", tx=0.5, ty=0.5)             # roof
    for y in (4.62, -4.72):
        m.box(-16, -10, y, y + 0.1, 9.0, 12.8, "glass")                          # cab windows
    m.box(-7.1, -6.9, -3.6, 3.6, 10.0, 12.8, "glass")
    m.box(-19, 18, 4.0, 4.4, 3.6, 4.4, "red"); m.box(-19, 18, -4.4, -4.0, 3.6, 4.4, "red")
    m.hexa([(17.5, -4.2, 0.6), (22.5, -0.5, 0.6), (22.5, 0.5, 0.6), (17.5, 4.2, 0.6),
            (17.5, -4.2, 4.4), (18.5, -0.5, 4.4), (18.5, 0.5, 4.4), (17.5, 4.2, 4.4)], "red")   # cowcatcher
    for x in (-1.5, 4.6, 10.7):                                                  # drivers
        for y in (4.3, -4.3):
            m.wheel(x, y, 3.0, 3.0, 1.0, "black", "red", n=14)
    for x, r in ((15.6, 1.6), (-12.5, 2.0), (-16.8, 2.0)):
        for y in (4.1, -4.1):
            m.wheel(x, y, r, r, 0.9, "black", "red", n=10)
    for y in (4.9, -4.9):
        m.box(-1.5, 10.7, y - 0.2, y + 0.2, 2.5, 3.3, "silver")                  # side rods
    m.cyl((-16, 0, 15.4), (-16, 0, 16.2), 0.6, m="brass", n=8)                   # whistle
    return m

def sp_train_car():
    m = Model(dict(
        red=mat([(232, 92, 70), (186, 50, 46), (122, 30, 34), (76, 20, 26)]),
        black=mat([(112, 112, 134), (70, 70, 90), (42, 42, 58), (26, 26, 38)]),
        gold=mat([(255, 226, 116), (214, 164, 54)]),
        glass=mat([(255, 236, 160), (230, 180, 90)], glow=True),
        roof=mat([(150, 150, 170), (100, 100, 120), (62, 62, 80)]),
    ))
    m.box(-18, 18, -4.4, 4.4, 3.4, 13.0, "red")                                  # body
    m.box(-18.6, 18.6, -5.0, 5.0, 13.0, 14.2, "roof", ty=1.2)                    # roof
    m.box(-15, 15, -3.2, 3.2, 14.2, 15.4, "roof", ty=0.8)                        # clerestory
    for y in (4.4, -4.5):
        m.box(-18, 18, y, y + 0.1, 7.0, 7.7, "gold")
        for i in range(7):
            x = -14.5 + i*4.8
            m.box(x - 1.4, x + 1.4, y, y + 0.12, 8.4, 11.8, "glass")             # windows
    for x in (-18.3, 18.2):
        m.box(x, x + 0.1, -1.4, 1.4, 4.0, 11.5, "black")                         # end doors
    for x in (-19.6, 18.6):
        m.box(x, x + 1.0, -4.0, 4.0, 2.6, 3.4, "black")                          # platforms
    m.box(-17, 17, -3.8, 3.8, 2.0, 3.4, "black")
    for bx in (-12, 12):                                                         # bogies
        m.box(bx - 3.6, bx + 3.6, -3.6, 3.6, 1.2, 2.4, "black")
        for x in (bx - 2.2, bx + 2.2):
            for y in (3.6, -3.6):
                m.wheel(x, y, 1.6, 1.6, 0.8, "black", "gold", n=10)
    return m

def sp_snowball(frame):
    m = Model(dict(
        snow=mat([(255, 255, 255), (226, 236, 252), (180, 198, 230), (130, 148, 196)]),
        rock=mat([(150, 140, 136), (96, 90, 92)], w=2),
        stick=mat([(160, 110, 66), (104, 68, 40)], w=2),
    ))
    R = 17.0
    with m.at((0, 0, R), ry=frame*22.5):
        m.ellip((0, 0, 0), R, "snow", rows=9, cols=16)
        lumps = [(10, 0.0, 4.5), (55, 0.45, 4.0), (30, -0.55, 4.0), (75, -0.15, 3.5), (40, 0.85, 3.5), (80, -0.85, 3.5)]
        for q in range(4):                                     # 4-fold symmetric about the roll axis
            for (a, yy, r) in lumps:
                t = math.radians(a + q*90)
                s = math.sqrt(max(0.0, 1 - yy*yy))
                m.ellip((R*0.86*s*math.sin(t), R*0.86*yy, R*0.86*s*math.cos(t)), r, "snow", rows=4, cols=7)
            t = math.radians(q*90 + 40)
            m.ellip((R*0.97*math.sin(t)*0.94, R*0.3, R*0.97*math.cos(t)*0.94), 2.0, "rock", rows=3, cols=6)
            t2 = math.radians(q*90 + 80)
            p = np.array([math.sin(t2), -0.35, math.cos(t2)]); p /= np.linalg.norm(p)
            m.cyl(tuple(p*R*0.9), tuple(p*R*1.05 + np.array([1.8*math.cos(t2), 0, -1.8*math.sin(t2)])), 0.7, m="stick", n=5)
    return m

def sp_dragon(frame):
    m = Model(dict(
        scale=mat([(255, 116, 84), (222, 58, 52), (152, 32, 42), (92, 20, 32)]),
        belly=mat([(255, 224, 124), (226, 172, 72), (160, 112, 42)]),
        wing=mat([(255, 168, 96), (226, 108, 64), (156, 64, 48), (100, 40, 38)]),
        bone=mat([(255, 248, 226), (214, 200, 170), (150, 136, 110)]),
        eye=mat([(255, 255, 120), (255, 220, 40)], glow=True, w=4),
        dark=DARK,
    ))
    m.ellip((0, 0, 0), (10, 5.2, 5.0), "scale", rows=7, cols=12)               # body
    m.ellip((2, 0, -1.6), (7.5, 3.8, 3.4), "belly")
    m.cyl((7, 0, 1.5), (14, 0, 7), 3.4, 2.4, "scale", n=10)                    # neck
    m.cyl((14, 0, 7), (17.5, 0, 9), 2.4, 2.1, "scale", n=10)
    m.ellip((19, 0, 9.6), (3.8, 3.0, 2.6), "scale")                            # head
    m.ellip((23, 0, 9.0), (3.4, 2.0, 1.6), "scale")                            # snout
    m.ellip((22, 0, 7.8), (3.0, 1.7, 0.9), "belly")                            # jaw
    for y in (1.6, -1.6):
        m.cyl((17.5, y, 11.4), (13.5, y*1.6, 14.4), 0.8, 0.0, "bone", n=6)    # horns
        m.box(20.0, 21.6, y*1.2 - 0.5, y*1.2 + 0.5, 10.4, 11.4, "eye")
    tail = [((-8, 0, 0), 3.6), ((-17, 0, -1.5), 2.6), ((-26, 0, 0.5), 1.6), ((-33, 0, 3.0), 0.6)]
    for (a, ra), (b, rb) in zip(tail, tail[1:]):
        m.cyl(a, b, ra, rb, "scale", n=8)
    m.prism([(-33, 0), (-37, 3.2), (-39.5, 0), (-37, -3.2)], 2.6, 3.4, "scale", plane="xy")   # tail spade
    for (x, z) in ((6, 4.6), (2, 5.4), (-2, 5.4), (-6, 4.6), (-12, 2.0), (-18, 0.6), (-24, 1.6)):
        m.cyl((x, 0, z - 1.0), (x - 1.4, 0, z + 1.8), 0.9, 0.0, "bone", n=5)  # back spikes
    for (x, y) in ((5, 3.2), (5, -3.2), (-5, 3.4), (-5, -3.4)):               # tucked legs
        m.cyl((x, y, -2.5), (x + 1.0, y, -6.5), 1.4, 1.0, "scale", n=6)
        m.ellip((x + 1.6, y, -6.8), (1.4, 0.9, 0.6), "bone", rows=3, cols=6)
    flap = 32 if frame == 0 else -18
    for side in (1, -1):
        with m.at((2, side*4.0, 3.0), rx=side*flap):
            W = lambda x, y: (x, side*y)
            S, Wr = (3.0, 0.0), (-1.0, 15.0)
            T1, T2, T3, Rt = (-3.0, 34.0), (-12.0, 27.0), (-16.0, 15.0), (-9.0, 1.0)
            M12, M23, M3R = (-5.8, 26.0), (-10.0, 17.5), (-9.5, 8.0)
            for tri in ((S, Wr, Rt), (Wr, T1, M12), (Wr, M12, T2), (Wr, T2, M23), (Wr, M23, T3), (Wr, T3, M3R), (Wr, M3R, Rt)):
                m.prism([W(*p) for p in tri], -0.35, 0.35, "wing", plane="xy")
            for a, b in ((S, Wr), (Wr, T1), (Wr, T2), (Wr, T3)):
                m.cyl((a[0], side*a[1], 0), (b[0], side*b[1], 0), 0.75, 0.4, "scale", n=5)
    return m

def sp_ship():
    m = Model(dict(
        wood=mat([(206, 136, 80), (156, 98, 56), (108, 66, 38), (68, 42, 26)]),
        dwood=mat([(130, 84, 50), (86, 54, 32), (54, 34, 22)]),
        gold=mat([(255, 224, 110), (210, 158, 48), (140, 100, 30)]),
        red=mat([(236, 84, 70), (186, 42, 44), (120, 26, 34)]),
        sail=mat([(255, 250, 232), (234, 222, 192), (190, 174, 140), (140, 124, 96)]),
        flag=mat([(70, 66, 88), (40, 38, 52)]),
        white=mat([(255, 255, 255), (210, 210, 220)]),
        glass=mat([(255, 230, 140), (230, 170, 80)], glow=True),
        dark=mat([(50, 42, 46), (28, 22, 28)], w=2),
    ))
    m.hexa([(-28, -7, -4), (22, -7, -4), (22, 7, -4), (-28, 7, -4),
            (-31, -11, 8), (26, -11, 8), (26, 11, 8), (-31, 11, 8)], "wood")            # hull
    m.hexa([(22, -7, -4), (32, 0, -2), (32, 0, -2), (22, 7, -4),
            (26, -11, 8), (38, 0, 10), (38, 0, 10), (26, 11, 8)], "wood")               # bow
    m.box(-31.5, 26, 10.4, 11.4, 2.0, 4.0, "red"); m.box(-31.5, 26, -11.4, -10.4, 2.0, 4.0, "red")
    for y in (10.95, -11.05):
        for i in range(7):
            x = -22 + i*6.5
            m.box(x - 1.0, x + 1.0, y, y + 0.1, 2.4, 3.6, "dark")                       # gun ports
    m.box(-32, -17, -11, 11, 8, 17, "wood")                                             # stern castle
    m.box(-32.5, -16.5, -11.5, 11.5, 16.4, 17.4, "gold")
    m.box(-32.2, -31.9, -8, 8, 10.5, 14.5, "glass")                                     # stern windows
    m.box(18, 30, -9, 9, 8, 12.5, "wood")                                               # forecastle
    m.box(-17, 18, -10.8, 10.8, 8, 9, "dwood")                                          # deck rail
    m.cyl((36, 0, 9.5), (46, 0, 16), 0.7, m="dwood", n=6)                               # bowsprit
    masts = ((16, 40, 9.5), (0, 46, 11), (-16, 38, 8.5))
    for (mx, h, w) in masts:
        m.cyl((mx, 0, 8), (mx, 0, h), 0.9, 0.6, "dwood", n=6)
        with m.at((mx, 0, 0), rz=30):                                                   # braced yards
            x = 0.0
            for (z0, z1, hw) in ((14, 26, w), (27, h - 6, w*0.75)):
                m.hexa([(x + 0.8, -hw, z0), (x + 1.4, -hw, z0), (x + 1.4, hw, z0), (x + 0.8, hw, z0),
                        (x + 0.8, -hw*0.9, z1), (x + 1.4, -hw*0.9, z1), (x + 1.4, hw*0.9, z1), (x + 0.8, hw*0.9, z1)], "sail")
                m.hexa([(x + 1.4, -hw*0.8, z0 + 1.5), (x + 3.0, -hw*0.6, z0 + 3), (x + 3.0, hw*0.6, z0 + 3), (x + 1.4, hw*0.8, z0 + 1.5),
                        (x + 1.4, -hw*0.75, z1 - 1.5), (x + 3.0, -hw*0.55, z1 - 3), (x + 3.0, hw*0.55, z1 - 3), (x + 1.4, hw*0.75, z1 - 1.5)], "sail")
                m.box(x - 0.2, x + 0.8, -hw - 1, hw + 1, z1, z1 + 0.8, "dwood")         # yards
            m.box(x + 3.0, x + 3.2, -2.5, 2.5, 18, 22, "red")                          # sail emblem
    m.box(-0.6, 0.6, -0.2, 0.2, 40, 46, "dwood")
    m.box(-9.0, -0.4, -0.25, 0.25, 41.5, 46, "flag")                                    # jolly flag
    m.box(-6.4, -3.6, -0.35, 0.35, 42.6, 45, "white")
    for (x, h, _) in (masts[0], masts[2]):
        m.box(x - 6, x - 0.4, -0.2, 0.2, h - 3, h, "red")                               # pennants
    return m

def sp_dino(frame):
    m = Model(dict(
        skin=mat([(146, 214, 166), (94, 168, 122), (58, 118, 88), (36, 78, 62)]),
        belly=mat([(250, 236, 186), (214, 196, 140), (160, 140, 96)]),
        spot=mat([(70, 140, 110), (44, 96, 76)]),
        toe=mat([(240, 232, 210), (190, 178, 150)]),
        eye=EYEW, dark=DARK,
    ))
    m.ellip((0, 0, 25), (18, 10.5, 11), "skin", rows=8, cols=14)               # body
    m.ellip((2, 0, 20.5), (13, 8.5, 6), "belly")
    for (x, y, r) in ((-4, 4, 3.2), (4, -3, 2.8), (-9, -2, 2.4), (6, 5, 2.2), (-1, -6, 2.6)):
        m.ellip((x, y, 34.6 - abs(y)*0.25), (r, r, 1.4), "spot", rows=3, cols=7)
    neck = [((13, 0, 28), 6.5), ((21, 0, 35), 4.8), ((27, 0, 41), 3.6), ((30.5, 0, 45), 2.9)]
    for (a, ra), (b, rb) in zip(neck, neck[1:]):
        m.cyl(a, b, ra, rb, "skin", n=10)
        m.ellip(b, rb, "skin", rows=4, cols=8)
    m.ellip((34, 0, 46.2), (4.4, 2.8, 2.5), "skin")                            # head
    m.box(34.0, 35.4, 1.9, 2.7, 46.6, 48.0, "eye"); m.box(34.0, 35.4, -2.7, -1.9, 46.6, 48.0, "eye")
    m.box(34.8, 35.6, 2.4, 2.8, 46.8, 47.6, "dark"); m.box(34.8, 35.6, -2.8, -2.4, 46.8, 47.6, "dark")
    m.box(36.0, 38.4, -1.4, 1.4, 45.0, 45.4, "dark")                           # smile
    tail = [((-15, 0, 25), 7.5), ((-27, 0, 20), 4.8), ((-37, 0, 15.5), 2.8), ((-46, 0, 12.5), 0.8)]
    for (a, ra), (b, rb) in zip(tail, tail[1:]):
        m.cyl(a, b, ra, rb, "skin", n=10)
    swing = 14 if frame == 0 else -14
    for (x, y, ph) in ((10, 6.5, 1), (10, -6.5, -1), (-10, 6.5, -1), (-10, -6.5, 1)):
        a = swing*ph
        with m.at((x, y, 21), ry=a):
            L = 21.0/math.cos(math.radians(a))
            m.cyl((0, 0, 0), (0, 0, -L + 1.5), 4.4, 3.6, "skin", n=8)
            m.cyl((0, 0, -L + 1.5), (0, 0, -L), 4.2, 4.4, "toe", n=8)
    return m

def sp_blimp():
    m = Model(dict(
        hull=mat([(248, 250, 255), (210, 216, 232), (156, 164, 190), (104, 110, 140)]),
        red=mat([(255, 100, 84), (220, 48, 50), (144, 26, 38)]),
        blue=mat([(96, 150, 255), (52, 88, 206), (30, 50, 128)]),
        gondola=mat([(96, 96, 120), (58, 58, 78)]),
        glass=mat([(160, 230, 255), (80, 160, 220)], glow=True),
        prop=mat([(200, 200, 210), (130, 130, 146)]),
    ))
    def stripes(x, y, z):
        if 0.05 < x < 0.32: return "red"
        if -0.18 < x < 0.0: return "blue"
        if x > 0.93: return "red"
        return None
    m.ellip((0, 0, 0), (35, 11, 11), "hull", rows=10, cols=16, fn=stripes)
    for rx in (0, 90, 180, 270):
        with m.at(rx=rx):
            m.prism([(-22, 6.5), (-33, 3.5), (-37, 13), (-31, 13)], -0.6, 0.6, "red", plane="xy")
    m.box(-7, 8, -3.2, 3.2, -14.2, -10.2, "gondola", tx=1.0)                     # gondola
    m.box(-4, 7.2, -3.3, 3.3, -12.8, -11.2, "glass")
    for y in (6.5, -6.5):
        m.cyl((-3, y, -10.5), (4, y, -10.5), 1.6, 1.2, "gondola", n=8)          # engine pods
        m.cyl((-3.4, y, -10.5), (-3.0, y, -10.5), 3.0, m="prop", n=8)
    return m

def sp_hovercar():
    m = Model(dict(
        body=mat([(220, 96, 230), (160, 50, 186), (100, 28, 128), (58, 18, 80)]),
        black=mat([(84, 84, 108), (48, 48, 66), (28, 28, 40)]),
        canopy=mat([(170, 240, 255), (90, 170, 230), (50, 100, 170)]),
        neon=mat([(170, 255, 255), (0, 224, 255)], glow=True, w=2),
        tail=mat([(255, 120, 200), (255, 60, 150)], glow=True, w=2),
        glow=mat([(200, 255, 255), (90, 230, 255)], glow=True),
    ))
    m.hexa([(-9, -4.2, 2.4), (8, -3.2, 2.0), (8, 3.2, 2.0), (-9, 4.2, 2.4),
            (-9, -4.0, 5.6), (9.5, -2.6, 3.6), (9.5, 2.6, 3.6), (-9, 4.0, 5.6)], "body")  # wedge body
    m.hexa([(-5, -3.2, 5.0), (3.5, -2.6, 4.2), (3.5, 2.6, 4.2), (-5, 3.2, 5.0),
            (-4, -2.4, 7.4), (0.5, -1.8, 7.0), (0.5, 1.8, 7.0), (-4, 2.4, 7.4)], "canopy")
    m.box(-9.6, -6.5, -4.6, 4.6, 5.0, 5.8, "black")                              # spoiler
    m.box(-9.4, -8.6, 3.2, 3.8, 5.0, 6.0, "black"); m.box(-9.4, -8.6, -3.8, -3.2, 5.0, 6.0, "black")
    m.box(-9.0, 8.5, 4.1, 4.4, 3.3, 3.8, "neon"); m.box(-9.0, 8.5, -4.4, -4.1, 3.3, 3.8, "neon")
    m.box(-9.3, -9.0, -3.6, 3.6, 3.6, 4.6, "tail")
    m.ellip((0, 0, 1.6), (7.5, 3.4, 0.6), "glow", rows=3, cols=10)               # hover glow
    for x in (5.5, -6.0):
        for y in (3.6, -3.6):
            m.cyl((x, y, 1.6), (x, y, 2.6), 1.4, m="black", n=8, cap="glow")    # hover pads
    return m

def sp_hoverbus():
    m = Model(dict(
        body=mat([(110, 230, 220), (60, 176, 176), (36, 116, 124), (22, 72, 84)]),
        black=mat([(84, 84, 108), (48, 48, 66), (28, 28, 40)]),
        win=mat([(255, 210, 120), (255, 150, 200)], glow=True),
        neon=mat([(255, 130, 230), (240, 60, 200)], glow=True, w=2),
        glow=mat([(200, 255, 255), (90, 230, 255)], glow=True),
        lamp=mat([(255, 255, 220)], glow=True, w=2),
    ))
    m.box(-18, 17, -5.2, 5.2, 3.0, 15.0, "body", tx=0.3, ty=0.3)                 # body
    m.wedge(17, 19.5, -5.0, 5.0, 3.0, 14.0, 6.0, "body", ty=0.3)                 # nose
    m.box(17.3, 19.0, -4.4, 4.4, 9.2, 12.8, "black")                             # windscreen
    m.box(-18.4, 17.4, -5.6, 5.6, 2.4, 3.4, "black")
    m.box(-12, 8, -3.4, 3.4, 15.0, 16.6, "black", tx=0.6, ty=0.6)                # roof unit
    for y in (5.2, -5.3):
        for i in range(6):
            x = -14.5 + i*5.2
            m.box(x - 1.8, x + 1.8, y, y + 0.1, 9.0, 12.6, "win")
        m.box(-18, 17, y, y + 0.12, 6.4, 7.2, "neon")
        m.box(-18, 17, y, y + 0.12, 14.0, 14.6, "neon")
    m.box(-18.2, -18.0, -4.0, 4.0, 10, 12.8, "win")
    m.box(19.0, 19.6, 2.6, 4.4, 4.0, 5.4, "lamp"); m.box(19.0, 19.6, -4.4, -2.6, 4.0, 5.4, "lamp")
    m.ellip((0, 0, 2.2), (16, 4.4, 0.8), "glow", rows=3, cols=12)
    return m

# ---- graveyard (HAUNTED): ghost carriage + ruined chapel arch -----------------------------------
def _spoked_wheel(m, x, y, z, r, w, rim, spoke, hub, n=12, spokes=8):
    """Open wheel on a y axle: a rim of n box segments, thin spokes and a hub (see-through)."""
    ri = r*0.80
    with m.at((x, y, z)):
        for i in range(n):
            a0, a1 = 2*math.pi*i/n, 2*math.pi*(i + 1)/n
            q = [(ri, a0), (r, a0), (r, a1), (ri, a1)]
            m.hexa([(rr*math.cos(a), yy, rr*math.sin(a)) for yy in (-w/2, w/2) for rr, a in q], rim)
        for k in range(spokes):
            with m.at(ry=360.0*k/spokes + 22.5):
                m.box(-0.38, 0.38, -w*0.3, w*0.3, 0, ri + 0.1, spoke)
        m.cyl((0, -w/2 - 0.25, 0), (0, w/2 + 0.25, 0), r*0.22, m=hub, n=8)

def _ghost_horse(m, x0, y, lead):
    """Spectral horse, ~15 long, withers ~12 up, nose toward +x. lead = which foreleg is forward."""
    m.ellip((x0, y, 9.6), (4.7, 1.8, 2.3), "horse", rows=5, cols=10)                     # barrel
    m.ellip((x0 - 3.2, y, 9.9), (2.4, 1.9, 2.4), "horse", rows=4, cols=8)                # haunch
    for (lx, fwd) in ((x0 + 3.3, lead), (x0 + 2.5, -lead), (x0 - 3.2, -lead), (x0 - 4.0, lead)):
        yy = y + 0.9*(1 if fwd > 0 else -1)
        m.hexa([(lx - 0.55 + fwd*1.3, yy - 0.5, 0), (lx + 0.55 + fwd*1.3, yy - 0.5, 0), (lx + 0.55 + fwd*1.3, yy + 0.5, 0), (lx - 0.55 + fwd*1.3, yy + 0.5, 0),
                (lx - 0.7, yy - 0.6, 8.6), (lx + 0.7, yy - 0.6, 8.6), (lx + 0.7, yy + 0.6, 8.6), (lx - 0.7, yy + 0.6, 8.6)], "horse")
    m.hexa([(x0 + 2.4, y - 1.2, 9.6), (x0 + 5.0, y - 1.2, 9.2), (x0 + 5.0, y + 1.2, 9.2), (x0 + 2.4, y + 1.2, 9.6),
            (x0 + 4.6, y - 0.8, 14.6), (x0 + 6.6, y - 0.8, 14.2), (x0 + 6.6, y + 0.8, 14.2), (x0 + 4.6, y + 0.8, 14.6)], "horse")   # neck
    m.hexa([(x0 + 5.0, y - 0.9, 12.6), (x0 + 9.0, y - 0.6, 11.0), (x0 + 9.0, y + 0.6, 11.0), (x0 + 5.0, y + 0.9, 12.6),
            (x0 + 5.0, y - 0.9, 15.2), (x0 + 9.0, y - 0.6, 12.6), (x0 + 9.0, y + 0.6, 12.6), (x0 + 5.0, y + 0.9, 15.2)], "horse")   # head
    for s in (0.55, -0.55):
        m.box(x0 + 4.8, x0 + 5.5, y + s - 0.3, y + s + 0.3, 15.0, 16.2, "horse")         # ears
    m.hexa([(x0 + 2.6, y - 0.3, 11.0), (x0 + 3.4, y - 0.3, 11.0), (x0 + 3.4, y + 0.3, 11.0), (x0 + 2.6, y + 0.3, 11.0),
            (x0 + 4.2, y - 0.3, 15.2), (x0 + 5.0, y - 0.3, 15.2), (x0 + 5.0, y + 0.3, 15.2), (x0 + 4.2, y + 0.3, 15.2)], "wisp")    # mane
    m.hexa([(x0 - 7.0, y - 0.5, 5.4), (x0 - 6.0, y - 0.5, 5.4), (x0 - 6.0, y + 0.5, 5.4), (x0 - 7.0, y + 0.5, 5.4),
            (x0 - 5.8, y - 0.6, 11.0), (x0 - 4.6, y - 0.6, 11.0), (x0 - 4.6, y + 0.6, 11.0), (x0 - 5.8, y + 0.6, 11.0)], "wisp")    # tail
    m.box(x0 + 1.3, x0 + 2.3, y - 2.0, y + 2.0, 7.2, 12.1, "black")                      # collar

def sp_carriage():
    """Ghost carriage: a black Victorian hearse and two spectral horses. 44 long (x -21..23, horses
    at +x = front), 14 wide over the wheels, 18 tall to the roof plumes."""
    m = Model(dict(
        black=mat([(86, 136, 138), (48, 66, 88), (30, 34, 56), (18, 16, 32)]),           # lit faces catch a spectral sheen
        wheel=mat([(150, 206, 200), (84, 116, 132), (44, 54, 80)], w=2.5),
        glass=mat([(92, 196, 184)], glow=True),
        trim=mat([(190, 255, 232)], glow=True, w=2),
        lamp=mat([(240, 255, 176)], glow=True, w=3),
        horse=mat([(224, 255, 244), (150, 226, 214), (92, 168, 184), (58, 108, 150)]),
        wisp=mat([(244, 255, 255)], glow=True, w=2),
    ))
    m.box(-20, 10.5, -4.4, 4.4, 4.6, 5.6, "black")                                       # chassis
    m.box(-20, 3, -5.0, 5.0, 5.6, 14.5, "black")                                         # hearse body
    m.box(-20.6, 3.6, -5.6, 5.6, 14.5, 15.5, "black", tx=0.4, ty=0.4)                    # cornice
    m.box(-19.4, 2.4, -4.6, 4.6, 15.5, 16.7, "black", tx=1.2, ty=1.4)                    # crowned roof
    for x in (-19.6, 2.6):
        for y in (4.6, -4.6):
            m.box(x - 0.45, x + 0.45, y - 0.45, y + 0.45, 15.5, 18.0, "trim")            # plumes
    for y in (5.0, -5.12):
        for x in (-18.4, -12.2, -6.0):
            m.box(x, x + 5.2, y, y + 0.12, 8.2, 13.2, "glass")                           # glass sides
        m.box(-20, 3, y, y + 0.12, 6.4, 7.1, "trim")
    m.box(-20.12, -20.0, -3.4, 3.4, 8.2, 13.2, "glass")                                  # rear door
    m.box(3, 8.2, -4.4, 4.4, 5.6, 9.2, "black")                                          # box seat
    m.box(3, 4.2, -4.4, 4.4, 9.2, 12.0, "black")
    m.wedge(8.2, 10.5, -4.0, 4.0, 5.6, 8.0, 6.2, "black")                                # footboard
    for y in (5.2, -5.2):                                                                # lanterns
        m.box(5.2, 5.8, y - 0.3, y + 0.3, 8.6, 11.6, "black")
        m.box(4.6, 6.4, y - 0.9, y + 0.9, 11.6, 14.0, "lamp")
        m.box(4.4, 6.6, y - 1.1, y + 1.1, 14.0, 14.7, "black", tx=0.5, ty=0.5)
    for y in (6.3, -6.3):                                                                # tall spoked wheels
        _spoked_wheel(m, -11.5, y, 5.6, 5.6, 1.0, "wheel", "wheel", "trim")
        _spoked_wheel(m, 4.6, y, 4.2, 4.2, 1.0, "wheel", "wheel", "trim")
    m.box(-11.5, 4.6, -5.8, 5.8, 4.0, 4.6, "black", tx=0.0)                              # axle bed
    m.box(10.5, 22.0, -0.35, 0.35, 5.6, 6.3, "black")                                    # pole
    _ghost_horse(m, 14.0, 3.1, 1)
    _ghost_horse(m, 14.0, -3.1, -1)
    return m

def sp_arch():
    """Ruined chapel arch spanning the road. Same footprint as the start gantry (piers centred on
    y = +-38 with the same feet, 73 clear between them, 20 clear at the springing, 36 to the top)."""
    m = Model(dict(
        stone=mat([(170, 180, 204), (120, 130, 162), (82, 90, 126), (54, 58, 92)]),
        ashlar=mat([(198, 206, 226), (146, 154, 184), (100, 108, 142)]),
        dark=mat([(64, 66, 98), (40, 40, 66)]),
        moss=mat([(112, 154, 108), (72, 112, 86), (46, 76, 70)], w=1.5),
    ))
    def zi(u): return 20.0 + 11.5*math.sin(u*0.95)/math.sin(0.95)                         # pointed intrados
    def gable(ay): return 33.6 - 8.6*ay/41.5
    N = 9
    jag = {1: [0.0, 0.8, 0.0, -0.6, 0.4, 0.0, 0.9, 0.0, 0.0], -1: [-2.6, -1.8, -2.4, -1.0, -1.6, 0.2, -0.8, 0.0, 0.0]}
    for s in (1, -1):
        y = 38*s
        m.box(-3.2, 3.2, y - 3.2, y + 3.2, 0, 1.6, "ashlar", tx=0.5, ty=0.5)             # plinth (= gantry foot)
        m.box(-2.5, 2.5, y - 1.5, y + 2.6, 1.6, 20.0, "stone") if s > 0 else m.box(-2.5, 2.5, y - 2.6, y + 1.5, 1.6, 20.0, "stone")
        yo0, yo1 = (36.5, 40.6) if s > 0 else (-40.6, -36.5)
        for z in (6.2, 10.8, 15.4):                                                      # courses
            m.box(2.5, 2.66, yo0, yo1, z, z + 0.5, "dark"); m.box(-2.66, -2.5, yo0, yo1, z, z + 0.5, "dark")
        m.box(-2.9, 2.9, yo0 - 0.3, yo1 + 0.3, 19.2, 20.4, "ashlar")                     # impost
        top = 26.6 if s > 0 else 22.8
        m.box(-2.3, 2.3, yo0, yo1, 20.4, top, "stone")                                   # shoulder
        if s > 0:
            m.box(-1.5, 1.5, 37.6, 40.4, top, top + 4.1, "ashlar", tx=1.1, ty=1.0)       # surviving pinnacle
        else:
            m.box(-2.3, 0.4, -40.6, -39.0, top, top + 1.3, "stone")                      # broken stub
        for i in range(N):
            u0, u1 = i/N, (i + 1)/N
            y0, y1 = s*36.5*(1 - u0), s*36.5*(1 - u1)
            a, b = zi(u0), zi(u1)
            t = max(gable(abs(y0 + y1)/2) + jag[s][i], b + 1.9)
            m.prism([(y0, a), (y1, b), (y1, t), (y0, t)], -2.2, 2.2, "stone", plane="yz")
            m.prism([(y0, a), (y1, b), (y1, b + 1.5), (y0, a + 1.5)], -2.7, 2.7, "ashlar", plane="yz")   # voussoirs
            if (i + (0 if s > 0 else 1)) % 3 == 0:
                m.box(-2.3, 2.3, min(y0, y1) + 0.3, max(y0, y1) - 0.3, t, t + 0.5, "moss")
            if i % 2 == 1 and t - b > 3.2:                                               # joints
                for x in (2.2, -2.34):
                    m.box(x, x + 0.14, y1 - 0.25, y1 + 0.25, b + 1.6, t - 0.4, "dark")
    m.box(-0.6, 0.6, -0.6, 0.6, 33.0, 36.0, "ashlar")                                    # gable cross
    m.box(-0.6, 0.6, -1.9, 1.9, 34.1, 35.0, "ashlar")
    for x in (2.5, -2.75):                                                               # ivy up the left pier
        for (ya, yb, za, zb) in ((38.2, 40.6, 1.6, 9.0), (39.2, 40.6, 9.0, 15.0), (37.0, 38.6, 1.6, 4.6), (39.8, 40.6, 15.0, 23.0),
                                 (38.6, 40.2, 21.0, 25.0)):
            m.box(x, x + 0.25, ya, yb, za, zb, "moss")
    for x in (2.2, -2.42):                                                               # moss in the right haunch
        m.box(x, x + 0.22, -34.0, -29.5, 23.2, 24.6, "moss"); m.box(x + (0.3 if x > 0 else -0.3), x + (0.52 if x > 0 else -0.08), -40.4, -38.4, 1.6, 4.4, "moss")
        m.box(x, x + 0.22, 9.0, 13.0, 31.6, 32.6, "moss")
    return m

def sp_chapel():
    """Ruined gothic chapel, the graveyard landmark: 90 long (x -45..45), 50 wide over the
    buttresses, roofless nave with pointed window openings, square bell tower ~110 tall at +x."""
    m = Model(dict(
        stone=mat([(170, 180, 204), (120, 130, 162), (82, 90, 126), (54, 58, 92)]),
        ashlar=mat([(198, 206, 226), (146, 154, 184), (100, 108, 142)]),
        dark=mat([(64, 66, 98), (40, 40, 66)]),
        moss=mat([(112, 154, 108), (72, 112, 86), (46, 76, 70)], w=1.5),
        void=mat([(30, 28, 52)], glow=True, w=1.5),
        glow=mat([(255, 200, 112)], glow=True, w=1.5),
    ))
    SILL, SPRING, APEX = 12.0, 25.0, 34.0
    def bay(x0, x1, y0, y1, top, hw=4.0, lit=False):
        """One wall bay along x with a pointed window opening; top < APEX = broken off below the head."""
        xc = (x0 + x1)/2; xa, xb = xc - hw, xc + hw
        m.box(x0, x1, y0, y1, 0, SILL, "stone")
        jt = min(top, SPRING)
        m.box(x0, xa, y0, y1, SILL, jt, "stone"); m.box(xb, x1, y0, y1, SILL, jt if top >= APEX else jt - 5.0, "stone")
        if top >= APEX:
            m.prism([(x0, SPRING), (xa, SPRING), (xc, APEX), (xc, top), (x0, top)], y0, y1, "stone", plane="xz")
            m.prism([(x1, SPRING), (x1, top - 2.5), (xc, top), (xc, APEX), (xb, SPRING)], y0, y1, "stone", plane="xz")
        if lit:
            ym = (y0 + y1)/2
            m.box(xa, xb, ym - 0.3, ym + 0.3, SILL, SPRING, "glow")
            m.prism([(xa, SPRING), (xb, SPRING), (xc, APEX)], ym - 0.3, ym + 0.3, "glow", plane="xz")
    tops = {1: [41.0, 38.0, 22.0, 40.0], -1: [36.0, 41.0, 40.0, 18.0]}                  # broken wall heads per bay
    for s in (1, -1):
        y0, y1 = (17.0, 20.0) if s > 0 else (-20.0, -17.0)
        for i in range(4):
            x0 = -43.0 + i*16.0
            bay(x0, x0 + 16.0, y0, y1, tops[s][i], lit=(s > 0 and i == 3))
            bx = x0                                                                      # buttress at each pier
            ya, yb = (20.0, 25.0) if s > 0 else (-25.0, -20.0)
            m.box(bx - 1.6, bx + 1.6, ya, yb, 0, 15.0, "stone")
            m.hexa([(bx - 1.6, ya, 15.0), (bx + 1.6, ya, 15.0), (bx + 1.6, yb, 15.0), (bx - 1.6, yb, 15.0),
                    (bx - 1.6, ya, 27.0), (bx + 1.6, ya, 27.0), (bx + 1.6, 20.6*s, 27.0), (bx - 1.6, 20.6*s, 27.0)] if s > 0 else
                   [(bx - 1.6, ya, 15.0), (bx + 1.6, ya, 15.0), (bx + 1.6, yb, 15.0), (bx - 1.6, yb, 15.0),
                    (bx - 1.6, 20.6*s, 27.0), (bx + 1.6, 20.6*s, 27.0), (bx + 1.6, yb, 27.0), (bx - 1.6, yb, 27.0)], "ashlar")
        m.box(21.0, 24.0, 12.0*s if s > 0 else -20.0, 20.0 if s > 0 else -12.0, 0, 40.0, "stone")   # east returns
    # west gable: big pointed window, broken peak
    for s in (1, -1):
        m.box(-45.0, -42.0, 7.0*s if s > 0 else -20.0, 20.0 if s > 0 else -7.0, 0, 30.0, "stone")
        ya, yb = 7.0*s, 20.0*s
        if s > 0:
            m.prism([(ya, 30.0), (yb, 30.0), (yb, 40.0), (5.0, 55.0), (0.0, 58.0), (0.0, 44.0)], -45.0, -42.0, "stone", plane="yz")
        else:
            m.prism([(ya, 30.0), (0.0, 44.0), (-4.0, 47.0), (yb + 7.0, 40.0), (yb, 36.0), (yb, 30.0)], -45.0, -42.0, "stone", plane="yz")
    m.box(-45.0, -42.0, -7.0, 7.0, 0, 9.0, "stone")
    m.box(-45.6, -41.4, -20.6, 20.6, 0, 2.4, "ashlar")
    # bell tower
    m.box(21.0, 45.0, -12.0, 12.0, 0, 98.0, "stone", tx=0.8, ty=0.8)
    for z in (38.0, 70.0):
        m.box(20.6, 45.4, -12.4, 12.4, z, z + 2.4, "ashlar")
    m.box(20.4, 45.6, -12.6, 12.6, 96.0, 99.6, "ashlar")                                 # parapet
    for (cx, cy, h) in ((22.4, 10.6, 110.0), (43.6, 10.6, 105.0), (43.6, -10.6, 108.0), (22.4, -10.6, 101.5)):
        m.box(cx - 2.0, cx + 2.0, cy - 2.0, cy + 2.0, 99.6, h, "ashlar", tx=1.2 if h > 104 else 0.2, ty=1.2 if h > 104 else 0.2)
    for s in (1, -1):                                                                    # belfry + slit openings
        y = 11.3*s
        m.box(29.0, 37.0, y - 0.25, y + 0.25, 76.0, 88.0, "void")
        m.prism([(29.0, 88.0), (37.0, 88.0), (33.0, 93.0)], y - 0.25, y + 0.25, "void", plane="xz")
        m.box(31.8, 34.2, y - 0.25, y + 0.25, 48.0, 58.0, "void")
        x = 33.0 + 11.3*s
        m.box(x - 0.25, x + 0.25, -4.0, 4.0, 76.0, 88.0, "void")
        m.prism([(-4.0, 88.0), (4.0, 88.0), (0.0, 93.0)], x - 0.25, x + 0.25, "void", plane="yz")
    m.box(45.0, 45.4, -4.0, 4.0, 0, 14.0, "void")                                        # tower door (+x face)
    m.prism([(-4.0, 14.0), (4.0, 14.0), (0.0, 19.0)], 45.0, 45.4, "void", plane="yz")
    m.box(44.8, 45.2, -1.2, 1.2, 48.0, 58.0, "void")
    for (x0, x1, y, z0, z1) in ((-40.0, -30.0, 20.0, 0.0, 7.0), (-12.0, -4.0, 20.0, 0.0, 10.0), (8.0, 17.0, -20.4, 0.0, 6.0),
                                (-30.0, -24.0, -20.4, 0.0, 9.0), (24.0, 32.0, 11.6, 0.0, 16.0), (36.0, 44.0, -12.0, 0.0, 11.0)):
        m.box(x0, x1, y, y + 0.4, z0, z1, "moss")                                        # ivy at the footings
    return m

# ---- trackside objects (world texels; road direction is +x, so boards face +x / -x) ----------
def sp_gantry():
    """Start/finish arch: posts at y = +-38, checkered banner beam 20..27 texels up."""
    m = Model(dict(
        steel=mat([(176, 190, 214), (118, 132, 164), (74, 84, 116), (46, 52, 78)]),
        red=mat([(255, 96, 80), (216, 44, 48), (140, 24, 40)]),
        white=mat([(255, 255, 255), (214, 220, 236), (150, 158, 186)]),
        black=mat([(64, 64, 80), (36, 36, 50), (22, 22, 32)]),
        gold=mat([(255, 226, 110), (226, 164, 48), (150, 96, 28)]),
        lamp=mat([(255, 250, 190), (255, 210, 90)], glow=True, w=3),
    ))
    for s in (1, -1):
        y = 38*s
        m.box(-3.2, 3.2, y - 3.2, y + 3.2, 0, 1.6, "black", tx=0.5, ty=0.5)        # foot
        for i in range(4):                                                          # hazard bands
            m.box(-1.9, 1.9, y - 1.9, y + 1.9, 1.6 + i*2.6, 4.2 + i*2.6, "red" if i % 2 == 0 else "white")
        m.box(-1.5, 1.5, y - 1.5, y + 1.5, 12.0, 20.0, "steel")                     # post
        m.box(-2.0, 2.0, y - 2.0, y + 2.0, 27.0, 28.4, "steel")                     # cap
        m.ellip((0, y, 29.6), 1.5, "lamp", rows=4, cols=8)                          # beacon
        m.prism([(y - 1.5*s, 20.0), (y - 9.0*s, 20.0), (y - 1.5*s, 12.5)] if s > 0 else
                [(y - 1.5*s, 20.0), (y - 1.5*s, 12.5), (y - 9.0*s, 20.0)], -0.7, 0.7, "steel", plane="yz")  # brace
    m.box(-1.5, 1.5, -40.5, 40.5, 20.0, 27.0, "red")                               # beam
    m.box(-1.9, 1.9, -40.9, 40.9, 26.4, 27.2, "steel")                             # top rail
    m.box(-1.9, 1.9, -40.9, 40.9, 19.8, 20.5, "steel")                             # bottom rail
    n = 24; cw = 72.0/n
    for r in range(2):
        for c in range(n):
            mm = "black" if (r + c) % 2 else "white"
            y0 = -36 + c*cw; z0 = 20.6 + r*2.9
            m.box(1.5, 1.72, y0, y0 + cw, z0, z0 + 2.9, mm)
            m.box(-1.72, -1.5, y0, y0 + cw, z0, z0 + 2.9, mm)
    m.cyl((-1.0, 0, 30.6), (1.0, 0, 30.6), 4.4, m="gold", n=8, cap="gold", ring="gold", inner=0.5, phase=math.pi/8)  # sprocket crest
    for i in range(8):
        a = 2*math.pi*i/8
        with m.at((0, 4.6*math.cos(a), 30.6 + 4.6*math.sin(a)), rx=math.degrees(a)):
            m.box(-0.9, 0.9, -0.9, 0.9, -0.8, 0.8, "gold")
    m.cyl((-1.25, 0, 30.6), (1.25, 0, 30.6), 1.7, m="black", n=8)
    return m

def sp_chevron(sign):
    """Corner board: 14 wide (along y), 8 tall. sign=+1: arrows point toward +y, which is to the
    RIGHT for a viewer standing on the +x side looking at the board; sign=-1: left."""
    m = Model(dict(
        yellow=mat([(255, 232, 80), (240, 196, 40), (188, 140, 28)]),
        black=mat([(52, 50, 64), (30, 28, 40), (20, 18, 28)], w=1.3),
        leg=mat([(170, 176, 196), (110, 116, 142), (68, 72, 96)]),
    ))
    for y in (-5.0, 5.0):
        m.box(-0.45, 0.45, y - 0.6, y + 0.6, 0, 2.4, "leg")
    m.box(-0.5, 0.5, -7.0, 7.0, 2.0, 8.5, "black")
    zc, hh, t, d = 5.25, 2.55, 1.7, 2.1
    for fx in (1, -1):
        x0, x1 = (0.5, 0.60) if fx > 0 else (-0.60, -0.5)
        m.box(x0, x1, -6.5, 6.5, 2.5, 8.0, "yellow")
        x2, x3 = (0.60, 0.70) if fx > 0 else (-0.70, -0.60)
        for a in (-2.2, 2.0, 6.2):
            up = [(a - d - t, zc + hh), (a - d, zc + hh), (a, zc), (a - t, zc)]
            dn = [(a - t, zc), (a, zc), (a - d, zc - hh), (a - d - t, zc - hh)]
            for poly in (up, dn):
                m.prism([(sign*y, z) for y, z in poly], x2, x3, "black", plane="yz")
    return m

def sp_ramp_sign():
    """'Jump ahead' board: blue square with a white up-arrow, on a post. Faces +x and -x."""
    m = Model(dict(
        blue=mat([(90, 150, 255), (48, 96, 220), (28, 56, 150)]),
        white=mat([(255, 255, 255), (220, 228, 244), (160, 170, 200)], w=1.4),
        post=mat([(170, 176, 196), (110, 116, 142), (68, 72, 96)]),
        dark=mat([(52, 50, 64), (30, 28, 40)]),
    ))
    m.box(-0.5, 0.5, -0.6, 0.6, 0, 5.0, "post")
    m.box(-1.2, 1.2, -1.6, 1.6, 0, 0.8, "dark")
    m.box(-0.5, 0.5, -4.6, 4.6, 4.6, 13.8, "white")
    for fx in (1, -1):
        x0, x1 = (0.5, 0.6) if fx > 0 else (-0.6, -0.5)
        x2, x3 = (0.6, 0.7) if fx > 0 else (-0.7, -0.6)
        m.box(x0, x1, -3.9, 3.9, 5.3, 13.1, "blue")
        m.prism([(-3.1, 9.4), (3.1, 9.4), (0, 12.6)], x2, x3, "white", plane="yz")
        m.box(x2, x3, -1.2, 1.2, 6.0, 9.5, "white")
    return m

# ---- BREAKFAST RUN: giant kitchen-table objects (1 texel ~ 0.145 cm; karts are toy-sized) ------
def quad(m, pts, mname, normal):
    """A flat quad with an explicit outward normal (emit() can't orient a planar primitive)."""
    R, t = m.stack[-1]
    P = np.array(pts, float) @ R.T + t
    n = R @ np.array(normal, float)
    mid = m.names.index(mname)
    for tri in ((0, 1, 2), (0, 2, 3)):
        T3 = P[list(tri)]
        if np.dot(np.cross(T3[1] - T3[0], T3[2] - T3[0]), n) < 0: T3 = T3[[0, 2, 1]]
        m.tris.append(T3); m.mids.append(mid); m.tags.append(m.tag)

def decal(m, grid, origin, du, dv, normal, names):
    """Paint a small indexed image onto a plane: grid[row][col] indexes `names` (None = skip);
    origin = top-left corner, du/dv = one cell right/down. Equal runs are merged into rects."""
    grid = np.asarray(grid); Hh, Ww = grid.shape
    o = np.array(origin, float); du = np.array(du, float); dv = np.array(dv, float)
    def flush(x0, x1, v, y0, y1):
        if names[v] is None: return
        quad(m, [o + du*x0 + dv*y0, o + du*x1 + dv*y0, o + du*x1 + dv*y1, o + du*x0 + dv*y1], names[v], normal)
    live = {}
    for y in range(Hh + 1):
        runs = set()
        if y < Hh:
            row = grid[y]; x = 0
            while x < Ww:
                x1 = x
                while x1 < Ww and row[x1] == row[x]: x1 += 1
                runs.add((x, x1, int(row[x]))); x = x1
        for k in [k for k in live if k not in runs]:
            flush(k[0], k[1], k[2], live.pop(k), y)
        for k in runs:
            if k not in live: live[k] = y

def _txt(text, scale=1):
    mk = _font_mask(text)
    return np.kron(mk, np.ones((scale, scale), bool)) if scale > 1 else mk

def _stamp(g, mk, x, y, val, ink=None, shadow=0):
    S = g.shape
    M = _place(S, mk, x, y)
    if ink is not None:
        if shadow: g[_dil(_place(S, mk, x + shadow, y + shadow), 1)] = ink
        g[_dil(M, 1)] = ink
    g[M] = val
    return M

# box-art colour indices: 1 red 2 yellow 3 orange 4 white 5 blue 6 teal 7 green 8 purple 9 black 10 pink
BOXART = [None, "red", "yellow", "orange", "white", "blue", "teal", "green", "purple", "black", "pink"]

def _cereal_front():
    W, Hh = 86, 129
    im = Image.new("P", (W, Hh), 1); d = ImageDraw.Draw(im)
    cx, cy = 43, 66
    for i in range(12):                                                    # sunburst
        a0 = 2*math.pi*i/12 + 0.1; a1 = a0 + math.pi/12
        d.polygon([(cx, cy), (cx + 200*math.cos(a0), cy + 200*math.sin(a0)), (cx + 200*math.cos(a1), cy + 200*math.sin(a1))], fill=3)
    for k, (col, off) in enumerate(((8, 3), (6, 0))):                      # 90s zigzags under the title
        pts = [(x, 43 + off + (4 if (x//6) % 2 else 0)) for x in range(-6, W + 12, 6)]
        d.line(pts, fill=9, width=4); d.line(pts, fill=col, width=2)
    # bowl of loops
    d.polygon([(9, 97), (77, 97), (65, 119), (21, 119)], fill=5, outline=9)
    d.rectangle([28, 119, 58, 121], fill=5, outline=9)
    d.line([(13, 105), (73, 105)], fill=4, width=2); d.line([(17, 112), (69, 112)], fill=6, width=2)
    d.ellipse([9, 89, 77, 104], fill=4, outline=9)
    loops = [(43, 82, 10), (33, 85, 2), (53, 85, 7), (24, 91, 6), (43, 90, 3), (62, 91, 8), (33, 93, 7), (53, 93, 2),
             (18, 96, 10), (43, 97, 6), (68, 96, 3), (29, 99, 8), (57, 99, 10)]
    for (lx, ly, col) in loops:
        d.ellipse([lx - 6, ly - 4, lx + 6, ly + 4], fill=9)
        d.ellipse([lx - 5, ly - 3, lx + 5, ly + 3], fill=col)
        d.ellipse([lx - 2, ly - 1, lx + 2, ly + 1], fill=9)
        d.point([(lx - 3, ly - 2), (lx - 2, ly - 3)], fill=4)
    d.rectangle([0, 122, W, Hh], fill=5); d.line([(0, 122), (W, 122)], fill=9)
    for x in range(4, W - 6, 10): d.line([(x, 125), (x + 5, 125)], fill=4)
    # starburst
    sx, sy = 68, 64; pts = []
    for i in range(20):
        r = 15 if i % 2 == 0 else 10; a = math.pi*i/10
        pts.append((sx + r*math.cos(a), sy + r*math.sin(a)))
    d.polygon(pts, fill=4, outline=9)
    g = np.array(im, np.uint8); S = g.shape
    _stamp(g, _txt("NEW"), 60, 61, 1)
    # mascot: Sprock, a grinning gear in shades
    mx, my = 28, 68
    G = _gear(S, mx, my, 17.5, 13.5, 0, teeth=10, phase=0.3, duty=0.0)
    g[_dil(G, 1)] = 9; g[G] = 2
    g[G & ~_disc(S, mx, my - 2.5, 13.5) & _disc(S, mx, my, 13.5)] = 3
    g[my - 6:my - 1, mx - 10:mx - 1] = 9; g[my - 6:my - 1, mx + 1:mx + 10] = 9; g[my - 5, mx - 11:mx + 11] = 9
    g[my - 5, mx - 9:mx - 6] = 4; g[my - 5, mx + 2:mx + 5] = 4
    mouth = _disc(S, mx, my + 2, 8.5); mouth[:my + 3, :] = False
    g[_dil(mouth, 1)] = 9; g[mouth] = 4; g[my + 6:my + 12, :][mouth[my + 6:my + 12, :]] = 1
    # title
    T1 = _txt("TURBO", 2)
    M = _stamp(g, T1, 14, 5, 2, ink=9, shadow=2)
    g[5:9, :][M[5:9, :]] = 4
    for i, (ch, col) in enumerate(zip("LOOPS", (6, 2, 10, 7, 4))):
        _stamp(g, _txt(ch, 2), 14 + i*12, 24, col, ink=9, shadow=2)
    return g

def _cereal_back():
    W, Hh = 86, 129
    im = Image.new("P", (W, Hh), 2); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 20], fill=1); d.line([(0, 21), (W, 21)], fill=9)
    d.rectangle([5, 26, 46, 114], fill=4, outline=9)                        # "nutrition" panel
    d.rectangle([8, 29, 43, 33], fill=9)
    for i, y in enumerate(range(38, 111, 5)):
        ln = (30, 22, 34, 26, 18, 32, 24)[i % 7]
        d.line([(8, y), (8 + ln, y)], fill=9)
        if i % 4 == 3: d.line([(8, y + 1), (43, y + 1)], fill=9)
    d.rectangle([52, 26, 81, 58], fill=6, outline=9)                        # maze game
    for (a, b) in (((55, 30), (55, 50)), ((55, 50), (66, 50)), ((60, 30), (60, 44)), ((60, 44), (72, 44)), ((66, 34), (78, 34)),
                   ((72, 38), (72, 54)), ((66, 54), (78, 54)), ((78, 40), (78, 50)), ((64, 38), (68, 38))):
        d.line([a, b], fill=8)
    d.ellipse([54, 64, 80, 90], fill=10, outline=9)                         # cut-out token
    d.ellipse([62, 72, 72, 82], fill=2, outline=9)
    d.rectangle([54, 97, 80, 114], fill=4, outline=9)                       # barcode
    for i, x in enumerate(range(56, 79)):
        if (i*7 + i//3) % 5 in (0, 1, 3): d.line([(x, 99), (x, 110)], fill=9)
    d.rectangle([0, 122, W, Hh], fill=5); d.line([(0, 122), (W, 122)], fill=9)
    g = np.array(im, np.uint8)
    _stamp(g, _txt("TURBO LOOPS"), 10, 7, 2, ink=9)
    return g

def sp_cereal():
    """'TURBO LOOPS' cereal box: 138 wide (y), 48 deep (x), 207 tall; loud front art on +x."""
    m = Model(dict(
        red=mat([(255, 72, 64), (188, 30, 48)]), yellow=mat([(255, 232, 64), (214, 160, 32)]),
        orange=mat([(255, 150, 40), (204, 92, 28)]), white=mat([(255, 255, 255), (190, 200, 228)]),
        blue=mat([(64, 110, 250), (36, 60, 170)]), teal=mat([(40, 220, 200), (20, 140, 150)]),
        green=mat([(110, 224, 70), (50, 150, 56)]), purple=mat([(170, 84, 240), (104, 44, 160)]),
        black=mat([(44, 36, 60), (24, 20, 36)]), pink=mat([(255, 120, 200), (196, 60, 140)]),
        card=mat([(255, 190, 60), (232, 140, 40), (176, 92, 32), (120, 60, 30)]),
        flap=mat([(250, 240, 220), (206, 190, 166), (150, 134, 120)]),
    ))
    m.box(-24, 24, -69, 69, 0, 207, "card", mats=["black", "flap", "card", "red", "card", "yellow"])
    m.box(-23, 23, -69.3, 69.3, 150, 168, "red"); m.box(-23, 23, -69.3, 69.3, 144, 148, "blue")   # side panel bands
    m.box(-2, 2, -69, 69, 207, 208.5, "flap")                                                      # top flap seam
    cw = 138.0/86; ch = 207.0/129
    decal(m, _cereal_front(), (24.25, -69, 207), (0, cw, 0), (0, 0, -ch), (1, 0, 0), BOXART)
    decal(m, _cereal_back(), (-24.25, 69, 207), (0, -cw, 0), (0, 0, -ch), (-1, 0, 0), BOXART)
    return m

def _milk_face(text):
    W, Hh = 30, 78
    im = Image.new("P", (W, Hh), 1); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 9], fill=2); d.rectangle([0, 10, W, 12], fill=3)
    d.rectangle([0, 71, W, Hh], fill=2)
    spots = ((3, 36, 14, 47), (17, 50, 29, 63), (-3, 58, 8, 68), (20, 30, 31, 39), (9, 62, 17, 69)) if text else \
            ((2, 18, 13, 30), (16, 26, 30, 38), (-2, 42, 9, 54), (12, 48, 24, 60), (20, 62, 31, 69), (4, 60, 11, 67))
    for b in spots: d.ellipse(b, fill=4)
    g = np.array(im, np.uint8)
    if text:
        d0 = _txt("MILK"); _stamp(g, d0, 4, 18, 2)
        g[27:29, 4:27] = 3
    return g

def sp_milk():
    m = Model(dict(
        white=mat([(255, 255, 255), (220, 228, 244), (168, 180, 214), (120, 132, 176)]),
        blue=mat([(80, 130, 255), (44, 80, 210), (28, 48, 140)]),
        red=mat([(255, 84, 76), (200, 36, 50)]),
        black=mat([(52, 46, 66), (28, 24, 40)]),
    ))
    names = [None, "white", "blue", "red", "black"]
    m.box(-24, 24, -24, 24, 0, 125, "white")
    m.prism([(-24, 125), (24, 125), (0, 154)], -24, 24, "blue", plane="xz", side="blue")
    m.prism([(-24, 125), (24, 125), (0, 154)], -24.2, -23.8, "white", plane="xz")       # folded-in gable ends
    m.prism([(-24, 125), (24, 125), (0, 154)], 23.8, 24.2, "white", plane="xz")
    m.box(-1.2, 1.2, -24, 24, 153, 165, "white")                                        # top seal fin
    m.box(-1.5, 1.5, -24.2, 24.2, 156.5, 159, "red")
    for k in range(4):
        with m.at(rz=90*k):
            decal(m, _milk_face(k % 2 == 0), (24.2, -24, 125), (0, 1.6, 0), (0, 0, -125.0/78), (1, 0, 0), names)
    return m

def sp_juice():
    m = Model(dict(
        juice=mat([(255, 190, 60), (255, 150, 30), (226, 110, 20), (170, 74, 20)]),
        top=mat([(255, 214, 110), (255, 180, 60)], glow=True),
        glass=mat([(240, 252, 255), (190, 226, 244), (140, 186, 222), (100, 140, 190)]),
        white=mat([(255, 255, 255), (214, 222, 240)], w=2),
        red=mat([(255, 84, 100), (204, 36, 70)], w=2),
        rind=mat([(255, 150, 40), (220, 100, 24)], w=1.5),
        pulp=mat([(255, 226, 130), (255, 200, 90)], glow=True, w=2),
    ))
    m.cyl((0, 0, 0), (0, 0, 7), 19.5, 20, "glass", n=16)                              # thick glass base
    m.cyl((0, 0, 7), (0, 0, 76), 20, 23.2, "juice", n=16)
    m.cyl((0, 0, 76), (0, 0, 96), 23.2, 24, "glass", n=16, cap1="top", ring="white", inner=0.88)
    m.cyl((0, 0, 75), (0, 0, 77.5), 23.6, m="pulp", n=16)                             # juice line
    # bendy straw, red/white stripes
    p0 = np.array([6.0, 4.0, 60.0]); p1 = np.array([-9.0, -7.0, 112.0])
    for i in range(8):
        a = p0 + (p1 - p0)*i/8; b = p0 + (p1 - p0)*(i + 1)/8
        m.cyl(tuple(a), tuple(b), 2.3, m="red" if i % 2 else "white", n=6)
    m.cyl(tuple(p1), (-13, -10, 116), 2.5, m="red", n=6)
    m.cyl((-13, -10, 116), (-27, -19, 118), 2.3, m="white", n=6)
    with m.at((17, -17, 96), rz=-45):                                                # orange slice on the rim
        m.cyl((0, -1.2, 0), (0, 1.2, 0), 12, m="rind", n=12, cap="pulp", ring="rind", inner=0.82)
    return m

def sp_syrup():
    m = Model(dict(
        amber=mat([(240, 150, 50), (196, 100, 30), (140, 62, 22), (90, 38, 20)]),
        shine=mat([(255, 214, 120), (255, 180, 70)], glow=True, w=1.5),
        cap=mat([(255, 232, 80), (232, 180, 40), (170, 120, 28)]),
        red=mat([(255, 84, 70), (206, 36, 46), (136, 22, 38)]),
        cream=mat([(255, 246, 214), (226, 208, 170)], w=1.5),
    ))
    prof = [(0, 17), (4, 18), (44, 18), (58, 12.5), (68, 15.5), (82, 16.5), (96, 10), (108, 7), (112, 7)]
    for (z0, r0), (z1, r1) in zip(prof, prof[1:]):
        m.cyl((0, 0, z0), (0, 0, z1), r0, r1, "amber", n=14)
    m.cyl((0, 0, 12), (0, 0, 42), 18.5, m="red", n=14)                                # label band
    m.cyl((0, 0, 12), (0, 0, 14.5), 18.8, m="cream", n=14); m.cyl((0, 0, 39.5), (0, 0, 42), 18.8, m="cream", n=14)
    for s in (1, -1):                                                                 # label lozenge, both sides
        with m.at(rz=0 if s > 0 else 180):
            m.prism([(-9, 27), (0, 35), (9, 27), (0, 19)], 17.6, 19.3, "cream", plane="yz")
            m.prism([(-4, 27), (0, 31), (4, 27), (0, 23)], 19.3, 19.7, "red", plane="yz")
    m.cyl((0, 0, 112), (0, 0, 116), 8.5, m="cap", n=10)
    m.cyl((0, 0, 116), (0, 0, 128), 7.5, 6.5, "cap", n=10)
    m.cyl((0, 0, 128), (0, 0, 131), 3.0, 2.0, "cap", n=6)
    hp = [(-8, 0, 102), (-17, 0, 104), (-20, 0, 94), (-18, 0, 84), (-13, 0, 80)]     # little jug handle
    for a, b in zip(hp, hp[1:]): m.cyl(a, b, 2.6, m="amber", n=6)
    m.box(12.5, 14.5, 5, 8, 62, 80, "shine")                                         # glassy glints
    m.box(15, 17, 6, 9, 46, 54, "shine")
    return m

def sp_toaster(pop):
    m = Model(dict(
        chl=mat([(250, 252, 255), (214, 224, 240), (170, 184, 212), (124, 138, 176)]),
        chm=mat([(196, 208, 232), (150, 166, 200), (108, 124, 164), (76, 88, 128)]),
        chd=mat([(110, 124, 164), (76, 88, 130), (52, 60, 98), (36, 40, 72)]),
        black=mat([(84, 80, 100), (52, 48, 66), (32, 30, 44), (22, 20, 32)]),
        slot=mat([(40, 34, 44), (24, 20, 30)]),
        glow=mat([(255, 170, 50), (255, 110, 30)], glow=True, w=1.5),
        knob=mat([(250, 240, 220), (190, 180, 170)], w=1.5),
        red=mat([(255, 84, 70), (206, 36, 46)], w=1.5),
        waffle=mat([(255, 214, 110), (236, 170, 66), (186, 120, 44), (130, 78, 32)]),
        wd=mat([(206, 136, 50), (150, 92, 36)], w=1.2),
    ))
    m.box(-26, 26, -90, 90, 0, 6, "black", tx=1, ty=1)                               # plinth / feet
    m.box(-30, 30, -85, 85, 6, 30, "chm")
    m.box(-30, 30, -85, 85, 30, 46, "chd")                                           # horizon reflection
    m.box(-30, 30, -85, 85, 46, 94, "chl")
    m.box(-30, 30, -85, 85, 94, 112, "chl", tx=7, ty=1)                              # rolled shoulders
    m.box(-30.3, 30.3, -85, 85, 60, 63, "chm")
    for s in (1, -1):
        m.box(-28, 28, 85*s if s > 0 else -97, 97 if s > 0 else -85, 3, 106, "black", tx=4, ty=0)   # bakelite ends
    for x in (9.5, -9.5):                                                            # slots
        m.box(x - 5.5, x + 5.5, -68, 68, 111.6, 112.4, "slot")
        if not pop: m.box(x - 3.2, x + 3.2, -64, 64, 112.4, 112.8, "glow")
        else:
            m.box(x - 2.8, x + 2.8, -58, 58, 108, 134, "waffle", ty=3)
            for y in range(-54, 50, 12):
                for fx in (x + 2.8, x - 3.2):
                    m.box(fx, fx + 0.4, y + 1.5, y + 9, 114, 121.5, "wd"); m.box(fx, fx + 0.4, y + 1.5, y + 9, 124, 131, "wd")
    lz = 82 if pop else 40
    m.box(-1.5, 1.5, 97, 97.6, 30, 94, "slot")                                       # lever track
    m.box(-8, 8, 97, 106, lz, lz + 10, "knob", ty=1)
    m.cyl((30, -58, 28), (33.5, -58, 28), 8.5, m="black", n=10, cap1="knob", ring="black", inner=0.7)   # browning dial
    m.box(30, 30.8, 22, 62, 70, 82, "red")                                           # badge
    m.box(30.8, 31.2, 27, 57, 74.5, 77.5, "knob")
    return m

def sp_mug():
    m = Model(dict(
        cream=mat([(255, 255, 250), (230, 232, 240), (184, 190, 214), (136, 144, 180)]),
        teal=mat([(60, 230, 210), (28, 170, 170), (20, 110, 124)]),
        purple=mat([(180, 100, 250), (124, 60, 196), (78, 36, 130)]),
        pink=mat([(255, 120, 200), (214, 64, 150), (144, 36, 104)]),
        yellow=mat([(255, 236, 90), (226, 180, 40)]),
        coffee=mat([(150, 92, 60), (104, 60, 40)], glow=True),
    ))
    n = 24; R = 26.0; rows = 27; dz = 68.0/rows
    def pat(i, r):
        z = (r + 0.5)*dz
        tri = abs((i % 6) - 3)/1.5 - 1.0
        if abs(z - (38 + 9*tri)) < 4.2: return "teal"
        if abs(z - (21 + 9*tri)) < 2.6: return "purple"
        hsh = (i*37 + r*101 + (i*r) % 7) % 23
        if z > 50 and hsh in (0, 9): return "pink"
        if z > 50 and hsh == 4: return "yellow"
        if z < 10 and hsh in (2, 13): return "pink"
        return "cream"
    for i in range(n):
        a0 = 2*math.pi*i/n; a1 = 2*math.pi*(i + 1)/n; am = (a0 + a1)/2
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        r = 0
        while r < rows:                                           # merge equal cells up the facet
            r1 = r
            while r1 < rows and pat(i, r1) == pat(i, r): r1 += 1
            quad(m, [(R*c0, R*s0, r*dz), (R*c1, R*s1, r*dz), (R*c1, R*s1, r1*dz), (R*c0, R*s0, r1*dz)], pat(i, r),
                 (math.cos(am), math.sin(am), 0))
            r = r1
    m.cyl((0, 0, 68), (0, 0, 70), R, R + 0.4, "cream", n=n, cap1="coffee", ring="cream", inner=0.86)
    hp = [(0, 25, 55), (0, 38, 54), (0, 45, 44), (0, 45, 30), (0, 38, 19), (0, 25, 17)]
    for a, b in zip(hp, hp[1:]): m.cyl(a, b, 4.2, m="purple", n=7)
    return m

def sp_orange(frame):
    m = Model(dict(
        skin=mat([(255, 176, 56), (246, 136, 30), (206, 96, 24), (150, 62, 24)]),
        pore=mat([(226, 110, 26), (176, 76, 22)], w=1.4),
        lite=mat([(255, 206, 110), (255, 176, 70)], w=1.2),
        green=mat([(120, 200, 70), (60, 136, 56), (36, 90, 50)], w=2),
        blue=mat([(90, 140, 255), (48, 84, 210)], w=1.5),
        white=mat([(255, 255, 255), (220, 226, 244)], w=2),
    ))
    R = 35.0
    with m.at((0, 0, R), ry=frame*90.0):
        m.ellip((0, 0, 0), R, "skin", rows=12, cols=20)
        pores = [(8, 0.1, 0), (30, 0.5, 1), (52, -0.3, 0), (74, 0.75, 0), (20, -0.6, 1), (64, 0.3, 0), (42, -0.82, 0),
                 (84, -0.5, 1), (12, 0.88, 0), (36, 0.0, 1), (58, 0.6, 0), (80, 0.05, 0), (25, 0.25, 0), (47, -0.55, 0), (69, -0.12, 1)]
        for q in range(4):                                        # pebbled skin, 4-fold about the roll axis
            for (a, yy, kind) in pores:
                t = math.radians(a + q*90); s = math.sqrt(max(0.0, 1 - yy*yy))
                p = (R*0.985*s*math.sin(t), R*0.985*yy, R*0.985*s*math.cos(t))
                m.ellip(p, 1.7 if kind == 0 else 2.2, "pore" if kind == 0 else "lite", rows=3, cols=5)
        m.cyl((0, 0, R - 1), (0, 0, R + 3.5), 3.2, 2.4, "green", n=6)             # stem nub + calyx
        for k in range(5):
            a = 2*math.pi*k/5
            m.ellip((6*math.cos(a), 6*math.sin(a), R - 0.9), (4.2, 4.2, 1.4), "green", rows=3, cols=6)
        m.ellip((0, 0, -R + 0.6), (3.2, 3.2, 1.2), "pore", rows=3, cols=6)       # navel
        t = math.radians(118); yy = 0.42; s = math.sqrt(1 - yy*yy)              # fruit sticker
        d = np.array([s*math.sin(t), yy, s*math.cos(t)])
        m.cyl(tuple(d*(R - 0.5)), tuple(d*(R + 0.9)), 8.5, m="blue", n=10, cap1="blue", ring="blue", inner=0.6)
        m.cyl(tuple(d*(R + 0.9)), tuple(d*(R + 1.3)), 4.6, m="white", n=8)
    return m

def sp_butter():
    m = Model(dict(
        butter=mat([(255, 246, 170), (250, 222, 110), (220, 176, 70), (166, 122, 48)]),
        cut=mat([(255, 252, 204), (255, 236, 140)], w=1.2),
        dish=mat([(255, 255, 255), (224, 232, 248), (176, 190, 226), (128, 142, 190)]),
        blue=mat([(90, 140, 255), (52, 90, 214), (32, 54, 150)], w=1.6),
    ))
    m.box(-19, 19, -47, 47, 0, 3.0, "dish", tx=-4, ty=-4)
    m.box(-23.4, 23.4, -51.4, 51.4, 3.0, 3.5, "blue")
    m.box(-20.6, 20.6, -48.6, 48.6, 3.0, 3.9, "dish")
    m.box(-15, 15, -41.5, 27, 3.9, 25.9, "butter", tx=1.2, ty=0.8)
    m.box(-13.8, 13.8, 26.4, 27.2, 5, 25, "cut")
    with m.at((0, 31, 3.9), rx=-24):
        m.box(-15, 15, 0, 4.5, 0, 22, "butter", tx=1.2)
    with m.at((0, 44, 3.9)):
        m.box(-11, 11, -3.5, 3.5, 0, 4.5, "butter", tx=0.8, ty=0.8)             # a pat
    return m

# set piece table: name, frame builders, world_per_px, angles, supersample
# ---- SEWER SURF ---------------------------------------------------------------------------------
def sp_wave(frame):
    """THE FLUSH: a surging wall of water, 80 wide (y), ~24 tall, travelling toward +x."""
    m = Model(dict(
        water=mat([(124, 214, 186), (66, 166, 146), (38, 116, 114), (24, 76, 88), (18, 48, 66)]),
        lite=mat([(190, 244, 220), (130, 220, 196)], w=1.2),
        foam=mat([(255, 255, 255), (220, 240, 244), (164, 206, 218), (112, 160, 184)]),
    ))
    n = 10; w = 80.0/n
    for i in range(n):
        y0 = -40 + i*w; yc = y0 + w/2
        h = (0.66 + 0.34*math.sin(math.pi*(i + 0.5)/n))*(1 + 0.10*math.sin(i*1.9 + frame*2.3))
        xo = 3.0*math.sin(i*1.3 + frame*1.9)
        with m.at((xo, 0, 0), s=(1, 1, h)):
            m.prism([(-26, 0), (8, 0), (9, 11), (5, 19), (-5, 21), (-17, 12)], y0, y0 + w, "water", plane="xz")
        k = (i*7 + frame*3) % 5
        m.ellip((5.5 + xo, yc, 20.5*h), (5.4, 4.8, 3.6 + 0.3*k), "foam", rows=4, cols=7)            # curling crest
        m.ellip((10.0 + xo, yc + (k - 2)*0.6, 17.5*h), (3.6, 4.4, 3.0), "foam", rows=4, cols=6)
        m.ellip((11.5 + xo, yc, 1.4), (4.4 + 0.3*k, 4.6, 1.8), "foam", rows=3, cols=6)              # churn at the foot
        m.ellip((-13 + xo, yc + 1.5, 15.2*h), (5.0, 1.3, 1.0), "lite", rows=3, cols=5)              # glints on the back
        m.ellip((-21 + xo, yc - 1.5, 6.2*h), (4.0, 1.2, 0.9), "lite", rows=3, cols=5)
        for j in range(2):                                                                          # spray
            q = (i*5 + j*3 + frame*4) % 7
            m.ellip((9 + xo + q*1.2, yc + (q - 3)*1.0, 23*h + 2 + (q*1.7) % 5), 0.9 + 0.12*q, "foam", rows=3, cols=4)
    return m

# ---- inside the home computer (1 texel ~ 0.145 cm) ----------------------------------------------
def wall(m, R, z0, z1, n, rows, pat):
    """Cylinder wall of n facets x rows cells; pat(facet, row) -> material (equal cells merged)."""
    dz = (z1 - z0)/rows
    for i in range(n):
        a0 = 2*math.pi*(i - 0.5)/n; a1 = 2*math.pi*(i + 0.5)/n; am = (a0 + a1)/2
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        r = 0
        while r < rows:
            r1 = r
            while r1 < rows and pat(i, r1) == pat(i, r): r1 += 1
            quad(m, [(R*c0, R*s0, z0 + r*dz), (R*c1, R*s1, z0 + r*dz), (R*c1, R*s1, z0 + r1*dz), (R*c0, R*s0, z0 + r1*dz)],
                 pat(i, r), (math.cos(am), math.sin(am), 0))
            r = r1

def sp_capacitor():
    m = Model(dict(
        blue=mat([(84, 130, 250), (48, 86, 214), (30, 54, 156), (20, 34, 104)]),
        stripe=mat([(226, 230, 240), (170, 176, 196), (116, 122, 150)]),
        mark=mat([(40, 40, 60), (24, 24, 40)]),
        alu=mat([(244, 246, 252), (196, 202, 220), (140, 148, 176)]),
        score=mat([(110, 116, 144), (76, 80, 108)], w=1.5),
        rubber=mat([(60, 58, 74), (36, 34, 48)]),
    ))
    n = 20
    def pat(i, r):
        if i in (n - 1, 0, 1):
            if i == 0 and r % 5 in (2, 3) and 2 < r < 22: return "mark"       # row of "-" marks down the stripe
            return "stripe"
        return "blue"
    m.cyl((0, 0, 0), (0, 0, 4), 26.5, m="rubber", n=n)
    wall(m, 28.0, 4, 90, n, 24, pat)
    m.cyl((0, 0, 86), (0, 0, 90), 28.3, 28.3, "blue", n=n, phase=math.pi/n)       # crimp ring
    m.cyl((0, 0, 90), (0, 0, 95), 28.0, 25.5, "blue", n=n, cap1="alu", phase=math.pi/n)
    for a in (90, 210, 330):                                                    # scored vent
        with m.at((0, 0, 95), rz=a):
            m.box(0, 19, -1.1, 1.1, 0, 0.35, "score")
    return m

def sp_chip():
    m = Model(dict(
        body=mat([(92, 92, 110), (60, 60, 78), (38, 38, 54), (24, 24, 36)]),
        ink=mat([(240, 240, 232), (180, 180, 184)], w=1.3),
        leg=mat([(244, 246, 252), (190, 196, 214), (130, 138, 166), (86, 92, 124)]),
        notch=mat([(34, 34, 48), (20, 20, 30)], w=1.3),
    ))
    m.box(-25, 25, -85, 85, 8, 26, "body", tx=1.5, ty=1.0)
    m.box(-25.2, 25.2, -85.2, 85.2, 16, 17.2, "notch")                           # mould seam
    m.cyl((0, 83, 25.6), (0, 83, 26.2), 7.0, m="notch", n=10)                    # end notch
    m.cyl((-15, 70, 25.9), (-15, 70, 26.2), 2.6, m="notch", n=8)                 # pin-1 dot
    for i in range(14):
        y = -78 + 12*i
        for s in (1, -1):
            x0, x1 = (24, 30) if s > 0 else (-30, -24)
            m.box(x0, x1, y - 3.2, y + 3.2, 13.5, 16.0, "leg")                  # shoulder
            xa, xb = (28, 30.4) if s > 0 else (-30.4, -28)
            m.box(xa, xb, y - 3.2, y + 3.2, 4, 15, "leg")
            m.box(xa + 0.3, xb - 0.3, y - 1.3, y + 1.3, 0, 4, "leg")            # pin
    c = 1.7
    def lab(text, x):
        g = _txt(text).astype(np.uint8)
        decal(m, g, (x, -(g.shape[1]*c)/2 - 6, 26.12), (0, c, 0), (c, 0, 0), (0, 0, 1), [None, "ink"])
    lab("SPROCKET", -14); lab("SR-8  8612", 2)
    return m

def sp_resistor():
    m = Model(dict(
        body=mat([(244, 220, 170), (214, 180, 128), (164, 128, 92), (110, 84, 66)]),
        red=mat([(255, 84, 70), (206, 36, 46)], w=1.2), violet=mat([(180, 100, 250), (118, 56, 190)], w=1.2),
        yellow=mat([(255, 236, 80), (220, 176, 36)], w=1.2), gold=mat([(240, 200, 100), (176, 130, 50)], w=1.2),
        lead=mat([(244, 246, 252), (186, 192, 212), (120, 128, 158)], w=1.5),
    ))
    z = 15.0
    prof = [(-30, 7.5), (-27, 9.6), (-21, 9.6), (-17, 8.4), (17, 8.4), (21, 9.6), (27, 9.6), (30, 7.5)]
    for (y0, r0), (y1, r1) in zip(prof, prof[1:]):
        m.cyl((0, y0, z), (0, y1, z), r0, r1, "body", n=12)
    for (y, mm) in ((-24, "red"), (-12, "violet"), (-3, "yellow"), (22, "gold")):
        r = 10.0 if abs(y) > 20 else 8.8
        m.cyl((0, y - 2.4, z), (0, y + 2.4, z), r, m=mm, n=12)
    for s in (1, -1):
        m.cyl((0, 30*s, z), (0, 41*s, z), 1.6, m="lead", n=6)
        m.cyl((0, 41*s, z + 1.2), (0, 41*s, 0), 1.6, m="lead", n=6)
        m.cyl((0, 41*s, 0), (0, 41*s, 1.2), 3.6, 2.4, "lead", n=8)               # solder fillet
    return m

def sp_heatsink():
    m = Model(dict(
        fin=mat([(110, 112, 132), (70, 72, 92), (42, 42, 60), (26, 26, 40)]),
        tab=mat([(244, 246, 252), (190, 196, 214), (130, 138, 166)], w=1.3),
        pkg=mat([(70, 70, 88), (44, 44, 60), (28, 28, 40)]),
        hole=mat([(30, 30, 44), (18, 18, 28)], w=2),
        ink=mat([(230, 230, 226), (170, 170, 176)], w=1.2),
    ))
    m.box(22, 30, -40, 40, 0, 70, "fin")                                         # front plate
    for i in range(8):
        y = -40 + i*(76.0/7)
        m.box(-30, 23, y, y + 4, 2, 70, "fin")
    m.box(-30, 23, -40, 40, 2, 9, "fin")
    m.box(30, 32, -10, 10, 30, 52, "tab")                                        # TO-220: metal tab + bolt
    m.cyl((32, 0, 44), (33.6, 0, 44), 4.2, m="tab", n=8, cap1="hole")
    m.box(30, 37.5, -10, 10, 12, 33, "pkg")
    m.box(37.5, 37.9, -6, 6, 20, 22, "ink"); m.box(37.5, 37.9, -6, 3, 25, 27, "ink")
    for y in (-6, 0, 6):
        m.box(33, 35, y - 1.3, y + 1.3, 0, 12, "tab")
    return m

def sp_crystal():
    m = Model(dict(
        can=mat([(250, 252, 255), (206, 212, 228), (156, 164, 190), (108, 116, 150)]),
        base=mat([(150, 156, 180), (100, 106, 134), (66, 70, 96)]),
        stamp=mat([(96, 100, 128), (66, 70, 96)], w=1.2),
        lead=mat([(230, 232, 240), (150, 156, 180)], w=1.5),
    ))
    for s in (1, -1):
        m.cyl((0, 12*s, 6), (0, 12*s, 46), 8.0, 7.6, "can", n=12)
        m.cyl((0, 12*s, 2), (0, 12*s, 6), 9.2, m="base", n=12)
        m.cyl((0, 7*s, 0), (0, 7*s, 2), 1.2, m="lead", n=5)
    m.box(-8, 8, -12, 12, 6, 46, "can", tx=0.4)
    m.box(-9.2, 9.2, -12, 12, 2, 6, "base")
    for fx in (1, -1):
        with m.at(rz=0 if fx > 0 else 180):
            g = _txt("8.00").astype(np.uint8)
            decal(m, g, (8.12, -11.5, 36), (0, 1.0, 0), (0, 0, -1.4), (1, 0, 0), [None, "stamp"])
            m.box(8.0, 8.14, -9, 9, 18, 19.4, "stamp"); m.box(8.0, 8.14, -9, 2, 14, 15.4, "stamp")
    return m

def sp_ceramic():
    m = Model(dict(
        disc=mat([(255, 170, 70), (236, 124, 40), (184, 84, 30), (124, 52, 28)]),
        mark=mat([(110, 50, 30), (76, 34, 26)], w=1.3),
        lead=mat([(240, 242, 250), (176, 182, 204), (112, 120, 150)], w=2),
    ))
    m.cyl((-2.6, 0, 18), (2.6, 0, 18), 12, m="disc", n=14)
    m.ellip((0, 0, 18), (3.4, 9.5, 9.5), "disc", rows=5, cols=10)               # dipped bulge
    for y in (-5, 5):
        m.cyl((0, y, 9), (0, y, 0), 1.1, m="lead", n=5)
        m.cyl((0, y, 9.5), (0, y*0.8, 6.5), 2.0, 1.2, "disc", n=6)               # epoxy runs down the legs
    for fx in (1, -1):
        m.box(3.3*fx - 0.15, 3.3*fx + 0.15, -5, 5, 19.5, 21.0, "mark"); m.box(3.2*fx - 0.15, 3.2*fx + 0.15, -4, 2, 15.5, 17.0, "mark")
    return m

def sp_moth(frame):
    """The bug in the system: wingspan ~64, body ~40 long, crawling toward +x."""
    m = Model(dict(
        wing=mat([(244, 226, 190), (212, 186, 146), (164, 136, 108), (112, 90, 80)]),
        band=mat([(150, 110, 80), (108, 76, 60), (72, 50, 46)], w=1.2),
        spot=mat([(52, 40, 50), (30, 24, 34)], w=1.5),
        iris=mat([(255, 214, 110), (240, 150, 60)], glow=True, w=1.5),
        fur=mat([(214, 180, 140), (166, 130, 100), (112, 84, 72), (72, 52, 52)]),
        dark=mat([(84, 64, 62), (52, 38, 44), (32, 24, 32)], w=2),
        eye=mat([(40, 30, 44), (20, 16, 26)], w=3),
    ))
    zb = 6.0
    for i, (x, r) in enumerate(((-6, 4.6), (-11, 4.4), (-16, 4.0), (-21, 3.2), (-25, 2.2))):      # segmented abdomen
        m.ellip((x, 0, zb - 0.4*i), (3.6, r, r*0.9), "fur" if i % 2 == 0 else "dark", rows=4, cols=8)
    m.ellip((2, 0, zb + 0.6), (6.0, 5.4, 5.0), "fur", rows=5, cols=9)                              # fuzzy thorax
    m.ellip((9.5, 0, zb), (3.4, 3.6, 3.2), "fur", rows=4, cols=8)                                  # head
    for s in (1, -1):
        m.ellip((10.8, 2.6*s, zb + 0.8), 1.5, "eye", rows=3, cols=6)
        pts = [(11, 1.5*s, zb + 2.5), (16, 5*s, zb + 6), (19, 9*s, zb + 6.5)]                      # feathery antennae
        for a, b in zip(pts, pts[1:]): m.cyl(a, b, 0.5, m="dark", n=4)
        for k in range(5):
            t = k/4.0
            p = np.array(pts[1])*(1 - t) + np.array(pts[2])*t if k > 1 else np.array(pts[0])*(1 - k/2.0) + np.array(pts[1])*(k/2.0)
            m.box(p[0] - 1.6, p[0] + 1.6, p[1] - 0.25, p[1] + 0.25, p[2] - 0.25, p[2] + 0.25, "dark")
        for j, lx in enumerate((6, 1, -4)):                                                        # legs
            sw = (2.2 if (j + frame) % 2 == 0 else -2.2)*(1 if s > 0 else -1)
            knee = (lx + sw*0.5, 8*s, zb + 1.5); foot = (lx + sw + (j - 1)*2.5, 13*s, 0)
            m.cyl((lx, 3.5*s, zb - 1), knee, 0.6, m="dark", n=4); m.cyl(knee, foot, 0.5, m="dark", n=4)
        ang = (38 if frame == 0 else 12)*s
        with m.at((0, 3.0*s, zb + 3.0), rx=ang):
            def P(pts): return [(x, y*s) for x, y in pts]
            m.prism(P([(8, 0), (13, 22), (9, 30), (-2, 31), (-8, 20), (-6, 0)]), 0.0, 0.7, "wing", plane="xy")       # forewing
            m.prism(P([(-5, 0), (-7, 19), (-13, 23), (-21, 18), (-22, 7), (-15, 0)]), -0.5, 0.2, "wing", plane="xy")  # hindwing
            m.prism(P([(11.6, 16), (13, 22), (9, 30), (4, 30.6), (9, 22)]), 0.7, 0.95, "band", plane="xy")           # tip band
            m.prism(P([(8, 3), (9.5, 9), (-6.8, 9), (-6.4, 4)]), 0.7, 0.95, "band", plane="xy")
            m.prism(P([(-9, 14), (-8, 18), (-13, 22.4), (-20, 17.6), (-18, 14)]), 0.2, 0.45, "band", plane="xy")
            m.cyl((3, 19*s, 0.7), (3, 19*s, 1.05), 4.6, m="spot", n=10)                                               # eye-spots
            m.cyl((3, 19*s, 1.05), (3, 19*s, 1.3), 2.8, m="iris", n=8)
            m.cyl((3, 19*s, 1.3), (3, 19*s, 1.5), 1.2, m="spot", n=6)
            m.cyl((-14, 10*s, 0.2), (-14, 10*s, 0.55), 3.0, m="spot", n=8)
            m.cyl((-14, 10*s, 0.55), (-14, 10*s, 0.8), 1.5, m="iris", n=6)
    return m

WPP_TABLE = 1.6
ROLLING = ("snowball", "orange", "asteroid")   # 4 rolling/tumbling frames instead of angles
# several states baked on one shared canvas (same size and anchor), written as separate set pieces
SPLIT = {"toaster": ("toaster", "toaster_pop"), "wave": ("wave_a", "wave_b"), "moth": ("moth_a", "moth_b")}
SET_PIECES = [
    ("train_loco", [sp_train_loco], 0.45, 24, 3),
    ("train_car", [sp_train_car], 0.45, 24, 3),
    ("snowball", [lambda f=f: sp_snowball(f) for f in range(4)], 0.45, 1, 3),
    ("dragon_a", [lambda: sp_dragon(0)], 0.75, 16, 3),
    ("dragon_b", [lambda: sp_dragon(1)], 0.75, 16, 3),
    ("ship", [sp_ship], 0.75, 16, 3),
    ("dino_a", [lambda: sp_dino(0)], 0.8, 16, 3),
    ("dino_b", [lambda: sp_dino(1)], 0.8, 16, 3),
    ("blimp", [sp_blimp], 0.7, 16, 3),
    ("hovercar", [sp_hovercar], 0.35, 24, 4),
    ("hoverbus", [sp_hoverbus], 0.45, 24, 3),
    # trackside objects. Frame 0 = seen from behind (camera looking along the road, +x);
    # frame angles/2 shows the +x face.
    ("chevron_l", [lambda: sp_chevron(-1)], 0.35, 16, 4),
    ("chevron_r", [lambda: sp_chevron(1)], 0.35, 16, 4),
    ("ramp_sign", [sp_ramp_sign], 0.35, 16, 4),
    # BREAKFAST RUN table objects (coarse: they tower over toy-sized karts)
    ("cereal", [sp_cereal], WPP_TABLE, 16, 3),
    ("milk", [sp_milk], WPP_TABLE, 16, 3),
    ("juice", [sp_juice], WPP_TABLE, 16, 3),
    ("syrup", [sp_syrup], WPP_TABLE, 16, 3),
    ("toaster", [lambda: sp_toaster(False), lambda: sp_toaster(True)], 2.0, 16, 3),   # split into toaster + toaster_pop
    ("mug", [sp_mug], WPP_TABLE, 16, 3),
    ("orange", [lambda f=f: sp_orange(f) for f in range(4)], 0.9, 1, 3),
    ("butter", [sp_butter], 0.9, 16, 3),
    # SEWER SURF
    ("wave", [lambda: sp_wave(0), lambda: sp_wave(1)], 0.6, 16, 3),                          # -> wave_a + wave_b
    # home-computer board (1 texel ~ 0.145 cm)
    ("capacitor", [sp_capacitor], 1.4, 16, 3),
    ("chip", [sp_chip], 1.4, 16, 3),
    ("resistor", [sp_resistor], 1.2, 16, 3),
    ("heatsink", [sp_heatsink], 1.4, 16, 3),
    ("crystal", [sp_crystal], 1.2, 16, 3),
    ("ceramic", [sp_ceramic], 1.2, 16, 4),
    ("moth", [lambda: sp_moth(0), lambda: sp_moth(1)], 0.7, 16, 3),                          # -> moth_a + moth_b
    # SOLAR SYSTEM (anchors at the body centre)
    ("asteroid", [lambda f=f: sp_asteroid(f) for f in range(4)], 0.6, 1, 3),
    ("comet", [lambda: sp_comet()], 0.8, 16, 3),
    ("satellite", [lambda: sp_satellite()], 0.5, 16, 4),
    # HAUNTED graveyard: the ghost carriage is a vehicle (like the hoverbus), the arch spans the road
    # like the gantry, the chapel is a giant static landmark (coarse, like the table objects)
    ("carriage", [sp_carriage], 0.45, 24, 3),
]

def bake_set_piece(name, builders, wpp, angles, ss):
    models = [b() for b in builders]
    ppu = 1.0 / wpp
    if name in ROLLING:
        rels = [math.pi]   # 4 roll frames, seen head-on (rolling toward the camera)
    else:
        rels = [2*math.pi*k/angles for k in range(angles)]
    imgs, W, H, ax, ay = render_set(models, ppu, rels, ss)
    pal, _ = models[0].palette()
    nfr = len(imgs)
    return {"name": name, "w": W, "h": H, "angles": nfr, "ax": ax, "ay": ay, "wpp": wpp, "imgs": imgs, "pal": pal}

# ================================================================================================
# props (drawn directly at 1x with PIL, indexed)
# ================================================================================================
def indexed(w, h, palette, draw_fn):
    """palette: list of RGBA tuples; index 0 must be transparent. draw_fn(draw) draws with
    integer fills = palette indices."""
    im = Image.new("P", (w, h), 0)
    d = ImageDraw.Draw(im)
    draw_fn(d)
    px = [[im.getpixel((x, y)) for x in range(w)] for y in range(h)]
    return {"w": w, "h": h, "px": px, "pal": palette}

def outline(sprite, idx):
    w, h, px = sprite["w"], sprite["h"], sprite["px"]
    res = [row[:] for row in px]
    for y in range(h):
        for x in range(w):
            if px[y][x]: continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                X, Y = x+dx, y+dy
                if 0 <= X < w and 0 <= Y < h and px[Y][X] not in (0, idx):
                    res[y][x] = idx; break
    sprite["px"] = res
    return sprite

def crystal(frame):
    pal = [T, INK, (120, 220, 255, 255), (60, 150, 240, 255), (30, 80, 200, 255), (255, 255, 255, 255), (255, 210, 90, 255)]
    def f(d):
        c = 7.5; sq = [1.0, 0.7, 0.35, 0.7][frame]
        hw = 6*sq + 1
        d.polygon([(c, 1), (c+hw, 7), (c, 14), (c-hw, 7)], fill=3)
        d.polygon([(c, 1), (c+hw, 7), (c, 7)], fill=2)
        d.polygon([(c-hw, 7), (c, 14), (c, 7)], fill=4)
        d.line([(c-1, 4), (c-1, 9)], fill=5)
        d.point([(c, 6)], fill=6)
    return outline(indexed(16, 16, pal, f), 1)

def fake_crystal():
    # Upside-down proportions (long point on top), a faint violet-red cast, glint on the wrong
    # side and a red core where the real one has a gold spark.
    pal = [T, INK, (150, 200, 255, 255), (96, 128, 236, 255), (84, 52, 184, 255), (255, 236, 240, 255), (255, 70, 90, 255)]
    def f(d):
        c = 7.5; hw = 7
        d.polygon([(c, 1), (c+hw, 9), (c, 14), (c-hw, 9)], fill=3)
        d.polygon([(c, 1), (c+hw, 9), (c, 9)], fill=2)
        d.polygon([(c-hw, 9), (c, 14), (c, 9)], fill=4)
        d.line([(c+2, 5), (c+2, 10)], fill=5)
        d.point([(c, 9)], fill=6)
    return outline(indexed(16, 16, pal, f), 1)

def bolt(frame):
    pal = [T, INK, (255, 230, 120, 255), (230, 170, 40, 255), (150, 95, 20, 255)]
    def f(d):
        # a coin turning on its upright axis: nine tall always, nine wide face-on (it was
        # eleven wide by seven tall, an oval), narrowing to an edge
        hw = [4, 3, 1, 3][frame]
        c = 1 if hw > 1 else 0   # the corners cut off, except on the thin edge
        d.polygon([(5-hw+c, 1), (5+hw-c, 1), (5+hw, 1+c), (5+hw, 9-c), (5+hw-c, 9), (5-hw+c, 9), (5-hw, 9-c), (5-hw, 1+c)], fill=3)
        if hw > 2: d.rectangle([5-hw+1, 2, 5, 4], fill=2)
        if hw > 2: d.point([(5, 6)], fill=4)
        if hw <= 1: d.line([(5, 2), (5, 5)], fill=2)
    return outline(indexed(11, 11, pal, f), 1)

def bomb():
    pal = [T, INK, (90, 90, 110, 255), (50, 50, 64, 255), (200, 200, 220, 255), (255, 160, 40, 255), (255, 240, 120, 255)]
    def f(d):
        d.ellipse([2, 4, 13, 15], fill=3)
        d.ellipse([3, 5, 10, 11], fill=2)
        d.point([(5, 7), (6, 7), (5, 8)], fill=4)
        d.line([(10, 5), (12, 2)], fill=4)
        d.point([(13, 1)], fill=5); d.point([(12, 0)], fill=6)
    return outline(indexed(16, 16, pal, f), 1)

def oil():
    pal = [T, (10, 8, 14, 255), (40, 32, 60, 255), (90, 70, 140, 255), (160, 120, 220, 255)]
    def f(d):
        d.ellipse([0, 2, 23, 9], fill=1)
        d.ellipse([2, 3, 20, 8], fill=2)
        d.ellipse([5, 4, 12, 6], fill=3)
        d.point([(8, 4), (9, 4)], fill=4)
    return indexed(24, 11, pal, f)

def tree():
    pal = [T, INK, (90, 200, 80, 255), (50, 150, 60, 255), (30, 100, 50, 255), (120, 80, 50, 255), (80, 50, 30, 255)]
    def f(d):
        d.rectangle([10, 22, 13, 31], fill=5); d.line([(12, 22), (12, 31)], fill=6)
        d.ellipse([2, 6, 21, 25], fill=4)
        d.ellipse([3, 4, 18, 21], fill=3)
        d.ellipse([5, 2, 15, 13], fill=2)
    return outline(indexed(24, 32, pal, f), 1)

def cactus():
    pal = [T, INK, (140, 210, 100, 255), (80, 160, 70, 255), (50, 110, 50, 255), (255, 120, 160, 255)]
    def f(d):
        d.rounded_rectangle([7, 3, 12, 31], radius=2, fill=3)
        d.line([(8, 4), (8, 30)], fill=2)
        d.rounded_rectangle([1, 12, 5, 21], radius=2, fill=3); d.rectangle([4, 18, 8, 21], fill=3)
        d.rounded_rectangle([14, 8, 18, 18], radius=2, fill=4); d.rectangle([11, 15, 15, 18], fill=4)
        d.point([(9, 2), (10, 2)], fill=5)
    return outline(indexed(20, 32, pal, f), 1)

def pine():
    pal = [T, INK, (240, 248, 255, 255), (60, 130, 110, 255), (30, 90, 80, 255), (110, 80, 60, 255)]
    def f(d):
        d.rectangle([9, 26, 11, 31], fill=5)
        for (y0, hw) in ((2, 4), (8, 6), (14, 8), (19, 9)):
            d.polygon([(10, y0), (10+hw, y0+8), (10-hw, y0+8)], fill=4)
            d.polygon([(10, y0), (10+hw//2, y0+5), (10-hw//2, y0+5)], fill=3)
            d.line([(10-hw//2, y0+4), (10, y0+1)], fill=2)
    return outline(indexed(21, 32, pal, f), 1)

def palm():
    pal = [T, INK, (150, 230, 90, 255), (80, 180, 70, 255), (40, 120, 60, 255), (210, 160, 100, 255), (150, 100, 60, 255), (110, 70, 40, 255)]
    def f(d):
        pts = [(12 + 3*math.sin(t/10.0*1.6) + t*0.0, 39 - t*2.6) for t in range(12)]
        for i, (x, y) in enumerate(pts):
            d.ellipse([x-2, y-2, x+2, y+1], fill=5 if i % 2 else 6)
            d.line([(x-2, y+1), (x+1, y+1)], fill=7)
        tx, ty = pts[-1]
        fronds = [(-14, 6), (-11, -3), (-5, -8), (4, -9), (11, -4), (14, 5), (8, 9), (-8, 9)]
        for (fx, fy) in fronds:
            ex, ey = tx + fx, ty + fy
            mx, my = tx + fx*0.5, ty + fy*0.5 - 4
            d.polygon([(tx, ty-1), (mx, my-2), (ex, ey), (mx, my+2)], fill=3)
            d.line([(tx, ty), (mx, my), (ex, ey)], fill=4)
            d.line([(tx, ty-1), (mx, my-1)], fill=2)
        d.ellipse([tx-3, ty, tx, ty+3], fill=7); d.ellipse([tx, ty+1, tx+3, ty+4], fill=6)
    return outline(indexed(32, 42, pal, f), 1)

def fern():
    pal = [T, INK, (160, 236, 110, 255), (90, 190, 80, 255), (46, 130, 66, 255), (26, 84, 50, 255)]
    def f(d):
        bx, by = 15, 23
        for (ang, ln, sh) in ((-160, 13, 4), (-20, 13, 4), (-140, 14, 3), (-40, 14, 3), (-115, 15, 3), (-65, 15, 3), (-90, 15, 2)):
            a = math.radians(ang)
            pts = []
            for i in range(ln + 1):
                t = i/ln
                droop = t*t*6
                pts.append((bx + math.cos(a)*i, by + math.sin(a)*i + droop))
            for i in range(1, ln):
                x, y = pts[i]; w = (1 - i/ln)*3 + 1
                d.line([(x - w*0.7, y - w*0.4), (x + w*0.7, y + w*0.4)], fill=sh)
                d.line([(x + w*0.7, y - w*0.4), (x - w*0.7, y + w*0.4)], fill=sh)
            d.line(pts, fill=sh - 1 if sh > 2 else 2)
        d.ellipse([bx-3, by-2, bx+3, by+1], fill=5)
    return outline(indexed(31, 25, pal, f), 1)

def jungle_tree():
    pal = [T, INK, (120, 210, 90, 255), (64, 160, 70, 255), (36, 112, 58, 255), (22, 74, 46, 255),
           (150, 104, 66, 255), (100, 66, 42, 255), (60, 40, 28, 255)]
    def f(d):
        d.polygon([(15, 47), (16, 22), (22, 22), (23, 47)], fill=6)
        d.polygon([(10, 47), (16, 38), (16, 47)], fill=7); d.polygon([(28, 47), (22, 38), (22, 47)], fill=7)
        d.line([(18, 24), (18, 46)], fill=7); d.line([(21, 26), (21, 46)], fill=8)
        for (x0, y0, x1, y1, c) in ((1, 10, 20, 28, 5), (17, 8, 37, 27, 5), (5, 2, 33, 22, 4), (2, 6, 18, 20, 4),
                                    (20, 4, 36, 18, 4), (8, 1, 28, 14, 3), (4, 6, 14, 14, 3), (24, 6, 34, 14, 3)):
            d.ellipse([x0, y0, x1, y1], fill=c)
        for (x, y) in ((11, 4), (19, 3), (27, 7), (7, 9)):
            d.ellipse([x, y, x+4, y+3], fill=2)
        for (x, y0, y1) in ((6, 20, 30), (12, 24, 36), (29, 22, 33), (33, 18, 27)):
            d.line([(x, y0), (x, y1)], fill=4)
            d.point([(x, y1)], fill=3)
    return outline(indexed(38, 48, pal, f), 1)

def neon_sign():
    pal = [T, INK, (60, 56, 80, 255), (36, 32, 52, 255), (255, 120, 230, 255), (255, 220, 250, 255),
           (90, 240, 255, 255), (220, 255, 255, 255), (110, 110, 130, 255)]
    def f(d):
        d.rectangle([5, 16, 6, 25], fill=8); d.rectangle([17, 16, 18, 25], fill=8)
        d.rectangle([0, 1, 23, 17], fill=3)
        d.rectangle([1, 2, 22, 16], fill=2)
        d.polygon([(3, 7), (8, 4), (8, 6), (12, 6), (12, 8), (8, 8), (8, 10)], outline=4)   # neon arrow + bolt
        d.point([(7, 5)], fill=5)
        d.line([(16, 4), (14, 8), (18, 8), (15, 13)], fill=6)
        d.point([(16, 5)], fill=7)
        d.line([(3, 14), (20, 14)], fill=4)
    return outline(indexed(24, 26, pal, f), 1)

def fire(frame):
    pal = [T, (90, 20, 20, 255), (255, 250, 200, 255), (255, 220, 80, 255), (255, 140, 40, 255), (220, 60, 30, 255)]
    def f(d):
        tips = [((3, 7), (8, 1), (13, 6)), ((2, 5), (7, 3), (12, 2)), ((4, 4), (9, 2), (14, 5))][frame]
        base = 15
        for (tx, ty), off in zip(tips, (-1, 0, 1)):
            d.polygon([(tx - 3, base), (tx, ty), (tx + 3, base)], fill=5)
        d.ellipse([1, 9, 14, 15], fill=5)
        for (tx, ty) in tips:
            d.polygon([(tx - 2, base - 1), (tx, ty + 3), (tx + 2, base - 1)], fill=4)
        d.ellipse([3, 10, 12, 15], fill=4)
        d.polygon([(5, 14), (7 + frame - 1, 7 + frame), (10, 14)], fill=3)
        d.ellipse([5, 11, 10, 15], fill=3)
        d.line([(7, 12), (8, 14)], fill=2)
    return outline(indexed(16, 16, pal, f), 1)

def cannonball():
    pal = [T, INK, (120, 120, 140, 255), (70, 70, 88, 255), (40, 40, 54, 255), (220, 220, 235, 255)]
    def f(d):
        d.ellipse([1, 1, 8, 8], fill=4)
        d.ellipse([1, 1, 7, 7], fill=3)
        d.ellipse([2, 2, 5, 5], fill=2)
        d.point([(3, 3)], fill=5)
    return outline(indexed(10, 10, pal, f), 1)

def explosion(frame):
    pal = [T, INK, (255, 255, 230, 255), (255, 230, 100, 255), (255, 150, 40, 255), (220, 70, 40, 255),
           (140, 130, 140, 255), (90, 84, 96, 255)]
    def f(d):
        c = 11.5
        if frame == 0:
            pts = []
            for i in range(16):
                r = 7 if i % 2 else 3.5
                a = i*math.pi/8
                pts.append((c + r*math.cos(a), c + r*math.sin(a)))
            d.polygon(pts, fill=3)
            d.ellipse([c-3, c-3, c+3, c+3], fill=2)
        elif frame == 1:
            pts = []
            for i in range(20):
                r = 11 if i % 2 else 7
                a = i*math.pi/10 + 0.2
                pts.append((c + r*math.cos(a), c + r*math.sin(a)))
            d.polygon(pts, fill=5)
            d.ellipse([c-7, c-7, c+7, c+7], fill=4)
            d.ellipse([c-5, c-6, c+4, c+3], fill=3)
            d.ellipse([c-3, c-4, c+1, c], fill=2)
        else:
            for (x, y, r, col) in ((7, 9, 5, 7), (15, 8, 5, 7), (11, 15, 6, 7), (8, 8, 4, 6), (15, 7, 4, 6), (11, 13, 4, 6)):
                d.ellipse([x-r, y-r, x+r, y+r], fill=col)
            d.ellipse([9, 11, 13, 15], fill=5); d.point([(11, 13)], fill=4)
            d.point([(5, 4), (19, 18), (3, 16)], fill=4)
    return outline(indexed(24, 24, pal, f), 1)

def rocket_fx():
    pal = [T, INK, (255, 255, 255, 255), (200, 205, 220, 255), (255, 90, 70, 255), (190, 40, 50, 255),
           (255, 230, 110, 255), (255, 140, 40, 255)]
    def f(d):
        d.polygon([(0, 4), (3, 6), (0, 8)], fill=7)
        d.point([(1, 6)], fill=6)
        d.rectangle([3, 4, 8, 7], fill=3); d.line([(3, 4), (8, 4)], fill=2)
        d.polygon([(8, 4), (11, 5.5), (11, 6), (8, 7)], fill=4)
        d.polygon([(3, 4), (5, 4), (3, 1)], fill=5); d.polygon([(3, 7), (5, 7), (3, 10)], fill=5)
        d.point([(6, 5)], fill=5)
    return outline(indexed(12, 12, pal, f), 1)

def warning_light(frame):
    pal = [T, INK, (240, 240, 245, 255), (180, 180, 190, 255), (230, 50, 50, 255), (255, 120, 100, 255),
           (90, 30, 34, 255), (60, 60, 70, 255), (255, 230, 220, 255)]
    def f(d):
        d.rectangle([7, 8, 8, 31], fill=3); d.line([(7, 8), (7, 31)], fill=2)
        d.line([(1, 1), (14, 8)], fill=2, width=2); d.line([(14, 1), (1, 8)], fill=2, width=2)   # crossbuck
        d.line([(2, 2), (13, 7)], fill=4); d.line([(13, 2), (2, 7)], fill=4)
        d.rectangle([1, 12, 14, 13], fill=7)
        for i, cx in enumerate((3, 12)):
            on = (i == frame)
            d.ellipse([cx-3, 13, cx+3, 19], fill=7)
            d.ellipse([cx-2, 14, cx+2, 18], fill=4 if on else 6)
            if on: d.point([(cx+1, 17)], fill=5)
            if on: d.point([(cx-1, 15)], fill=8)
        d.rectangle([4, 30, 11, 31], fill=7)
    return outline(indexed(16, 32, pal, f), 1)

def icon_nitro():
    pal = [T, INK, (240, 70, 60, 255), (170, 30, 40, 255), (230, 230, 240, 255), (255, 200, 60, 255), (255, 120, 30, 255)]
    def f(d):
        d.rounded_rectangle([4, 2, 11, 11], radius=2, fill=2)
        d.rectangle([5, 3, 6, 10], fill=4)
        d.rectangle([4, 7, 11, 8], fill=3)
        d.polygon([(5, 12), (10, 12), (7.5, 15)], fill=6); d.point([(7, 12), (8, 13)], fill=5)
    return outline(indexed(16, 16, pal, f), 1)

def icon_nitro3():
    pal = [T, INK, (240, 70, 60, 255), (170, 30, 40, 255), (230, 230, 240, 255), (255, 200, 60, 255), (255, 120, 30, 255)]
    def f(d):
        for (x, y) in ((1, 5), (10, 5), (5, 1)):
            d.rounded_rectangle([x, y, x+4, y+7], radius=1, fill=2)
            d.line([(x+1, y+1), (x+1, y+6)], fill=4)
            d.line([(x, y+4), (x+4, y+4)], fill=3)
            d.polygon([(x+1, y+8), (x+3, y+8), (x+2, y+10)], fill=6)
        d.point([(7, 9), (2, 13), (11, 13)], fill=5)
    return outline(indexed(16, 16, pal, f), 1)

def icon_shield():
    pal = [T, INK, (140, 255, 230, 255), (60, 200, 200, 255), (30, 120, 150, 255), (255, 255, 255, 255)]
    def f(d):
        d.ellipse([1, 1, 14, 14], fill=4)
        d.ellipse([2, 2, 13, 13], fill=3)
        d.ellipse([4, 4, 11, 11], fill=0)
        d.arc([3, 3, 12, 12], 200, 260, fill=5)
    return outline(indexed(16, 16, pal, f), 1)

def icon_oil():
    pal = [T, INK, (90, 70, 140, 255), (50, 40, 80, 255), (170, 140, 230, 255)]
    def f(d):
        d.polygon([(8, 1), (13, 9), (8, 14), (3, 9)], fill=3)
        d.ellipse([3, 6, 13, 14], fill=3)
        d.ellipse([4, 7, 9, 11], fill=2)
        d.point([(6, 8)], fill=4)
    return outline(indexed(16, 16, pal, f), 1)

def icon_rocket():
    pal = [T, INK, (255, 255, 255, 255), (200, 205, 220, 255), (255, 90, 70, 255), (190, 40, 50, 255),
           (255, 230, 110, 255), (255, 140, 40, 255)]
    def f(d):   # diagonal rocket pointing up-right
        d.polygon([(2, 14), (3, 10), (6, 13)], fill=7); d.point([(3, 12)], fill=6)
        d.polygon([(4, 10), (10, 4), (12, 6), (6, 12)], fill=3)
        d.line([(5, 10), (10, 5)], fill=2)
        d.polygon([(10, 4), (14, 2), (12, 6)], fill=4)
        d.polygon([(4, 10), (3, 7), (6, 8)], fill=5); d.polygon([(6, 12), (9, 13), (8, 10)], fill=5)
    return outline(indexed(16, 16, pal, f), 1)

def icon_emp():
    pal = [T, INK, (255, 255, 200, 255), (255, 230, 70, 255), (90, 220, 255, 255), (40, 130, 220, 255)]
    def f(d):
        d.ellipse([0, 0, 15, 15], fill=5)
        d.ellipse([1, 1, 14, 14], fill=4)
        d.ellipse([3, 3, 12, 12], fill=0)
        d.polygon([(9, 1), (4, 9), (7, 9), (6, 14), (11, 6), (8, 6)], fill=3)
        d.line([(8, 3), (6, 7)], fill=2)
    return outline(indexed(16, 16, pal, f), 1)

def icon_fake():
    s = fake_crystal()
    s["px"] = [row[:] for row in s["px"]]
    # small "?" badge in the corner so the HUD reads it as a decoy
    s["pal"] = s["pal"] + [(255, 255, 255, 255)]
    q = 7
    for (x, y) in ((12, 0), (13, 0), (14, 1), (13, 2), (13, 4)):
        s["px"][y][x] = q
    for (x, y) in ((11, 0), (15, 0), (12, 1), (15, 1), (12, 2), (14, 2), (12, 3), (14, 3), (12, 4), (14, 4), (13, 3), (13, 5), (12, 5), (14, 5), (13, 1), (11, 1)):
        if s["px"][y][x] == 0: s["px"][y][x] = 1
    return s

def icon_magnet():
    pal = [T, INK, (255, 110, 100, 255), (210, 40, 50, 255), (240, 240, 250, 255), (170, 175, 190, 255), (120, 220, 255, 255)]
    def f(d):
        d.arc([2, 1, 13, 12], 180, 360, fill=3, width=4)
        d.rectangle([2, 6, 5, 10], fill=3); d.rectangle([10, 6, 13, 10], fill=3)
        d.arc([3, 2, 12, 11], 200, 260, fill=2)
        d.rectangle([2, 11, 5, 13], fill=4); d.rectangle([10, 11, 13, 13], fill=4)
        d.line([(5, 11), (5, 13)], fill=5); d.line([(13, 11), (13, 13)], fill=5)
        d.point([(1, 15), (7, 15), (14, 15)], fill=6)
    return outline(indexed(16, 16, pal, f), 1)

# ---- 3D-rendered props ----
def prop_castle_tower():
    m = Model(dict(
        stone=mat([(214, 208, 196), (166, 160, 152), (112, 108, 108), (72, 70, 76)]),
        stone2=mat([(196, 188, 176), (148, 142, 134), (98, 94, 96), (62, 60, 68)]),
        dark=mat([(50, 44, 56), (30, 26, 36)], w=3),
        roof=mat([(110, 120, 200), (66, 76, 156), (40, 46, 104)]),
        red=mat([(255, 90, 80), (210, 40, 46)], w=2),
        pole=mat([(120, 90, 60), (80, 58, 40)]),
    ))
    for i in range(8):
        m.cyl((0, 0, i*3.0), (0, 0, i*3.0 + 3.0), 6.0, m="stone" if i % 2 else "stone2", n=12, phase=(i % 2)*math.pi/12)
    m.cyl((0, 0, 24), (0, 0, 26.5), 7.2, m="stone", n=12)                       # corbel
    for i in range(6):
        a = 2*math.pi*i/6 + 0.3
        with m.at((6.4*math.cos(a), 6.4*math.sin(a), 26.5), rz=math.degrees(a)):
            m.box(-0.9, 0.9, -1.8, 1.8, 0, 3.0, "stone2")
    m.cyl((0, 0, 26.4), (0, 0, 27.0), 6.2, m="stone2", n=12)
    m.box(5.4, 6.3, -0.8, 0.8, 9, 14, "dark"); m.box(5.4, 6.3, -0.9, 0.9, 17, 21, "dark")
    m.cyl((0, 0, 27), (0, 0, 37), 0.35, m="pole", n=5)
    m.box(-6.0, 0, -0.3, 0.3, 32.5, 37, "red")
    return render_prop3d(m, 1.0, math.radians(110), ss=4)

def prop_banner():
    m = Model(dict(
        pole=mat([(200, 160, 100), (140, 104, 60), (92, 66, 40)]),
        gold=mat([(255, 224, 110), (210, 158, 48)], w=2),
        cloth=mat([(90, 140, 255), (52, 84, 204), (30, 46, 124)]),
        red=mat([(255, 100, 90), (210, 40, 50)], w=2),
    ))
    m.cyl((0, 0, 0), (0, 0, 32), 0.6, m="pole", n=6)
    m.ellip((0, 0, 32.6), 1.0, "gold", rows=3, cols=6)
    m.box(-0.4, 0.4, -6, 6, 29.5, 30.5, "pole")
    m.prism([(-5.6, 29.5), (5.6, 29.5), (5.6, 14), (0, 10), (-5.6, 14)], -0.3, 0.3, "cloth", plane="yz")
    m.prism([(-1.4, 26), (1.4, 26), (1.4, 16), (0, 14.5), (-1.4, 16)], -0.5, 0.5, "red", plane="yz")
    m.prism([(-4.4, 21.5), (4.4, 21.5), (4.4, 19.5), (-4.4, 19.5)], -0.45, 0.45, "gold", plane="yz")
    return render_prop3d(m, 1.0, math.radians(165), ss=4)

def prop_barrel():
    m = Model(dict(
        wood=mat([(220, 150, 84), (172, 108, 58), (118, 72, 40), (76, 46, 28)]),
        band=mat([(120, 120, 140), (76, 76, 94), (46, 46, 60)], w=2),
        top=mat([(200, 140, 80), (150, 98, 52)]),
    ))
    m.cyl((0, 0, 0), (0, 0, 5), 4.2, 5.0, "wood", n=12)
    m.cyl((0, 0, 5), (0, 0, 10), 5.0, 4.2, "wood", n=12, cap1="top")
    for z in (1.0, 8.4):
        m.cyl((0, 0, z), (0, 0, z + 0.9), 4.6, 4.6, "band", n=12)
    m.cyl((0, 0, 4.6), (0, 0, 5.4), 5.15, m="band", n=12)
    return render_prop3d(m, 1.25, math.radians(130), ss=4)

def prop_lamp():
    m = Model(dict(
        pole=mat([(110, 110, 134), (66, 66, 86), (38, 38, 52)]),
        neon=mat([(170, 255, 255), (0, 224, 255)], glow=True, w=3),
        pink=mat([(255, 140, 236), (240, 60, 200)], glow=True, w=3),
    ))
    m.cyl((0, 0, 0), (0, 0, 1.5), 2.0, 1.6, "pole", n=8)
    m.cyl((0, 0, 1.5), (0, 0, 30), 0.7, 0.55, "pole", n=6)
    m.cyl((0, 0, 30), (4.5, 0, 33), 0.5, m="pole", n=6)
    m.box(3.0, 10.0, -1.6, 1.6, 32.4, 34.2, "pole")
    m.box(3.4, 9.6, -1.3, 1.3, 30.8, 32.4, "neon")
    m.box(-1.3, 1.3, -1.3, 1.3, 8, 24, "pole")
    for a in (0, 90, 180, 270):
        with m.at(rz=a):
            m.box(1.2, 1.6, -0.5, 0.5, 9, 23, "pink")
    return render_prop3d(m, 1.0, math.radians(100), ss=4)

def prop_snow_rock():
    m = Model(dict(
        rock=mat([(170, 166, 176), (124, 120, 134), (84, 82, 98), (56, 54, 70)]),
        snow=mat([(255, 255, 255), (220, 232, 252), (170, 190, 230)]),
    ))
    m.hexa([(-9, -6, 0), (8, -7, 0), (9, 6, 0), (-8, 7, 0), (-6, -4, 9), (5, -5, 10), (6, 3, 8), (-5, 5, 9)], "rock")
    m.hexa([(4, -8, 0), (13, -5, 0), (12, 4, 0), (5, 3, 0), (6, -6, 5), (11, -4, 5), (11, 2, 4), (7, 2, 6)], "rock")
    m.ellip((-0.5, 0, 9.2), (6.5, 5.5, 1.8), "snow")
    m.ellip((8.6, -2, 5.3), (3.2, 3.2, 1.0), "snow", rows=4, cols=8)
    m.ellip((0, 0, 0.3), (11, 9, 1.0), "snow", rows=4, cols=10)
    return render_prop3d(m, 1.0, math.radians(140), ss=4)

# ---- more trackside props (3D-baked, single view) ----------------------------------------------
STONE = [(214, 208, 196), (166, 160, 152), (112, 108, 108), (72, 70, 76)]

def prop_log():
    m = Model(dict(
        bark=mat([(150, 102, 66), (112, 72, 46), (78, 48, 34), (52, 32, 26)]),
        wood=mat([(244, 206, 140), (214, 164, 96), (160, 112, 62)], w=1.5),
        moss=mat([(140, 210, 90), (84, 160, 60), (48, 108, 50)]),
    ))
    m.cyl((0, -11, 3.4), (0, 11, 3.4), 3.4, m="bark", n=10, cap="wood", ring="bark", inner=0.78)
    m.cyl((0, -11.3, 3.4), (0, -11.0, 3.4), 1.0, m="bark", n=6)
    m.cyl((0, 11.0, 3.4), (0, 11.3, 3.4), 1.0, m="bark", n=6)
    m.cyl((0.4, 4.5, 5.6), (1.6, 6.5, 9.2), 1.2, 1.0, "bark", n=6, cap1="wood")
    m.ellip((0.3, -4.5, 6.4), (2.4, 4.2, 0.9), "moss", rows=4, cols=8)
    return render_prop3d(m, 1.0, math.radians(152), ss=4)

def prop_bones():
    m = Model(dict(bone=mat([(255, 250, 226), (226, 214, 180), (170, 154, 128), (112, 98, 90)])))
    m.cyl((-9.5, 0, 1.0), (9.5, 0, 1.0), 1.2, 0.9, "bone", n=6)
    s20 = math.sin(math.radians(20))
    for x, R, Hh in ((-7.0, 6.0, 12.5), (0.0, 7.6, 16.0), (7.0, 6.4, 13.0)):
        m.ellip((x, 0, 1.2), (1.3, 1.6, 1.3), "bone", rows=3, cols=6)
        for s in (1, -1):
            pts = []
            for i in range(6):
                a = math.radians(-20 + 100*i/5)
                pts.append((x - 0.25*i, s*R*math.cos(a), Hh*(math.sin(a) + s20)/(math.sin(math.radians(80)) + s20)))
            for i in range(5):
                m.cyl(pts[i], pts[i+1], 1.25 - 0.12*i, 1.13 - 0.12*i, "bone", n=5)
    return render_prop3d(m, 1.0, math.radians(128), ss=4)

def prop_crate():
    m = Model(dict(
        wood=mat([(250, 206, 130), (218, 160, 88), (166, 112, 60), (112, 72, 42)]),
        frame=mat([(176, 110, 56), (128, 74, 40), (86, 48, 30)], w=1.6),
        nail=mat([(220, 220, 232), (130, 130, 150)], w=2),
    ))
    m.box(-5, 5, -5, 5, 0, 10, "wood")
    for rz in (0, 90, 180, 270):
        with m.at(rz=rz):
            m.box(5, 5.35, -5.2, 5.2, 8.4, 10.2, "frame"); m.box(5, 5.35, -5.2, 5.2, -0.0, 1.8, "frame")
            m.box(5, 5.35, -5.2, -3.5, 0, 10.2, "frame"); m.box(5, 5.35, 3.5, 5.2, 0, 10.2, "frame")
            m.prism([(-3.5, 1.8), (-1.6, 1.8), (3.5, 8.4), (1.6, 8.4)], 5, 5.3, "frame", plane="yz")
            m.box(5.35, 5.45, -4.7, -4.1, 9.0, 9.6, "nail"); m.box(5.35, 5.45, 4.1, 4.7, 9.0, 9.6, "nail")
    m.box(-5.2, 5.2, -5.2, -3.6, 10, 10.3, "frame"); m.box(-5.2, 5.2, 3.6, 5.2, 10, 10.3, "frame")
    m.box(-5.2, -3.6, -5.2, 5.2, 10, 10.3, "frame"); m.box(3.6, 5.2, -5.2, 5.2, 10, 10.3, "frame")
    return render_prop3d(m, 1.2, math.radians(150), ss=4)

def prop_buoy():
    m = Model(dict(
        red=mat([(255, 96, 80), (216, 44, 48), (140, 24, 40)]),
        white=mat([(255, 255, 255), (214, 222, 240), (150, 162, 196)]),
        foam=mat([(230, 250, 255), (150, 214, 250)], glow=True),
        metal=mat([(200, 206, 224), (130, 136, 160), (84, 88, 112)], w=2.5),
        lamp=mat([(255, 250, 170), (255, 200, 70)], glow=True, w=3),
    ))
    m.ellip((0, 0, 0.3), (9.0, 9.0, 0.5), "foam", rows=3, cols=12)                 # ripple ring
    m.ellip((0, 0, 3.6), (7.0, 7.0, 4.6), "red", rows=7, cols=12,
            fn=lambda x, y, z: "white" if 0.05 < z < 0.55 else None)                # fat float, white band
    m.cyl((0, 0, 7.4), (0, 0, 8.6), 3.6, 3.2, "red", n=10)
    for a in (20, 140, 260):                                                        # lattice tower
        r = math.radians(a)
        m.cyl((3.0*math.cos(r), 3.0*math.sin(r), 8.6), (0.6*math.cos(r), 0.6*math.sin(r), 16.0), 0.5, m="metal", n=4)
    m.cyl((0, 0, 12.0), (0, 0, 12.6), 2.2, m="metal", n=6)
    m.cyl((0, 0, 16.0), (0, 0, 16.8), 1.6, m="red", n=8)
    m.ellip((0, 0, 18.0), 1.6, "lamp", rows=4, cols=8)
    return render_prop3d(m, 1.0, math.radians(140), ss=4)

def prop_snowman():
    m = Model(dict(
        snow=mat([(255, 255, 255), (224, 234, 252), (176, 194, 234), (128, 146, 200)]),
        coal=mat([(60, 56, 72), (30, 28, 40)], w=4),
        carrot=mat([(255, 160, 50), (226, 104, 30)], w=4),
        hat=mat([(84, 80, 110), (50, 46, 72), (30, 28, 46)]),
        red=mat([(255, 96, 80), (214, 44, 50), (140, 24, 40)], w=1.5),
        stick=mat([(150, 100, 60), (96, 62, 40)], w=3),
    ))
    m.ellip((0, 0, 5.0), (6.6, 6.6, 5.6), "snow", rows=6, cols=12)
    m.ellip((0, 0, 12.4), (4.8, 4.8, 4.2), "snow", rows=6, cols=12)
    m.ellip((0, 0, 18.4), 3.4, "snow", rows=6, cols=10)
    m.cyl((0, 0, 14.8), (0, 0, 16.2), 3.5, 3.3, "red", n=10)                       # scarf
    m.box(1.8, 3.4, 1.6, 3.0, 11.0, 15.0, "red")
    m.cyl((3.0, 0, 18.2), (6.6, 0, 17.6), 0.8, 0.1, "carrot", n=6)                 # nose
    m.box(2.9, 3.5, 0.9, 1.9, 19.2, 20.2, "coal"); m.box(2.9, 3.5, -1.9, -0.9, 19.2, 20.2, "coal")
    for z in (10.6, 12.8):
        m.box(4.5, 5.1, -0.5, 0.5, z, z + 1.0, "coal")
    m.cyl((0, 0, 20.8), (0, 0, 21.5), 4.4, m="hat", n=10)                          # hat
    m.cyl((0, 0, 21.5), (0, 0, 25.5), 2.8, 2.9, "hat", n=10)
    m.cyl((0, 0, 21.5), (0, 0, 22.5), 2.95, m="red", n=10)
    for s in (1, -1):
        m.cyl((0, 4.0*s, 13.2), (1.0, 9.0*s, 16.5), 0.45, m="stick", n=4)
        m.cyl((0.8, 7.6*s, 15.6), (0.5, 8.4*s, 18.0), 0.35, m="stick", n=4)
    return render_prop3d(m, 1.0, math.radians(158), ss=4)

def prop_cone():
    m = Model(dict(
        orange=mat([(255, 150, 50), (236, 100, 30), (170, 62, 26)]),
        white=mat([(255, 255, 255), (220, 226, 240), (160, 168, 196)], w=1.3),
        base=mat([(200, 84, 30), (140, 54, 26), (92, 36, 24)]),
    ))
    m.box(-4.6, 4.6, -4.6, 4.6, 0, 1.2, "base")
    m.cyl((0, 0, 1.2), (0, 0, 5.0), 3.5, 2.6, "orange", n=10)
    m.cyl((0, 0, 5.0), (0, 0, 8.0), 2.6, 1.9, "white", n=10)
    m.cyl((0, 0, 8.0), (0, 0, 12.0), 1.9, 0.9, "orange", n=10)
    return render_prop3d(m, 1.2, math.radians(140), ss=4)

def prop_pillar():
    m = Model(dict(
        stone=mat(STONE), stone2=mat([(196, 188, 176), (148, 142, 134), (98, 94, 96), (62, 60, 68)]),
        dark=mat([(84, 80, 92), (50, 46, 60)], w=1.5),
        moss=mat([(130, 190, 96), (80, 140, 70)], w=1.5),
    ))
    m.box(-6, 6, -6, 6, 0, 2.2, "stone2")
    m.box(-5, 5, -5, 5, 2.2, 3.6, "stone")
    for i in range(5):
        z = 3.6 + i*4.1
        m.cyl((0, 0, z), (0, 0, z + 4.1), 4.0 - 0.08*i, 4.0 - 0.08*(i+1), "stone" if i % 2 else "stone2", n=10,
              phase=(i % 2)*math.pi/10)
        m.cyl((0, 0, z + 3.9), (0, 0, z + 4.1), 4.05 - 0.08*i, m="dark", n=10, phase=(i % 2)*math.pi/10)
    m.box(-5, 5, -5, 5, 24.1, 25.6, "stone")
    m.box(-6, 6, -6, 6, 25.6, 27.8, "stone2", tx=0.0)
    m.box(5.9, 6.1, -4, -1, 0.4, 1.6, "moss"); m.box(-2, 2, -6.1, -5.9, 0.3, 1.5, "moss")
    return render_prop3d(m, 1.0, math.radians(150), ss=4)

def prop_torch_frames():
    def build(frame):
        m = Model(dict(
            iron=mat([(120, 116, 140), (78, 74, 98), (46, 44, 64), (30, 28, 44)]),
            gold=mat([(255, 214, 100), (196, 146, 50)], w=1.5),
            core=mat([(255, 252, 200), (255, 226, 96)], glow=True, w=1.5),
            flame=mat([(255, 150, 44), (240, 90, 30)], glow=True),
            ember=mat([(240, 70, 40), (200, 40, 36)], glow=True, w=2),
        ))
        for a in (30, 150, 270):
            r = math.radians(a)
            m.cyl((4.6*math.cos(r), 4.6*math.sin(r), 0), (0, 0, 6.0), 0.75, 0.6, "iron", n=5)
        m.cyl((0, 0, 4.5), (0, 0, 17.0), 0.9, 0.8, "iron", n=6)
        m.cyl((0, 0, 9.5), (0, 0, 10.6), 1.4, m="gold", n=6)
        m.cyl((0, 0, 17.0), (0, 0, 20.0), 1.6, 4.4, "iron", n=10, cap1="ember")
        m.cyl((0, 0, 19.6), (0, 0, 20.4), 4.7, m="gold", n=10, cap1="ember")
        if frame == 0:
            m.ellip((0, 0, 21.8), (3.3, 3.3, 2.6), "core", rows=5, cols=8)
            m.cyl((0, 0.2, 22.6), (0, 1.6, 29.5), 2.9, 0.0, "flame", n=8)
            m.cyl((0, -2.0, 22.0), (0, -3.2, 25.5), 1.4, 0.0, "flame", n=6)
            m.ellip((0, -1.4, 30.4), 0.7, "ember", rows=3, cols=5)
        else:
            m.ellip((0, 0, 21.6), (3.5, 3.5, 2.4), "core", rows=5, cols=8)
            m.cyl((0, -0.2, 22.4), (0, -1.9, 28.2), 3.0, 0.0, "flame", n=8)
            m.cyl((0, 2.0, 22.0), (0, 3.4, 26.6), 1.5, 0.0, "flame", n=6)
            m.ellip((0, 1.8, 30.0), 0.7, "ember", rows=3, cols=5)
        return m
    ms = [build(0), build(1)]
    imgs, W, H, ax, ay = render_set(ms, 1.0, [math.radians(170)], 4)
    pal, _ = ms[0].palette()
    return [{"w": W, "h": H, "px": im.tolist(), "pal": pal} for im in imgs]

def prop_cyber_barrier():
    m = Model(dict(
        body=mat([(96, 96, 124), (60, 60, 84), (38, 38, 56), (24, 24, 38)]),
        neon=mat([(170, 255, 255), (0, 224, 255)], glow=True, w=2),
        pink=mat([(255, 140, 236), (240, 60, 200)], glow=True, w=1.6),
        glow=mat([(200, 255, 255), (90, 230, 255)], glow=True),
    ))
    m.box(-2.0, 2.0, -12, 12, 1.2, 8.0, "body", tx=0.5)
    m.box(-1.3, 1.3, -12.3, 12.3, 8.0, 8.9, "neon")
    for y in (-9, 9):
        m.box(-2.6, 2.6, y - 1.6, y + 1.6, 0, 1.4, "body")
        m.box(-2.9, 2.9, y - 1.0, y + 1.0, 0.2, 0.8, "glow")
    for fx in (1, -1):
        for i in range(5):
            y = -10.2 + i*4.6
            poly = [(y, 2.4), (y + 2.0, 2.4), (y + 4.0, 6.8), (y + 2.0, 6.8)]
            with m.at(rz=0 if fx > 0 else 180):
                m.prism(poly, 1.72, 1.95, "pink", plane="yz")
    return render_prop3d(m, 1.0, math.radians(160), ss=4)

def prop_rock():
    m = Model(dict(
        rock=mat([(190, 176, 160), (144, 130, 122), (100, 90, 92), (64, 58, 66)]),
        rock2=mat([(170, 158, 150), (124, 114, 112), (84, 78, 84), (54, 50, 60)]),
    ))
    m.hexa([(-8, -6, 0), (7, -7, 0), (8, 6, 0), (-7, 7, 0), (-5, -3.5, 9), (3.5, -4.5, 11), (4.5, 3, 9.5), (-4, 4, 8)], "rock")
    m.hexa([(3, -8, 0), (12, -5, 0), (11, 4, 0), (4, 3, 0), (5.5, -6, 5.5), (10, -4, 5), (9.5, 1.5, 4.5), (6, 1.5, 6.5)], "rock2")
    m.hexa([(-11, -2, 0), (-5, -4, 0), (-4, 5, 0), (-10, 5, 0), (-9, -1, 4), (-6, -2, 5), (-5.5, 3, 4.5), (-8.5, 3.5, 3.5)], "rock2")
    return render_prop3d(m, 1.0, math.radians(140), ss=4)

def prop_shrub():
    m = Model(dict(
        leaf=mat([(150, 226, 96), (92, 186, 70), (52, 136, 60), (30, 92, 56)]),
        berry=mat([(255, 110, 120), (220, 50, 70)], glow=True, w=5),
    ))
    m.ellip((0, 0, 4.2), (5.4, 6.0, 4.6), "leaf", rows=5, cols=9)
    m.ellip((0.5, -5.0, 3.0), (4.0, 4.2, 3.4), "leaf", rows=4, cols=8)
    m.ellip((-0.5, 5.2, 3.2), (4.2, 4.0, 3.6), "leaf", rows=4, cols=8)
    m.ellip((1.0, 1.8, 7.6), (3.0, 3.2, 2.4), "leaf", rows=4, cols=7)
    for (x, y, z) in ((5.2, -1.6, 4.4), (3.6, 4.6, 5.2), (3.8, -5.6, 3.4), (4.0, 1.6, 7.6)):
        m.box(x, x + 0.5, y - 0.55, y + 0.55, z - 0.55, z + 0.55, "berry")
    return render_prop3d(m, 1.0, math.radians(165), ss=4)

def tumbleweed():
    pal = [T, INK, (236, 200, 132, 255), (196, 150, 88, 255), (140, 98, 58, 255), (98, 66, 44, 255)]
    def f(d):
        d.ellipse([1, 1, 14, 14], fill=4)
        d.ellipse([1, 1, 14, 14], outline=3)
        d.arc([2, 2, 12, 11], 150, 330, fill=2)
        d.arc([4, 4, 13, 13], 320, 140, fill=5)
        d.arc([3, 5, 10, 12], 200, 20, fill=3)
        d.arc([5, 2, 12, 9], 30, 200, fill=2)
        d.line([(4, 11), (11, 4)], fill=5); d.line([(4, 5), (9, 12)], fill=3)
        d.point([(6, 6), (9, 8), (7, 10), (10, 6)], fill=2)
        d.point([(5, 8), (8, 5), (10, 10)], fill=5)
        d.point([(0, 6), (15, 9), (6, 0), (10, 15)], fill=3)          # stray twigs
    return outline(indexed(16, 16, pal, f), 1)

# ---- 5x7 font ------------------------------------------------------------------------------
FONT = {
 '0': ["01110","10001","10011","10101","11001","10001","01110"],
 '1': ["00100","01100","00100","00100","00100","00100","01110"],
 '2': ["01110","10001","00001","00110","01000","10000","11111"],
 '3': ["11110","00001","00001","01110","00001","00001","11110"],
 '4': ["00010","00110","01010","10010","11111","00010","00010"],
 '5': ["11111","10000","11110","00001","00001","10001","01110"],
 '6': ["00110","01000","10000","11110","10001","10001","01110"],
 '7': ["11111","00001","00010","00100","01000","01000","01000"],
 '8': ["01110","10001","10001","01110","10001","10001","01110"],
 '9': ["01110","10001","10001","01111","00001","00010","01100"],
 'A': ["01110","10001","10001","11111","10001","10001","10001"],
 'B': ["11110","10001","10001","11110","10001","10001","11110"],
 'C': ["01110","10001","10000","10000","10000","10001","01110"],
 'D': ["11100","10010","10001","10001","10001","10010","11100"],
 'E': ["11111","10000","10000","11110","10000","10000","11111"],
 'F': ["11111","10000","10000","11110","10000","10000","10000"],
 'G': ["01110","10001","10000","10111","10001","10001","01111"],
 'H': ["10001","10001","10001","11111","10001","10001","10001"],
 'I': ["01110","00100","00100","00100","00100","00100","01110"],
 'J': ["00111","00010","00010","00010","00010","10010","01100"],
 'K': ["10001","10010","10100","11000","10100","10010","10001"],
 'L': ["10000","10000","10000","10000","10000","10000","11111"],
 'M': ["10001","11011","10101","10101","10001","10001","10001"],
 'N': ["10001","10001","11001","10101","10011","10001","10001"],
 'O': ["01110","10001","10001","10001","10001","10001","01110"],
 'P': ["11110","10001","10001","11110","10000","10000","10000"],
 'Q': ["01110","10001","10001","10001","10101","10010","01101"],
 'R': ["11110","10001","10001","11110","10100","10010","10001"],
 'S': ["01111","10000","10000","01110","00001","00001","11110"],
 'T': ["11111","00100","00100","00100","00100","00100","00100"],
 'U': ["10001","10001","10001","10001","10001","10001","01110"],
 'V': ["10001","10001","10001","10001","10001","01010","00100"],
 'W': ["10001","10001","10001","10101","10101","10101","01010"],
 'X': ["10001","10001","01010","00100","01010","10001","10001"],
 'Y': ["10001","10001","10001","01010","00100","00100","00100"],
 'Z': ["11111","00001","00010","00100","01000","10000","11111"],
 '.': ["00000","00000","00000","00000","00000","01100","01100"],
 ',': ["00000","00000","00000","00000","01100","00100","01000"],
 ':': ["00000","01100","01100","00000","01100","01100","00000"],
 '!': ["00100","00100","00100","00100","00100","00000","00100"],
 '?': ["01110","10001","00001","00010","00100","00000","00100"],
 '-': ["00000","00000","00000","11111","00000","00000","00000"],
 '/': ["00001","00010","00010","00100","01000","01000","10000"],
 "'": ["00100","00100","01000","00000","00000","00000","00000"],
 '"': ["01010","01010","10100","00000","00000","00000","00000"],
 '<': ["00010","00100","01000","10000","01000","00100","00010"],
 '>': ["01000","00100","00010","00001","00010","00100","01000"],
 '+': ["00000","00100","00100","11111","00100","00100","00000"],
 '%': ["11001","11010","00010","00100","01000","01011","10011"],
 '(': ["00010","00100","01000","01000","01000","00100","00010"],
 ')': ["01000","00100","00010","00010","00010","00100","01000"],
 '#': ["01010","11111","01010","01010","01010","11111","01010"],
 '*': ["00000","10101","01110","11111","01110","10101","00000"],
 ' ': ["00000"]*7,
}

# ================================================================================================
# emit
# ================================================================================================
# ================================================================================================
# HUD / UI art (numpy masks -> indexed sprites)
# ================================================================================================
BIG = {  # chunky 9x11 display face (3-px strokes), upscaled with scale2x/3x corner rounding
 'S': [".#######.","#########","###...###","###......","########.","#########",".########","......###","###...###","#########",".#######."],
 'P': ["########.","#########","###...###","###...###","###...###","#########","########.","###......","###......","###......","###......"],
 'R': ["########.","#########","###...###","###...###","#########","########.","###.###..","###..###.","###..###.","###...###","###...###"],
 'O': [".#######.","#########","###...###","###...###","###...###","###...###","###...###","###...###","###...###","#########",".#######."],
 'C': [".#######.","#########","###...###","###......","###......","###......","###......","###......","###...###","#########",".#######."],
 'K': ["###...###","###..###.","###.###..","######...","#####....","#####....","######...","###.###..","###..###.","###...###","###...###"],
 'E': ["#########","#########","###......","###......","#######..","#######..","###......","###......","###......","#########","#########"],
 'T': ["#########","#########","...###...","...###...","...###...","...###...","...###...","...###...","...###...","...###...","...###..."],
 'A': ["..#####..",".#######.","###...###","###...###","###...###","#########","#########","###...###","###...###","###...###","###...###"],
 'L': ["###......","###......","###......","###......","###......","###......","###......","###......","###......","#########","#########"],
 'Y': ["###...###","###...###","###...###",".###.###.","..#####..","...###...","...###...","...###...","...###...","...###...","...###..."],
 '1': ["...###...","..####...",".#####...","...###...","...###...","...###...","...###...","...###...","...###...",".#######.",".#######."],
 '2': [".#######.","#########","###...###","......###",".....###.","....###..","..####...",".###.....","###......","#########","#########"],
 '3': [".#######.","#########","###...###","......###","..######.","..######.","......###","......###","###...###","#########",".#######."],
 '4': ["....####.","...#####.","..######.",".###.###.","###..###.","###..###.","#########","#########",".....###.",".....###.",".....###."],
 '5': ["#########","#########","###......","###......","########.","#########","......###","......###","###...###","#########",".#######."],
 '6': [".#######.","#########","###...###","###......","########.","#########","###...###","###...###","###...###","#########",".#######."],
 '7': ["#########","#########","......###",".....###.",".....###.","....###..","....###..","...###...","...###...","...###...","...###..."],
 '8': [".#######.","#########","###...###","###...###",".#######.",".#######.","###...###","###...###","###...###","#########",".#######."],
}

def _scale2x(a):
    h, w = a.shape; p = np.pad(a, 1)
    E = a; B = p[:-2, 1:-1]; Hh = p[2:, 1:-1]; D = p[1:-1, :-2]; F = p[1:-1, 2:]
    o = np.zeros((h*2, w*2), a.dtype)
    o[0::2, 0::2] = np.where((D == B) & (B != F) & (D != Hh), D, E)
    o[0::2, 1::2] = np.where((B == F) & (B != D) & (F != Hh), F, E)
    o[1::2, 0::2] = np.where((D == Hh) & (D != B) & (Hh != F), D, E)
    o[1::2, 1::2] = np.where((Hh == F) & (D != Hh) & (B != F), F, E)
    return o

def _scale3x(a):
    h, w = a.shape; p = np.pad(a, 1)
    A = p[:-2, :-2]; B = p[:-2, 1:-1]; C = p[:-2, 2:]; D = p[1:-1, :-2]; E = a; F = p[1:-1, 2:]
    G = p[2:, :-2]; Hh = p[2:, 1:-1]; I = p[2:, 2:]
    ok = (B != Hh) & (D != F)
    o = np.zeros((h*3, w*3), a.dtype)
    o[0::3, 0::3] = np.where(ok & (D == B), D, E)
    o[0::3, 1::3] = np.where(ok & (((D == B) & (E != C)) | ((B == F) & (E != A))), B, E)
    o[0::3, 2::3] = np.where(ok & (B == F), F, E)
    o[1::3, 0::3] = np.where(ok & (((D == B) & (E != G)) | ((D == Hh) & (E != A))), D, E)
    o[1::3, 1::3] = E
    o[1::3, 2::3] = np.where(ok & (((B == F) & (E != I)) | ((Hh == F) & (E != C))), F, E)
    o[2::3, 0::3] = np.where(ok & (D == Hh), D, E)
    o[2::3, 1::3] = np.where(ok & (((D == Hh) & (E != I)) | ((Hh == F) & (E != G))), Hh, E)
    o[2::3, 2::3] = np.where(ok & (Hh == F), F, E)
    return o

def _glyph(ch, scale=1, table=None):
    rows = (table or BIG)[ch]
    a = np.array([[c in "#1" for c in r] for r in rows], np.uint8)
    if scale == 2: a = _scale2x(a)
    elif scale == 3: a = _scale3x(a)
    elif scale == 4: a = _scale2x(_scale2x(a))
    return a.astype(bool)

def _shear(m, rows_per_px):
    h, w = m.shape; ex = (h - 1)//rows_per_px
    o = np.zeros((h, w + ex), bool)
    for y in range(h):
        s = (h - 1 - y)//rows_per_px
        o[y, s:s + w] = m[y]
    return o

def _dil(m, n=1):
    for i in range(n):
        p = np.pad(m, 1)
        o = p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
        if i % 2 == 1: o |= p[:-2, :-2] | p[:-2, 2:] | p[2:, :-2] | p[2:, 2:]
        m = o
    return m

def _place(shape, m, x, y):
    """mask m positioned at (x, y) on a canvas-sized bool array (clipped)."""
    Hc, Wc = shape; o = np.zeros(shape, bool); h, w = m.shape
    x0, y0 = max(0, x), max(0, y); x1, y1 = min(Wc, x + w), min(Hc, y + h)
    if x1 > x0 and y1 > y0: o[y0:y1, x0:x1] = m[y0 - y:y1 - y, x0 - x:x1 - x]
    return o

def _poly(shape, pts):
    im = Image.new("1", (shape[1], shape[0]), 0)
    ImageDraw.Draw(im).polygon(pts, fill=1)
    return np.array(im, bool)

def _disc(shape, cx, cy, r):
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    return (xx - cx)**2 + (yy - cy)**2 <= r*r

def _gear(shape, cx, cy, r_out, r_in, hole, teeth=8, phase=0.0, duty=0.0):
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    dx, dy = xx - cx, yy - cy
    d = np.hypot(dx, dy); a = np.arctan2(dy, dx)
    tooth = np.cos(teeth*(a - phase)) > duty
    return (d <= np.where(tooth, r_out, r_in)) & (d > hole)

def _emboss(c, M, ramp, hi=None, lo=None, ink=1, ow=2):
    """Outline mask M with `ink` (ow px), fill it with a top->bottom band ramp of palette
    indices, then a 1-px highlight along top edges and shade along bottom edges."""
    if not M.any(): return
    if ow: c[_dil(M, ow) & ~M] = ink
    ys = np.nonzero(M.any(1))[0]; y0, y1 = ys[0], ys[-1] + 1
    for y in range(y0, y1):
        k = min(len(ramp) - 1, (y - y0)*len(ramp)//(y1 - y0))
        c[y][M[y]] = ramp[k]
    p = np.pad(M, 1)
    if hi is not None: c[M & ~p[:-2, 1:-1]] = hi
    if lo is not None: c[M & ~p[2:, 1:-1]] = lo

def _spr(c, pal):
    assert len(pal) <= 32 and int(c.max()) < len(pal), (len(pal), int(c.max()))
    return {"w": c.shape[1], "h": c.shape[0], "px": c.tolist(), "pal": pal}

def _word(text, scale, gap, table=None):
    gs = [_glyph(ch, scale, table) for ch in text]
    h = gs[0].shape[0]; w = sum(g.shape[1] for g in gs) + gap*(len(gs) - 1)
    o = np.zeros((h, w), bool); x = 0; xs = []
    for g in gs:
        o[:, x:x + g.shape[1]] = g; xs.append(x); x += g.shape[1] + gap
    return o, xs

def _font_mask(text):
    o = np.zeros((7, 6*len(text) - 1), bool)
    for i, ch in enumerate(text):
        for y, r in enumerate(FONT[ch]):
            for x, b in enumerate(r):
                if b == "1": o[y, i*6 + x] = True
    return o

def logo():
    W, Hh = 200, 64
    pal = [T, INK,
           (236, 250, 255, 255), (132, 208, 255, 255), (60, 136, 240, 255), (36, 72, 176, 255),   # 2..5 chrome blue
           (255, 255, 255, 255), (24, 40, 112, 255),                                             # 6 hi, 7 lo
           (255, 244, 150, 255), (255, 204, 60, 255), (244, 140, 36, 255), (200, 80, 32, 255),   # 8..11 gold
           (255, 255, 224, 255), (120, 44, 28, 255),                                             # 12 hi, 13 lo
           (255, 236, 120, 255), (255, 160, 48, 255), (240, 80, 40, 255), (176, 28, 48, 255),    # 14..17 flame
           (255, 255, 210, 255), (96, 16, 44, 255),                                              # 18 hi, 19 lo
           (40, 36, 64, 255), (72, 60, 110, 255)]                                                # 20 checker dark, 21 shadow
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    # --- bottom word: RALLY, big italic flame letters
    rw, _ = _word("RALLY", 3, 3)
    rw = _shear(rw, 4)
    rx, ry = 30, 28
    R = _place(S, rw, rx, ry)
    # speed lines streaming off the left of RALLY
    lines = np.zeros(S, bool)
    for i, (yy, x0) in enumerate(((33, 8), (40, 1), (47, 10), (54, 3))):
        lines[yy:yy + 3, x0:rx + 12 - i*2] = True
    lines &= ~_dil(R, 3)
    # checkered flag flying off the right
    flag = np.zeros(S, bool); chk = np.zeros(S, bool)
    for yy in range(33, 55):
        sh = int(round(1.5*math.sin((yy - 33)/22*math.pi)))
        for xx in range(181, 197):
            X = xx + (54 - yy)//6 + sh
            if 0 <= X < W:
                flag[yy, X] = True
                if ((xx - 181)//4 + (yy - 33)//4) % 2: chk[yy, X] = True
    pole = np.zeros(S, bool); 
    for yy in range(31, 62):
        X = 179 + (54 - yy)//6
        pole[yy, X:X + 2] = True
    # --- top word: SPROCKET in chrome blue, the O is a golden sprocket
    tw, xs = _word("SPROCKET", 2, 2)
    tx, ty = 21, 3
    ox = xs[3]
    tw[:, ox:ox + 18] = False
    Tm = _place(S, tw, tx, ty)
    gcx, gcy = tx + ox + 8.5, ty + 10.5
    G = _gear(S, gcx, gcy, 11.6, 8.6, 3.4, teeth=8, phase=math.pi/8, duty=0.1)
    # paint back to front
    c[_dil(lines, 1) & ~lines] = 1
    _emboss(c, lines, [18, 14, 15], ow=0)
    c[_dil(flag | pole, 1)] = 1
    c[flag] = 6; c[chk] = 20; c[pole] = 9
    sh = _place(S, rw, rx + 2, ry + 2)
    c[_dil(sh, 1)] = 1
    c[_dil(Tm | G, 2)] = 1
    c[_dil(R, 2)] = 1
    _emboss(c, Tm, [2, 3, 3, 4, 4, 5], hi=6, lo=7, ow=0)
    _emboss(c, G, [8, 9, 9, 10, 11], hi=12, lo=13, ow=0)
    c[_disc(S, gcx, gcy, 3.4)] = 1
    c[_disc(S, gcx, gcy, 1.6)] = 10
    _emboss(c, R, [14, 14, 15, 15, 16, 16, 17], hi=18, lo=19, ow=0)
    return _spr(c, pal)

PLACE_RAMPS = [
    [(255, 252, 190), (255, 220, 80), (240, 160, 36), (180, 96, 24), (255, 255, 240), (110, 54, 20)],       # gold
    [(250, 252, 255), (200, 210, 230), (140, 152, 186), (92, 100, 140), (255, 255, 255), (56, 60, 96)],     # silver
    [(255, 208, 160), (230, 150, 90), (184, 100, 56), (130, 64, 40), (255, 236, 210), (84, 36, 26)],        # bronze
    [(255, 255, 255), (200, 232, 255), (128, 184, 248), (72, 120, 216), (255, 255, 255), (36, 60, 140)],    # white-blue
]

def place_badge(n):
    W, Hh = 34, 26
    ramp = PLACE_RAMPS[min(n, 4) - 1]
    pal = [T, INK] + [tuple(r) + (255,) for r in ramp]
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    D = _place(S, _glyph(str(n), 2), 2, 2)
    suf = {1: "ST", 2: "ND", 3: "RD"}.get(n, "TH")
    Sm = _place(S, _font_mask(suf), 21, 3)
    sx = 20 if n == 1 else 21
    if n == 1: Sm = _place(S, _font_mask(suf), sx, 3)
    c[_dil(D, 2)] = 1
    c[_dil(Sm, 2)] = 1
    _emboss(c, D, [2, 2, 3, 3, 4, 5], hi=6, lo=7, ow=0)
    _emboss(c, Sm, [2, 3], ow=0)
    return _spr(c, pal)

def start_light(state):
    W, Hh = 40, 18
    pal = [T, INK, (96, 100, 124, 255), (60, 62, 84, 255), (38, 38, 56, 255),            # 2..4 housing
           (84, 36, 44, 255), (52, 24, 34, 255),                                         # 5,6 lamp off
           (255, 80, 64, 255), (208, 28, 40, 255), (255, 200, 180, 255),                 # 7..9 red on
           (110, 255, 120, 255), (30, 196, 70, 255), (220, 255, 220, 255),               # 10..12 green on
           (130, 136, 160, 255)]                                                         # 13 housing highlight
    im = Image.new("P", (W, Hh), 0); d = ImageDraw.Draw(im)
    d.rectangle([7, 0, 9, 3], fill=3); d.rectangle([30, 0, 32, 3], fill=3)
    d.rounded_rectangle([0, 2, 39, 17], radius=3, fill=1)
    d.rounded_rectangle([1, 3, 38, 16], radius=2, fill=3)
    d.line([(3, 3), (36, 3)], fill=13); d.line([(2, 4), (37, 4)], fill=2)
    d.line([(2, 15), (37, 15)], fill=4); d.line([(3, 16), (36, 16)], fill=4)
    for i, cx in enumerate((7, 19, 31)):
        if state == 3: on, a, b, hl = True, 10, 11, 12
        else: on, a, b, hl = i < state, 7, 8, 9
        d.ellipse([cx - 4, 5, cx + 5, 14], fill=1)
        if on:
            d.ellipse([cx - 3, 6, cx + 4, 13], fill=b)
            d.ellipse([cx - 3, 6, cx + 3, 12], fill=a)
            d.rectangle([cx - 1, 7, cx, 8], fill=hl); d.point([(cx - 2, 8), (cx + 3, 12)], fill=hl)
        else:
            d.ellipse([cx - 3, 6, cx + 4, 13], fill=6)
            d.ellipse([cx - 3, 6, cx + 3, 12], fill=5)
            d.point([(cx - 1, 7), (cx, 7), (cx - 2, 8)], fill=2)
    px = [[im.getpixel((x, y)) for x in range(W)] for y in range(Hh)]
    return {"w": W, "h": Hh, "px": px, "pal": pal}

def trophy(kind):
    ramp = PLACE_RAMPS[kind]
    pal = [T, INK] + [tuple(r) + (255,) for r in ramp] + [(150, 96, 60, 255), (104, 62, 40, 255), (70, 40, 30, 255)]
    W, Hh = 28, 32
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    bowl = _poly(S, [(6, 2), (21, 2), (21, 8), (19, 13), (16, 16), (11, 16), (8, 13), (6, 8)])
    hl = (_disc(S, 5.5, 7.5, 4.6) & ~_disc(S, 5.5, 7.5, 2.4)); hl[:, 7:] = False
    hr = (_disc(S, 21.5, 7.5, 4.6) & ~_disc(S, 21.5, 7.5, 2.4)); hr[:, :21] = False
    stem = np.zeros(S, bool); stem[16:21, 12:16] = True; stem[18:19, 11:17] = True
    foot = np.zeros(S, bool); foot[21:23, 9:19] = True; foot[23:25, 7:21] = True
    cup = bowl | hl | hr | stem | foot
    plinth = np.zeros(S, bool); plinth[25:31, 5:23] = True
    c[_dil(cup | plinth, 1)] = 1
    for x in range(W):                                        # metal: shade by column
        k = 3 if x <= 7 else 6 if x <= 9 else 2 if x <= 11 else 3 if x <= 16 else 4 if x <= 18 else 5
        c[:, x][cup[:, x]] = k
    c[(hl | hr) & ~bowl] = 3
    p = np.pad(hl | hr, 1); c[(hl | hr) & ~bowl & ~p[2:, 1:-1]] = 5
    c[2, 6:22] = 6; c[3, 8:20] = 5; c[3, 9:12] = 4                               # rim + dark mouth
    c[14:16][:, 8:20][bowl[14:16, 8:20]] = 4; c[15, 11:17] = 5
    c[18, 11:17] = 2; c[21, 9:19] = 2; c[24, 7:21] = 5
    c[plinth] = 9; c[25, 5:23] = 8; c[30, 5:23] = 10; c[26:30, 21:23] = 10
    c[27:29, 10:18] = 3; c[27, 10:18] = 2
    return _spr(c, pal)

def cup_emblem(which):
    W = Hh = 24
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    if which == 1:   # Sprocket Cup: golden gear around a red hub with a white keyed bore
        pal = [T, INK, (255, 248, 170, 255), (255, 208, 64, 255), (236, 144, 36, 255), (176, 88, 28, 255),
               (255, 110, 90, 255), (214, 44, 50, 255), (136, 22, 40, 255), (255, 255, 255, 255), (200, 210, 236, 255)]
        G = _gear(S, 11.5, 11.5, 11.2, 8.4, 0, teeth=8, phase=math.pi/8, duty=0.05)
        c[_dil(G, 1)] = 1
        _emboss(c, G, [2, 3, 3, 4, 5], ow=0)
        c[_disc(S, 11.5, 11.5, 6.6)] = 1
        hub = _disc(S, 11.5, 11.5, 5.6)
        _emboss(c, hub, [6, 7, 7, 8], ow=0)
        c[_disc(S, 11.5, 11.5, 2.6)] = 1
        c[_disc(S, 11.5, 11.5, 1.6)] = 9
        c[12:14, 11:13] = 10
        for (x, y) in ((8, 8), (15, 8), (8, 15), (15, 15)): c[y, x] = 9
    else:            # Turbo Cup: violet disc, cyan ring, yellow lightning bolt
        pal = [T, INK, (200, 150, 255, 255), (140, 84, 232, 255), (88, 44, 172, 255), (52, 24, 112, 255),
               (140, 250, 255, 255), (40, 190, 240, 255), (255, 252, 180, 255), (255, 216, 64, 255), (240, 144, 32, 255)]
        Dm = _disc(S, 11.5, 11.5, 11.2)
        c[_dil(Dm, 1)] = 1
        c[Dm] = 7
        c[Dm & (np.mgrid[0:Hh, 0:W][0] < 9)] = 6
        c[_disc(S, 11.5, 11.5, 9.4)] = 1
        _emboss(c, _disc(S, 11.5, 11.5, 8.6), [2, 3, 3, 4, 5], ow=0)
        B = _poly(S, [(14, 2), (7, 12), (11, 12), (8, 21), (17, 9), (13, 9), (16, 2)])
        c[_dil(B, 1) & _disc(S, 11.5, 11.5, 11.2)] = 1
        _emboss(c, B, [8, 9, 9, 10], ow=0)
    return _spr(c, pal)

def podium():
    W, Hh = 120, 36
    pal = [T, INK, (250, 250, 255, 255), (206, 214, 236, 255), (150, 160, 196, 255), (100, 108, 150, 255),   # 2..5 block
           (255, 110, 96, 255), (214, 48, 54, 255), (140, 26, 44, 255)]                                       # 6..8 carpet
    base = len(pal)
    for r in PLACE_RAMPS[:3]: pal += [tuple(v) + (255,) for v in (r[1], r[2], r[5])]
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    # (x0, x1, top, digit, ramp#)
    for (x0, x1, top, dg, k) in ((1, 40, 9, "2", 1), (80, 119, 15, "3", 2), (40, 80, 1, "1", 0)):
        c[top - 1:Hh, x0 - 1:x1 + 1] = 1
        c[top:top + 4, x0:x1] = 7; c[top, x0:x1] = 6; c[top + 3, x0:x1] = 8
        c[top + 4:Hh - 1, x0:x1] = 3
        c[top + 4, x0:x1] = 2; c[top + 5:Hh - 1, x0:x0 + 2] = 2
        c[top + 5:Hh - 1, x1 - 3:x1] = 4; c[Hh - 4:Hh - 1, x0:x1] = 4; c[Hh - 2, x0:x1] = 5
        g = _glyph(dg, 1)
        gy = top + 4 + max(1, (Hh - 5 - top - 4 - 11)//2 + 1)
        Gm = _place(S, g, (x0 + x1)//2 - 4, gy)
        c[_dil(Gm, 1)] = 1
        b = base + k*3
        _emboss(c, Gm, [b, b, b + 1], lo=b + 2, ow=0)
    return _spr(c, pal)

def arrow_wrongway():
    W = Hh = 24
    pal = [T, INK, (255, 244, 150, 255), (255, 196, 56, 255), (255, 110, 40, 255), (226, 40, 44, 255),
           (255, 255, 255, 255), (130, 20, 40, 255)]
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    cx, cy = 12.0, 9.5
    arc = _disc(S, cx, cy, 8.6) & ~_disc(S, cx, cy, 3.4); arc[10:, :] = False
    M = arc.copy()
    M[10:22, 16:21] = True                      # right leg (where you came from)
    M[10:14, 4:9] = True                        # left leg, ends in the head pointing back down
    M |= _poly(S, [(1, 13), (12, 13), (6.5, 21)])
    c[_dil(M, 1)] = 1
    _emboss(c, M, [2, 3, 3, 4, 4, 5], hi=6, lo=7, ow=0)
    return _spr(c, pal)

def cursor():
    pal = [T, INK, (255, 255, 255, 255), (255, 226, 96, 255), (240, 150, 40, 255), (176, 90, 28, 255)]
    rows = [".11.......",
            ".1211.....",
            ".123211...",
            ".12333211.",
            ".123333321",
            ".134444431",
            ".13444411.",
            ".134411...",
            ".1411.....",
            ".11......."]
    c = np.array([[int(ch) if ch != "." else 0 for ch in r] for r in rows], np.uint8)
    return _spr(c, pal)

def item_frame():
    W = Hh = 24
    pal = [T, INK, (255, 250, 190, 255), (255, 214, 80, 255), (236, 150, 40, 255), (170, 90, 30, 255),
           (120, 230, 255, 255), (40, 150, 230, 255), (255, 255, 255, 255)]
    c = np.zeros((Hh, W), np.uint8)
    c[0:24, 0:24] = 1
    c[1:23, 1:23] = 3
    c[1, 1:23] = 2; c[1:23, 1] = 2; c[22, 1:23] = 5; c[1:23, 22] = 5
    c[2, 2:22] = 3; c[21, 2:22] = 4; c[2:22, 21] = 4
    c[3:21, 3:21] = 1
    c[4:20, 4:20] = 0                                         # transparent 16x16 window
    for (y, x) in ((0, 0), (0, 23), (23, 0), (23, 23)): c[y, x] = 0
    # cyan corner studs (sprocket teeth poking into the window corners)
    for (y, x, dy, dx) in ((4, 4, 1, 1), (4, 19, 1, -1), (19, 4, -1, 1), (19, 19, -1, -1)):
        c[y, x] = 1; c[y + dy, x] = 1; c[y, x + dx] = 1
        c[y - dy, x - dx] = 6; c[y - 2*dy, x - 2*dx] = 8
    c[0, 10:14] = 1; c[1, 10:14] = 6; c[2, 10:14] = 7
    c[23, 10:14] = 1; c[22, 10:14] = 7; c[21, 10:14] = 7
    return _spr(c, pal)

# ---- BREAKFAST RUN props (about 0.56 texel per pixel) -------------------------------------------
def prop_sugar_cube():
    m = Model(dict(
        sugar=mat([(255, 255, 255), (232, 238, 252), (188, 202, 236), (142, 158, 206)]),
        grain=mat([(206, 220, 250), (168, 186, 232)], w=3),
        spark=mat([(255, 255, 210), (255, 240, 140)], glow=True, w=6),
    ))
    m.box(-4.5, 4.5, -4.5, 4.5, 0, 8, "sugar", tx=0.3, ty=0.3)   # modelled at 9 units, baked at 0.75x
    for (x, y) in ((-2.5, -1.5), (1.5, 2.2), (2.6, -2.8), (-1.0, 3.0)): m.box(x, x + 0.7, y, y + 0.7, 8, 8.15, "grain")
    for (y, z) in ((-2.6, 5.6), (1.8, 2.4), (-0.6, 3.6), (3.0, 6.2)): m.box(4.5, 4.62, y, y + 0.7, z, z + 0.7, "grain")
    for (x, z) in ((-2.6, 2.6), (1.4, 5.4), (2.8, 1.8)): m.box(x, x + 0.7, -4.62, -4.5, z, z + 0.7, "grain")
    s = render_prop3d(m, 1.79*0.75, math.radians(150), ss=4)
    pal, base = m.palette(); k = base[m.names.index("spark")]
    px = s["px"]; cx, cy = s["w"] - 3, 2
    for (dx, dy) in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (0, -2), (0, 2), (2, 0), (-2, 0)):
        if 0 <= cx + dx < s["w"] and 0 <= cy + dy < s["h"]: px[cy + dy][cx + dx] = k if abs(dx) + abs(dy) < 2 else k + 1
    return s

def shaker(kind):
    fill = [(255, 255, 255, 255), (214, 222, 240, 255)] if kind == "S" else [(120, 96, 84, 255), (70, 54, 54, 255)]
    lab = (226, 48, 56, 255) if kind == "S" else (44, 40, 60, 255)
    pal = [T, INK, (226, 244, 252, 255), (160, 204, 232, 255), (104, 150, 200, 255), fill[0], fill[1],
           (252, 252, 255, 255), (186, 194, 214, 255), (116, 122, 150, 255), lab, (255, 255, 255, 255)]
    def f(d):
        d.polygon([(3, 9), (10, 9), (12, 27), (1, 27)], fill=3)                 # glass body
        d.polygon([(3, 13), (10, 13), (11, 26), (2, 26)], fill=5)               # contents
        for (x, y) in ((4, 15), (8, 14), (6, 24), (9, 22), (3, 22), (9, 17)): d.point([(x, y)], fill=6)
        d.line([(3, 10), (2, 26)], fill=2); d.line([(4, 10), (4, 12)], fill=2)
        d.line([(10, 10), (11, 26)], fill=4)
        d.rectangle([1, 27, 12, 28], fill=4); d.line([(2, 27), (6, 27)], fill=3)
        d.rectangle([4, 16, 9, 23], fill=10)                                    # label with letter
        g = ["111", "100", "111", "001", "111"] if kind == "S" else ["111", "101", "111", "100", "100"]
        for y, r in enumerate(g):
            for x, b in enumerate(r):
                if b == "1": d.point([(5 + x + (1 if False else 0), 17 + y)], fill=11)
        d.ellipse([2, 1, 11, 10], fill=8)                                       # chrome cap
        d.rectangle([2, 6, 11, 9], fill=8); d.line([(2, 9), (11, 9)], fill=9); d.line([(2, 6), (11, 6)], fill=9)
        d.line([(4, 2), (6, 2)], fill=7); d.point([(3, 3), (3, 4), (3, 7), (3, 8), (4, 7)], fill=7)
        d.point([(10, 4), (10, 7), (10, 8), (9, 8)], fill=9)
        holes = [(6, 3), (8, 4), (6, 5)] if kind == "S" else [(5, 4), (7, 3), (9, 4), (7, 5)]
        d.point(holes, fill=1)
    return outline(indexed(14, 30, pal, f), 1)

def waffle(frame):
    """Toaster waffle tumbling through the air: 0 = face-on, 1 = tilted nearly flat."""
    pal = [T, INK, (255, 232, 140, 255), (246, 186, 78, 255), (204, 132, 48, 255), (150, 90, 36, 255)]
    def f(d):
        if frame == 0:
            d.rounded_rectangle([2, 1, 15, 14], radius=2, fill=3)
            d.line([(4, 1), (13, 1)], fill=2); d.line([(2, 3), (2, 12)], fill=2)
            d.line([(4, 14), (13, 14)], fill=4); d.line([(15, 3), (15, 12)], fill=4)
            for i in range(3):
                for j in range(3):
                    x = 4 + i*4; y = 3 + j*4
                    d.rectangle([x, y, x + 2, y + 2], fill=4)
                    d.line([(x, y), (x + 2, y)], fill=5); d.point([(x, y + 1), (x, y + 2)], fill=5)
        else:
            top = [(1, 9), (11, 3), (16, 6), (6, 12)]
            d.polygon([(1, 9), (6, 12), (16, 6), (16, 8), (6, 14), (1, 11)], fill=4)     # edge thickness
            d.line([(1, 11), (6, 14)], fill=5); d.line([(6, 14), (16, 8)], fill=5)
            d.polygon(top, fill=3)
            d.line([(1, 9), (11, 3)], fill=2)
            for i in range(3):
                for j in range(3):
                    u = (i + 0.5)/3; v = (j + 0.5)/3
                    x = 1 + 10*u + 5*v; y = 9 - 6*u + 3*v
                    d.point([(int(x), int(y)), (int(x) + 1, int(y))], fill=5)
    return outline(indexed(18, 16, pal, f), 1)

LOOP_COLS = {
    "r": [(255, 150, 140), (240, 70, 70), (160, 30, 50)], "y": [(255, 246, 160), (250, 206, 60), (180, 130, 30)],
    "g": [(190, 250, 130), (100, 210, 70), (44, 136, 56)], "b": [(160, 210, 255), (80, 140, 250), (40, 70, 180)],
}

def cereal_loop(col):
    pal = [T, INK] + [c + (255,) for c in LOOP_COLS[col]]
    def f(d):
        d.ellipse([1, 1, 8, 6], fill=3)
        d.rectangle([4, 3, 5, 4], fill=1)
        d.point([(2, 2), (3, 1), (4, 1), (2, 3)], fill=2)
        d.point([(7, 5), (6, 6), (5, 6), (7, 4), (4, 6)], fill=4)
        d.point([(4, 5), (5, 5)], fill=2)
    return outline(indexed(10, 8, pal, f), 1)

def egg_cup():
    pal = [T, INK, (255, 250, 232, 255), (250, 226, 190, 255), (210, 170, 136, 255), (255, 255, 255, 255),
           (255, 214, 60, 255), (250, 160, 40, 255), (90, 140, 255, 255), (52, 90, 214, 255), (32, 54, 150, 255),
           (220, 230, 250, 255)]
    def f(d):
        d.ellipse([3, 2, 12, 16], fill=3)                                       # egg
        d.line([(4, 6), (4, 11)], fill=2); d.point([(5, 4), (5, 5)], fill=2)
        d.line([(11, 6), (11, 12)], fill=4); d.point([(10, 13)], fill=4)
        d.ellipse([4, 1, 11, 6], fill=5); d.ellipse([4, 1, 11, 6], outline=1)   # top sliced off: white + yolk
        d.ellipse([6, 2, 9, 5], fill=6); d.point([(8, 4), (9, 4), (8, 5)], fill=7); d.point([(6, 3)], fill=5)
        d.polygon([(2, 12), (13, 12), (11, 19), (4, 19)], fill=8)               # cup
        d.line([(2, 12), (13, 12)], fill=11); d.line([(3, 15), (12, 15)], fill=5); d.line([(3, 16), (12, 16)], fill=5)
        d.line([(12, 13), (11, 18)], fill=9); d.line([(4, 19), (11, 19)], fill=10)
        d.rectangle([6, 20, 9, 21], fill=9); d.point([(6, 20), (6, 21)], fill=8)
        d.ellipse([3, 21, 12, 24], fill=8); d.line([(5, 24), (10, 24)], fill=10); d.line([(5, 22), (8, 22)], fill=11)
    return outline(indexed(16, 26, pal, f), 1)

def cup_random():
    """k_cup3: the '?' random cup, same disc style as the Turbo Cup."""
    W = Hh = 24
    pal = [T, INK, (170, 250, 150, 255), (80, 210, 96, 255), (36, 150, 84, 255), (20, 92, 70, 255),
           (255, 150, 220, 255), (236, 70, 160, 255), (255, 255, 255, 255), (255, 240, 130, 255), (250, 180, 50, 255)]
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    Dm = _disc(S, 11.5, 11.5, 11.2)
    c[_dil(Dm, 1)] = 1
    c[Dm] = 7
    c[Dm & (np.mgrid[0:Hh, 0:W][0] < 9)] = 6
    c[_disc(S, 11.5, 11.5, 9.4)] = 1
    _emboss(c, _disc(S, 11.5, 11.5, 8.6), [2, 3, 3, 4, 5], ow=0)
    q = [".#######.", "#########", "###...###", "......###", ".....###.", "...####..", "...###...", "...###...", ".........",
         "...###...", "...###..."]
    Q = _place(S, np.array([[ch == "#" for ch in r] for r in q]), 7, 6)
    c[_dil(Q, 1)] = 1
    _emboss(c, Q, [8, 9, 9, 10], ow=0)
    return _spr(c, pal)

def table_props():
    w0, w1 = waffle(0), waffle(1)
    p = {"sugar_cube": prop_sugar_cube(), "salt": shaker("S"), "pepper": shaker("P"), "waffle0": w0, "waffle1": w1}
    for k in "rygb": p["cereal_loop_" + k] = cereal_loop(k)
    p["egg_cup"] = egg_cup(); p["cup3"] = cup_star()
    return p

# ================================================================================================
# SOLAR SYSTEM: planets are painted (not modelled): shaded, dithered spheres, one view each
# ================================================================================================
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0

def _vnoise(P, seed=0):
    """Value noise on an (..., 3) array of points, 0..1."""
    P = np.asarray(P, float); ip = np.floor(P).astype(np.int64); f = P - ip; f = f*f*(3 - 2*f)
    def h(ix, iy, iz):
        v = (ix*374761393 + iy*668265263 + iz*1274126177 + seed*1013904223) & 0xffffffff
        v = ((v ^ (v >> 13))*1274126177) & 0xffffffff
        return ((v ^ (v >> 16)) & 0xffff) / 65535.0
    x, y, z = ip[..., 0], ip[..., 1], ip[..., 2]; fx, fy, fz = f[..., 0], f[..., 1], f[..., 2]
    def lerp(a, b, t): return a + (b - a)*t
    return lerp(lerp(lerp(h(x, y, z), h(x+1, y, z), fx), lerp(h(x, y+1, z), h(x+1, y+1, z), fx), fy),
                lerp(lerp(h(x, y, z+1), h(x+1, y, z+1), fx), lerp(h(x, y+1, z+1), h(x+1, y+1, z+1), fx), fy), fz)

def _fbm(P, octaves=4, seed=0):
    P = np.asarray(P, float); tot = 0.0; amp = 1.0; s = 0.0
    for k in range(octaves):
        tot = tot + amp*_vnoise(P*(2**k), seed + k*17); s += amp; amp *= 0.5
    return tot/s

def _globe(D, tilt=0.0, spin=0.0, pad=1):
    W = D + 2*pad; c = (W - 1)/2.0; R = D/2.0
    yy, xx = np.mgrid[0:W, 0:W]
    sx = (xx - c)/R; sy = -(yy - c)/R; r2 = sx*sx + sy*sy
    mask = r2 <= 1.0; sz = np.sqrt(np.clip(1 - r2, 0, 1))
    t = math.radians(tilt)
    bx = sx*math.cos(t) + sy*math.sin(t); by = -sx*math.sin(t) + sy*math.cos(t)
    lat = np.arcsin(np.clip(by, -1, 1)); lon = np.arctan2(bx, sz) + spin
    P = np.stack([np.cos(lat)*np.sin(lon), by, np.cos(lat)*np.cos(lon)], -1)
    L = np.array([-0.55, 0.5, 0.67]); L /= np.linalg.norm(L)
    lam = np.clip(sx*L[0] + sy*L[1] + sz*L[2], 0, 1)
    return dict(W=W, c=c, R=R, mask=mask, lat=lat, lon=lon, P=P, lam=lam, sz=sz, r=np.sqrt(r2), xx=xx, yy=yy)

def _ramps(ramps):
    pal = [T, INK]; bases = []
    for r in ramps:
        bases.append(len(pal)); pal += [tuple(c) + (255,) for c in r]
    assert len(pal) <= 32, len(pal)
    return pal, bases

def _paint(c, mask, rid, b, ramps, bases, yy, xx, dith=0.9):
    """rid: ramp id per pixel, b: brightness 0..1 -> ordered-dithered ramp index."""
    th = BAYER[yy % 4, xx % 4] - 0.5
    for i, r in enumerate(ramps):
        K = len(r)
        lv = np.clip(np.floor((1 - np.clip(b, 0, 1))*K + th*dith), 0, K - 1).astype(int)
        sel = mask & (rid == i)
        c[sel] = (bases[i] + lv)[sel]

def _body(name, c, pal, wpp, ax=None, ay=None):
    Hh, W = c.shape
    return {"name": name, "w": W, "h": Hh, "angles": 1, "ax": W//2 if ax is None else ax, "ay": Hh//2 if ay is None else ay,
            "wpp": wpp, "imgs": [c], "pal": pal}

def _lit(g, gamma=0.8, amb=0.10): return amb + (1 - amb)*g["lam"]**gamma

def sp_sun():
    D = 150; pad = 15
    g = _globe(D, pad=pad); W = g["W"]
    ramps = [[(255, 255, 230), (255, 244, 150), (255, 214, 70), (255, 170, 40), (246, 120, 28), (214, 72, 24), (160, 36, 28)]]
    pal, bases = _ramps(ramps)
    c = np.zeros((W, W), np.uint8)
    gran = _fbm(g["P"]*9.0, 3, 5); big = _fbm(g["P"]*2.5, 3, 9)
    b = 0.30 + 0.62*np.sqrt(g["sz"]) + 0.34*(gran - 0.5) + 0.22*(big - 0.5)
    spots = _fbm(g["P"]*3.0 + 7.0, 2, 3) > 0.78
    b = np.where(spots, b - 0.38, b)
    _paint(c, g["mask"], np.zeros((W, W), int), b, ramps, bases, g["yy"], g["xx"])
    # corona: flame tongues leaping off the limb
    ang = np.arctan2(g["yy"] - g["c"], g["xx"] - g["c"])
    circ = np.stack([np.cos(ang)*6, np.sin(ang)*6, np.zeros_like(ang)], -1)
    hgt = 2.0 + 12.0*np.clip(_fbm(circ, 3, 21)*1.5 - 0.35, 0, 1)**1.5
    d = (g["r"] - 1.0)*g["R"]
    fl = (~g["mask"]) & (d < hgt)
    fb = 0.42 - 0.36*(d/np.maximum(hgt, 1e-3))
    _paint(c, fl, np.zeros((W, W), int), fb, ramps, bases, g["yy"], g["xx"])
    return _body("sun", c, pal, 2.4)

def sp_jupiter():
    g = _globe(110, tilt=4)
    ramps = [[(255, 244, 220), (240, 220, 186), (206, 180, 150), (150, 124, 112), (92, 72, 80)],          # zones
             [(236, 170, 110), (206, 128, 80), (160, 90, 60), (110, 58, 50), (68, 36, 44)],             # belts
             [(255, 130, 90), (220, 80, 60), (160, 46, 48)]]                                            # great red spot
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    turb = _fbm(np.stack([g["P"][..., 0]*5, g["P"][..., 1]*14, g["P"][..., 2]*5], -1), 3, 2)
    la = g["lat"] + 0.09*(turb - 0.5)
    band = np.sin(la*11.0) + 0.5*np.sin(la*23.0 + 1.0)
    rid = (band > 0.15).astype(int)
    rid[np.abs(la) > 1.15] = 1
    dl = (g["lon"] - 0.35); dla = (g["lat"] + 0.38)
    spot = (dl/0.36)**2 + (dla/0.16)**2
    rid[spot < 1.0] = 2
    b = _lit(g)*(0.9 + 0.25*(turb - 0.5)); b = np.where((spot < 1.0) & (spot > 0.55), b*0.85, b)
    _paint(c, g["mask"], rid, b, ramps, bases, g["yy"], g["xx"])
    return _body("jupiter", c, pal, 2.0)

def sp_saturn():
    Rp = 38.0; W, Hh = 176, 84; cx, cy = (W - 1)/2.0, (Hh - 1)/2.0
    ramps = [[(255, 240, 190), (240, 210, 150), (206, 168, 116), (150, 116, 90), (92, 70, 70)],           # globe
             [(226, 196, 130), (196, 156, 100), (150, 112, 80), (104, 76, 66), (66, 48, 54)],           # darker bands
             [(255, 246, 214), (232, 212, 170), (190, 164, 130), (140, 116, 100)]]                      # rings
    pal, bases = _ramps(ramps); c = np.zeros((Hh, W), np.uint8)
    yy, xx = np.mgrid[0:Hh, 0:W]
    dx = xx - cx; dy = -(yy - cy)
    roll = math.radians(14); k = 0.36
    u = dx*math.cos(roll) + dy*math.sin(roll); v = -dx*math.sin(roll) + dy*math.cos(roll)
    rho = np.sqrt(u*u + (v/k)**2)/Rp
    ring = ((rho > 1.28) & (rho < 2.27)) & ~((rho > 1.94) & (rho < 2.03))
    ring &= ~((rho < 1.5) & (BAYER[yy % 4, xx % 4] > 0.45))                       # gauzy C ring
    rb = np.where(rho < 1.5, 0.35, np.where(rho < 1.94, 0.80 + 0.2*np.sin(rho*40), 0.55 + 0.15*np.sin(rho*60)))
    # globe
    sx = u/Rp; sy = v/Rp; r2 = sx*sx + sy*sy; disc = r2 <= 1.0
    sz = np.sqrt(np.clip(1 - r2, 0, 1))
    L = np.array([-0.55, 0.5, 0.67]); L /= np.linalg.norm(L)
    wx = dx/Rp; wy = dy/Rp
    lam = np.clip(wx*L[0] + wy*L[1] + sz*L[2], 0, 1)
    lat = np.arcsin(np.clip(sy*math.cos(math.asin(k)) + sz*k, -1, 1))            # ring-plane latitude
    band = np.sin(lat*9.0 + 0.4)
    rid = (band > 0.3).astype(int)
    gb = 0.10 + 0.90*lam**0.8
    shadow = disc & (np.abs(np.sqrt(u*u + ((v + 5.0)/k)**2)/Rp - 1.7) < 0.22) & (v > -Rp*0.2)   # ring shadow on the globe
    gb = np.where(shadow, gb*0.55, gb)
    back = ring & (v > 0)
    dark = back & (np.abs(u) < Rp*0.92)                                         # globe's shadow across the far ring
    _paint(c, back, np.full(c.shape, 2), np.where(dark, rb*0.35, rb), ramps, bases, yy, xx, dith=0.6)
    _paint(c, disc, rid, gb, ramps, bases, yy, xx)
    _paint(c, ring & (v <= 0), np.full(c.shape, 2), rb, ramps, bases, yy, xx, dith=0.6)
    return _body("saturn", c, pal, 2.0)

def sp_earth():
    g = _globe(64, tilt=18, spin=0.6)
    ramps = [[(110, 190, 255), (56, 130, 240), (36, 84, 196), (26, 50, 136), (18, 28, 80)],               # ocean
             [(140, 220, 110), (80, 170, 80), (46, 116, 70), (30, 70, 60)],                              # land
             [(240, 214, 150), (196, 160, 104), (136, 104, 80)],                                        # desert
             [(255, 255, 255), (220, 232, 250), (160, 180, 224)]]                                       # cloud / ice
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    P = g["P"]
    land = _fbm(P*1.7 + 3.0, 4, 11) > 0.53
    dry = _fbm(P*3.0, 2, 4) > 0.56
    rid = np.zeros((W, W), int); rid[land] = 1; rid[land & dry & (np.abs(g["lat"]) < 0.6)] = 2
    cloud = _fbm(np.stack([P[..., 0]*2.4, P[..., 1]*5.0, P[..., 2]*2.4], -1) + 9.0, 4, 31) > 0.60
    rid[cloud | (np.abs(g["lat"]) > 1.22)] = 3
    _paint(c, g["mask"], rid, _lit(g, 0.85, 0.06), ramps, bases, g["yy"], g["xx"])
    return _body("earth", c, pal, 1.6)

def sp_moon():
    g = _globe(28, spin=0.3)
    ramps = [[(244, 244, 250), (200, 202, 216), (150, 152, 172), (100, 102, 128), (60, 60, 86)],
             [(170, 172, 190), (124, 126, 148), (84, 86, 112), (54, 54, 80)]]
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    P = g["P"]
    rid = (_fbm(P*1.6 + 2.0, 3, 8) > 0.55).astype(int)                         # maria
    b = _lit(g, 0.8, 0.08)
    for (cx, cy, cz, r) in ((0.3, 0.5, 0.8, 0.20), (-0.5, -0.2, 0.84, 0.16), (0.1, -0.6, 0.79, 0.22), (0.62, -0.1, 0.78, 0.13),
                            (-0.2, 0.3, 0.93, 0.11), (-0.6, 0.55, 0.58, 0.15)):
        d = np.sqrt((P[..., 0] - cx)**2 + (P[..., 1] - cy)**2 + (P[..., 2] - cz)**2)
        b = np.where(d < r*0.7, b*0.62, np.where(d < r, b*1.35 + 0.08, b))
    _paint(c, g["mask"], rid, b, ramps, bases, g["yy"], g["xx"], dith=0.7)
    return _body("moon", c, pal, 1.2)

def sp_mars():
    g = _globe(50, tilt=-20, spin=1.0)
    ramps = [[(255, 180, 120), (236, 128, 76), (196, 86, 52), (140, 54, 44), (84, 32, 40)],
             [(170, 96, 70), (124, 62, 52), (84, 40, 44), (52, 26, 36)],
             [(255, 255, 255), (226, 232, 246), (170, 182, 214)]]
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    P = g["P"]
    rid = (_fbm(P*2.0 + 5.0, 4, 14) > 0.56).astype(int)
    cap = g["lat"] + 0.10*(_fbm(P*6.0, 2, 3) - 0.5)
    rid[cap > 1.12] = 2; rid[cap < -1.32] = 2
    _paint(c, g["mask"], rid, _lit(g, 0.8, 0.08)*(0.92 + 0.2*(_fbm(P*5.0, 2, 6) - 0.5)), ramps, bases, g["yy"], g["xx"])
    return _body("mars", c, pal, 1.6)

def sp_neptune():
    g = _globe(70, tilt=22)
    ramps = [[(150, 210, 255), (84, 150, 250), (48, 96, 220), (32, 58, 164), (22, 32, 104)],
             [(40, 60, 150), (28, 38, 108), (18, 24, 70)],
             [(255, 255, 255), (206, 230, 255)]]
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    P = g["P"]
    turb = _fbm(np.stack([P[..., 0]*3, P[..., 1]*10, P[..., 2]*3], -1), 3, 5)
    la = g["lat"] + 0.08*(turb - 0.5)
    rid = np.zeros((W, W), int)
    spot = ((g["lon"] + 0.25)/0.34)**2 + ((g["lat"] + 0.30)/0.17)**2
    rid[spot < 1.0] = 1
    wisp = (np.abs(la + 0.52 + 0.05*np.sin(g["lon"]*5)) < 0.035) & (np.abs(g["lon"] + 0.2) < 0.6)
    wisp |= (np.abs(la - 0.45) < 0.03) & (np.abs(g["lon"] - 0.3) < 0.45)
    rid[wisp] = 2
    b = _lit(g, 0.8, 0.08)*(0.93 + 0.16*np.sin(la*9.0))
    _paint(c, g["mask"], rid, b, ramps, bases, g["yy"], g["xx"])
    return _body("neptune", c, pal, 1.8)

def sp_venus():
    g = _globe(56, tilt=-8)
    ramps = [[(255, 252, 214), (250, 232, 160), (226, 196, 116), (170, 136, 90), (104, 80, 72)],
             [(250, 214, 130), (224, 170, 84), (176, 122, 64), (120, 80, 56), (74, 50, 50)]]
    pal, bases = _ramps(ramps); W = g["W"]; c = np.zeros((W, W), np.uint8)
    P = g["P"]
    n = _fbm(P*2.2 + 1.0, 3, 19)
    sw = np.sin(g["lat"]*7.0 - np.abs(g["lon"])*2.2 + n*5.0)                     # chevron cloud swirls
    rid = (sw > 0.25).astype(int)
    _paint(c, g["mask"], rid, _lit(g, 0.8, 0.10)*(0.95 + 0.1*sw), ramps, bases, g["yy"], g["xx"])
    return _body("venus", c, pal, 1.6)

PAINTED_SETS = [sp_sun, sp_jupiter, sp_saturn, sp_earth, sp_moon, sp_mars, sp_neptune, sp_venus]

def sp_asteroid(frame):
    m = Model(dict(
        rock=mat([(196, 180, 164), (150, 134, 124), (104, 92, 92), (66, 58, 66), (40, 36, 46)]),
        rock2=mat([(170, 150, 130), (126, 108, 98), (86, 72, 74), (54, 46, 56)]),
        crater=mat([(84, 72, 76), (54, 46, 56), (34, 30, 40)], w=1.3),
        rim=mat([(220, 206, 190), (170, 154, 142)], w=1.2),
    ))
    A = (16.0, 13.0, 12.5)
    def surf(d):
        d = np.array(d, float); d /= np.linalg.norm(d)
        return d, 1.0/math.sqrt((d[0]/A[0])**2 + (d[1]/A[1])**2 + (d[2]/A[2])**2)
    with m.at(ry=frame*45.0, rz=12):
        m.ellip((0, 0, 0), A, "rock", rows=7, cols=11)
        for half in (0, 1):                                   # 2-fold about the tumble axis: 4 frames loop
            with m.at(ry=180*half):
                for (d, r, mm) in (((1, 0.5, 0.4), 6.5, "rock2"), ((0.3, -0.8, 0.9), 6.0, "rock"), ((0.9, -0.3, -0.5), 5.5, "rock"),
                                   ((0.2, 0.9, -0.6), 6.0, "rock2"), ((0.6, 1.0, 0.7), 4.5, "rock"), ((-0.1, -1.0, -0.2), 5.0, "rock2")):
                    dd, rs = surf(d)
                    m.ellip(tuple(dd*(rs - r*0.35)), (r, r*0.85, r*0.8), mm, rows=4, cols=7)
                for (d, r) in (((0.5, 0.2, 1.0), 3.6), ((1.0, -0.6, 0.1), 2.8), ((0.1, 1.0, 0.3), 3.0), ((0.8, 0.5, -0.9), 2.4)):
                    dd, rs = surf(d)
                    m.cyl(tuple(dd*(rs - 0.4)), tuple(dd*(rs + 0.5)), r, r*0.9, "rim", n=8, cap1="crater", ring="rim", inner=0.72)
    return m

def sp_comet():
    m = Model(dict(
        ice=mat([(255, 255, 255), (214, 236, 255), (160, 200, 244), (110, 150, 214)]),
        g0=mat([(255, 255, 255)], glow=True), g1=mat([(190, 244, 255)], glow=True), g2=mat([(110, 200, 255)], glow=True),
        g3=mat([(64, 130, 240)], glow=True), g4=mat([(40, 70, 180)], glow=True),
    ))
    st = [(-2, 6.4, "g0"), (-10, 7.0, "g1"), (-22, 6.4, "g1"), (-36, 5.4, "g2"), (-52, 4.0, "g3"), (-68, 2.6, "g4"), (-86, 0.0, None)]
    for (x0, r0, mm), (x1, r1, _) in zip(st, st[1:]):
        m.cyl((x0, 0, 0), (x1, 0, 0), r0, r1, mm, n=10)
    for (y, z, ln) in ((6, 1, 64), (-6, -1, 58), (0, 5, 50), (2, -5, 44)):                 # streamers
        m.cyl((-4, y*0.7, z*0.7), (-ln*0.5, y*1.8, z*1.8), 2.2, 1.6, "g2", n=6)
        m.cyl((-ln*0.5, y*1.8, z*1.8), (-ln, y*2.8, z*2.8), 1.6, 0.0, "g3", n=6)
    m.ellip((0, 0, 0), (7.6, 7.0, 6.8), "ice", rows=6, cols=10)
    for (p, r) in (((3, 4, 3), 3.4), ((4, -3, -2), 3.0), ((1, -4, 4), 2.8), ((5, 1, -4), 2.6)):
        m.ellip(p, r, "ice", rows=4, cols=6)
    m.ellip((6.6, 0, 1), (2.4, 4.0, 3.4), "g0", rows=4, cols=6)                           # glowing nose
    return m

def sp_satellite():
    m = Model(dict(
        foil=mat([(255, 226, 120), (232, 170, 60), (176, 112, 40), (116, 70, 34)]),
        white=mat([(255, 255, 255), (214, 222, 240), (160, 172, 206), (112, 122, 164)]),
        panel=mat([(90, 130, 255), (48, 76, 210), (30, 44, 140)]),
        cell=mat([(170, 210, 255), (110, 150, 240)], w=1.3),
        dark=mat([(96, 96, 120), (60, 60, 82), (36, 36, 54)], w=1.5),
        red=mat([(255, 90, 80), (220, 40, 50)], glow=True, w=3),
    ))
    m.box(-5, 5, -4, 4, -4, 4, "foil")
    m.box(-5.3, -3.6, -4.3, 4.3, -4.3, 4.3, "dark"); m.box(3.2, 3.8, -4.2, 4.2, -4.2, 4.2, "white")
    m.cyl((5, 0, 0), (7, 0, 0), 1.2, m="dark", n=6)
    m.cyl((7, 0, 0), (9.6, 0, 0), 2.0, 7.2, "white", n=12, cap1="cell")                   # high-gain dish
    m.cyl((9.6, 0, 0), (13.5, 0, 0), 0.45, m="dark", n=4); m.ellip((13.8, 0, 0), 0.9, "dark", rows=3, cols=5)
    for s in (1, -1):
        m.cyl((0, 4*s, 0), (0, 6.2*s, 0), 0.6, m="dark", n=5)
        y0, y1 = (6.2, 15.6) if s > 0 else (-15.6, -6.2)
        m.box(-3.6, 3.6, y0, y1, -0.3, 0.3, "panel")
        for z0, z1 in ((0.3, 0.42), (-0.42, -0.3)):
            m.box(-0.2, 0.2, y0, y1, z0, z1, "cell")
            for k in (1, 2):
                yy = y0 + (y1 - y0)*k/3
                m.box(-3.6, 3.6, yy - 0.2, yy + 0.2, z0, z1, "cell")
    m.cyl((-5, 0, -2), (-12, 0, -5), 0.5, m="dark", n=4)                                   # RTG boom
    m.cyl((-11, 0, -4.6), (-14.5, 0, -6.1), 1.4, m="dark", n=6, cap1="red")
    m.cyl((0, 0, 4), (-2.5, 0, 11.5), 0.35, m="white", n=4)                                # whip antenna
    m.ellip((-2.6, 0, 11.8), 0.8, "red", rows=3, cols=5)
    return m

# ================================================================================================
# props for SEWER SURF, the home-computer board, SOLAR SYSTEM and HAUNTED MANSION
# ================================================================================================
def _pil(w, h, pal, f, ol=1):
    s = indexed(w, h, pal, f)
    return outline(s, ol) if ol else s

def _chars(rows, key, pal, ol=1):
    px = [[key.get(ch, 0) for ch in r] for r in rows]
    w = len(rows[0]); assert all(len(r) == w for r in rows), [len(r) for r in rows]
    px = [[0]*(w + 2)] + [[0] + r + [0] for r in px] + [[0]*(w + 2)]
    s = {"w": w + 2, "h": len(rows) + 2, "px": px, "pal": pal}
    return outline(s, ol) if ol else s

# ---- sewer --------------------------------------------------------------------------------------
def prop_toxic_barrel():
    m = Model(dict(
        drum=mat([(250, 220, 70), (214, 170, 40), (160, 116, 32), (104, 72, 30)]),
        band=mat([(70, 66, 80), (44, 40, 54), (28, 26, 38)], w=1.5),
        rust=mat([(170, 96, 56), (120, 62, 42)], w=1.2),
        ooze=mat([(190, 255, 110), (90, 230, 60)], glow=True, w=2),
        ooze2=mat([(90, 230, 60), (40, 170, 50)], glow=True, w=1.5),
    ))
    m.ellip((2.5, -1.5, 0.25), (7.5, 6.5, 0.5), "ooze2", rows=3, cols=10)        # glowing puddle
    m.ellip((3.5, -2.5, 0.4), (4.5, 3.5, 0.5), "ooze", rows=3, cols=8)
    with m.at(rx=-5, ry=4):
        m.cyl((0, 0, 0), (0, 0, 11.5), 4.7, m="drum", n=12, cap1="ooze")
        for z in (0.2, 5.3, 10.6): m.cyl((0, 0, z), (0, 0, z + 0.9), 5.0, m="band", n=12)
        m.box(4.4, 4.9, -2.0, 2.0, 6.6, 9.8, "band")                             # hazard plate
        m.box(4.9, 5.05, -1.2, 1.2, 7.3, 9.2, "ooze")
        m.box(3.6, 4.9, -3.6, -2.4, 1.4, 4.8, "rust")                            # dent
        m.cyl((3.0, -3.0, 11.7), (4.4, -3.6, 11.0), 1.3, m="ooze", n=6)          # leak over the rim
        m.cyl((4.4, -3.6, 11.2), (4.9, -3.7, 3.0), 1.0, 0.6, "ooze", n=5)
    return render_prop3d(m, 1.25, math.radians(140), ss=4)

def pipe(frame):
    pal = [T, INK, (176, 150, 140, 255), (126, 100, 100, 255), (84, 64, 74, 255), (40, 30, 44, 255),      # 2..5 rusty pipe
           (150, 230, 200, 255), (84, 180, 160, 255), (44, 124, 124, 255), (28, 80, 96, 255),             # 6..9 murky water
           (255, 255, 255, 255), (200, 236, 236, 255), (200, 110, 60, 255)]                               # 10,11 foam, 12 rust
    def f(d):
        d.ellipse([3, 1, 26, 17], fill=3)                                       # flange
        d.ellipse([3, 1, 26, 15], fill=2)
        d.ellipse([6, 3, 23, 15], fill=4); d.ellipse([7, 4, 22, 14], fill=5)    # bore
        for (x, y) in ((4, 8), (25, 8), (14, 1), (8, 3), (21, 3)): d.point([(x, y)], fill=4)   # bolts
        d.point([(5, 12), (6, 13), (24, 11), (10, 16), (19, 16)], fill=12)
        d.polygon([(9, 12), (20, 12), (22, 30), (7, 30)], fill=7)               # falling water
        d.ellipse([9, 10, 20, 14], fill=7)
        d.polygon([(9, 12), (11, 12), (9, 30), (7, 30)], fill=8); d.polygon([(19, 12), (20, 12), (22, 30), (20, 30)], fill=9)
        for k, x in enumerate((11, 14, 17)):
            y0 = 13 + ((k*5 + frame*4) % 8)
            d.line([(x, y0), (x + (k - 1)*0.4*(y0 - 12)/4, y0 + 5)], fill=6)
            y1 = y0 + 9
            if y1 < 28: d.line([(x + 1, y1), (x + 1, y1 + 2)], fill=10)
        d.ellipse([3, 27, 26, 33], fill=11)                                     # splash
        if frame == 0:
            d.ellipse([6, 26, 13, 31], fill=10); d.ellipse([15, 27, 23, 32], fill=10)
            d.point([(4, 24), (25, 23), (2, 28)], fill=10)
        else:
            d.ellipse([4, 27, 11, 32], fill=10); d.ellipse([13, 26, 21, 31], fill=10); d.ellipse([20, 28, 25, 32], fill=10)
            d.point([(6, 22), (23, 25), (27, 28), (13, 24)], fill=10)
        d.line([(8, 32), (21, 32)], fill=7)
    return _pil(30, 34, pal, f)

def rat():
    pal = [T, INK, (176, 150, 150, 255), (126, 104, 112, 255), (84, 68, 84, 255), (236, 214, 204, 255), (255, 150, 170, 255),
           (255, 255, 255, 255)]
    key = {"l": 2, "g": 3, "d": 4, "b": 5, "p": 6, "e": 1, "w": 7}
    rows = ["....pp......",
            "...pgll.....",
            "...glegg....",
            "...gggggp...",
            "....gbbgw...",
            "...ggbbbpp..",
            "..gggbbb....",
            "..ggggbbg...",
            "p.dgggggg...",
            ".pppdd.dpp.."]
    return _chars(rows, key, pal)

def rubber_duck():
    pal = [T, INK, (255, 250, 170, 255), (255, 220, 60, 255), (236, 160, 36, 255), (255, 130, 40, 255), (255, 255, 255, 255)]
    key = {"y": 2, "Y": 3, "o": 4, "O": 5, "e": 1, "w": 6}
    rows = [".....YYYY...",
            "....YyyYYY..",
            "....YyYeYYOO",
            "....YYYYYOOO",
            ".....YYYYo..",
            "Y...YYYYY...",
            "YY.YYyyYYYY.",
            "YYYYyyYYYYY.",
            "YYYYYYYYYYo.",
            "oYYYYYYYYoo.",
            ".ooYYYYooo..",
            "...ooooo...."]
    return _chars(rows, key, pal)

def sewer_lamp():
    pal = [T, INK, (150, 150, 170, 255), (100, 100, 124, 255), (64, 64, 86, 255), (255, 252, 210, 255), (255, 220, 110, 255),
           (250, 160, 50, 255), (200, 110, 60, 255)]
    def f(d):
        d.rectangle([5, 12, 6, 28], fill=3); d.line([(5, 12), (5, 28)], fill=2)
        d.rectangle([3, 27, 8, 28], fill=4); d.point([(5, 20), (6, 16)], fill=8)
        d.rectangle([3, 11, 8, 12], fill=4)
        d.rectangle([3, 1, 8, 2], fill=4); d.line([(5, 1), (6, 1)], fill=2)
        d.ellipse([2, 3, 9, 10], fill=7); d.ellipse([3, 3, 8, 9], fill=6); d.ellipse([4, 4, 7, 8], fill=5)
        for x in (2, 5, 9): d.line([(x, 3), (x, 10)], fill=4)                   # cage bars
        d.line([(2, 6), (9, 6)], fill=4)
    s = _pil(12, 30, pal, f)
    for (x, y) in ((0, 3), (11, 4), (0, 9), (11, 8), (1, 1), (10, 1)):          # warm glow flecks
        if s["px"][y][x] == 0: s["px"][y][x] = 7
    return s

def ladder():
    pal = [T, INK, (196, 130, 90, 255), (150, 90, 66, 255), (104, 60, 54, 255), (236, 150, 70, 255), (120, 120, 140, 255)]
    def f(d):
        for x in (1, 7):
            d.rectangle([x, 1, x + 1, 32], fill=3); d.line([(x, 1), (x, 32)], fill=2)
        for y in range(4, 32, 5):
            d.line([(3, y), (6, y)], fill=3); d.line([(3, y + 1), (6, y + 1)], fill=4)
        d.point([(1, 9), (8, 14), (2, 22), (7, 27), (4, 14), (5, 24)], fill=5)
        d.point([(1, 1), (8, 1), (1, 32), (8, 32)], fill=6)                     # wall bolts
    return _pil(10, 34, pal, f)

# ---- circuit board ------------------------------------------------------------------------------
def led(col, lit):
    base = {"red": [(255, 120, 110), (220, 40, 50), (140, 22, 40), (84, 16, 34)], "green": [(150, 255, 140), (50, 200, 70), (26, 120, 56), (16, 70, 44)]}[col]
    core = (255, 236, 220, 255) if col == "red" else (236, 255, 226, 255)
    pal = [T, INK] + [c + (255,) for c in base] + [core, (220, 222, 234, 255), (140, 144, 170, 255)]
    def f(d):
        d.line([(5, 15), (5, 22)], fill=7); d.line([(8, 15), (8, 20)], fill=7)  # legs (long one = anode)
        d.point([(5, 22), (8, 20)], fill=8)
        a, b, c2 = (2, 3, 4) if lit else (3, 4, 5)
        d.ellipse([3, 2, 10, 10], fill=b); d.rectangle([3, 6, 10, 13], fill=b)
        d.rectangle([9, 5, 10, 13], fill=c2)
        d.rectangle([2, 13, 11, 14], fill=c2); d.line([(2, 13), (8, 13)], fill=b)
        d.line([(4, 5), (4, 11)], fill=a); d.point([(5, 3)], fill=a)
        if lit:
            d.ellipse([5, 4, 8, 9], fill=6); d.rectangle([6, 9, 7, 11], fill=2)
        else:
            d.rectangle([6, 8, 7, 11], fill=c2)                                 # the die on its post
    s = _pil(14, 24, pal, f)
    if lit:
        for (x, y) in ((0, 3), (13, 3), (0, 8), (13, 8), (2, 0), (11, 0), (6, 0), (7, 0), (1, 5), (12, 5)):
            if s["px"][y][x] == 0: s["px"][y][x] = 2
    return s

def jumper():
    pal = [T, INK, (110, 160, 255, 255), (56, 100, 230, 255), (34, 60, 160, 255), (70, 70, 90, 255), (40, 40, 56, 255),
           (240, 220, 130, 255), (180, 150, 70, 255)]
    def f(d):
        d.rectangle([1, 2, 10, 10], fill=3)
        d.line([(1, 2), (10, 2)], fill=2); d.line([(1, 2), (1, 10)], fill=2); d.rectangle([9, 3, 10, 10], fill=4)
        d.rectangle([3, 3, 4, 4], fill=4); d.rectangle([7, 3, 8, 4], fill=4)   # test holes
        d.line([(3, 7), (8, 7)], fill=2)
        d.rectangle([1, 11, 10, 12], fill=5); d.line([(1, 12), (10, 12)], fill=6)
        for x in (3, 8): d.rectangle([x, 13, x, 14], fill=7); d.point([(x, 14)], fill=8)
    return _pil(12, 16, pal, f)

def trimpot():
    pal = [T, INK, (120, 170, 255, 255), (60, 110, 236, 255), (36, 66, 170, 255), (255, 232, 130, 255), (210, 160, 60, 255),
           (130, 90, 40, 255), (240, 240, 250, 255), (150, 154, 180, 255)]
    def f(d):
        d.rectangle([1, 4, 13, 12], fill=3)
        d.rectangle([1, 4, 13, 5], fill=2); d.rectangle([12, 6, 13, 12], fill=4); d.line([(1, 12), (13, 12)], fill=4)
        d.rectangle([9, 1, 13, 4], fill=6); d.line([(9, 1), (13, 1)], fill=5); d.line([(9, 3), (13, 3)], fill=7)   # brass screw
        d.point([(9, 2), (13, 4)], fill=5)
        for x in (3, 5, 7): d.point([(x, 8)], fill=8); d.point([(x, 9)], fill=8)
        d.point([(5, 8)], fill=3)
        for x in (3, 7, 11): d.line([(x, 13), (x, 14)], fill=9)
    return _pil(16, 16, pal, f)

def spark(frame):
    pal = [T, (40, 70, 200, 255), (110, 220, 255, 255), (220, 250, 255, 255), (255, 255, 255, 255)]
    arms = [
        [[(8, 8), (5, 6), (6, 3), (2, 1)], [(8, 8), (11, 6), (10, 3), (13, 1)], [(8, 8), (11, 10), (10, 12), (14, 14)], [(8, 8), (5, 10), (3, 9), (1, 13)]],
        [[(8, 8), (7, 4), (9, 3), (8, 1)], [(8, 8), (12, 8), (13, 6), (14, 7)], [(8, 8), (8, 11), (6, 13), (7, 14)], [(8, 8), (4, 8), (3, 10), (1, 8)],
         [(8, 8), (11, 11), (13, 11)]],
        [[(8, 8), (4, 5), (4, 3), (1, 3)], [(8, 8), (12, 5), (14, 4)], [(8, 8), (12, 11), (11, 13), (13, 14)], [(8, 8), (5, 11), (5, 13), (2, 14)],
         [(8, 8), (8, 4), (10, 2)]],
    ][frame]
    def f(d):
        for a in arms: d.line(a, fill=2)
        for a in arms: d.line(a[:2], fill=3)
        r = (2, 3, 2)[frame]
        d.ellipse([8 - r, 8 - r, 8 + r, 8 + r], fill=3); d.ellipse([7, 7, 9, 9], fill=4)
        for a in arms: d.point([a[-1]], fill=4)
    return _pil(16, 16, pal, f)

# ---- space --------------------------------------------------------------------------------------
def astronaut():
    pal = [T, INK, (255, 255, 255, 255), (214, 222, 240, 255), (160, 172, 208, 255), (255, 200, 80, 255), (230, 130, 40, 255),
           (60, 70, 120, 255), (230, 60, 60, 255), (80, 140, 255, 255), (120, 124, 150, 255)]
    def f(d):
        d.rectangle([3, 9, 5, 16], fill=10)                                     # backpack
        d.line([(12, 11), (15, 6)], fill=2, width=2); d.rectangle([14, 3, 16, 5], fill=10)      # waving arm + glove
        d.line([(6, 11), (3, 15)], fill=3, width=2); d.rectangle([2, 15, 3, 17], fill=10)
        d.rectangle([5, 9, 12, 16], fill=2); d.rectangle([11, 10, 12, 16], fill=3); d.line([(5, 16), (12, 16)], fill=4)
        d.rectangle([7, 11, 10, 13], fill=4); d.point([(7, 12)], fill=8); d.point([(9, 12)], fill=9)   # chest pack
        d.line([(7, 17), (6, 21)], fill=2, width=2); d.line([(10, 17), (12, 20)], fill=3, width=2)
        d.rectangle([5, 21, 7, 22], fill=10); d.rectangle([12, 20, 14, 21], fill=10)
        d.ellipse([4, 1, 13, 9], fill=2); d.ellipse([10, 3, 13, 8], fill=3)
        d.ellipse([6, 3, 12, 8], fill=6); d.ellipse([6, 3, 11, 7], fill=5); d.rectangle([9, 5, 11, 7], fill=7)   # gold visor
        d.point([(7, 4), (8, 4), (7, 5)], fill=2)
    return _pil(18, 24, pal, f)

def beacon(frame):
    pal = [T, INK, (220, 226, 240, 255), (156, 164, 196, 255), (100, 108, 146, 255), (255, 220, 60, 255), (60, 56, 76, 255),
           (255, 96, 80, 255), (150, 30, 44, 255), (255, 220, 200, 255), (80, 130, 250, 255)]
    def f(d):
        d.line([(5, 4), (5, 8)], fill=3); d.line([(6, 4), (6, 8)], fill=4)      # mast
        d.polygon([(5, 8), (6, 8), (9, 12), (6, 18), (5, 18), (2, 12)], fill=3)
        d.polygon([(5, 8), (2, 12), (5, 18)], fill=2); d.polygon([(6, 12), (9, 12), (6, 18)], fill=4)
        d.rectangle([2, 12, 9, 13], fill=5); d.point([(3, 12), (5, 12), (7, 12), (9, 12), (2, 13), (4, 13), (6, 13), (8, 13)], fill=6)
        d.line([(0, 15), (2, 14)], fill=10); d.line([(9, 14), (11, 15)], fill=10)   # little solar vanes
        if frame: d.ellipse([4, 1, 7, 4], fill=7); d.point([(5, 2)], fill=9)
        else: d.ellipse([4, 2, 7, 4], fill=8)
    s = _pil(12, 20, pal, f)
    if frame:
        for (x, y) in ((1, 2), (10, 2), (2, 0), (9, 0), (5, 0), (6, 0), (2, 5), (9, 5)):
            if s["px"][y][x] == 0: s["px"][y][x] = 7
    return s

def flare(frame):
    pal = [T, (150, 30, 30, 255), (255, 255, 230, 255), (255, 226, 90, 255), (255, 150, 40, 255), (232, 70, 30, 255)]
    spine = [[(9, 19), (8, 13), (10, 8), (13, 4), (15, 1)], [(9, 19), (10, 13), (8, 8), (6, 4), (7, 1)], [(9, 19), (9, 12), (11, 7), (10, 3), (12, 1)]][frame]
    def f(d):
        for (w, col) in ((6, 5), (4, 4), (2, 3)):
            for i, (a, b) in enumerate(zip(spine, spine[1:])):
                ww = max(1, w - i - (1 if i > 1 else 0))
                d.line([a, b], fill=col, width=ww)
                d.ellipse([a[0] - ww//2, a[1] - ww//2, a[0] + ww//2, a[1] + ww//2], fill=col)
        d.ellipse([3, 15, 15, 21], fill=5); d.ellipse([5, 16, 13, 21], fill=4); d.ellipse([7, 17, 11, 21], fill=3)
        d.line([spine[0], spine[1]], fill=2)
        x, y = spine[-1]; d.point([(x + (frame - 1)*2, y + 3), (x - 3 + frame, y + 6)], fill=4)   # shed embers
    return _pil(18, 20, pal, f)

# ---- haunted mansion ----------------------------------------------------------------------------
STONE_P = [(214, 216, 230, 255), (160, 164, 186, 255), (110, 114, 144, 255), (70, 72, 104, 255), (96, 150, 90, 255)]

def tomb(kind):
    pal = [T, INK] + STONE_P
    def f(d):
        if kind == 0:
            d.ellipse([1, 1, 12, 12], fill=3); d.rectangle([1, 7, 12, 16], fill=3)
            d.arc([1, 1, 12, 12], 180, 300, fill=2); d.line([(2, 6), (2, 15)], fill=2)
            d.rectangle([11, 6, 12, 16], fill=4); d.arc([1, 1, 12, 12], 300, 360, fill=4)
            d.line([(4, 7), (9, 7)], fill=5); d.line([(4, 9), (8, 9)], fill=5); d.line([(5, 11), (9, 11)], fill=4)   # worn epitaph
            d.line([(8, 1), (7, 4), (9, 6), (8, 9), (10, 12)], fill=5)                                                # crack
            d.rectangle([0, 16, 13, 17], fill=4); d.line([(0, 16), (9, 16)], fill=3)
            d.point([(1, 15), (3, 16), (12, 15), (0, 17)], fill=6)
        else:
            d.rectangle([5, 1, 8, 19], fill=3); d.rectangle([1, 5, 12, 8], fill=3)
            d.line([(5, 1), (5, 19)], fill=2); d.line([(1, 5), (12, 5)], fill=2); d.line([(5, 1), (8, 1)], fill=2)
            d.line([(8, 2), (8, 19)], fill=4); d.line([(1, 8), (12, 8)], fill=4); d.point([(12, 6), (12, 7)], fill=4)
            d.point([(6, 6), (7, 7)], fill=5); d.line([(6, 11), (7, 13)], fill=5)
            d.rectangle([3, 19, 10, 21], fill=4); d.line([(3, 19), (8, 19)], fill=3)
            d.point([(3, 18), (10, 20), (2, 21)], fill=6)
    return _pil(14, 18 if kind == 0 else 22, pal, f)

def dead_tree():
    pal = [T, INK, (150, 130, 136, 255), (108, 90, 102, 255), (74, 60, 78, 255), (48, 38, 56, 255)]
    def f(d):
        d.polygon([(10, 38), (18, 38), (16, 22), (15, 12), (13, 12), (12, 22)], fill=3)
        d.line([(12, 36), (13, 14)], fill=2); d.line([(17, 37), (15, 16)], fill=4)
        for (pts, w) in (([(13, 24), (8, 17), (5, 11), (2, 8)], 2), ([(8, 17), (4, 18), (2, 16)], 1), ([(5, 11), (6, 6), (4, 3)], 1),
                         ([(15, 21), (20, 14), (23, 8), (26, 6)], 2), ([(20, 14), (24, 15), (26, 13)], 1), ([(23, 8), (22, 4), (24, 1)], 1),
                         ([(14, 13), (13, 7), (11, 3), (11, 1)], 2), ([(13, 7), (16, 4), (17, 1)], 1), ([(11, 3), (8, 2)], 1),
                         ([(10, 38), (6, 38), (4, 39)], 1), ([(18, 38), (22, 38), (24, 39)], 1)):
            d.line(pts, fill=3, width=w)
        d.line([(12, 23), (8, 17), (5, 11)], fill=2); d.line([(14, 12), (13, 7)], fill=2); d.line([(16, 20), (20, 14)], fill=4)
        d.ellipse([13, 27, 15, 31], fill=5); d.point([(14, 26)], fill=4)        # knot hole
        d.line([(11, 34), (12, 30)], fill=4); d.line([(16, 34), (16, 30)], fill=5)
    return _pil(28, 40, pal, f)

def fence():
    pal = [T, INK, (150, 150, 178, 255), (96, 96, 126, 255), (60, 60, 86, 255)]
    def f(d):
        d.line([(1, 8), (22, 8)], fill=3); d.line([(1, 17), (22, 17)], fill=3)
        for x in (6, 11, 16):
            d.line([(x, 4), (x, 20)], fill=3); d.point([(x, 1), (x, 2), (x, 3)], fill=2); d.point([(x - 1, 3), (x + 1, 3)], fill=3)
        for x in (1, 21):
            d.rectangle([x, 4, x + 1, 20], fill=3); d.line([(x, 4), (x, 20)], fill=2)
            d.rectangle([x, 1, x + 1, 2], fill=2); d.point([(x + 1, 2)], fill=4)
            d.line([(x, 20), (x + 1, 20)], fill=4)
        d.point([(3, 12), (8, 13), (13, 12), (18, 13)], fill=4)                 # scroll hints
    return _pil(24, 22, pal, f)

def candelabra(frame):
    pal = [T, INK, (255, 236, 130, 255), (226, 176, 60, 255), (150, 104, 40, 255), (255, 255, 240, 255), (214, 210, 200, 255),
           (255, 250, 190, 255), (255, 170, 50, 255), (230, 80, 40, 255)]
    def f(d):
        d.rectangle([3, 27, 10, 28], fill=3); d.line([(3, 28), (10, 28)], fill=4); d.rectangle([5, 25, 8, 26], fill=3)
        d.rectangle([6, 13, 7, 25], fill=3); d.line([(6, 13), (6, 25)], fill=2); d.rectangle([5, 19, 8, 20], fill=2)
        d.arc([2, 8, 11, 17], 0, 180, fill=3); d.line([(2, 12), (2, 13)], fill=3); d.line([(11, 12), (11, 13)], fill=3)
        for (x, top) in ((2, 8), (6, 5), (10, 8)):
            d.rectangle([x - 1, top + 4, x + 2, top + 4], fill=2)               # drip cup
            d.rectangle([x, top, x + 1, top + 3], fill=5); d.point([(x + 1, top + 1), (x + 1, top + 2), (x + 1, top + 3)], fill=6)
            lean = (frame*2 - 1) if x != 6 else (1 - frame*2)
            d.point([(x, top - 1), (x + 1, top - 1)], fill=7); d.point([(x + (1 if lean > 0 else 0), top - 2)], fill=8)
            d.point([(x + (1 if lean > 0 else 0), top - 3)], fill=9 if frame else 8)
    return _pil(14, 30, pal, f)

def pumpkin():
    pal = [T, INK, (255, 180, 70, 255), (244, 130, 36, 255), (190, 84, 28, 255), (120, 50, 30, 255), (255, 250, 150, 255),
           (255, 210, 60, 255), (110, 170, 70, 255), (60, 110, 56, 255)]
    def f(d):
        d.ellipse([1, 3, 14, 13], fill=3)
        d.ellipse([4, 3, 11, 13], fill=2); d.ellipse([6, 3, 9, 13], fill=3)
        d.arc([1, 3, 14, 13], 20, 160, fill=4); d.line([(4, 5), (3, 11)], fill=4); d.line([(11, 5), (12, 11)], fill=4)
        d.rectangle([7, 1, 8, 3], fill=8); d.point([(8, 1), (8, 2)], fill=9)
        for x in (4, 9):
            d.polygon([(x, 7), (x + 1, 5), (x + 2, 7)], fill=7); d.point([(x + 1, 6)], fill=6)
        d.point([(7, 8), (8, 8)], fill=7)
        d.line([(3, 9), (12, 9)], fill=7); d.line([(4, 10), (11, 10)], fill=6); d.line([(5, 11), (10, 11)], fill=7)
        d.point([(5, 9), (8, 9), (11, 9), (6, 11), (9, 11)], fill=3)            # teeth
    return _pil(16, 15, pal, f)

def armor():
    pal = [T, INK, (240, 244, 252, 255), (180, 190, 214, 255), (120, 130, 164, 255), (76, 82, 116, 255), (230, 60, 60, 255),
           (150, 104, 66, 255), (100, 66, 46, 255), (255, 220, 100, 255), (30, 28, 44, 255)]
    def f(d):
        d.line([(14, 4), (14, 33)], fill=7); d.point([(14, 12), (14, 22), (14, 30)], fill=8)     # halberd
        d.polygon([(14, 3), (11, 4), (10, 7), (11, 10), (14, 10)], fill=3); d.line([(11, 5), (11, 9)], fill=2)
        d.point([(14, 1), (14, 2)], fill=2); d.point([(13, 7)], fill=4)
        d.rectangle([7, 1, 8, 2], fill=6)                                       # plume
        d.ellipse([5, 2, 10, 8], fill=3); d.line([(6, 3), (6, 6)], fill=2); d.line([(6, 5), (9, 5)], fill=10); d.point([(9, 4), (9, 6), (10, 5)], fill=4)
        d.rectangle([4, 9, 11, 18], fill=3); d.line([(4, 9), (4, 17)], fill=2); d.rectangle([10, 10, 11, 18], fill=4)
        d.line([(7, 10), (7, 17)], fill=4); d.point([(8, 12), (8, 15)], fill=9)
        d.ellipse([1, 8, 5, 11], fill=3); d.ellipse([10, 8, 14, 11], fill=3); d.point([(2, 9), (11, 9)], fill=2)
        d.rectangle([2, 11, 3, 18], fill=4); d.rectangle([12, 11, 13, 16], fill=4); d.rectangle([12, 16, 14, 18], fill=3)
        d.rectangle([4, 19, 11, 22], fill=4); d.line([(4, 19), (11, 19)], fill=9); d.line([(5, 22), (10, 22)], fill=5)
        d.rectangle([5, 23, 7, 31], fill=3); d.rectangle([8, 23, 10, 31], fill=4); d.line([(5, 23), (5, 31)], fill=2)
        d.point([(6, 26), (9, 26)], fill=5)
        d.rectangle([4, 32, 7, 33], fill=4); d.rectangle([8, 32, 11, 33], fill=5)
        d.rectangle([2, 34, 13, 34], fill=5)
    return _pil(16, 36, pal, f)

def coffin():
    pal = [T, INK, (176, 120, 80, 255), (130, 84, 60, 255), (88, 54, 46, 255), (30, 24, 40, 255), (255, 224, 90, 255),
           (255, 240, 120, 255), (56, 36, 40, 255)]
    def f(d):
        d.polygon([(4, 1), (8, 1), (10, 8), (8, 28), (4, 28), (2, 8)], fill=8)                  # box
        d.polygon([(4, 3), (7, 3), (8, 8), (7, 26), (4, 26), (3, 8)], fill=5)                   # dark inside
        d.point([(4, 11), (6, 11)], fill=7)                                                     # somebody's home
        d.polygon([(8, 1), (12, 1), (14, 8), (12, 28), (8, 28), (6, 8)], fill=3)                # lid, ajar
        d.line([(8, 1), (6, 8), (8, 28)], fill=2); d.line([(12, 1), (14, 8), (12, 28)], fill=4)
        d.line([(10, 5), (10, 13)], fill=6); d.line([(8, 8), (12, 8)], fill=6)                  # brass cross
        d.line([(9, 17), (11, 17)], fill=4); d.line([(9, 22), (11, 22)], fill=4)
    return _pil(16, 30, pal, f)

def bat(frame):
    pal = [T, INK, (150, 110, 190, 255), (100, 70, 140, 255), (64, 44, 100, 255), (255, 210, 60, 255), (255, 255, 255, 255)]
    def f(d):
        wing = [(7, 6), (5, 2), (1, 1), (1, 5), (3, 4), (4, 7), (6, 8)] if frame == 0 else [(7, 5), (4, 4), (1, 7), (1, 10), (3, 8), (5, 10), (7, 8)]
        for mir in (False, True):
            pts = [((17 - x) if mir else x, y) for x, y in wing]
            d.polygon(pts, fill=4)
            d.line(pts[:3], fill=3)
        d.ellipse([7, 4, 10, 9], fill=3); d.point([(7, 3), (10, 3)], fill=3); d.point([(8, 8), (9, 9)], fill=4)
        d.point([(8, 5), (10, 5)], fill=5); d.point([(8, 7), (9, 7)], fill=6)                   # eyes, fangs
    return _pil(18, 12, pal, f)

def grandfather():
    pal = [T, INK, (190, 130, 84, 255), (140, 90, 60, 255), (96, 58, 46, 255), (60, 36, 36, 255), (255, 250, 220, 255),
           (255, 220, 100, 255), (190, 140, 50, 255), (30, 26, 40, 255)]
    def f(d):
        d.rectangle([2, 3, 13, 13], fill=3); d.rectangle([1, 1, 14, 2], fill=2); d.line([(1, 2), (14, 2)], fill=3)
        d.polygon([(5, 1), (7, 0), (8, 0), (10, 1)], fill=2)
        d.line([(2, 3), (2, 13)], fill=2); d.rectangle([12, 3, 13, 13], fill=4)
        d.ellipse([3, 3, 12, 12], fill=8); d.ellipse([4, 4, 11, 11], fill=6)                    # dial
        d.line([(7, 7), (7, 5)], fill=9); d.line([(7, 7), (9, 8)], fill=9); d.point([(7, 4), (10, 7), (7, 10), (4, 7)], fill=8)
        d.rectangle([1, 13, 14, 14], fill=2); d.line([(1, 14), (14, 14)], fill=4)
        d.rectangle([4, 15, 11, 30], fill=3); d.line([(4, 15), (4, 30)], fill=2); d.rectangle([10, 15, 11, 30], fill=4)
        d.rectangle([6, 16, 9, 29], fill=9)                                                     # glass door
        d.line([(7, 16), (8, 23)], fill=8); d.ellipse([7, 23, 9, 26], fill=7); d.point([(8, 24)], fill=6)
        d.rectangle([2, 31, 13, 37], fill=3); d.rectangle([2, 31, 13, 31], fill=2); d.rectangle([12, 32, 13, 37], fill=4)
        d.rectangle([5, 33, 10, 35], fill=4); d.line([(2, 37), (13, 37)], fill=5)
        d.point([(2, 38), (3, 38), (12, 38), (13, 38)], fill=5)
    return _pil(16, 40, pal, f)

def cup_star():
    """k_cup3: the Star Cup - a gold shooting star on a deep-blue disc with a silver-blue ring."""
    W = Hh = 24
    pal = [T, INK, (120, 150, 255, 255), (60, 84, 220, 255), (34, 44, 150, 255), (22, 24, 90, 255),
           (220, 240, 255, 255), (130, 170, 240, 255), (255, 255, 220, 255), (255, 226, 90, 255), (246, 160, 40, 255),
           (255, 255, 255, 255)]
    c = np.zeros((Hh, W), np.uint8); S = c.shape
    Dm = _disc(S, 11.5, 11.5, 11.2)
    c[_dil(Dm, 1)] = 1
    c[Dm] = 7
    c[Dm & (np.mgrid[0:Hh, 0:W][0] < 9)] = 6
    c[_disc(S, 11.5, 11.5, 9.4)] = 1
    _emboss(c, _disc(S, 11.5, 11.5, 8.6), [3, 4, 4, 5, 5], ow=0)
    for (x, y) in ((6, 7), (17, 15), (9, 17), (16, 6)): c[y, x] = 11              # distant stars
    for i, (x0, y0, ln) in enumerate(((5, 17, 6), (4, 14, 5), (8, 19, 5))):          # comet trail to the lower left
        for k in range(ln):
            c[y0 - k, x0 + k] = 10 if k < 2 else 9
    sx, sy = 14.0, 10.0; pts = []
    for i in range(10):
        r = 6.2 if i % 2 == 0 else 2.7; a = -math.pi/2 + math.pi*i/5 + 0.25
        pts.append((sx + r*math.cos(a), sy + r*math.sin(a)))
    St = _poly(S, pts)
    c[_dil(St, 1) & _disc(S, 11.5, 11.5, 11.2)] = 1
    _emboss(c, St, [8, 9, 9, 10], ow=0)
    return _spr(c, pal)

# ---- haunted set pieces: painted, single view ---------------------------------------------------
def sp_pendulum():
    """Brass bob 46 texels across on ~90 texels of rod; anchor = bottom centre of the bob."""
    W, Hh = 70, 197; cx = 34.5; R = 33.0; cy = Hh - 2 - R
    ramps = [[(255, 246, 170), (250, 206, 90), (210, 150, 52), (150, 98, 40), (96, 60, 36), (60, 38, 34)]]
    pal, bases = _ramps(ramps); c = np.zeros((Hh, W), np.uint8)
    yy, xx = np.mgrid[0:Hh, 0:W]; dx = xx - cx; dy = yy - cy; r = np.hypot(dx, dy)
    lit = (-dx - dy)/np.maximum(r, 1e-6)/math.sqrt(2)
    rod = (np.abs(dx) < 2.4) & (yy < cy)
    collar = (np.abs(dx) < 5.5) & (np.abs(yy - (cy - R - 3)) < 4) | (np.abs(dx) < 4.0) & (np.abs(yy - (cy - R - 9)) < 2)
    bob = r <= R
    b = np.full(c.shape, 0.5)
    b = np.where(rod, 0.75 - 0.22*dx, b)
    b = np.where(collar, 0.8 - 0.09*dx - 0.05*(yy - (cy - R - 3)), b)
    face = 0.56 - 0.20*lit*(r/R) + 0.10*np.cos(r*0.9)*0                             # shallow dish
    rim = 0.62 + 0.36*lit
    b = np.where(bob, np.where(r > R - 5, rim, np.where(r > R - 7, 0.22, face)), b)
    ang = np.arctan2(dy, dx)
    studs = bob & (np.abs(r - (R - 11)) < 1.8) & (np.cos(ang*12) > 0.72)
    b = np.where(studs, 0.95, b)
    b = np.where(bob & (np.abs(r - (R - 15.5)) < 0.8), 0.25, b)
    moon = (np.hypot(dx + 1, dy) <= 12.5) & ~(np.hypot(dx - 4.5, dy + 2.5) <= 10.5)
    b = np.where(moon, 0.16, b)
    edge = moon & ~np.roll(np.roll(moon, 1, 0), 1, 1)
    b = np.where(np.roll(np.roll(moon, 1, 0), 1, 1) & ~moon & bob, 0.98, b)        # engraved lip catches the light
    solid = rod | collar | bob
    _paint(c, solid, np.zeros(c.shape, int), b, ramps, bases, yy, xx, dith=0.5)
    c[_dil(solid, 1) & ~solid] = 1
    return _body("pendulum", c, pal, 0.7, ax=35, ay=Hh - 1)

def _ghost_img(frame):
    W, Hh = 56, 66
    yy, xx = np.mgrid[0:Hh, 0:W]
    oy = 0 if frame == 0 else -3; cx = 27.5
    hy = 20 + oy
    head = np.hypot(xx - cx, (yy - hy)) <= 15.5
    hem = 49 + oy + 2.6*np.sin(xx*0.95 + frame*1.6)
    body = (np.abs(xx - cx) <= 15.5 + (yy - hy)*0.07) & (yy >= hy) & (yy < hem)
    t = np.clip((yy - (44 + oy))/(19.0 - oy), 0, 1)
    tcx = cx + (9.0 if frame == 0 else -6.0)*t*t + 2.0*np.sin(t*5)
    tail = (yy >= 44 + oy) & (np.abs(xx - tcx) <= 11.5*(1 - t)**1.25)
    if frame == 0:
        arms = (((xx - 9.5)/7.0)**2 + ((yy - 35)/4.2)**2 <= 1) | (((xx - 45.5)/7.0)**2 + ((yy - 35)/4.2)**2 <= 1)
    else:
        arms = (((xx - 8.5)/4.6)**2 + ((yy - 19)/7.5)**2 <= 1) | (((xx - 46.5)/4.6)**2 + ((yy - 19)/7.5)**2 <= 1)
        arms |= (np.abs(xx - cx) <= 20) & (np.abs(yy - 26) <= 3)
    M = head | body | tail | arms
    nx = np.clip((xx - cx)/19.0, -1, 1)
    b = 0.30 + 0.70*np.clip(np.sqrt(1 - nx*nx)*0.85 - nx*0.30, 0, 1)
    b = b - 0.30*np.clip((yy - (40 + oy))/22.0, 0, 1)                             # darker toward the hem
    b = np.where(head & (yy < hy - 9), b + 0.1, b)
    return M, b, oy, cx, yy, xx

def sp_ghosts():
    ramps = [[(255, 255, 255), (228, 242, 255), (184, 212, 250), (136, 168, 232), (96, 122, 200), (66, 82, 156)],
             [(255, 176, 206), (236, 130, 176)]]
    pal, bases = _ramps(ramps); out = []
    for frame in (0, 1):
        M, b, oy, cx, yy, xx = _ghost_img(frame)
        c = np.zeros(M.shape, np.uint8)
        _paint(c, M, np.zeros(M.shape, int), b, ramps, bases, yy, xx, dith=0.5)
        ey = 19 + oy
        for ex in (cx - 6.5, cx + 6.5):
            eye = ((xx - ex)/3.6)**2 + ((yy - ey)/(4.6 if frame == 0 else 5.2))**2 <= 1
            c[eye] = 1
            c[(np.abs(xx - (ex - 1.2)) < 1.1) & (np.abs(yy - (ey - 2)) < 1.1)] = bases[0]
        mouth = ((xx - cx)/(2.8 if frame == 0 else 3.6))**2 + ((yy - (29 + oy))/(3.6 if frame == 0 else 4.8))**2 <= 1
        c[mouth] = 1
        for ex in (cx - 11.5, cx + 11.5):
            c[(((xx - ex)/2.6)**2 + ((yy - (25 + oy))/1.6)**2 <= 1) & M] = bases[1] + 1
        c[_dil(M, 1) & ~M] = 1
        out.append(c)
    return [_body("ghost_a", out[0], pal, 0.5, ax=28, ay=65), _body("ghost_b", out[1], pal, 0.5, ax=28, ay=65)]

# (sp_pendulum, sp_arch and sp_chapel are kept above but no longer baked: the graveyard dropped them)

# ---- graveyard (HAUNTED outdoors) ---------------------------------------------------------------
HAND_P = [(226, 234, 204, 255), (176, 198, 164, 255), (116, 142, 126, 255),           # bone: light, mid, shade
          (122, 92, 78, 255), (86, 62, 60, 255), (56, 40, 50, 255)]                    # churned soil: light, mid, dark

def grave_hand(frame):
    """16x24, bottom-centre anchored. 0 = soil mound + clods (warning), 1 = fingertips breaking
    through, 2 = forearm with open grasping hand, 3 = same with the fingers curled."""
    pal = [T, INK] + HAND_P
    def bone(d, pts, shade=True):
        d.line(pts, fill=3)
        d.point(pts[-1], fill=2)                                                       # pale fingertip
    def f(d):
        if frame >= 2:                                                                 # forearm (behind the mound)
            d.polygon([(6, 20), (9, 20), (9, 12), (7, 12)], fill=3)
            d.line([(6, 19), (7, 12)], fill=2); d.line([(9, 19), (9, 13)], fill=4)
            d.point([(8, 15), (8, 17)], fill=4)                                        # radius/ulna gap
        d.ellipse([1, 17, 14, 29], fill=6)                                             # mound
        d.ellipse([3, 18, 11, 27], fill=6)
        d.line([(3, 19), (5, 18), (8, 17)], fill=5); d.point([(2, 21), (6, 20), (10, 19)], fill=5)
        d.point([(4, 22), (9, 21), (12, 22), (7, 23), (2, 23), (11, 20)], fill=7)
        d.line([(12, 23), (14, 23)], fill=7); d.point([(13, 21)], fill=7)
        if frame == 0:
            d.rectangle([11, 14, 12, 15], fill=6); d.point([(11, 14)], fill=5)         # clods
            d.point([(3, 15)], fill=6); d.point([(3, 14)], fill=5); d.point([(7, 14)], fill=7)
            d.line([(6, 19), (8, 21), (7, 22)], fill=7)                                # crack
        elif frame == 1:
            for (x, y0) in ((4, 15), (6, 13), (9, 13), (11, 15)):                      # four fingertips
                d.line([(x, y0), (x, 18)], fill=3); d.point([(x, y0)], fill=2); d.point([(x, y0 + 2)], fill=4)
            d.point([(2, 18), (13, 17)], fill=6); d.point([(7, 17), (8, 18)], fill=7)
        else:
            d.rectangle([5, 8, 10, 12], fill=3)                                        # palm
            d.line([(5, 8), (5, 11)], fill=2); d.line([(10, 9), (10, 12)], fill=4); d.line([(6, 12), (10, 12)], fill=4)
            d.point([(7, 10), (8, 11)], fill=4)
            if frame == 2:
                for pts in ([(5, 8), (4, 5), (3, 2)], [(7, 8), (6, 4), (6, 1)], [(9, 8), (9, 4), (10, 1)], [(10, 8), (12, 5), (13, 3)]):
                    d.line(pts, fill=3); d.point([pts[1]], fill=4); d.point([pts[2]], fill=2)
                d.line([(5, 11), (3, 10), (2, 8)], fill=3); d.point([(2, 8)], fill=2)  # thumb
            else:
                for (x, yt) in ((5, 5), (7, 4), (9, 4), (11, 6)):                      # fingers folded at the knuckle
                    d.line([(x, yt), (x, 8)], fill=3); d.point([(x, yt)], fill=2); d.point([(x, yt + 1)], fill=4)
                d.point([(11, 8), (11, 9)], fill=3)
                d.line([(4, 10), (6, 10)], fill=2); d.point([(7, 11)], fill=4)         # thumb across the palm
    return _pil(16, 24, pal, f)

MOON_P = [(255, 250, 226, 255), (244, 234, 196, 255), (214, 206, 180, 255), (170, 168, 164, 255), (132, 132, 140, 255)]

def graveyard_moon(dim):
    """28x28 full moon, pale cream with grey craters; dim = the same pixels behind haze."""
    W = 28; c = 13.5
    yy, xx = np.mgrid[0:W, 0:W]; dx = xx - c; dy = yy - c; r = np.hypot(dx, dy)
    disc = r <= 13.6
    px = np.zeros((W, W), np.uint8)
    b = 0.95 - 0.42*np.clip((dx*0.55 + dy*0.75)/13.6, 0, 1) - 0.25*np.clip(r/13.6 - 0.8, 0, 1)
    th = BAYER[yy % 4, xx % 4] - 0.5
    lv = np.clip(np.floor((1 - b)*5.2 + th*0.7), 0, 2).astype(int)
    px[disc] = (1 + lv)[disc]
    for (cx, cy, cr) in ((8.5, 9.0, 3.2), (17.5, 7.0, 2.0), (19.0, 16.5, 3.6), (10.0, 19.5, 2.2), (14.0, 13.0, 1.3), (22.5, 11.0, 1.2), (6.0, 14.5, 1.2)):
        d = np.hypot(xx - cx, yy - cy)
        px[disc & (d <= cr)] = 4
        px[disc & (d <= cr) & (np.hypot(xx - cx + 0.9, yy - cy + 0.9) > cr)] = 5       # shaded lower-right wall
    pal = [T] + MOON_P
    if dim:
        pal = [T] + [(int(r_*0.42 + 6), int(g*0.48 + 10), int(b_*0.66 + 30), 255) for (r_, g, b_, _) in MOON_P]
    return {"w": W, "h": W, "px": px.tolist(), "pal": pal}

def graveyard_cloud():
    """56x18 dark purple-grey cloud bank with a lighter upper rim (slides across the moon)."""
    W, Hh = 56, 18
    yy, xx = np.mgrid[0:Hh, 0:W]
    M = np.zeros((Hh, W), bool)
    for (cx, cy, rx, ry) in ((27.5, 11.5, 27.4, 5.6), (12, 9.5, 8.5, 5.5), (22, 6.5, 8, 6.2), (33, 7.5, 9, 6.8), (44, 10, 8, 5), (38, 12.5, 12, 4.6),
                             (6, 12.5, 6, 3.2), (50.5, 12.5, 5.2, 3.0)):
        M |= ((xx - cx)/rx)**2 + ((yy - cy)/ry)**2 <= 1
    px = np.zeros((Hh, W), np.uint8)
    up = np.zeros_like(M); up[1:] = M[:-1]
    up2 = np.zeros_like(M); up2[2:] = M[:-2]
    px[M] = 3
    px[M & (yy >= 12 + ((xx//3) % 2))] = 4                                             # darker belly
    px[M & ~up2] = 2
    px[M & ~up] = 1                                                                    # rim
    pal = [T, (112, 100, 142, 255), (84, 72, 114, 255), (60, 50, 88, 255), (44, 36, 70, 255)]
    return {"w": W, "h": Hh, "px": px.tolist(), "pal": pal}

def graveyard_props():
    return {"hand0": grave_hand(0), "hand1": grave_hand(1), "hand2": grave_hand(2), "hand3": grave_hand(3),
            "moon": graveyard_moon(False), "moon_dim": graveyard_moon(True), "moon_cloud": graveyard_cloud()}

def world_props():
    p = {"toxic_barrel": prop_toxic_barrel(), "pipe0": pipe(0), "pipe1": pipe(1), "rat": rat(), "rubber_duck": rubber_duck(),
         "sewer_lamp": sewer_lamp(), "ladder": ladder(),
         "led_red0": led("red", 0), "led_red1": led("red", 1), "led_green0": led("green", 0), "led_green1": led("green", 1),
         "jumper": jumper(), "trimpot": trimpot(), "spark0": spark(0), "spark1": spark(1), "spark2": spark(2),
         "astronaut": astronaut(), "beacon0": beacon(0), "beacon1": beacon(1), "flare0": flare(0), "flare1": flare(1), "flare2": flare(2),
         "tomb0": tomb(0), "tomb1": tomb(1), "dead_tree": dead_tree(), "fence": fence(), "candle0": candelabra(0), "candle1": candelabra(1),
         "pumpkin": pumpkin(), "armor": armor(), "coffin": coffin(), "bat0": bat(0), "bat1": bat(1), "grandfather": grandfather()}
    p.update(graveyard_props())
    return p

def extra_props():
    t0, t1 = prop_torch_frames()
    p = {
        "log": prop_log(), "bones": prop_bones(), "crate": prop_crate(), "buoy": prop_buoy(),
        "snowman": prop_snowman(), "cone": prop_cone(), "pillar": prop_pillar(), "torch0": t0, "torch1": t1,
        "cyber_barrier": prop_cyber_barrier(), "rock": prop_rock(), "shrub": prop_shrub(), "tumbleweed": tumbleweed(),
        "logo": logo(),
    }
    for n in range(1, 9): p["place%d" % n] = place_badge(n)
    for s in range(4): p["light%d" % s] = start_light(s)
    p["trophy_gold"] = trophy(0); p["trophy_silver"] = trophy(1); p["trophy_bronze"] = trophy(2)
    p["cup1"] = cup_emblem(1); p["cup2"] = cup_emblem(2)
    p["podium"] = podium(); p["arrow_wrongway"] = arrow_wrongway(); p["cursor"] = cursor(); p["item_frame"] = item_frame()
    return p

def c_bytes(flat):
    flat = list(flat)
    return ",\n".join(",".join(map(str, flat[i:i+64])) for i in range(0, len(flat), 64))

def rgba32(c):
    r, g, b, a = c
    # SNES-style 15-bit color: quantize each channel to 5 bits
    q = lambda v: (v >> 3) * 255 // 31
    return "0x%02x%02x%02x%02xu" % (a, q(b), q(g), q(r))  # little-endian RGBA in memory

def pal32(pal):
    assert len(pal) <= 32, len(pal)
    return ", ".join(["0u"] + [rgba32(c) for c in pal[1:]] + ["0u"]*(32 - len(pal)))

def flat_px(s):
    return [v for row in s["px"] for v in row]

def main():
    import time
    t0 = time.time()
    karts, kpals, portraits = [], [], []
    for i, fn in enumerate(RACERS):
        model, _info = fn()
        pal, _ = model.palette()
        assert len(pal) <= 32, (RACER_NAMES[i], len(pal))
        # fit check
        P, _ = model.arrays()
        for k in range(N_ANGLES):
            sx, sy = project(P.reshape(-1, 3), 2*math.pi*k/N_ANGLES)
            xs = ANCHOR[0] + sx*KART_PPU; ys = ANCHOR[1] - sy*KART_PPU
            if xs.min() < 0.5 or xs.max() > SPR - 0.5 or ys.min() < 0.5 or ys.max() > SPR - 0.5:
                print("  warn: %s angle %d exceeds sprite (x %.1f..%.1f y %.1f..%.1f)" % (RACER_NAMES[i], k, xs.min(), xs.max(), ys.min(), ys.max()))
                break
        karts.append(render_racer(model)); kpals.append(pal)
        portraits.append(hand_portrait(i))
        sys.stdout.write("%s " % RACER_NAMES[i]); sys.stdout.flush()
    print("\nracers %.1fs" % (time.time() - t0))
    sets = []
    for (name, builders, wpp, angles, ss) in SET_PIECES:
        t1 = time.time()
        sp = bake_set_piece(name, builders, wpp, angles, ss)
        if name in SPLIT:
            for k, nm in enumerate(SPLIT[name]):
                sets.append(dict(sp, name=nm, imgs=sp["imgs"][k*angles:(k + 1)*angles], angles=angles))
        else:
            sets.append(sp)
        print(" %s %dx%d x%d (%.1fs)" % (name, sp["w"], sp["h"], sp["angles"], time.time() - t1))
    for fn in PAINTED_SETS:   # painted single-view bodies (planets, pendulum)
        sp = fn(); sets.append(sp)
        print(" %s %dx%d (painted)" % (sp["name"], sp["w"], sp["h"]))
    sets += sp_ghosts()
    props = {
        "crystal0": crystal(0), "crystal1": crystal(1), "crystal2": crystal(2), "crystal3": crystal(3),
        "bolt0": bolt(0), "bolt1": bolt(1), "bolt2": bolt(2), "bolt3": bolt(3),
        "bomb": bomb(), "oil": oil(), "tree": tree(), "cactus": cactus(), "pine": pine(),
        "castle_tower": prop_castle_tower(), "banner": prop_banner(), "palm": palm(), "barrel": prop_barrel(),
        "fern": fern(), "jungle_tree": jungle_tree(), "lamp": prop_lamp(), "neon_sign": neon_sign(),
        "snow_rock": prop_snow_rock(),
        "fire0": fire(0), "fire1": fire(1), "fire2": fire(2), "cannonball": cannonball(),
        "explosion0": explosion(0), "explosion1": explosion(1), "explosion2": explosion(2),
        "rocket": rocket_fx(), "fake_crystal": fake_crystal(),
        "warning_light0": warning_light(0), "warning_light1": warning_light(1),
        "icon_nitro": icon_nitro(), "icon_shield": icon_shield(), "icon_oil": icon_oil(),
        "icon_nitro3": icon_nitro3(), "icon_rocket": icon_rocket(), "icon_emp": icon_emp(),
        "icon_fake": icon_fake(), "icon_magnet": icon_magnet(),
    }
    props.update(extra_props())
    props.update(table_props())
    props.update(world_props())
    # ---- the art file: every sprite's pixels and palette, found by name (see src/art.cc) ----
    # Header: "SRAT", u32 version, u32 count, u32 0. Then `count` entries of 64 bytes:
    #   char name[32]; u16 kind (0 a prop, 1 a set piece, 2 a racer's kart frames), w, h, frames;
    #   i16 anchor_x, anchor_y; f32 world_per_px; u32 pal_count, px_offset, px_size, pal_offset.
    # Then the data: pixels as palette indices, palettes as u32 colors at 4-byte offsets.
    import struct
    def colors(pal, n=None):   # as the game's u32s: index 0 transparent, then SNES 15-bit colors
        out = [0] + [int(rgba32(c)[:-1], 16) for c in pal[1:]]
        return out + [0] * ((n or len(out)) - len(out))
    entries = []   # (name, kind, w, h, frames, ax, ay, wpp, [u32 colors], bytes)
    for i, frames in enumerate(karts):
        entries.append(("kart%d" % i, 2, SPR, SPR, N_ANGLES, ANCHOR[0], ANCHOR[1], 0.0, colors(kpals[i], 32),
                        bytes(v for fr in frames for v in fr.flatten().tolist())))
    for i, sp in enumerate(portraits):
        entries.append(("portrait%d" % i, 0, sp["w"], sp["h"], 1, 0, 0, 0.0, colors(sp["pal"], 32), bytes(flat_px(sp))))
    for sp in sets:
        entries.append(("sp_" + sp["name"], 1, sp["w"], sp["h"], sp["angles"], sp["ax"], sp["ay"], sp["wpp"], colors(sp["pal"], 32),
                        bytes(v for im in sp["imgs"] for v in im.flatten().tolist())))
    for name, sp in props.items():
        entries.append((name, 0, sp["w"], sp["h"], 1, 0, 0, 0.0, colors(sp["pal"]), bytes(flat_px(sp))))
    assert len(set(e[0] for e in entries)) == len(entries) and all(len(e[0]) < 32 for e in entries)
    head = 16 + 64 * len(entries)
    data = bytearray()
    table = bytearray()
    for (name, kind, w, h, fr, ax, ay, wpp, pal, px) in entries:
        assert len(px) == w * h * fr, name
        px_off = head + len(data); data += px
        data += b"\0" * (-len(data) % 4)
        pal_off = head + len(data); data += struct.pack("<%dI" % len(pal), *pal)
        table += struct.pack("<32sHHHHhhfIIII", name.encode(), kind, w, h, fr, ax, ay, wpp, len(pal), px_off, len(px), pal_off)
    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    with open(OUT_BIN + ".tmp", "wb") as f:
        f.write(struct.pack("<4sIII", b"SRAT", 1, len(entries), 0) + table + data)
    os.replace(OUT_BIN + ".tmp", OUT_BIN)   # whole or not at all: a running game may be watching it

    # ---- the header: the names the code draws with. They are empty until art.bin is loaded. ----
    with open(OUT_H, "w") as f:
        f.write("// Generated by tools/bake_art.py. Do not edit.\n#pragma once\n#include \"korvid/core/types.h\"\n\nnamespace sr {\n\n")
        f.write("// The sprites themselves are in assets/art.bin. These are the names the code draws them\n"
                "// by: empty until art_load() fills them from the file, and filled again whenever the file\n"
                "// is reloaded, so a picture can change while the game runs.\n")
        f.write("struct PropImg { int w, h; const kv::u8* px; const kv::u32* pal; };\n")
        f.write("// Multi-angle sprite: px holds angles*w*h indices, angle-major. Frame k shows heading\n"
                "// rel = k*2pi/angles relative to the view direction (0 = seen from behind, angles/4 =\n"
                "// nose to screen-right). (anchor_x, anchor_y) is the model origin: the ground point under\n"
                "// its center (for flyers: its center at flight height). world_per_px = world texels per\n"
                "// sprite pixel at scale 1. pal has 32 entries, 0 = transparent.\n")
        f.write("struct SetPieceImg { int w, h, angles, anchor_x, anchor_y; float world_per_px; const kv::u8* px; const kv::u32* pal; };\n\n")
        f.write("// ---- racers: 0 Bruno, 1 Pip-8, 2 Madame Hex, 3 Cap'n Gullbeard, 4 Tiny Rex, 5 Volt Vega,\n"
                "// 6 Sir Clanks, 7 Granny Goggles. Each has its own 32-entry palette (0 transparent).\n"
                "// Kart sprites are 0.35 world texels per pixel.\n")
        f.write("constexpr int CHAR_COUNT = %d, KART_ANGLES = %d, KART_SIZE = %d, KART_ANCHOR_X = %d, KART_ANCHOR_Y = %d;\n"
                % (len(karts), N_ANGLES, SPR, ANCHOR[0], ANCHOR[1]))
        f.write("inline const kv::u8* k_kart_px[CHAR_COUNT][KART_ANGLES]; // each KART_SIZE * KART_SIZE indices\n")
        f.write("inline const kv::u32* k_kart_pal[CHAR_COUNT];            // 32 colors each\n\n")
        f.write("// Portraits: 32x32 head-and-shoulders busts.\n")
        f.write("inline PropImg k_portrait[CHAR_COUNT];\n\n")
        f.write("// ---- set pieces ----\n")
        for sp in sets: f.write("inline SetPieceImg k_sp_%s;\n" % sp["name"])
        f.write("\n// ---- props, FX and HUD icons ----\n")
        for name in props: f.write("inline PropImg k_%s;\n" % name)
        f.write("\n// What art_load() fills: each name in the file, and where it goes.\n")
        f.write("struct ArtSlot { const char* name; int kind; void* at; int index; }; // kind as in the file; index: which racer\n")
        f.write("inline const ArtSlot k_art_slots[] = {\n")
        for i in range(len(karts)): f.write("    {\"kart%d\", 2, nullptr, %d},\n" % (i, i))
        for i in range(len(portraits)): f.write("    {\"portrait%d\", 0, &k_portrait[%d], %d},\n" % (i, i, i))
        for sp in sets: f.write("    {\"sp_%s\", 1, &k_sp_%s, 0},\n" % (sp["name"], sp["name"]))
        for name in props: f.write("    {\"%s\", 0, &k_%s, 0},\n" % (name, name))
        f.write("};\n\n")
        f.write("// 5x7 font, ASCII 32..95; each glyph is 7 rows of 5 bits (MSB = leftmost).\n")
        f.write("inline const kv::u8 k_font[64][7] = {\n")
        for code in range(32, 96):
            g = FONT.get(chr(code), FONT['?'])
            rows = [int(r, 2) for r in g]
            f.write("{%s}, // '%s'\n" % (",".join(str(r) for r in rows), chr(code) if chr(code) not in "\\" else "bs"))
        f.write("};\n\n} // namespace sr\n")
    write_preview(karts, kpals, portraits, sets, props)
    print("wrote", OUT_BIN, "(%.1f MB)," % (os.path.getsize(OUT_BIN)/1e6), OUT_H, "and", PREVIEW, "in %.1fs" % (time.time() - t0))

def _paste(sheet, img, pal, x, y):
    img = np.asarray(img)
    H, W = img.shape
    for yy in range(H):
        for xx in range(W):
            v = int(img[yy, xx])
            if v and 0 <= x+xx < sheet.width and 0 <= y+yy < sheet.height:
                sheet.putpixel((x+xx, y+yy), pal[v])

def write_preview(karts, kpals, portraits, sets, props):
    BG = (40, 90, 60, 255)
    # rows: 8 racers x 24 angles + portrait column; set pieces at 4 angles; props
    sp_show = []
    for sp in sets:
        if sp["name"] in ROLLING: sel = list(range(4))
        elif sp["angles"] == 1: sel = [0]
        else:
            a = sp["angles"]; sel = [0, a//8, a//4, 3*a//8, a//2, 3*a//4]
        sp_show.append((sp, sel))
    width = SPR*25 + 8
    rows_h = SPR*8 + 8
    y = rows_h
    layout = []; x = 0; rowh = 0
    for sp, sel in sp_show:
        wtot = (sp["w"] + 2)*len(sel)
        if x + wtot > width: x = 0; y += rowh + 4; rowh = 0
        layout.append((sp, sel, x, y)); x += wtot + 6; rowh = max(rowh, sp["h"])
    y += rowh + 6
    px_y = y; x = 0; rowh = 0; plays = []
    for name, s in props.items():
        if x + s["w"] > width: x = 0; y += rowh + 3; rowh = 0
        plays.append((s, x, y)); x += s["w"] + 3; rowh = max(rowh, s["h"])
    y += rowh + 2
    sheet = Image.new("RGBA", (width, y), BG)
    for i, frames in enumerate(karts):
        for k, img in enumerate(frames):
            _paste(sheet, img, kpals[i], k*SPR, i*SPR)
        _paste(sheet, np.array(portraits[i]["px"]), portraits[i]["pal"], 24*SPR + 8, i*SPR)
    for sp, sel, x0, y0 in layout:
        for j, k in enumerate(sel):
            _paste(sheet, sp["imgs"][k], sp["pal"], x0 + j*(sp["w"] + 2), y0)
    for s, x0, y0 in plays:
        _paste(sheet, np.array(s["px"]), s["pal"], x0, y0)
    sheet.resize((sheet.width*2, sheet.height*2), Image.NEAREST).save(PREVIEW)

if __name__ == "__main__":
    main()
