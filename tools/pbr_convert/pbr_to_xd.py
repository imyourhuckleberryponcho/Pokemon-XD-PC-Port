"""A Battle Revolution model (sdr_read.Sdr) as the neutral model, set into the same Pokemon's XD container.

Choices (each is ours; see docs/BATTLE_REVOLUTION.md "The converter"):
- joints: XD's two top joints are kept from XD's own file (joint 0, which the game moves, and the skeleton root);
  every PBR node follows as a joint, in PBR's order: joint = PBR node number + 2; then one "holder" joint per
  skinned mesh node, directly under the skeleton root (a skinned mesh must not hang on a joint that moves, and
  the holder is what a visibility track switches off);
- every joint is a "classical" HSD joint (flag 8): the game multiplies T * R * S down the tree. PBR's joint
  orient, pivots and scale compensation are worked into the sampled matrices (pbr_eval.py);
- size: the skeleton root's scale is XD's own, times a factor when the PBR model is made in another size
  (idle boxes compared), so the Pokemon is as big on the field as XD's;
- skin: PBR's weights rounded to steps (default 0.1, at most 4 joints a vertex) so that vertices share weight
  combinations; the writer cuts the mesh into pieces of at most 10 combinations;
- animations: XD's slots (and so XD's own section table: which animation plays for which kind of move) are
  kept. A slot gets the PBR action with the same number of frames; when there is none, the PBR action whose
  name fits what the sections using the slot are for. Every node is evaluated 30 times a second and written
  as straight-line keys; a channel that moves in any action is written in every action;
- an action that carries the body away while XD's stays in place has the travel taken out (on the skeleton root);
- meshes switched on and off (PBR's visibility curves): HSD "node" tracks on the mesh's joint;
- textures: copied as they are; a texture with fully see-through texels makes a cut-out material, one with
  in-between alpha a blended one (XD's own material numbers for those);
- the 16 attachment joints of each section and the head joint: XD's joint -> the PBR joint nearest to it in
  the idle pose;
- NOT done: eye / mouth texture animations (the four header records are switched off), XD's hidden touch boxes.

Third pass (2026-11-12):
- meshes with header byte 1 & 0x20 have 48-byte vertices (GX attribute 0x19: normal, binormal, tangent): read at
  that stride in sdr_read (38 meshes; Metagross, Rayquaza, Deoxys, Shedinja, Kyogre, Entei, Venusaur... were
  crumpled by it, not by scale compensation);
- PBR's touch boxes (meshes named col_*) are left out (they were the white boxes on Butterfree / Venomoth);
- a textured material on meshes without UVs is a reflection map: XD's own flags for that (Steelix's metal);
- untextured materials keep PBR's colours; brightness-only sheets (I4 / I8) are blended and tinted;
- pictures: sides at most 256 and whole blocks, file cap 600 KB (pictures halved down to 128, then animation
  keys thinned); more than 80 drawing pieces = coarser skin weights (gx_tex.py: our GX picture codec).

Frozen pass (2026-11-12, docs/PBR_CONVERTER_FROZEN.md):
- joint PAIRS: Maya's segment scale compensate makes a bone's 3x3 diag(1/parent scale) * R * S, a shear that no single
  T*R*S joint holds (Buneary's ears, 50 % of the height out). A model whose converted pose is more than POSE_TOL_PCT
  (1 %) of its height from the source is made again with every bone whose 3x3 a single joint holds worse than
  SHEAR_TOL as TWO joints: the first holds the translation and the compensating scale diag(1/parent scale), the second
  the rotation and the bone's own scale. Exact (the factors are the file's own channels, so no SVD and no sign or
  near-degenerate-scale ambiguity), children / meshes / skin weights / inverse bind stay on the second joint. The
  game numbers joints depth first, so the pair's first joint sits just before its second: joint numbers = jn[node]
  (a table, no longer node + 2). Models within the tolerance are not touched (byte-identical to before).
- more than MAX_REFLECT_PIECES drawing pieces with a reflection layer = coarser skin weights (the graphics layer never
  gives such a picture back; Ditto and Dialga).
"""
import math
import os
import sys

import hsd_read
import hsd_write
import sdr_read
from model import (Model, Joint, Mesh, Vertex, Material, Texture, Image, Animation, AnimObj, Track, MatAnim, TexAnim,
                   mat_mul, mat_inv, mat_srt, OP_LIN, OP_CON, J_ROTX, J_TRAX, J_SCAX)

FACE_ACTIONS = ('tx_sleep', 'tx_wakeup', 'tx_wink', None)   # XD's four face records, in header order
T_TRAU, T_TRAV = 2, 3                                       # HSD texture tracks: slide sideways / up and down

FPS = 30.0


def quantise_weights(w, step, most=4):
    """{node: weight} -> tuple of (node, weight) with weights in whole steps that add up to 1."""
    items = sorted(w.items(), key=lambda kv: -kv[1])[:most]
    total = sum(x for _, x in items) or 1.0
    n = int(round(1.0 / step))
    parts = [(j, x / total * n) for j, x in items]
    whole = [int(math.floor(x)) for _, x in parts]
    left = n - sum(whole)
    order = sorted(range(len(parts)), key=lambda i: -(parts[i][1] - whole[i]))
    for i in order[:left]:
        whole[i] += 1
    return tuple(sorted((j, k / float(n)) for (j, _), k in zip(parts, whole) if k > 0))


def thin(samples, tol):
    """Frames whose value a straight line between kept neighbours gives within tol are dropped.
    samples: list of values, one per frame. Returns [(frame, value)]."""
    n = len(samples)
    keep = [0]
    a = 0
    while a < n - 1:
        b = a + 1
        best = b
        while b < n:
            ok = True
            for k in range(a + 1, b):
                lin = samples[a] + (samples[b] - samples[a]) * (k - a) / float(b - a)
                if abs(lin - samples[k]) > tol:
                    ok = False
                    break
            if not ok:
                break
            best = b
            b += 1
        keep.append(best)
        a = best
    return [(f, samples[f]) for f in keep]


