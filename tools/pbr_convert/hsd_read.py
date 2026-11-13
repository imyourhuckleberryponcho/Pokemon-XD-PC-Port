"""Reader: an XD battle model (PKX container + HSD archive) into the neutral model (model.py).

Layouts: the PKX header and sections as XD's loader reads them (decomp: wazaSequence/modelSequence.cpp,
PkxHeader / MotionSection); the HSD structures as XD's library reads them (decomp: GSAPI/baselib jobj, dobj,
pobj, mobj, tobj, aobj, fobj and include/hsd_ref). Checked on the disc's own files.
"""
import struct

from model import (Model, Joint, Mesh, Vertex, Material, Texture, Image, Tlut, Animation, AnimObj, Track, MatAnim,
                   TexAnim, mat_mul, mat_inv, mat_point, mat_vec, mat_srt, IDENT, OP_CON, OP_LIN, OP_SPL0, OP_SPL,
                   OP_SLP, OP_KEY)

JOBJ_SKELETON, JOBJ_SKELETON_ROOT, JOBJ_HIDDEN, JOBJ_PTCL, JOBJ_SPLINE = 1, 2, 0x10, 0x20, 0x4000


def align32(x):
    return (x + 0x1F) & ~0x1F


class Pkx:
    """The container around the archive: header (0x84 bytes), sections (0xD0 each), optional particle bank."""
    def __init__(self):
        self.header = b''
        self.sections = []       # bytes, 0xD0 each
        self.particles = b''
        self.archive = b''

    @staticmethod
    def parse(b):
        p = Pkx()
        size, _, psize, _, count = struct.unpack_from('>5I', b, 0)
        if struct.unpack_from('>H', b, 0x1A)[0] != 0xC or size == 0:
            raise ValueError('not an XD PKX (type word is not 0x000C or the archive size is 0)')
        p.header = b[:0x84]
        p.sections = [b[0x84 + i * 0xD0:0x84 + (i + 1) * 0xD0] for i in range(count)]
        at = align32(0x84 + count * 0xD0)
        if psize:
            p.particles = b[at:at + psize]
            at += align32(psize)
        p.archive = b[at:at + size]
        return p

    def build(self):
        h = bytearray(self.header)
        struct.pack_into('>I', h, 0, len(self.archive))
        struct.pack_into('>I', h, 4, 0)
        struct.pack_into('>I', h, 8, len(self.particles))
        struct.pack_into('>I', h, 0xC, 0)
        struct.pack_into('>I', h, 0x10, len(self.sections))
        out = bytearray(h)
        for s in self.sections:
            out += s
        out += bytes(align32(len(out)) - len(out))
        if self.particles:
            out += self.particles
            out += bytes(align32(len(out)) - len(out))
        out += self.archive
        out += bytes(align32(len(out)) - len(out))
        return bytes(out)

    # the fields a converter touches
    def head_joint(self):
        return struct.unpack_from('>H', self.header, 0x6E)[0]

    def set_head_joint(self, j):
        h = bytearray(self.header)
        struct.pack_into('>H', h, 0x6E, j)
        self.header = bytes(h)

    def section_joints(self, s):
        return list(struct.unpack_from('>16i', self.sections[s], 0x4C))

    def set_section_joints(self, s, joints):
        b = bytearray(self.sections[s])
        struct.pack_into('>16i', b, 0x4C, *joints)
        self.sections[s] = bytes(b)

    def section_motions(self, s):
        n = struct.unpack_from('>i', self.sections[s], 4)[0]
        return n, [struct.unpack_from('>ii', self.sections[s], 0x8C + m * 8) for m in range(8)]

    def set_section_motion(self, s, m, kind, index):
        b = bytearray(self.sections[s])
        struct.pack_into('>ii', b, 0x8C + m * 8, kind, index)
        self.sections[s] = bytes(b)


class Archive:
    def __init__(self, a):
        fs, ds, nr, npub, next_ = struct.unpack_from('>5I', a, 0)
        self.d = a[0x20:0x20 + ds]
        self.relocs = set(struct.unpack_from('>%dI' % nr, a, 0x20 + ds))
        o = 0x20 + ds + nr * 4
        strs = o + (npub + next_) * 8
        self.public = {}
        for i in range(npub):
            off, name = struct.unpack_from('>II', a, o + i * 8)
            self.public[a[strs + name:a.index(b'\0', strs + name)].decode('latin1')] = off


