"""Do the Gen 4 pack's words fit their boxes? Measured with the game's own fonts (docs/GEN4.md "Languages").

The widths come from the fonts of a language pack the player's PC made (xd_languages: <pack>/tables/resident_fnt*.bin,
common_fnt*.bin; nothing of them is kept in the repository). Without such a pack the check says so and does nothing; the
character-count checks of gen4_text_i18n_test.py still run.

  python tools/pbr_convert/gen4_text_fit.py <language pack folder> [<Japanese pack folder>] [--pack <Gen 4 pack>] [--all]
      the folder holding tables\\ (e.g. build\\gen4lang\\pack\\languages); exit 1 if a text is too wide

The limits are what the screens showed (native 640 x 480 pixels, font 1 = the dialogue font):
  bag list row (item name; machine row = "No88 " + move name)   240   (the count "X 1" starts ~245 px from the name; 240 = Falsotortazo, XD's own)
  bag description box line                                      225
  summary move description line (2 lines)                       325   (box ~ 330 px)
  summary ability name / description                            180 / 350   (the name tag / the black bar; XD's own longest 322)
  Strategy Memo category line (font 3, with the game's "POKéMON")  215
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen4_text_i18n as T

LIM_ROW = 240
LIM_BAG_DOC = 225
LIM_SUMMARY_DOC = 325
LIM_ABILITY_NAME = 180
LIM_ABILITY_DOC = 350
LIM_CATEGORY = 215

CATEGORY_TEMPLATE = {'sp': 'POKéMON %s', 'fr': 'POKéMON %s', 'ge': '%s', 'it': 'POKéMON %s', 'jp': '%sポケモン'}
FONT = 1

MACHINE_PREFIX = {'sp': 'N.°%d ', 'fr': 'N°%d ', 'ge': 'Nr.%d ', 'it': 'N°%d ', 'jp': 'No%d '}


def _blocks(path):
    b = open(path, 'rb').read()
    off, out = 0, {}
    while True:
        bid, w, h, nxt = struct.unpack_from('>HBBI', b, off)
        if bid == 0xFFFF:
            break
        cnt, _, _ = struct.unpack_from('>HHI', b, off + 8)
        d = out.setdefault(bid, {})
        for i in range(cnt):
            code, gw, gh, bits = struct.unpack_from('>HBBI', b, off + 0x18 + i * 8)
            d[code] = gw
        if nxt == 0:
            break
        off += nxt
    return out


def load_fonts(pack, names):
    f = {}
    for n in names:
        p = os.path.join(pack, 'tables', n)
        if os.path.exists(p):
            for k, v in _blocks(p).items():
                f.setdefault(k, {}).update(v)
    return f


class Fonts:
    def __init__(self, pal_pack, jp_pack=None):
        self.pal = load_fonts(pal_pack, ('resident_fnt.bin', 'common_fnt.bin')) if pal_pack else {}
        self.jp = load_fonts(jp_pack, ('resident_fnt_jp.bin', 'common_fnt_jp.bin', 'common_fnt2_jp.bin')) if jp_pack else {}

    def have(self, lang):
        return bool(self.jp if lang == 'jp' else self.pal)

    def width(self, lang, font, text):
        f = (self.jp if lang == 'jp' else self.pal)[font]
        return sum(f.get(ord(c), f[ord('M')]) for c in text if c not in '{}')


def machine_moves(pack):
    """The moves of the extra machines (the pack's machines.txt: "machine <n> <TM no> <move>"), as the rows of the table call them;
    without a pack every move counts (--all)."""
    out = set()
    p = os.path.join(pack, 'machines.txt') if pack else ''
    if p and os.path.exists(p):
        for ln in open(p, encoding='utf-8'):
            f = ln.split()
            if f and f[0] == 'machine' and not f[3].isdigit():
                out.add(f[3].replace('_', ' '))
    return out


def check(fonts, machine_moves, all_rows=False, number=88):
    """[(what, lang, text, width, limit)] for every text that is too wide."""
    bad = []
    rows = T.rows()
    for lang in T.LANGS:
        if not fonts.have(lang):
            continue
        for (kind, key) in rows:
            if kind == 'item_name':
                s = T.shown(lang, T.raw(kind, key, lang))
                w = fonts.width(lang, FONT, s)
                if w > LIM_ROW:
                    bad.append(('item name ' + key, lang, s, w, LIM_ROW))
            elif kind in ('ability_name', 'ability_doc'):
                s = T.shown(lang, T.raw(kind, key, lang)) if kind == 'ability_name' else T.raw(kind, key, lang)
                lim = LIM_ABILITY_NAME if kind == 'ability_name' else LIM_ABILITY_DOC
                w = fonts.width(lang, FONT, s)
                if w > lim:
                    bad.append((kind + ' ' + key, lang, s, w, lim))
            elif kind == 'species_category':
                s = CATEGORY_TEMPLATE[lang] % T.shown(lang, T.raw(kind, key, lang))
                w = fonts.width(lang, 3, s)
                if w > LIM_CATEGORY:
                    bad.append(('Memo category ' + key, lang, s, w, LIM_CATEGORY))
            elif kind == 'move_name' and (all_rows or key in machine_moves):
                s = MACHINE_PREFIX[lang] % number + T.shown(lang, T.raw(kind, key, lang))
                w = fonts.width(lang, FONT, s)
                if w > LIM_ROW:
                    bad.append(('machine row ' + key, lang, s, w, LIM_ROW))
            elif kind in ('item_doc', 'move_doc'):
                if kind == 'move_doc':
                    texts = [(T.box_wrap(lang, T.raw(kind, key, lang), 'summary'), LIM_SUMMARY_DOC)]
                    if key in machine_moves or all_rows:
                        texts.append((T.box_wrap(lang, T.raw(kind, key, lang), 'bag'), LIM_BAG_DOC))
                else:
                    texts = [(T.box_wrap(lang, T.raw(kind, key, lang), 'bag'), LIM_BAG_DOC)]
                for t, lim in texts:
                    for line in t.split('{00}'):
                        w = fonts.width(lang, FONT, line)
                        if w > lim:
                            bad.append(('%s %s' % (kind, key), lang, line, w, lim))
    return bad


def main():
    argv = sys.argv[1:]
    pack = None
    if '--pack' in argv:
        i = argv.index('--pack')
        pack = argv[i + 1]
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith('--')]
    if not args:
        print(__doc__)
        return 0
    fonts = Fonts(args[0], args[1] if len(args) > 1 else None)
    moves = machine_moves(pack)
    bad = check(fonts, moves, all_rows='--all' in sys.argv or not moves)
    for b in bad:
        print('TOO WIDE: %s [%s] "%s" %d px (limit %d)' % b)
    print('%d texts too wide' % len(bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
