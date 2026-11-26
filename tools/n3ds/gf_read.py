"""Readers for the Gen 6 and 7 3DS model / texture / motion files (GFL2 family).
Written from reading the format's open documentation (SPICA by gdkchan: used as documentation only, no code copied)
and checked against the player's own files. Pure Python + numpy. Read only; nothing here writes game data anywhere."""
import struct
import numpy as np


def _cstr(b, off, n):
    s = b[off:off + n]
    z = s.find(b'\0')
    return (s if z < 0 else s[:z]).decode('ascii', 'replace')


class R:
    def __init__(self, b, p=0):
        self.b, self.p = b, p

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.b, self.p)[0]
        self.p += 4
        return v

    def fl(self, n):
        v = struct.unpack_from('<%df' % n, self.b, self.p)
        self.p += 4 * n
        return v

    def pstr(self, n):
        s = _cstr(self.b, self.p, n)
        self.p += n
        return s

    def bstr(self):
        n = self.u8()
        s = self.b[self.p:self.p + n].decode('ascii', 'replace')
        self.p += n
        return s

    def istr(self):
        n = self.u32()
        s = _cstr(self.b, self.p, n)
        self.p += n
        return s

    def pad16(self):
        if self.p & 15:
            self.p += 16 - (self.p & 15)

    def section(self):
        mg = self.pstr(8)
        ln = self.u32()
        self.u32()
        return mg, ln


def float24(v):
    if v & 0x7fffff:
        m = v & 0xffff
        e = ((v >> 16) & 0x7f) + 64
        s = (v >> 23) & 1
        f = (m << 7) | (e << 23) | (s << 31)
    else:
        f = (v & 0x800000) << 8
    return struct.unpack('<f', struct.pack('<I', f))[0]


def vec_f24(w0, w1, w2):
    return (float24(w2 & 0xffffff), float24((w2 >> 24) | ((w1 & 0xffff) << 8)),
            float24((w1 >> 16) | ((w0 & 0xff) << 16)), float24(w0 >> 8))


def pica_cmds(words):
    """List of (register, [params], mask) from a PICA command word list."""
    i, out = 0, []
    n = len(words)
    while i + 1 < n:
        param = words[i]
        cmd = words[i + 1]
        i += 2
        rid = cmd & 0xffff
        mask = (cmd >> 16) & 15
        extra = (cmd >> 20) & 0x7ff
        cons = cmd >> 31
        if cons:
            for k in range(extra + 1):
                out.append((rid, [param], mask))
                rid += 1
                if k < extra:
                    param = words[i]
                    i += 1
        else:
            ps = [param] + list(words[i:i + extra])
            i += extra
            out.append((rid, ps, mask))
        if i & 1:
            i += 1
    return out


SCALES = [1 / 127.0, 1 / 255.0, 1 / 32767.0, 1.0]
AN = {0: 'pos', 1: 'nrm', 2: 'tan', 3: 'col', 4: 'uv0', 5: 'uv1', 6: 'uv2', 7: 'bidx', 8: 'bw'}


def _read_commands(r):
    lst = []
    while True:
        ln = r.u32()
        idx = r.u32()
        cnt = r.u32()
        r.u32()
        lst.append(struct.unpack_from('<%dI' % (ln >> 2), r.b, r.p))
        r.p += ln
        if idx >= cnt - 1:
            break
    return lst


