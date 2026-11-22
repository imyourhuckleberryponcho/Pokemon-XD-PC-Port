"""usum_hand: the hand-set colour table for single Ultra Sun / Ultra Moon species (numbers only, no pictures, no game data).

Why: the automatic per-species colour match (usum_match) is right for most species and a poor guess for a few. For those the table
below can (a) turn the match off for the species and / or (b) apply a hand-set correction to one colour group.

Every row of RULES is one colour group of one species:
    (species number, 'part of the material name' or '*' for all body pieces, who, what)
  who  = which pixels, by their colour in the Lab colour space (the converter's own colour measure):
           hue = (from, to) degrees (0 = red-magenta, 40 = red-orange, 70 = orange, 95 = yellow, 140 = green, 270 = blue, 310 = purple);
           L = (from, to) lightness 0..100; C = (from, to) colourfulness. Left out = any. Edges are feathered, no hard seams.
  what = dL (lighter + / darker -), gain (colourfulness x), dh (hue shift in degrees, + goes red -> yellow -> green).
'*' never touches iris, line, tongue or mouth pieces; a part named 'Mouth' only if a rule names it, or the rule says '*+Mouth'
(all body pieces and the mouth plate: for a species whose mouth plate carries a patch of face skin, Koffing, Whismur).
Per species (SPECIES): match = False turns the automatic match off; match_plates = names of face plates whose skin follows the match.
After the match and the light tweak, rules are applied in the order written here.

Target colours: XD's own look by default (what the game shows), checked against the current official artwork; where they differ the
note says so. Only numbers are kept here.
"""
import numpy as np
import usum_match as UM


SPECIES = {
    25: dict(name='Pikachu', match=True, match_plates=('Mouth', 'BodyB'), note='face: muzzle and the plate round the cheek follow the body colour, cheeks red like XD, ear tips black like XD. Official art is a paler yellow than XD; XD followed'),
    28: dict(name='Sandslash', match=False, note='the match made it paler-tan; XD is a bright yellow, official art is tan-brown; XD followed in part (a little more colour)'),
    94: dict(name='Gengar', match=True, note='XD is a very dark purple, official art a middle purple; darker than USUM, not as dark as XD'),
    109: dict(name='Koffing', match=False, note='XD a deep violet, official art a greyer mid violet; about two thirds of the way to XD. Match off: it left a pale patch on the face'),
    110: dict(name='Weezing', match=True, note='XD a dark mauve, official art a mid mauve-grey; between the two'),
    178: dict(name='Xatu', match=False, note='the match made the greens neon; the unmatched colours are close to XD and to official art'),
    213: dict(name='Shuckle', match=False, note='official art: red-orange shell, XD: dark crimson; hue moved about halfway to XD'),
    216: dict(name='Teddiursa', match=False, note='XD and official art agree (salmon-orange); the match was too orange'),
    237: dict(name='Hitmontop', match=False, note='unmatched was already close'),
    250: dict(name='Ho-Oh', match=False, note='several colour areas that the match could not pair up; unmatched is close'),
    251: dict(name='Celebi', match=True, note='official art is a pale mint, XD a vivid lime green; went part of the way to XD'),
    51: dict(name='Dugtrio', match=True, note='once-over: USUM is a light tan, XD and official art a darker brown; darker and a little more colour'),
    100: dict(name='Voltorb', match=True, note='once-over: the face plate was a pale salmon patch on the red ball; now the red of the body (official art has a pink gloss, XD a plain red)'),
    172: dict(name='Pichu', match=True, note='once-over: ear tips were mid grey; XD and official art black'),
    197: dict(name='Umbreon', match=True, glow=0.35, note='once-over: the ring glow was added at full strength and turned the yellow rings white; XD and official art have gold rings'),
    256: dict(name='Combusken', match=False, note='the match turned the orange to brown; XD and official art both have orange legs and a yellow body'),
    258: dict(name='Mudkip', match=False, note='official art is a light blue, XD a deeper blue; about 60 per cent towards XD'),
    293: dict(name='Whismur', match=False, note='XD lilac-blue, official art pink-lilac; went to the middle. Ears: pale yellow in both'),
    308: dict(name='Medicham', match=False, note='XD hot pink-magenta, official art a softer pink; most of the way to XD'),

    687: dict(name='Malamar', match=False, note='gen57: pale blue body pictures; official art is deep purple-navy: darker and shifted towards purple'),
    639: dict(name='Terrakion', match=False, note='gen57: pale grey body darkened a little towards official art'),
    883: dict(name='Marshadow', match=False, note='gen57: the body-A picture is a white mask (smoke / hair); darkened to a slate like official art'),
    874: dict(name='Lunala', match=False, note='gen57: the wing membrane picture is a reflection map (teal / magenta); flat deep purple like official art'),
    316: dict(name='Gulpin', match=False, note='unmatched was already close'),
}