def read_fobj_keys(data, frac_value, frac_slope):
    """HSD track bytes -> [(op, value, slope, wait)] (fobj.cpp: parseOpCode, parsePackInfo, parseFloat, parseWait)."""
    def num(pos, frac):
        kind = frac & 0xE0
        den = float(1 << (frac & 0x1F))
        if kind == 0x00:
            return struct.unpack_from('<f', data, pos)[0], pos + 4
        if kind == 0x20:
            return struct.unpack_from('<h', data, pos)[0] / den, pos + 2
        if kind == 0x40:
            return struct.unpack_from('<H', data, pos)[0] / den, pos + 2
        if kind == 0x60:
            return struct.unpack_from('<b', data, pos)[0] / den, pos + 1
        if kind == 0x80:
            return data[pos] / den, pos + 1
        raise ValueError('unknown track number format 0x%02X' % frac)

    keys = []
    pos = 0
    n = len(data)
    while pos < n:
        d = data[pos]
        pos += 1
        op = d & 0xF
        count = ((d >> 4) & 7) + 1
        shift = 3
        while d & 0x80:
            d = data[pos]
            pos += 1
            count += (d & 0x7F) << shift
            shift += 7
        if op == 0:
            break                 # padding at the end of the data
        for _ in range(count):
            v = s = 0.0
            w = 0
            if op in (OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_KEY):
                v, pos = num(pos, frac_value)
            if op in (OP_SPL, OP_SLP):
                s, pos = num(pos, frac_slope)
            if op in (OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_KEY):
                if pos >= n:      # the last key has no wait (FObjLoadWait: end of data)
                    keys.append((op, v, s, 0))
                    return keys
                shift = 0
                while True:
                    d = data[pos]
                    pos += 1
                    w |= (d & 0x7F) << shift
                    shift += 7
                    if not d & 0x80:
                        break
            keys.append((op, v, s, w))
    return keys


