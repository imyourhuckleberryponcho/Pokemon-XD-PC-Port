"""Writer: the neutral model (model.py) into an HSD archive as XD's library loads it.

What XD needs was read off its loaders (decomp snapshot: GSAPI/baselib jobj, dobj, pobj, mobj, tobj, aobj, fobj,
displayfunc; GSAPI/GSmodel/GSmodel.cpp _modelLoad; GSAPI/GSbound) and checked against the disc's own models:
- public "scene_data" -> list of model sets -> (joint tree, list of joint animation trees, list of material
  animation trees, list of shape animation trees); public "bound_box" -> a box per frame of each animation;
- a skinned mesh is drawn with at most 10 matrices per polygon object (pobj.cpp SetupEnvelopeModelMtx): the
  writer cuts each mesh into pieces of at most 10 weight combinations and gives every drawn vertex a matrix
  index (GX_VA_PNMTXIDX, slot * 3);
- an envelope vertex is stored in the mesh joint's own space; the game goes from there into the skeleton root's
  space itself (displayfunc.cpp _HSD_mkEnvelopeModelNodeMtx), then through joint * inverse bind;
- animation tracks: fobj.cpp's byte stream. A track never gets fewer than two keys (a one-key track ends
  before anything is handed to the joint).
"""
import math
import struct

from model import (mat_mul, mat_inv, mat_point, mat_vec, pose_at, skin_positions, OP_CON, OP_LIN, OP_SPL0, OP_SPL,
                   OP_SLP, OP_KEY)

JOBJ_SKELETON, JOBJ_SKELETON_ROOT, JOBJ_ENVELOPE_MODEL, JOBJ_HIDDEN, JOBJ_LIGHTING = 1, 2, 4, 0x10, 0x80
JOBJ_OPA, JOBJ_XLU, JOBJ_TEXEDGE = 1 << 18, 1 << 19, 1 << 20
JOBJ_ROOT_OPA, JOBJ_ROOT_XLU, JOBJ_ROOT_TEXEDGE = 1 << 28, 1 << 29, 1 << 30
MAX_ENVELOPES = 10


class Blob:
    """A piece of the archive's data section with the pointers it holds."""
    def __init__(self, data=b'', align=4):
        self.b = bytearray(data)
        self.align = align
        self.ptrs = []           # (offset in this blob, target blob, offset in target)
        self.at = None

    def ptr(self, off, target, add=0):
        if target is not None:
            self.ptrs.append((off, target, add))


class Builder:
    def __init__(self):
        self.blobs = []

    def add(self, data=b'', align=4):
        b = Blob(data, align)
        self.blobs.append(b)
        return b

    def new(self, size, align=4):
        return self.add(bytes(size), align)

    def finish(self, publics):
        at = 0
        for b in self.blobs:
            at = (at + b.align - 1) & ~(b.align - 1)
            b.at = at
            at += len(b.b)
        size = (at + 3) & ~3
        data = bytearray(size)
        relocs = []
        for b in self.blobs:
            for off, target, add in b.ptrs:
                struct.pack_into('>I', b.b, off, target.at + add)
                relocs.append(b.at + off)
            data[b.at:b.at + len(b.b)] = b.b
        relocs.sort()
        strings = bytearray()
        pub = bytearray()
        for name, blob, add in publics:
            pub += struct.pack('>II', blob.at + add, len(strings))
            strings += name.encode('latin1') + b'\0'
        body = bytes(data) + struct.pack('>%dI' % len(relocs), *relocs) + bytes(pub) + bytes(strings)
        total = 0x20 + len(body)
        head = struct.pack('>5I', total, size, len(relocs), len(publics), 0) + bytes(12)
        return head + body


# ---- animation track bytes ----

def _pack_num(v, frac):
    kind = frac & 0xE0
    if kind == 0:
        return struct.pack('<f', v)
    q = int(round(v * (1 << (frac & 0x1F))))
    if kind == 0x20:
        return struct.pack('<h', max(-32768, min(32767, q)))
    if kind == 0x40:
        return struct.pack('<H', max(0, min(65535, q)))
    if kind == 0x60:
        return struct.pack('<b', max(-128, min(127, q)))
    return struct.pack('<B', max(0, min(255, q)))


