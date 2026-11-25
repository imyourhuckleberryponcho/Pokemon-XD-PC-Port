"""usum-iris: the eye's iris, baked into the eye picture (a stage of gf_to_xd.make_images).

On the 3DS an eye is two draws: the plate with the eye picture (4 rows of eye states, the left / right column set picked by the
UV slide) and, on top of it, an "Iris" plate (LIris / RIris): a small iris picture tiled over the plate, shown only where the
eye's own mask picture (the 'Nor' picture, its alpha = the lens-shaped opening of each eye state) lets it through. XD has no
second picture to mask with: the iris plate drawn as it is covers the white of the eye, lies on the same surface as the plate
(parts cut off by the depth test) and shows through closed eyelids (Zoroark: a teal ring floating beside the eye).

So the iris is painted into the eye picture itself, once, for every state the picture has: for each slide state (column set,
row) the plate's UV area is walked in small steps, each step gives the place in the eye picture, the iris picture's colour and
the mask's alpha; the weighted result is blended into the eye picture. The iris plates are then not drawn at all.
Whole numbers for the sums, plain double arithmetic for the coordinates (the same in engine/formats/n3ds/gf_iris.cpp).
"""
import math

import numpy as np

import gf_read


def _wrap(t, mode):
    """Picture coordinate -> 0..1 by the 3DS wrap mode (2 repeat, 3 mirrored repeat, else clamp)."""
    if mode == 2:
        return t - np.floor(t)
    if mode == 3:
        m = t - 2.0 * np.floor(t / 2.0)
        return np.where(m > 1.0, 2.0 - m, m)
    return np.minimum(np.maximum(t, 0.0), 1.0)


def _texel(f, n):
    return np.minimum(np.floor(f * n).astype(np.int64), n - 1)


def mesh_bbox(gm, mat_name):
    """(u0, u1, v0, v1) of the UV of every drawn piece of a material, None if it has none."""
    lo = hi = None
    for me in gm['meshes']:
        for sm in me['subs']:
            if sm['name'] != mat_name or 'uv0' not in sm['verts']:
                continue
            tri = gf_read.triangles(sm)
            if not len(tri):
                continue
            uv = sm['verts']['uv0'][np.unique(tri)][:, :2]
            a, b = uv.min(axis=0), uv.max(axis=0)
            lo = a if lo is None else np.minimum(lo, a)
            hi = b if hi is None else np.maximum(hi, b)
    if lo is None:
        return None
    return (float(lo[0]), float(hi[0]), float(lo[1]), float(hi[1]))


def _coord(mt, unit):
    for c in mt['coords']:
        if c['unit'] == unit:
            return c
    return None


def plan(gm, texs, colour_coord):
    """{eye material name: [iris material names]} for the models whose iris plates can be baked into an eye plate."""
    irises, eyes = {}, {}
    for mt in gm['materials']:
        n = mt['name']
        co = colour_coord(mt)
        if co is None or co['name'] not in texs:
            continue
        if 'Iris' in n:
            bb = mesh_bbox(gm, n)
            if bb:
                irises[n] = bb
        elif 'Eye' in n:
            bb = mesh_bbox(gm, n)
            if bb:
                eyes[n] = bb
    out = {}
    for iname, ib in irises.items():
        best, area = None, 0.0
        for ename, eb in eyes.items():
            du = min(ib[1], eb[1]) - max(ib[0], eb[0])
            dv = min(ib[3], eb[3]) - max(ib[2], eb[2])
            if du > 0 and dv > 0 and du * dv > area:
                best, area = ename, du * dv
        if best is not None:
            out.setdefault(best, []).append(iname)
    return out


def bakeable(eye_co, w, h):
    sx, sy = eye_co['scale']
    return sy == 1.0 and sx in (1.0, 2.0) and h >= 64 and w >= 8


def bake(eye, eye_co, irises):
    """eye: uint8 [h, w, 4] (the eye picture as it will be encoded); eye_co: the plate's unit-0 coordinate entry;
    irises: list of dicts tile (uint8 [h, w, 4]), co (unit-0 entry), mask (uint8 [h, w, 4] or None), mco, bbox.
    -> a new uint8 [h, w, 4]."""
    h, w = eye.shape[:2]
    sx, sy = eye_co['scale']
    etx, ety = eye_co['trans']
    wu, wv = eye_co['wrap']
    nh = int(round(sx))
    rows = max(1, h // 64)
    acc = np.zeros((h * w, 3), np.int64)
    sum_a = np.zeros(h * w, np.int64)
    cnt = np.zeros(h * w, np.int64)
    for ir in irises:
        u0, u1, v0, v1 = ir['bbox']
        nu = max(2, int(math.ceil((u1 - u0) * sx * w * 2.0)))
        nv = max(2, int(math.ceil((v1 - v0) * sy * h * 2.0)))
        su, sv = (u1 - u0) / nu, (v1 - v0) / nv
        ug = u0 + (np.arange(nu) + 0.5) * su
        vg = v0 + (np.arange(nv) + 0.5) * sv
        U, V = np.meshgrid(ug, vg)
        ico, tile = ir['co'], ir['tile']
        th, tw = tile.shape[:2]
        ti = _texel(_wrap(ico['scale'][0] * (U - ico['trans'][0]), ico['wrap'][0]), tw)
        tj = _texel(_wrap(ico['scale'][1] * (V - ico['trans'][1]), ico['wrap'][1]), th)
        trgba = tile[tj, ti].astype(np.int64)
        for hh in range(nh):
            txs = -hh / sx if sx else 0.0
            for kk in range(rows):
                tys = kk / (rows * sy)
                ex = _texel(_wrap(sx * (U - (etx + txs)), wu), w)
                ey = _texel(_wrap(sy * (V - (ety + tys)), wv), h)
                a = trgba[..., 3]
                if ir['mask'] is not None:
                    mco, mk = ir['mco'], ir['mask']
                    mh, mw = mk.shape[:2]
                    mi = _texel(_wrap(mco['scale'][0] * (U - (mco['trans'][0] + txs)), mco['wrap'][0]), mw)
                    mj = _texel(_wrap(mco['scale'][1] * (V - (mco['trans'][1] + tys)), mco['wrap'][1]), mh)
                    a = a * mk[mj, mi][..., 3].astype(np.int64)
                else:
                    a = a * 255
                idx = (ey * w + ex).ravel()
                af = a.ravel()
                np.add.at(cnt, idx, 1)
                np.add.at(sum_a, idx, af)
                for k in range(3):
                    np.add.at(acc[:, k], idx, af * trgba[..., k].ravel())
    out = eye.copy().reshape(h * w, 4)
    den = cnt * 65025
    hit = (cnt > 0) & (sum_a > 0)
    base = out[:, :3].astype(np.int64)
    new = (base * (den - sum_a)[:, None] + acc + (den // 2)[:, None]) // np.maximum(den, 1)[:, None]
    out[hit, :3] = new[hit].astype(np.uint8)
    return out.reshape(h, w, 4)


def round5(rgba):
    """uint8 [h, w, 4] -> the colours rounded to 5 bits and spread back to 8 (the same numbers the GX 5-bit decoder makes), alpha 255."""
    out = rgba.copy()
    v5 = (out[..., :3].astype(np.int32) * 31 + 127) // 255
    out[..., :3] = ((v5 << 3) | (v5 >> 2)).astype(np.uint8)
    out[..., 3] = 255
    return out
