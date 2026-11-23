"""Adds ONE battle form to a finished Gen 5-7 pack: Zygarde's Complete Forme (Power Construct, 0.7.1 round 3), without remaking the pack.

  python -I gen57_zygarde.py --rom <Ultra Sun .cci> --pack <a finished pack folder> [--tm-cache xd_machines.json]

The real generator (gen57_forms.py) makes the same row from tools/n3ds/gen57_forms.json ("nat": 718, "form": 4, model "none": the base Zygarde's model is kept,
as for Blade Forme and Core); this script is the quick way to give a pack that is already made the same row (same functions, same key "Zygarde (Complete)").
It is idempotent. Numbers only: the row comes from the player's own dump and stays in the player's pack folder.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_data as D
import gen57_pack as P
import gen57_all as A
import gen57_forms as F

NAT, FORM, KEY = 718, 4, 'Zygarde (Complete)'


def add(pack, rom, tm_cache):
    u = D.Usum(rom)
    c = json.load(open(tm_cache))
    tm_moves, tutors = c['tm_moves'], c['tutors']
    defined = set(range(468, 729))
    pre = {e.species for n in range(1, A.LAST + 1) for e in u.evolutions(n)}
    base = u.personal(NAT)
    record = base.form_stats_index + FORM - 1
    name = u.species_name(NAT, 'en')
    sh = NAT
    row, _rep = A.build_row(u, NAT, sh, defined, tm_moves, tutors, pre, [], record=record, pkx_row=0)
    row['copy'] = name
    row.pop('pkxDataId', None)
    row['sinka.pokemonDataId'] = list(row['sinka.pokemonDataId'])
    n = 0
    for lg in ['us'] + list(P.LANG_DIRS):
        dump_lang = 'en' if lg == 'us' else P.LANG_DIRS[lg]
        folder = 'data' if lg == 'us' else 'data_' + lg
        path = os.path.join(pack, folder, '03_forms.json')
        if not os.path.exists(path):
            continue
        d = json.load(open(path, encoding='utf-8'))
        r = dict(row)
        r['text'] = {'name': F.shown_form(u.species_name(NAT, dump_lang), 'battle', NAT, FORM), 'typeName': P.text_for(u, NAT, dump_lang)['typeName']}
        d['add']['pokemon'][KEY] = r
        json.dump(d, open(path, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        n += 1
    ft = os.path.join(pack, 'forms.txt')
    text = open(ft, encoding='utf-8').read()
    line = 'form %s|%s|battle' % (KEY, name)
    if line not in text:
        lines = text.rstrip('\n').split('\n')
        at = max(i for i, x in enumerate(lines) if x.startswith('form ')) + 1
        lines.insert(at, line)
        open(ft, 'w', encoding='utf-8', newline='').write('\n'.join(lines) + '\n')
    return n


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--tm-cache', default=None)
    a = ap.parse_args()
    cache = a.tm_cache or os.path.join(os.path.dirname(os.path.abspath(a.pack)), 'xd_machines.json')
    print('Zygarde Complete Forme added to', add(os.path.abspath(a.pack), a.rom, cache), 'language folders')
