"""Test: a decrypted .cia is read like a .cci (ncch.py), and the refusals say why. All bytes are made up (no game data).
  python -I tools/n3ds/test_ncch_cia.py
  python -I tools/n3ds/test_ncch_cia.py --real-cci <your decrypted .cci> --scratch <folder>
The second form (a local check only) wraps the cartridge dump's game (partition 0) into a .cia layout in the scratch folder,
reads some files and one model through both, requires identical bytes, and deletes the scratch copy."""
import argparse, hashlib, os, struct, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ncch

FAILS = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        FAILS.append(what)


def fake_ncch(code=b'CTR-P-A2AA', plain=True, payload=b'made-up file bytes'):
    """A small NCCH: header 0x200, then a RomFS holding the file 'x/hello.bin' (IVFC + level 3, as the real layout)."""

    root = struct.pack('<6I', 0, 0xFFFFFFFF, 0x18, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    name_x = 'x'.encode('utf-16le')
    dx = struct.pack('<6I', 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0xFFFFFFFF, len(name_x)) + name_x + b'\0\0'
    dmeta = root + dx
    name_f = 'hello.bin'.encode('utf-16le')
    fmeta = struct.pack('<IIQQII', 0x18, 0xFFFFFFFF, 0, len(payload), 0xFFFFFFFF, len(name_f)) + name_f
    fmeta += b'\0' * (-len(fmeta) % 4)
    dmeta += b'\0' * (-len(dmeta) % 4)
    hl = 0x28
    dmo = hl; fmo = dmo + len(dmeta); fdo = (fmo + len(fmeta) + 0x1F) & ~0x1F
    l3 = struct.pack('<10I', hl, 0, 0, dmo, len(dmeta), 0, 0, fmo, len(fmeta), fdo) + dmeta + fmeta
    l3 += b'\0' * (fdo - len(l3)) + payload
    ivfc = b'IVFC' + struct.pack('<I', 0x10000) + struct.pack('<I', 0) + b'\0' * (0x60 - 12)
    romfs = ivfc + b'\0' * (0x1000 - len(ivfc)) + l3
    romfs += b'\0' * (-len(romfs) % 0x200)
    h = bytearray(0x200)
    h[0x100:0x104] = b'NCCH'
    h[0x150:0x150 + len(code)] = code
    if plain:
        h[0x188 + 7] = 4
    struct.pack_into('<II', h, 0x1B0, 1, len(romfs) // 0x200)
    return bytes(h) + romfs, payload


def make_cci(game):
    h = bytearray(0x200)
    h[0x100:0x104] = b'NCSD'
    struct.pack_into('<II', h, 0x120, 2, len(game) // 0x200)
    return bytes(h) + b'\0' * 0x200 + game


def align(n, a=0x40):
    return (n + a - 1) & ~(a - 1)


def cia_prefix(content_size, title_high=0x00040000, encrypted=False, sig=0x10004, extra_content=0):
    """Everything in front of the contents: header, certificates, ticket, TMD (two content records when extra_content)."""
    sigsize = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[sig]
    count = 2 if extra_content else 1
    tmd = bytearray(sigsize + 0xC4 + 0x900 + count * 0x30)
    struct.pack_into('>I', tmd, 0, sig)
    struct.pack_into('>Q', tmd, sigsize + 0x4C, (title_high << 32) | 0x00123400)
    struct.pack_into('>H', tmd, sigsize + 0x9E, count)
    rec = sigsize + 0xC4 + 0x900
    struct.pack_into('>IHHQ', tmd, rec, 0, 0, 1 if encrypted else 0, content_size)
    if extra_content:
        struct.pack_into('>IHHQ', tmd, rec + 0x30, 1, 1, 0, extra_content)
    cert, tik = b'\1' * 0x100, b'\2' * 0x2C4
    h = bytearray(0x2020)
    struct.pack_into('<IHHIIIIQ', h, 0, 0x2020, 0, 0, len(cert), len(tik), len(tmd), 0, content_size + extra_content)
    h[0x20] = 0xC0 if extra_content else 0x80
    out = bytes(h) + b'\0' * (align(0x2020) - 0x2020)
    out += cert + b'\0' * (align(len(cert)) - len(cert))
    out += tik + b'\0' * (align(len(tik)) - len(tik))
    out += bytes(tmd) + b'\0' * (align(len(tmd)) - len(tmd))
    return out


def sentence(fn):
    try:
        fn()
    except SystemExit as ex:
        return str(ex.code)
    return ''


def read_file(path, name='x/hello.bin'):
    r, prod = ncch.open_rom(path)
    return r.read(name), prod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--real-cci')
    ap.add_argument('--scratch')
    a = ap.parse_args()
    d = tempfile.mkdtemp(prefix='cia_test_')
    game, payload = fake_ncch()

    def put(name, data):
        p = os.path.join(d, name)
        open(p, 'wb').write(data)
        return p

    cci = put('a.cci', make_cci(game))
    check(read_file(cci) == (payload, 'CTR-P-A2AA'), 'the made-up .cci reads')
    for sig in (0x10000, 0x10004, 0x10005):
        cia = put('a%x.cia' % sig, cia_prefix(len(game), sig=sig) + game)
        check(read_file(cia) == (payload, 'CTR-P-A2AA'), 'a .cia (signature type %x) reads the same file and code as the .cci' % sig)
    cia2 = put('two.cia', cia_prefix(len(game), extra_content=0x400) + game + b'\0' * 0x400)
    check(read_file(cia2) == (payload, 'CTR-P-A2AA'), 'a .cia with a second content after the game reads')
    enc = put('enc.cia', cia_prefix(len(game), encrypted=True) + game)
    check('encrypted' in sentence(lambda: ncch.open_rom(enc)) and 'decrypt anything itself' in sentence(lambda: ncch.open_rom(enc)),
          'a .cia marked encrypted in its TMD is refused with the plain sentence')
    g2, _ = fake_ncch(plain=False)
    enc2 = put('enc2.cia', cia_prefix(len(g2)) + g2)
    check('encrypted' in sentence(lambda: ncch.open_rom(enc2)), 'a .cia whose game is not NoCrypto is refused')
    upd = put('upd.cia', cia_prefix(len(game), title_high=0x0004000E, encrypted=True) + game)
    check('update' in sentence(lambda: ncch.open_rom(upd)), 'an update .cia is refused as the update')
    dlc = put('dlc.cia', cia_prefix(len(game), title_high=0x0004008C) + game)
    check('DLC' in sentence(lambda: ncch.open_rom(dlc)), 'a DLC .cia is refused as DLC')
    sysd = put('sys.cia', cia_prefix(len(game), title_high=0x00040010) + game)
    check('not a game' in sentence(lambda: ncch.open_rom(sysd)), 'a system title .cia is refused')
    cut = put('cut.cia', (cia_prefix(len(game)) + game)[:align(0x2020) + 0x200])
    check('cut short' in sentence(lambda: ncch.open_rom(cut)), 'a .cia cut inside its TMD is refused as cut short')
    cut2 = put('cut2.cia', (cia_prefix(len(game)) + game)[:len(cia_prefix(len(game))) + 0x80])
    check('cut short' in sentence(lambda: ncch.open_rom(cut2)), 'a .cia cut inside the game header is refused as cut short')
    if a.real_cci:
        real(a, put)
    for n in os.listdir(d):
        os.remove(os.path.join(d, n))
    os.rmdir(d)
    print('FAILED %d' % len(FAILS) if FAILS else 'all passed')
    return 1 if FAILS else 0


def real(a, put):
    """Wrap partition 0 of the real dump into a .cia layout (scratch copy, deleted at the end), compare what is read."""
    import shutil
    scratch = os.path.abspath(a.scratch)
    os.makedirs(scratch, exist_ok=True)
    cia = os.path.join(scratch, 'wrapped.cia')
    r1 = r2 = None
    try:
        with open(a.real_cci, 'rb') as f:
            h = f.read(0x200)
            po, ps = struct.unpack_from('<II', h, 0x120)
            size = ps * 0x200
            with open(cia, 'wb') as o:
                o.write(cia_prefix(size))
                f.seek(po * 0x200)
                left = size
                while left:
                    b = f.read(min(left, 16 << 20))
                    if not b:
                        break
                    o.write(b)
                    left -= len(b)
        r1, p1 = ncch.open_rom(a.real_cci)
        r2, p2 = ncch.open_rom(cia)
        check(p1 == p2, 'real: same product code (%s)' % p1)
        check(sorted(r1.files) == sorted(r2.files), 'real: the same %d files listed' % len(r1.files))
        names = sorted(r1.files)
        pick = [n for n in names if n.startswith('a/') and r1.files[n][1] < 2_000_000][:6] + names[:2] + names[-2:]
        same = all(hashlib.sha256(r1.read(n)).digest() == hashlib.sha256(r2.read(n)).digest() for n in pick)
        check(same, 'real: SHA-256 equal for %d files (%s ...)' % (len(pick), pick[0]))
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gamedetect, garc
        d1, d2 = gamedetect.detect(r1, p1), gamedetect.detect(r2, p2)
        check(d1 == d2, 'real: gamedetect finds the same archives (%s)' % d1.get('name'))
        g1, g2 = gamedetect.archive(r1, d1, 'personal'), gamedetect.archive(r2, d2, 'personal')
        check(all(g1.read(i) == g2.read(i) for i in range(0, len(g1.entries), 50)), 'real: the personal GARC reads equal')
        m1, m2 = gamedetect.archive(r1, d1, 'models'), gamedetect.archive(r2, d2, 'models')
        check(all(m1.read(i) == m2.read(i) for i in (1, 2, 10, 500)), 'real: the models GARC reads equal (4 entries)')
    finally:
        for r in (r1, r2):
            if r is not None:
                r.f.close()
        if os.path.exists(cia):
            os.remove(cia)


if __name__ == '__main__':
    sys.exit(main())
