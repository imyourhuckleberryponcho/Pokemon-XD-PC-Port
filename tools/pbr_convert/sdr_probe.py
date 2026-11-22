"""Raw look at a Battle Revolution model file (sdr): prints the fields the converter relies on so each layout
fact can be checked against the bytes. Development aid (output names game data: keep under build/).

Usage: python tools/pbr_convert/sdr_probe.py <file.sdr> [head|nodes|mesh|weights|anim <node name> [action]|tex|mat]
"""
import struct, sys

b = open(sys.argv[1], 'rb').read()
what = sys.argv[2] if len(sys.argv) > 2 else 'head'
u32 = lambda o: struct.unpack_from('>I', b, o)[0]
u16 = lambda o: struct.unpack_from('>H', b, o)[0]
f32 = lambda o: struct.unpack_from('>f', b, o)[0]


def cstr(o):
    return b[o:b.index(b'\0', o)].decode('latin1') if o and o < len(b) else ''


def fl(o, n):
    return ' '.join('%.4g' % x for x in struct.unpack_from('>%df' % n, b, o)) if o else '-'


def nodes(root):
    out = []

    def walk(o, depth, parent):
        while o:
            out.append((o, depth, parent))
            walk(u32(o + 0x24), depth + 1, o)
            o = u32(o + 0x28)
    walk(root, 0, 0)
    return out


sk = u32(u32(8))
root = u32(sk + 0x10)
if what == 'head':
    print('header', b[:0x40].hex(' ', 4))
    print('skeleton', b[sk:sk + 0x20].hex(' ', 4), cstr(u32(sk)))
    n_act = u16(sk + 8)
    for a in range(n_act):
        o = u32(sk + 0xC) + a * 0x30
        print('  action %d %-10s %s' % (a, cstr(u32(o)), b[o + 4:o + 0x30].hex(' ', 4)))
elif what == 'nodes':
    for o, depth, parent in nodes(root):
        kind = u32(o)
        print('%s%s @%x kind %d idx %d flags %04X  T(%s) R(%s) S(%s) piv@%x anim@%x' % (
            '  ' * depth, cstr(u32(o + 4)), o, kind, u16(o + 8), u16(o + 0xA), fl(u32(o + 0xC), 3), fl(u32(o + 0x10), 3),
            fl(u32(o + 0x14), 3), u32(o + 0x18), u32(o + 0x20)))
        if u32(o + 0x18):
            print('%s    pivots %s' % ('  ' * depth, fl(u32(o + 0x18), 12)))
        print('%s    +2C %s' % ('  ' * depth, b[o + 0x2C:o + 0x44].hex(' ', 4)))
        if kind == 2:
            print('%s    jflags %08X orient(%s) invbind %s' % ('  ' * depth, u32(o + 0x30), fl(o + 0x34, 3), fl(o + 0x44, 12)))
elif what == 'mesh':
    for o, depth, parent in nodes(root):
        if u32(o) == 3:
            m = u32(o + 0x30)
            print('mesh of', cstr(u32(o + 4)), '@%x' % m, b[m:m + 0x30].hex(' ', 4))
            print(' first vertices', fl(u32(m + 8), 12))
            uvl = u32(m + 0x14)
            print(' uv layer @%x: %s' % (uvl, b[uvl:uvl + 0x10].hex(' ', 4)))
            p = u32(m + 0x18)
            while p:
                print(' part @%x %s material %s' % (p, b[p:p + 0x20].hex(' ', 4), cstr(u32(u32(p + 8)))))
                k = u32(p + 0x10)
                while b[k] != 0xFF:
                    print('   attr', b[k:k + 8].hex(' '))
                    k += 8
                print('   prims', b[u32(p + 0x14):u32(p + 0x14) + 32].hex(' '))
                p = u32(p + 0x1C)
elif what == 'weights':
    for o, depth, parent in nodes(root):
        if u32(o) == 3:
            m = u32(o + 0x30)
            w = u32(m + 0xC)
            print('mesh', cstr(u32(o + 4)), 'vertices', u16(m + 2), 'weights @%x' % w)
            if not w:
                continue
            print(' block', b[w:w + 0x20].hex(' ', 4))
            n1, p1, n2, p2, p2w, n3, p3 = u32(w), u32(w + 4), u32(w + 8), u32(w + 0xC), u32(w + 0x10), u32(w + 0x14), u32(w + 0x18)
            print(' list1 %d: %s' % (n1, b[p1:p1 + min(n1, 12) * 4].hex(' ', 2)))
            print(' list2 %d: %s' % (n2, b[p2:p2 + min(n2, 8) * 6].hex(' ', 2)))
            print(' list2 amounts: %s' % b[p2w:p2w + 32].hex(' ', 2))
            print(' list3 %d: %s' % (n3, b[p3:p3 + min(n3, 6) * 10].hex(' ', 2)))
elif what == 'anim':
    name = sys.argv[3]
    want = int(sys.argv[4]) if len(sys.argv) > 4 else None
    for o, depth, parent in nodes(root):
        if cstr(u32(o + 4)) != name:
            continue
        a = u32(o + 0x20)
        while a:
            act, nc, cp = u16(a), u16(a + 2), u32(a + 4)
            print('block @%x action %d curves %d  raw %s' % (a, act, nc, b[a:a + 0x10].hex(' ', 4)))
            if want is None or want == act:
                for c in range(nc):
                    q = cp + c * 0x10
                    kd = u32(q + 8)
                    print('  curve %s -> keydata @%x %s' % (b[q:q + 0x10].hex(' '), kd, b[kd:kd + 0x18].hex(' ', 4)))
                    vals, slopes, nv, keys, nk = u32(kd), u32(kd + 4), u16(kd + 8), u32(kd + 0x10), u16(kd + 0x14)
                    print('    values %d @%x: %s' % (nv, vals, b[vals:vals + 24].hex(' ')))
                    if slopes:
                        print('    slopes @%x: %s' % (slopes, b[slopes:slopes + 24].hex(' ')))
                    if keys:
                        print('    keys %d @%x: %s' % (nk, keys, b[keys:keys + 48].hex(' ', 2)))
            a = u32(a + 0xC)
elif what == 'tex':
    n = u16(0x1A)
    for i in range(n):
        t = u32(u32(0xC) + 4 * i)
        print('texture %d @%x %s' % (i, t, b[t:t + 0x50].hex(' ', 4)))
elif what == 'mat':
    n = u16(0x1E)
    for i in range(n):
        m = u32(u32(0x14) + 4 * i)
        print('material %d @%x %s: %s' % (i, m, cstr(u32(m)), b[m:m + 0x80].hex(' ', 4)))
