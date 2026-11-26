"""Fire / glow / special-effect parts of the Ultra Sun / Ultra Moon models (the "usum-fx" converter stage).
Kept apart from gf_to_xd.py: gf_to_xd calls the small hooks below when opts['fx'] is set."""


def is_fire_material(mt):
    return 'Fire' in mt['shader'] or 'Fire' in mt['name']


import copy
import numpy as np

from model import AnimObj



PTCL_BONE = {
    4: {48: 'TailA01'},
    5: {22: 'TailA01'},
    6: {54: 'TailA01', 56: 'TailA01'},
    126: {91: 'TailA01'},
}


def species_of(gm):
    nm = gm['bones'][0]['name']
    return int(nm[2:6]) if nm.startswith('pm') and nm[2:6].isdigit() else 0


def _norm(pts):
    pts = np.asarray(pts, float)
    h = float(pts[:, 1].max() - pts[:, 1].min()) or 1.0
    return pts / h


def add_particle_tracks(m, xd_model, xwm, jn, sk, gm, log, root_pos=None):
    """XD's tail flames, sparks, drips... are particle generators spawned by a track (kind 40) of the idle / move animations
    (the model itself has no flame mesh). The 3DS model has none, so the converter put none: copy those tracks onto the
    bone that stands where the XD joint stands (or the one the PTCL_BONE table names). -> number of tracks copied."""
    names = {sk.names[i]: jn[i] for i in range(len(sk.names))}
    nat = species_of(gm)
    ours = [(jn[i], np.array(m.world_static()[jn[i]][3::4][:3]) if False else None) for i in range(len(sk.names))]
    wm = m.world_static()
    ours = [(jn[i], np.array([wm[jn[i]][3], wm[jn[i]][7], wm[jn[i]][11]])) for i in range(len(sk.names))]
    xj_pos = {j: np.array([xwm[j][3], xwm[j][7], xwm[j][11]]) for j in range(len(xwm))}
    xs = _norm(list(xj_pos.values()))
    h_x = max(float(np.array(list(xj_pos.values()))[:, 1].max() - np.array(list(xj_pos.values()))[:, 1].min()), 1e-6)
    op = np.array([p for _, p in ours])
    h_o = max(float(op[:, 1].max() - op[:, 1].min()), 1e-6)
    cache = {}

    def target(xj):
        if xj in cache:
            return cache[xj]
        nm = PTCL_BONE.get(nat, {}).get(xj)
        if nm and nm in names:
            cache[xj] = names[nm]
        else:
            d = (((op - op[:, :].mean(axis=0) * 0) / h_o) - (xj_pos[xj] / h_x)[None]) ** 2
            cache[xj] = ours[int(d.sum(axis=1).argmin())][0]
        return cache[xj]
    n = 0
    for slot, xa in enumerate(xd_model.animations):
        if slot >= len(m.animations) or m.animations[slot].name == 'none':
            continue
        an = m.animations[slot]
        used = set()
        for xj, xo in xa.joints.items():
            for t in xo.tracks:
                if t.kind != 40:
                    continue
                tj = target(xj)
                if tj in used:
                    continue
                used.add(tj)
                ao = an.joints.get(tj)
                if ao is None:
                    ao = AnimObj()
                    ao.end = float(max([a.end for a in an.joints.values()] or [1.0]))
                    an.joints[tj] = ao
                ao.tracks.append(copy.deepcopy(t))
                n += 1
    if n:
        log('INFO: fx: %d particle-spawn tracks copied from XD (flame / spark generators)' % n)
    return n






T_TRAU, T_TRAV = 2, 3
FALLBACK_SPEED = (0.01, 0.05)


def find_fire_motion(mots):
    """The material-only motion that carries the flame slides (None when the species has none)."""
    return mots.get('_fx')


def fire_mesh_materials(gm):
    return {i for i, mt in enumerate(gm['materials']) if is_fire_material(mt)}


