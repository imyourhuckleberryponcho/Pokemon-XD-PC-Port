"""usum-look: eyes / blinks, sizes and hover rules for the Ultra Sun / Ultra Moon converter (a stage of gf_to_xd.convert).

Kept apart from gf_to_xd.py; gf_to_xd calls into it when opts['eyes'] / opts['size_rule'] / opts['hover_rule'] ask.

EYES. A 3DS Pokemon's eyes are one picture (4 rows of eye states; the Eye material) and the battle motions slide its UV
in 0.25 steps (row 0 = open at the idle's value, 0.25 = half shut, 0.5 = shut, 0.75 = pained / fainted, the 'tx' value picks
a left / right column set: angry, normal). XD draws blinks and sleeping eyes through its own mechanism: the PKX header's
four face records (sleep, wake up, wink, -) each name an animation slot; the game plays that slot on the model now and then,
and the slot's animation holds nothing but texture slides on the eye material (HSD texture tracks U / V, step keys).
We do the same: the picture slides are made from the 3DS values (never invented positions), the wink / sleep / wake slots
are made from the open and shut rows the Pokemon's own motions use (a 4-frame open, half, shut, half, open like XD's own
Bulbasaur), and every battle slot carries the eye state of the 3DS motion that plays there (hit = pained, faint = shut
...), constant open in the idle, so a slot never leaves the eyes in the previous slot's state.
"""
import numpy as np

import gf_read

T_TRAU, T_TRAV = 2, 3
OP_CON = 1

SIGN_U = 1.0
SIGN_V = 1.0

SKIP_MATS = ('Fire', 'Flame', 'Aura', 'Glow')


def animated_materials(mots):
    """Names of the materials whose unit-0 UV slides in any of the motions (eyes, mouths), effect pieces left out."""
    names = []
    for mo in mots.values():
        for mm in mo['mats']:
            if mm['unit'] != 0 or any(s in mm['name'] for s in SKIP_MATS):
                continue
            if (mm['tx'] or mm['ty']) and mm['name'] not in names:
                if any(len({round(gf_read.herp(ch, f) or 0.0, 3) for f in range(mo['frames'] + 1)}) > 1 for ch in (mm['tx'], mm['ty']) if ch):
                    names.append(mm['name'])
    return names


def uv_at(mo, name, f, base):
    """(tx, ty) of a material's unit-0 UV translation in motion mo at source frame f (channels without keys keep the base)."""
    for mm in mo['mats']:
        if mm['name'] == name and mm['unit'] == 0:
            a = gf_read.herp(mm['tx'], f)
            b = gf_read.herp(mm['ty'], f)
            return (base[0] if a is None else a, base[1] if b is None else b)
    return base


def uv_base(idle, name):
    """The picture position the converter bakes into the vertices (the idle's first frame)."""
    tx = ty = 0.0
    for mm in idle['mats']:
        if mm['name'] == name and mm['unit'] == 0:
            a = gf_read.herp(mm['tx'], 0)
            b = gf_read.herp(mm['ty'], 0)
            tx = 0.0 if a is None else a
            ty = 0.0 if b is None else b
    return tx, ty


def shut_levels(mots, name, base):
    """(open, half, shut) values of the V slide for this Pokemon's eye. The Pokemon's own blink motion (mots['_blink'], else
    the other motions) gives the rows: open = the idle's, shut = the furthest row from open below 0.7 (0.5, the plain shut
    line; 0.75 is the pained / fainted lid), half = a row between (or half way)."""
    op = base[1]
    src = [mots['_blink']] if '_blink' in mots else [m for k, m in mots.items() if k != 'idle']
    vals = set()
    for mo in src:
        for f in range(mo['frames'] + 1):
            vals.add(round(uv_at(mo, name, f, base)[1], 3))
    cand = [v for v in vals if abs(v - op) > 0.01 and v < 0.7]
    shut = max(cand, key=lambda v: abs(v - op)) if cand else op
    between = [v for v in vals if min(op, shut) + 0.01 < v < max(op, shut) - 0.01]
    half = min(between, key=lambda v: abs(v - (op + shut) / 2)) if between else (op + shut) / 2
    return op, half, shut