def texture_alpha(fmt, w, h, data):
    """How a GX picture uses alpha: 'opa' (none), 'texedge' (only fully see-through and solid texels: cut-out)
    or 'xlu' (in-between values: blended). Looks at the texels only."""
    import numpy as np
    d = np.frombuffer(data, dtype=np.uint8)
    if fmt == 14:                        # CMPR: 8-byte blocks; colour 0 <= colour 1 makes index 3 see-through
        blk = d[:len(d) // 8 * 8].reshape(-1, 8)
        c0 = blk[:, 0].astype(int) << 8 | blk[:, 1]
        c1 = blk[:, 2].astype(int) << 8 | blk[:, 3]
        bits = blk[:, 4:8][c0 <= c1]
        cnt = 0
        for sh in (6, 4, 2, 0):
            cnt += int((((bits >> sh) & 3) == 3).sum())
        return 'texedge' if cnt > 0.002 * w * h else 'opa'
    if fmt in (0, 1):                    # I4 / I8: one brightness channel, which GX also gives as alpha (Gastly's
        return 'xlu'                     # glow sheet): blended, so its dark part is see-through
    if fmt == 5:                         # RGB5A3
        v = d[:len(d) // 2 * 2].reshape(-1, 2)
        al = np.where((v[:, 0] & 0x80) == 0, (v[:, 0] >> 4) & 7, 7)
    elif fmt == 6:                       # RGBA8: blocks of 64 bytes, the first 32 = (A, R) pairs
        blk = d[:len(d) // 64 * 64].reshape(-1, 64)
        al = (blk[:, 0:32:2].reshape(-1).astype(int) * 7 + 127) // 255
    elif fmt == 2:                       # IA4
        al = ((d >> 4).astype(int) * 7 + 7) // 15
    elif fmt == 3:                       # IA8
        al = (d[0::2].astype(int) * 7 + 127) // 255
    else:
        return 'opa'
    n = float(len(al)) or 1.0
    mid = ((al > 0) & (al < 7)).sum() / n
    zero = (al == 0).sum() / n
    if mid > 0.02:
        return 'xlu'
    if zero > 0.002:
        return 'texedge'
    return 'opa'


# What XD's sections are for, as PBR action names in order of preference. Counted over the 388 pairs: for each
# section, which PBR action has the length of the XD animation the section plays.
PHYSICAL = ('tackle', 'kime_phy', 'punch', 'kick', 'hikkaku', 'kyukoka', 'binta', 'kime', 'kime_a')
ROLE_NAMES = {0: ('wait', 'kuchu_wait'), 16: ('kuchu_wait', 'wait'), 8: ('damage', 'damege', 'damage_a'),
              10: ('down', 'kuchu_down'), 9: ('damage', 'damege', 'down'), 6: ('kime', 'wait_a', 'wait'),
              1: ('kime', 'kime_a', 'kime_b'), 12: ('kime', 'kime_a'), 13: ('kime', 'kime_a'), 14: ('kime', 'kime_a'),
              15: ('mamoru', 'kime_a', 'kime'), 11: ('wait',),
              2: PHYSICAL, 3: PHYSICAL, 4: PHYSICAL, 5: PHYSICAL, 7: PHYSICAL}
ROLE_ORDER = (0, 16, 8, 10, 1, 9, 2, 3, 4, 5, 7, 6, 12, 13, 14, 15, 11)


def pick_actions(sdr, xd_pkx, xd_frames, log):
    """XD animation slot -> PBR action number (or None = nothing plays there). First by length (the animations
    the two games share have the same number of frames), then by what the sections that use the slot are for."""
    acts = [(i, a[0], max(int(round(a[3] * FPS)), 1)) for i, a in enumerate(sdr.actions) if not a[0].startswith('tx_')]
    users = {}
    for s in range(len(xd_pkx.sections)):
        for mo, (kind, idx) in enumerate(xd_pkx.section_motions(s)[1]):
            if kind and 0 <= idx < len(xd_frames):
                users.setdefault(idx, []).append((s, mo))

    def wanted(slot):
        us = sorted(users.get(slot, []), key=lambda sm: ROLE_ORDER.index(sm[0]) if sm[0] in ROLE_ORDER else 99)
        if not us:
            return None, ()
        s, mo = us[0]
        return s, (ROLE_NAMES[10] if s == 9 and mo >= 1 else ROLE_NAMES.get(s, ('wait',)))

    pick, how = {}, {}
    taken = set()
    for slot, f in enumerate(xd_frames):
        same = [a for a in acts if a[2] == f]
        if not same:
            continue
        free = [a for a in same if a[0] not in taken] or same
        want = wanted(slot)[1]
        free.sort(key=lambda a: (want.index(a[1]) if a[1] in want else 99, a[0]))
        pick[slot], how[slot] = free[0][0], 'length'
        taken.add(free[0][0])
    for slot, f in enumerate(xd_frames):
        if slot in pick or slot not in users:
            continue
        s, want = wanted(slot)
        cands = [a for a in acts if a[1] in want]
        if s in (2, 3, 4, 5, 7):
            cands = [a for a in cands if a[0] not in taken] or cands
        if cands:
            cands.sort(key=lambda a: (want.index(a[1]), abs(a[2] - f)))
            pick[slot], how[slot] = cands[0][0], 'role'
            taken.add(cands[0][0])
            log('NOTE: an XD animation (section %d) has no PBR action of its length: PBR\'s "%s" plays there' % (s, cands[0][1]))
        else:
            log('WRONG: an XD animation used by section %d has no PBR action to stand in: nothing plays there' % s)
    return [pick.get(i) for i in range(len(xd_frames))], how, users


def pick_actions_named(sdr, xd_pkx, xd_frames, by_slot, log):
    """Models with no XD counterpart: XD slot -> PBR action number, by the NAME of the action the stand-in's own
    pair plays in that slot (by_slot: slot -> PBR action name, from pick_actions on the stand-in's PBR model; the
    two games' names for the same actions agree). A name this model lacks: the action that fits what the sections
    using the slot are for (the same role lists pick_actions falls back to)."""
    names = {}
    for i, a in enumerate(sdr.actions):
        if not a[0].startswith('tx_'):
            names.setdefault(a[0], i)
    users = {}
    for s_ in range(len(xd_pkx.sections)):
        for mo, (kind, idx) in enumerate(xd_pkx.section_motions(s_)[1]):
            if kind and 0 <= idx < len(xd_frames):
                users.setdefault(idx, []).append((s_, mo))
    pick, how, taken = {}, {}, set()
    for slot in range(len(xd_frames)):
        nm = by_slot.get(slot)
        if nm in names:
            pick[slot], how[slot] = names[nm], 'name'
            taken.add(names[nm])
    for slot in range(len(xd_frames)):
        if slot in pick or slot not in users:
            continue
        us = sorted(users[slot], key=lambda sm: ROLE_ORDER.index(sm[0]) if sm[0] in ROLE_ORDER else 99)
        sec, mo = us[0]
        want = ROLE_NAMES[10] if sec == 9 and mo >= 1 else ROLE_NAMES.get(sec, ('wait',))
        if by_slot.get(slot) in PHYSICAL:
            want = PHYSICAL + tuple(w for w in want if w not in PHYSICAL)
        cands = [names[w] for w in want if w in names]
        if sec in (2, 3, 4, 5, 7):
            cands = [c for c in cands if c not in taken] or cands
        if cands:
            pick[slot], how[slot] = cands[0], 'role'
            taken.add(cands[0])
            log('NOTE: no PBR action called "%s" (what the stand-in plays in slot %d): "%s" plays there' % (
                by_slot.get(slot), slot, sdr.actions[cands[0]][0]))
        else:
            log('WRONG: slot %d (section %d) has no PBR action to stand in ("%s" missing): nothing plays there' % (
                slot, sec, by_slot.get(slot)))
    return [pick.get(i) for i in range(len(xd_frames))], how, users


def smooth(a, half=4):
    import numpy as np
    out = np.empty_like(a)
    for i in range(len(a)):
        out[i] = a[max(0, i - half):i + half + 1].mean(axis=0)
    return out


def vis_keys(v):
    """Drawn / not drawn per frame -> HSD keys for the joint's "node" track (above 0.5 = drawn)."""
    runs = []
    start = 0
    for f in range(1, len(v) + 1):
        if f == len(v) or bool(v[f]) != bool(v[start]):
            runs.append((1.0 if v[start] else 0.0, f - start))
            start = f
    keys = [(OP_CON, val, 0.0, n) for val, n in runs]
    keys[-1] = (OP_CON, keys[-1][1], 0.0, max(keys[-1][3] - 1, 1))
    keys.append((OP_CON, keys[-1][1], 0.0, 0))
    return keys


def step_keys(v):
    """Values per frame -> HSD keys that hold each value until the next change (the eye strip jumps)."""
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


def keys_of(samples, tol):
    if max(samples) - min(samples) <= tol:
        return [(OP_CON, samples[0], 0.0, len(samples) - 1), (OP_CON, samples[0], 0.0, 0)]
    pts = thin(samples, tol)
    keys = [(OP_LIN, v0, 0.0, f1 - f0) for (f0, v0), (f1, _) in zip(pts, pts[1:])]
    keys.append((OP_LIN, pts[-1][1], 0.0, 0))
    return keys


def _brightness(t):
    """Average brightness (0..1) of a PBR picture."""
    import numpy as np
    import gx_tex
    px = np.asarray(gx_tex.decode(t['format'], t['width'], t['height'], t['data']), dtype=float).reshape(-1, 4)
    return float((px[:, :3] @ [0.3, 0.59, 0.11]).mean() / 255.0)


def _dimmed(m, classes, i, f, cache, lift=0):
    """A copy of picture i with its colour times f (an added shine at that strength), CMPR; its number."""
    import numpy as np
    import gx_tex
    key = (i, round(f, 3), lift)
    if key not in cache:
        src = m.images[i]
        px = np.asarray(gx_tex.decode(src.format, src.width, src.height, src.data), dtype=float)
        px[..., :3] *= f
        if lift:
            px[..., :3][px[..., :3].max(axis=2) < lift] = 255
        px[..., 3] = 255
        im = Image()
        im.width, im.height, im.format = src.width, src.height, 14
        im.data = gx_tex.encode_cmpr(np.clip(px + 0.5, 0, 255).astype(np.uint8))
        m.images.append(im)
        classes.append('opa')
        cache[key] = len(m.images) - 1
    return cache[key]


def _mid_sheet(tl):
    """A brightness sheet that is flat mid-grey (Rayquaza's 256x256 on coordinate set 1, Palkia's: 84 to 92 % of the
    pixels between 90 and 170, average 0.5): grey 0x80 means "no change" (a 2x modulate), so multiplied in plainly
    it darkens the whole model by half."""
    import numpy as np
    import gx_tex
    px = np.asarray(gx_tex.decode(tl['format'], tl['width'], tl['height'], tl['data']), dtype=float)[..., 0]
    return 0.4 <= px.mean() / 255.0 <= 0.7 and ((px > 90) & (px < 170)).mean() > 0.8


def _matte_cut(m, classes, i, cache):
    """Castform's eye strip (2026-11-12): each eye frame sits on an opaque WHITE box. On PBR's white face patch the
    box is invisible; our face patch is not quite where the box is, so a white square showed round one eye. For an
    overlay picture made of several opaque boxes (each piece fills its whole rectangle) with near-white rims, the
    light pixels reached from each box's rim are made transparent (the eye stays); RGB5A3 copy, its number.
    Any other picture: i unchanged. Survey of every model's overlay layers: only Castform's strip is like that."""
    if i in cache:
        return cache[i]
    import numpy as np
    import gx_tex
    cache[i] = i
    src = m.images[i]
    px = np.array(gx_tex.decode(src.format, src.width, src.height, src.data), dtype=np.uint8).reshape(src.height, src.width, 4)
    h, w = px.shape[:2]
    opaque = px[..., 3] >= 128
    white = opaque & (px[..., :3].min(axis=2) > 225)
    seen = np.zeros_like(opaque)
    pieces = []
    for y0, x0 in zip(*np.nonzero(opaque)):
        if seen[y0, x0]:
            continue
        st, pts = [(y0, x0)], []
        seen[y0, x0] = True
        while st:
            y, x = st.pop()
            pts.append((y, x))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (y + dy, x + dx)
                if 0 <= q[0] < h and 0 <= q[1] < w and opaque[q] and not seen[q]:
                    seen[q] = True
                    st.append(q)
        ys, xs = zip(*pts)
        pieces.append((min(ys), max(ys), min(xs), max(xs), len(pts)))
    if len(pieces) < 2:
        return i
    rim = []
    for y0, y1, x0, x1, n in pieces:
        if n < 0.98 * (y1 - y0 + 1) * (x1 - x0 + 1):
            return i
        rim += [(y, x) for y in range(y0, y1 + 1) for x in (x0, x1)] + [(y, x) for x in range(x0, x1 + 1) for y in (y0, y1)]
    if sum(white[p] for p in rim) < 0.95 * len(rim):
        return i
    # from the rim inwards through light pixels; their transparency follows their lightness (white = clear, the
    # eye's soft edge fades), so the copy is RGB5A3 (CMPR has only clear or solid: the eye came out blocky)
    lo = 200
    light = opaque & (px[..., :3].min(axis=2) > lo)
    cut = np.zeros_like(opaque)
    st = [p for p in rim if light[p]]
    for p in st:
        cut[p] = True
    while st:
        y, x = st.pop()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (y + dy, x + dx)
            if 0 <= q[0] < h and 0 <= q[1] < w and light[q] and not cut[q]:
                cut[q] = True
                st.append(q)
    mn = px[..., :3].min(axis=2).astype(float)
    px[..., 3] = np.where(cut, np.clip((255 - mn) * 255 / (255 - lo), 0, 255), px[..., 3]).astype(np.uint8)
    px[~opaque] = (255, 255, 255, 0)
    im = Image()
    im.width, im.height, im.format = src.width, src.height, 5
    im.data = gx_tex.encode_rgb5a3(px)
    m.images.append(im)
    classes.append(classes[i])
    cache[i] = len(m.images) - 1
    return cache[i]


def _layer_placement(tx, l, slid):
    """A PBR layer's picture placement (record +04..+14: offset u / v, rotation, scale u / v) onto an HSD texture:
    whole-number scales as HSD's repeat counts (XD's own Skarmory: rep 2, 1), others as HSD's scale (the game
    uses repeat / scale); an offset only where it is not a whole number (whole ones change nothing on a repeating
    picture) and the layer has no slide animation of its own on that axis (that animation sets it), sign turned as
    the animations' are. A whole-number offset on a picture that does not repeat (clamped) is kept: Cacturne's eye
    and mouth strips lie one picture to the right (u 1..2) with offset u -1 and slide only up and down; left out,
    the face showed the strips' green edge (2026-11-12). slid: the axes (1 u, 2 v) the layer's animation moves."""
    su, sv = l['scale']
    rep, sc = [1, 1], [1.0, 1.0]
    for i, x in enumerate((su, sv)):
        if x > 0 and abs(x - round(x)) < 1e-4 and 1 <= round(x) <= 255:
            rep[i] = int(round(x))
        elif abs(x) > 1e-6:
            sc[i] = 1.0 / x
    tx.repeat_s, tx.repeat_t = rep
    tx.scale = (sc[0], sc[1], 1.0)
    tr = [0.0, 0.0]
    for i, (o, wrap) in enumerate(zip(l['offset'], (tx.wrap_s, tx.wrap_t))):
        if i + 1 in slid:
            continue
        if abs(o - round(o)) > 1e-4:
            tr[i] = -(o - math.floor(o))
        elif wrap == 0 and round(o):
            tr[i] = -float(round(o))
    if tr != [0.0, 0.0]:
        tx.trans = (tr[0], tr[1], 0.0)


class StopConversion(Exception):
    """Raised by convert(stats=...) once the measurements are in (survey runs: nothing is written)."""


POSE_TOL_PCT = 1.0      # a model whose converted pose is further than this (% of its height) from the source gets
                        # its sheared bones held by pairs of joints (see convert)
SHEAR_TOL = 0.003       # ... every bone whose 3x3 a single joint holds worse than this (fraction of its size)


def convert(sdr, xd_pkx, xd_model, step=0.1, log=print, compensate='joint8_under_joint', tol_scale=1.0, xd_full=None,
            stats=None, gen4=None, pairs=None, _info=None):
    """pairs: None = automatic: converted with one joint per PBR node first; when the converted pose is more than
    POSE_TOL_PCT of the height away from the source (Maya's segment scale compensate shear, which a T*R*S joint
    cannot hold: Buneary's ears), the model is made again with every bone that has such a shear (> SHEAR_TOL) as a PAIR
    of joints. A set of node numbers = exactly those nodes paired (empty = none). Models within the tolerance are
    not touched."""
    if pairs is None:
        buf, info = [], {}
        res = convert(sdr, xd_pkx, xd_model, step, buf.append, compensate, tol_scale, xd_full, stats, gen4, frozenset(), info)
        if not info.get('need'):
            for l in buf:
                log(l)
            return res
        log('INFO: pose %.2f %% of the height from the source with one joint per bone: %d sheared bone(s) become a pair of joints'
            % (info['pct'], len(info['need'])))
        return convert(sdr, xd_pkx, xd_model, step, log, compensate, tol_scale, xd_full, stats, gen4, frozenset(info['need']))
    import numpy as np
    import pbr_eval
    from model import pose_at
    ev = pbr_eval.Eval(sdr, compensate)
    if gen4 is not None and gen4.get('comp_t'):
        ev.comp_t = True
    # gen4: a Pokemon with no XD model (docs/GEN4.md): xd_pkx / xd_model are a STAND-IN's; gen4 = {'pick_names':
    # slot -> action name, 'roles': XD joint -> (PBR node name, place in the body)} from the stand-in's own pair
    # (gen4.py)
    odd = sorted(set(n.kind for n in sdr.nodes if n.kind not in (0, 2, 3, 6)))
    if odd:
        log('NOTE: node kind %s (not known what it is): kept as a plain joint' % odd)
    m = Model()
    # --- joints: XD's two top joints, every PBR node, then one holder joint per skinned mesh node ---
    # (XD's skeleton root is joint 0 itself in two thirds of its models, joint 1 to 4 in most others: ours is
    # always joint 1, holding whatever XD has between joint 0 and its skeleton root)
    from model import mat_decompose
    x0 = xd_model.joints[0] if gen4 is None else Joint()
    x1 = Joint()
    if xd_model.skeleton_root > 0 and gen4 is None:
        xs = xd_model.world_static()
        xm1 = mat_mul(mat_inv(xs[0]), xs[xd_model.skeleton_root])
        x1.scale, x1.rot, x1.pos = mat_decompose(xm1)
        # PBR's own top nodes often hold the very same transform (same artists' files: Slaking's "ani_top" = XD's
        # joint 1, turn -103 deg, scale 0.85; Vigoroth's "null_broll"). Copied as joint 1 as well, the model got it
        # TWICE: Slaking upside down, Vigoroth on its back (2026-11-12). Where a PBR node above the first bone holds
        # XD's transform, joint 1 stays plain.
        w0 = ev.world(ev.sample(None).local)[0]
        xm1 = np.vstack([np.array(xm1, dtype=float).reshape(3, 4), [0, 0, 0, 1]])
        for i, nd in enumerate(sdr.nodes):
            if nd.kind == 2:
                break
            # a node that is not plain, at the same place (within 10 % of the offset: Vigoroth's 9 %) and turned alike (within 20 deg: Vigoroth's PBR node rests at
            # -88 deg where XD's joint says -79)
            dt = np.linalg.norm(w0[i][:3, 3] - xm1[:3, 3])
            ra, rb = w0[i][:3, :3], xm1[:3, :3]
            ra = ra / np.cbrt(max(abs(np.linalg.det(ra)), 1e-12))
            rb = rb / np.cbrt(max(abs(np.linalg.det(rb)), 1e-12))
            turn = math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(ra.T @ rb) - 1) / 2))))
            plain = np.linalg.norm(w0[i][:3, 3]) < 0.01 and np.abs(ra - np.eye(3)).max() < 0.01
            if not plain and dt < 0.1 * np.linalg.norm(xm1[:3, 3]) + 0.01 and turn < 20:
                log('INFO: PBR node %s already holds the XD transform above the skeleton: not added twice' % nd.name)
                x1 = Joint()
                break
    for src in (x0, x1):
        j = Joint()
        j.parent = len(m.joints) - 1
        j.rot, j.scale, j.pos = src.rot, src.scale, src.pos
        m.joints.append(j)
    m.skeleton_root = 1
    spose = ev.sample(None)
    s0, r0, t0 = pbr_eval.decompose(spose, ev, pairs=pairs)
    # Joint numbers (the game numbers a model's joints depth first, and XD's tables and our tracks go by those
    # numbers): node i is joint jn[i]; a node held by a PAIR of joints has the pair's first joint (translation,
    # compensating scale) just before it, ja[i] = jn[i] - 1, and children, meshes and skin weights stay on jn[i]
    # (the second joint: rotation, the node's own scale). pl = the paired nodes; extended entry nn + k = ja of pl[k].
    pl = sorted(pairs)
    nn = len(sdr.nodes)
    jn, ja, j_ = [], {}, 2
    for i in range(nn):
        if i in pairs:
            ja[i] = j_
            j_ += 1
        jn.append(j_)
        j_ += 1
    node_of = {j: i for i, j in enumerate(jn)}
    ext_joint = list(jn) + [ja[i] for i in pl]       # extended entry -> joint number
    for i, n in enumerate(sdr.nodes):
        par = jn[n.parent] if n.parent >= 0 else 1
        if i in pairs:
            j = Joint()
            j.parent = par
            k = pl.index(i)
            j.scale, j.rot, j.pos = tuple(s0[0, nn + k]), tuple(r0[0, nn + k]), tuple(t0[0, nn + k])
            m.joints.append(j)
            par = ja[i]
        j = Joint()
        j.name = n.name
        j.parent = par
        j.scale, j.rot, j.pos = tuple(s0[0, i]), tuple(r0[0, i]), tuple(t0[0, i])
        if n.inv_bind is not None:
            j.inv_bind = list(n.inv_bind)
        m.joints.append(j)
    holder = {}
    for i, n in enumerate(sdr.nodes):
        if n.kind == 3 and n.mesh is not None and n.mesh['weights'] is not None:
            j = Joint()
            j.name = n.name
            j.parent = 1
            holder[i] = len(m.joints)
            m.joints.append(j)
    # --- textures and materials ---
    alpha = []
    for t in sdr.textures:
        im = Image()
        if t['format'] is None:
            raise ValueError('texture format code 0x%X not known' % t['code'])
        im.width, im.height, im.format, im.data = t['width'], t['height'], t['format'], t['data']
        if im.format == 10:
            # a palette picture (Regigigas's body): drawn out into colours, kept as RGB5A3
            import gx_tex
            im.data = gx_tex.encode_rgb5a3(gx_tex.decode_c14x2(im.width, im.height, im.data, t['palette_format']))
            im.format = 5
        if im.format in (8, 9, 10):
            raise ValueError('palette texture: not handled')
        want = hsd_read.image_bytes(im.width, im.height, im.format)
        if len(im.data) < want:
            raise ValueError('texture data shorter than its size says')
        im.data = im.data[:want]
        alpha.append(texture_alpha(im.format, im.width, im.height, im.data))
        m.images.append(im)
    classes = {}
    # A textured material on a mesh without texture coordinates is a reflection map (Steelix's and Metagross's
    # metal: the picture is placed by the surface's direction). XD's own Steelix writes those as flags 0x00030081
    # (coordinates: reflection; light map: extra), blend 0.65 (dump_hsd on XD's file); we write the same.
    no_uv = set()
    with_uv = set()
    for n in sdr.nodes:
        if n.kind == 3 and n.mesh is not None and not n.name.startswith('col_'):
            for part in n.mesh['parts']:
                (with_uv if n.mesh['uvs'] else no_uv).add(part['material'])
    reflect = no_uv - with_uv
    # Materials of one name are one material in the artist's tool: Steelix's "ref_E3" is on seven segments without
    # coordinates and one with a few; all eight are reflection.
    ref_names = set(sdr.materials[i]['name'] for i in reflect if i is not None)
    reflect |= set(i for i, pm in enumerate(sdr.materials) if pm['name'] in ref_names and pm['texture'] is not None)
    layer_tex = {}       # (material, PBR layer) -> output texture number (GX texture map), for the eye animations
    dimmed = {}          # (picture, strength) -> darkened copy (shine)
    mattes = {}          # picture -> copy without white boxes round its pieces (Castform's eyes)
    for mi, pm in enumerate(sdr.materials):
        ma = Material()
        ma.name = pm['name']
        ma.draw_class = 'opa'
        # PBR's picture layers (2026-11-12): up to four per material, each placed by one of the mesh's coordinate
        # sets or reflected; Dusclops's face = skin + eye white + eye strip (the blink slides the third). Written as
        # XD's own layered materials are (XD's Dusclops, Registeel): the first coordinate layer as before, further
        # coordinate layers pasted on by their picture's transparency (0x00010010), reflection layers last, the same
        # way (0x00010081). Layers in that order: a material whose layer 0 is a reflection gets it on top.
        orig = [k for k, l in enumerate(pm.get('layers', [])) if l['texture'] is not None]
        layers = [pm['layers'][k] for k in orig]
        if not layers and pm['texture'] is not None:
            layers = [{'texture': pm['texture'], 'kind': 'uv', 'uvset': 0, 'offset': (0.0, 0.0), 'rotation': 0.0,
                       'scale': (1.0, 1.0)}]
        order = [k for k, l in enumerate(layers) if l['kind'] == 'uv'] + [k for k, l in enumerate(layers) if l['kind'] != 'uv']
        # Shine (2026-11-12, by comparing with XD's own models): a reflection on top is ADDED (Luvdisc's and
        # Wailord's nearly black shine sheets change nothing), and a brightness-only coordinate layer next to it is
        # where the shine goes (Huntail's: average 13 of 255 = almost none; Scizor's: 225). HSD cannot multiply two
        # layers before adding, so the shine picture is a copy darkened by the mask's average and the mask is left
        # out. Without a mask it depends on the picture (below: greyscale pasted by its brightness, opaque colour
        # blended 0.4, intensity + alpha added at half strength). A layer showing the base picture again (Onix,
        # Magcargo: same picture twice) is left out too: multiplied by itself it came out dark.
        shine, masked = 1.0, False
        # PBR's own shine settings (found 2026-11-12, every layered material agrees): record +0x76 = the shine
        # layer (a reflection), +0x74 = the layer masking it (0xFF none), +0x78 = the shine's colour = its strength
        # (Registeel 0x66 = 40 %, XD's own Registeel blends 0.4; Nosepass 0x0A = 4 %; Milotic and Voltorb 0x26;
        # Glalie's ice 0x4D). Where they are given, the shine is ADDED at that strength (times the mask's average);
        # the guesses below stay for materials without them.
        raw = pm['raw']
        pos = {o: k for k, o in enumerate(orig)}
        spec = pos.get(raw[0x76]) if raw[0x76] != 0xFF else None
        if spec is not None and layers[spec]['kind'] == 'uv':
            spec = None
        if spec == 0 and any(l['kind'] == 'uv' for l in layers):
            # the shine as PBR's FIRST layer with a picture after it (Registeel's metal): added on top it came out
            # white; the guesses below (pasted by brightness) match XD's own Registeel
            spec = None
        spec_f = None
        if spec is not None:
            spec_f = sum(raw[0x78:0x7B]) / 765.0
            mk = pos.get(raw[0x74]) if raw[0x74] != 0xFF else None
            if mk is not None and mk != spec and layers[mk]['kind'] == 'uv':
                tm = sdr.textures[layers[mk]['texture']]
                if tm['format'] in (0, 1):
                    spec_f *= _brightness(tm)
                    order = [k for k in order if k != mk]
        if spec is not None:
            pass
        elif order and any(layers[k]['kind'] != 'uv' for k in order[1:]):
            masks = [k for k in order[1:] if layers[k]['kind'] == 'uv'
                     and sdr.textures[layers[k]['texture']]['format'] in (0, 1)]
            shine = 0.5
            if masks:
                shine, masked = 1.0, True
                for k in masks:
                    shine *= _brightness(sdr.textures[layers[k]['texture']])
                order = [k for k in order if k not in masks]
        if order:
            base_px = sdr.textures[layers[order[0]]['texture']]['data']
            order = order[:1] + [k for k in order[1:] if sdr.textures[layers[k]['texture']]['data'] != base_px]
        if not (raw[0x5D] & 0x01) and (raw[0x5D] & 0x02) and order:
            # a brightness sheet on an unlit glowing material (Torkoal's shell holes: a scrolling flicker) is left
            # out: multiplied in it halved the glow, added it went pink-white; XD's own holes are plain bright red
            order = order[:1] + [k for k in order[1:] if not (layers[k]['kind'] == 'uv'
                                 and sdr.textures[layers[k]['texture']]['format'] in (0, 1))]
        animated = {}             # layer -> the axes its slide animation moves (1 u, 2 v)
        for k_, blocks in (pm.get('anims') or ({0: pm['anim']} if pm.get('anim') else {})).items():
            animated[k_] = {c.axis for chs in blocks.values() for c in chs or [] if c.what == 0x14}
        if layers:
            first = layers[order[0]]
            tx = Texture()
            tx.image = first['texture']
            t = sdr.textures[first['texture']]
            tx.wrap_s, tx.wrap_t = t['wrap_s'], t['wrap_t']
            ma.draw_class = alpha[first['texture']]
            if mi in reflect or first['kind'] != 'uv':
                ma.draw_class = 'opa'
                tx.flags, tx.blending = 0x00030081, 0.65
                if order[0] == spec:
                    # the shine on the material's own colour (Glalie's ice, Unown, Anorith's eye glass, Registeel's
                    # limbs): BLENDED at its strength (added, Registeel's limbs came out white)
                    tx.blending = max(0.05, spec_f)
                if first['kind'] != 'uv':
                    # only a reflection: what it is mixed with is the material's own colour (Unown's black body:
                    # ambient and diffuse 0; Glalie's ice: pale blue)
                    ma.ambient, ma.diffuse = tuple(pm['ambient']), tuple(pm['diffuse'])
            elif t['format'] in (0, 1) and (pm['raw'][0x5D] & 0x60) == 0x60 and pm['ambient'][:3] != (255, 255, 255):
                # A brightness-only picture on a SOLID material (flag byte 0x60 on; Gastly's see-through glow has it
                # off) that is not self-lit (ambient 255: Sableye's eye glow): Lairon's lower body, Clamperl's lid
                # inside, Nosepass's dark face, Sandshrew's and Dratini's bellies. A grey picture drawn as any
                # picture is (multiplied by PBR's diffuse 204 the white bellies came out grey). Tried once before the
                # reflection layers were told apart: it broke Anorith's eyes and Magnemite's body, which are
                # reflections (layer record kind 0) and handled above now (2026-11-12)
                ma.draw_class = 'opa'
            elif ma.draw_class != 'opa':
                tx.flags = 0x00230010
                ma.rendermode = 0x40002015 if ma.draw_class == 'texedge' else 0x60002015
                if t['format'] in (0, 1):
                    # A brightness-only sheet (Gastly's glow) takes its colour from the material: PBR's diffuse
                    # colour (record +0x64) times the picture (HSD colour map "modulate" instead of "blend").
                    ma.diffuse = tuple(pm['diffuse'])
                    tx.flags = 0x00240010
                    fl0 = pm['raw'][0x5D]
                    if (fl0 & 0x01) and not (fl0 & 0x62):
                        # lit, see-through, no glow colour: only Gastly's aura (survey 2026-11-12). Its diffuse is
                        # a very dark purple (30, 16, 37): lit as ours are it came out a black haze where XD's own
                        # aura is a bright purple drawn unlit (rendermode 0x60002011). The hue at full brightness,
                        # unlit
                        top = max(max(pm['diffuse'][:3]), 1)
                        c = tuple(min(255, round(x * 255 / top)) for x in pm['diffuse'][:3]) + (255,)
                        ma.diffuse = ma.ambient = c
                        ma.rendermode &= ~0x4
            # PBR's flag byte (record +0x5D; survey of all 3,900 materials 2026-11-12): bit 0x01 = lit, 0x02 = has a
            # glow colour (+0x70), 0x60 both off on see-through glows. Unlit + glow colour (Chinchou's and Lanturn's
            # lures: ambient and diffuse black, so lit they came out black; Torkoal's shell holes: ambient white + a
            # faint red glow): drawn unlit in ambient + glow times the picture, as XD's own lures are (rendermode
            # 0x60002011); the see-through kind ADDED to the picture behind with XD's own lure blend
            # (pe 19 00 00 00 01 04 01 05 03 07 00 07: blend, source alpha + one).
            fl = pm['raw'][0x5D]
            glow = tuple(pm['raw'][0x70:0x74])
            if not (fl & 0x01) and (fl & 0x02) and glow[:3] != (0, 0, 0):
                c = tuple(min(255, a + g) for a, g in zip(pm['ambient'][:3], glow[:3])) + (255,)
                ma.diffuse = ma.ambient = c
                tx.flags = 0x00240010
                if fl & 0x60:
                    ma.rendermode = ma.rendermode & ~0x4
                else:
                    ma.draw_class = 'xlu'
                    ma.rendermode = 0x60002011
                    ma.pe = bytes.fromhex('190000000104010503070007')
            ma.textures.append(tx)
            for q, k in enumerate(order):
                l = layers[k]
                if q:
                    tx = Texture()
                    tx.image = l['texture']
                    tl = sdr.textures[l['texture']]
                    tx.wrap_s, tx.wrap_t = tl['wrap_s'], tl['wrap_t']
                    # How a layer goes on top (2026-11-12), by what its picture holds:
                    #  - brightness only (I4 / I8): a coordinate layer is shading detail, multiplied in (colour map
                    #    "modulate"; pasted, it painted Scizor white); a reflection is pasted by its transparency,
                    #    which in these sheets is the brightness (Luvdisc's dark shine: almost nothing; Registeel's
                    #    metal: about half, as XD's own Registeel's blend 0.4)
                    #  - with transparency of its own: pasted by it (Dusclops's eye, Castform's eyes)
                    #  - opaque colour: coordinate layer multiplied in
                    #  - a reflection: added (see "Shine" above)
                    grey = tl['format'] in (0, 1)
                    see = alpha[l['texture']] != 'opa'
                    mid = l['kind'] == 'uv' and grey and k > 1 and _mid_sheet(tl)
                    # a nearly black sheet added next to such a mid-grey sheet (Rayquaza's white rings on black)
                    # washed XD's yellow rings out to white: added at 0.3 strength (rings yellow as in XD)
                    ring_add = any(j > 1 and layers[j]['kind'] == 'uv' and sdr.textures[layers[j]['texture']]['format'] in (0, 1)
                                   and _mid_sheet(sdr.textures[layers[j]['texture']]) for j in order)
                    if k == spec:
                        tx.flags, tx.blending = 0x00070081, 1.0
                        if spec_f < 0.98:
                            tx.image = _dimmed(m, alpha, l['texture'], spec_f, dimmed)
                    elif l['kind'] == 'uv' and (grey or not see) and _brightness(tl) < 0.3:
                        # a nearly black brightness sheet that is not a shine mask (Huntail's repeated speckle,
                        # average 13 of 255; Corphish's, Shelgon's, Regirock's: 0 to 22 %): multiplied in it
                        # blackened the body; added it changes little. The same for an opaque colour sheet that is
                        # nearly black (Kyogre's: its red line pattern on black, average 2 %: the body went black)
                        tx.flags, tx.blending = 0x00070010, 1.0
                        if ring_add:
                            tx.image = _dimmed(m, alpha, l['texture'], 0.3, dimmed)
                    elif l['kind'] == 'uv':
                        tx.flags = 0x00010010 if see and not grey else 0x00040010
                        tx.blending = 1.0
                        if mid:
                            # 2x brightened (clamped); its dark lines (the ring outlines, under 0x50) lifted to
                            # white so they no longer blacken the yellow rings of the base picture
                            tx.image = _dimmed(m, alpha, l['texture'], 2.0, dimmed, 160)
                        if see and not grey:
                            tx.image = _matte_cut(m, alpha, l['texture'], mattes)
                    elif shine < 1.0 and not masked and tl['format'] in (0, 1):
                        # brightness only, no mask: pasted by its transparency (= brightness): Registeel's metal,
                        # Voltorb's highlight (added at half strength Voltorb went pink, 2026-11-12)
                        tx.flags, tx.blending = 0x00010081, 1.0
                    elif not masked and tl['format'] not in (2, 3) and not see:
                        # opaque colour, no mask: blended 0.4, as XD's own Registeel (added, Nosepass went pale)
                        tx.flags, tx.blending = 0x00030081, 0.4
                    else:
                        tx.flags, tx.blending = 0x00070081, 1.0
                        if shine < 0.98:
                            tx.image = _dimmed(m, alpha, l['texture'], shine, dimmed)
                    ma.textures.append(tx)
                    ma.rendermode |= 0x10 << q
                tx = ma.textures[q]
                tx.tex_id = q
                layer_tex[(mi, k)] = q
                if l['kind'] == 'uv' and not (q == 0 and mi in reflect):
                    tx.src = 4 + min(l['uvset'], 2)
                    _layer_placement(tx, l, animated.get(k, set()))
            # A see-through material (diffuse alpha, record +0x67, below 255; survey 2026-11-12): Anorith's eye
            # glass (26 of 255: drawn solid it hid the eye as a grey ball), Ninjask's and Ledyba's wings, Starmie's
            # and Staryu's jewels, Sableye's gems. Drawn as HSD's see-through kind with the material's alpha
            # (rendermode alpha from the material 0x2000, XLU + no depth write 0x60000000, as XD's own xlu pieces).
            # The untextured "lambert1" ones are PBR's touch boxes (left out above). Only where it looked right in
            # game: plain picture layers on a solid-flagged material (0x60: wings) or reflection layers only (glass).
            # A picture + reflection material (Milotic's body: 128) went see-through all over, and Torkoal's glowing
            # shell holes (flag 0x81) came out dull: there the number means something else.
            kinds = set(l['kind'] for l in layers)
            if pm['diffuse'][3] < 255 and (kinds == {'uv'} and (pm['raw'][0x5D] & 0x60) == 0x60 or kinds == {'reflect'}):
                ma.alpha = pm['diffuse'][3] / 255.0
                ma.draw_class = 'xlu'
                ma.rendermode |= 0x60002000
        else:
            ma.rendermode = 0x2005
            # No picture: the colour is the material's own (PBR record +0x60 ambient, +0x64 diffuse: black eyes
            # and mouths, grey teeth). Left at XD's white they were white (Ditto's eyes, 2026-11-12).
            ma.ambient, ma.diffuse = tuple(pm['ambient']), tuple(pm['diffuse'])
        m.materials.append(ma)
    # --- meshes ---
    static_world_pbr = ev.world(spose.local)[0]
    combos = set()
    world_static = m.world_static()
    root_inv = mat_inv(world_static[1])
    hidden_boxes = 0
    for i, n in enumerate(sdr.nodes):
        if n.kind != 3 or n.mesh is None:
            continue
        if n.name.startswith('col_'):
            # PBR's touch boxes (col_faceShape, col_lhandShape ...: 12 untextured triangles each): never drawn by
            # PBR; drawn, they are the white boxes over Butterfree's body (2026-11-12). Kept out of the model.
            hidden_boxes += 1
            continue
        if 'shadow' in n.name.lower() and all(sdr.materials[p_['material']]['texture'] is None for p_ in n.mesh['parts']):
            # Jumpluff's untextured "lhandShadow" / "rhandShadow" blobs round its puffs (the only ones, survey
            # 2026-11-12): shadow helpers, drawn they were grey boxes on the puffs. Left out like the touch boxes.
            hidden_boxes += 1
            continue
        pm = n.mesh
        # A mesh node with a mirroring transform (scale -1: Makuhita's body, Golduck's, Entei's wing pieces; 7
        # models, 2026-11-12): PBR turns its triangles' front side round; XD's game does not, so the body showed
        # its inside (Makuhita looked turned away and dark). Its triangles are written the other way round.
        mirrored = np.linalg.det(static_world_pbr[i][:3, :3]) < 0
        for part in pm['parts']:
            me = Mesh()
            me.rigid = pm['weights'] is None
            me.joint = jn[i] if me.rigid else holder[i]
            me.material = part['material']
            me.name = n.name
            index = {}
            for tri in part['tris']:
                out = []
                for (pi, ni, ti, *more) in tri:
                    pos = pm['verts'][pi][0]
                    nrm = pm['verts'][ni if ni is not None else pi][1]
                    uv = pm['uvs'][ti] if ti is not None and ti < len(pm['uvs']) else None
                    uvx = None
                    if uv and len(pm['uvsets']) > 1:
                        uvx = tuple(tuple(pm['uvsets'][k + 1][x]) if x is not None and k + 1 < len(pm['uvsets'])
                                    and x < len(pm['uvsets'][k + 1]) else None for k, x in enumerate(more[:2]))
                    w = None
                    if not me.rigid:
                        w = quantise_weights(pm['weights'][pi] or {n.number: 1.0}, step)
                        w = tuple((jn[sdr.by_number[j]], x) for j, x in w)
                        combos.add(w)
                    v = Vertex(tuple(pos), tuple(nrm), tuple(uv) if uv else None, None, w, uvx)
                    k = v.key()
                    vi = index.get(k)
                    if vi is None:
                        vi = index[k] = len(me.verts)
                        me.verts.append(v)
                    out.append(vi)
                if mirrored:
                    out = [out[0], out[2], out[1]]
                if len(set(out)) == 3:
                    me.tris.append(tuple(out))
            if me.tris:
                m.meshes.append(me)
                c = m.materials[me.material].draw_class if me.material is not None else 'opa'
                classes[c] = classes.get(c, 0) + 1
    if hidden_boxes:
        log('INFO: %d touch box(es) (col_ meshes) left out' % hidden_boxes)
    if reflect:
        log('INFO: %d reflection-mapped material(s) (texture on a mesh without coordinates)' % len(reflect))
    if classes.get('texedge'):
        log('INFO: %d cut-out mesh part(s)' % classes['texedge'])
    if classes.get('xlu'):
        log('INFO: %d see-through (blended) mesh part(s)' % classes['xlu'])
    for me in m.meshes:
        if me.rigid:
            continue
        for v in me.verts:
            for j, w in v.weights:
                if m.joints[j].inv_bind is None:
                    log('NOTE: skin weight on a node that is not a joint: bound where it stands')
                    m.joints[j].inv_bind = mat_inv(mat_mul(root_inv, world_static[j]))
    log('skin: %d different weight combinations at step %g' % (len(combos), step))
    # --- which PBR action plays in which XD slot ---
    xd_frames = [int(round(a.end())) for a in xd_model.animations]
    if gen4 is None:
        pick, how, users = pick_actions(sdr, xd_pkx, xd_frames, log)
    else:
        pick, how, users = pick_actions_named(sdr, xd_pkx, xd_frames, gen4['pick_names'], log)
    if stats is not None:
        stats['pick_names'] = {i: sdr.actions[a][0] for i, a in enumerate(pick) if a is not None}
        stats['pick_times'] = {i: (sdr.actions[a][2], sdr.actions[a][3]) for i, a in enumerate(pick) if a is not None}
    # Retiming (no-counterpart models): an action the stand-in plays in a slot is stretched so that its HIT lands at
    # the stand-in's hit and it ends when the stand-in's does (PBR's action table: start, hit, end), because XD's
    # move scripts time their effects against the slot, not against the model
    tmaps = {}
    if gen4 is not None:
        for slot, a in enumerate(pick):
            tgt = gen4['pick_times'].get(slot)
            if a is None or how.get(slot) != 'name' or a in tmaps or not tgt:
                continue
            hs, es = tgt
            hg, eg = sdr.actions[a][2], sdr.actions[a][3]
            if hs <= 0.05 or hg <= 0.05 or hs >= es - 0.05 or hg >= eg - 0.05:
                k = eg / es
                fn = (lambda t, k=k: t * k)
            else:
                fn = (lambda t, hs=hs, es=es, hg=hg, eg=eg: t * hg / hs if t <= hs else hg + (t - hs) * (eg - hg) / (es - hs))
            tmaps[a] = (max(int(round(es * FPS)), 1), fn)
    # --- eyes: XD's face records (sleep, wake up, blink) name an animation slot each; that slot gets PBR's tx_
    # action of the same kind (its body stays as it is: only the picture slides) ---
    names = [x[0] for x in sdr.actions]
    eye_mats = [i for i, pm in enumerate(sdr.materials) if pm.get('anim')]
    face_slots, face_kept = {}, set()
    for r, want in enumerate(FACE_ACTIONS):
        rec = 0x1C + r * 0x13
        on, idx = xd_pkx.header[rec], xd_pkx.header[rec + 0x12]
        if on and want and want in names and eye_mats and idx < len(xd_frames):
            pick[idx], how[idx] = names.index(want), 'face'
            face_slots[idx] = names.index(want)
            face_kept.add(r)
    if eye_mats:
        log('INFO: eyes: %d material(s) animated; face records kept: %s' % (
            len(eye_mats), ', '.join(FACE_ACTIONS[r] for r in sorted(face_kept)) or 'none'))
    idle_slot = 0
    for kind, idx in xd_pkx.section_motions(0)[1] if xd_pkx.sections else []:
        if kind and 0 <= idx < len(xd_frames):
            idle_slot = idx
            break
    # --- size: the PBR model is brought to the size XD's model has on the field ---
    j0m = np.eye(4)
    j0m[:3, :] = np.array(mat_srt(x0.scale, x0.rot, x0.pos)).reshape(3, 4)

    def root_matrix(scale):
        r = np.eye(4)
        r[:3, :] = np.array(mat_srt(scale, x1.rot, x1.pos)).reshape(3, 4)
        return j0m @ r

    poses = {a: (ev.sample(a, tmaps[a][0], tmaps[a][1]) if a in tmaps else ev.sample(a))
             for a in sorted(set(x for x in pick if x is not None))}
    idle_act = pick[idle_slot] if pick else None
    xb = xd_model.animations[idle_slot].bounds if xd_model.animations else None
    if stats is not None and idle_act is not None:
        # survey hook (changes nothing in the output): PBR's native idle box (its own top nodes, no XD transform) and
        # XD's idle box, mean extent per axis over the frames
        po = poses[idle_act]
        bo = pbr_eval.boxes(ev.points(np.eye(4)[None, None] @ ev.world(po.local), po.visible))
        stats['pbr_native'] = tuple(float(x) for x in (bo[:, 3:] - bo[:, :3]).mean(axis=0))
        stats['pbr_native_box'] = tuple(float(x) for x in bo.mean(axis=0))
        if xb:
            xbn_ = np.array(xb)
            stats['xd_idle'] = tuple(float(x) for x in (xbn_[:, 3:] - xbn_[:, :3]).mean(axis=0))
            stats['xd_idle_box'] = tuple(float(x) for x in xbn_.mean(axis=0))
        stats['idle_action'] = sdr.actions[idle_act][0]
        if stats.get('stop'):
            raise StopConversion()
    if idle_act is not None and xb and (any(abs(v) > 1e-4 for v in tuple(x1.rot) + tuple(x1.pos))
                                        or any(abs(v - 1) > 1e-4 for v in x1.scale)):
        # XD's transform above its skeleton, or none? PBR's top nodes may hold part of it already (Entei: XD's
        # joints = size 4.59 and a quarter turn, PBR's "kime_skl" = the size alone: copied, the size was taken
        # twice and the body turned). Both tried; the one whose idle pose fits XD's idle box better is kept.
        po = poses[idle_act]
        xbn = np.array(xb)

        def fit(c):
            r = np.eye(4)
            r[:3, :] = np.array(mat_srt(c.scale, c.rot, c.pos)).reshape(3, 4)
            pts = ev.world(po.local)
            bo = pbr_eval.boxes(ev.points((j0m @ r)[None, None] @ pts, po.visible))
            q = float((xbn[:, 3:] - xbn[:, :3]).mean(axis=0).sum() / max((bo[:, 3:] - bo[:, :3]).mean(axis=0).sum(), 1e-9))
            r[:3, :3] *= q
            bo = pbr_eval.boxes(ev.points((j0m @ r)[None, None] @ pts, po.visible))
            h = float(max((bo[0, 3:] - bo[0, :3]).max(), 1e-6))
            n = min(len(bo), len(xbn))
            return float(np.abs(bo[:n] - xbn[:n]).mean() / h)
        plain = Joint()
        d_xd, d_plain = fit(x1), fit(plain)
        if d_plain < 0.7 * d_xd:
            log('INFO: the XD transform above the skeleton left out: idle box %.1f %% apart with it, %.1f %% without'
                % (100 * d_xd, 100 * d_plain))
            x1 = plain
            m.joints[1].rot, m.joints[1].pos = x1.rot, x1.pos
    scale1 = tuple(x1.scale)
    if gen4 is not None:
        log("INFO: size: PBR's own (its units are XD's: median XD/PBR idle box 1.005 over the 387 shared models)")
    elif idle_act is not None and xb:
        po = poses[idle_act]
        ours = pbr_eval.boxes(ev.points(root_matrix(scale1)[None, None] @ ev.world(po.local), po.visible))
        xbn = np.array(xb)
        eo = (ours[:, 3:] - ours[:, :3]).mean(axis=0)
        ex = (xbn[:, 3:] - xbn[:, :3]).mean(axis=0)
        ratio = float(ex.sum() / max(eo.sum(), 1e-9))
        if abs(ratio - 1.0) > 0.04:
            scale1 = tuple(c * ratio for c in scale1)
            log('INFO: size factor %.3f on top of XD\'s own (the PBR model is made in another size)' % ratio)
        if ratio > 2.5 or ratio < 0.4:
            log('NOTE: size factor far from 1: PBR\'s model or its idle pose is very different from XD\'s')
    m.joints[1].scale = scale1
    root = root_matrix(scale1)
    # --- every used action sampled, taken apart, and checked against the source ---
    data = {}
    worst = (0.0, '', 0)
    height = None
    for a, po in poses.items():
        s, r, t = pbr_eval.decompose(po, ev, pairs=pairs)
        rw = root[None, None] @ ev.world(po.local)
        ref = ev.points(rw, None)
        got = ev.points(root[None, None] @ ev.world(pbr_eval.compose_local(s, r, t, nn, pairs)), None)
        bx = pbr_eval.boxes(ev.points(rw, po.visible))
        if height is None or a == idle_act:
            height = float(max((bx[0, 3:] - bx[0, :3]).max(), 1e-6))
        for (_, p), (_, q) in zip(ref, got):
            d = np.abs(p - q).max(axis=(1, 2))
            f = int(d.argmax())
            if d[f] > worst[0]:
                worst = (float(d[f]), sdr.actions[a][0], f)
        data[a] = [s, r, t, po.visible, bx]
    if height:
        pct = 100.0 * worst[0] / height
        if _info is not None and not pairs and pct > POSE_TOL_PCT:
            err = np.zeros(nn)
            for a in poses:
                err = np.maximum(err, pbr_eval.shear_error(poses[a], ev))
            need = [int(i) for i in np.nonzero(err > SHEAR_TOL)[0]]
            if need:
                _info['need'], _info['pct'] = need, pct
                return None, None
        log('CHECK: converted pose against the source, largest distance %.2f %% of the height (%s frame %d)' % (pct, worst[1], worst[2]))
        if pct > 3.0:
            log('WRONG: pose differs from the source by more than 3 %% of the height (scale compensation shear)')
        if xb and idle_act is not None and gen4 is None:
            ob = data[idle_act][4]
            xbn = np.array(xb)
            nfr = min(len(ob), len(xbn))
            d = float(np.abs(ob[:nfr] - xbn[:nfr]).mean() / height)
            log('CHECK: idle box against XD\'s own model: %.1f %% of the height apart on average' % (100.0 * d))
    # --- travel: XD's animations stay in place; a PBR action that carries the body away is held back ---
    travel = {}
    for slot, a in enumerate(pick):
        if a is None or slot not in users or a in travel:
            continue
        bx = data[a][4]
        c = (bx[:, :3] + bx[:, 3:]) / 2
        own = float(np.abs(c[:, (0, 2)] - c[0, (0, 2)]).max())
        xown = 0.0
        xbs = xd_model.animations[slot].bounds
        if xbs:
            xc = np.array(xbs)
            xc = (xc[:, :3] + xc[:, 3:]) / 2
            xown = float(np.abs(xc[:, (0, 2)] - xc[0, (0, 2)]).max())
        if own > 0.3 * height and xown < 0.5 * own:
            off = smooth(c - c[0])
            off[:, 1] = 0.0
            travel[a] = -off
            log('NOTE: "%s" carries the body %.0f %% of its height away and XD\'s animation stays in place: travel taken out' % (
                sdr.actions[a][0], 100.0 * own / height))
    # --- channels to write: whatever differs from the static value in any used action ---
    ne = nn + len(pl)
    chan = np.zeros((ne, 3, 3), dtype=bool)
    stat = (s0[0], r0[0], t0[0])
    tols = tuple(x * tol_scale for x in (0.001, 0.0003, 0.001))
    switched = np.zeros(nn, dtype=bool)
    for a in data:
        for w in range(3):
            chan[:, w, :] |= np.abs(data[a][w] - stat[w][None]).max(axis=0) > tols[w]
        switched |= ~data[a][3].all(axis=0)
    vis_nodes = [i for i, n in enumerate(sdr.nodes) if n.kind == 3 and n.mesh is not None and switched[i]]
    if vis_nodes:
        log('INFO: %d mesh node(s) switched on and off by the animations (visibility tracks written)' % len(vis_nodes))
    kinds = (J_SCAX, J_ROTX, J_TRAX)
    j0inv = np.linalg.inv(j0m[:3, :3])
    # where each animated material is drawn: (joint, display object number); the writer makes one display
    # object per mesh, in m.meshes order, on the mesh's joint
    n_dobj, mat_places = {}, {}
    for me in m.meshes:
        k = n_dobj.get(me.joint, 0)
        n_dobj[me.joint] = k + 1
        mat_places.setdefault(me.material, []).append((me.joint, k))

    def material_anims(a, frames):
        """PBR's picture slides of action a -> {joint: [MatAnim or None per display object]} (sign turned: an
        HSD texture's translation moves the other way; XD's own Bulbasaur blink = PBR's, negated)."""
        out = {}
        for mi in eye_mats:
            anims = sdr.materials[mi].get('anims') or {0: sdr.materials[mi]['anim']}
            tas = []
            for layer, blocks in sorted(anims.items()):
                tracks = []
                for c in blocks.get(a) or []:
                    if c.what == 0x14 and c.axis in (1, 2) and c.values:
                        tm = tmaps[a][1] if a in tmaps else (lambda t: t)
                        vals = [-round(c.at(tm(f / FPS)), 4) for f in range(frames + 1)]
                        tracks.append(Track(T_TRAU if c.axis == 1 else T_TRAV, step_keys(vals)))
                if tracks and (mi, layer) in layer_tex:
                    ao = AnimObj()
                    ao.end = float(frames)
                    ao.tracks = tracks
                    ta = TexAnim()
                    ta.tex_id = layer_tex[(mi, layer)]
                    ta.aobj = ao
                    tas.append(ta)
            if not tas:
                continue
            for j, d in mat_places.get(mi, []):
                ma = MatAnim()
                ma.texanims = list(tas)
                out.setdefault(j, [None] * n_dobj[j])[d] = ma
        return out

    made = {}
    for slot, a in enumerate(pick):
        an = Animation()
        if slot in face_slots:
            frames = max(int(round(sdr.actions[a][3] * FPS)), 1)
            an.name = sdr.actions[a][0]
            ao = AnimObj()
            ao.end = float(frames)
            ao.tracks.append(Track(J_SCAX, [(OP_CON, scale1[0], 0.0, frames), (OP_CON, scale1[0], 0.0, 0)]))
            an.joints[1] = ao
            an.materials = material_anims(a, frames)
            m.animations.append(an)
            continue
        if a is None:
            frames = max(xd_frames[slot], 1)
            an.name = 'none'
            ao = AnimObj()
            ao.end = float(frames)
            ao.tracks.append(Track(J_SCAX, [(OP_CON, scale1[0], 0.0, frames), (OP_CON, scale1[0], 0.0, 0)]))
            an.joints[1] = ao
            m.animations.append(an)
            continue
        if a in made:
            an.name, an.joints, an.bounds, an.materials = made[a].name, made[a].joints, made[a].bounds, made[a].materials
            m.animations.append(an)
            continue
        an.name = sdr.actions[a][0]
        s, r, t, vis, bx = data[a]
        frames = s.shape[0] - 1
        an.materials = material_anims(a, frames)
        arrs = (s, r, t)
        for i in range(ne):
            ao = AnimObj()
            ao.end = float(frames)
            for w in range(3):
                for ax in range(3):
                    if chan[i, w, ax]:
                        ao.tracks.append(Track(kinds[w] + ax, keys_of([float(x) for x in arrs[w][:, i, ax]], tols[w])))
            if i < nn and i in vis_nodes and i not in holder:
                ao.tracks.append(Track(11, vis_keys(vis[:, i])))
            if ao.tracks:
                an.joints[ext_joint[i]] = ao
        for i in vis_nodes:
            if i in holder:
                ao = AnimObj()
                ao.end = float(frames)
                ao.tracks.append(Track(11, vis_keys(vis[:, i])))
                an.joints[holder[i]] = ao
        if travel:
            ao = AnimObj()
            ao.end = float(frames)
            if a in travel:
                tv = travel[a] @ j0inv.T + np.array(x1.pos)[None]
                bx = bx.copy()
                bx[:, :3] += travel[a]
                bx[:, 3:] += travel[a]
                for ax in (0, 2):
                    ao.tracks.append(Track(J_TRAX + ax, keys_of([float(x) for x in tv[:, ax]], 0.001)))
            else:
                for ax in (0, 2):
                    ao.tracks.append(Track(J_TRAX + ax, [(OP_CON, x1.pos[ax], 0.0, frames), (OP_CON, x1.pos[ax], 0.0, 0)]))
            an.joints[1] = ao
        an.bounds = [tuple(float(x) for x in row) for row in bx]
        made[a] = an
        m.animations.append(an)
    idle_box = m.animations[idle_slot].bounds if m.animations and m.animations[idle_slot].bounds else None
    if idle_box is None:
        sp = pbr_eval.boxes(ev.points(root[None, None] @ ev.world(spose.local), None))
        idle_box = [tuple(float(x) for x in sp[0])]
    for slot, an in enumerate(m.animations):
        if an.bounds is None:
            an.bounds = [idle_box[0]] * (max(xd_frames[slot], 1) + 1)
    m.bound_head = list(xd_model.bound_head or [])
    # --- XD's container, re-pointed: each XD joint -> the PBR joint nearest to it in the idle pose ---
    xw = pose_at(xd_model, xd_model.animations[idle_slot] if xd_model.animations else None, 0.0)
    ipose = poses[idle_act] if idle_act is not None else spose
    iw = root[None, None] @ ev.world(ipose.local[:1])
    cands = [(jn[i], iw[0, i, :3, 3]) for i, n in enumerate(sdr.nodes) if n.kind in (0, 2)]
    cpos = np.array([c[1] for c in cands])

    # Roles (docs/GEN4.md): an XD attachment joint is the PBR effect point (ef_*, fixed names in every PBR model)
    # nearest to it on the stand-in's own pair; a model with no XD counterpart takes the point of the same name, or
    # else the one at the same place in the body (position as a fraction of the bones' box).
    body = [(jn[i], iw[0, i, :3, 3]) for i, n in enumerate(sdr.nodes) if n.kind == 2 or n.name.startswith('ef_')]
    bpos = np.array([c[1] for c in body]) if body else np.zeros((1, 3))
    blo, bext = bpos.min(axis=0), np.maximum(bpos.max(axis=0) - bpos.min(axis=0), 1e-6)
    efs = [(j, q) for j, q in body if m.joints[j].name.startswith('ef_')] or body

    def norm(p):
        q = (np.asarray(p) - blo) / bext
        return (float(q[0]) - 0.5, float(q[1]), float(q[2]) - 0.5)   # x, z about the middle, y from the feet

    def role_of(p):
        j = min(efs, key=lambda c: float(((c[1] - p) ** 2).sum()))[0]
        return (m.joints[j].name, norm(iw_pos(j)))

    def iw_pos(j):
        return iw[0, node_of[j], :3, 3]

    def gen4_node(xj):
        nm, nr = gen4['roles'][xj]
        for j, q in efs + body:
            if m.joints[j].name == nm:
                return j
        nr = np.array(nr)
        return min(efs, key=lambda c: float(((np.array(norm(c[1])) - nr) ** 2).sum()))[0]

    def remap(xj):
        if xj < 0:
            return xj
        if xj >= len(xd_model.joints):
            return 0
        if xj == 0 or xj == xd_model.skeleton_root:
            return 0 if xj == 0 else 1
        p = np.array([xw[xj][3], xw[xj][7], xw[xj][11]])
        if gen4 is not None:
            return gen4_node(xj)
        if stats is not None:
            stats.setdefault('roles', {})[xj] = role_of(p)
        return cands[int(((cpos - p[None]) ** 2).sum(axis=1).argmin())][0]

    # Camera-facing pieces (2026-11-12): XD marks the joints of its flat cards as billboards (HSD joint flags
    # 0xE00: Jumpluff's three puffs, Gastly's aura); PBR's file has no such mark that was found. Each XD billboard
    # joint -> our nearest rigid textured mesh node in the idle pose, which gets the same billboard kind (seen
    # edge-on, Jumpluff's puffs looked squashed).
    mesh_nodes = [(jn[i], iw[0, i, :3, 3]) for i, n in enumerate(sdr.nodes)
                  if n.kind == 3 and n.mesh is not None and n.mesh['weights'] is None and not n.name.startswith('col_')
                  and any(sdr.materials[p_['material']]['texture'] is not None for p_ in n.mesh['parts'])]
    for xj, jt in enumerate(xd_model.joints):
        bb = (jt.flags or 0) & 0xE00
        if bb and mesh_nodes and xj < len(xw) and gen4 is None:
            p = np.array([xw[xj][3], xw[xj][7], xw[xj][11]])
            d = [float(((q - p) ** 2).sum()) for _, q in mesh_nodes]
            k = int(np.argmin(d))
            m.joints[mesh_nodes[k][0]].billboard = bb
            log('INFO: camera-facing: XD joint %d (0x%X) -> %s' % (xj, bb, m.joints[mesh_nodes[k][0]].name))
    pkx = hsd_read.Pkx()
    pkx.header, pkx.sections, pkx.particles = xd_pkx.header, list(xd_pkx.sections), xd_pkx.particles
    head = pkx.head_joint()
    pkx.set_head_joint(remap(head))
    log('head joint: XD %d -> %d (%s)' % (head, pkx.head_joint(), m.joints[pkx.head_joint()].name))
    table = {}
    for s in range(len(pkx.sections)):
        js = pkx.section_joints(s)
        new = [remap(j) for j in js]
        for a, b in zip(js, new):
            table[a] = b
        pkx.set_section_joints(s, new)
    log('attachment joints, XD -> ours: ' + ', '.join('%d -> %d %s' % (a, b, m.joints[b].name if b >= 0 else '') for a, b in sorted(table.items())))
    h = bytearray(pkx.header)
    for r in range(4):               # face records without a PBR eye action to play: off
        rec = 0x1C + r * 0x13
        if r not in face_kept:
            h[rec] = 0
        elif h[rec] == 2:
            # A "part" record (wazaSequenceSysPartAnimationStart case 2) names XD's own joints and display objects
            # (parts[8], tex[8]); ours are numbered differently, and GSpartSetTexAnimIndex walks the material
            # animation chain of that joint without a null check: a joint without one crashed the game (9 of the
            # roster: yadon, betbeter ...). Our face slots hold only the eye tracks, so the whole-model kind (case 1,
            # same slot id) plays the same thing.
            h[rec] = 1
            h[rec + 1] = 0
            h[rec + 2:rec + 0x12] = b'\xff' * 16
            log('face record %d: part kind -> whole model (slot %d)' % (r, h[rec + 0x12]))
    pkx.header = bytes(h)
    if stats is not None and stats.get('stop_roles'):
        raise StopConversion()
    if xd_full is not None:
        graft_xd_pieces(m, xd_full, xd_full.graft, idle_slot, alpha, log, step)
    m.image_classes = alpha
    log('slots: ' + ' '.join('%d=%s%s' % (i, sdr.actions[a][0] if a is not None else '-', '' if how.get(i) != 'role' else '(role)') for i, a in enumerate(pick)))
    return m, pkx