def _channel(fxmo, name, key):
    if not fxmo:
        return None
    for c in fxmo['mats']:
        if c['name'] == name and c['unit'] == 0 and c[key]:
            return c[key]
    return None


def add_fire_scroll(m, gm, fxmo, log, sign=-1.0):
    """Texture slides for the flame pieces in every animation slot. -> number of pieces animated."""
    import gf_read
    from pbr_to_xd import keys_of
    from model import MatAnim, TexAnim, Track
    fire = fire_mesh_materials(gm)
    if not fire:
        return 0
    place, cnt = [], {}
    for me in m.meshes:
        k = cnt.get(me.joint, 0)
        cnt[me.joint] = k + 1
        if me.material in fire:
            place.append((me.joint, k, me.material))
    if not place:
        return 0
    period = max(int(fxmo['frames']), 1) if fxmo else 40
    for slot, an in enumerate(m.animations):
        if an.name == 'none':
            continue
        frames = max(int(round(max([a.end for a in an.joints.values()] or [1.0]))), 1)
        for joint, k, mi in place:
            mt = gm['materials'][mi]
            co = [c for c in mt['coords'] if c['unit'] == 0]
            sx, sy = co[0]['scale'] if co else (1.0, 1.0)
            tracks = []
            for key, kind, scale, fb in (('tx', T_TRAU, sx, FALLBACK_SPEED[0]), ('ty', T_TRAV, sy, FALLBACK_SPEED[1])):
                ch = _channel(fxmo, mt['name'], key)
                if ch:
                    vals = [sign * scale * float(gf_read.herp(ch, f % period)) for f in range(frames + 1)]
                else:
                    vals = [sign * scale * fb * f for f in range(frames + 1)]
                tracks.append(Track(kind, keys_of(vals, 1e-4)))
            ao = AnimObj()
            ao.end = float(frames)
            ao.tracks = tracks
            ta = TexAnim()
            ta.tex_id = 0
            ta.aobj = ao
            ma = MatAnim()
            ma.texanims = [ta]
            lst = an.materials.setdefault(joint, [None] * cnt[joint])
            if len(lst) < cnt[joint]:
                lst.extend([None] * (cnt[joint] - len(lst)))
            lst[k] = ma
        an.has_material_tree = True
    log('INFO: fx: %d flame piece(s) slide their picture (%s)' % (len(place), 'the dump\'s flame motion' if fxmo else 'fallback speed'))
    return len(place)


def fire_alpha(rgba):
    """The tinted flame picture gets its see-through-ness from its own brightness: dark = nothing, bright = flame."""
    out = rgba.copy()
    lum = rgba[..., 0].astype(float)

    g = rgba[..., 1].astype(float)
    a = np.clip((g - 55.0) / 135.0, 0.0, 1.0)
    out[..., 3] = (np.sqrt(a) * 255.0).astype(np.uint8)
    return out


def xd_has_particles(xd_model):
    """True when XD's own model spawns particle generators from its animations (a kind-40 track anywhere)."""
    return any(t.kind == 40 for an in xd_model.animations for ao in an.joints.values() for t in ao.tracks)






def emission_map(mt, texs):
    """Name of the glow picture of a material (texture unit 1 or 2 named *Inc*), else None."""
    for c in mt['coords']:
        if c['unit'] in (1, 2) and 'Inc' in c['name'] and c['name'] in texs:
            return c['name']
    return None


def emission_mask(mt, texs):
    """Name of the glow MASK of a material: its texture-unit-2 "Nor" picture when that picture's alpha is not all opaque, else None.
    The Gen 7 shaders keep the glow's mask in the alpha of the normal map: the glow picture ("Inc") is a plain colour over the whole
    texture (Finneon: pink everywhere) and only where the mask is set does it glow (Finneon's spots, Dusknoir's bands). Added without
    the mask the whole body was washed over with the glow colour (Finneon purple-pink, Dusknoir tan)."""
    for c in mt['coords']:
        if c['unit'] == 2 and 'Nor' in c['name'] and c['name'] in texs:
            a = texs[c['name']][2]
            if int(a[..., 3].min()) < 250:
                return c['name']
    return None


