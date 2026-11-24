"""The Gen 4 pack's words in the game's other languages (docs/GEN4.md "Languages", docs/LANGUAGES.md).

Part of the Gen 4 pack. gen4_moves.py, gen4_items.py, gen4_machines.py and gen4_pack.py call it; the C++ pack builder reads the
file it generates (engine/formats/pbr/pbr_pack_text_i18n.inc), so the two builders write the same words by construction.
Nothing here is game data: the names are the games' public official names (from public name lists: Spanish of Spain, French,
German, Italian, Japanese), the descriptions and battle messages are our own words, MACHINE-MADE (docs/GEN4.md "Languages":
a native speaker should look at them). The one source is gen4_text_i18n.tsv next to this file:

    kind <TAB> English key <TAB> Spanish <TAB> French <TAB> German <TAB> Italian <TAB> Japanese

kinds: move_name, move_doc, ability_name, ability_doc, message, item_name, item_doc, species_name, species_category, sys, tm_label,
fairy. Names are written as the games write them (mixed case); `shown()` makes them the way XD writes them in each language.

Language codes are the game's (system.language): sp, fr, ge, it, jp. (English is the pack's own text.)

XD's own style, found in the European and Japanese discs' text banks:
  sp  capitals, accents kept (Ñ Á É Í Ó Ú Ü)           PLACAJE, ELEC. ESTÁT.
  fr  capitals, NO accents on capitals                  ECLAIR, PERCEE NOIRE
  ge  capitals, umlauts kept, ß written SS              KISMETWUNSCH, FUSSKICK
  it  capitals, accents kept (À È Ù)                    AGILITÀ
  jp  as written (kana)                                 たいあたり
Straight apostrophes only. Move and ability names are at most 12 characters in the European banks (7 in Japanese), descriptions
of an ability at most 28; where an official name is longer a SHORT form is used (SHORT below) and listed in the hand-off.
"""
import os
import unicodedata

LANGS = ('sp', 'fr', 'ge', 'it', 'jp')
HERE = os.path.dirname(os.path.abspath(__file__))
TSV = os.path.join(HERE, 'gen4_text_i18n.tsv')




SHORT = {
    ('move_name', 'Flash Cannon', 'sp'): 'Cañón Respl.',
    ('move_name', 'Iron Head', 'sp'): 'Cabeza Hierro',
    ('move_name', 'Last Resort', 'fr'): 'Dern. Recours',
    ('move_name', 'Ice Shard', 'sp'): 'Esquirla Hel.',
    ('move_name', 'Metal Burst', 'sp'): 'Repres. Metal',
    ('ability_name', 'Sniper', 'sp'): 'Francotirad.',
    ('item_name', 'Linking Cord', 'ge'): 'Verbindungsschn.',
}

_rows = None


def rows():
    """{(kind, key): {lang: text}} read from the TSV."""
    global _rows
    if _rows is None:
        _rows = {}
        with open(TSV, encoding='utf-8') as f:
            for line in f:
                line = line.rstrip('\n')
                if not line or line.startswith('#'):
                    continue
                p = line.split('\t')
                assert len(p) == 7, line
                _rows[(p[0], p[1])] = dict(zip(LANGS, p[2:]))
    return _rows


def raw(kind, key, lang):
    t = SHORT.get((kind, key, lang))
    return t if t is not None else rows()[(kind, key)][lang]


def shown(lang, text):
    """A name the way XD writes it in that language (see the top)."""
    if lang == 'jp':
        return text
    t = text.replace('’', "'")
    if lang == 'fr':
        t = ''.join(c for c in unicodedata.normalize('NFD', t) if not unicodedata.combining(c))
        t = t.replace('œ', 'oe').replace('Œ', 'OE')
    return t.upper()


