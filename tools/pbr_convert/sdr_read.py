"""Reader: a Battle Revolution model file ("sdr", FSYS member type 2, first bytes 01 00 00 04) into plain data.

Big-endian; every pointer is an offset from the start of the file. The layout is from our own survey
(docs/BATTLE_REVOLUTION.md, build notes) and every field used here was checked on the disc's files with
sdr_probe.py; what was found different from the notes is marked "(checked)".

  file      +08 list of skeletons, +0C list of textures, +14 list of materials; u16 counts +18 / +1A / +1E
  texture   u16 width, height; +08 u32 format (Genius Sonority's codes); +10 / +14 wrap S / T; +28 picture
            offset FROM THE TEXTURE RECORD; +4C picture bytes
  material  +00 name, +18 / +1C / +20 / +24 texture record of layer 0..3, +2C.. layer records, +40.. layer animation
            chains, +5A u8 layer count; +60 / +64 RGBA ambient, diffuse (checked 2026-11-12: Dusclops's face = skin,
            eye, glint layers). Layer record: +00 u8 1 = placed by coordinates (+01 = GX source 4 + the mesh's
            coordinate set), 0 (+01 1, +02 1) = reflection (by the surface's direction); +04 / +08 f32 offset u / v,
            +0C rotation, +10 / +14 scale u / v (survey: 128 of 493 models have materials with 2..4 layers)
  skeleton  +00 name, +06 u16 nodes, +08 u16 actions, +0C action table (0x30 each: name, three times in
            seconds: start of the hit window, the hit, the END = the action's length), +10 root node
  node      +00 kind (0 helper, 2 joint, 3 skin node with a mesh, 6 second coarse mesh), +04 name, +08 u16 number,
            +0A u16 flags, +0C / +10 / +14 pointers to translate / rotate (Euler, radians) / scale (absent = none),
            +18 pivots, +20 first animation block, +24 first child, +28 next sibling;
            joint: +30 flags, +34 joint orient (3 Euler angles), +44 inverse bind matrix (3x4);
            skin node: +30 mesh
  mesh      +02 u16 vertices, +04 u32 coordinate sets, +08 vertices (position, normal: 6 floats), +0C weight
            block, +14 -> per coordinate set (UV address, u16 count, u16 0), +18 first part; set k = GX attribute 13 + k
  part      +08 material, +0C u16 primitives, +10 attribute list (8 bytes each, 0xFF ends), +14 primitives
            (GX: command byte, u16 count, one u16 index per attribute), +18 their byte size, +1C next part
  weights   three lists; counts are u16 (checked): +00 n, +04 runs (u16 vertices, u16 node): fully on one node;
            +08 n, +0C runs (u16 vertices, u16 node A, u16 node B), +10 u16 amounts (one per vertex: A's share
            / 65535); +14 n, +18 extras of 10 bytes (u16 vertex, node A, node B or FFFF, u16 weight A, weight B):
            added on top, the weights the vertex had are scaled down by what the extras take.
            Lists 1 and 2 cover the vertices in order.
  animation node +20 -> chain of blocks, one per action: u16 action, u16 curves, +04 curve table, +08 f32 the
            action's length, +0C next block.  curve (0x10): +01 what (0 translate, 1 rotate, 2 scale,
            3 visibility), +02 axis 1..3, +06 number kind (0 float, 5 / 6 unsigned / signed byte, 7 / 8 unsigned /
            signed 16 bit), +07 exponent (whole numbers / 2^exponent), +08 key data.
            key data: +00 values, +04 slopes (floats), +08 u16 values, +10 key list, +14 u16 keys,
            +16 u16 frames per second of the one-value-per-frame form (60 on the files looked at) (checked).
            key (12 bytes): u16 interpolation (0 step, 1 line, 2 spline), u16 value index, u16 slope index
            arriving, u16 slope index leaving, f32 time in seconds. No key list = one value per frame.
"""
import struct

GS_TO_GX_FORMAT = {0x40: 0, 0x42: 1, 0x41: 2, 0x43: 3, 0x44: 4, 0x90: 5, 0x45: 6, 0x00: 8, 0x01: 9, 0x30: 10, 0xB0: 14}


class Node:
    pass


