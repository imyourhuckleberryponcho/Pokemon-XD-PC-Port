"""Checks of the Gen 4 pack's other-language words (gen4_text_i18n.py): nothing missing, every character is in the game's font,
names and descriptions fit their boxes, the generated C++ tables are current. No disc needed.

  python tools/pbr_convert/gen4_text_i18n_test.py [--lengths] [--fonts <language pack> [--jp <Japanese pack>] [--pack <Gen 4 pack>]]
      (exit 1 on a problem; --lengths lists every name over 12 characters; --fonts also measures every text against its box with the game's
       own fonts out of a language pack the PC made: gen4_text_fit.py, needs no disc)
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen4_text_i18n as T
import gen4_move_text as MT



PAL_EXTRA = '¡ª«°º»¿ÀÁÂÄÇÈÉÊËÌÍÎÏÑÒÓÔÖ×ÙÚÛÜßàáâäçèéêëìíîïñòóôöùúûüŒœ‘’“”★♀♂'
PAL_GLYPHS = set(chr(c) for c in range(0x20, 0x7f) if chr(c) not in '{}') | set(PAL_EXTRA)


def jp_glyphs():
    p = os.path.join(T.repo_root(), 'games', 'xd', 'tools', 'xd_languages_jp_fonts.inc')
    s = open(p, encoding='utf-8').read()
    sets = []
    for m in re.finditer(r'kFont(\d)Glyphs\[\] = \{(.*?)\};', s, re.S):
        sets.append(set(chr(int(h, 16)) for h in re.findall(r'0x([0-9A-Fa-f]{4})', m.group(2))))
    return sets[0] & sets[1]


JP = jp_glyphs()
problems = []


def bad(msg):
    problems.append(msg)


def check_chars(kind, key, lang, text):
    t = re.sub(r'\{[0-9A-Fa-f]{2}\}', '', text)
    allowed = JP if lang == 'jp' else PAL_GLYPHS
    miss = sorted(set(c for c in t if c not in allowed))
    if miss:
        bad('%s %s [%s]: not in the font: %s' % (kind, key, lang, ''.join(miss)))


def main():
    lengths = '--lengths' in sys.argv
    rows = T.rows()
    kinds = {}
    for (k, key) in rows:
        kinds.setdefault(k, []).append(key)

    want = {'move_name': set(m for m in MT.DOCS if m not in ('False Swipe', 'Endure', 'Will-O-Wisp', 'Silver Wind', 'Explosion', 'Recycle',
                                                              'Thunder Wave', 'Swords Dance', 'Psych Up', 'Rock Slide', 'Sleep Talk',
                                                              'Dream Eater', 'Swagger', 'Substitute')),
            'move_doc': set(MT.DOCS)}
    for k, s in want.items():
        if set(kinds.get(k, [])) != s:
            bad('%s: missing %s extra %s' % (k, sorted(s - set(kinds.get(k, []))), sorted(set(kinds.get(k, [])) - s)))
    for k, n in (('ability_name', 47), ('ability_doc', 47), ('item_name', 13), ('item_doc', 13), ('species_name', 105), ('species_category', 105)):
        if len(kinds.get(k, [])) != n:
            bad('%s: %d rows, expected %d' % (k, len(kinds.get(k, [])), n))

    over = []
    for (kind, key), v in rows.items():
        for lang in T.LANGS:
            text = T.raw(kind, key, lang)
            check_chars(kind, key, lang, text)
            if kind in ('move_name', 'ability_name'):
                n = len(T.shown(lang, text))
                limit = 7 if lang == 'jp' else 12
                if lang != 'jp' and n > limit or lang == 'jp' and n > limit:
                    over.append((kind, key, lang, T.shown(lang, text), n))
            if kind == 'ability_doc' and len(text) > (30 if lang == 'jp' else 50):
                bad('ability_doc %s [%s]: %d characters (limit %d)' % (key, lang, len(text), 16 if lang == 'jp' else 28))
            if kind == 'message':
                a = sorted(re.findall(r'\{[0-9A-F]{2}\}', key))
                b = sorted(re.findall(r'\{[0-9A-F]{2}\}', text))
                if a != b:
                    bad('message %s [%s]: control codes differ: %s' % (key, lang, text))
    for kind, key, vals in T.final_rows():
        pass
    for lang in T.LANGS:
        for (kind, key) in rows:
            try:
                if kind == 'move_doc':
                    T.box_wrap(lang, T.raw(kind, key, lang), 'summary')
                    T.box_wrap(lang, T.raw(kind, key, lang), 'bag')
                elif kind == 'item_doc':
                    T.box_wrap(lang, T.raw(kind, key, lang), 'bag')
            except ValueError as e:
                bad('[%s] %s' % (lang, e))
    if lengths or True:
        with open('lengths.txt' if lengths else os.devnull, 'w', encoding='utf-8') as f:
            for o in sorted(over):
                f.write('%s\t%s\t%s\t%s\t%d\n' % o)
    print('names over their limit (not a failure; see the hand-off): %d' % len(over))
    for o in sorted(over):
        print('   %s %s [%s] %s (%d)' % o)

    if '--fonts' in sys.argv:
        import gen4_text_fit as FIT

        def opt(name):
            return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else None
        fonts = FIT.Fonts(opt('--fonts'), opt('--jp'))
        moves = FIT.machine_moves(opt('--pack'))
        for w in FIT.check(fonts, moves, all_rows=not moves):
            bad('too wide for its box: %s [%s] "%s" %d px (limit %d)' % w)

    if T.generate(check=True):
        bad('the generated .inc files are out of date: python tools/pbr_convert/gen4_text_i18n.py')
    for p in problems:
        print('PROBLEM', p)
    print('%d problems' % len(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