def wrap(lang, text, width, lines):
    """Greedy word wrap into at most `lines` lines joined with the game's line break "{00}". width = characters of the Latin
    languages; Japanese lines are full-width, so half as many characters (a space is U+3000, kept inside a line; Japanese may also
    break inside a long word)."""
    if lang == 'jp':
        cap, out, cur = width // 2, [], ''
        for ch in text.replace(' ', '　'):
            if len(cur) == cap:
                if ch == '　':
                    ch = ''
                elif '　' in cur:
                    cut = cur.rindex('　')
                    out.append(cur[:cut])
                    cur = cur[cut + 1:]
                else:
                    out.append(cur)
                    cur = ''
            if cur or ch != '　':
                cur += ch
        out.append(cur)
    else:
        cap, words, out, cur = width, text.split(' '), [], ''
        for w in words:
            if cur and len(cur) + 1 + len(w) > cap:
                out.append(cur)
                cur = w
            else:
                cur = w if not cur else cur + ' ' + w
        out.append(cur)
    if len(out) > lines or any(len(l) > cap for l in out):
        raise ValueError('does not fit %d lines of %d: %s' % (lines, cap, text))
    return '{00}'.join(out)




DOC_BOX = {'summary': (28, 2), 'bag': (19, 3)}
CAPS = {}


def box_wrap(lang, text, box):
    """wrap() into one of the DOC_BOX boxes ('summary' / 'bag')."""
    width, lines = DOC_BOX[box]
    return wrap(lang, text, CAPS.get((box, lang), width), lines)


def lang_fields(field_values):
    """{'name': {lang: text}, 'doc': {...}} -> {'name.sp': ..., 'doc.sp': ..., 'name.fr': ...}: the language keys of a "text" block,
    languages in LANGS order, the fields in the order given."""
    out = {}
    for lang in LANGS:
        for field, values in field_values:
            out['%s.%s' % (field, lang)] = values[lang]
    return out


def move_text(name):
    """The "text" block of a Gen 4 move row: English name and doc as gen4_move_text.py makes them, plus every language."""
    import gen4_move_text as MT
    t = {'name': MT.upper_name(name), 'doc': MT.move_doc(name)}
    t.update(lang_fields([
        ('name', {l: shown(l, raw('move_name', name, l)) for l in LANGS}),
        ('doc', {l: box_wrap(l, raw('move_doc', name, l), 'summary') for l in LANGS})]))
    return t


def not_built_text(name):
    """A move row that is not built yet: its name and the 'does nothing' line."""
    import gen4_move_text as MT
    t = {'name': MT.upper_name(name), 'doc': 'Not built yet: does nothing.'}
    t.update(lang_fields([
        ('name', {l: shown(l, raw('move_name', name, l)) for l in LANGS}),
        ('doc', {l: raw('sys', 'Not built yet: does nothing.', l) for l in LANGS})]))
    return t


def tm_name(lang, number):
    """The machine's name: TM51 / MT51 / CT51 / わざマシン５１ (the Japanese disc writes the number full-width)."""
    label = shown(lang, raw('tm_label', 'TM', lang))
    if lang == 'jp':
        return label + ''.join(chr(0xFF10 + int(c)) for c in '%02d' % number)
    return '%s%02d' % (label, number)


def machine_text(item, number, move):
    """The "text" block of the bag item of a machine: name TMnn, the move's description in the bag's box (3 lines of 19)."""
    import gen4_move_text as MT
    t = {'name': item, 'doc': MT.item_doc(move)}
    t.update(lang_fields([
        ('name', {l: tm_name(l, number) for l in LANGS}),
        ('doc', {l: box_wrap(l, raw('move_doc', move, l), 'bag') for l in LANGS})]))
    return t


def item_text(name, english_doc):
    """The "text" block of one of the 13 new items."""
    t = {'name': name.upper(), 'doc': english_doc}
    t.update(lang_fields([
        ('name', {l: shown(l, raw('item_name', name, l)) for l in LANGS}),
        ('doc', {l: box_wrap(l, raw('item_doc', name, l), 'bag') for l in LANGS})]))
    return t


