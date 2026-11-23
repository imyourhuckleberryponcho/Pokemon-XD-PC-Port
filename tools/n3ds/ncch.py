"""Read an unencrypted (decrypted) 3DS game: a cartridge dump (NCSD/CCI) or a digital copy (.cia) -> NCCH -> RomFS.
Pure Python, read only. A .cia is read in place: its header and TMD say where content 0 (the game's NCCH, the same
NCCH a .cci holds as partition 0) starts, and everything else is an offset into the same file (nothing is copied).
Only DECRYPTED copies are read; the tool never decrypts, and never asks for or contains any console keys.
Usage: python -I ncch.py <game.cci|game.cia> [list [prefix]]   (the game's data is never copied by this tool)"""
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

ENCRYPTED_CIA = ('this .cia is encrypted. The converter only reads decrypted copies and does not decrypt anything itself: '
                 're-export the game decrypted with your own dumping tool.')
UPDATE_CIA = "this .cia is the game's update, not the game itself. Choose the game's own .cia."
DLC_CIA = "this .cia is the game's downloadable content (DLC), not the game itself. Choose the game's own .cia."
SHORT_CIA = 'this .cia looks cut short or damaged: the game data is not where its header says.'


def _align(n, a=0x40):
    return (n + a - 1) & ~(a - 1)


def cia_content0(f, size):
    """(offset of content 0 = the game's NCCH inside the .cia, title id) or SystemExit(plain sentence).
    Layout: header 0x2020 | cert chain | ticket | TMD | meta | contents, each part padded to 0x40."""
    f.seek(0)
    h = f.read(0x2020)
    if len(h) < 0x2020:
        raise SystemExit(SHORT_CIA)
    hdr, typ, ver, cert, tik, tmd, meta, csize = struct.unpack_from('<IHHIIIIQ', h, 0)
    if hdr != 0x2020 or typ != 0:
        raise SystemExit('this is not a 3DS game file the converter can read (a .3ds, .cci or decrypted .cia).')
    tmd_off = _align(hdr) + _align(cert) + _align(tik)
    cont_off = tmd_off + _align(tmd) + _align(meta)
    f.seek(tmd_off)
    t = f.read(min(tmd, 0x10000))
    if len(t) < 4 or tmd < 0x100:
        raise SystemExit(SHORT_CIA)
    sigtype = struct.unpack_from('>I', t, 0)[0]
    sig = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(sigtype)
    if sig is None or len(t) < sig + 0xC4 + 0x900:
        raise SystemExit(SHORT_CIA)
    tid = struct.unpack_from('>Q', t, sig + 0x4C)[0]
    hi = tid >> 32
    if hi == 0x0004000E:
        raise SystemExit(UPDATE_CIA)
    if hi == 0x0004008C:
        raise SystemExit(DLC_CIA)
    if hi != 0x00040000:
        raise SystemExit('this .cia is not a game (its title id is %016X).' % tid)
    count = struct.unpack_from('>H', t, sig + 0x9E)[0]
    rec = sig + 0xC4 + 0x900
    if count == 0 or len(t) < rec + count * 0x30:
        raise SystemExit(SHORT_CIA)
    present = h[0x20:0x2020]
    off = cont_off
    for i in range(count):
        cid, cidx, ctype, csz = struct.unpack_from('>IHHQ', t, rec + i * 0x30)
        if not present[cidx >> 3] & (0x80 >> (cidx & 7)):
            continue
        if cidx == 0:
            if ctype & 1:
                raise SystemExit(ENCRYPTED_CIA)
            if off + 0x200 > size:
                raise SystemExit(SHORT_CIA)
            return off, tid
        off += csz
    raise SystemExit(SHORT_CIA)


def open_rom(path, part=0):
    f = open(path, 'rb')
    h = f.read(0x200)
    if h[0x100:0x104] == b'NCSD':
        po, ps = struct.unpack_from('<II', h, 0x120 + part * 8)
        base = po * 0x200
    else:
        assert len(h) >= 8 and struct.unpack_from('<I', h, 0)[0] == 0x2020, 'not NCSD'
        f.seek(0, 2)
        base, _tid = cia_content0(f, f.tell())
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
