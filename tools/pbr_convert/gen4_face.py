"""Gen 4 face pictures: Pokemon Battle Revolution's menu pictures -> XD's face textures (docs/GEN4.md, "Faces").

  python tools/pbr_convert/gen4_face.py table  --pbr <PBR disc>                 species -> picture table, printed
  python tools/pbr_convert/gen4_face.py convert <out folder> --pbr <PBR disc> --xd <XD disc> [--only 448,445]
        <out>/faces/<dex>_<name>.face (XD texture file), faces.tsv, contact sheet faces.png
  python tools/pbr_convert/gen4_face.py xdpic <out folder> --xd <XD disc> --species 25   (an XD face, as a PNG, to compare)

How a Gen 4 species finds its picture (decoded, checked on eight species by looking at the pictures):
  common.fsys member 21 ("species -> picture row", 495 x 12 bytes, row = national number): u16 at +4 = row of
  member 0 ("picture rows", 562 x 16 bytes): four u16 (id, 0x0C00) pairs = {menu_face id, menu_pokemon id, menu_face id of
  the female, menu_pokemon id of the female}; the ids are FSYS member ids of menu_face.fsys / menu_pokemon.fsys.
Pictures: menu_face members are GX C8 textures (256 colours, RGB5A3 palette), 48 x 96 = two round 48 x 48 faces
  stacked (normal on top, shiny below); 96 x 96 = two of those side by side (male | female).
XD: poke_face.fsys member "faceNNN" = 42 x 84 C8 texture, the same stacking; poke_face (REL, 838 x 8 bytes): u8 flags
  (0x40 normal, 0xC0 = the shiny half), 3 zero bytes, u32 (fsys id << 16 | 0x1200); cgData.spriteId = row.
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import noddisc

XD_W, XD_H = 42, 84
DATA_OFF = 0x80


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def rgb5a3(v):
    if v & 0x8000:
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255)
    a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17, a * 255 // 7)


def to_rgb5a3(rgba):
    r, g, b, a = rgba
    if a >= 0xE0:
        return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
    return (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)


def decode_c8(d):
    """GX C8 texture file (header 0x80: w, h at 0; pixel data offset at +0x28; palette offset at +0x48) -> PIL RGBA image"""
    from PIL import Image
    w, h = struct.unpack_from('>HH', d, 0)
    off = struct.unpack_from('>I', d, 0x28)[0]
    poff = struct.unpack_from('>I', d, 0x48)[0]
    pal = [rgb5a3(be16(d, poff + 2 * i)) for i in range(256)]
    pw, ph = (w + 7) // 8 * 8, (h + 3) // 4 * 4
    im = Image.new('RGBA', (w, h))
    px = im.load()
    p = off
    for ty in range(0, ph, 4):
        for tx in range(0, pw, 8):
            for y in range(4):
                for x in range(8):
                    idx = d[p]
                    p += 1
                    if tx + x < w and ty + y < h:
                        px[tx + x, ty + y] = pal[idx]
    return im


def encode_c8(im, template):
    """PIL RGBA (w x h, the template's size) -> XD texture file bytes (the template's header, new pixels and palette)"""
    from PIL import Image
    w, h = struct.unpack_from('>HH', template, 0)
    assert im.size == (w, h)
    off = struct.unpack_from('>I', template, 0x28)[0]
    poff = struct.unpack_from('>I', template, 0x48)[0]
    q = im.quantize(colors=255, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    pal = q.getpalette()[:255 * 3]
    rgba = im.load()
    qp = q.load()
    colours = [(0, 0, 0, 0)] + [tuple(pal[3 * i:3 * i + 3]) + (255,) for i in range(255)]
    pw, ph = (w + 7) // 8 * 8, (h + 3) // 4 * 4
    out = bytearray(template)
    p = off
    for ty in range(0, ph, 4):
        for tx in range(0, pw, 8):
            for y in range(4):
                for x in range(8):
                    if tx + x < w and ty + y < h:
                        a = rgba[tx + x, ty + y][3]
                        if a < 16:
                            idx = 0
                        else:
                            idx = qp[tx + x, ty + y] + 1
                            c = colours[idx]


                    else:
                        idx = 0
                    out[p] = idx
                    p += 1
    for i, c in enumerate(colours):
        struct.pack_into('>H', out, poff + 2 * i, to_rgb5a3(c))
    return bytes(out)


class PBRFaces:
    def __init__(self, pbr_disc):
        self.disc = noddisc.Disc(pbr_disc)
        common = self.disc.file('common.fsys')
        es = noddisc.fsys_entries(common)
        mem = lambda i: noddisc.extract(common, [e for e in es if e.index == i][0])
        self.m0, self.m21 = mem(0), mem(21)
        self.face_data = self.disc.file('menu_face.fsys')
        self.face = {e.id: e for e in noddisc.fsys_entries(self.face_data)}

    def row(self, dex):
        """-> (menu_face id male, menu_pokemon id male, menu_face id female, menu_pokemon id female)"""
        r = struct.unpack_from('>6H', self.m21, 0x30 + dex * 12)
        assert r[1] == dex, 'member 21 row %d is not this species' % dex
        w = struct.unpack_from('>8H', self.m0, 0x30 + r[2] * 16)
        return w[0], w[2], w[4], w[6]

    def picture(self, dex, female=False):
        """-> (normal, shiny) as PIL images of the 48 x 48 round face"""
        fid = self.row(dex)[2 if female else 0]
        im = decode_c8(noddisc.extract(self.face_data, self.face[fid]))
        w, h = im.size
        if w == 96:
            im = im.crop((48 if female else 0, 0, 48 if female else 48, h))
        return im.crop((0, 0, 48, 48)), im.crop((0, 48, 48, 96))


def make_xd_face(normal, shiny, template):
    from PIL import Image
    sheet = Image.new('RGBA', (XD_W, XD_H), (0, 0, 0, 0))
    for k, im in enumerate((normal, shiny)):
        sheet.paste(im.resize((XD_W, XD_W), Image.Resampling.LANCZOS), (0, k * XD_W))
    return encode_c8(sheet, template)


def xd_template(xd_disc):
    D = noddisc.Disc(xd_disc)
    d = D.file('poke_face.fsys')
    es = noddisc.fsys_entries(d)
    e = [x for x in es if x.name == 'face025'][0]
    return D, d, es, noddisc.extract(d, e)


def convert(a):
    from PIL import Image
    from gen4_data import PBR
    pbr = PBR(a.pbr)
    faces = PBRFaces(a.pbr)
    _D, _d, _es, tpl = xd_template(a.xd)
    out = os.path.join(a.out, 'faces')
    os.makedirs(out, exist_ok=True)
    only = set(int(x) for x in a.only.split(',')) if a.only else None
    rows, tiles = [], []
    for dex in range(387, 494):
        if only and dex not in only:
            continue
        n, s = faces.picture(dex)
        data = make_xd_face(n, s, tpl)
        name = pbr.species_name(dex)
        fn = '%03d_%s.face' % (dex, ''.join(c for c in name if c.isalnum()))
        open(os.path.join(out, fn), 'wb').write(data)
        rows.append((dex, name, fn, len(data), faces.row(dex)[0]))
        tiles.append(decode_c8(data))
    with open(os.path.join(out, 'faces.tsv'), 'w', encoding='utf-8') as f:
        f.write('dex\tname\tfile\tbytes\tpbr_menu_face_id\n')
        for r in rows:
            f.write('\t'.join(str(x) for x in r) + '\n')
    cols = 14
    sheet = Image.new('RGBA', (cols * XD_W, ((len(tiles) + cols - 1) // cols) * XD_H), (90, 90, 90, 255))
    for k, t in enumerate(tiles):
        sheet.paste(t, ((k % cols) * XD_W, (k // cols) * XD_H), t)
    sheet.save(os.path.join(out, 'faces.png'))
    print('%d faces -> %s' % (len(rows), out))
    return 0


def table(a):
    from gen4_data import PBR
    pbr = PBR(a.pbr)
    faces = PBRFaces(a.pbr)
    for dex in range(1, 494):
        r = faces.row(dex)
        print(dex, pbr.species_name(dex), r)
    return 0


def xdpic(a):
    D, d, es, _t = xd_template(a.xd)
    e = [x for x in es if x.name == 'face%03d' % a.species][0]
    decode_c8(noddisc.extract(d, e)).save(os.path.join(a.out, 'xd_face%03d.png' % a.species))
    return 0


TEST_PLAN = [(133, 448), (135, 445), (134, 468)]


def test(a):
    """The party list of a library save (Eevee, Jolteon, Vaporeon...) with three faces replaced by Gen 4 faces, and
    without (a mod made of the three converted textures: file replacement in poke_face.fsys)."""
    import argparse as ap_
    import json
    import shutil
    sys.path.insert(0, os.path.dirname(HERE))
    import menu_sweep
    import gen4_data
    D, d, es, tpl = xd_template(a.xd)
    x = gen4_data.XD(a.xd, a.out, a.exe_tool)
    faces = PBRFaces(a.pbr)
    rel = x.rel
    sig = bytes([0x40, 0, 0, 0, 0x0d, 0x07, 0x12, 0])
    i = rel.find(sig)
    base = i - 8
    assert rel[base + 25 * 8 + 4:base + 25 * 8 + 6] == bytes([0x0d, 0x21]), 'poke_face table not found'
    byid = {e.id: e for e in es}
    work = os.path.abspath(a.out)
    results = {}
    for variant in ('plain', 'gen4'):
        run = os.path.join(work, 'face_' + variant)
        settings = os.path.join(run, 'settings')
        shutil.rmtree(settings, ignore_errors=True)
        os.makedirs(settings)
        open(os.path.join(settings, 'settings.ini'), 'w').write('format = 1\npreset = original\n')
        if variant == 'gen4':
            mod = os.path.join(settings, 'mods', 'face-test')
            dst = os.path.join(mod, 'fsys', 'poke_face.fsys')
            os.makedirs(dst)
            json.dump({'id': 'face-test', 'name': 'Face test', 'version': '0', 'game': 'GXXE01', 'applies': 'any_time'},
                      open(os.path.join(mod, 'mod.json'), 'w'))
            for sp, dex in TEST_PLAN:
                rec = x.rec(sp)
                sprite = be16(rec, 0x116)
                tid = struct.unpack_from('>I', rel, base + sprite * 8 + 4)[0] >> 16
                n, s = faces.picture(dex)
                open(os.path.join(dst, '#%d' % byid[tid].index), 'wb').write(make_xd_face(n, s, tpl))
                print('species %d: face row %d -> texture id %#x (%s) <- Gen 4 dex %d' % (sp, sprite, tid, byid[tid].name, dex))
            open(os.path.join(settings, 'mods.ini'), 'w').write('profile = default\n\n[profile default]\nface-test = on\n')
        os.environ['XD_SETTINGS_DIR'] = settings
        args = ap_.Namespace(turbo=16, port=a.exe, disc=a.xd, timeout=a.timeout)
        out = os.path.join(run, 'out')
        os.makedirs(out, exist_ok=True)
        r = menu_sweep.port_run(args, out, 'face ' + variant, 'menusweep_kit_390', '5500:y,5600:a,5750:a,5850:a',
                                [5720, 5830, 5980], 6050)
        print(variant, r['result'], r['code'], r['secs'], 's')
        results[variant] = r
    shots = []
    from PIL import Image
    for variant in ('plain', 'gen4'):
        for f in (5720, 5980):
            p = os.path.join(work, 'face_' + variant, 'out', 'shots', 'f%d.png' % f)
            if os.path.exists(p):
                shots.append(Image.open(p).convert('RGB'))
    if shots:
        w, h = shots[0].size
        sheet = Image.new('RGB', (2 * w, ((len(shots) + 1) // 2) * h))
        for k, im in enumerate(shots):
            sheet.paste(im, ((k % 2) * w, (k // 2) * h))
        sheet.save(os.path.join(work, 'face_test.png'))
        print('sheet', os.path.join(work, 'face_test.png'))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['table', 'convert', 'xdpic', 'test'])
    ap.add_argument('--exe', default='')
    ap.add_argument('--exe-tool', dest='exe_tool', default='')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('out', nargs='?', default='.')
    ap.add_argument('--pbr', default='')
    ap.add_argument('--xd', default='')
    ap.add_argument('--only', default='')
    ap.add_argument('--species', type=int, default=25)
    a = ap.parse_args()
    return {'table': table, 'convert': convert, 'xdpic': xdpic, 'test': test}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main() or 0)