def choose_format(values, tolerance):
    """The smallest number format that keeps every value within `tolerance`: signed 16-bit fixed point with as
    many fraction bits as fit, else floats."""
    if not values:
        return 0x20
    top = max(abs(v) for v in values)
    if top == 0:
        return 0x60              # signed bytes, all zero
    for frac in range(15, -1, -1):
        if top * (1 << frac) <= 32767:
            if 0.5 / (1 << frac) <= tolerance:
                return 0x20 | frac
            break
    return 0


def write_fobj_keys(keys, frac_value, frac_slope):
    out = bytearray()
    i = 0
    n = len(keys)
    while i < n:
        op = keys[i][0]
        j = i
        while j < n and keys[j][0] == op:
            j += 1
        count = j - i - 1
        d = op | ((count & 7) << 4)
        count >>= 3
        if count:
            d |= 0x80
        out.append(d)
        while count:
            d = count & 0x7F
            count >>= 7
            if count:
                d |= 0x80
            out.append(d)
        for k in range(i, j):
            _, v, s, w = keys[k]
            if op in (OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_KEY):
                out += _pack_num(v, frac_value)
            if op in (OP_SPL, OP_SLP):
                out += _pack_num(s, frac_slope)
            if op in (OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_KEY):
                if k == n - 1 and w == 0:
                    break        # the last key carries no wait
                while True:
                    d = w & 0x7F
                    w >>= 7
                    if w:
                        d |= 0x80
                    out.append(d)
                    if not w:
                        break
        i = j
    return bytes(out)


# ---- the model ----

def joint_flags(model):
    """HSD joint flags for a model that was not read from an HSD file (the pattern of XD's own Pokemon)."""
    n = len(model.joints)
    flags = [0] * n
    bound = set()
    for me in model.meshes:
        if not me.rigid:
            for v in me.verts:
                for j, w in v.weights:
                    bound.add(j)
    for i, j in enumerate(model.joints):
        if i == model.skeleton_root:
            flags[i] |= JOBJ_SKELETON_ROOT
        elif i in bound or j.inv_bind:
            flags[i] |= JOBJ_SKELETON
        if j.hidden:
            flags[i] |= JOBJ_HIDDEN
        flags[i] |= 0x8          # "classical" scaling: the game multiplies T * R * S down the tree, no compensation
        flags[i] |= getattr(j, 'billboard', 0)   # camera-facing kind (0xE00), set by the converter
    for me in model.meshes:
        f = JOBJ_OPA | JOBJ_LIGHTING
        if not me.rigid:
            f |= JOBJ_ENVELOPE_MODEL
        mode = getattr(model.materials[me.material], 'draw_class', 'opa') if me.material is not None else 'opa'
        if mode == 'xlu':
            f = (f & ~JOBJ_OPA) | JOBJ_XLU | JOBJ_TEXEDGE
        elif mode == 'texedge':
            f = (f & ~JOBJ_OPA) | JOBJ_TEXEDGE
        flags[me.joint] |= f
    for i in range(n - 1, -1, -1):
        up = 0
        if flags[i] & (JOBJ_OPA | JOBJ_ROOT_OPA):
            up |= JOBJ_ROOT_OPA
        if flags[i] & (JOBJ_XLU | JOBJ_ROOT_XLU):
            up |= JOBJ_ROOT_XLU
        if flags[i] & (JOBJ_TEXEDGE | JOBJ_ROOT_TEXEDGE):
            up |= JOBJ_ROOT_TEXEDGE
        p = model.joints[i].parent
        if p >= 0:
            flags[p] |= up
    return flags


def cut_into_pieces(me):
    """Triangles of a skinned mesh grouped so that no group uses more than 10 different weight combinations.
    Returns [(list of weight combinations, list of triangles)]."""
    left = list(me.tris)
    pieces = []
    while left:
        envs = []
        have = set()
        tris = []
        progress = True
        while progress and left:
            progress = False
            rest = []
            # first everything that needs no new matrix, then the triangle that needs the fewest new ones
            best = None
            best_new = None
            for t in left:
                need = set(me.verts[i].weights for i in t)
                new = need - have
                if not new:
                    tris.append(t)
                    progress = True
                    continue
                rest.append(t)
                if len(have) + len(new) <= MAX_ENVELOPES and (best is None or len(new) < len(best_new)):
                    best, best_new = t, new
            left = rest
            if best is not None:
                for e in sorted(best_new):
                    have.add(e)
                    envs.append(e)
                progress = True
        if not tris:
            raise ValueError('a triangle needs more than %d weight combinations' % MAX_ENVELOPES)
        pieces.append((envs, tris))
    return pieces


