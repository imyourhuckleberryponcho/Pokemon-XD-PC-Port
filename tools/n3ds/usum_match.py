"""usum_match: move a 3DS (Ultra Sun / Ultra Moon) model's colours towards XD's own model of the same species.

At convert time the converter has XD's own model of the species (read from the player's XD disc). Its pictures are measured
here (nothing of XD is stored anywhere): the main colour groups ("clusters") of the whole model, in the Lab colour space, each
with its share of the surface. The same is done for the 3DS pictures. Every 3DS colour group is matched to the nearest XD
colour group and moved towards it in lightness, colourfulness (chroma) and, a little, hue. Pixels in between are moved by a
smooth mix of the groups around them, so there are no hard edges.

Limits (all can be changed through the converter option dict, see DEFAULTS):
  * a species whose overall colour is already close to XD's (mean colour distance below `skip_de`) is not touched;
  * every move is capped (lightness +-`max_dl`, chroma factor `min_gain`..`max_gain`, hue +-`max_dh` degrees), so a pose or
    shape difference between the two models cannot drive a wild recolour;
  * only the main body pictures are changed: eyes, mouths, iris, small pieces, glow and effect pieces, and pieces that an
    earlier per-species fix (usum_recolour) already set are left alone.
Pure picture change: no model or game change. Used by gf_to_xd.make_images through opts['match'].
"""
import numpy as np

DEFAULTS = dict(strength=1.0, strength_l=0.3, strength_c=0.9, strength_h=0.5, k=5, skip_de=12.0, max_dl=12.0, min_gain=0.75, max_gain=1.7, max_dh=15.0, max_match=50.0, sigma=12.0,
                min_share=0.03, share_weight=12.0, max_hue_gap=45.0, min_piece=0.02, sample=6000)

_M = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
_WHITE = np.array([0.95047, 1.0, 1.08883])


def rgb_to_lab(rgb):
    """uint8 or float 0..255 array (..., 3) -> Lab (..., 3)."""
    c = np.asarray(rgb, float) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    xyz = (lin @ _M.T) / _WHITE
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16.0 / 116.0)
    return np.stack([116.0 * f[..., 1] - 16.0, 500.0 * (f[..., 0] - f[..., 1]), 200.0 * (f[..., 1] - f[..., 2])], axis=-1)


def lab_to_rgb(lab):
    """Lab (..., 3) -> float RGB 0..255 (clipped)."""
    fy = (lab[..., 0] + 16.0) / 116.0
    fx = fy + lab[..., 1] / 500.0
    fz = fy - lab[..., 2] / 200.0
    f = np.stack([fx, fy, fz], axis=-1)
    xyz = np.where(f ** 3 > 0.008856, f ** 3, (f - 16.0 / 116.0) / 7.787) * _WHITE
    lin = np.clip(xyz @ np.linalg.inv(_M).T, 0.0, 1.0)
    c = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * lin ** (1 / 2.4) - 0.055)
    return np.clip(c * 255.0, 0, 255)


def kmeans(lab, w, k, iters=14):
    """Weighted k-means in Lab, deterministic (farthest-point start). -> (centres (k, 3), share (k,))."""
    w = w / w.sum()
    cen = [(lab * w[:, None]).sum(axis=0)]
    d = ((lab - cen[0]) ** 2).sum(axis=1)
    for _ in range(1, k):
        i = int(np.argmax(d * (w > 0)))
        cen.append(lab[i].copy())
        d = np.minimum(d, ((lab - cen[-1]) ** 2).sum(axis=1))
    cen = np.array(cen)
    for _ in range(iters):
        dd = ((lab[:, None, :] - cen[None]) ** 2).sum(axis=2)
        a = dd.argmin(axis=1)
        for j in range(len(cen)):
            m = a == j
            if m.any() and w[m].sum() > 0:
                cen[j] = (lab[m] * w[m, None]).sum(axis=0) / w[m].sum()
    dd = ((lab[:, None, :] - cen[None]) ** 2).sum(axis=2)
    a = dd.argmin(axis=1)
    share = np.array([w[a == j].sum() for j in range(len(cen))])
    keep = share > 0
    return cen[keep], share[keep]


def sample_pixels(rgba, n):
    """Opaque pixels of a picture, at most about n of them, evenly spread -> (rgb float (m, 3))."""
    px = rgba.reshape(-1, 4)
    px = px[px[:, 3] > 128]
    if len(px) > n:
        px = px[np.linspace(0, len(px) - 1, n).astype(int)]
    return px[:, :3].astype(float)


class Pool:
    """Weighted colour pixels of a whole model: add pieces (rgb pixels + the surface share they stand for)."""

    def __init__(self):
        self.lab, self.w = [], []

    def add(self, rgb, share):
        if len(rgb) and share > 0:
            self.lab.append(rgb_to_lab(rgb))
            self.w.append(np.full(len(rgb), share / len(rgb)))

    def ok(self):
        return bool(self.lab)

    def arrays(self):
        return np.concatenate(self.lab), np.concatenate(self.w)


def mean_lab(pool):
    lab, w = pool.arrays()
    return (lab * w[:, None]).sum(axis=0) / w.sum()


def delta_e(a, b):
    return float(np.sqrt(((np.asarray(a) - np.asarray(b)) ** 2).sum()))


def _hue(a, b):
    return np.degrees(np.arctan2(b, a))


def _wrap(d):
    return (d + 180.0) % 360.0 - 180.0