class Reader:
    def __init__(self, archive_bytes):
        self.arc = Archive(archive_bytes)
        self.d = self.arc.d
        self.model = Model()
        self.joint_at = {}       # file offset -> joint index
        self.joint_off = []      # joint index -> file offset
        self.image_at = {}
        self.tlut_at = {}
        self.mat_at = {}
        self.dobj_count = {}     # joint index -> number of display objects
        self.warnings = []

    def u32(self, o):
        return struct.unpack_from('>I', self.d, o)[0]

    def u16(self, o):
        return struct.unpack_from('>H', self.d, o)[0]

    def f32(self, o):
        return struct.unpack_from('>f', self.d, o)[0]

    def cstr(self, o):
        return self.d[o:self.d.index(b'\0', o)].decode('latin1') if o else None

    # --- joints ---
    def read_joints(self, o, parent):
        d = self.d
        while o:
            j = Joint()
            idx = len(self.model.joints)
            self.model.joints.append(j)
            self.joint_at[o] = idx
            self.joint_off.append(o)
            j.name = self.cstr(self.u32(o))
            j.parent = parent
            j.flags = self.u32(o + 4)
            j.hidden = bool(j.flags & JOBJ_HIDDEN)
            r = struct.unpack_from('>9f', d, o + 0x14)
            j.rot, j.scale, j.pos = r[0:3], r[3:6], r[6:9]
            m = self.u32(o + 0x38)
            if m:
                j.inv_bind = list(struct.unpack_from('>12f', d, m))
            if self.u32(o + 0x3C):
                self.warnings.append('joint %d has a constraint (robj): not carried over' % idx)
            self.read_joints(self.u32(o + 8), idx)
            o = self.u32(o + 0xC)

    # --- materials ---
    def image(self, o):
        if o not in self.image_at:
            im = Image()
            data = self.u32(o)
            im.width, im.height, im.format, im.mipmap = self.u16(o + 4), self.u16(o + 6), self.u32(o + 8), self.u32(o + 12)
            im.min_lod, im.max_lod = self.f32(o + 16), self.f32(o + 20)
            im.data = self.d[data:data + image_bytes(im.width, im.height, im.format, im.mipmap, im.max_lod)]
            self.image_at[o] = len(self.model.images)
            self.model.images.append(im)
        return self.image_at[o]

    def tlut(self, o):
        if o not in self.tlut_at:
            t = Tlut()
            data = self.u32(o)
            t.format, t.name, t.count = self.u32(o + 4), self.u32(o + 8), self.u16(o + 12)
            t.data = self.d[data:data + t.count * 2]
            self.tlut_at[o] = len(self.model.tluts)
            self.model.tluts.append(t)
        return self.tlut_at[o]

    def material(self, o):
        if o in self.mat_at:
            return self.mat_at[o]
        d = self.d
        m = Material()
        m.name = self.cstr(self.u32(o)) or ''
        m.rendermode = self.u32(o + 4)
        mat = self.u32(o + 12)
        m.ambient, m.diffuse, m.specular = tuple(d[mat:mat + 4]), tuple(d[mat + 4:mat + 8]), tuple(d[mat + 8:mat + 12])
        m.alpha, m.shininess = self.f32(mat + 12), self.f32(mat + 16)
        if self.u32(o + 16):
            self.warnings.append('material has a render description: not carried over')
        pe = self.u32(o + 20)
        if pe:
            m.pe = d[pe:pe + 12]
        t = self.u32(o + 8)
        while t:
            x = Texture()
            x.tex_id, x.src = self.u32(t + 8), self.u32(t + 12)
            x.rot = struct.unpack_from('>3f', d, t + 0x10)
            x.scale = struct.unpack_from('>3f', d, t + 0x1C)
            x.trans = struct.unpack_from('>3f', d, t + 0x28)
            x.wrap_s, x.wrap_t, x.repeat_s, x.repeat_t = self.u32(t + 0x34), self.u32(t + 0x38), d[t + 0x3C], d[t + 0x3D]
            x.flags, x.blending, x.mag_filter = self.u32(t + 0x40), self.f32(t + 0x44), self.u32(t + 0x48)
            if self.u32(t + 0x4C):
                x.image = self.image(self.u32(t + 0x4C))
            if self.u32(t + 0x50):
                x.tlut = self.tlut(self.u32(t + 0x50))
            if self.u32(t + 0x54):
                x.lod = d[self.u32(t + 0x54):self.u32(t + 0x54) + 16]
            if self.u32(t + 0x58):
                x.tev = d[self.u32(t + 0x58):self.u32(t + 0x58) + 32]
            m.textures.append(x)
            t = self.u32(t + 4)
        self.mat_at[o] = len(self.model.materials)
        self.model.materials.append(m)
        return self.mat_at[o]

    # --- meshes ---
    def vertex_layout(self, o):
        lay = []
        while self.u32(o) != 0xFF:
            lay.append({'attr': self.u32(o), 'type': self.u32(o + 4), 'cnt': self.u32(o + 8), 'comp': self.u32(o + 12),
                        'frac': self.d[o + 16], 'stride': self.u16(o + 18), 'ptr': self.u32(o + 20)})
            o += 0x18
        lay.sort(key=lambda a: a['attr'])
        return lay

    def component(self, a, index, n):
        o = a['ptr'] + a['stride'] * index
        comp = a['comp']
        if comp == 4:
            return struct.unpack_from('>%df' % n, self.d, o)
        fmt = {0: 'B', 1: 'b', 2: 'H', 3: 'h'}[comp]
        den = float(1 << a['frac'])
        return tuple(v / den for v in struct.unpack_from('>%d%s' % (n, fmt), self.d, o))

    def read_meshes(self, jidx):
        o = self.joint_off[jidx]
        j = self.model.joints[jidx]
        if j.flags & (JOBJ_PTCL | JOBJ_SPLINE):
            if self.u32(o + 0x10):
                self.warnings.append('joint %d holds a particle / spline: not carried over' % jidx)
            return
        dob = self.u32(o + 0x10)
        n = 0
        while dob:
            n += 1
            me = Mesh()
            me.joint = jidx
            me.name = self.cstr(self.u32(dob)) or ''
            me.material = self.material(self.u32(dob + 8)) if self.u32(dob + 8) else None
            index = {}
            po = self.u32(dob + 12)
            first = True
            while po and self.with_meshes:
                self.read_pobj(po, me, index, first)
                first = False
                po = self.u32(po + 4)
            self.model.meshes.append(me)
            dob = self.u32(dob + 4)
        self.dobj_count[jidx] = n

    def skin_space(self, jidx):
        """(skeleton root joint, matrix from the mesh joint's space into the skeleton root's) for an envelope
        mesh on joint jidx; None for the matrix when the mesh joint is itself the skeleton root
        (displayfunc.cpp, _HSD_mkEnvelopeModelNodeMtx)."""
        m = self.model
        if m.joints[jidx].flags & JOBJ_SKELETON_ROOT:
            return jidx, None
        x = jidx
        while x >= 0 and not m.joints[x].flags & (JOBJ_SKELETON | JOBJ_SKELETON_ROOT):
            x = m.joints[x].parent
        if x < 0:
            raise ValueError('skinned mesh on joint %d has no skeleton above it' % jidx)
        world = self.world
        if x == jidx or not m.joints[x].flags & JOBJ_SKELETON_ROOT:
            # the two other cases of _HSD_mkEnvelopeModelNodeMtx (XD's Moltres, joint 109): the mesh joint is itself
            # a bound joint (its bind space = inverse of its inverse bind matrix), or the nearest one above it is a
            # bound joint, not a skeleton root (world x inverse bind of that joint, then the mesh joint). Positions
            # come out in the bind space, the one every inverse bind matrix starts from; no skeleton root is named.
            ib = m.joints[x].inv_bind
            if ib is None:
                raise ValueError('skinned mesh on joint %d: joint %d has no inverse bind matrix' % (jidx, x))
            if x == jidx:
                return None, mat_inv(ib)
            return None, mat_mul(mat_inv(mat_mul(world[x], ib)), world[jidx])
        return x, mat_mul(mat_inv(world[x]), world[jidx])

    def read_pobj(self, po, me, index, first):
        d = self.d
        flags, ndisp, disp, u = self.u16(po + 0xC), self.u16(po + 0xE), self.u32(po + 0x10), self.u32(po + 0x14)
        kind = (flags >> 12) & 3
        lay = self.vertex_layout(self.u32(po + 8))
        if first:
            me.pobj_flags = flags
        elif (flags & ~0x3000) != (me.pobj_flags & ~0x3000):
            self.warnings.append('polygon objects of one display object differ in flags (%04X / %04X)' % (flags, me.pobj_flags))
        envs = []
        conv = None
        if kind == 2:
            root, conv = self.skin_space(me.joint)
            if root is not None:
                if self.model.skeleton_root not in (None, root) and self.skin_root_set:
                    raise ValueError('two skeleton roots in one model: not handled')
                self.model.skeleton_root = root
                self.skin_root_set = True
            k = 0
            while self.u32(u + k * 4):
                e = self.u32(u + k * 4)
                ws = []
                while self.u32(e):
                    ws.append((self.joint_at[self.u32(e)], self.f32(e + 4)))
                    e += 8
                envs.append(tuple(ws))
                k += 1
        elif kind == 1:
            self.warnings.append('shape (morph) animation mesh: read as a plain mesh')
            me.rigid = True
        else:
            if u:
                raise ValueError('shared-vertex skin (two-joint polygon object): not handled')
            me.rigid = True
        # the display list
        p, end = disp, disp + ndisp * 32
        cache = {}
        while p < end:
            op = d[p]
            if op == 0:
                break
            cnt = self.u16(p + 1)
            p += 3
            prim = op & 0xF8
            idxs = []
            for _ in range(cnt):
                mtx = None
                pos = nrm = uv = col = None
                for a in lay:
                    t = a['type']
                    if t == 0:
                        continue
                    attr = a['attr']
                    if attr == 0 and t == 1:
                        mtx = d[p] // 3
                        p += 1
                        continue
                    if t == 2:
                        i = d[p]
                        p += 1
                    elif t == 3:
                        i = self.u16(p)
                        p += 2
                    else:
                        raise ValueError('direct vertex data for attribute %d: not handled' % attr)
                    if attr == 9:
                        pos = (a, i)
                    elif attr == 10:
                        nrm = (a, i)
                    elif attr == 11:
                        col = (a, i)
                    elif attr == 13:
                        uv = (a, i)
                    else:
                        self.note_once('vertex attribute %d ignored' % attr)
                key = (mtx, pos and pos[1], nrm and nrm[1], uv and uv[1], col and col[1])
                vi = cache.get(key)
                if vi is None:
                    vi = cache[key] = self.make_vertex(me, index, envs, conv, mtx, pos, nrm, uv, col)
                idxs.append(vi)
            if prim == 0x90:
                for i in range(0, cnt - 2, 3):
                    me.tris.append((idxs[i], idxs[i + 1], idxs[i + 2]))
            elif prim == 0x98:
                for i in range(cnt - 2):
                    t = (idxs[i], idxs[i + 1], idxs[i + 2]) if i % 2 == 0 else (idxs[i + 1], idxs[i], idxs[i + 2])
                    me.tris.append(t)
            elif prim == 0xA0:
                for i in range(1, cnt - 1):
                    me.tris.append((idxs[0], idxs[i], idxs[i + 1]))
            elif prim == 0x80:
                for i in range(0, cnt - 3, 4):
                    me.tris.append((idxs[i], idxs[i + 1], idxs[i + 2]))
                    me.tris.append((idxs[i], idxs[i + 2], idxs[i + 3]))
            else:
                raise ValueError('display list command 0x%02X: not handled' % op)

    def note_once(self, text):
        if text not in self.warnings:
            self.warnings.append(text)

    def make_vertex(self, me, index, envs, conv, mtx, pos, nrm, uv, col):
        p = self.component(pos[0], pos[1], 3)
        n = self.component(nrm[0], nrm[1], 3) if nrm else None
        t = self.component(uv[0], uv[1], 2) if uv else None
        c = None
        if col:
            a = col[0]
            o = a['ptr'] + a['stride'] * col[1]
            if a['comp'] == 5:       # RGBA8
                c = tuple(self.d[o:o + 4])
            elif a['comp'] == 1:     # RGB8
                c = tuple(self.d[o:o + 3]) + (255,)
            else:
                self.note_once('vertex colour format %d read as white' % a['comp'])
                c = (255, 255, 255, 255)
        w = None
        if not me.rigid:
            if mtx is None:
                mtx = 0
            w = envs[mtx]
            m = conv
            if conv is None:
                # the mesh sits on the skeleton root: a fully bound vertex is in its joint's own space
                if len(w) == 1 and w[0][1] >= 1.0 - 1.2e-7:
                    jb = self.model.joints[w[0][0]].inv_bind
                    m = mat_inv(jb) if jb else mat_mul(mat_inv(self.world[self.model.skeleton_root]), self.world[w[0][0]])
            if m is not None:
                p = mat_point(m, p)
                if n:
                    n = mat_vec(m, n)
        v = Vertex(tuple(p), tuple(n) if n else None, tuple(t) if t else None, c, w)
        k = v.key()
        i = index.get(k)
        if i is None:
            i = index[k] = len(me.verts)
            me.verts.append(v)
        return i

    # --- animations ---
    def aobj(self, o):
        if not o:
            return None
        a = AnimObj()
        a.flags, a.end = self.u32(o), self.f32(o + 4)
        if self.u32(o + 12):
            self.note_once('animation object refers to another object (obj_id): not carried over')
        f = self.u32(o + 8)
        while f:
            length, start = self.u32(f + 4), self.f32(f + 8)
            kind, fv, fs = self.d[f + 12], self.d[f + 13], self.d[f + 14]
            ad = self.u32(f + 16)
            t = Track(kind, read_fobj_keys(self.d[ad:ad + length], fv, fs), start)
            t.src_format = (fv, fs)
            t.src_bytes = self.d[ad:ad + length]
            a.tracks.append(t)
            f = self.u32(f)
        return a

    def read_animjoint(self, o, counter, anim):
        """An animation tree has the joint tree's shape: walking it in the same order numbers its nodes as the
        joints are numbered."""
        while o:
            jidx = counter[0]
            counter[0] += 1
            a = self.u32(o + 8)
            if a:
                anim.joints[jidx] = self.aobj(a)
            if self.u32(o + 12):
                self.note_once('constraint animation (robj): not carried over')
            anim.joint_flags[jidx] = self.u32(o + 16)
            self.read_animjoint(self.u32(o), counter, anim)
            o = self.u32(o + 4)

    def read_matanimjoint(self, o, counter, anim):
        while o:
            jidx = counter[0]
            counter[0] += 1
            ma = self.u32(o + 8)
            lst = []
            while ma:
                m = MatAnim()
                m.aobj = self.aobj(self.u32(ma + 4))
                if self.u32(ma + 12):
                    m.has_render_anim = True
                    self.note_once('render animation: not carried over')
                ta = self.u32(ma + 8)
                while ta:
                    t = TexAnim()
                    t.tex_id = self.u32(ta + 4)
                    t.aobj = self.aobj(self.u32(ta + 8))
                    it, tt, ni, nt = self.u32(ta + 12), self.u32(ta + 16), self.u16(ta + 20), self.u16(ta + 22)
                    t.images = [self.image(self.u32(it + 4 * i)) for i in range(ni)]
                    t.tluts = [self.tlut(self.u32(tt + 4 * i)) for i in range(nt)]
                    m.texanims.append(t)
                    ta = self.u32(ta)
                lst.append(m)
                ma = self.u32(ma)
            if lst:
                anim.materials[jidx] = lst
            self.read_matanimjoint(self.u32(o), counter, anim)
            o = self.u32(o + 4)

    def read(self, meshes=True):
        """meshes=False: joints, materials, animations and boxes only (the frame of an XD model that a
        converted model is set into); the skeleton root is then the first joint flagged as one."""
        d = self.d
        self.with_meshes = meshes
        scene = self.arc.public.get('scene_data')
        if scene is None:
            raise ValueError('archive has no scene_data')
        sets = self.u32(scene)
        ms = self.u32(sets)
        if self.u32(sets + 4):
            self.warnings.append('more than one model set: only the first is read')
        if self.u32(scene + 4) or self.u32(scene + 8) or self.u32(scene + 12):
            self.warnings.append('scene has camera / lights / fog: not carried over')
        root, aj, maj, saj = struct.unpack_from('>4I', d, ms)
        self.read_joints(root, -1)
        m = self.model
        self.next_sibling = {}
        last = {}
        for i, j in enumerate(m.joints):
            if j.parent in last:
                self.next_sibling[last[j.parent]] = i
            last[j.parent] = i
        self.world = m.world_static()
        m.skeleton_root = None
        self.skin_root_set = False
        for i in range(len(m.joints)):
            self.read_meshes(i)
        if m.skeleton_root is None and not meshes:
            roots = [i for i, j in enumerate(m.joints) if j.flags & JOBJ_SKELETON_ROOT]
            m.skeleton_root = roots[0] if roots else None
        if m.skeleton_root is None:
            m.skeleton_root = 0
        k = 0
        while aj and self.u32(aj + k * 4):
            an = Animation()
            c = [0]
            self.read_animjoint(self.u32(aj + k * 4), c, an)
            if c[0] != len(m.joints):
                self.warnings.append('animation %d has %d nodes for %d joints' % (k, c[0], len(m.joints)))
            m.animations.append(an)
            k += 1
        k = 0
        while maj and self.u32(maj + k * 4):
            if k >= len(m.animations):
                m.animations.append(Animation())
            m.animations[k].has_material_tree = True
            c = [0]
            self.read_matanimjoint(self.u32(maj + k * 4), c, m.animations[k])
            if c[0] != len(m.joints):
                self.warnings.append('material animation %d has %d nodes for %d joints' % (k, c[0], len(m.joints)))
            k += 1
        if saj and self.u32(saj):
            self.warnings.append('shape animations: not carried over')
        bb = self.arc.public.get('bound_box')
        if bb is not None:
            count = self.u16(bb)
            m.bound_head = []
            p = bb + 4
            for i in range(count):
                a, frames = self.u16(p), self.u16(p + 2)
                m.bound_head.append(a)
                boxes = [struct.unpack_from('>6f', d, p + 4 + f * 24) for f in range(frames)]
                if i < len(m.animations):
                    m.animations[i].bounds = boxes
                p += 4 + frames * 24
        return m


