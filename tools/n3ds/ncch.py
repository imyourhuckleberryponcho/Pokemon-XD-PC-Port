"""Read an unencrypted (decrypted) 3DS cartridge dump: NCSD/CCI -> NCCH -> RomFS. Pure Python, read only.
Usage: python -I ncch.py <game.cci> [list [prefix]]   (the game's data is never copied by this tool)"""
import struct, sys

class RomFS:
    def __init__(self, f, base):
        self.f = f
        f.seek(base)
        h = f.read(0x60)
        assert h[:4] == b'IVFC', 'not IVFC'

        mh = struct.unpack_from('<I', h, 8)[0]
        l3off = struct.unpack_from('<Q', h, 0x3C)[0]


        start = (0x60 + mh + 0xFFF) & ~0xFFF
        self.l3 = base + start
        f.seek(self.l3)
        hd = struct.unpack('<10I', f.read(0x28))
        (hl, dho, dhs, dmo, dms, fho, fhs, fmo, fms, fdo) = hd
        self.fdo = fdo
        f.seek(self.l3 + dmo); self.dmeta = f.read(dms)
        f.seek(self.l3 + fmo); self.fmeta = f.read(fms)
        self.files = {}
        self._walk(0, '')

    @staticmethod
    def _name(b, off, n):
        return b[off:off + n * 2].decode('utf-16le')

    def _walk(self, doff, path):
        d = self.dmeta
        parent, sib, child, fchild, nxt, nl = struct.unpack_from('<6I', d, doff)
        name = self._name(d, doff + 0x18, nl // 2)
        p = path + (name + '/' if name else '')
        fo = fchild
        while fo != 0xFFFFFFFF:
            par, sb, fdo, fds, nx, nl2 = struct.unpack_from('<IIQQII', self.fmeta, fo)
            fn = self._name(self.fmeta, fo + 0x20, nl2 // 2)
            self.files[p + fn] = (fdo, fds)
            fo = sb
        co = child
        while co != 0xFFFFFFFF:
            self._walk(co, p)
            co = struct.unpack_from('<I', d, co + 4)[0]

    def read(self, path):
        o, s = self.files[path]
        self.f.seek(self.l3 + self.fdo + o)
        return self.f.read(s)

def open_rom(path, part=0):
    f = open(path, 'rb')
    h = f.read(0x200)
    assert h[0x100:0x104] == b'NCSD'
    po, ps = struct.unpack_from('<II', h, 0x120 + part * 8)
    base = po * 0x200
    f.seek(base)
    n = f.read(0x200)
    assert n[0x100:0x104] == b'NCCH'
    flags = n[0x188:0x190]
    if not (flags[7] & 4):
        raise SystemExit('this dump is encrypted; only decrypted dumps are supported (the tool does not decrypt)')
    ro, rs = struct.unpack_from('<II', n, 0x1B0)
    prod = n[0x150:0x160].rstrip(b'\0').decode()
    return RomFS(f, base + ro * 0x200), prod

if __name__ == '__main__':
    r, prod = open_rom(sys.argv[1])
    print(prod, len(r.files), 'files')
    pre = sys.argv[3] if len(sys.argv) > 3 else ''
    for k, (o, s) in sorted(r.files.items()):
        if k.startswith(pre):
            print('%10d %s' % (s, k))