MAX_SIDE = 256          # XD's own battle models: no picture side above 256 (Battle Revolution has 512)
FILE_CAP = 600 * 1024   # XD's largest own battle model is 542 KB; two to four share the battle heap
MAX_POBJ = 80           # drawing pieces (HSD polygon objects) a model may have; see convert_file
MAX_REFLECT_PIECES = 54  # ... of which this many drawn with a reflection layer: Ditto drew at 48, Kusaihana (Bellossom) at 54,
                         # Ditto at 57 / 59 / 66 never came back from the graphics layer (docs/GEN4.md 10.5, shine_fault.md)


def _po2(n):
    p = 8
    while p < n:
        p *= 2
    return p


def _reencode(im, rgba, w, h, cls):
    import gx_tex
    if cls == 'xlu':
        if im.format in (0, 1, 2, 3):
            im.format, im.data = 3, gx_tex.encode_ia8(rgba)
        else:
            im.format, im.data = 5, gx_tex.encode_rgb5a3(rgba)
    else:
        im.format, im.data = 14, gx_tex.encode_cmpr(rgba)
    im.width, im.height = w, h


def used_images(m):
    """The pictures a material or an animation uses: only those are written (a shine's mask and the picture a
    darkened shine copy replaces are not), so only those count for the file's size."""
    used = set(t.image for ma in m.materials for t in ma.textures if t.image is not None)
    for an in m.animations:
        for lst in an.materials.values():
            for ma in lst or []:
                for ta in (ma.texanims if ma is not None else []):
                    used.update(ta.images)
    return used