class Writer:
    def __init__(self, model):
        self.m = model
        self.b = Builder()
        self.b.new(32, 32)       # nothing of ours sits at offset 0 (a pointer of 0 reads as "none" in tools)
        self.report = {'pobj': 0, 'reflect_pieces': 0, 'most_envelopes': 0, 'display_list_bytes': 0, 'track_bytes': 0, 'texture_bytes': 0}

    def floats(self, values, align=4):
        return self.b.add(struct.pack('>%df' % len(values), *values), align)

    # --- textures and materials ---
    def image_blob(self, i):
        if i not in self.images:
            im = self.m.images[i]
            data = self.b.add(im.data, 32)
            self.report['texture_bytes'] += len(im.data)
            d = self.b.new(0x18)
            d.ptr(0, data)
            struct.pack_into('>HHIIff', d.b, 4, im.width, im.height, im.format, im.mipmap, im.min_lod, im.max_lod)
            self.images[i] = d
        return self.images[i]

    def tlut_blob(self, i):
        if i not in self.tluts:
            t = self.m.tluts[i]
            data = self.b.add(t.data, 32)
            d = self.b.new(0x10)
            d.ptr(0, data)
            struct.pack_into('>IIH', d.b, 4, t.format, t.name, t.count)
            self.tluts[i] = d
        return self.tluts[i]

    def material_blob(self, i):
        if i in self.materials:
            return self.materials[i]
        ma = self.m.materials[i]
        tobjs = []
        for t in ma.textures:
            o = self.b.new(0x5C)
            struct.pack_into('>II', o.b, 8, t.tex_id, t.src)
            struct.pack_into('>9f', o.b, 0x10, *(tuple(t.rot) + tuple(t.scale) + tuple(t.trans)))
            struct.pack_into('>IIBB', o.b, 0x34, t.wrap_s, t.wrap_t, t.repeat_s, t.repeat_t)
            struct.pack_into('>IfI', o.b, 0x40, t.flags, t.blending, t.mag_filter)
            if t.image is not None:
                o.ptr(0x4C, self.image_blob(t.image))
            if t.tlut is not None:
                o.ptr(0x50, self.tlut_blob(t.tlut))
            if t.lod:
                o.ptr(0x54, self.b.add(t.lod))
            if t.tev:
                o.ptr(0x58, self.b.add(t.tev))
            tobjs.append(o)
        for a, b in zip(tobjs, tobjs[1:]):
            a.ptr(4, b)
        col = self.b.add(bytes(ma.ambient) + bytes(ma.diffuse) + bytes(ma.specular) + struct.pack('>ff', ma.alpha, ma.shininess))
        o = self.b.new(0x18)
        struct.pack_into('>I', o.b, 4, ma.rendermode)
        if tobjs:
            o.ptr(8, tobjs[0])
        o.ptr(12, col)
        if ma.pe:
            o.ptr(20, self.b.add(ma.pe))
        self.materials[i] = o
        return o

    # --- meshes ---
    def mesh_blobs(self, me):
        """The polygon objects of one mesh (linked); returns the first."""
        m = self.m
        conv = None
        single_local = False
        if not me.rigid:
            if me.joint == m.skeleton_root:
                single_local = True
            else:
                c = mat_mul(mat_inv(self.world[m.skeleton_root]), self.world[me.joint])
                conv = mat_inv(c)
        has_nrm = any(v.nrm is not None for v in me.verts)
        has_uv = any(v.uv is not None for v in me.verts)
        has_col = any(v.color is not None for v in me.verts)
        # more coordinate sets (a material with several picture layers placed differently: GX TEX1, TEX2)
        n_uvx = max((len(v.uvx) for v in me.verts if v.uvx), default=0) if has_uv else 0
        pos_i, nrm_i, uv_i, col_i = {}, {}, {}, {}
        pos_l, nrm_l, uv_l, col_l = [], [], [], []
        uvx_i, uvx_l = [{} for _ in range(n_uvx)], [[] for _ in range(n_uvx)]

        def idx(table, lst, value):
            i = table.get(value)
            if i is None:
                i = table[value] = len(lst)
                lst.append(value)
            return i

        corner = []
        for v in me.verts:
            p, n = v.pos, v.nrm
            if not me.rigid:
                mt = conv
                if single_local and len(v.weights) == 1:
                    mt = m.joints[v.weights[0][0]].inv_bind
                if mt is not None:
                    p = mat_point(mt, p)
                    if n is not None:
                        n = mat_vec(mt, n)
                        l = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
                        n = (n[0] / l, n[1] / l, n[2] / l)
            p = struct.unpack('>3f', struct.pack('>3f', *p))
            c = [idx(pos_i, pos_l, p)]
            if has_nrm:
                n = struct.unpack('>3f', struct.pack('>3f', *(n or (0.0, 1.0, 0.0))))
                c.append(idx(nrm_i, nrm_l, n))
            if has_col:
                c.append(idx(col_i, col_l, tuple(v.color or (255, 255, 255, 255))))
            if has_uv:
                t = struct.unpack('>2f', struct.pack('>2f', *(v.uv or (0.0, 0.0))))
                c.append(idx(uv_i, uv_l, t))
                for k in range(n_uvx):
                    x = v.uvx[k] if v.uvx and k < len(v.uvx) and v.uvx[k] is not None else (v.uv or (0.0, 0.0))
                    c.append(idx(uvx_i[k], uvx_l[k], struct.unpack('>2f', struct.pack('>2f', *x))))
            corner.append(c)
        if max([len(pos_l), len(nrm_l), len(uv_l)] + [len(l) for l in uvx_l]) > 65535:
            raise ValueError('mesh too large for 16-bit indices')
        pos_b = self.floats([x for p in pos_l for x in p], 32)
        nrm_b = self.floats([x for p in nrm_l for x in p], 32) if has_nrm else None
        col_b = self.b.add(b''.join(bytes(c) for c in col_l), 32) if has_col else None
        uv_b = self.floats([x for p in uv_l for x in p], 32) if has_uv else None
        uvx_b = [self.floats([x for p in l for x in p], 32) for l in uvx_l]

        def layout(skinned):
            ents = []
            if skinned:
                ents.append((0, 1, 0, 4, 0, 0, None))
            ents.append((9, 3, 1, 4, 0, 12, pos_b))
            if has_nrm:
                ents.append((10, 3, 0, 4, 0, 12, nrm_b))
            if has_col:
                ents.append((11, 3, 1, 5, 0, 4, col_b))
            if has_uv:
                ents.append((13, 3, 1, 4, 0, 8, uv_b))
                for k in range(n_uvx):
                    ents.append((14 + k, 3, 1, 4, 0, 8, uvx_b[k]))
            o = self.b.new(0x18 * (len(ents) + 1))
            for k, (attr, typ, cnt, comp, frac, stride, blob) in enumerate(ents):
                struct.pack_into('>IIIIBBH', o.b, k * 0x18, attr, typ, cnt, comp, frac, 0, stride)
                o.ptr(k * 0x18 + 0x14, blob)
            struct.pack_into('>I', o.b, len(ents) * 0x18, 0xFF)
            return o

        if me.rigid:
            pieces = [(None, me.tris)]
        else:
            pieces = cut_into_pieces(me)
        lay = layout(not me.rigid)
        pobjs = []
        for envs, tris in pieces:
            slot = {e: k for k, e in enumerate(envs)} if envs is not None else None
            dl = bytearray()
            for start in range(0, len(tris), 21845):
                chunk = tris[start:start + 21845]
                dl += struct.pack('>BH', 0x90, len(chunk) * 3)
                for t in chunk:
                    for vi in t:
                        if slot is not None:
                            dl.append(slot[me.verts[vi].weights] * 3)
                        dl += struct.pack('>%dH' % len(corner[vi]), *corner[vi])
            dl += bytes((-len(dl)) % 32)
            self.report['display_list_bytes'] += len(dl)
            dlb = self.b.add(bytes(dl), 32)
            o = self.b.new(0x18)
            o.ptr(8, lay)
            flags = me.pobj_flags
            if flags is None:
                flags = 0x8000
            flags &= ~0x3000
            if not me.rigid:
                flags |= 0x2001
            struct.pack_into('>HH', o.b, 0xC, flags, len(dl) // 32)
            o.ptr(0x10, dlb)
            if envs is not None:
                self.report['most_envelopes'] = max(self.report['most_envelopes'], len(envs))
                tab = self.b.new(4 * (len(envs) + 1))
                for k, e in enumerate(envs):
                    eb = self.b.new(8 * (len(e) + 1))
                    for q, (j, w) in enumerate(e):
                        eb.ptr(q * 8, self.joint_blob[j])
                        struct.pack_into('>f', eb.b, q * 8 + 4, w)
                    tab.ptr(k * 4, eb)
                o.ptr(0x14, tab)
            pobjs.append(o)
            self.report['pobj'] += 1
            # pieces drawn with a reflection (sphere-map) layer: what the graphics layer fails on past about 50 (docs/GEN4.md 10.5)
            mt = self.m.materials[me.material] if me.material is not None else None
            if mt is not None and any((t.flags & 0xF) == 1 for t in mt.textures):
                self.report['reflect_pieces'] = self.report.get('reflect_pieces', 0) + 1
        for a, b in zip(pobjs, pobjs[1:]):
            a.ptr(4, b)
        return pobjs[0] if pobjs else None

    # --- animations ---
    def aobj_blob(self, a, tolerances):
        if a is None:
            return None
        o = self.b.new(0x10)
        struct.pack_into('>If', o.b, 0, a.flags, a.end)
        fobjs = []
        for t in a.tracks:
            keys = t.keys
            if not keys:
                continue
            if len([k for k in keys if k[0] != OP_SLP]) < 2:
                # never a one-key track
                k = [k for k in keys if k[0] != OP_SLP][0]
                keys = [(OP_CON, k[1], 0.0, int(max(a.end, 1))), (OP_CON, k[1], 0.0, 0)]
            fmt = getattr(t, 'src_format', None)
            if fmt is None:
                tol = tolerances(t.kind)
                fmt = (choose_format([k[1] for k in keys if k[0] != OP_SLP], tol),
                       choose_format([k[2] for k in keys if k[0] in (OP_SPL, OP_SLP)], tol))
            data = write_fobj_keys(keys, fmt[0], fmt[1])
            self.report['track_bytes'] += len(data)
            db = self.b.add(data + bytes((-len(data)) % 4))
            f = self.b.new(0x14)
            struct.pack_into('>IfBBB', f.b, 4, len(data), t.start, t.kind, fmt[0], fmt[1])
            f.ptr(0x10, db)
            fobjs.append(f)
        for x, y in zip(fobjs, fobjs[1:]):
            x.ptr(0, y)
        if fobjs:
            o.ptr(8, fobjs[0])
        return o

    def tree(self, make):
        """Blobs for a tree with the joint tree's shape; make(joint index) -> blob whose first two words are
        child and next. Returns the root's blob."""
        m = self.m
        nodes = [make(i) for i in range(len(m.joints))]
        last_child = {}
        for i, j in enumerate(m.joints):
            if j.parent >= 0:
                if j.parent in last_child:
                    nodes[last_child[j.parent]].ptr(self.next_off, nodes[i])
                else:
                    nodes[j.parent].ptr(self.child_off, nodes[i])
                last_child[j.parent] = i
        roots = [i for i, j in enumerate(m.joints) if j.parent < 0]
        for a, b in zip(roots, roots[1:]):
            nodes[a].ptr(self.next_off, nodes[b])
        return nodes

    def write(self):
        m = self.m
        self.images, self.tluts, self.materials = {}, {}, {}
        self.world = m.world_static()
        flags = [j.flags for j in m.joints]
        if any(f is None for f in flags):
            made = joint_flags(m)
            flags = [made[i] if f is None else f for i, f in enumerate(flags)]
        # joints
        self.child_off, self.next_off = 8, 12
        self.joint_blob = None
        blobs = [self.b.new(0x40) for _ in m.joints]
        self.joint_blob = blobs
        self.tree(lambda i: blobs[i])
        by_joint = {}
        for me in m.meshes:
            by_joint.setdefault(me.joint, []).append(me)
        for i, j in enumerate(m.joints):
            o = blobs[i]
            struct.pack_into('>I', o.b, 4, flags[i])
            struct.pack_into('>9f', o.b, 0x14, *(tuple(j.rot) + tuple(j.scale) + tuple(j.pos)))
            if j.inv_bind:
                o.ptr(0x38, self.floats(j.inv_bind))
            dobjs = []
            for me in by_joint.get(i, []):
                d = self.b.new(0x10)
                if me.material is not None:
                    d.ptr(8, self.material_blob(me.material))
                d.ptr(12, self.mesh_blobs(me))
                dobjs.append(d)
            for a, b in zip(dobjs, dobjs[1:]):
                a.ptr(4, b)
            if dobjs:
                o.ptr(0x10, dobjs[0])
        # animations
        def tol(kind):
            return 0.0005 if 1 <= kind <= 3 else 0.002
        anim_roots, mat_roots = [], []
        self.child_off, self.next_off = 0, 4
        for an in m.animations:
            def make_aj(i, an=an):
                o = self.b.new(0x14)
                o.ptr(8, self.aobj_blob(an.joints.get(i), tol))
                struct.pack_into('>I', o.b, 16, an.joint_flags.get(i, 0))
                return o
            anim_roots.append(self.tree(make_aj)[0])

            def make_mj(i, an=an):
                o = self.b.new(0x0C)
                lst = an.materials.get(i)
                n = len(by_joint.get(i, []))
                if lst is None and n:
                    lst = [None] * n
                mats = []
                for ma in lst or []:
                    mb = self.b.new(0x10)
                    if ma is not None:
                        mb.ptr(4, self.aobj_blob(ma.aobj, lambda k: 0.002))
                        tas = []
                        for ta in ma.texanims:
                            tb = self.b.new(0x18)
                            struct.pack_into('>I', tb.b, 4, ta.tex_id)
                            tb.ptr(8, self.aobj_blob(ta.aobj, lambda k: 0.0005))
                            if ta.images:
                                it = self.b.new(4 * len(ta.images))
                                for q, im in enumerate(ta.images):
                                    it.ptr(q * 4, self.image_blob(im))
                                tb.ptr(12, it)
                            if ta.tluts:
                                tt = self.b.new(4 * len(ta.tluts))
                                for q, tl in enumerate(ta.tluts):
                                    tt.ptr(q * 4, self.tlut_blob(tl))
                                tb.ptr(16, tt)
                            struct.pack_into('>HH', tb.b, 20, len(ta.images), len(ta.tluts))
                            tas.append(tb)
                        for x, y in zip(tas, tas[1:]):
                            x.ptr(0, y)
                        if tas:
                            mb.ptr(8, tas[0])
                    mats.append(mb)
                for x, y in zip(mats, mats[1:]):
                    x.ptr(0, y)
                if mats:
                    o.ptr(8, mats[0])
                return o
            mat_roots.append(self.tree(make_mj)[0])

        def ptr_list(items):
            o = self.b.new(4 * (len(items) + 1))
            for k, it in enumerate(items):
                o.ptr(k * 4, it)
            return o

        ms = self.b.new(0x10)
        ms.ptr(0, blobs[0])
        if anim_roots:
            ms.ptr(4, ptr_list(anim_roots))
            ms.ptr(8, ptr_list(mat_roots))
        sets = ptr_list([ms])
        scene = self.b.new(0x10)
        scene.ptr(0, sets)
        publics = [('scene_data', scene, 0)]
        # bound boxes
        if m.animations and all(a.bounds for a in m.animations):
            bb = bytearray(struct.pack('>HH', len(m.animations), 0))
            for k, a in enumerate(m.animations):
                head = m.bound_head[k] if m.bound_head and k < len(m.bound_head) else 0
                bb += struct.pack('>HH', head, len(a.bounds))
                for box in a.bounds:
                    bb += struct.pack('>6f', *box)
            publics.append(('bound_box', self.b.add(bytes(bb)), 0))
        return self.b.finish(publics)


def make_bounds(model):
    """A box around the drawn model for every frame of every animation (what "bound_box" holds), in the space
    of the model's root."""
    for an in model.animations:
        frames = int(round(an.end())) + 1
        boxes = []
        for f in range(frames):
            pts = [p for mesh in skin_positions(model, pose_at(model, an, float(f))) for p in mesh]
            if not pts:
                boxes.append((0.0,) * 6)
                continue
            boxes.append((min(p[0] for p in pts), min(p[1] for p in pts), min(p[2] for p in pts),
                          max(p[0] for p in pts), max(p[1] for p in pts), max(p[2] for p in pts)))
        an.bounds = boxes


def write_archive(model):
    """model -> (archive bytes, report dict)."""
    w = Writer(model)
    data = w.write()
    w.report['archive_bytes'] = len(data)
    return data, w.report
