"""Gen 4 PC box pictures: Pokemon Battle Revolution's menu pictures -> XD's little animated "ant" pictures (docs/GEN4.md, section 14).

The PC box grid draws each Pokemon as an "ant" resource of poke_dance.fsys (cgData.pokebodyId, the key = member id << 16 | 0x3200):
  0x00 u32 animation count, 0x04 u32 frame table offset (0x28), 0x08 u32 texture file size, 0x0C u32 texture offset (0xA0),
  0x10 u16 per animation: its script's offset; the scripts (flag byte: 1 = frame follows, 2 = wait follows (x 1.8 / 60 s),
  4 = x, y offset follows, 8 = jump back by the next byte); the frame table: per frame u16 part count + u16 offset, each part
  0x1C bytes (u, v, w, h as u16, x, y as s16: where the rectangle of the texture is drawn); then the texture file (header 0x80:
  C8 = 256 colours, pixels at 0x80, palette at 0x3080, 64 x 192 = three 64 x 64 frames stacked).
The same layout is written here: the file of XD's own picture "unknown_a" as the template (its scripts and frame table are the
same for every species), with the texture replaced. The picture is Battle Revolution's menu_pokemon picture of the species
(54 x 108 = the normal 54 x 54 on top, the shiny one below; the same rendered-from-the-model look as XD's own, outline included)
drawn at its own size in the three frames, moved 2 pixels up from frame to frame: the template's walk script (frames 0, 1, 2, 1)
then bobs it, and the idle script (frame 1) shows it still.
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import noddisc
import gen4_face as F

TEMPLATE_NAME = 'unknown_a'
TEX_OFF = 0xA0
CELL = 64
PIC = 54
FIRST_ID = 0x2200
FRAME_TOPS = (10, 8, 6)


def icon_id(k, shiny):
    return FIRST_ID + 2 * k + (1 if shiny else 0)


def icon_key(k, shiny):
    """cgData.pokebodyId: the member's id << 16 | type 25 << 9"""
    return (icon_id(k, shiny) << 16) | 0x3200


def make_ant(pic, template):
    """pic: PIL RGBA PIC x PIC; template: an XD ant file -> a new ant file (same scripts and frames, this picture's texture)"""
    from PIL import Image
    tex = template[TEX_OFF:]
    sheet = Image.new('RGBA', (CELL, 3 * CELL), (0, 0, 0, 0))
    x = (CELL - PIC) // 2
    for f, top in enumerate(FRAME_TOPS):
        sheet.paste(pic, (x, f * CELL + top))
    return template[:TEX_OFF] + F.encode_c8(sheet, tex)


class Icons:
    def __init__(self, pbr_disc, xd_disc):
        self.faces = F.PBRFaces(pbr_disc)
        d = self.faces.disc
        self.pic_data = d.file('menu_pokemon.fsys')
        self.pic = {e.id: e for e in noddisc.fsys_entries(self.pic_data)}
        D = noddisc.Disc(xd_disc)
        self.dance_name = [k for k in D.files if k.lower() == 'poke_dance.fsys'][0]
        self.dance = D.file(self.dance_name)
        es = noddisc.fsys_entries(self.dance)
        self.dance_entries = es
        self.template = noddisc.extract(self.dance, [e for e in es if e.name == TEMPLATE_NAME][0])

    def picture(self, dex):
        """-> (normal, shiny) PIL images PIC x PIC (the male picture)"""
        im = F.decode_c8(noddisc.extract(self.pic_data, self.pic[self.faces.row(dex)[1]]))
        assert im.size == (PIC, 2 * PIC), im.size
        return im.crop((0, 0, PIC, PIC)), im.crop((0, PIC, PIC, 2 * PIC))

    def members(self, dex, k):
        """-> [(word0, type, name, data)] of the species' two new members of poke_dance.fsys"""
        n, s = self.picture(dex)
        out = []
        for shiny, im in ((False, n), (True, s)):
            out.append((icon_key(k, shiny), 25, 'g4body%03d%s' % (dex, '_c' if shiny else ''), make_ant(im, self.template)))
        return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--pbr', default='')
    ap.add_argument('--xd', default='')
    ap.add_argument('--only', default='445,448,468,387,493,487')
    a = ap.parse_args()
    from PIL import Image
    ic = Icons(a.pbr, a.xd)
    os.makedirs(a.out, exist_ok=True)
    tiles = []
    for dex in [int(x) for x in a.only.split(',')]:
        for w0, _t, name, data in ic.members(dex, dex - 387):
            tiles.append(F.decode_c8(data[TEX_OFF:]))
    sheet = Image.new('RGBA', (CELL * len(tiles), 3 * CELL), (90, 90, 90, 255))
    for k, t in enumerate(tiles):
        sheet.paste(t, (k * CELL, 0), t)
    sheet.save(os.path.join(a.out, 'icons.png'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
