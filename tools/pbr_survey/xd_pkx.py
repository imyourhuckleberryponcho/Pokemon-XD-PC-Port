"""Numbers out of an XD battle model (unpacked PKX, type 15): header, motion sections, and the HSD archive's
joints, meshes, triangles, textures and animations (read only).

Usage: python tools/pbr_survey/xd_pkx.py <unpacked pkx> [--sections] [--tree]
Layouts: PKX header / sections from the decomp's modelSequence.cpp (PkxHeader, MotionSection); HSD structures
as in the public sysdolphin headers (JObjDesc, DObjDesc, PObjDesc, MObjDesc, TObjDesc, AnimJoint, AObjDesc).
"""
import struct, sys


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def bes32(b, o):
    return struct.unpack_from('>i', b, o)[0]


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def bef(b, o):
    return struct.unpack_from('>f', b, o)[0]


def align32(x):
    return (x + 0x1F) & ~0x1F


GX_FMT = {0: 'I4', 1: 'I8', 2: 'IA4', 3: 'IA8', 4: 'RGB565', 5: 'RGB5A3', 6: 'RGBA8', 8: 'CI4', 9: 'CI8', 10: 'CI14X2',
          14: 'CMPR'}


class Hsd:
    def __init__(self, b):
        self.b = b
        self.file_size, self.data_size, self.n_reloc, self.n_pub, self.n_ext = struct.unpack_from('>5I', b, 0)
        self.d = b[0x20:0x20 + self.data_size]
        o = 0x20 + self.data_size + self.n_reloc * 4
        strs = o + (self.n_pub + self.n_ext) * 8
        self.public = []
        for i in range(self.n_pub):
            off, name = struct.unpack_from('>II', b, o + i * 8)
            s = b[strs + name:b.index(b'\0', strs + name)].decode('latin1')
            self.public.append((s, off))


def analyse(hsd, show_tree=False):
    d = hsd.d
    out = {'joints': 0, 'named_joints': 0, 'dobj': 0, 'pobj': 0, 'tris': 0, 'draw_verts': 0, 'textures': {},
           'envelope_pobj': 0, 'max_env': 0, 'pos_arrays': {}, 'mobj': 0, 'tobj': 0, 'tev_tobj': 0, 'anims': [],
           'matanims': 0, 'shapeanim': 0, 'max_depth': 0, 'pobj_flags': {}, 'attrs': {}, 'max_tobj_per_mobj': 0,
           'dl_bytes': 0, 'max_weights': 0}

    def vtx_layout(o):
        lay = []
        while True:
            attr, atype, cnt, ctype = struct.unpack_from('>4I', d, o)
            if attr == 0xFF:
                break
            frac = d[o + 0x10]
            stride = be16(d, o + 0x12)
            ptr = be32(d, o + 0x14)
            lay.append((attr, atype, cnt, ctype, frac, stride, ptr))
            o += 0x18
        return lay

    def vsize(lay):
        n = 0
        for attr, atype, cnt, ctype, frac, stride, ptr in lay:
            if atype == 0:
                continue
            if atype == 2:
                n += 1
            elif atype == 3:
                n += 2
            else:
                if attr <= 8:
                    n += 1
                elif attr in (11, 12):
                    n += {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}.get(ctype, 4)
                else:
                    comps = {9: (2, 3), 10: (3, 9), 25: (9, 9)}.get(attr, (1, 2))[cnt & 1] if attr in (9, 10, 25) else (1 + (cnt & 1))
                    n += comps * {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}.get(ctype, 4)
        return n

    def pobj(o):
        while o:
            out['pobj'] += 1
            lay = vtx_layout(be32(d, o + 8))
            flags = be16(d, o + 0xC)
            n_disp = be16(d, o + 0xE)
            disp = be32(d, o + 0x10)
            kind = (flags >> 12) & 3
            out['pobj_flags'][kind] = out['pobj_flags'].get(kind, 0) + 1
            for a in lay:
                key = (a[0], a[1], a[2], a[3])
                out['attrs'][key] = out['attrs'].get(key, 0) + 1
            if kind == 2:
                out['envelope_pobj'] += 1
                env = be32(d, o + 0x14)
                k = 0
                while env and be32(d, env + k * 4):
                    e = be32(d, env + k * 4)
                    w = 0
                    while be32(d, e + w * 8):
                        w += 1
                    out['max_weights'] = max(out['max_weights'], w)
                    k += 1
                out['max_env'] = max(out['max_env'], k)
            if kind == 1:
                out['shapeanim'] += 1
            vs = vsize(lay)
            posptr = [a[6] for a in lay if a[0] == 9]
            pos_at = None
            pos_idx = 0
            off = 0
            for a in lay:
                if a[0] == 9:
                    pos_at = off
                    pos_idx = a[1]
                off += 0 if a[1] == 0 else 1 if (a[1] == 2 or (a[1] == 1 and a[0] <= 8)) else 2 if a[1] == 3 else 0
            p = disp
            end = disp + n_disp * 32
            out['dl_bytes'] += n_disp * 32
            maxi = out['pos_arrays'].get(posptr[0], -1) if posptr else -1
            while p < end and vs:
                op = d[p]
                if op == 0:
                    break
                cnt = be16(d, p + 1)
                p += 3
                prim = op & 0xF8
                out['draw_verts'] += cnt
                if prim == 0x90:
                    out['tris'] += cnt // 3
                elif prim in (0x98, 0xA0):
                    out['tris'] += max(cnt - 2, 0)
                elif prim == 0x80:
                    out['tris'] += cnt // 4 * 2
                if pos_at is not None and pos_idx in (2, 3):
                    for v in range(cnt):
                        q = p + v * vs + pos_at
                        i = d[q] if pos_idx == 2 else be16(d, q)
                        if i > maxi:
                            maxi = i
                p += cnt * vs
            if posptr:
                out['pos_arrays'][posptr[0]] = maxi
            o = be32(d, o + 4)

    def mobj(o):
        out['mobj'] += 1
        t = be32(d, o + 8)
        n = 0
        while t:
            n += 1
            out['tobj'] += 1
            img = be32(d, t + 0x4C)
            if be32(d, t + 0x58):
                out['tev_tobj'] += 1
            if img:
                w, h, fmt = be16(d, img + 4), be16(d, img + 6), be32(d, img + 8)
                out['textures'][be32(d, img)] = (w, h, GX_FMT.get(fmt, fmt))
            t = be32(d, t + 4)
        out['max_tobj_per_mobj'] = max(out['max_tobj_per_mobj'], n)

    def jobj(o, depth):
        while o:
            out['joints'] += 1
            out['max_depth'] = max(out['max_depth'], depth)
            flags = be32(d, o + 4)
            if be32(d, o):
                out['named_joints'] += 1
            if show_tree:
                print('  ' * depth + 'joint %d flags %08X' % (out['joints'] - 1, flags))
            if not flags & 0x4020:
                dob = be32(d, o + 0x10)
                while dob:
                    out['dobj'] += 1
                    if be32(d, dob + 8):
                        mobj(be32(d, dob + 8))
                    pobj(be32(d, dob + 0xC))
                    dob = be32(d, dob + 4)
            jobj(be32(d, o + 8), depth + 1)
            o = be32(d, o + 0xC)

    def animjoint_len(o, acc):
        while o:
            a = be32(d, o + 8)
            if a:
                acc[0] = max(acc[0], bef(d, a + 4))
                f = be32(d, a + 8)
                while f:
                    acc[1] += 1
                    acc[2] += be32(d, f + 4)
                    f = be32(d, f)
            animjoint_len(be32(d, o), acc)
            o = be32(d, o + 4)

    for name, off in hsd.public:
        if name != 'scene_data':
            continue
        sets = be32(d, off)
        k = 0
        while be32(d, sets + k * 4):
            ms = be32(d, sets + k * 4)
            j, aj, maj, saj = struct.unpack_from('>4I', d, ms)
            jobj(j, 0)
            i = 0
            while aj and be32(d, aj + i * 4):
                acc = [0.0, 0, 0]
                animjoint_len(be32(d, aj + i * 4), acc)
                out['anims'].append(acc)
                i += 1
            i = 0
            while maj and be32(d, maj + i * 4):
                i += 1
            out['matanims'] = i
            k += 1
        out['modelsets'] = k
    return out