def _wrap_coord(q, mode):
    """3DS texture wrap: 0 / 1 clamp, 2 repeat, 3 mirrored repeat"""
    if mode == 2:
        return q - np.floor(q)
    if mode == 3:
        m = q - 2.0 * np.floor(q / 2.0)
        return np.where(m > 1.0, 2.0 - m, m)
    return np.clip(q, 0.0, 1.0)


def register_to_colour(src, ck, c0, w, h):
    """A picture (h, w, 4) of another texture unit laid into the colour picture's coordinates: every unit has its own scale / offset / wrap
    (Dusknoir: the colour picture and its glow picture are tiled twice, mirrored; the normal map once). The colour picture's texel p is model
    coordinate p / scale0 + trans0, which the unit reads at scale_k * (that - trans_k). Same coordinates: the picture itself."""
    if tuple(ck['scale']) == tuple(c0['scale']) and tuple(ck['trans']) == tuple(c0['trans']):
        return src
    py = (np.arange(h, dtype=float) + 0.5) / float(h)
    px = (np.arange(w, dtype=float) + 0.5) / float(w)
    uy = py / c0['scale'][1] + c0['trans'][1]
    ux = px / c0['scale'][0] + c0['trans'][0]
    qy = _wrap_coord(ck['scale'][1] * (uy - ck['trans'][1]), ck['wrap'][1])
    qx = _wrap_coord(ck['scale'][0] * (ux - ck['trans'][0]), ck['wrap'][0])
    iy = np.minimum((qy * src.shape[0]).astype(int), src.shape[0] - 1)
    ix = np.minimum((qx * src.shape[1]).astype(int), src.shape[1] - 1)
    return src[iy[:, None], ix[None, :]]


def coord_of(mt, name):
    for c in mt['coords']:
        if c['name'] == name and c['unit'] != 0:
            return c
    return None


def bake_emission(rgba, inc, strength=1.0, raw=None, mask=None):
    w, h, irgba = inc
    if irgba.shape[0] != rgba.shape[0] or irgba.shape[1] != rgba.shape[1]:
        import gx_tex
        irgba = gx_tex.resize(irgba, rgba.shape[1], rgba.shape[0])
    mk = None
    if mask is not None:
        mk = mask[2]
        if mk.shape[0] != irgba.shape[0] or mk.shape[1] != irgba.shape[1]:
            import gx_tex
            mk = gx_tex.resize(mk, irgba.shape[1], irgba.shape[0])
        mk = mk[..., 3].astype(float) / 255.0
    if strength == 1.0:
        if mk is None:
            strength = 1.0 if float(irgba[..., :3].mean()) / 255.0 < 0.12 else 0.3
        else:
            strength = 1.0 if float((irgba[..., :3].astype(float) * mk[..., None]).mean()) / 255.0 < 0.12 else 0.3
    base = rgba[..., :3].astype(float)
    glow = irgba[..., :3].astype(float) * (irgba[..., 3:4].astype(float) / 255.0 if irgba[..., 3].min() < 250 else 1.0)
    if mk is not None:
        glow = glow * mk[..., None]
    if raw is not None:


        rw = raw
        if rw.shape[0] != irgba.shape[0] or rw.shape[1] != irgba.shape[1]:
            import gx_tex
            rw = gx_tex.resize(rw, irgba.shape[1], irgba.shape[0])
        d = np.abs(irgba[..., :3].astype(float) - rw[..., :3].astype(float)).max(axis=2)
        glow = glow * np.clip((d - 10.0) / 30.0, 0.0, 1.0)[..., None]
    out = rgba.copy()
    out[..., :3] = np.clip(base + glow * strength, 0, 255).astype(np.uint8)
    return out