class Plan:
    """The moves of one species: per 3DS colour group a lightness shift, a chroma factor and a hue shift."""

    def __init__(self, cen, dl, gain, dh, sigma, info):
        self.cen, self.dl, self.gain, self.dh, self.sigma, self.info = cen, dl, gain, dh, sigma, info

    def apply(self, rgba, skin_only=False):
        """skin_only: for face plates that carry the eyes: only the coloured, not too dark pixels move (the eye drawing stays as it is)."""
        f = rgba[..., :3].astype(float)
        lab = rgb_to_lab(f).reshape(-1, 3)
        d2 = ((lab[:, None, :] - self.cen[None]) ** 2).sum(axis=2)
        wt = np.exp(-(d2 - d2.min(axis=1, keepdims=True)) / (2.0 * self.sigma ** 2))
        wt /= wt.sum(axis=1, keepdims=True)
        dl = wt @ self.dl
        gain = wt @ self.gain
        dh = wt @ self.dh
        L = np.clip(lab[:, 0] + dl, 0.0, 100.0)
        a, b = lab[:, 1], lab[:, 2]
        c = np.hypot(a, b) * gain
        h = np.radians(_hue(a, b) + dh)
        out = rgba.copy()
        new = lab_to_rgb(np.stack([L, c * np.cos(h), c * np.sin(h)], axis=-1)).reshape(f.shape)
        if skin_only:
            chroma = np.hypot(a, b)
            w = (np.clip((chroma - 8.0) / 8.0, 0.0, 1.0) * np.clip((lab[:, 0] - 30.0) / 10.0, 0.0, 1.0)).reshape(f.shape[:2] + (1,))
            new = f + (new - f) * w
        out[..., :3] = (new + 0.5).astype(np.uint8)
        return out


def make_plan(us, xd, opt=None, report=None):
    """us, xd: Pool of the 3DS pictures (after the converter's own tweaks) and of XD's pictures -> Plan, or None when nothing
    should change. plan.info says what was measured (the converter logs it)."""
    o = dict(DEFAULTS, **(opt or {}))
    if not us.ok() or not xd.ok():
        return None
    ul, uw = us.arrays()
    xl, xw = xd.arrays()
    mu, mx = (ul * uw[:, None]).sum(axis=0) / uw.sum(), (xl * xw[:, None]).sum(axis=0) / xw.sum()
    de0 = delta_e(mu, mx)
    info = report if report is not None else {}
    info.update(de_before=round(de0, 1), mean_us=[round(float(v), 1) for v in mu], mean_xd=[round(float(v), 1) for v in mx])
    if de0 < o['skip_de']:
        info['why'] = 'close already'
        return None
    uc, ush = kmeans(ul, uw, o['k'])
    xc, xsh = kmeans(xl, xw, o['k'])
    big = xsh >= 0.04
    xc, xsh = xc[big], xsh[big]
    n = len(uc)
    dl, gain, dh = np.zeros(n), np.ones(n), np.zeros(n)
    s = float(o['strength'])
    sl, sc, sh = s * o['strength_l'], s * o['strength_c'], s * o['strength_h']
    for j in range(n):
        if ush[j] < o['min_share']:
            continue
        d = np.sqrt((xc[:, 1] - uc[j, 1]) ** 2 + (xc[:, 2] - uc[j, 2]) ** 2 + (0.6 * (xc[:, 0] - uc[j, 0])) ** 2)
        cost = d + o['share_weight'] * np.abs(np.log(np.maximum(xsh, 1e-3) / ush[j]))
        cu_ = float(np.hypot(uc[j, 1], uc[j, 2]))
        if cu_ >= 15.0:
            xh = np.hypot(xc[:, 1], xc[:, 2])
            dh_ = np.abs(_wrap(_hue(xc[:, 1], xc[:, 2]) - _hue(uc[j, 1], uc[j, 2])))
            cost = np.where((xh >= 15.0) & (dh_ > o['max_hue_gap']), 1e9, cost)
        i = int(cost.argmin())
        if cost[i] >= 1e9 or d[i] > o['max_match']:
            continue
        dl[j] = float(np.clip(xc[i, 0] - uc[j, 0], -o['max_dl'], o['max_dl'])) * sl
        cu, ct = float(np.hypot(uc[j, 1], uc[j, 2])), float(np.hypot(xc[i, 1], xc[i, 2]))
        if cu >= 6.0:
            gain[j] = 1.0 + (float(np.clip(ct / cu, o['min_gain'], o['max_gain'])) - 1.0) * sc
            if cu >= 10.0 and ct >= 10.0:
                dh[j] = float(np.clip(_wrap(_hue(xc[i, 1], xc[i, 2]) - _hue(uc[j, 1], uc[j, 2])), -o['max_dh'], o['max_dh'])) * sh
    plan = Plan(uc, dl, gain, dh, o['sigma'], info)

    probe = np.clip(lab_to_rgb(ul[:: max(1, len(ul) // 4000)]), 0, 255)
    moved = plan.apply(np.concatenate([probe, np.full((len(probe), 1), 255.0)], axis=1).astype(np.uint8).reshape(-1, 1, 4))
    ml = rgb_to_lab(moved[..., :3].reshape(-1, 3))
    wsub = uw[:: max(1, len(ul) // 4000)][: len(ml)]
    mm = (ml * wsub[:, None]).sum(axis=0) / wsub.sum()
    info['de_after'] = round(delta_e(mm, mx), 1)
    info['groups'] = [dict(us=[round(float(v), 1) for v in uc[j]], share=round(float(ush[j]), 2), dl=round(float(dl[j]), 1), gain=round(float(gain[j]), 2),
                           dh=round(float(dh[j]), 1)) for j in range(n)]
    if info['de_after'] >= de0:
        info['why'] = 'no gain'
        return None
    return plan
