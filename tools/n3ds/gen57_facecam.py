"""Where a Gen 5-7 species' face camera looks (docs/GEN57.md "Faces").

For one species: the point and the radius, in the converted model's own space (what the game's renderer sees:
XD_RENDER_ICON=pokemon:<model row>:face=x/y/z/r), of its HEAD. Found from the dump's skeleton and skin weights:

  - the bone called Head (or the nearest like it), its subtree minus the thin parts that stick out (ears, feelers, horns, hair, flames,
    leaves ...) = the head bones;
  - the vertices that are mostly weighted to those bones = the head, in the bind pose; their middle 90 % box gives the head's size;
  - the converted model (the pack's own file) gives where the bind pose and the idle's first frame put that box in the game's space.

A hand table (FRAMING below: model number -> a factor on the radius, a move of the centre in head sizes, a different bone) is for the
few heads the rule frames badly. Nothing of the game is kept in the repository.
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_convert'))
import gf_pose
import hsd_read
import model as hm


THIN = re.compile(r'ear|feeler|horn|hair|antenn|crest|flame|fire|leaf|petal|plume|tentacle|whisk|tail|wing|fin\b|spike|mane|tuft|ribbon|'
                  r'bud|hat|cap|hood|smoke|ember|beard|tusk|stem|vine|branch', re.I)





FRAMING = {n: dict(share=1.0) for n in (517, 518, 524, 535, 546, 550, 562, 568, 577, 602, 607, 608, 609, 611, 618, 626, 710, 712, 748, 761, 764, 773,
                                        779, 780, 788, 790, 805)}
FRAMING.update({n: dict(share=0.6) for n in (590, 591)})


VIEW_RULES = 1


def view_key(nat, pk_hash, wide=False):
    """What a face view of the table was made from: the converted model's hash, this species' hand framing, wide or not, the rule's number.
    A table entry is used only when the key is the same now (gen57_speed.py)."""
    import hashlib
    return hashlib.sha1(('%s|%r|%s|%d' % (pk_hash, sorted(FRAMING.get(nat, {}).items()), 'w' if wide else 'f', VIEW_RULES)).encode()).hexdigest()[:20]


def head_bone(sk, want=None):
    names = list(sk.names)
    if want:
        return names.index(want) if want in names else None
    for pat in (r'^head$', r'^head', r'head', r'^neck'):
        for i, n in enumerate(names):
            if re.search(pat, n, re.I) and not re.search(r'tail|headband', n, re.I):
                return i
    return None


def subtree(sk, root, thin=True):
    out = {root}
    for i in range(len(sk.names)):
        j, chain = i, []
        while j >= 0:
            chain.append(j)
            if j == root:
                break
            j = sk.parent[j]
        else:
            continue

        if thin and any(THIN.search(sk.names[c]) for c in chain[:-1]):
            continue
        out.add(i)
    return out


def head_points(gm, sk, bones):
    """Bind-pose positions of the vertices that are mostly weighted to `bones`."""
    pts = []
    for me in gm['meshes']:
        for sm in me['subs']:
            v = sm['verts']
            pos = np.asarray(v['pos'], float)
            tab = np.array(list(sm['bone_idx']) + [0] * 256)
            if 'bidx' not in v:
                b = int(tab[0])
                if b in bones:
                    pts.append(pos)
                continue
            bi, bw = v['bidx'].astype(int), v['bw']
            share = np.zeros(len(pos))
            for k in range(bi.shape[1]):
                w = bw[:, k] if k < bw.shape[1] else np.zeros(len(pos))
                share += np.where(np.isin(tab[bi[:, k]], list(bones)), w, 0.0)
            tot = np.zeros(len(pos))
            for k in range(min(bi.shape[1], bw.shape[1])):
                tot += bw[:, k]
            pts.append(pos[share >= 0.5 * np.maximum(tot, 1e-9)])
    return np.concatenate(pts) if pts else np.zeros((0, 3))


def dfs_joint_numbers(sk):
    """The converted model's joint number of every source bone (gf_to_xd.dfs_order: 2 + position)."""
    import gf_to_xd
    return {i: 2 + p for p, i in enumerate(gf_to_xd.dfs_order(sk))}


def idle_animation(m):
    for a in m.animations:
        if a.joints:
            return a
    return None


def face_view(gm, pkx_bytes, nat=0, frame=None, log=None, wide=False):
    """-> dict(x, y, z, r, yaw, pitch, how) or None (no head found: the caller falls back to the box icon)."""
    if frame is None:
        fv = os.environ.get('FACE_FRAME', '')
        frame = float(fv) if fv not in ('', 'bind') else (None if fv == 'bind' else 0.0)
    fr = FRAMING.get(nat, {})
    if wide:
        fr = dict(whole=1, scale=2.2)
    sk = gf_pose.Skeleton(gm['bones'])
    hb = head_bone(sk, fr.get('bone'))
    pts = np.zeros((0, 3))
    if hb is not None and not fr.get('whole'):
        bones = subtree(sk, hb)
        pts = head_points(gm, sk, bones)
        if len(pts) < 20:
            bones = subtree(sk, hb, thin=False)
            pts = head_points(gm, sk, bones)
    if len(pts) < 20:

        hb = list(sk.names).index('Waist') if 'Waist' in list(sk.names) else 1
        pts = head_points(gm, sk, set(range(len(sk.names))))
        if len(pts) < 20:
            return None
        top = 1.0 if fr.get('whole') else fr.get('top', 0.30)
        pts = pts[pts[:, 1] >= pts[:, 1].min() + (1.0 - top) * (pts[:, 1].max() - pts[:, 1].min())]
        if len(pts) < 20:
            return None
        how_top = True
    else:
        how_top = False
    lo, hi = np.percentile(pts, 5, axis=0), np.percentile(pts, 95, axis=0)
    ctr = (lo + hi) * 0.5
    half = max(hi[0] - lo[0], hi[1] - lo[1], 0.8 * (hi[2] - lo[2])) * 0.5

    _pkx, m, _w = hsd_read.read_pkx(pkx_bytes, meshes=False)
    wm = m.world_static()
    root = np.array(wm[1], float).reshape(3, 4) if len(wm[1]) == 12 else np.array(wm[1], float)[:3, :4]
    sf = float(np.linalg.norm(root[:, 0]))
    c_game = root[:, :3] @ ctr + root[:, 3]
    jh = dfs_joint_numbers(sk)[hb]
    anim = idle_animation(m)
    if anim is not None and frame is not None:
        pw = hm.pose_at(m, anim, frame)
        Pb, Pp = np.array(wm[jh], float).reshape(-1)[:12].reshape(3, 4), np.array(pw[jh], float).reshape(-1)[:12].reshape(3, 4)
        Pb4, Pp4 = np.vstack([Pb, [0, 0, 0, 1]]), np.vstack([Pp, [0, 0, 0, 1]])
        c_game = (Pp4 @ np.linalg.inv(Pb4) @ np.append(c_game, 1.0))[:3]
    r = half * sf * 1.45 * fr.get('scale', 1.0)
    c_game = c_game + np.array([fr.get('dx', 0.0), fr.get('dy', 0.0), fr.get('dz', 0.0)]) * r
    return dict(x=float(c_game[0]), y=float(c_game[1]), z=float(c_game[2]), r=float(r), yaw=fr.get('yaw', 18.0), pitch=fr.get('pitch', 6.0),
                how=('top part, %d vertices' % len(pts)) if how_top else 'bone %s, %d vertices' % (sk.names[hb], len(pts)))
