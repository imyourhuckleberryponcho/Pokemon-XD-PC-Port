"""X/Y models from a BCH: the first model (skeleton, materials, meshes with skin) and the textures of a texture BCH.
Struct layouts of the format's version 7 as the open documentation of the H3D classes describes them, checked on the
player's own files (see STUDY.md). Reads only."""
import struct
import numpy as np

import gf_read as _g
from bch_read import Bch


class _Uniforms:
    """The vertex shader's float constants set by a mesh's commands (they hold the attribute scales)."""

    def __init__(self):
        self.u = np.zeros((96, 4))
        self.idx = 0
        self.f32 = False
        self.w = [0, 0, 0]

    def set_index(self, v):
        self.idx = (v & 0xff) << 2
        self.f32 = bool(v >> 31)

    def data(self, params):
        for p in params:
            ui = (self.idx >> 2) & 0x5f
            if self.f32:
                val = struct.unpack('<f', struct.pack('<I', p))[0]
                self.u[ui][[3, 2, 1, 0][self.idx & 3]] = val
            else:
                self.w[self.idx & 3] = p
                if (self.idx & 3) == 2:
                    self.idx += 1
                    self.u[ui] = _g.vec_f24(*self.w)
            self.idx += 1


def _mesh_attributes(cmds):
    """A mesh's enable commands -> (attrs, fixed, stride, buffer address, uniforms)."""
    un = _Uniforms()
    fmt = perm = battr = 0
    stride = ntot = addr = 0
    fixed = {}
    fidx = 0
    for rg, ps, mk in _g.pica_cmds(cmds):
        p = ps[0]
        if rg == 0x201:
            fmt |= p
        elif rg == 0x202:
            fmt |= p << 32
        elif rg == 0x203:
            addr = p
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
        elif rg == 0x2c0:
            un.set_index(p)
        elif 0x2c1 <= rg <= 0x2c8:
            un.data(ps)
    attrs, fixa = [], []
    scale_of = {'pos': (7, 0), 'nrm': (7, 1), 'tan': (7, 2), 'col': (7, 3), 'uv0': (8, 0), 'uv1': (8, 1), 'uv2': (8, 2),
                'bw': (8, 3)}
    for k in range(ntot):
        if (fmt >> (48 + k)) & 1:
            nm = _g.AN.get((perm >> (k * 4)) & 15, 'x')
            fixa.append((nm, _g.vec_f24(*fixed.get(k, [0, 0, 0]))))
        else:
            pi = (battr >> (k * 4)) & 15
            nm = _g.AN.get((perm >> (pi * 4)) & 15, 'x')
            f = (fmt >> (pi * 4)) & 15
            sc = 1.0
            if nm in scale_of:
                r_, c_ = scale_of[nm]
                sc = float(un.u[r_][c_])
            attrs.append(dict(name=nm, fmt=f & 3, n=(f >> 2) + 1, scale=sc))
    return attrs, fixa, stride, addr, un


