"""Print the HSD archive inside an XD battle model (unpacked PKX) as a readable tree: joints with flags
and pose, display objects, materials, texture objects, polygon objects with vertex descriptions and
skinning envelopes. Development aid for the converter (read only; output names game data: keep under build/).

Usage: python tools/pbr_convert/dump_hsd.py <unpacked pkx or bare .dat>
"""
import struct, sys

be32 = lambda d, o: struct.unpack_from('>I', d, o)[0]
be16 = lambda d, o: struct.unpack_from('>H', d, o)[0]
bef = lambda d, o: struct.unpack_from('>f', d, o)[0]


def main():
    b = open(sys.argv[1], 'rb').read()
    size, _, psize, _, count, msize = struct.unpack_from('>6I', b, 0)
    if be32(b, 0) == len(b):
        a = b
    else:
        print('hdr', b[:0x84].hex(' ', 4))
        body = (0x84 + count * 0xD0 + 31) & ~31
        if psize:
            body += (psize + 31) & ~31
        print('body at %x size %x file %x' % (body, size, len(b)))
        a = b[body:body + size]
    fs, ds, nr, npub, next_ = struct.unpack_from('>5I', a, 0)
    print('archive', fs, ds, nr, npub, next_, a[0x14:0x20].hex())
    d = a[0x20:0x20 + ds]
    o = 0x20 + ds + nr * 4
    strs = o + (npub + next_) * 8
    scene = None
    for i in range(npub):
        off, name = struct.unpack_from('>II', a, o + i * 8)
        nm = a[strs + name:a.index(b'\0', strs + name)]
        print('public', hex(off), nm)
        if nm == b'scene_data':
            scene = off
        if nm == b'bound_box':
            print('  bound_box head', d[off:off + 0x40].hex(' ', 4))
    print('scene', [hex(be32(d, scene + i * 4)) for i in range(4)])
    msl = be32(d, scene)
    print('modelset list', [hex(be32(d, msl + i * 4)) for i in range(3)])
    ms = be32(d, msl)
    print('modelset', [hex(be32(d, ms + i * 4)) for i in range(4)])
    idx = [0]

    def cs(o):
        return d[o:d.index(b'\0', o)] if o else None

    def jobj(o, depth):
        while o:
            ind = '  ' * depth
            fl = be32(d, o + 4)
            r = struct.unpack_from('>9f', d, o + 0x14)
            print('%sj%d @%x name=%s flags=%08X dobj=%x mtx=%x robj=%x R=(%.3f %.3f %.3f) S=(%.2f %.2f %.2f) T=(%.2f %.2f %.2f)' % (
                ind, idx[0], o, cs(be32(d, o)), fl, be32(d, o + 0x10), be32(d, o + 0x38), be32(d, o + 0x3c), *r))
            idx[0] += 1
            dob = be32(d, o + 0x10)
            if not fl & 0x4020:
                while dob:
                    mo = be32(d, dob + 8)
                    po = be32(d, dob + 12)
                    print('%s   dobj @%x name=%s mobj=%x' % (ind, dob, cs(be32(d, dob)), mo))
                    if mo:
                        print('%s     mobj rm=%08X tex=%x mat=%x render=%x pe=%x' % (
                            ind, be32(d, mo + 4), be32(d, mo + 8), be32(d, mo + 12), be32(d, mo + 16), be32(d, mo + 20)))
                        m = be32(d, mo + 12)
                        print('%s     mat amb=%s dif=%s spec=%s alpha=%g shin=%g' % (
                            ind, d[m:m + 4].hex(), d[m + 4:m + 8].hex(), d[m + 8:m + 12].hex(), bef(d, m + 12), bef(d, m + 16)))
                        pe = be32(d, mo + 20)
                        if pe:
                            print('%s     pe %s' % (ind, d[pe:pe + 12].hex(' ')))
                        t = be32(d, mo + 8)
                        while t:
                            img = be32(d, t + 0x4c)
                            tl = be32(d, t + 0x50)
                            print('%s     tobj @%x id=%d src=%d rot=%s scale=%s tr=%s wrap=%d,%d rep=%d,%d flags=%08X blend=%g mag=%d img=%x tlut=%x lod=%x tev=%x' % (
                                ind, t, be32(d, t + 8), be32(d, t + 12), struct.unpack_from('>3f', d, t + 0x10),
                                struct.unpack_from('>3f', d, t + 0x1c), struct.unpack_from('>3f', d, t + 0x28), be32(d, t + 0x34),
                                be32(d, t + 0x38), d[t + 0x3c], d[t + 0x3d], be32(d, t + 0x40), bef(d, t + 0x44), be32(d, t + 0x48),
                                img, tl, be32(d, t + 0x54), be32(d, t + 0x58)))
                            if img:
                                print('%s       image data=%x %dx%d fmt=%d mip=%d lod %g %g' % (
                                    ind, be32(d, img), be16(d, img + 4), be16(d, img + 6), be32(d, img + 8), be32(d, img + 12),
                                    bef(d, img + 16), bef(d, img + 20)))
                            if tl:
                                print('%s       tlut data=%x fmt=%d name=%x n=%d' % (
                                    ind, be32(d, tl), be32(d, tl + 4), be32(d, tl + 8), be16(d, tl + 12)))
                            t = be32(d, t + 4)
                    while po:
                        fl2 = be16(d, po + 0xc)
                        nd = be16(d, po + 0xe)
                        vd = be32(d, po + 8)
                        lay = []
                        while be32(d, vd) != 0xff:
                            lay.append((be32(d, vd), be32(d, vd + 4), be32(d, vd + 8), be32(d, vd + 12), d[vd + 16],
                                        be16(d, vd + 18), hex(be32(d, vd + 20))))
                            vd += 0x18
                        env = be32(d, po + 0x14)
                        envs = []
                        if (fl2 >> 12) & 3 == 2:
                            k = 0
                            while be32(d, env + k * 4):
                                e = be32(d, env + k * 4)
                                ws = []
                                while be32(d, e):
                                    ws.append((hex(be32(d, e)), round(bef(d, e + 4), 3)))
                                    e += 8
                                envs.append(ws)
                                k += 1
                        print('%s     pobj @%x name=%s flags=%04X ndisp=%d disp=%x u=%x' % (
                            ind, po, cs(be32(d, po)), fl2, nd, be32(d, po + 0x10), env))
                        for l in lay:
                            print('%s        vtx attr=%d type=%d cnt=%d comp=%d frac=%d stride=%d ptr=%s' % ((ind,) + l))
                        for e in envs:
                            print('%s        env %s' % (ind, e))
                        po = be32(d, po + 4)
                    dob = be32(d, dob + 4)
            mt = be32(d, o + 0x38)
            if mt and depth < 3:
                print(ind, '   invbind', ['%.3f' % x for x in struct.unpack_from('>12f', d, mt)])
            jobj(be32(d, o + 8), depth + 1)
            o = be32(d, o + 0xc)

    jobj(be32(d, ms), 0)


if __name__ == '__main__':
    main()