class Curve:
    def __init__(self):
        self.what = self.axis = 0
        self.values = []
        self.slopes = []
        self.keys = None         # [(interp, value index, slope in, slope out, time)] or None
        self.fps = 60

    def at(self, t):
        v = self.values
        if not v:
            return 0.0
        if self.keys is None:
            x = t * self.fps
            i = int(x)
            if i >= len(v) - 1:
                return v[-1]
            if i < 0:
                return v[0]
            f = x - i
            return v[i] + (v[i + 1] - v[i]) * f
        k = self.keys
        if t <= k[0][4]:
            return v[k[0][1]]
        for a, b in zip(k, k[1:]):
            if t < b[4]:
                dt = b[4] - a[4]
                p0, p1 = v[a[1]], v[b[1]]
                if b[0] == 0 or dt <= 0:
                    return p0
                s = (t - a[4]) / dt
                if b[0] == 1:
                    return p0 + (p1 - p0) * s
                m0 = self.slopes[a[3]] if a[3] < len(self.slopes) else 0.0
                m1 = self.slopes[b[2]] if b[2] < len(self.slopes) else 0.0
                s2, s3 = s * s, s * s * s
                return ((2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * dt * m0 + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * dt * m1)
        return v[k[-1][1]]


class Sdr:
    def __init__(self, b):
        self.b = b
        self.warnings = []
        if b[:4] != b'\x01\x00\x00\x04':
            raise ValueError('not a Battle Revolution model file')
        self.textures = self._textures()
        self.materials = self._materials()
        sk = self.u32(self.u32(8))
        if self.u16(0x18) != 1:
            self.warnings.append('%d skeletons in the file: only the first is read' % self.u16(0x18))
        self.name = self.cstr(self.u32(sk))
        self.actions = []
        for a in range(self.u16(sk + 8)):
            o = self.u32(sk + 0xC) + a * 0x30
            self.actions.append((self.cstr(self.u32(o)),) + struct.unpack_from('>3f', b, o + 4))
        self.nodes = []
        self._walk(self.u32(sk + 0x10), -1)
        self.by_number = {n.number: i for i, n in enumerate(self.nodes)}
        self._material_anims()

    def _material_anims(self):
        """Material animations (the eyes): material +0x40 -> 8 bytes (00 00 FF FF, 0) and then a chain of blocks
        like a node's (u16 action, u16 curves, +04 curve table, +08 f32 length, +0C next); curves as a node's,
        "what" 0x14 = picture slide (axis 1 sideways, 2 up/down) (checked: 447 of the 493 models; every block's
        length is its action's length; Bulbasaur's tx_wink slides 0, 1/6, 2/6, 1/6, 0 = XD's own, sign turned).
        A chain whose blocks don't match the actions is not taken (warning).
        Each layer has a chain of its own (+40 + 4 x layer; first bytes 00 <layer> FF FF): Dusclops's eye is layer 1.
        m['anims'] = {layer: blocks}; m['anim'] = the first animated layer's."""
        for i, m in enumerate(self.materials):
            m['anim'], m['anims'] = {}, {}
            mo = self.u32(self.u32(0x14) + 4 * i)
            for k in range(max(len(m['layers']), 1)):
                p = self.u32(mo + 0x40 + 4 * k)
                if not p or self.b[p] != 0 or self.b[p + 2:p + 4] != b'\xff\xff':
                    continue
                blocks = {}
                blk, ok = p + 8, True
                while blk and len(blocks) <= len(self.actions):
                    act, nc, ct, ln, nxt = struct.unpack_from('>HHIfI', self.b, blk)
                    if act >= len(self.actions) or abs(ln - self.actions[act][3]) > 0.02 or nc > 16:
                        ok = False
                        break
                    blocks[act] = [self._curve(ct + c * 0x10) for c in range(nc)]
                    blk = nxt
                if ok and blocks:
                    m['anims'][k] = blocks
                    if not m['anim']:
                        m['anim'] = blocks
                elif blocks:
                    self.warnings.append('material %d layer %d: animation blocks that do not fit the actions: not read' % (i, k))

    def u32(self, o):
        return struct.unpack_from('>I', self.b, o)[0]

    def u16(self, o):
        return struct.unpack_from('>H', self.b, o)[0]

    def cstr(self, o):
        return self.b[o:self.b.index(b'\0', o)].decode('latin1') if o else ''

    def vec(self, o, default):
        return struct.unpack_from('>3f', self.b, o) if o else default

    def _textures(self):
        out = []
        self.tex_at = {}
        for i in range(self.u16(0x1A)):
            t = self.u32(self.u32(0xC) + 4 * i)
            self.tex_at[t] = i
            code = self.u32(t + 8)
            size = self.u32(t + 0x4C)
            data = t + self.u32(t + 0x28)
            out.append({'width': self.u16(t), 'height': self.u16(t + 2), 'code': code, 'format': GS_TO_GX_FORMAT.get(code),
                        'wrap_s': self.u32(t + 0x10), 'wrap_t': self.u32(t + 0x14), 'data': self.b[data:data + size],
                        'palette_format': self.u32(t + 0xC), 'palette_at': self.u32(t + 0x48), 'levels': self.b[t + 5]})
        return out

    def _materials(self):
        out = []
        self.mat_at = {}
        for i in range(self.u16(0x1E)):
            m = self.u32(self.u32(0x14) + 4 * i)
            self.mat_at[m] = i
            tex = self.u32(m + 0x18)
            layers = []
            for k in range(min(self.b[m + 0x5A], 4)):
                r = self.u32(m + 0x2C + 4 * k)
                rec = self.b[r:r + 0x18] if r else bytes(0x18)
                ou, ov, rot, su, sv = struct.unpack_from('>5f', rec, 4) if r else (0.0, 0.0, 0.0, 1.0, 1.0)
                layers.append({'texture': self.tex_at.get(self.u32(m + 0x18 + 4 * k)),
                               'kind': 'uv' if rec[0] == 1 else 'reflect',
                               'uvset': max(rec[1] - 4, 0) if rec[0] == 1 else None,
                               'offset': (ou, ov), 'rotation': rot, 'scale': (su, sv), 'raw': rec})
            out.append({'name': self.cstr(self.u32(m)), 'texture': self.tex_at.get(tex), 'layers': layers,
                        'ambient': tuple(self.b[m + 0x60:m + 0x64]), 'diffuse': tuple(self.b[m + 0x64:m + 0x68]),
                        'raw': self.b[m:m + 0x80]})
        return out

    def _walk(self, o, parent):
        b = self.b
        while o:
            n = Node()
            idx = len(self.nodes)
            self.nodes.append(n)
            n.parent = parent
            n.kind, n.name, n.number, n.flags = self.u32(o), self.cstr(self.u32(o + 4)), self.u16(o + 8), self.u16(o + 0xA)
            n.pos = self.vec(self.u32(o + 0xC), (0.0, 0.0, 0.0))
            n.rot = self.vec(self.u32(o + 0x10), (0.0, 0.0, 0.0))
            n.scale = self.vec(self.u32(o + 0x14), (1.0, 1.0, 1.0))
            n.pivots = struct.unpack_from('>12f', b, self.u32(o + 0x18)) if self.u32(o + 0x18) else None
            n.jflags = 0
            n.orient = (0.0, 0.0, 0.0)
            n.inv_bind = None
            n.mesh = None
            if n.kind == 2:
                n.jflags = self.u32(o + 0x30)
                n.orient = struct.unpack_from('>3f', b, o + 0x34)
                n.inv_bind = list(struct.unpack_from('>12f', b, o + 0x44))
            elif n.kind == 3:
                n.mesh = self._mesh(self.u32(o + 0x30))
            n.anim = {}
            a = self.u32(o + 0x20)
            while a:
                n.anim[self.u16(a)] = [self._curve(self.u32(a + 4) + c * 0x10) for c in range(self.u16(a + 2))]
                a = self.u32(a + 0xC)
            self._walk(self.u32(o + 0x24), idx)
            o = self.u32(o + 0x28)

    def _curve(self, q):
        b = self.b
        c = Curve()
        c.what, c.axis = b[q + 1], b[q + 2]
        kind, exp = b[q + 6], b[q + 7]
        kd = self.u32(q + 8)
        vals, slopes, nv, keys, nk, fps = self.u32(kd), self.u32(kd + 4), self.u16(kd + 8), self.u32(kd + 0x10), self.u16(kd + 0x14), self.u16(kd + 0x16)
        fmt = {0: 'f', 5: 'B', 6: 'b', 7: 'H', 8: 'h'}.get(kind)
        if fmt is None:
            self.warnings.append('curve number kind %d not read (taken as constant 0)' % kind)
            return c
        raw = struct.unpack_from('>%d%s' % (nv, fmt), b, vals)
        c.values = list(raw) if kind == 0 else [x / float(1 << exp) for x in raw]
        if keys:
            c.keys = []
            top = 0
            for k in range(nk):
                i, v, si, so, t = struct.unpack_from('>4Hf', b, keys + k * 12)
                c.keys.append((i, v, si, so, t))
                top = max(top, si, so)
            if slopes:
                c.slopes = list(struct.unpack_from('>%df' % (top + 1), b, slopes))
        else:
            c.fps = (fps & 0xFF) or 30
        return c

    def _mesh(self, m):
        b = self.b
        me = {'verts': [], 'weights': None, 'uvs': [], 'uvsets': [], 'parts': []}
        nv = self.u16(m + 2)
        vp = self.u32(m + 8)
        # Vertex record size: 24 bytes (position, normal) or, when header byte 1 has 0x20, 48 (position, normal and
        # two more direction vectors: GX attribute 0x19, normal + binormal + tangent) (checked: the parts' attribute
        # lists give the position's stride, byte 5 of its entry: 0x18 or 0x30; 38 meshes on the disc have 0x20).
        stride = 48 if b[m + 1] & 0x20 else 24
        p0 = self.u32(m + 0x18)
        if p0:
            k = self.u32(p0 + 0x10)
            while b[k] != 0xFF:
                if b[k] == 9 and b[k + 5] not in (0, stride):
                    self.warnings.append('vertex stride %d in the attribute list, %d by the mesh flag: list taken' % (b[k + 5], stride))
                    stride = b[k + 5]
                k += 8
        for i in range(nv):
            f = struct.unpack_from('>6f', b, vp + i * stride)
            me['verts'].append((f[0:3], f[3:6]))
        uvl = self.u32(m + 0x14)
        if uvl:
            for k in range(max(min(self.u32(m + 4), 4), 1)):
                ua, un = self.u32(uvl + 8 * k), self.u16(uvl + 8 * k + 4)
                me['uvsets'].append([struct.unpack_from('>2f', b, ua + i * 8) for i in range(un)] if ua else [])
            me['uvs'] = me['uvsets'][0]
        w = self.u32(m + 0xC)
        if w:
            ws = []
            n1, p1 = self.u16(w), self.u32(w + 4)
            for i in range(n1):
                cnt, node = struct.unpack_from('>HH', b, p1 + i * 4)
                ws += [{node: 1.0} for _ in range(cnt)]
            n2, p2, pa = self.u16(w + 8), self.u32(w + 0xC), self.u32(w + 0x10)
            k = 0
            for i in range(n2):
                cnt, na, nb = struct.unpack_from('>HHH', b, p2 + i * 6)
                for _ in range(cnt):
                    a = self.u16(pa + k * 2) / 65535.0
                    k += 1
                    d = {na: a}
                    d[nb] = d.get(nb, 0.0) + 1.0 - a
                    ws.append(d)
            n3, p3 = self.u16(w + 0x14), self.u32(w + 0x18)
            for i in range(n3):
                v, na, nb, wa, wb = struct.unpack_from('>5H', b, p3 + i * 10)
                wa /= 65535.0
                wb = wb / 65535.0 if nb != 0xFFFF else 0.0
                if v >= len(ws):
                    self.warnings.append('weight extra for vertex %d beyond the lists' % v)
                    continue
                keep = 1.0 - wa - wb
                d = {j: x * keep for j, x in ws[v].items()}
                d[na] = d.get(na, 0.0) + wa
                if nb != 0xFFFF:
                    d[nb] = d.get(nb, 0.0) + wb
                ws[v] = d
            if len(ws) != nv:
                self.warnings.append('weight lists cover %d of %d vertices' % (len(ws), nv))
                ws += [{} for _ in range(nv - len(ws))]
            me['weights'] = ws
        p = self.u32(m + 0x18)
        while p:
            attrs = []
            k = self.u32(p + 0x10)
            while b[k] != 0xFF:
                attrs.append((b[k], b[k + 4]))
                k += 8
            size_of = {2: 1, 3: 2}
            if any(t not in size_of for a, t in attrs):
                raise ValueError('mesh part with direct vertex data: not handled')
            tris = []
            q, end = self.u32(p + 0x14), self.u32(p + 0x14) + self.u32(p + 0x18)
            for _ in range(self.u16(p + 0xC)):
                op = b[q]
                cnt = self.u16(q + 1)
                q += 3
                corners = []
                for _ in range(cnt):
                    c = {}
                    for a, t in attrs:
                        if t == 2:
                            c[a] = b[q]
                            q += 1
                        else:
                            c[a] = self.u16(q)
                            q += 2
                    nrm = c.get(10, c.get(0x19))   # 0x19 = normal + binormal + tangent: the normal is the first
                    corners.append((c.get(9), nrm, c.get(13), c.get(14), c.get(15)))
                prim = op & 0xF8
                if prim == 0x90:
                    for i in range(0, cnt - 2, 3):
                        tris.append((corners[i], corners[i + 1], corners[i + 2]))
                elif prim == 0x98:
                    for i in range(cnt - 2):
                        tris.append((corners[i], corners[i + 1], corners[i + 2]) if i % 2 == 0 else (corners[i + 1], corners[i], corners[i + 2]))
                elif prim == 0xA0:
                    for i in range(1, cnt - 1):
                        tris.append((corners[0], corners[i], corners[i + 1]))
                elif prim == 0x80:
                    for i in range(0, cnt - 3, 4):
                        tris.append((corners[i], corners[i + 1], corners[i + 2]))
                        tris.append((corners[i], corners[i + 2], corners[i + 3]))
                else:
                    raise ValueError('mesh primitive 0x%02X: not handled' % op)
            me['parts'].append({'material': self.mat_at.get(self.u32(p + 8)), 'tris': tris})
            p = self.u32(p + 0x1C)
        return me


def read(b):
    return Sdr(b)