def species_text(key, english_name, english_category):
    """The "text" block of a new species: name and (when the pack has one) the category."""
    fields = [('name', {l: shown(l, raw('species_name', key, l)) for l in LANGS})]
    t = {'name': english_name}
    if english_category is not None:
        t['typeName'] = english_category
        fields.append(('typeName', {l: shown(l, raw('species_category', key, l)) for l in LANGS}))
    t.update(lang_fields(fields))
    return t




def cstr(s):
    """A C string literal: ASCII as it is, every other byte as a 3-digit octal escape (no compiler character-set surprises)."""
    out = ['"']
    for b in s.encode('utf-8'):
        c = chr(b)
        if b >= 0x80 or c in '\\"' or b < 0x20:
            out.append('\\%03o' % b)
        elif c == '?':
            out.append('\\077')
        else:
            out.append(c)
    out.append('"')
    return ''.join(out)


def final_rows():
    """[(kind, key, [text per language in LANGS order])]: the strings the game shows, final (names capitalised, descriptions wrapped).
    Kinds in the generated table: move_name, move_doc (summary: 2 lines of 28), item_doc_move (the TM's bag box: 3 lines of 19),
    ability_name, ability_doc, message, item_name, item_doc, species_name, species_category, sys, tm_label, fairy."""
    out = []
    for (kind, key), v in rows().items():
        vals = lambda f: [f(l, raw(kind, key, l)) for l in LANGS]
        if kind in ('move_name', 'ability_name', 'item_name', 'species_name', 'species_category', 'fairy', 'tm_label'):
            out.append((kind, key, vals(shown)))
        elif kind == 'move_doc':
            out.append((kind, key, vals(lambda l, t: box_wrap(l, t, 'summary'))))
            out.append(('item_doc_move', key, vals(lambda l, t: box_wrap(l, t, 'bag'))))
        elif kind == 'item_doc':
            out.append((kind, key, vals(lambda l, t: box_wrap(l, t, 'bag'))))
        else:
            out.append((kind, key, vals(lambda l, t: t)))
    return out


def inc_text():
    lines = ['// GENERATED by tools/pbr_convert/gen4_text_i18n.py (python tools/pbr_convert/gen4_text_i18n.py generate): do not edit.',
             '// The Gen 4 pack\'s words in sp, fr, ge, it, jp (source: tools/pbr_convert/gen4_text_i18n.tsv; machine-made descriptions).',
             '// { kind, English key, { sp, fr, ge, it, jp } }']
    for kind, key, vals in final_rows():
        lines.append('{%s, %s, {%s}},' % (cstr(kind), cstr(key), ', '.join(cstr(v) for v in vals)))
    return '\n'.join(lines) + '\n'


INC_FILES = (
    os.path.join('engine', 'formats', 'pbr', 'pbr_pack_text_i18n.inc'),
    os.path.join('games', 'xd', 'src', 'gen4_text_i18n.inc'),
)


def repo_root():
    return os.path.normpath(os.path.join(HERE, '..', '..'))


def _no_comments(t):
    """The public export strips every comment of the .inc files: compare what is left (no // lines, no empty lines)."""
    return [l.rstrip() for l in t.split(chr(10)) if l.strip() and not l.lstrip().startswith('//')]


def generate(check=False):
    text = inc_text()
    bad = 0
    for rel in INC_FILES:
        p = os.path.join(repo_root(), rel)
        have = open(p, encoding='utf-8', newline='').read().replace(chr(13) + chr(10), chr(10)) if os.path.exists(p) else None
        if (have is None) or _no_comments(have) != _no_comments(text):
            bad += 1
            if not check:
                with open(p, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
                print('wrote', rel)
            else:
                print('OUT OF DATE:', rel)
    return 1 if (check and bad) else 0


if __name__ == '__main__':
    import sys
    sys.exit(generate(check=(len(sys.argv) > 1 and sys.argv[1] == 'check')))
