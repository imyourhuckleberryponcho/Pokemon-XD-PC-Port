"""Numbers out of a Battle Revolution model file (unpacked FSYS member whose first bytes are 01 00 00 04).

The layout below was worked out from the files during the survey (no outside documentation used); every
field marked "?" is unknown. All offsets are from the file start unless said otherwise, big-endian.

  file header   +08 offset of the model pointer table   +0C offset of the texture pointer table
                +14 offset of the material pointer table +18 u16 models, +1A u16 textures, +1E u16 materials
  texture head  u16 width, u16 height, u8 bits?, u8 levels?, +0B u8 format (the XD texture file's codes:
                0x44 RGB565, 0x90 RGB5A3, 0xB0 CMPR ...), +28 data offset FROM THIS HEADER
  model         +00 name, +04 u32 nodes, +08 u16 animations, +0C animation table, +10 root node
  animation     0x30 bytes: +00 name, +04 +08 +0C three times in seconds (+0C = length), ... flags
  node          +00 kind (0 joint, 3 skinned mesh, 6 second mesh "..._col"), +04 name, +08 u16 number, +0A u16 ?,
                +0C translation (3 floats) ptr, +10 rotation? ptr, +14 scale ptr, +18 float (radius of "ct_" nodes),
                +20 first per-animation track set, +24 child, +28 next, +30 kind's data (kind 3: mesh ptr)
  track set     +00 ?, +04 u32 tracks, +08 track table, +0C float length, +10 next set (the next animation's)
  mesh          +02 u16 vertices, +04 u32 ?, +08 positions+normals (24 bytes each), +0C weights, +14 UVs,
                +18 first draw group, +1C bounds (per animation, per frame)
  draw group    +08 material, +0C u16 attributes?, +10 vertex description (8 bytes per attribute: GX attribute,
                component count, component type, index type(2 = 8 bit, 3 = 16 bit), stride; 0xFF ends),
                +14 GX display list, +18 its size, +1C next draw group

Usage: python tools/pbr_survey/sdr.py <file> [--nodes] [--anims] [--brief]
"""
import struct, sys

FMT = {0x00: 'C4', 0x01: 'C8', 0x30: 'C14X2', 0x40: 'I4', 0x41: 'IA4', 0x42: 'I8', 0x43: 'IA8', 0x44: 'RGB565',
       0x45: 'RGBA8', 0x90: 'RGB5A3', 0xB0: 'CMPR'}


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def bef(b, o):
    return struct.unpack_from('>f', b, o)[0]


def cstr(b, o):
    if not o or o >= len(b):
        return ''
    return b[o:b.index(b'\0', o)].decode('latin1')