def step_keys(v):
    """Values per frame (len n + 1) -> HSD keys holding each value until the next change."""
    runs = []
    start = 0
    for f in range(1, len(v) + 1):
        if f == len(v) or abs(v[f] - v[start]) > 1e-4:
            runs.append((v[start], f - start))
            start = f
    keys = [(OP_CON, val, 0.0, n) for val, n in runs]
    keys[-1] = (OP_CON, keys[-1][1], 0.0, max(keys[-1][3] - 1, 1))
    keys.append((OP_CON, keys[-1][1], 0.0, 0))
    return keys


def wink_pattern(n, kind, op, half, shut):
    """V values for frames 0..n of a face slot of n frames. wink: open, half, shut, half (like XD's 4-frame blink), open at
    the end; sleep: open to shut (held); wake: shut to open."""
    if kind == 'wink':
        lv = [op, half, shut, half] if n >= 4 else [half, shut][:max(n, 1)]
        out = [lv[min(int(k * len(lv) / max(n, 1)), len(lv) - 1)] for k in range(n)]
        return out + [op]
    ramp = []
    for k in range(n + 1):
        t = k / float(max(n, 1))
        ramp.append(op if t < 0.34 else half if t < 0.67 else shut)
    ramp[-1] = shut
    if kind == 'sleep':
        return ramp
    return ramp[::-1]


class EyePlan:
    """Everything about the eye slides of one model: which materials, their base position, the shut levels."""

    def __init__(self, gm, mots, sx_sy):
        self.mots = mots
        self.names = animated_materials(mots)
        self.base = {n: uv_base(mots['idle'], n) for n in self.names}
        self.sxy = sx_sy
        self.levels = {n: shut_levels(mots, n, self.base[n]) for n in self.names}

    def role_values(self, role, src_frames):
        """{material: (U values, V values)} for the output frames whose source frames are given (retimed motions)."""
        mo = self.mots.get(role) or self.mots['idle']
        out = {}
        for n in self.names:
            sx, sy = self.sxy[n]
            b = self.base[n]
            us, vs = [], []
            for f in src_frames:
                tx, ty = uv_at(mo, n, f, b)

                tx, ty = _snap(tx, b[0]), _snap(ty, b[1])
                us.append(SIGN_U * sx * (tx - b[0]))
                vs.append(SIGN_V * sy * (ty - b[1]))
            out[n] = (us, vs)
        return out

    def face_values(self, kind, n):
        out = {}
        for nm in self.names:
            if 'Eye' not in nm and 'Iris' not in nm:
                continue
            sx, sy = self.sxy[nm]
            op, half, shut = self.levels[nm]
            b = self.base[nm]
            v = wink_pattern(n, kind, op, half, shut)
            out[nm] = ([0.0] * (n + 1), [SIGN_V * sy * (x - b[1]) for x in v])
        return out


def _snap(v, ref):
    """Eye rows sit on a 0.25 grid: values between two rows pick the nearer one (a step, not a blend)."""
    return round(v * 4.0) / 4.0






FIT_A, FIT_B = -1.5484, 0.8229


def mainline_box_height(height_cm):
    import math
    return math.exp(FIT_A + FIT_B * math.log(max(height_cm, 10)))


def mean_idle_height(sk, gm, idle, used_of, h0):
    """Height of the 3DS model averaged over the idle (XD's number is a mean over its idle too): a pose with raised arms or
    a stretched neck at frame 0 does not decide the size."""
    import gf_pose
    hs = [h0]
    n = max(idle['frames'], 1)
    for f in (n * 0.25, n * 0.5, n * 0.75):
        s, r, t, ax = gf_pose.sample_motion(sk, idle, f)
        W = sk.world(s, r, t, ax)
        ys = []
        for me in gm['meshes']:
            for sm in me['subs']:
                if (me['name'] in idle['vis'] and not idle['vis'][me['name']][0]) or me.get('hidden'):
                    continue
                ys.append(gf_pose.skin_points(sk, W, sm['bone_idx'], sm['verts'])[used_of(sm)][:, 1])
        y = np.concatenate(ys)
        hs.append(float(y.max() - y.min()))
    return float(np.mean(hs))



HOVER_MIN_FRACTION = 0.12


def model_hover(ymin, height, size_factor):
    """Hover height in XD units for a species with no XD model: the 3DS idle's own lowest point when it is clearly off the
    floor, else 0 (a walker; the converter's grounding then keeps the feet on the floor)."""
    return ymin * size_factor if height > 0 and ymin / height > HOVER_MIN_FRACTION else 0.0