RULES = [

    (25, 'BodyB', dict(C=(18, 80), hue=(10, 70)), dict(dL=-9, gain=1.8, dh=5)),
    (25, 'BodyA', dict(L=(0, 42), C=(0, 12)), dict(dL=-22, gain=1.0, dh=0)),
    (28, '*', dict(hue=(70, 115), C=(15, 80)), dict(dL=-5, gain=1.2, dh=0)),
    (94, '*', dict(hue=(270, 360), L=(30, 85), C=(10, 60)), dict(dL=-14, gain=0.85, dh=0)),
    (109, '*+Mouth', dict(hue=(270, 350), L=(45, 95), C=(5, 45)), dict(dL=-19, gain=1.5, dh=8)),
    (109, '*+Mouth', dict(hue=(270, 350), L=(10, 45), C=(3, 40)), dict(dL=-6, gain=1.3, dh=6)),
    (110, '*', dict(hue=(270, 360), L=(35, 95), C=(5, 40)), dict(dL=-13, gain=1.3, dh=4)),
    (213, '*', dict(hue=(5, 55), C=(30, 90), L=(25, 62)), dict(dL=-8, gain=1.0, dh=-12)),
    (216, '*', dict(hue=(30, 90), C=(15, 90)), dict(dL=-3, gain=1.0, dh=-9)),
    (51, '*', dict(hue=(40, 100), C=(15, 70), L=(40, 95)), dict(dL=-10, gain=1.25, dh=-6)),
    (100, '*', dict(hue=(10, 70), L=(55, 92), C=(14, 58)), dict(dL=-19, gain=2.5, dh=0)),
    (172, 'BodyA', dict(L=(0, 52), C=(0, 14)), dict(dL=-22, gain=1.0, dh=0)),
    (251, '*', dict(hue=(95, 175), C=(8, 60), L=(55, 100)), dict(dL=0, gain=1.35, dh=0)),
    (256, '*', dict(hue=(35, 80), C=(15, 80), L=(40, 82)), dict(dL=-5, gain=1.5, dh=0)),
    (256, '*', dict(hue=(80, 118), C=(15, 70), L=(65, 100)), dict(dL=2, gain=1.35, dh=6)),
    (258, '*', dict(hue=(235, 290), C=(8, 70)), dict(dL=-7, gain=1.5, dh=0)),
    (293, '*+Mouth', dict(hue=(280, 360), C=(5, 40), L=(60, 100)), dict(dL=-6, gain=1.9, dh=-12)),
    (293, '*+Mouth', dict(hue=(60, 112), C=(15, 70), L=(65, 98)), dict(dL=9, gain=0.9, dh=12)),
    (308, '*', dict(hue=(-20, 35), C=(15, 70), L=(40, 85)), dict(dL=-8, gain=1.4, dh=-7)),


    (687, '*', dict(hue=(200, 285), L=(45, 100), C=(5, 60)), dict(dL=-38, gain=1.3, dh=22)),

    (639, '*', dict(L=(55, 95), C=(0, 18)), dict(dL=-10, gain=1.0, dh=0)),
    (883, 'BodyA', dict(L=(55, 100)), dict(dL=-60, gain=1.0, dh=0)),
]

def flat_for(nat, mat_name):
    """A flat colour (r, g, b) for a material whose picture is only a reflection / environment map on the 3DS (Lunala's wing membrane:
    the map is a teal / magenta sunburst, the wing is a plain deep purple), or None. Table: FLAT, keyed by model number + part of the material name."""
    for n, key, rgb in FLAT:
        if n == nat and key in mat_name:
            return rgb
    return None



FLAT = [
    (874, 'BodyB', (62, 40, 116)),
]

SKIP_STAR = ('Iris', 'Line', 'Tongue', 'Mouth')


def match_enabled(nat, default=True):
    """False when the table turns the automatic match off for this species."""
    row = SPECIES.get(nat)
    if row is None or row.get('match', True):
        return default
    return False


def glow_strength(nat):
    """How strongly the glow picture (Inc) is added to the body colour: 1.0 = as the converter's own rule decides; a number = fixed."""
    row = SPECIES.get(nat)
    return float(row['glow']) if row and 'glow' in row else 1.0


def plates_follow_match(nat, mat_name):
    row = SPECIES.get(nat)
    return bool(row and any(k in mat_name for k in row.get('match_plates', ())))


def rules_for(nat, mat_name):
    out = []
    for n, key, who, what in RULES:
        if n != nat:
            continue
        if key in ('*', '*+Mouth'):
            if any(k in mat_name for k in (SKIP_STAR if key == '*' else SKIP_STAR[:3])):
                continue
        elif key not in mat_name:
            continue
        out.append((who, what))
    return out


def _box(x, rng, feather):
    if rng is None:
        return 1.0
    lo, hi = rng
    return np.clip((x - (lo - feather)) / feather, 0, 1) * np.clip(((hi + feather) - x) / feather, 0, 1)


def _hue_box(h, rng, feather=12.0):
    if rng is None:
        return 1.0
    lo, hi = rng
    mid, half = (lo + hi) / 2.0, (hi - lo) / 2.0
    d = np.abs((h - mid + 180.0) % 360.0 - 180.0)
    return np.clip((half + feather - d) / feather, 0, 1)


def tune(rgba, rules):
    """Apply the (who, what) rules to an RGBA picture; alpha untouched."""
    if not rules:
        return rgba
    f = rgba[..., :3].astype(float)
    lab = UM.rgb_to_lab(f).reshape(-1, 3)
    L, a, b = lab[:, 0].copy(), lab[:, 1].copy(), lab[:, 2].copy()
    for who, what in rules:
        C = np.hypot(a, b)
        h = np.degrees(np.arctan2(b, a)) % 360.0
        w = _box(L, who.get('L'), 6.0) * _box(C, who.get('C'), 4.0) * _hue_box(h, who.get('hue'))
        nL = np.clip(L + what.get('dL', 0.0), 0.0, 100.0)
        nC = C * what.get('gain', 1.0)
        nh = np.radians(h + what.get('dh', 0.0))
        L = L + (nL - L) * w
        na, nb = nC * np.cos(nh), nC * np.sin(nh)
        a, b = a + (na - a) * w, b + (nb - b) * w
    out = rgba.copy()
    out[..., :3] = (UM.lab_to_rgb(np.stack([L, a, b], axis=-1)).reshape(f.shape) + 0.5).astype(np.uint8)
    return out
