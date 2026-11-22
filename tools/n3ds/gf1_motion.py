"""Gen 6 (X/Y) battle motions: the pack of key-framed skeletal animations in the Pokemon's animation file.
Layout from the open documentation of the format (SPICA's GF1Motion reader, read as documentation only), checked on
the player's own files. Output matches gf_read.parse_motion (bones: name -> dict(axis, ch[9])) so the rest of the
pipeline treats both generations alike. Channels: scale xyz, rotation xyz (Euler, radians), translation xyz."""
import math
import struct


class _R:
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

    def u24(self):
        v = self.b[self.p] | self.b[self.p + 1] << 8 | self.b[self.p + 2] << 16
        self.p += 3
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.b, self.p)[0]
        self.p += 4
        return v

    def cstr(self):
        e = self.b.index(b'\0', self.p)
        s = bytes(self.b[self.p:e]).decode('latin1')
        self.p = e + 1
        return s

    def align(self, n):
        self.p = (self.p + n - 1) // n * n


def read_skeleton(r):
    n = r.u8()
    r.u8()
    bones = [dict(name='Origin', parent=-1, flags=0)]
    for _ in range(1, n):
        bones.append(dict(parent=r.u8(), flags=r.u8(), childs=r.u8()))
    for i in range(1, n):
        bones[i]['name'] = r.cstr()
    r.align(4)
    for i in range(n):
        bones[i]['t'] = struct.unpack_from('<3f', r.b, r.p)
        r.p += 12
        bones[i]['q'] = struct.unpack_from('<4f', r.b, r.p)
        r.p += 16
    return bones


def read_motion(r, skel):
    octn = r.u16()
    frames = r.u16()
    octals = []
    cur = 0
    kfn = 0
    for i in range(octn):
        if (i & 7) == 0:
            cur = r.u24()
        octals.append(cur & 7)
        cur >>= 3
        if octals[-1] > 5:
            kfn += 1
    f16 = frames > 0xff
    if f16 and (r.p & 1):
        r.u8()
    kfs = []
    for i in range(kfn):
        cnt = r.u16() if f16 else r.u8()
        lst = [0]
        for j in range(cnt):
            lst.append(r.u16() if f16 else r.u8())
        lst.append(frames)
        kfs.append(lst)
    r.align(4)
    bones = {}
    cur_b = None
    kfl = 0
    oi = 2
    ei = 0
    old = -1
    while oi < octn:
        o = octals[oi]
        oi += 1
        if o != 1:
            bi = ei // 9
            if bi != old:
                cur_b = [[] for _ in range(9)]
                bones[skel[bi]['name']] = dict(axis=False, ch=cur_b, world=bool(skel[bi]['flags'] & 8))
                old = bi
            el = ei % 9

            slot = {0: 6, 1: 7, 2: 8, 3: 3, 4: 4, 5: 5, 6: 0, 7: 1, 8: 2}[el]
            L = cur_b[slot]
            if o == 0:
                L.append((0, 0.0, None))
            elif o == 2:
                L.append((0, math.pi * 0.5, None))
            elif o == 3:
                L.append((0, math.pi, None))
            elif o == 4:
                L.append((0, -math.pi * 0.5, None))
            elif o == 5:
                L.append((0, r.f32(), None))
            elif o == 6:
                for f in kfs[kfl]:
                    L.append((f, r.f32(), None))
                kfl += 1
            elif o == 7:
                for f in kfs[kfl]:
                    v = r.f32()
                    s = r.f32()
                    L.append((f, v, s))
                kfl += 1
            ei += 1
        else:
            ei += 3
    return dict(frames=frames, loop=False, bones=bones, vis={}, mats=[])


def parse_pack(b):
    """The motion pack (first entry of the animation file) -> {table index: motion}; table index 1 is the first
    animation (slot 0 = idle 'waitA')."""
    cnt = struct.unpack_from('<I', b, 0)[0]
    sk_addr = struct.unpack_from('<I', b, 4)[0]
    skel = read_skeleton(_R(b, sk_addr))
    out = {}
    for i in range(1, cnt):
        a = struct.unpack_from('<I', b, 4 + 4 * i)[0]
        if a == 0:
            continue
        try:
            out[i] = read_motion(_R(b, a), skel)
        except Exception as ex:
            out[i] = None
    return out, skel
