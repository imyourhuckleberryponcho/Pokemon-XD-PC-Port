"""usum_recolour: per-species colour corrections for Ultra Sun / Ultra Moon pictures that come out wrong in XD's plain light.

The 3DS shaders of a few models mix the colour picture with constants, a second picture and lookup ramps that this converter does not
copy; the plain picture then shows the wrong colour (Voltorb salmon, Houndour grey-brown, Gligar pink, Jumpluff beige, Starmie's gem dull).
Each entry below says which species and which material, and how to move the colours. Pure picture changes: no model or game change.
"""
import numpy as np


def _hsv(f):
    mx, mn = f.max(axis=2), f.min(axis=2)
    d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0.0)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    dd = np.maximum(d, 1e-6)
    h = np.where(mx == r, ((g - b) / dd) % 6, np.where(mx == g, (b - r) / dd + 2, (r - g) / dd + 4)) * 60.0
    return h, s, mx


def _rgb(h, s, v):
    c = v * s
    hp = (h % 360.0) / 60.0
    x = c * (1 - np.abs(hp % 2 - 1))
    z = np.zeros_like(c)
    sel = [(hp < 1), (hp < 2), (hp < 3), (hp < 4), (hp < 5), (hp >= 5)]
    rs = np.select(sel, [c, x, z, z, x, c])
    gs = np.select(sel, [x, c, c, x, z, z])
    bs = np.select(sel, [z, z, x, c, c, x])
    m = (v - c)[..., None]
    return np.stack([rs, gs, bs], axis=2) + m


def retone(rgba, hue=None, hue_from=None, sat_mul=1.0, sat_min=0.0, val_mul=1.0, val_lift=0.0, min_sat=0.0, max_val=1.0, max_sat=1.0, min_val=0.0):
    """Move the colours of the pixels chosen by saturation / brightness (and, with hue_from = (lo, hi) degrees, by hue):
    hue set to `hue`, saturation * sat_mul (at least sat_min), brightness * val_mul + val_lift."""
    f = rgba[..., :3].astype(float) / 255.0
    h, s, v = _hsv(f)
    sel = (s >= min_sat) & (s <= max_sat) & (v <= max_val) & (v >= min_val)
    if hue_from is not None:
        lo, hi = hue_from
        sel &= (h >= lo) & (h <= hi) if lo <= hi else ((h >= lo) | (h <= hi))
    nh = np.where(sel, hue if hue is not None else h, h)
    ns = np.where(sel, np.clip(np.maximum(s * sat_mul, sat_min), 0, 1), s)
    nv = np.where(sel, np.clip(v * val_mul + val_lift, 0, 1), v)
    out = rgba.copy()
    out[..., :3] = np.clip(_rgb(nh, ns, nv) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return out


def whiten(rgba, k=0.85, floor=0.93):
    """Towards white, keeping a little of the picture's own shading (Jumpluff's cotton)."""
    f = rgba[..., :3].astype(float) / 255.0
    lum = (f @ np.array([0.30, 0.59, 0.11]))[..., None]
    shade = floor + (1 - floor) * np.clip(lum / max(float(lum.max()), 1e-6), 0, 1)
    out = rgba.copy()
    out[..., :3] = np.clip((f * (1 - k) + shade * k) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return out



FIXES = [

    ((100, 101), 'Body', lambda im: retone(im, hue=0.0, sat_mul=3.0, sat_min=0.82, val_mul=1.0, min_sat=0.18, val_lift=-0.04)),

    ((228, 229), 'Body', lambda im: retone(im, val_mul=0.42, max_sat=0.3, max_val=0.62)),

    ((207,), 'Body', lambda im: retone(im, hue=268.0, hue_from=(285.0, 350.0), sat_mul=0.95, min_sat=0.12, val_mul=0.88)),

    ((187, 188, 189), 'Cotton', lambda im: whiten(im)),

    ((120, 121), 'Hitodeman', lambda im: retone(im, hue=338.0, sat_mul=1.6, sat_min=0.8, val_mul=1.0, val_lift=0.35)),
]


def fix_for(nat, mat_name):
    """The list of picture functions that apply to this species' material (empty list = none)."""
    return [fn for nats, key, fn in FIXES if nat in nats and key in mat_name]