def main():
    b = open(sys.argv[1], 'rb').read()
    size, _, unk8, _, count, model_size = struct.unpack_from('>6I', b, 0)
    pid, ptype = be16(b, 0x18), be16(b, 0x1A)
    print('PKX: archive size %d, particle bytes %d, sections %d, model size class %d, id %d, type 0x%X, head joint %d'
          % (size, unk8, count, bes32(b, 0x14), pid, ptype, be16(b, 0x6E)))
    secs = []
    for s in range(count):
        o = 0x84 + s * 0xD0
        n = bes32(b, o + 4)
        joints = struct.unpack_from('>16i', b, o + 0x4C)
        motions = [struct.unpack_from('>ii', b, o + 0x8C + m * 8) for m in range(8)]
        secs.append((n, joints, motions))
        if '--sections' in sys.argv:
            print(' section %2d: motions %d %s  joints %s  head bytes %s' % (
                s, n, ' '.join('%d:%d' % m for m in motions[:max(n, 0)]), ' '.join(str(j) for j in joints),
                b[o:o + 4].hex() + ' ' + b[o + 8:o + 0x24].hex(' ', 4)))
    body = align32(0x84 + count * 0xD0)
    if unk8:
        body += align32(unk8)
    hsd = Hsd(b[body:body + size])
    print('HSD archive at 0x%X: %d bytes, data %d, relocations %d, public %s' % (
        body, hsd.file_size, hsd.data_size, hsd.n_reloc, [n for n, o in hsd.public]))
    r = analyse(hsd, '--tree' in sys.argv)
    verts = sum(v + 1 for v in r['pos_arrays'].values())
    print('joints %d (named %d, depth %d); dobj %d, pobj %d (by kind %s), display lists %d bytes' % (
        r['joints'], r['named_joints'], r['max_depth'], r['dobj'], r['pobj'], r['pobj_flags'], r['dl_bytes']))
    print('triangles %d, vertices drawn %d, distinct positions %d in %d arrays' % (
        r['tris'], r['draw_verts'], verts, len(r['pos_arrays'])))
    print('skinned pobj %d, most matrices in one pobj %d, most weights on one matrix %d' % (
        r['envelope_pobj'], r['max_env'], r['max_weights']))
    print('materials %d, texture objects %d (with TEV block %d, most per material %d)' % (
        r['mobj'], r['tobj'], r['tev_tobj'], r['max_tobj_per_mobj']))
    tex = sorted(r['textures'].values())
    print('textures %d: %s' % (len(tex), ', '.join('%dx%d %s' % t for t in tex)))
    print('vertex attributes (attr,type,cnt,comp)->pobj:', r['attrs'])
    print('animations %d (material animation sets %d): frames/tracks/track bytes' % (len(r['anims']), r['matanims']))
    print('  ' + ', '.join('%d:%g/%d/%d' % (i, a[0], a[1], a[2]) for i, a in enumerate(r['anims'])))
    used = sorted(set(m[1] for n, j, ms in secs for m in ms[:max(n, 0)] if m[0] != 0))
    print('animation indices used by the sections:', used)


if __name__ == '__main__':
    main()
