"""Read a 3DS GARC archive (Gen 6/7 games) from a file object + offset. Read only."""
import struct

class Garc:
    def __init__(self, f, off, size):
        self.f, self.off = f, off
        f.seek(off)
        h = f.read(0x1C)
        assert h[:4] == b'CRAG', 'not GARC'
        hl, bom, ver, nchunk, dataoff, flen, maxlen = struct.unpack_from('<IHHIIII', h, 4)
        self.data_off = dataoff
        f.seek(off + hl)
        o = f.read(0x10)
        assert o[:4] == b'OTAF'
        osz, nfiles = struct.unpack_from('<IH', o, 4)
        f.seek(off + hl + osz)
        f.seek(off + hl + osz)
        b = f.read(12)
        assert b[:4] == b'BTAF', b
        bsz, n2 = struct.unpack_from('<II', b, 4)
        body = f.read(bsz - 12)
        self.entries = []
        p = 0
        for i in range(n2):
            mask = struct.unpack_from('<I', body, p); p += 4
            subs = []
            for bit in range(32):
                if mask[0] >> bit & 1:
                    s, e, l = struct.unpack_from('<III', body, p); p += 12
                    subs.append((s, l))
                else:
                    subs.append(None)
            self.entries.append(subs)

    def read(self, i, sub=0):
        s = self.entries[i][sub]
        self.f.seek(self.off + self.data_off + s[0])
        return self.f.read(s[1])

    def nsub(self, i):
        return sum(1 for s in self.entries[i] if s)


def lz11(b):
    """Nintendo LZ11 (type byte 0x11) decompression."""
    assert b[0] == 0x11
    size = b[1] | b[2] << 8 | b[3] << 16
    p = 4
    if size == 0:
        size = int.from_bytes(b[4:8], 'little'); p = 8
    out = bytearray()
    while len(out) < size:
        flags = b[p]; p += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if not (flags >> bit & 1):
                out.append(b[p]); p += 1
            else:
                b0 = b[p]; ind = b0 >> 4
                if ind == 0:
                    b1, b2 = b[p + 1], b[p + 2]; p += 3
                    ln = ((b0 & 15) << 4 | b1 >> 4) + 0x11
                    disp = ((b1 & 15) << 8 | b2) + 1
                elif ind == 1:
                    b1, b2, b3 = b[p + 1], b[p + 2], b[p + 3]; p += 4
                    ln = ((b0 & 15) << 12 | b1 << 4 | b2 >> 4) + 0x111
                    disp = ((b2 & 15) << 8 | b3) + 1
                else:
                    b1 = b[p + 1]; p += 2
                    ln = ind + 1
                    disp = ((b0 & 15) << 8 | b1) + 1
                for _ in range(ln):
                    out.append(out[-disp])
    return bytes(out[:size])

def maybe(b):
    if b and b[0] == 0x11:
        try:
            return lz11(b)
        except (IndexError, AssertionError):
            return b
    return b
