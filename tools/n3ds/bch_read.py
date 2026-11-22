"""Reader for CTR BCH files (Pokemon X/Y models and motions): header, relocation, and the H3D content dictionaries.
Written from the open documentation of the format (SPICA's H3D classes, read as documentation only; no code copied)
and checked on the player's own files. Pure Python + numpy. Read only."""
import struct
import numpy as np


class Bch:
    def __init__(self, data):
        d = bytearray(data)
        assert d[:4] == b'BCH\0', 'not a BCH'
        self.bc, self.fc, self.cv = d[4], d[5], struct.unpack_from('<H', d, 6)[0]
        p = 8
        names = ['contents', 'strings', 'commands', 'raw']
        if self.bc >= 0x21:
            names.append('rawext')
        names.append('reloc')
        self.addr = {}
        for n in names:
            self.addr[n] = struct.unpack_from('<i', d, p)[0]
            p += 4
        self.len = {}
        for n in names:
            self.len[n] = struct.unpack_from('<i', d, p)[0]
            p += 4
        self.d = d
        self.relocate()

    def section_addr(self, sec):
        a = self.addr


        if sec == 0: return a['contents']
        if sec == 1: return a['strings']
        if sec in (2, 3): return a['commands']
        if sec in (4, 5, 6, 8): return a['raw']
        if sec == 7: return a['raw'] | (1 << 31)
        if sec in (9, 10, 11, 13): return a.get('rawext', 0)
        if sec == 12: return a.get('rawext', 0) | (1 << 31)
        return 0

    def relocate(self):
        d = self.d
        base = self.addr['reloc']
        for off in range(0, self.len['reloc'], 4):
            v = struct.unpack_from('<I', d, base + off)[0]
            ptr = v & 0x1ffffff
            tgt = (v >> 25) & 0xf
            src = v >> 29
            if self.bc > 7 and self.bc < 0x21 and tgt >= 6:
                tgt -= 1
            elif self.bc < 7 and tgt >= 3:
                tgt += 1
            if tgt != 1:
                ptr <<= 2
            at = (self.section_addr(src) & 0x7fffffff) + ptr
            val = struct.unpack_from('<I', d, at)[0]
            struct.pack_into('<I', d, at, (val + self.section_addr(tgt)) & 0xffffffff)


    def u32(self, p): return struct.unpack_from('<I', self.d, p)[0]
    def i32(self, p): return struct.unpack_from('<i', self.d, p)[0]
    def u16(self, p): return struct.unpack_from('<H', self.d, p)[0]
    def i16(self, p): return struct.unpack_from('<h', self.d, p)[0]
    def f32(self, p): return struct.unpack_from('<f', self.d, p)[0]
    def fl(self, p, n): return struct.unpack_from('<%df' % n, self.d, p)

    def cstr(self, p):
        if not p:
            return ''
        e = self.d.index(b'\0', p)
        return bytes(self.d[p:e]).decode('latin1')

    def dict_names(self, pt):
        """Patricia tree node array at pt: names in order (the root node is skipped)."""
        names = []
        maxi = 0
        i = 0
        while i <= maxi:
            ref, l, r, nm = struct.unpack_from('<IHHI', self.d, pt + i * 12)
            maxi = max(maxi, l, r)
            if i > 0:
                names.append(self.cstr(nm))
            i += 1
        return names

    def contents_dicts(self):
        """The 15 content dictionaries: list of (values pointer, count, names)."""
        out = []
        c = self.addr['contents']
        for k in range(15):
            vp, cnt, tp = struct.unpack_from('<III', self.d, c + k * 12)
            out.append((vp, cnt, self.dict_names(tp) if tp and cnt else []))
        return out