def parse_model(b):
    """-> dict(bones, materials, meshes, tex_names). Positions in the model's bind pose; skin weights by bone index."""
    r = R(b)
    r.u32()
    r.u32()
    r.pad16()
    _mg, _ln = r.section()
    mend = r.p + _ln
    tables = []
    for _ in range(4):
        n = r.u32()
        t = []
        for _ in range(n):
            r.u32()
            t.append(r.pstr(0x40))
        tables.append(t)
    shader_names, tex_names, mat_names, mesh_names = tables
    bbmin = r.fl(4)
    bbmax = r.fl(4)
    r.fl(16)
    ul = r.u32()
    uo = r.u32()
    r.u32()
    r.u32()
    r.p += uo + ul
    nb = r.i32()
    r.p += 0xc
    bones = []
    for _ in range(nb):
        nm = r.bstr()
        par = r.bstr()
        fl = r.u8()
        sc = r.fl(3)
        rot = r.fl(3)
        tr = r.fl(3)
        bones.append(dict(name=nm, parent=par, flags=fl, scale=sc, rot=rot, pos=tr))
    r.pad16()
    nl = r.i32()
    ll = r.i32()
    r.pad16()
    r.p = mend
    mats = []
    for _ in range(len(mat_names)):
        mg, ln = r.section()
        start = r.p
        m = {}
        r.u32()
        m['name'] = r.bstr()
        r.u32()
        m['shader'] = r.bstr()
        r.u32()
        m['vtx_shader'] = r.bstr()
        r.u32()
        m['frag_shader'] = r.bstr()
        r.p += 16
        r.p += 8
        cols = [tuple(r.b[r.p + 4 * k:r.p + 4 * k + 4]) for k in range(12)]
        r.p += 48
        m['colors'] = dict(zip(('c0', 'c1', 'c2', 'c3', 'c4', 'c5', 'spec0', 'spec1', 'blend', 'emission', 'ambient', 'diffuse'), cols))
        r.p += 16
        r.p += 16
        r.p += 8
        r.p += 36
        r.p += 4
        r.p += 16
        nu = r.u32()
        coords = []
        for _ in range(nu):
            r.u32()
            nm = r.bstr()
            unit = r.u8()
            mapping = r.u8()
            sc = r.fl(2)
            rot = r.f32()
            tr = r.fl(2)
            wu, wv, mag, mn, mlod = r.u32(), r.u32(), r.u32(), r.u32(), r.u32()
            coords.append(dict(name=nm, unit=unit, mapping=mapping, scale=sc, rot=rot, trans=tr, wrap=(wu, wv)))
        m['coords'] = coords

        words = struct.unpack_from('<%dI' % (ln // 4), r.b, start)
        m['blend'] = m['depth'] = None
        for k in range(101, len(words) - 1):
            hdr = words[k + 1]
            if ((hdr >> 16) & 0xf) == 0xf and ((hdr >> 20) & 0x7ff) == 0:
                if (hdr & 0xffff) == 0x101:
                    m['blend'] = words[k]
                elif (hdr & 0xffff) == 0x107:
                    m['depth'] = words[k]
        r.p = start + ln
        mats.append(m)
    meshes = []
    for _ in range(len(mesh_names)):
        mg, ln = r.section()
        start = r.p
        r.u32()
        name = r.pstr(0x40)
        r.u32()
        r.fl(4)
        r.fl(4)
        nsub = r.u32()
        r.i32()
        r.p += 0x10
        cmds = _read_commands(r)
        subs, sizes = [], []
        for _ in range(nsub):
            r.u32()
            nm = r.istr()
            nbi = r.u8()
            bi = list(r.b[r.p:r.p + 0x1f])
            r.p += 0x1f
            vc, ic, vl, il = r.i32(), r.i32(), r.i32(), r.i32()
            subs.append(dict(name=nm, bone_idx=bi[:nbi]))
            sizes.append((vc, ic, vl, il))
        for si, sm in enumerate(subs):
            en, idx = cmds[si * 3], cmds[si * 3 + 2]
            fmt = perm = battr = 0
            stride = 0
            ntot = 0
            fixed = {}
            fidx = 0
            for rg, ps, mk in pica_cmds(en):
                p = ps[0]
                if rg == 0x201:
                    fmt |= p
                elif rg == 0x202:
                    fmt |= p << 32
                elif rg == 0x204:
                    battr |= p
                elif rg == 0x205:
                    battr |= (p & 0xffff) << 32
                    stride = (p >> 16) & 0xff
                elif rg == 0x232:
                    fidx = p
                elif rg == 0x233:
                    fixed.setdefault(fidx, [0, 0, 0])[0] = p
                elif rg == 0x234:
                    fixed.setdefault(fidx, [0, 0, 0])[1] = p
                elif rg == 0x235:
                    fixed.setdefault(fidx, [0, 0, 0])[2] = p
                elif rg == 0x242:
                    ntot = p + 1
                elif rg == 0x2bb:
                    perm |= p
                elif rg == 0x2bc:
                    perm |= p << 32
            attrs, fixa = [], []
            for k in range(ntot):
                if (fmt >> (48 + k)) & 1:
                    nm = AN.get((perm >> (k * 4)) & 15, 'x')
                    v = vec_f24(*fixed.get(k, [0, 0, 0]))
                    if nm in ('col', 'bw'):
                        v = tuple(x / 255.0 for x in v)
                    fixa.append((nm, v))
                else:
                    pi = (battr >> (k * 4)) & 15
                    nm = AN.get((perm >> (pi * 4)) & 15, 'x')
                    f = (fmt >> (pi * 4)) & 15
                    attrs.append(dict(name=nm, fmt=f & 3, n=(f >> 2) + 1, scale=1.0 if nm == 'bidx' else SCALES[f & 3]))
            bufaddr = cnt = prim = 0
            for rg, ps, mk in pica_cmds(idx):
                if rg == 0x227:
                    bufaddr = ps[0]
                elif rg == 0x228:
                    cnt = ps[0]
                elif rg == 0x25e:
                    prim = ps[0] >> 8
            vc, ic, vl, il = sizes[si]
            sm['raw'] = b[r.p:r.p + vl]
            r.p += vl
            if bufaddr >> 31:
                sm['indices'] = np.frombuffer(b, '<u2', cnt, r.p).astype(np.int64)
            else:
                sm['indices'] = np.frombuffer(b, 'u1', cnt, r.p).astype(np.int64)
            r.p += il
            sm['attrs'], sm['fixed'], sm['stride'], sm['prim'] = attrs, fixa, stride, prim
            sm['verts'] = decode_verts(sm)
        meshes.append(dict(name=name, subs=subs))
        r.p = start + ln
    return dict(bones=bones, materials=mats, meshes=meshes, tex_names=tex_names, bbox=(bbmin, bbmax))


def decode_verts(sm):
    """-> dict of numpy arrays: pos (n,3), nrm, uv0, col, bidx (n,k), bw (n,k)."""
    stride = sm['stride']
    raw = sm['raw']
    n = len(raw) // stride if stride else 0
    out = {}
    off = 0
    dt = {0: ('i1', 1), 1: ('u1', 1), 2: ('<i2', 2), 3: ('<f4', 4)}
    cols = {}
    for a in sm['attrs']:
        t, sz = dt[a['fmt']]
        if off % sz:
            off += sz - off % sz
        arr = np.empty((n, a['n']), dtype=np.float64)
        for e in range(a['n']):
            v = np.ndarray((n,), dtype=t, buffer=raw, offset=off + e * sz, strides=(stride,))
            arr[:, e] = v.astype(np.float64)
        off += a['n'] * sz
        arr *= a['scale']
        if a['name'] in ('bidx', 'bw'):
            cols.setdefault(a['name'], []).append(arr)
        else:
            out[a['name']] = arr
    for k, v in cols.items():
        out[k] = np.concatenate(v, axis=1)
    for nm, v in sm['fixed']:
        if nm in ('bidx', 'bw') and nm not in out:
            out[nm] = np.tile(np.array(v[:3]), (n, 1))
        elif nm not in out:
            out['fixed_' + nm] = np.array(v)
    out['n'] = n
    return out


def triangles(sm):
    ix = sm['indices']
    p = sm['prim']
    if p == 0:
        return ix[:len(ix) // 3 * 3].reshape(-1, 3)
    tris = []
    if p == 1:
        for i in range(len(ix) - 2):
            a, b_, c = ix[i], ix[i + 1], ix[i + 2]
            if a == b_ or b_ == c or a == c:
                continue
            tris.append((a, b_, c) if i % 2 == 0 else (b_, a, c))
    elif p == 2:
        for i in range(1, len(ix) - 1):
            tris.append((ix[0], ix[i], ix[i + 1]))
    return np.array(tris, dtype=np.int64).reshape(-1, 3)




def parse_texture(b):
    r = R(b)
    r.u32()
    r.u32()
    r.section()
    ln = r.u32()
    r.p += 12
    name = r.pstr(0x40)
    w, h, fmt, mip = r.u16(), r.u16(), r.u16(), r.u16()
    r.p += 0x10
    return dict(name=name, w=w, h=h, fmt=fmt, mip=mip, data=b[r.p:r.p + ln])


_ETC_MOD = [(2, 8, -2, -8), (5, 17, -5, -17), (9, 29, -9, -29), (13, 42, -13, -42),
            (18, 60, -18, -60), (24, 80, -24, -80), (33, 106, -33, -106), (47, 183, -47, -183)]


def _etc1_block(d):
    """Decode one 8-byte ETC1 block as the 3DS stores it (little-endian 64-bit) -> 4x4x3 uint8 [row][col]."""
    v = int.from_bytes(d, 'little')
    lo = v & 0xffffffff
    hi = v >> 32
    b0 = (hi >> 24) & 255
    b1 = (hi >> 16) & 255
    b2 = (hi >> 8) & 255
    b3 = hi & 255
    diff = (b3 >> 1) & 1
    flip = b3 & 1
    t1 = (b3 >> 5) & 7
    t2 = (b3 >> 2) & 7
    if diff:
        r1, g1, bl1 = b0 >> 3, b1 >> 3, b2 >> 3
        r2 = r1 + (((b0 & 7) ^ 4) - 4)
        g2 = g1 + (((b1 & 7) ^ 4) - 4)
        bl2 = bl1 + (((b2 & 7) ^ 4) - 4)
        c1 = [(x << 3) | (x >> 2) for x in (r1, g1, bl1)]
        c2 = [((x & 31) << 3) | ((x & 31) >> 2) for x in (r2, g2, bl2)]
    else:
        c1 = [(b0 >> 4) * 17, (b1 >> 4) * 17, (b2 >> 4) * 17]
        c2 = [(b0 & 15) * 17, (b1 & 15) * 17, (b2 & 15) * 17]
    out = np.zeros((4, 4, 3), np.uint8)
    msb = (lo >> 16) & 0xffff
    lsb = lo & 0xffff
    for x in range(4):
        for y in range(4):
            k = x * 4 + y
            idx = ((msb >> k) & 1) << 1 | ((lsb >> k) & 1)
            sub = (0 if x < 2 else 1) if flip == 0 else (0 if y < 2 else 1)
            base = c1 if sub == 0 else c2
            mod = _ETC_MOD[t1 if sub == 0 else t2][idx]
            for c in range(3):
                out[y, x, c] = max(0, min(255, base[c] + mod))
    return out


def _unmorton(w, h, tile=8):
    xs, ys = [], []
    for ty in range(0, h, tile):
        for tx in range(0, w, tile):
            for i in range(tile * tile):
                x = (i & 1) | ((i >> 2) & 1) << 1 | ((i >> 4) & 1) << 2
                y = ((i >> 1) & 1) | ((i >> 3) & 1) << 1 | ((i >> 5) & 1) << 2
                xs.append(tx + x)
                ys.append(ty + y)
    return np.array(xs), np.array(ys)


def decode_texture(t):
    """-> (w, h, RGBA uint8 array h*w*4), top row first."""
    w, h, fmt, d = t['w'], t['h'], t['fmt'], t['data']
    img = np.zeros((h, w, 4), np.uint8)
    img[..., 3] = 255
    if fmt in (0x2a, 0x2b):
        has_a = fmt == 0x2b
        bs = 16 if has_a else 8
        nbx, nby = w // 4, h // 4
        order = []
        for ty in range(0, nby, 2):
            for tx in range(0, nbx, 2):
                for (bx, by) in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    order.append((tx + bx, ty + by))
        for n_, (bx, by) in enumerate(order):
            o = n_ * bs
            if o + bs > len(d):
                break
            blk = _etc1_block(d[o + (8 if has_a else 0):o + bs])
            img[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4, :3] = blk
            if has_a:
                av = int.from_bytes(d[o:o + 8], 'little')
                for k in range(16):
                    img[by * 4 + (k % 4), bx * 4 + (k // 4), 3] = ((av >> (4 * k)) & 15) * 17
        return w, h, img[::-1].copy()
    bpp = {0x2: 2, 0x3: 3, 0x4: 4, 0x16: 2, 0x17: 2, 0x23: 2, 0x24: 2, 0x25: 1, 0x26: 1, 0x27: 1, 0x28: 0.5, 0x29: 0.5}[fmt]
    xs, ys = _unmorton(w, h)
    n = w * h
    if bpp == 0.5:
        raw = np.frombuffer(d, np.uint8, n // 2)
        nib = np.stack([raw & 15, raw >> 4], 1).reshape(n, 1) * 17
        a = nib.astype(np.uint8)
    else:
        a = np.frombuffer(d, np.uint8, int(n * bpp)).reshape(n, int(bpp))
    px = np.zeros((n, 4), np.uint8)
    px[:, 3] = 255
    if fmt == 0x4:
        px = np.stack([a[:, 3], a[:, 2], a[:, 1], a[:, 0]], 1)
    elif fmt == 0x3:
        px[:, 0], px[:, 1], px[:, 2] = a[:, 2], a[:, 1], a[:, 0]
    elif fmt in (0x2, 0x16, 0x17):
        v = a[:, 0].astype(np.uint16) | a[:, 1].astype(np.uint16) << 8
        if fmt == 0x2:
            px[:, 0] = ((v >> 11) & 31) * 255 // 31
            px[:, 1] = ((v >> 5) & 63) * 255 // 63
            px[:, 2] = (v & 31) * 255 // 31
        elif fmt == 0x16:
            px[:, 0] = ((v >> 12) & 15) * 17
            px[:, 1] = ((v >> 8) & 15) * 17
            px[:, 2] = ((v >> 4) & 15) * 17
            px[:, 3] = (v & 15) * 17
        else:
            px[:, 0] = ((v >> 11) & 31) * 255 // 31
            px[:, 1] = ((v >> 6) & 31) * 255 // 31
            px[:, 2] = ((v >> 1) & 31) * 255 // 31
            px[:, 3] = (v & 1) * 255
    elif fmt == 0x23:
        px[:, 0] = px[:, 1] = px[:, 2] = a[:, 1]
        px[:, 3] = a[:, 0]
    elif fmt == 0x27:
        px[:, 0] = px[:, 1] = px[:, 2] = (a[:, 0] >> 4) * 17
        px[:, 3] = (a[:, 0] & 15) * 17
    elif fmt == 0x28:
        px[:, 0] = px[:, 1] = px[:, 2] = a[:, 0]
    elif fmt == 0x29:
        px[:, :3] = 255
        px[:, 3] = a[:, 0]
    elif fmt == 0x25:
        px[:, 0] = px[:, 1] = px[:, 2] = a[:, 0]
    elif fmt == 0x26:
        px[:, :3] = 255
        px[:, 3] = a[:, 0]
    elif fmt == 0x24:
        px[:, 0], px[:, 1], px[:, 2] = a[:, 1], a[:, 0], 0
    img[ys, xs] = px
    return w, h, img[::-1].copy()




def _keys(r, flags, frames):
    f = flags & 7
    if f == 3:
        return [(0, r.f32(), 0.0)]
    if f in (4, 5):
        n = r.u32()
        fr = [r.u16() if frames > 0xff else r.u8() for _ in range(n)]
        while r.p & 3:
            r.p += 1
        out = []
        if flags & 1:
            for i in range(n):
                out.append((fr[i], r.f32(), r.f32()))
        else:
            vs, vo, ss, so = r.f32(), r.f32(), r.f32(), r.f32()
            for i in range(n):
                v = r.u16() / 65535.0 * vs + vo
                s = r.u16() / 65535.0 * ss + so
                out.append((fr[i], v, s))
        return out
    return []


def parse_motion(b):
    """-> dict(frames, loop, bones={name: dict(axis=bool, ch=[9 key lists: sx sy sz rx ry rz tx ty tz])}) or None."""
    if len(b) < 16:
        return None
    r = R(b)
    r.u32()
    nsec = r.u32()
    if nsec > 16 or 8 + nsec * 12 > len(b):
        return None
    secs = [(r.u32(), r.u32(), r.u32()) for _ in range(nsec)]
    r.p = secs[0][2]
    frames = r.u32()
    loop = r.u16() & 1
    r.u16()
    r.fl(3)
    r.fl(3)
    r.u32()
    bones = {}
    for kind, ln, addr in secs[1:]:
        if kind == 1:
            r.p = addr
            nn = r.i32()
            nl = r.u32()
            st = r.p
            names = [r.bstr() for _ in range(nn)]
            r.p = st + nl
            for nm in names:
                fl = r.u32()
                r.u32()
                ax = (fl >> 31) == 0
                ch = []
                f = fl
                for e in range(9):
                    ch.append(_keys(r, f, frames))
                    f >>= 3
                bones[nm] = dict(axis=ax, ch=ch)
    vis, mats = {}, []
    for kind, ln, addr in secs[1:]:
        if kind == 6:
            r.p = addr
            nn = r.i32()
            nl = r.u32()
            st = r.p
            names = [r.bstr() for _ in range(nn)]
            r.p = st + nl
            for nm in names:
                vals = []
                byte = 0
                for i in range(frames + 1):
                    if (i & 7) == 0:
                        byte = r.u8()
                    vals.append(bool(byte & (1 << (i & 7))))
                vis[nm] = vals
        elif kind == 3:
            r.p = addr
            nn = r.i32()
            nl = r.u32()
            units = [r.u32() for _ in range(nn)]
            st = r.p
            names = [r.bstr() for _ in range(nn)]
            r.p = st + nl
            for nm, nu in zip(names, units):
                for _ in range(nu):
                    unit = r.u32()
                    fl = r.u32()
                    r.u32()
                    ch = []
                    for e in range(5):
                        ch.append(_keys(r, fl, frames))
                        fl >>= 3
                    mats.append(dict(name=nm, unit=unit, sx=ch[0], sy=ch[1], rot=ch[2], tx=ch[3], ty=ch[4]))
    return dict(frames=frames, loop=bool(loop), bones=bones, vis=vis, mats=mats)


def herp(k, frame):
    """Hermite sample of one channel (list of (frame, value, slope)); None when the channel has no keys."""
    if not k:
        return None
    if len(k) == 1 or frame <= k[0][0]:
        return k[0][1]
    if frame >= k[-1][0]:
        return k[-1][1]
    lo = 0
    for i in range(len(k) - 1):
        if k[i][0] <= frame:
            lo = i
    a, b = k[lo], k[lo + 1]
    d = b[0] - a[0]
    if d == 0:
        return a[1]
    t = (frame - a[0]) / d
    if a[2] is None or b[2] is None:
        return a[1] + (b[1] - a[1]) * t
    t2, t3 = t * t, t * t * t
    return ((2 * t3 - 3 * t2 + 1) * a[1] + (t3 - 2 * t2 + t) * a[2] * d +
            (-2 * t3 + 3 * t2) * b[1] + (t3 - t2) * b[2] * d)