def parse(b):
    r = {'ok': b[:4] == b'\x01\x00\x00\x04', 'size': len(b)}
    if not r['ok']:
        return r
    models_at, tex_at, mat_at = be32(b, 8), be32(b, 0xC), be32(b, 0x14)
    n_models, n_tex, n_mats = be16(b, 0x18), be16(b, 0x1A), be16(b, 0x1E)
    r['materials'] = n_mats
    r['mat_names'] = [cstr(b, be32(b, be32(b, mat_at + 4 * i))) for i in range(n_mats)] if mat_at else []
    tex = []
    for i in range(n_tex):
        h = be32(b, tex_at + 4 * i)
        tex.append((be16(b, h), be16(b, h + 2), FMT.get(b[h + 0xB], '0x%02X' % b[h + 0xB]), b[h + 4], b[h + 5]))
    r['textures'] = tex
    r['models'] = []
    for m in range(n_models):
        mo = be32(b, models_at + 4 * m)
        md = {'name': cstr(b, be32(b, mo)), 'node_count': be32(b, mo + 4), 'anims': [], 'nodes': [], 'meshes': []}
        n_anim, anim_at, root = be16(b, mo + 8), be32(b, mo + 0xC), be32(b, mo + 0x10)
        for a in range(n_anim):
            o = anim_at + a * 0x30
            md['anims'].append((cstr(b, be32(b, o)), bef(b, o + 4), bef(b, o + 8), bef(b, o + 0xC), b[o + 0x28:o + 0x2C].hex()))
        seen = set()

        def walk(o, depth, parent):
            while o and o not in seen and o + 0x30 <= len(b):
                seen.add(o)
                kind, name, num = be32(b, o), cstr(b, be32(b, o + 4)), be16(b, o + 8)
                sets = 0
                tracks = 0
                s = be32(b, o + 0x20)
                guard = 0
                while s and guard < 200 and s + 0x14 <= len(b) and s % 4 == 0:
                    sets += 1
                    tracks += be32(b, s + 4)
                    s = be32(b, s + 0x10)
                    guard += 1
                md['nodes'].append((num, depth, kind, name, parent, sets, tracks, be16(b, o + 0xA)))
                if kind == 3:
                    md['meshes'].append(mesh(be32(b, o + 0x30), name))
                elif kind == 6:
                    md['meshes'].append({'name': name, 'verts': be16(b, o + 0x32), 'tris': be16(b, o + 0x34), 'col': True})
                walk(be32(b, o + 0x24), depth + 1, num)
                o = be32(b, o + 0x28)

        def mesh(o, name):
            me = {'name': name, 'verts': be16(b, o + 2), 'tris': 0, 'drawn': 0, 'groups': 0, 'dl_bytes': 0, 'attrs': set(),
                  'col': False, 'head': b[o:o + 8].hex()}
            g = be32(b, o + 0x18)
            guard = 0
            while g and guard < 500:
                guard += 1
                me['groups'] += 1
                vd, dl, size = be32(b, g + 0x10), be32(b, g + 0x14), be32(b, g + 0x18)
                vs = 0
                k = vd
                while b[k] != 0xFF:
                    attr, cnt, ctype, itype = b[k], b[k + 1], b[k + 2], b[k + 4]
                    me['attrs'].add((attr, cnt, ctype, itype, b[k + 5]))
                    vs += 1 if itype == 2 else 2 if itype == 3 else 0
                    if itype not in (2, 3):
                        me['direct'] = True
                    k += 8
                me['dl_bytes'] += size
                p, end = dl, dl + size
                while p < end and vs:
                    op = b[p]
                    if op == 0:
                        p += 1
                        continue
                    if op & 0x80 == 0:
                        me['odd_op'] = op
                        break
                    cnt = be16(b, p + 1)
                    p += 3 + cnt * vs
                    me['drawn'] += cnt
                    prim = op & 0xF8
                    me['tris'] += cnt // 3 if prim == 0x90 else max(cnt - 2, 0) if prim in (0x98, 0xA0) else cnt // 4 * 2 if prim == 0x80 else 0
                g = be32(b, g + 0x1C)
            return me

        walk(root, 0, -1)
        r['models'].append(md)
    return r


def main():
    b = open(sys.argv[1], 'rb').read()
    r = parse(b)
    if not r['ok']:
        print('not a model file of this kind')
        return 1
    print('file %d bytes; materials %d; textures %d: %s' % (
        r['size'], r['materials'], len(r['textures']), ', '.join('%dx%d %s' % t[:3] for t in r['textures'])))
    for md in r['models']:
        nodes = md['nodes']
        kinds = {}
        for n in nodes:
            kinds[n[2]] = kinds.get(n[2], 0) + 1
        print('model "%s": nodes said %d, walked %d (by kind %s), deepest %d, animated nodes %d' % (
            md['name'], md['node_count'], len(nodes), kinds, max(n[1] for n in nodes), sum(1 for n in nodes if n[5])))
        for me in md['meshes']:
            if me['col']:
                print('  second mesh "%s": %d vertices, %d triangles' % (me['name'], me['verts'], me['tris']))
            else:
                print('  mesh "%s": %d vertices, %d triangles (%d drawn vertices), %d draw groups, display lists %d bytes, '
                      'attributes %s%s' % (me['name'], me['verts'], me['tris'], me['drawn'], me['groups'], me['dl_bytes'],
                                           sorted(me['attrs']), ' ODD' if 'odd_op' in me or 'direct' in me else ''))
        print('  animations %d: %s' % (len(md['anims']), ', '.join('%s %.3gs(%d fr)' % (a[0], a[3], round(a[3] * 30)) for a in md['anims'])))
        if '--anims' in sys.argv:
            for a in md['anims']:
                print('    %-12s times %.3f %.3f %.3f flags %s' % a)
        if '--nodes' in sys.argv:
            for n in nodes:
                print('    %3d %s%s  kind %d  anim sets %d tracks %d flags %04X' % (n[0], '  ' * n[1], n[3], n[2], n[5], n[6], n[7]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