def parse_model(b, mp=None):
    """The first model of a BCH -> dict like gf_read.parse_model (bones, materials, meshes), X/Y flavour."""
    dd = b.contents_dicts()
    if mp is None:
        mp = b.u32(dd[0][0])
    mv, mc, mt = b.u32(mp + 52), b.u32(mp + 56), b.u32(mp + 60)
    sv, sc, st = b.u32(mp + 112), b.u32(mp + 116), b.u32(mp + 120)
    names = b.dict_names(st)
    bones = []
    for i in range(sc):
        q = sv + i * 100
        par = b.i16(q + 4)
        bones.append(dict(name=b.cstr(b.u32(q + 92)), parent=(names[par] if par >= 0 else ''), flags=b.u32(q),
                          scale=b.fl(q + 8, 3), rot=b.fl(q + 20, 3), pos=b.fl(q + 32, 3)))
    mats = []
    for i in range(mc):
        q = mv + i * 88
        pp = b.u32(q)
        tex0 = b.cstr(b.u32(q + 72))
        co = dict(name=tex0, unit=0, mapping=0, scale=b.fl(pp + 16, 2), rot=b.f32(pp + 24), trans=b.fl(pp + 28, 2),
                  wrap=(b.d[q + 25], b.d[q + 26]))
        mats.append(dict(name=b.cstr(b.u32(q + 84)), shader='', vtx_shader='', frag_shader='', coords=[co],
                         colors=dict(diffuse=(255, 255, 255, 255))))
    vp_, vn_ = b.u32(mp + 124), b.u32(mp + 128)
    node_names = b.dict_names(b.u32(mp + 140)) if b.u32(mp + 140) else []
    node_vis = [bool(b.d[vp_ + (i >> 3)] >> (i & 7) & 1) for i in range(vn_)] if vp_ else []
    meshes_v, meshes_n = b.u32(mp + 64), b.u32(mp + 68)
    meshes = []
    for i in range(meshes_n):
        q = meshes_v + i * 56
        midx = b.u16(q)
        node = b.u16(q + 4)
        en_ptr, en_n = b.u32(q + 8), b.u32(q + 12)
        sub_ptr, sub_n = b.u32(q + 16), b.u32(q + 20)
        cmds = struct.unpack_from('<%dI' % en_n, b.d, en_ptr)
        attrs, fixa, stride, addr, un = _mesh_attributes(cmds)
        subs = []
        maxidx = 0
        for s in range(sub_n):
            sq = sub_ptr + s * 52
            skin = b.d[sq]
            nbi = b.u16(sq + 2)
            bi = [b.u16(sq + 4 + 2 * k) for k in range(nbi)]
            cp, cn = b.u32(sq + 44), b.u32(sq + 48)
            bufaddr = cnt = prim = 0
            for rg, ps, mk in _g.pica_cmds(struct.unpack_from('<%dI' % cn, b.d, cp)):
                if rg == 0x227:
                    bufaddr = ps[0]
                elif rg == 0x228:
                    cnt = ps[0]
                elif rg == 0x25e:
                    prim = ps[0] >> 8
            a = bufaddr & 0x7fffffff
            if bufaddr >> 31:
                ix = np.frombuffer(bytes(b.d[a:a + cnt * 2]), '<u2').astype(np.int64)
            else:
                ix = np.frombuffer(bytes(b.d[a:a + cnt]), 'u1').astype(np.int64)
            maxidx = max(maxidx, int(ix.max()) if len(ix) else 0)
            subs.append(dict(name=mats[midx]['name'] if midx < len(mats) else '', bone_idx=bi, skin=skin, indices=ix,
                             prim=prim, attrs=attrs, fixed=fixa, stride=stride))
        raw = bytes(b.d[addr:addr + (maxidx + 1) * stride])
        pos_off = un.u[6]
        for sm in subs:
            sm['raw'] = raw
            sm['verts'] = _g.decode_verts(sm)
            if 'pos' in sm['verts']:
                sm['verts']['pos'] = sm['verts']['pos'][:, :3] + pos_off[:3]
        meshes.append(dict(name=(node_names[node] if node < len(node_names) else 'mesh%d' % i), material=midx, subs=subs, node=node,
                           hidden=(node < len(node_vis) and not node_vis[node])))
    return dict(bones=bones, materials=mats, meshes=meshes, tex_names=[], bbox=None)


H3D_TO_GF_FMT = {0: 0x4, 1: 0x3, 2: 0x17, 3: 0x2, 4: 0x16, 5: 0x23, 6: 0x24, 7: 0x25, 8: 0x26, 9: 0x27, 10: 0x28, 11: 0x29,
                 12: 0x2a, 13: 0x2b}
_BITS = {0x4: 32, 0x3: 24, 0x17: 16, 0x2: 16, 0x16: 16, 0x23: 16, 0x24: 16, 0x25: 8, 0x26: 8, 0x27: 8, 0x28: 4, 0x29: 4,
         0x2a: 4, 0x2b: 8}


def parse_textures(b):
    """-> {name: (w, h, rgba)} for the textures of a BCH (decoded as gf_read does, top row first)."""
    vp, cnt, nm = b.contents_dicts()[3]
    out = {}
    for i in range(cnt):
        q = b.u32(vp + 4 * i)
        cp, cn = b.u32(q), b.u32(q + 4)
        fmt = b.d[q + 24]
        name = b.cstr(b.u32(q + 28))
        w = h = addr = 0
        for rg, ps, mk in _g.pica_cmds(struct.unpack_from('<%dI' % cn, b.d, cp)):
            if rg == 0x82:
                h = ps[0] & 0x7ff
                w = (ps[0] >> 16) & 0x7ff
            elif rg == 0x85:
                addr = ps[0]
        gf = H3D_TO_GF_FMT[fmt]
        ln = w * h * _BITS[gf] // 8
        t = dict(name=name, w=w, h=h, fmt=gf, data=bytes(b.d[addr:addr + ln]))
        out[name] = _g.decode_texture(t)
    return out


def parse_vis_anim(b):
    """A visibility-animation BCH -> {node name: [bool per frame]}."""
    vp, cnt, nm = b.contents_dicts()[10]
    out = {}
    for k in range(cnt):
        ap = b.u32(vp + 4 * k)
        ep, en = b.u32(ap + 12), b.u32(ap + 16)
        for i in range(en):
            e = b.u32(ep + 4 * i)
            name = b.cstr(b.u32(e))
            lp, ln = b.u32(e + 20), b.u32(e + 24)
            out[name] = [bool(b.d[lp + (j >> 3)] >> (j & 7) & 1) for j in range(max(ln, 1))]
    return out
