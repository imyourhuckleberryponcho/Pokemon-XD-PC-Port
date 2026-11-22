"""The Pokedex entry texts of the player's own Ultra Sun / Ultra Moon dump (docs/design/4-pokedex.md, P7 / P8).

    python -I tools/n3ds/usum_dex_entries.py --rom <decrypted .cci> --out <folder> [--check]

Writes <folder>/entries_<lang>_<sun|moon>.txt for en, fr, it, de, es and ja (kana only): one line "<National No.><TAB><text>", UTF-8, 1..807.
The game reads them from <data folder>\\extra_assets\\pokedex at run time. Nothing of the game is kept in the repository or the program; the
launcher's Extra assets page runs this on the player's PC with his dump, as it does for the models.

Where the texts are (found by content, like the rest of the 3DS readers): the text archives a/0/3/N are one per language (0 = Japanese kana,
1 = Japanese kanji, 2 en, 3 fr, 4 it, 5 de, 6 es); in each, entry 124 holds the Ultra Sun Pokedex texts and 125 the Ultra Moon ones, a line per
National number (line 25 = Pikachu; 1118 lines: 807 species, then forms). The text format is the one gamedetect.decode_text reads.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'pbr_survey'))
sys.path.insert(0, os.path.join(HERE, '..', 'pbr_convert'))
import garc
import gamedetect
import ncch

LANGS = (('ja', 0), ('en', 2), ('fr', 3), ('it', 4), ('de', 5), ('es', 6))
VERSIONS = (('sun', 124), ('moon', 125))
LAST = 807


SWAP = {0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2026: '...', 0x2013: '-', 0x2014: '-', 0x00A0: ' ', 0x3000: ' ',
        0xE07F: '!', 0xE080: '?', 0x2640: '', 0x2642: ''}


def clean(text, lang):
    out = []
    for ch in text.replace('\\n', ' '):
        c = ord(ch)
        if ch in '\n\r':
            out.append(' ')
        elif c < 0x20 or 0xD800 <= c < 0xF900:
            continue
        else:
            out.append(SWAP.get(c, ch))
    s = re.sub(r' +', ' ', ''.join(out)).strip()
    return s


def entries(r, archive, entry):
    g = gamedetect._garc(r, archive)
    return gamedetect.decode_text(garc.maybe(g.read(entry)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--check', action='store_true', help='print counts only')
    a = ap.parse_args()
    r, prod = ncch.open_rom(a.rom)
    os.makedirs(a.out, exist_ok=True)
    made = 0
    for lang, arch in LANGS:
        for ver, entry in VERSIONS:
            try:
                lines = entries(r, 'a/0/3/%d' % arch, entry)
            except Exception as e:
                print('%s %s: not found (%s)' % (lang, ver, e))
                continue
            if len(lines) < LAST or len(lines[25]) < 8:
                print('%s %s: entry %d is not the Pokedex text (%d lines)' % (lang, ver, entry, len(lines)))
                continue
            rows = []
            for no in range(1, LAST + 1):
                t = clean(lines[no], lang)
                if t:
                    rows.append('%d\t%s' % (no, t))
            print('%s %s: %d texts' % (lang, ver, len(rows)))
            if not a.check:
                with open(os.path.join(a.out, 'entries_%s_%s.txt' % (lang, ver)), 'w', encoding='utf-8', newline='\n') as f:
                    f.write('\n'.join(rows) + '\n')
                made += 1
    print('product %s: %d file(s)' % (prod, made))
    return 0 if made or a.check else 1


if __name__ == '__main__':
    sys.exit(main())