def image_bytes(w, h, fmt, mipmap=0, max_lod=0.0):
    """Byte size of a GX texture (all mip levels)."""
    def level(w, h):
        if fmt in (0, 8):                # I4, CI4
            return ((w + 7) // 8) * ((h + 7) // 8) * 32
        if fmt in (1, 2, 9):             # I8, IA4, CI8
            return ((w + 7) // 8) * ((h + 3) // 4) * 32
        if fmt in (3, 4, 5, 10):         # IA8, RGB565, RGB5A3, CI14X2
            return ((w + 3) // 4) * ((h + 3) // 4) * 32
        if fmt == 6:                     # RGBA8
            return ((w + 3) // 4) * ((h + 3) // 4) * 64
        if fmt == 14:                    # CMPR
            return ((w + 7) // 8) * ((h + 7) // 8) * 32
        raise ValueError('texture format %d' % fmt)
    n = level(w, h)
    if mipmap:
        levels = int(max_lod) + 1 if max_lod else 1
        for _ in range(1, levels):
            w, h = max(w // 2, 1), max(h // 2, 1)
            n += level(w, h)
    return n


def read_pkx(b, meshes=True):
    """bytes of an unpacked PKX -> (Pkx, Model, warnings)."""
    pkx = Pkx.parse(b)
    r = Reader(pkx.archive)
    model = r.read(meshes)
    return pkx, model, r.warnings