def fit_textures(m, classes, log, budget=None):
    """Pictures brought to what XD's models use: sides at most MAX_SIDE and whole blocks (a 98-pixel-wide CMPR
    picture never came back from the graphics card: Snorlax), then, with a budget in bytes, the biggest pictures
    halved until all of them fit. Changed pictures are re-encoded: CMPR (see-through texels kept as CMPR's
    see-through index), or RGB5A3 / IA8 for blended ones. Returns the bytes saved."""
    import gx_tex
    used = used_images(m)
    size = lambda: sum(len(m.images[k].data) for k in used)
    before = size()
    for i, im in enumerate(m.images):
        if i not in used:
            continue
        bw, bh = gx_tex.TILE.get(im.format, (4, 4))
        w, h = im.width, im.height
        nw, nh = w, h
        # a side over MAX_SIDE becomes MAX_SIDE (texture coordinates are fractions of the picture, so any size
        # keeps the placement); halving it until it fit took Smoochum's 528-high eye strip down to 132 and the
        # eyes came out garbled (2026-11-12)
        # A strip of frames (Castform's 64 x 768 eyes, Smoochum's 528-high strip) keeps its length when it holds no
        # more texels than a MAX_SIDE square (GX takes sides up to 1024): squeezed to 256 every eye frame lost two
        # thirds of its height and the eyes came out blocky (2026-11-12)
        if w * h <= MAX_SIDE * MAX_SIDE and max(w, h) <= 1024:
            pass
        else:
            nw, nh = min(nw, MAX_SIDE), min(nh, MAX_SIDE)
        if nw % bw or nh % bh:
            nw, nh = min(_po2(nw), MAX_SIDE), min(_po2(nh), MAX_SIDE)
        if (nw, nh) != (w, h):
            rgba = gx_tex.decode(im.format, w, h, im.data)
            _reencode(im, gx_tex.resize(rgba, nw, nh), nw, nh, classes[i])
    if budget is not None:
        while size() > budget:
            # the biggest picture that may still be halved (below 128 a Pokemon looks blurred: the cap gives way).
            # Picking the biggest of ALL and stopping when it was small stopped Sharpedo at 690 KB (2026-11-12)
            can = [k for k in used if max(m.images[k].width, m.images[k].height) > 128]
            if not can:
                # then opaque 16- and 32-bit pictures stored as CMPR (4 bits a texel, XD's usual), biggest first
                deep = [k for k in used if m.images[k].format in (4, 5, 6) and classes[k] == 'opa']
                if not deep:
                    break
                k = max(deep, key=lambda q: len(m.images[q].data))
                im = m.images[k]
                if im.width % 8 or im.height % 8:
                    break
                rgba = gx_tex.decode(im.format, im.width, im.height, im.data)
                im.format, im.data = 14, gx_tex.encode_cmpr(rgba)
                continue
            i = max(can, key=lambda k: len(m.images[k].data))
            im = m.images[i]
            rgba = gx_tex.decode(im.format, im.width, im.height, im.data)
            _reencode(im, gx_tex.resize(rgba, im.width // 2, im.height // 2), im.width // 2, im.height // 2, classes[i])
    after = size()
    if after != before:
        log('INFO: pictures %d -> %d bytes (largest side %d)' % (before, after, max(max(im.width, im.height) for im in m.images)))
    return before - after


# Species that keep XD's own model: Battle Revolution's model lacks something XD's draws as part of the model
# (checked in the game, whole-roster sheets 2026-11-12). Keyed by the file name (XD's archive name).
# Models whose reflection layers are left out (2026-11-12). Ditto: with its reflection (a 32x32 shine on its one
# skinned body mesh, 64 pieces) the game's picture never comes back from the graphics card (icon renderer: exit 2;
# no error printed; the same layer placed by coordinates draws). Cause not found yet: build\handoff\pbrconv4.md.
def graft_xd_pieces(m, xd, regions, idle_slot, alpha, log, step):
    """XD's own flame pieces put on the PBR body (Ponyta, Rapidash, Moltres, 2026-11-12). PBR draws their flames as
    particle effects the XD game does not have; XD's are see-through meshes with a sliding flame picture. Each such
    mesh (see-through material with a picture animation) is copied in XD's idle pose and cut by region: the XD
    flame bones of each region (GRAFT_XD: head, neck, back, tail, each leg) go to the PBR file's own fire point of
    that region (node kind 7, ef_fireTail ...: where PBR starts its flame effect), moved by the step from the
    region's first XD bone to that point, and bound to it, so they follow the PBR body. Nearest-joint matching
    put Ponyta's tail flame on a hind leg (the two skeletons stand differently: PBR's head and tail are higher).
    The picture slides as in XD, looped over each of our animations' lengths."""
    import copy
    import numpy as np
    from model import pose_at, track_value
    four = lambda x: np.vstack([np.array(x, dtype=float).reshape(3, 4), [0, 0, 0, 1]])
    xa = xd.animations[idle_slot] if idle_slot < len(xd.animations) else None
    xw = [four(w) for w in pose_at(xd, xa, 0.0)]
    ow = [four(w) for w in pose_at(m, m.animations[idle_slot] if idle_slot < len(m.animations) else None, 0.0)]
    world_static = m.world_static()
    root_inv = mat_inv(world_static[1])
    # which display object of its joint each XD mesh is (the material animations are listed that way)
    dobj, count = [], {}
    for me in xd.meshes:
        dobj.append(count.get(me.joint, 0))
        count[me.joint] = dobj[-1] + 1

    def xd_matanim(an, k):
        lst = an.materials.get(xd.meshes[k].joint) or []
        return lst[dobj[k]] if len(lst) > dobj[k] else None

    picked = []
    for k, me in enumerate(xd.meshes):
        ma = xd.materials[me.material] if me.material is not None else None
        if ma is None or not ma.textures or not ma.rendermode & 0x40000000:
            continue
        if not any(xd_matanim(an, k) for an in xd.animations):
            continue
        picked.append(k)
    if not picked:
        log('NOTE: no XD flame pieces found to copy')
        return
    img_map, tlut_map, mat_map = {}, {}, {}

    def image(i):
        if i not in img_map:
            img_map[i] = len(m.images)
            m.images.append(copy.deepcopy(xd.images[i]))
            alpha.append('xlu')
        return img_map[i]

    def tlut(i):
        if i is None:
            return None
        if i not in tlut_map:
            tlut_map[i] = len(m.tluts)
            m.tluts.append(copy.deepcopy(xd.tluts[i]))
        return tlut_map[i]

    by_name = {j.name: i for i, j in enumerate(m.joints) if j.name}
    # The PBR body is bigger in the file than XD's (Ponyta 1.48 x, Rapidash 1.43 x: hips, shoulders, withers
    # measured from the ground): the flames grow with it.
    hs = [ow[by_name[r[0][0]]][1, 3] / xw[r[1][0]][1, 3] for r in regions if abs(xw[r[1][0]][1, 3]) > 1e-3]
    ratio = float(np.median(hs)) if hs else 1.0
    region_of, places = {}, []
    for (pts, xb, bones) in regions:
        qa = by_name[pts[0]]
        x0, y0 = xw[xb[0]][:3, 3], ow[qa][:3, 3]
        rot, qb, u, ln, v_ = np.eye(3), None, None, None, None
        if len(pts) == 2:
            # a span (the mane): XD's segment from bone a to bone b turned onto the PBR one from joint A to joint B,
            # each vertex bound to A and B by how far along it lies. PBR's neck stands more upright than XD's:
            # moved without turning, the mane stood off the neck.
            qb = by_name[pts[1]]
            x1, y1 = xw[xb[1]][:3, 3], ow[qb][:3, 3]
            ln = np.linalg.norm(x1 - x0)
            u, v_ = (x1 - x0) / ln, (y1 - y0) / np.linalg.norm(y1 - y0)
            ax = np.cross(u, v_)
            sn, cs = np.linalg.norm(ax), float(u @ v_)
            k_ = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
            rot = np.eye(3) + k_ + k_ @ k_ * ((1 - cs) / (sn * sn) if sn > 1e-9 else 0.0)
        places.append((x0, y0, rot, qa, qb, u, ln))
        for b in bones:
            region_of[b] = len(places) - 1
    log('flames: body %.2f x the XD size' % ratio)
    holder = len(m.joints)
    hj = Joint()
    hj.name = 'xd_flames'
    hj.parent = 1
    m.joints.append(hj)
    mine = []
    for k in picked:
        me = xd.meshes[k]
        if me.material not in mat_map:
            ma = copy.deepcopy(xd.materials[me.material])
            for t in ma.textures:
                t.image = image(t.image)
                t.tlut = tlut(t.tlut)
            ma.draw_class = 'xlu'
            mat_map[me.material] = len(m.materials)
            m.materials.append(ma)
        out = Mesh()
        out.joint = holder
        out.material = mat_map[me.material]
        out.rigid = False
        out.pobj_flags = me.pobj_flags
        out.name = 'xd_flame_%d' % k
        for v in me.verts:
            p = np.array(list(v.pos) + [1.0])
            if me.rigid or not v.weights:
                world = xw[me.joint] @ p
                nw = xw[me.joint][:3, :3] @ np.array(v.nrm) if v.nrm else None
                ws = {me.joint: 1.0}
            else:
                # weights as fractions of their sum (a Rapidash vertex has 1.0 on two joints at the same place:
                # summed as they stand it went to twice its height, a spike over the mane)
                tw = sum(w for _, w in v.weights) or 1.0
                world = sum(w / tw * (xw[j] @ four(xd.joints[j].inv_bind) @ p) for j, w in v.weights)
                nw = sum(w * (xw[j][:3, :3] @ four(xd.joints[j].inv_bind)[:3, :3] @ np.array(v.nrm))
                         for j, w in v.weights) if v.nrm else None
                ws = {j: w / tw for j, w in v.weights}
            # the region holding most of the vertex's weight moves it (two regions' moves mixed threw a Rapidash
            # tail vertex weighted 20 % to the spine 50 units into the air: the mane's turn applied far from the neck)
            share = {}
            for j, w in ws.items():
                if j not in region_of:
                    raise ValueError('XD flame bone %d has no region in GRAFT_XD' % j)
                share[region_of[j]] = share.get(region_of[j], 0.0) + w
            x0, y0, rot, qa, qb, u, ln = places[max(share, key=share.get)]
            d = world[:3] - x0
            moved = y0 + ratio * (rot @ d)
            if qb is None:
                ours = {qa: 1.0}
            else:
                f = min(max(float(d @ u) / ln, 0.0), 1.0)
                ours = {qa: 1 - f, qb: f}
            wq = quantise_weights(ours, step)
            world = world.copy()
            world[:3] = moved
            a = np.zeros((4, 4))
            for j, w in wq:
                if m.joints[j].inv_bind is None:
                    m.joints[j].inv_bind = mat_inv(mat_mul(root_inv, world_static[j]))
                a += w * (ow[j] @ four(m.joints[j].inv_bind))
            pos = np.linalg.solve(a[:3, :3], world[:3] - a[:3, 3])
            nrm = None
            if nw is not None:
                nb = a[:3, :3].T @ nw
                nb = nb / (np.linalg.norm(nb) or 1.0)
                nrm = tuple(float(x) for x in nb)
            out.verts.append(Vertex(tuple(float(x) for x in pos), nrm, v.uv, v.color, wq, v.uvx))
        out.tris = list(me.tris)
        m.meshes.append(out)
        mine.append(k)
    # picture slides: XD's track of the same slot (else the idle one), looped over our slot's length
    for slot, an in enumerate(m.animations):
        frames = max(int(round(max([a.end for a in an.joints.values()] or [1.0]))), 1)
        src = xd.animations[slot] if slot < len(xd.animations) else None
        lst = []
        for k in mine:
            xm = (xd_matanim(src, k) if src is not None else None) or xd_matanim(xd.animations[idle_slot], k)
            if xm is None:
                lst.append(None)
                continue
            mm = MatAnim()
            for ta in xm.texanims:
                t = TexAnim()
                t.tex_id = ta.tex_id
                t.images = [image(i) for i in ta.images]
                t.tluts = [tlut(i) for i in ta.tluts]
                ao = AnimObj()
                ao.flags = ta.aobj.flags
                ao.end = float(frames)
                end = max(ta.aobj.end, 1.0)
                for tr in ta.aobj.tracks:
                    if tr.keys:
                        vals = [float(track_value(tr, f % end)) for f in range(frames + 1)]
                        ao.tracks.append(Track(tr.kind, keys_of(vals, 1e-4)))
                t.aobj = ao
                mm.texanims.append(t)
            lst.append(mm)
        if any(lst):
            an.materials[holder] = lst
            an.has_material_tree = True
    log('INFO: XD flame pieces copied onto the PBR body: %d mesh(es), %d vertices' % (
        len(mine), sum(len(xd.meshes[k].verts) for k in picked)))


NO_REFLECTION = {}      # (Ditto and Dialga were in here until the piece budget MAX_REFLECT_PIECES was found)

KEEP_XD = {
}

# PBR's flames are particle effects (node kind 7 attach points), XD's are see-through model pieces: XD's are copied
# onto the PBR body (graft_xd_pieces)
# XD flame bones by region: (PBR joint[, PBR joint]), (XD bone[, XD bone]) = the same place on both bodies (hip,
# shoulder, pelvis; for a span the neck's base and the head), then the XD flame bones that go with it. Read off
# XD's and PBR's idle poses (bone places and parent chains, 2026-11-12); +x = the Pokemon's left in both.
GRAFT_XD = {
    'ponyta': [(('jnt3_1',), (7,), [16, 17, 18]), (('jnt8_1',), (20,), [29, 30, 31]),
               (('jnt4_1',), (5,), [32, 33, 34, 35]),
               (('jnt10_1',), (39,), [47, 48]), (('jnt2_1',), (49,), [57, 58]),
               (('jnt4_2', 'head'), (37, 64), [77, 78, 79, 80, 81, 99, 100, 101])],
    'gallop': [(('jnt1_1',), (6,), [15, 16]), (('jnt9_1',), (18,), [27, 28]),
               (('jnt13_1',), (36,), [43, 44]), (('jnt14_1',), (46,), [53, 54]),
               (('jnt11_1',), (4,), [30, 93, 94, 95, 96]),
               (('jnt10_1', 'jnt7_1'), (2, 59), [70, 4, 31, 32, 33, 34, 98, 99, 100, 101, 102])],
    # Moltres: XD's flames run along each wing's edge (3 bones a side, PBR's wings stand as XD's) and the tail
    # (PBR has no tail bones: the hips)
    'fire': [(('WL1j',), (93,), [93]), (('WL2_J',), (94,), [94]), (('WL3_J',), (95,), [95]),
             (('WR1_J',), (110,), [110]), (('WR2_J',), (111,), [111]), (('WR3_J',), (112,), [112]),
             (('KOSIjnt',), (2,), [2, 99, 100, 101, 102, 103, 104, 108])],
}


class KeepXD(Exception):
    pass


def convert_file(sdr_path, xd_path, out, log=print, step=0.1, compensate='joint8_under_joint', cap=FILE_CAP, gen4=None):
    name = os.path.splitext(os.path.basename(out))[0]
    if name in KEEP_XD:
        raise KeepXD('XD\'s own model is kept: ' + KEEP_XD[name])
    sdr = sdr_read.read(open(sdr_path, 'rb').read())
    for w in sdr.warnings:
        log('NOTE: ' + w)
    if name in NO_REFLECTION:
        for pm in sdr.materials:
            if any(l['kind'] != 'uv' for l in pm.get('layers', [])):
                pm['layers'] = [l for l in pm['layers'] if l['kind'] == 'uv'] or pm['layers'][:1]
                raw = bytearray(pm['raw'])
                raw[0x76] = 0xFF
                pm['raw'] = bytes(raw)
        log('NOTE: reflection layers left out: ' + NO_REFLECTION[name])
    xd_pkx, xd_model, warnings = hsd_read.read_pkx(open(xd_path, 'rb').read(), meshes=False)
    xd_full = hsd_read.read_pkx(open(xd_path, 'rb').read())[1] if name in GRAFT_XD else None
    if xd_full is not None:
        xd_full.graft = GRAFT_XD[name]
    def build(tol_scale, quiet=False):
        # Skin weights in steps of `step`; a model cut into more than MAX_POBJ drawing pieces (one per 10 weight
        # combinations) is redone with coarser steps: Snorlax at 114 pieces never came back from the graphics
        # card, at 51 it draws; 77 pieces were seen working (2026-11-12).
        st = step
        while True:
            model, pkx = convert(sdr, xd_pkx, xd_model, st, (lambda s: None) if quiet else log, compensate, tol_scale, xd_full,
                                 gen4=gen4)
            archive, report = hsd_write.write_archive(model)
            if (report.get('pobj', 0) <= MAX_POBJ and report.get('reflect_pieces', 0) <= MAX_REFLECT_PIECES) or st >= 0.34:
                break
            log('INFO: %d drawing pieces (%d with a reflection layer): skin weights redone in steps of %g' % (
                report['pobj'], report.get('reflect_pieces', 0), round(st + 0.05, 2)))
            st = round(st + 0.05, 2)
            quiet = True
        fit_textures(model, model.image_classes, log)
        archive, report = hsd_write.write_archive(model)
        pkx.archive = archive
        data = pkx.build()
        if cap and len(data) > cap:
            tex = sum(len(model.images[k].data) for k in used_images(model))
            fit_textures(model, model.image_classes, log, max(tex - (len(data) - cap), 0))
            archive, report = hsd_write.write_archive(model)
            pkx.archive = archive
            data = pkx.build()
        return data, report

    data, report = build(1.0)
    # Still over: the animations' keys thinned with wider tolerances (rotations 0.0003 rad -> up to 0.003 rad,
    # 0.17 degree; moves and scales 0.001 -> 0.01) before the file is let through over the cap.
    for ts in (3.0, 10.0):
        if not cap or len(data) <= cap:
            break
        log('INFO: %d bytes: animation keys thinned (tolerance x%g)' % (len(data), ts))
        data, report = build(ts, quiet=True)
    if cap and len(data) > cap:
        log('NOTE: %d bytes after shrinking pictures and thinning keys: still over %d' % (len(data), cap))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    log('written %s: %d bytes (XD original %d, PBR file %d); %s' % (out, len(data), os.path.getsize(xd_path), os.path.getsize(sdr_path), report))
    return len(data)


def main(args):
    if len(args) < 3:
        print(__doc__)
        return 2
    step = 0.1
    for a in args[3:]:
        if a.startswith('--step='):
            step = float(a[7:])
    convert_file(args[0], args[1], args[2], step=step)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
