"""Prototype: a Gen 6/7 3DS Pokemon (GFModel + GFMotion, read by gf_read.py) as an XD battle model (PKX).

Follows the Battle Revolution converter's pattern (tools/pbr_convert/pbr_to_xd.py): the neutral model of model.py,
HSD written by hsd_write.py, the container (header, 17 sections) from XD's own file of the same Pokemon, so the
game's move scripts keep working. Choices:
- joints: XD's joint 0 and a plain joint 1 (the skeleton root, which carries the size), then every 3DS bone in
  depth-first order, then one holder joint per 3DS mesh (skinned meshes hang on holders under joint 1);
- skin: every vertex is skinned (a rigid piece = one bone, weight 1); weights in steps of 0.1, at most 4 bones;
- materials: ONE picture each (the colour map, texture unit 0); the 3DS's second picture (shade / emission),
  normal map, rim light, phong and the outline (edge) pass are dropped: XD's own lighting is used;
- eyes: the 3DS eye is an atlas picked by a UV animation: the idle's first frame picks the open eye (baked into the UVs);
  the eye slide (V offset of the Eye / Mouth / Iris materials) is written as XD material animations per slot, and the
  pure eye-slide motion '_blink' feeds XD's face records (blink / sleep / wake) where the slot is free;
- animation slots: XD's own table (which XD animation plays for which kind of move) is kept; each slot gets the
  3DS motion that fits its role (idle, hit, faint, physical, special); mode 'native' keeps the 3DS timing,
  mode 'xd' stretches each motion to the length of XD's animation in that slot and smooths it;
- NOT done: per-motion mesh visibility (the vines of Bulbasaur stay hidden), the
  second picture's shading, decimation (3DS models have 3 to 6 times XD's triangles).
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PBR = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', 'pbr_convert')
for p in (HERE, os.path.join(os.path.dirname(HERE), 'pbr_convert')):
    if p not in sys.path:
        sys.path.insert(0, p)

import gf_pose
import usum_fx
import usum_recolour
import usum_match
import usum_hand
import gf_read
import gx_tex
import hsd_read
import hsd_write
from model import (Model, Joint, Mesh, Vertex, Material, Texture, Image, Animation, AnimObj, Track, MatAnim, TexAnim,
                   mat_mul, mat_inv, J_ROTX, J_TRAX, J_SCAX, OP_CON, OP_LIN)
import pbr_to_xd as P
import decimate as DEC
import usum_look as LOOK
import usum_iris

FPS = 30.0
FACE_KINDS = ('sleep', 'wake', 'wink', None)
ROLE_OF_SECTION = {0: 'idle', 16: 'idle', 8: 'hit', 10: 'faint', 1: 'special', 6: 'special', 12: 'special',
                   13: 'special', 14: 'special', 15: 'special', 2: 'phys', 3: 'phys', 4: 'phys', 5: 'phys', 7: 'phys'}
GF_SLOT = {'idle': 0, 'hit': 16, 'faint': 17, 'phys': 8, 'special': 12}


def used_of(sm):
    """Indices of the vertices a submesh's triangles use (a shared vertex buffer holds the others' too)."""
    if 'used' not in sm:
        t = gf_read.triangles(sm)
        sm['used'] = np.unique(t) if len(t) else np.array([], dtype=np.int64)
    return sm['used']


def dfs_order(sk):
    nb = len(sk.names)
    kids = {}
    for i in range(nb):
        kids.setdefault(sk.parent[i], []).append(i)
    out = []

    def go(i):
        out.append(i)
        for c in kids.get(i, []):
            go(c)
    for r in kids.get(-1, []):
        go(r)
    return out


def fire_tint(rgba):
    """Grey flame intensity -> red at the edge, orange, yellow at the core (what the 3DS shader's colour constants do)."""
    lum = np.clip(rgba[..., 0].astype(float) / 255.0 * 1.7, 0.0, 1.0)[..., None]
    lo = np.array([235.0, 55.0, 15.0])
    mid = np.array([255.0, 150.0, 25.0])
    hi = np.array([255.0, 235.0, 120.0])
    c = np.where(lum < 0.5, lo + (mid - lo) * (lum / 0.5), mid + (hi - mid) * ((lum - 0.5) / 0.5))
    out = rgba.copy()
    out[..., :3] = np.clip(c, 0, 255).astype(np.uint8)
    out[..., 3] = 255
    return out


def is_lure_glow(mt):
    """The Chinchou / Lanturn lure piece: shader chonchie_Glow, its colour slot holds an environment glow disc."""
    return 'chonchie_Glow' in mt['shader']


def lure_fill(rgba):
    """Flat glowing pale yellow (the disc's bright core colour, a little lighter so XD's light cannot darken it to orange)."""
    out = rgba.copy()
    out[..., 0], out[..., 1], out[..., 2], out[..., 3] = 255, 250, 190, 255
    return out


def cheek_boost(rgba, k=1.0):
    """Face plates of the "Neon" shader (Pikachu line cheeks): the 3DS adds the glow picture on top, which makes the painted cells vivid; here the glow
    picture is a copy and adds nothing, so a pale pink cell (Pichu) stays dull. Pixels that differ from the plate's skin colour (its most common colour)
    get their saturation raised; already saturated cells (Pikachu's red) hardly change."""
    f = rgba[..., :3].astype(float)
    q = (f // 8).astype(int).reshape(-1, 3)
    key = q[:, 0] * 1024 + q[:, 1] * 32 + q[:, 2]
    vals, cnt = np.unique(key[rgba[..., 3].reshape(-1) > 128], return_counts=True)
    if len(vals) == 0:
        return rgba
    kk = int(vals[cnt.argmax()])
    skin = np.array([kk // 1024, (kk // 32) % 32, kk % 32], float) * 8.0 + 4.0
    dist = np.abs(f - skin).max(axis=2)
    w = np.clip((dist - 15.0) / 25.0, 0.0, 1.0)
    mx, mn = f.max(axis=2), f.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    gain = 1.0 + 1.4 * k * np.clip(1.0 - sat / 0.55, 0.0, 1.0)
    mean = f.mean(axis=2, keepdims=True)
    boosted = np.clip(mean + (f - mean) * gain[..., None], 0, 255)
    out = rgba.copy()
    out[..., :3] = (f + (boosted - f) * w[..., None] + 0.5).astype(np.uint8)
    return out


def light_tweak(rgba, k=1.0):
    """Subtle: the 3DS pictures are drawn for a shader that adds shade, light and rim; in XD's plain light they come out grey.
    A little lift (gamma) and a little more colour, no change of hue, nothing painted in."""
    f = rgba[..., :3].astype(float) / 255.0
    f = f ** (1.0 - 0.12 * k)
    lum = (f @ np.array([0.30, 0.59, 0.11]))[..., None]
    f = lum + (f - lum) * (1.0 + 0.14 * k)
    out = rgba.copy()
    out[..., :3] = np.clip(f * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return out


def sat_boost(rgba, k):
    """Pack-wide paleness test option (opts['sat_boost'], 0.25 = +25 % colour): every colour moves away from its own grey by (1 + k); grey stays grey.
    Off by default (a product decision)."""
    f = rgba[..., :3].astype(float)
    lum = (f @ np.array([0.30, 0.59, 0.11]))[..., None]
    out = rgba.copy()
    out[..., :3] = np.clip(lum + (f - lum) * (1.0 + k) + 0.5, 0, 255).astype(np.uint8)
    return out


KEEP_FIRE_MESH = (146,)
STEEL_SPECIES = (304, 305, 306)


def steel_tint(rgba, k=1.0):
    """Metal bodies (Lairon, Aggron: the 3DS shader 'Body_Add' adds a second picture and a coloured rim) are plain grey pictures; in XD's
    light they stay colourless. A cool steel-blue cast on near-grey pictures only (coloured parts, eyes' blue, are left alone)."""
    f = rgba[..., :3].astype(float)
    chroma = (f.max(axis=2) - f.min(axis=2))
    w = np.clip(1.0 - chroma / 40.0, 0.0, 1.0)[..., None]
    mult = np.array([1.0 - 0.10 * k, 1.0 - 0.02 * k, 1.0 + 0.12 * k])
    out = rgba.copy()
    out[..., :3] = np.clip(f * (1.0 + (mult - 1.0) * w) + 0.5, 0, 255).astype(np.uint8)
    return out


def alpha_class(rgba, use_alpha):
    if not use_alpha:
        return 'opa'
    a = rgba[..., 3]
    if (a >= 250).all():
        return 'opa'
    mid = ((a > 8) & (a < 247)).mean()
    return 'xlu' if mid > 0.02 else 'texedge'


def colour_coord(mt):
    """The texture coordinate entry that carries a material's colour picture: unit 0, except the Spinda spot pieces (shader patcheel_*Patch):
    their unit 0 is a flat red, unit 1 a MASK (one white ellipse, tiled 8 x 8 with a shift, clamped: a single spot lands where the piece's UV
    crosses the shifted tile). The mask is used as the picture (alpha = brightness) in the flat red: the spot appears."""
    co = [c for c in mt['coords'] if c['unit'] == 0]
    if 'patcheel' in mt['shader']:
        c1 = [c for c in mt['coords'] if c['unit'] == 1]
        if c1:
            return c1[0]
    if co and co[0]['name'].startswith('DummyTex'):

        c1 = [c for c in mt['coords'] if c['unit'] == 1 and not c['name'].startswith('DummyTex')]
        if c1:
            return c1[0]
    return co[0] if co else None


def is_effect_layer(mt):
    """A material whose colour slot (texture unit 0) holds a mask (Grimer / Muk streaks) or that is a tail glow piece (pokabu_Tail: environment / glow map only): the 3DS shader
    mixes it with other pictures; used as a plain colour it is black noise (Grimer's / Muk's streak layer, Marill's tail glow)."""
    co = [c for c in mt['coords'] if c['unit'] == 0]
    n0 = co[0]['name'] if co else ''

    if not mt['coords'] and 'Mask' in mt['shader']:
        return True

    if '_Thunder' in mt['shader'] or mt['name'].startswith('AuraEf') or (mt['name'].startswith('GlowInc') and '_Glow' in mt['shader']):
        return True
    return 'Mask' in n0 or 'pokabu_Tail' in mt['shader'] or ('Fire' in mt['shader'] and 'Inc' in mt['name'] and 'Fire' not in mt['name'])


MATCH_SKIP_NAMES = ('Iris', 'Mouth', 'Line', 'Tongue')


def match_piece_ok(mt, fixes_):
    """A 3DS material the colour match may change: a body picture. Eyes, mouths, iris, lines, cheek (Neon) plates, glow / effect
    pieces and pieces an earlier per-species fix already set are left alone."""
    n = mt['name']
    return (not fixes_ and not any(k in n for k in MATCH_SKIP_NAMES) and 'Neon' not in mt['shader'] and 'Fire' not in mt['shader']
            and 'Fire' not in n and not is_effect_layer(mt))


def usum_areas(gm):
    """material name -> surface area of its drawn triangles (3DS units; only the shares matter)."""
    mat_names = {mt['name'] for mt in gm['materials']}
    out = {}
    for me in gm['meshes']:
        for sm in me['subs']:
            if sm['name'] not in mat_names:
                continue
            tr = gf_read.triangles(sm)
            if not len(tr):
                continue
            P = sm['verts']['pos']
            a = np.linalg.norm(np.cross(P[tr[:, 1]] - P[tr[:, 0]], P[tr[:, 2]] - P[tr[:, 0]]), axis=1).sum() / 2.0
            out[sm['name']] = out.get(sm['name'], 0.0) + float(a)
    return out


def xd_picture(mod, tx):
    """RGBA of an XD texture layer (also the palette ones, C4 / C8 with their own palette), or None when it has no colour (intensity)."""
    im = mod.images[tx.image]
    if im.format in (8, 9):
        if tx.tlut is None:
            return None
        tl = mod.tluts[tx.tlut]
        n = len(tl.data) // 2
        hh = ((n + 15) // 16) * 4
        pal = gx_tex.decode({0: 3, 1: 4, 2: 5}.get(tl.format, 5), 4, hh, tl.data[:n * 2] + bytes(hh * 8 - n * 2)).reshape(-1, 4)
        raw = gx_tex.decode(0 if im.format == 8 else 1, im.width, im.height, im.data)[..., 0].astype(int)
        idx = raw // 17 if im.format == 8 else raw
        return pal[np.minimum(idx, len(pal) - 1)]
    if im.format in (0, 1):
        return None
    if im.format not in gx_tex.TILE:
        return None
    return gx_tex.decode(im.format, im.width, im.height, im.data)


def xd_colour_pool(xd_bytes, opt):
    """XD's own model of the species, read from the player's disc at convert time: its pictures' colours (times the material's
    diffuse colour) weighted by surface share -> usum_match.Pool (nothing of it is kept). When the mesh can't be read (a few
    models have vertex data the reader does not handle) the share of a picture is its pixel count."""
    b = bytes(xd_bytes)
    area = {}
    try:
        pk, mod, _w = hsd_read.read_pkx(bytearray(b), meshes=True)
        for me in mod.meshes:
            P = np.array([v.pos for v in me.verts], float).reshape(-1, 3)
            tr = np.array(me.tris, int).reshape(-1, 3)
            if len(tr):
                a = np.linalg.norm(np.cross(P[tr[:, 1]] - P[tr[:, 0]], P[tr[:, 2]] - P[tr[:, 0]]), axis=1).sum() / 2.0
                area[me.material] = area.get(me.material, 0.0) + float(a)
    except Exception:
        pk, mod, _w = hsd_read.read_pkx(bytearray(b), meshes=False)
        for mi, ma in enumerate(mod.materials):
            if ma.textures and ma.textures[0].image is not None:
                im = mod.images[ma.textures[0].image]
                area[mi] = float(im.width * im.height)
    area = {mi: a for mi, a in area.items() if mod.materials[mi].textures and mod.materials[mi].textures[0].image is not None}
    tot = sum(area.values()) or 1.0
    pool = usum_match.Pool()
    for mi, a in area.items():
        ma = mod.materials[mi]
        if not ma.textures or ma.textures[0].image is None or a / tot < opt.get('min_piece', 0.02):
            continue
        rgba = xd_picture(mod, ma.textures[0])
        if rgba is None or (rgba[..., 3] < 128).mean() > 0.5 or rgba.shape[0] * rgba.shape[1] < 64:
            continue
        rgb = usum_match.sample_pixels(rgba, opt.get('sample', 6000))
        rgb = rgb * (np.array(ma.diffuse[:3], float) / 255.0)
        pool.add(rgb, a / tot)
    return pool


def usum_colour_pool(gm, texs, opts, opt):
    """The 3DS body pictures as they will be before the match (after the per-species fixes and the light tweak) -> Pool."""
    area = usum_areas(gm)
    tot = sum(area.values()) or 1.0
    pool = usum_match.Pool()
    seen = {}
    nat = usum_fx.species_of(gm) if gm.get('bones') else 0
    for mt in gm['materials']:
        co = colour_coord(mt)
        nm = co['name'] if co else None
        fixes_ = usum_recolour.fix_for(nat, mt['name']) if opts.get('recolour', True) else []
        a = area.get(mt['name'], 0.0) / tot
        if nm not in texs or 'Eye' in mt['name'] or not match_piece_ok(mt, fixes_) or a < opt.get('min_piece', 0.02):
            continue
        rgba = texs[nm][2]
        if (rgba[..., 3] < 128).mean() > 0.5:
            continue
        rgba = light_tweak(rgba, opts.get('light'))
        pool.add(usum_match.sample_pixels(rgba, opt.get('sample', 6000)), a)
    return pool


def colour_match_plan(gm, texs, xd_bytes, opts, log):
    """The per-species colour match (opts['match'] true or a dict of usum_match.DEFAULTS values) -> usum_match.Plan or None."""
    opt = opts.get('match')
    if not opt:
        return None
    opt = dict(opt) if isinstance(opt, dict) else {}
    try:
        xp = xd_colour_pool(xd_bytes, opt)
        up = usum_colour_pool(gm, texs, opts, opt)
        rep = {}
        plan = usum_match.make_plan(up, xp, opt, rep)
        if not (up.ok() and xp.ok()):
            log('INFO: colour match: no pictures to measure (XD side %s, 3DS side %s)' % (xp.ok(), up.ok()))
        elif plan is None:
            log('INFO: colour match: left alone (dE %.1f, %s)' % (rep['de_before'], rep.get('why')))
        else:
            log('INFO: colour match: texture dE %.1f -> %.1f' % (rep['de_before'], rep['de_after']))
        return plan
    except Exception as ex:
        log('INFO: colour match skipped: %s: %s' % (type(ex).__name__, ex))
        return None


def plate_is_tra(mt, nm, texs):
    """Does the converter draw this plate as a see-through picture (its class 'xlu' / 'texedge' follow from the picture)."""
    tra = ('Tra' in mt['shader']) or ('Alp' in mt['shader']) or ('Xlu' in mt['shader']) or ('Iris' in mt['shader']) or ('Iris' in mt['name']) or mt['name'].startswith('Line')
    if not tra and nm in texs and mt['name'].startswith('Mouth') and (texs[nm][2][..., 3] < 8).mean() > 0.25:
        tra = True
    if not tra and nm in texs and mt['name'].startswith('Eye') and 'Iris' not in mt['name'] and (texs[nm][2][..., 3] < 8).mean() > 0.6:
        tra = True
    return tra


def iris_plan(gm, texs, opts):
    """{eye plate material: [iris plate materials]} that are baked into the eye picture (usum_iris); the iris plates are then not drawn."""
    if opts is not None and not opts.get('iris_bake', True):
        return {}
    if '_iris_plan' not in gm:
        out = {}
        for en, irs in usum_iris.plan(gm, texs, colour_coord).items():
            mt = next(m for m in gm['materials'] if m['name'] == en)
            co = colour_coord(mt)
            w, h = texs[co['name']][0], texs[co['name']][1]
            if plate_is_tra(mt, co['name'], texs) or not usum_iris.bakeable(co, w, h):
                continue
            good = []
            for i in irs:
                im = next(m for m in gm['materials'] if m['name'] == i)
                ic = colour_coord(im)
                if ic is not None and ic['name'] in texs and plate_is_tra(im, ic['name'], texs):
                    good.append(i)
            if good:
                out[en] = good
        gm['_iris_plan'] = out
    return gm['_iris_plan']


def bake_eye_irises(gm, texs, mt, iris_names, rgba, opts):
    irs = []
    for i in iris_names:
        im = next(m for m in gm['materials'] if m['name'] == i)
        ic = colour_coord(im)
        tile = texs[ic['name']][2]
        if opts and opts.get('light'):
            tile = light_tweak(tile, opts.get('light'))
        mk = mco = None
        for c in im['coords']:
            if c['unit'] == 2 and 'Nor' in c['name'] and c['name'] in texs:
                mk, mco = texs[c['name']][2], c
        irs.append(dict(tile=tile, co=ic, mask=mk, mco=mco, bbox=usum_iris.mesh_bbox(gm, i)))
    return usum_iris.bake(rgba, colour_coord(mt), irs)


def make_images(gm, texs, log, max_side=256, opts=None):
    max_side = (opts or {}).get('max_side', max_side)
    """-> images list, per-material texture (image index) and class."""
    m_imgs, name_to_img, cls = [], {}, {}
    out = []
    plan_ = iris_plan(gm, texs, opts)
    baked_ = {i for v in plan_.values() for i in v}
    for mt in gm['materials']:
        if mt['name'] in baked_:
            out.append((None, 'opa'))
            continue
        co = [colour_coord(mt)] if colour_coord(mt) else []
        nm = co[0]['name'] if co else None
        patch = 'patcheel' in mt['shader'] and co and co[0]['unit'] == 1
        tra = plate_is_tra(mt, nm, texs)

        eye_plate_ = 'Eye' in mt['name'] and 'Iris' not in mt['name'] and bool(opts is None or opts.get('eye_rgb5', True))
        eye_mat = bool(opts and opts.get('eye_blend') and 'Eye' in mt['name'] and 'Iris' not in mt['name'] and nm and 'Eye' in nm)
        inc = usum_fx.emission_map(mt, texs) if opts and opts.get('fx') else None


        ghost = bool(opts and opts.get('ghost_alpha') and 'ghos_BodyAdd' in mt['shader'] and 'Eye' not in mt['name'] and 'Iris' not in mt['name'])
        cell = 'uniran_Cell' in mt['shader']
        if ghost or cell:
            tra = True
        if patch:
            tra = True
        fixes_ = usum_recolour.fix_for(usum_fx.species_of(gm) if gm.get('bones') else 0, mt['name']) if (opts or {}).get('recolour', True) else []
        nat_ = usum_fx.species_of(gm) if gm.get('bones') else 0
        hand_on = bool((opts or {}).get('hand', True))
        hand_rules = usum_hand.rules_for(nat_, mt['name']) if hand_on else []
        flat_ = usum_hand.flat_for(nat_, mt['name']) if hand_on else None
        plate_ = hand_on and usum_hand.plates_follow_match(nat_, mt['name'])
        do_match = bool((opts or {}).get('_match_plan') is not None and (match_piece_ok(mt, fixes_) or (plate_ and not fixes_)) and not ghost)
        key = (nm, tra, inc, ghost, cell, patch, tuple(id(f_) for f_ in fixes_), do_match, plate_, tuple(id(r_[1]) for r_ in hand_rules), flat_, tuple(plan_.get(mt['name'], ())))
        if nm is None or nm not in texs:
            out.append((None, 'opa'))
            continue
        if key not in name_to_img:
            w, h, rgba = texs[nm]
            if patch:

                c0 = [x for x in mt['coords'] if x['unit'] == 0]
                col = texs[c0[0]['name']][2][..., :3].reshape(-1, 3).mean(axis=0) if c0 and c0[0]['name'] in texs else np.array([214.0, 110.0, 108.0])
                rgba = rgba.copy()
                lum = rgba[..., :3].astype(float).mean(axis=2)
                rgba[..., :3] = np.clip(col, 0, 255).astype(np.uint8)
                rgba[..., 3] = np.clip(lum * (rgba[..., 3] / 255.0), 0, 255).astype(np.uint8)
            c = alpha_class(rgba, tra)
            if cell:
                cc = mt.get('colors', {})
                c0_ = np.array(cc.get('c0', (61, 165, 101))[:3], float)
                if c0_.min() > 200:
                    c0_ = np.array([61.0, 165.0, 101.0])
                col = np.array(cc.get('c3', (150, 230, 130))[:3], float) * 0.4 + c0_ * 0.6
                rgba = np.zeros((16, 16, 4), np.uint8)
                rgba[..., :3] = np.clip(col, 0, 255).astype(np.uint8)
                rgba[..., 3] = 150
                w = h = 16
                c = 'xlu'
            if ghost:
                c = 'xlu'
                rgba = rgba.copy()
                rgba[..., 3] = int(255 * opts['ghost_alpha'])
                rgba[..., :3] = (rgba[..., :3].astype(float) * 0.75 + np.array([80.0, 30.0, 115.0]) * 0.25).astype(np.uint8)
            if not tra:
                rgba = rgba.copy()
                rgba[..., 3] = 255
            if opts and opts.get('lure') and is_lure_glow(mt):
                rgba = lure_fill(rgba)
                c = 'opa'
            elif opts and opts.get('fire') and ('Fire' in mt['shader'] or 'Fire' in mt['name']) and (rgba[..., 0] == rgba[..., 1]).all():

                rgba = fire_tint(rgba)
                c = 'opa'
                if opts.get('fx'):
                    rgba = usum_fx.fire_alpha(rgba)
                    c = 'xlu'
            elif opts and opts.get('light') and ('Eye' not in mt['name'] or eye_mat):
                if True:
                    for fix_ in fixes_:
                        rgba = fix_(rgba)
                rgba = light_tweak(rgba, opts.get('light'))
                if do_match:
                    rgba = opts['_match_plan'].apply(rgba, skin_only=('Eye' in mt['name'] or plate_))
                if opts.get('sat_boost') and 'Eye' not in mt['name'] and 'Iris' not in mt['name']:
                    rgba = sat_boost(rgba, float(opts['sat_boost']))
                if hand_rules:
                    rgba = usum_hand.tune(rgba, hand_rules)
                if flat_:
                    rgba = rgba.copy()
                    rgba[..., :3] = np.array(flat_, np.uint8)
                steel_k = float(os.environ.get('N3DS_STEEL', opts.get('steel') or 0))
                if steel_k and ('PTS_Body_Add' in mt['shader'] or (usum_fx.species_of(gm) in STEEL_SPECIES and 'Eye' not in mt['name'] and 'Iris' not in mt['name'] and 'Mouth' not in mt['name'])):
                    rgba = steel_tint(rgba, steel_k)
                if opts.get('cheeks') and 'Neon' in mt['shader']:
                    rgba = cheek_boost(rgba, opts['cheeks'])
                if inc:
                    rgba = usum_fx.bake_emission(rgba, texs[inc], strength=usum_hand.glow_strength(nat_), raw=texs[nm][2])
            if os.environ.get('N3DS_WHITE'):
                rgba = rgba.copy()
                rgba[..., :3] = 255
            if mt['name'] in plan_:
                rgba = bake_eye_irises(gm, texs, mt, plan_[mt['name']], rgba, opts)
            nw, nh = min(w, max_side), min(h, max_side)
            if (nw, nh) != (w, h):
                rgba = gx_tex.resize(rgba, nw, nh)
            im = Image()
            im.width, im.height = nw, nh
            if c == 'xlu' or (c == 'opa' and eye_plate_):
                if c == 'opa':
                    rgba = usum_iris.round5(rgba)
                im.format, im.data = 5, gx_tex.encode_rgb5a3(rgba)
            else:
                im.format, im.data = 14, gx_tex.encode_cmpr(rgba)
            name_to_img[key] = len(m_imgs)
            m_imgs.append(im)
            cls[len(m_imgs) - 1] = c
        out.append((name_to_img[key], cls[name_to_img[key]]))
    return m_imgs, out, cls


def wrap_code(w):
    return {0: 0, 1: 0, 2: 1, 3: 1 if os.environ.get('N3DS_REPEAT') else 2}.get(w, 1)


def uv_transform(co, uv, mat_override=None):
    sx, sy = co['scale']
    tx, ty = co['trans']
    if os.environ.get('N3DS_NOSCALE') and 'Eye' not in co['name'] and 'Iris' not in co['name']:
        sx, sy = 1.0, 1.0
    if mat_override:
        tx = mat_override.get('tx', tx)
        ty = mat_override.get('ty', ty)


    u = sx * (uv[:, 0] - tx)
    v = sy * (uv[:, 1] - ty)
    return np.stack([u, v], 1)


def first_frame_override(mo, mat_name):
    """Texture unit 0 translation of a material at frame 0 of a motion (the eye animation's first pick)."""
    out = {}
    for mm in mo['mats']:
        if mm['name'] == mat_name and mm['unit'] == 0:
            for key, ch in (('tx', mm['tx']), ('ty', mm['ty'])):
                v = gf_read.herp(ch, 0)
                if v is not None:
                    out[key] = v
    return out


def sample_motion_arrays(sk, mo, nsamples, stretch=1.0):
    """per-sample arrays s, r, t (n, nb, 3) with rotation unwrapped; axis-angle rotations turned to Euler."""
    return sample_at(sk, mo, [k * stretch for k in range(nsamples)])


def sample_at(sk, mo, frames):
    """Like sample_motion_arrays but at the given (fractional) source frames. The answer is kept in the motion (`_memo`) for the same skeleton and
    frames: the size ladder (gen57_speed.convert_ladder) converts one model up to 11 times and every step asks for the very same poses; callers
    change the arrays they get (ground offsets), so each call gets copies."""
    key = ('at', id(sk.bones), tuple(frames))
    memo = mo.setdefault('_memo', {})
    if key not in memo:
        memo[key] = _sample_at(sk, mo, frames)
    return tuple(a.copy() for a in memo[key])


def _sample_at(sk, mo, frames):
    nb = len(sk.names)
    nsamples = len(frames)
    S = np.zeros((nsamples, nb, 3))
    R = np.zeros((nsamples, nb, 3))
    T = np.zeros((nsamples, nb, 3))
    for k in range(nsamples):
        f = frames[k]
        s, r, t, ax = gf_pose.sample_motion(sk, mo, f)
        for i in range(nb):
            if ax[i]:
                r[i] = gf_pose.euler_from_mat(gf_pose.axis_angle_mat(r[i]))
        S[k], R[k], T[k] = s, r, t
    R = np.unwrap(R, axis=0)
    return S, R, T


def motion_energy(sk, mo):
    key = ('energy', id(sk.bones))
    memo = mo.setdefault('_memo', {})
    if key not in memo:
        memo[key] = _motion_energy(sk, mo)
    return memo[key].copy()


def _motion_energy(sk, mo):
    """Per source frame interval: how much the pose changes (rotations, and translations at 1/50) = where the strike is."""
    nat = max(mo['frames'], 1)
    S, R, T = sample_at(sk, mo, [float(k) for k in range(nat + 1)])
    e = np.abs(np.diff(R, axis=0)).sum(axis=(1, 2)) + np.abs(np.diff(T, axis=0)).sum(axis=(1, 2)) / 50.0
    k = np.array([1.0, 2.0, 1.0]) / 4.0
    for _ in range(2):
        e = np.convolve(np.pad(e, 1, mode='edge'), k, mode='valid')
    return e


def role_warp(role, nat, xd_len, e):
    """Source frame for every output frame: the 3DS motion retimed by ROLE, toward XD's weight and length.
    idle: a little slower (heavier), still a loop. attack: slow wind-up, snappy strike at its own speed, a held
    impact, a slow recovery. hit: instant recoil, slow settle. faint: the fall at its own pace (a touch slower),
    then the last pose is HELD (heavy) for the rest of XD's length. Never a uniform stretch."""
    e = np.asarray(e, float)
    emax = float(e.max()) or 1.0
    hot = np.nonzero(e >= 0.35 * emax)[0]
    s0, s1 = (int(hot[0]), int(hot[-1])) if len(hot) else (0, nat - 1)
    pk = int(e.argmax())
    d = np.ones(nat)
    hold_at, hold = None, 0
    if role == 'idle':
        d[:] = 1.25
    elif role in ('phys', 'special'):
        d[:s0] = 1.8
        d[s0:s1 + 1] = 0.95
        d[s1 + 1:] = 1.7
        hold_at, hold = s1, max(3, int(0.08 * xd_len))
    elif role == 'hit':
        d[:pk + 1] = 0.9
        d[pk + 1:] = 2.4
        hold_at, hold = pk, max(2, int(0.05 * xd_len))
    elif role == 'faint':
        d[:s1 + 1] = 1.35
        d[s1 + 1:] = 1.5
        hold_at = nat - 1
        hold = max(0, int(0.85 * xd_len - d.sum()))
    k = np.array([1.0, 2.0, 1.0]) / 4.0
    for _ in range(2):
        d = np.convolve(np.pad(d, 1, mode='edge'), k, mode='valid')
    total = d.sum() + hold
    cap = {'idle': 1e9, 'phys': 0.9, 'special': 0.9, 'hit': 0.75, 'faint': 0.9}[role] * xd_len
    if total > cap:
        g = cap / total
        d *= g
        hold = int(hold * g)
    ts, fs = [0.0], [0.0]
    cum = 0.0
    for k_ in range(nat):
        cum += d[k_]
        ts.append(cum)
        fs.append(float(k_ + 1))
        if hold_at is not None and k_ == hold_at and hold:
            cum += hold
            ts.append(cum)
            fs.append(float(k_ + 1))
    n = int(round(cum))
    return [float(np.interp(j, ts, fs)) for j in range(n + 1)]


HOVER_MIN = 0.35


def xd_idle_lowest(xd_bytes, xd_model, xd_pkx):
    """XD's own lowest point (XD units) in its idle animation: the larger of the posed vertices' mean over the idle and the idle slot's box
    (Gastly's gas hangs below its body: the box is the body's). The IDLE slot, not slot 0 (Nincada's slot 0 is a jump). 0 when unreadable."""
    idle_i = 0
    users = {}
    for s_ in range(len(xd_pkx.sections)):
        for mo, (kind, idx) in enumerate(xd_pkx.section_motions(s_)[1]):
            if kind and 0 <= idx < len(xd_model.animations):
                users.setdefault(idx, []).append(s_)
    for i_ in sorted(users):
        if 0 in users[i_] or 16 in users[i_]:
            idle_i = i_
            break
    box = 0.0
    if xd_model.animations and xd_model.animations[idle_i].bounds:
        box = float(np.mean(np.array(xd_model.animations[idle_i].bounds)[:, 1]))
    posed = 0.0
    try:
        import model as MD
        _p, xm, _w = hsd_read.read_pkx(xd_bytes, meshes=True)
        an = xm.animations[idle_i] if xm.animations else None
        end = an.end() if an else 0
        lows = []
        for f in ([0, end * 0.25, end * 0.5, end * 0.75] if end > 1 else [0]):
            sk_ = MD.skin_positions(xm, MD.pose_at(xm, an, f))
            ys_ = [p[1] for l in sk_ for p in l]
            if ys_:
                lows.append(min(ys_))
        posed = float(np.mean(lows)) if lows else 0.0
    except Exception:
        pass
    return max(box, posed)


def ground_offsets(sk, S, R, T, pts, shift, hover):
    """Vertical correction per frame (3DS units) so the feet stand on the floor (non-flyers: any hover is pulled down, a little
    jump is kept; nothing sinks below the floor) or the body keeps XD's flyer height (flyers: never below the floor)."""
    n = len(S)
    idx = list(range(0, n, 3))
    if idx[-1] != n - 1:
        idx.append(n - 1)
    ym = []
    for k in idx:
        W = sk.world(S[k], R[k], T[k])
        ym.append(min(float(gf_pose.skin_points(sk, W, bi, v)[:, 1].min()) for bi, v in pts))
    ym = np.interp(np.arange(n), idx, ym) + shift
    adj = np.where(ym < 0, -ym, 0.0)
    if hover <= HOVER_MIN:
        adj = adj - 0.85 * np.where(ym > 0, ym, 0.0)
    k = np.array([1, 2, 3, 2, 1], float) / 9.0
    adj = np.convolve(np.pad(adj, 2, mode='edge'), k, mode='valid')
    return adj


def convert(gm, texs, mots, xd_bytes, mode='native', log=print, step=0.1, size_factor=None, flip=False, yaw=0.0, drop_vco=False, opts=None, hover=None):
    """gm: gf_read.parse_model dict; texs: name -> (w, h, rgba); mots: role -> parsed motion (idle required);
    xd_bytes: XD's own PKX of the same Pokemon. -> (pkx bytes, info dict)."""
    opts = opts or {}
    xd_copy = bytes(xd_bytes)
    xd_pkx, xd_model, _w = hsd_read.read_pkx(xd_bytes, meshes=False)
    sk = gf_pose.Skeleton(gm['bones'])
    nb = len(sk.names)
    order = dfs_order(sk)
    m = Model()

    j0 = Joint()
    x0 = xd_model.joints[0]
    j0.rot, j0.scale, j0.pos = x0.rot, x0.scale, x0.pos
    j1 = Joint()
    j1.parent = 0
    m.joints += [j0, j1]
    m.skeleton_root = 1
    jn = {}
    for pos, i in enumerate(order):
        jn[i] = 2 + pos
    for i in order:
        j = Joint()
        j.name = sk.names[i]
        j.parent = jn[sk.parent[i]] if sk.parent[i] >= 0 else 1
        j.scale, j.rot, j.pos = tuple(sk.bind_s[i]), tuple(sk.bind_r[i]), tuple(sk.bind_t[i])
        m.joints.append(j)
    holder = {}
    for me in gm['meshes']:
        if me['name'] in holder:
            continue
        j = Joint()
        j.name = me['name']
        j.parent = 1
        holder[me['name']] = len(m.joints)
        m.joints.append(j)

    idle = mots['idle']
    mat_index = {mt['name']: i for i, mt in enumerate(gm['materials'])}

    skip_fire = bool(opts.get('fx_skip_fire') or (opts.get('fx') and usum_fx.xd_has_particles(xd_model))) and usum_fx.species_of(gm) not in KEEP_FIRE_MESH

    def drawn(me_, sm):
        """True for a piece that ends up in the model. Height, grounding and the bounds must only look at these: a piece that is dropped
        later (flame, gas, mask / glow layers, hidden meshes) must not decide the size (Gastly, Weezing, Koffing, Torkoal)."""
        if (me_['name'] in idle['vis'] and not idle['vis'][me_['name']][0]) or me_.get('hidden'):
            return False
        mi_ = mat_index.get(sm['name'])
        if mi_ is None:
            return False
        mt_ = gm['materials'][mi_]
        if 'Vco' in mt_['name'] and drop_vco:
            return False
        if os.environ.get('N3DS_DROP') and mt_['name'] in os.environ['N3DS_DROP'].split(','):
            return False
        if skip_fire and usum_fx.is_fire_material(mt_):
            return False
        return not is_effect_layer(mt_)

    def idle_stats():
        """(frame-0 height, mean height over the idle, frame-0 lowest point, mean lowest point over the idle) of the DRAWN pieces."""
        n_ = max(idle['frames'], 1)
        hs, ms = [], []
        for f in (0, n_ * 0.25, n_ * 0.5, n_ * 0.75):
            s_, r_, t_, ax_ = gf_pose.sample_motion(sk, idle, f)
            W_ = sk.world(s_, r_, t_, ax_)
            y_ = np.concatenate([gf_pose.skin_points(sk, W_, sm['bone_idx'], sm['verts'])[used_of(sm)][:, 1]
                                 for me in gm['meshes'] for sm in me['subs'] if drawn(me, sm)])
            hs.append(float(y_.max() - y_.min()))
            ms.append(float(y_.min()))
        return hs[0], float(np.mean(hs)), ms[0], float(np.mean(ms))
    s0, r0, t0, ax0 = gf_pose.sample_motion(sk, idle, 0)
    W0 = sk.world(s0, r0, t0, ax0)
    ys = []
    for me in gm['meshes']:
        for sm in me['subs']:
            if drawn(me, sm):
                ys.append(gf_pose.skin_points(sk, W0, sm['bone_idx'], sm['verts'])[used_of(sm)])
    allp = np.concatenate(ys)
    my_h = float(allp[:, 1].max() - allp[:, 1].min())
    my_h0 = my_h
    idle_ymin_mean = float(allp[:, 1].min())
    if opts.get('size_rule'):
        _h0, my_h, _y0, idle_ymin_mean = idle_stats()
    xb = xd_model.animations[0].bounds if xd_model.animations and xd_model.animations[0].bounds else None
    xd_h = -1
    if size_factor is None:
        if xb:
            xbn = np.array(xb)
            xd_h = float((xbn[:, 4] - xbn[:, 1]).mean())
            size_factor = xd_h / my_h
        else:
            size_factor = 0.1
    if opts.get('size_rule') and opts.get('height_cm') and (xd_h <= 0 or opts.get('size_rule') == 'mainline'):
        size_factor = LOOK.mainline_box_height(opts['height_cm']) / my_h
    if opts.get('scale'):
        size_factor = opts['scale']
    log('INFO: height %.1f (3DS units), XD idle box height %.1f -> scale %.4f' % (my_h, xd_h, size_factor))
    if opts.get('size_rule'):
        log('INFO: size rule: frame-0 height %.1f, idle mean %.1f, scale before the rule %.4f' % (my_h0, my_h, (xd_h / my_h0) if xd_h > 0 else size_factor))
    xd_ymin = float(np.mean(np.array(xb)[:, 1])) if xb else 0.0
    if hover is None and opts.get('hover_rule') == 'model':
        hover = LOOK.model_hover(float(allp[:, 1].min()), my_h, size_factor)
    if hover is None:


        xd_low = xd_idle_lowest(xd_copy, xd_model, xd_pkx) if opts.get('hover_idle', True) else xd_ymin
        hover = xd_low if xd_low > HOVER_MIN else 0.0
    shift = 0.0
    if opts.get('ground'):


        shift = hover / size_factor - (idle_ymin_mean if hover > HOVER_MIN else float(allp[:, 1].min()))
        m.joints[1].pos = (0.0, shift * size_factor, 0.0)
        log('INFO: grounding: %s, shift %.2f' % (('hovers at %.1f like the XD flyers' % hover) if hover > 0.5 else 'feet on the floor', shift))
    bake = bool(opts.get('bake_scale'))
    sf_root = 1.0 if bake else size_factor
    m.joints[1].scale = (sf_root,) * 3
    if bake:
        for i in range(nb):
            m.joints[jn[i]].pos = tuple(float(x) * size_factor for x in sk.bind_t[i])
    m.joints[1].rot = (0.0, yaw, 0.0)
    ws = m.world_static()
    root_inv = mat_inv(ws[1])
    for i in range(nb):
        m.joints[jn[i]].inv_bind = mat_inv(mat_mul(root_inv, ws[jn[i]]))

    if opts.get('match') and opts.get('hand', True) and not usum_hand.match_enabled(usum_fx.species_of(gm) if gm.get('bones') else 0):
        log('INFO: colour match: off for this species (hand table usum_hand)')
    elif opts.get('match'):
        opts = dict(opts, _match_plan=colour_match_plan(gm, texs, xd_copy, opts, log))
    images, mat_img, classes = make_images(gm, texs, log, opts=opts)
    baked_iris = {i for v in iris_plan(gm, texs, opts).values() for i in v}
    if baked_iris:
        log('INFO: iris plates painted into the eye picture: %s' % sorted(baked_iris))
    m.images = images
    for mi, mt in enumerate(gm['materials']):
        ma = Material()
        ma.name = mt['name']
        ima, cls = mat_img[mi]
        ma.draw_class = cls
        if ima is not None:
            tx = Texture()
            tx.image = ima
            co = colour_coord(mt)
            tx.wrap_s, tx.wrap_t = wrap_code(co['wrap'][0]), wrap_code(co['wrap'][1])
            if opts.get('fx') and usum_fx.is_fire_material(mt):
                tx.wrap_s = tx.wrap_t = 1
            if cls == 'texedge':
                tx.flags = 0x00230010
                ma.rendermode = 0x40002015
            elif cls == 'xlu':
                tx.flags = 0x00230010
                ma.rendermode = 0x60002015
            ma.textures.append(tx)
        else:
            ma.rendermode = 0x2005
            c = mt['colors']['diffuse']
            ma.ambient = ma.diffuse = (c[0], c[1], c[2], 255)
        m.materials.append(ma)
    m.image_classes = classes
    eyes = None
    if opts.get('eyes'):
        sxy = {}
        for mt in gm['materials']:
            co0 = [c for c in mt['coords'] if c['unit'] == 0]
            if co0:
                sxy[mt['name']] = tuple(co0[0]['scale'])
        eyes = LOOK.EyePlan(gm, mots, sxy)
        eyes.names = [n_ for n_ in eyes.names if n_ in sxy and n_ not in baked_iris]
        log('INFO: eyes: sliding materials %s, levels %s' % (eyes.names, {n_: eyes.levels[n_] for n_ in eyes.names}))

    mat_index = {mt['name']: i for i, mt in enumerate(gm['materials'])}
    combos = set()
    hidden = []
    tri_total = 0
    ratio = 1.0
    tri_before = 0
    if opts.get('decimate'):
        tot = 0
        for me_ in gm['meshes']:
            if (me_['name'] in idle['vis'] and not idle['vis'][me_['name']][0]) or me_.get('hidden'):
                continue
            for sm in me_['subs']:
                tot += len(gf_read.triangles(sm))
        ratio = min(1.0, float(opts['decimate']) / max(tot, 1))

    skip_fire = bool(opts.get('fx_skip_fire') or (opts.get('fx') and usum_fx.xd_has_particles(xd_model))) and usum_fx.species_of(gm) not in KEEP_FIRE_MESH
    for me_ in gm['meshes']:
        if (me_['name'] in idle['vis'] and not idle['vis'][me_['name']][0]) or me_.get('hidden'):
            hidden.append(me_['name'])
            continue
        for sm in me_['subs']:
            mi = mat_index.get(sm['name'])
            if mi is not None and 'Vco' in gm['materials'][mi]['name'] and drop_vco:
                mi = None
            if mi is None:
                continue
            if os.environ.get('N3DS_DROP') and gm['materials'][mi]['name'] in os.environ['N3DS_DROP'].split(','):
                continue
            if skip_fire and usum_fx.is_fire_material(gm['materials'][mi]):
                continue
            if gm['materials'][mi]['name'] in baked_iris:
                continue
            if is_effect_layer(gm['materials'][mi]):
                continue
            V = sm['verts']
            n = V['n']
            mt = gm['materials'][mi]
            co = [colour_coord(mt)] if colour_coord(mt) else []
            if co and 'uv0' in V:
                uv = uv_transform(co[0], V['uv0'][:, :2], first_frame_override(idle, mt['name']) if co[0]['unit'] == 0 else None)
            else:
                uv = None
            tab = list(sm['bone_idx'])
            if 'bidx' in V and 'bw' in V:
                bi = V['bidx'].astype(int)
                bw = V['bw']
            else:
                bi = bw = None
            me = Mesh()
            me.material = mi
            me.joint = holder[me_['name']]
            me.rigid = False
            me.name = me_['name']
            tris = gf_read.triangles(sm)
            used = np.unique(tris) if len(tris) else np.array([], dtype=np.int64)
            remap = {int(v): k for k, v in enumerate(used)}
            verts = []
            for v in used:
                v = int(v)
                w = {}
                if bi is None:
                    w[jn[tab[0] if tab else 0]] = 1.0
                else:
                    for k in range(bi.shape[1]):
                        wk = float(bw[v, k]) if k < bw.shape[1] else 0.0
                        if wk > 0 and bi[v, k] < len(tab):
                            jj = jn[tab[bi[v, k]]]
                            w[jj] = w.get(jj, 0.0) + wk
                    if not w:
                        w[jn[tab[0] if tab else 0]] = 1.0
                wq = P.quantise_weights(w, step)
                combos.add(wq)
                nr = V['nrm'][v] if 'nrm' in V else (0, 1, 0)
                ln = math.sqrt(float(nr[0]) ** 2 + float(nr[1]) ** 2 + float(nr[2]) ** 2) or 1.0
                pv = [float(x) for x in V['pos'][v]]
                if 'patcheel' in mt['shader'] or mt['name'].startswith('Line'):
                    lift = 0.006 * my_h / ln
                    pv = [pv[0] + float(nr[0]) * lift, pv[1] + float(nr[1]) * lift, pv[2] + float(nr[2]) * lift]
                nv = (float(nr[0]) / ln, float(nr[1]) / ln, float(nr[2]) / ln)
                uvv = tuple(float(x) for x in uv[v]) if uv is not None else None
                if opts.get('quant'):

                    q_ = float(opts['quant'])
                    nv = tuple(round(x * q_) / q_ for x in nv)
                    uvv = tuple(round(x * 512) / 512 for x in uvv) if uvv is not None else None
                verts.append(Vertex(tuple(x * (size_factor if bake else 1.0) for x in pv), nv, uvv, None, wq, None))
            keep = []
            pos = V['pos']
            nrm = V['nrm'] if 'nrm' in V else None
            for a, b, c in tris:
                if len({a, b, c}) == 3:
                    if nrm is not None:

                        gn = np.cross(pos[b] - pos[a], pos[c] - pos[a])
                        vn = nrm[a] + nrm[b] + nrm[c]
                        out_ccw = float(np.dot(gn, vn)) > 0
                        if os.environ.get('N3DS_FLIPWIND') and mt['name'] in os.environ['N3DS_FLIPWIND'].split(','):
                            out_ccw = not out_ccw
                    else:
                        out_ccw = flip
                    ra, rb, rc = remap[int(a)], remap[int(b)], remap[int(c)]
                    keep.append((ra, rc, rb) if out_ccw else (ra, rb, rc))
            tri_before += len(keep)
            if ratio < 1.0 and len(keep) > 40:
                tgt = max(int(len(keep) * ratio), 30)
                kept, keep = DEC.decimate([v.pos for v in verts], [v.uv for v in verts] if uv is not None else None,
                                          [v.weights for v in verts], keep, tgt)
                verts = [verts[i] for i in kept]
            me.verts = verts
            me.tris = keep
            tri_total += len(keep)
            if keep:
                m.meshes.append(me)
    n_dobj, mat_places = {}, {}
    for me in m.meshes:
        k_ = n_dobj.get(me.joint, 0)
        n_dobj[me.joint] = k_ + 1
        mat_places.setdefault(me.material, []).append((me.joint, k_))

    def eye_anims(values, nfr):
        """{material name: (U values, V values)} -> Animation.materials (HSD texture slides on the eye pieces)."""
        out = {}
        for nm, (us, vs) in (values or {}).items():
            mi_ = mat_index.get(nm)
            if mi_ is None or mi_ not in mat_places:
                continue
            ao = AnimObj()
            ao.end = float(nfr)
            ao.tracks = [Track(LOOK.T_TRAU, LOOK.step_keys(us)), Track(LOOK.T_TRAV, LOOK.step_keys(vs))]
            ta = TexAnim()
            ta.tex_id = 0
            ta.aobj = ao
            for j_, d_ in mat_places[mi_]:
                ma_ = MatAnim()
                ma_.texanims = [ta]
                out.setdefault(j_, [None] * n_dobj[j_])[d_] = ma_
        return out
    if ratio < 1.0:
        log('INFO: slimmed %d -> %d triangles' % (tri_before, tri_total))
    log('INFO: %d triangles, %d meshes (hidden at idle: %s), %d weight combinations' % (tri_total, len(m.meshes), hidden, len(combos)))

    xd_frames = [int(round(a.end())) for a in xd_model.animations]
    users = {}
    for s in range(len(xd_pkx.sections)):
        for mo, (kind, idx) in enumerate(xd_pkx.section_motions(s)[1]):
            if kind and 0 <= idx < len(xd_frames):
                users.setdefault(idx, []).append((s, mo))
    order_roles = (0, 16, 8, 10, 1, 9, 2, 3, 4, 5, 7, 6, 12, 13, 14, 15, 11)

    def role_of(slot):
        us = sorted(users.get(slot, []), key=lambda sm: order_roles.index(sm[0]) if sm[0] in order_roles else 99)
        if not us:
            return None
        s, mo = us[0]
        if s == 9 and mo >= 1:
            return 'faint'
        return ROLE_OF_SECTION.get(s)
    slot_role = [role_of(i) for i in range(len(xd_frames))]
    log('INFO: XD slots -> roles: ' + ' '.join('%d=%s' % (i, r or '-') for i, r in enumerate(slot_role)))

    rest_s, rest_r, rest_t = sk.bind_s, sk.bind_r, sk.bind_t
    cache = {}
    src_frames = {}
    roots = [i for i in range(nb) if sk.parent[i] < 0]
    gpts = []
    if opts.get('ground'):
        for me_ in gm['meshes']:
            if me_['name'] in hidden:
                continue
            for sm in me_['subs']:
                if not drawn(me_, sm):
                    continue
                u = used_of(sm)[::2]
                vv = {k: v[u] for k, v in sm['verts'].items() if isinstance(v, np.ndarray) and len(v) == sm['verts']['n']}
                if 'pos' in vv and len(vv['pos']):
                    gpts.append((sm['bone_idx'], vv))

    def sampled(role, nframes):
        key = (role, nframes)
        if key in cache:
            return cache[key]
        mo = mots.get(role) or mots['idle']
        nat = max(mo['frames'], 1)
        if mode == 'role':
            fr = role_warp(role, nat, nframes, motion_energy(sk, mo))
            src_frames[(role, nframes)] = fr
            S, R, T = sample_at(sk, mo, fr)
            n = len(fr) - 1
            if opts.get('ground') and role != 'idle' or opts.get('ground') and role == 'idle':
                adj = ground_offsets(sk, S, R, T, gpts, shift, hover)
                for r_ in roots:
                    T[:, r_, 1] += adj
            cache[key] = (S, R, T, n)
            return cache[key]
        if mode == 'xd':
            n = nframes
            stretch = nat / float(n)
        else:
            n = nat
            stretch = 1.0
        S, R, T = sample_motion_arrays(sk, mo, n + 1, stretch)
        src_frames[(role, nframes)] = [k * stretch for k in range(n + 1)]
        if mode == 'xd':

            for A in (R, T):
                A[1:-1] = (A[:-2] + 2 * A[1:-1] + A[2:]) / 4.0 if n > 4 else A[1:-1]
        cache[key] = (S, R, T, n)
        return cache[key]
    uses = {}
    for slot, role in enumerate(slot_role):
        if role is None:
            continue
        frames = xd_frames[slot]
        uses[slot] = sampled(role, frames)

    atol = float(opts.get('anim_tol', 1.0))
    tols = (0.001 * atol, 0.0003 * atol, 0.001 * atol)
    ctol = float(opts.get('chan_tol', 1.0))
    chan = np.zeros((nb, 3, 3), bool)
    bind = (rest_s, rest_r, rest_t)
    for slot, (S, R, T, n) in uses.items():
        for w, A in enumerate((S, R, T)):
            chan[:, w, :] |= np.abs(A - bind[w][None]).max(axis=0) > tols[w] * ctol
    kinds = (J_SCAX, J_ROTX, J_TRAX)
    face_slots = {}
    if eyes is not None and eyes.names:
        for r_, kind_ in enumerate(FACE_KINDS):
            rec = 0x1C + r_ * 0x13
            idx_ = xd_pkx.header[rec + 0x12]
            if xd_pkx.header[rec] and kind_ and idx_ < len(xd_frames) and slot_role[idx_] is None and idx_ not in face_slots:
                face_slots[idx_] = (kind_, r_)
        log('INFO: face slots %s' % {k: v[0] for k, v in face_slots.items()})
    for slot, frames in enumerate(xd_frames):
        an = Animation()
        if slot in face_slots:
            nfr = max(frames, 1)
            an.name = 'face_' + face_slots[slot][0]
            ao = AnimObj()
            ao.end = float(nfr)
            ao.tracks.append(Track(J_SCAX, [(OP_CON, sf_root, 0.0, nfr), (OP_CON, sf_root, 0.0, 0)]))
            an.joints[1] = ao
            an.materials = eye_anims(eyes.face_values(face_slots[slot][0], nfr), nfr)
            m.animations.append(an)
            continue
        if slot not in uses:
            an.name = 'none'
            if eyes is not None:
                an.materials = eye_anims(eyes.role_values('idle', [0.0] * (max(frames, 1) + 1)), max(frames, 1))
            ao = AnimObj()
            ao.end = float(max(frames, 1))
            ao.tracks.append(Track(J_SCAX, [(OP_CON, sf_root, 0.0, max(frames, 1)), (OP_CON, sf_root, 0.0, 0)]))
            an.joints[1] = ao
            m.animations.append(an)
            continue
        S, R, T, n = uses[slot]
        an.name = slot_role[slot]
        if eyes is not None:
            an.materials = eye_anims(eyes.role_values(slot_role[slot], src_frames[(slot_role[slot], xd_frames[slot])]), n)
        for i in range(nb):
            ao = AnimObj()
            ao.end = float(n)
            for w, A in enumerate((S, R, T)):
                for ax in range(3):
                    if chan[i, w, ax]:
                        k_ = size_factor if (bake and w == 2) else 1.0
                        ao.tracks.append(Track(kinds[w] + ax, P.keys_of([float(x) * k_ for x in A[:, i, ax]], tols[w] * k_ * (3.0 if mode == 'xd' else 1.5 if mode == 'role' else 1.0))))
            if ao.tracks:
                an.joints[jn[i]] = ao

        pts_fn = []
        boxes = []
        for k in range(n + 1):
            if k % 3 == 0 or k == n:
                W = sk.world(S[k], R[k], T[k])
                pp = []
                for me_ in gm['meshes']:
                    if me_['name'] in hidden:
                        continue
                    for sm in me_['subs']:
                        if drawn(me_, sm):
                            pp.append(gf_pose.skin_points(sk, W, sm['bone_idx'], sm['verts'])[used_of(sm)][::4])
                pp = np.concatenate(pp)
                pp[:, 1] += shift
                pp = pp * size_factor
                last = (float(pp[:, 0].min()), float(pp[:, 1].min()), float(pp[:, 2].min()),
                        float(pp[:, 0].max()), float(pp[:, 1].max()), float(pp[:, 2].max()))
            boxes.append(last)
        an.bounds = boxes
        log('INFO: bounds slot %d (%s): y %.2f..%.2f over %d frames' % (slot, slot_role[slot], min(b[1] for b in boxes), max(b[4] for b in boxes), len(boxes)))
        m.animations.append(an)
    m.bound_head = list(xd_model.bound_head or [])

    idle_slot = next((i for i, r in enumerate(slot_role) if r == 'idle'), 0)
    xw = np.array([[1.0]])
    from model import pose_at
    xwm = pose_at(xd_model, xd_model.animations[idle_slot] if xd_model.animations else None, 0.0)
    wm = m.world_static()
    cand = [(jn[i], np.array([wm[jn[i]][3], wm[jn[i]][7], wm[jn[i]][11]])) for i in range(nb)]
    cpos = np.array([c[1] for c in cand])

    def remap(xj):
        if xj < 0:
            return xj
        if xj >= len(xd_model.joints):
            return 0
        if xj == 0 or xj == xd_model.skeleton_root:
            return 0 if xj == 0 else 1
        p = np.array([xwm[xj][3], xwm[xj][7], xwm[xj][11]])
        return cand[int(((cpos - p[None]) ** 2).sum(axis=1).argmin())][0]
    if opts.get('fx'):
        usum_fx.add_particle_tracks(m, xd_model, xwm, jn, sk, gm, log)
        usum_fx.add_fire_scroll(m, gm, usum_fx.find_fire_motion(mots), log)
    pkx = hsd_read.Pkx()
    pkx.header, pkx.sections, pkx.particles = xd_pkx.header, list(xd_pkx.sections), xd_pkx.particles
    pkx.set_head_joint(remap(pkx.head_joint()))
    for s in range(len(pkx.sections)):
        pkx.set_section_joints(s, [remap(j) for j in pkx.section_joints(s)])
    h = bytearray(pkx.header)
    for r in range(4):
        rec = 0x1C + r * 0x13
        if eyes is not None and any(v[1] == r for v in face_slots.values()):
            if h[rec] == 2:
                h[rec] = 1
                h[rec + 1] = 0
                h[rec + 2:rec + 0x12] = bytes([255]) * 16
        else:
            h[rec] = 0
    pkx.header = bytes(h)

    st = step
    while True:
        archive, report = hsd_write.write_archive(m)
        if report.get('pobj', 0) <= P.MAX_POBJ or st >= 0.34:
            break
        log('INFO: %d drawing pieces: skin weights redone coarser' % report['pobj'])
        return convert(gm, texs, mots, xd_bytes, mode, log, round(st + 0.05, 2), size_factor, flip, yaw, drop_vco, opts, hover)
    pkx.archive = archive
    data = pkx.build()
    info = dict(size=len(data), report=report, tris=tri_total, scale=size_factor)
    log('written %d bytes (XD original %d); %s' % (len(data), len(xd_bytes), report))
    return data, info
