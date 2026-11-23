"""The Gen 5-7 pack builder (docs/GEN57.md), data driven: tools/n3ds/gen57_species.json is the list.

  python -I gen57_pack.py --rom <decrypted Ultra Sun / Moon .cci> --xd-disc <XD iso> --gen4-pack <extra_assets/pbr_gen4> --out <pack folder>
                          [--no-models] [--list gen57_species.json]

The pack uses the Gen 4 pack's layout (games/xd/src/gen4_pack.cpp reads it): data/*.json ("add" blocks), tables.txt (model rows),
files/ (archives + GSfsys.toc), plus data_<lang>/ (the same data files with the words of another language: ge fr it sp jp),
gen57.json and report.json. Its row numbers start where the Gen 4 pack's end (read from --gen4-pack), so the Gen 4 rows must be loaded too.
Everything it writes is game data made from the player's own dump: it stays under the player's data folder, never in the repository.
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
for sub in ('pbr_survey', 'pbr_convert'):
    for base in (os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub), os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub)):
        if os.path.isdir(base) and base not in sys.path:
            sys.path.insert(0, base)

import gen57_data as D
import gen57_moves as MV
import gen57_abilities as AB
import gen57_hidden as HID

PACK_VERSION = 1
LANG_DIRS = {'ge': 'de', 'fr': 'fr', 'it': 'it', 'sp': 'es', 'jp': 'ja'}
GEN3_MOVES = 354
GEN4_LAST = 467
GEN4_SHIFT = 20
FIRST_NEW_MOVE_ROW = 488
FIRST_NEW_SPECIES = 520
GEN4_PKX_END = 525
PKX_GROUP_FIRST = 4200
PKX_RES = 0x00011E00


EVO_LEVEL_FALLBACK = 36


def shown(s):
    """XD writes names in capitals; one space per run of white space; the dump's variables are dropped."""
    s = re.sub(r'\{var\}', '', s).replace('\u2019', "'")
    return ' '.join(s.split()).upper() if s and ord(s[0]) < 0x2E80 else s


def desc(s):
    return re.sub(r'\{var\}', '', s).replace('\u2019', "'").replace('\n', '{00}')


def move_row_id(usum_id, new_rows):
    """The XD row of a USUM move, or None when XD (with the Gen 4 pack and this pack) has no such move."""
    if 1 <= usum_id <= GEN3_MOVES:
        return usum_id
    if GEN3_MOVES < usum_id <= 728:
        return usum_id + GEN4_SHIFT
    return new_rows.get(usum_id)


def evo_entry(e, target_name, level_fallback=EVO_LEVEL_FALLBACK):
    """(kind, buff, note) for one Gen 7 evolution record whose target is in the pack."""
    if e.method == 4:
        return 4, e.level, ''
    if e.method in (1, 2, 3):
        return 1, 0, ''
    if e.method == 5:
        return 5, 0, ''
    return 4, e.level or level_fallback, 'method %d has no XD counterpart: level %d' % (e.method, e.level or level_fallback)


def machine_bits(learned, tm_moves, tutors):
    """The 58 TM bytes and 12 tutor bytes: set when the species learns that move by level (phase 1; USUM's own TM flags need its TM list)."""
    return [1 if m in learned else 0 for m in tm_moves], [1 if m in learned else 0 for m in tutors]


def species_row(u, nat, standin_id, model_row, moves_by_row, tm_moves, tutors, in_pack, new_rows, notes, record=None):
    rec = nat if record is None else record
    p = u.personal(rec)
    t1, t2 = D.TYPE_XD.get(p.type1), D.TYPE_XD.get(p.type2)
    if t1 is None:
        t1 = t2 if t2 is not None else 0
        notes.append('Fairy type has no XD row: shown as the other type (Normal)')
    if t2 is None:
        t2 = t1
    abil = [a for a in p.abilities]
    ab = [abil[0], abil[1] if abil[1] else 0]
    learn, seen = [], set()
    for lv, mv in u.levelup(rec):
        row = move_row_id(mv, new_rows)
        if row is None:
            continue
        if row in seen:
            continue
        seen.add(row)
        learn.append((max(lv, 1), row))
    learn.sort()
    learn = learn[:20]
    ev = p.ev
    evo = [(0, 0, 0)] * 5
    k = 0
    for e in u.evolutions(rec):
        if e.species not in in_pack or k >= 5:
            if e.species not in in_pack:
                notes.append('evolution into %s left out: not in the pack' % u.species_name(e.species))
            continue
        kind, buff, note = evo_entry(e, u.species_name(e.species))
        if note:
            notes.append(note)
        evo[k] = (kind, buff, u.species_name(e.species))
        k += 1
    evo = evo + [(0, 0, 0)] * (5 - len(evo))
    wazaMcn, teach = machine_bits({m for _, m in learn}, tm_moves, tutors)
    row = {
        'copy': standin_id,
        'growDataId': p.growth, 'get': p.catch_rate, 'sexRatio': p.gender, 'giveExp': min(p.base_exp, 255), 'initFriend': p.friendship,
        'height': max(1, round(p.height_cm / 10)), 'weight': p.weight_hg,
        'numPokemon': nat, 'numZukan': nat,
        'zokuseiDataId': [t1, t2], 'tokuseiDataId': ab,
        'wazaMcn': wazaMcn, 'teachWaza': teach,
        'itemDataId': [0, 0],
        'basisStatus': [p.hp, p.atk, p.defn, p.spa, p.spd, p.spe, ev['hp'], ev['atk'], ev['def'], ev['spa'], ev['spd'], ev['spe']],
        'sinka.kind': [x[0] for x in evo], 'sinka.buff': [x[1] for x in evo], 'sinka.pokemonDataId': [x[2] for x in evo],
        'waza.level': [lv for lv, _ in learn] + [0] * (20 - len(learn)), 'waza.dataId': [mv for _, mv in learn] + [0] * (20 - len(learn)),
        'pkxDataId': model_row,
    }
    return row, dict(types=(p.type1, p.type2), abilities=abil, mapped=ab, bst=p.bst(), moves=learn)


def text_for(u, nat, lang):
    name = shown(u.species_name(nat, lang))
    cls = u.species_class(nat, lang)
    cls = re.sub(r'\s*(Pok\u00e9mon|Pok\u00e9mon|Pokemon|Pok\u00e9mons|\u30dd\u30b1\u30e2\u30f3)\s*$', '', cls).strip()
    return {'name': name, 'typeName': shown(cls)}


def move_json(u, mv, donor, lang):
    m = u.move(mv)
    d = {'copy': donor, 'pp': m.pp, 'zokuseiDataId': D.TYPE_XD.get(m.type) or 0, 'iryoku': m.power, 'avg': m.accuracy or 100, 'pri': m.priority,
         'pad37': {0: 0, 1: 1, 2: 2}[m.category],
         'text': {'name': shown(u.move_name(mv, lang)), 'doc': desc(u.move_desc(mv, lang))}}
    return d


NO_EFFECT = {'en': ' (No effect in this game yet.)', 'de': ' (In diesem Spiel noch ohne Wirkung.)', 'fr': " (Pas encore d'effet dans ce jeu.)",
             'it': ' (Ancora nessun effetto in questo gioco.)', 'es': ' (A\u00fan sin efecto en este juego.)', 'ja': '\uff08\u3053\u306e\u30b2\u30fc\u30e0\u3067\u306f\u307e\u3060\u52b9\u679c\u306a\u3057\uff09'}


def ability_lines(u, ids, built=()):
    """abilities.txt: one line per ability: id, then name and description in the 6 languages (tab separated); an ability with no battle
    effect built says so at the end of its words."""
    langs = ['en', 'de', 'fr', 'it', 'es', 'ja']
    out = ['format 1']
    for a in ids:
        cells = []
        for l in langs:
            d = desc(u.ability_desc(a, l))
            if a not in built:
                d += NO_EFFECT[l]
            cells.append(shown(u.ability_name(a, l)) + '\t' + d)
        out.append('\t'.join([str(a)] + cells))
    return '\n'.join(out) + '\n'


def convert_models(u, args, plan, d, toc, tmp):
    import noddisc
    import usum_models as UM
    import gf_to_xd
    import make_proto as M
    import gen4_pack as G4
    xdmap = UM.xd_species_models(d)
    toc_rows, pkx_rows, report = [], [], {}
    for k, s in enumerate(plan):
        nat = s['nat']
        arch, idx, _name = xdmap[s['standin']]
        data = d.file(arch)
        ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
        xd_bytes = noddisc.extract(data, ents[0])
        gm, texs, mots, group = M.load_species(u.r, nat, s.get('group'))
        p = u.personal(nat)
        pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lambda t: None,
                                     opts=dict(ground=True, decimate=2600, fire=True, light=1.0, eyes=True, size_rule='mainline',
                                               height_cm=p.height_cm, hover_rule='model', match=False), hover=None)
        name = 'n3_%d' % nat
        gid = PKX_GROUP_FIRST + k
        open(os.path.join(tmp, 'files', 'pkx_%s.fsys' % name), 'wb').write(G4.build_fsys(gid, [(PKX_RES, 15, name, pkx)]))
        toc_rows.append((gid, 'pkx_' + name))
        pkx_rows.append('pkx %d %d %d' % (GEN4_PKX_END + k, gid, PKX_RES))
        report[nat] = dict(tris=info['tris'], size=info['size'], group=group)
    return toc_rows, pkx_rows, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--gen4-pack', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--list', default=os.path.join(HERE, 'gen57_species.json'))
    ap.add_argument('--no-models', action='store_true')
    ap.add_argument('--xd-moves-cache', default=None, help='json of XD move rows by number (from xd_data_mods --show); effect donors are picked from it')
    ap.add_argument('--tm-cache', default=None, help='json with tm_moves / tutors (else read with xd_data_mods)')
    a = ap.parse_args()
    plan = json.load(open(a.list, encoding='utf-8'))
    u = D.Usum(a.rom)
    cache = a.tm_cache or os.path.join(os.path.dirname(os.path.abspath(a.out)), 'xd_machines.json')
    if os.path.exists(cache):
        c = json.load(open(cache))
    else:
        import gen4_data as G
        x = G.XD(a.xd_disc, os.path.dirname(cache), '')
        c = dict(tm_moves=x.tm_moves, tutors=x.tutors)
        json.dump(c, open(cache, 'w'))
    tm_moves, tutors = c['tm_moves'], c['tutors']

    new_rows = {}
    mcache = json.load(open(a.xd_moves_cache or os.path.join(os.path.dirname(os.path.abspath(a.out)), 'xd_moves_cache.json')))['xd']
    mrows, mbattle, mreport_moves = {}, None, None
    in_pack = {s['nat'] for s in plan['species']}
    out = os.path.abspath(a.out)
    tmp = out + '.tmp'
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    for sub in ('files', 'data'):
        os.makedirs(os.path.join(tmp, sub))
    pkx_rows, toc_rows, mreport = [], [], {}
    if not a.no_models:
        import pbr_models as PM
        PM.find_nod()
        d = PM.open_disc(a.xd_disc, 'GXXE01', 'XD')
        toc_name = [k for k in d.files if k.lower() == 'gsfsys.toc'][0]
        import gen4_pack as G4
        g4toc = open(os.path.join(a.gen4_pack, 'files', 'GSfsys.toc'), 'rb').read()
        toc_rows, pkx_rows, mreport = convert_models(u, a, plan['species'], d, g4toc, tmp)
        open(os.path.join(tmp, 'files', toc_name), 'wb').write(G4.extend_toc(g4toc, toc_rows))
    else:
        pkx_rows = ['pkx %d 0 0' % (GEN4_PKX_END + k) for k in range(len(plan['species']))]
    langs = ['us'] + list(LANG_DIRS)
    report = []
    for lg in langs:
        dump_lang = 'en' if lg == 'us' else LANG_DIRS[lg]
        folder = 'data' if lg == 'us' else 'data_' + lg
        os.makedirs(os.path.join(tmp, folder), exist_ok=True)
        rws, bt, rp = MV.build_all(u, mcache, langs=(dump_lang,))
        moves = {k: {kk: vv for kk, vv in v.items() if not kk.startswith('_')} for k, v in rws[dump_lang].items()}
        if lg == 'us':
            mbattle, mreport_moves = bt, rp
        species = {}
        for k, s in enumerate(plan['species']):
            notes = []
            row, rep = species_row(u, s['nat'], s['standin'], GEN4_PKX_END + k, None, tm_moves, tutors, in_pack, new_rows, notes)
            row['text'] = text_for(u, s['nat'], dump_lang)
            species[u.species_name(s['nat'], 'en')] = row
            if lg == 'us':
                rep.update(nat=s['nat'], name=u.species_name(s['nat'], 'en'), notes=notes, new_number=FIRST_NEW_SPECIES + k, model=mreport.get(s['nat']))
                report.append(rep)
        note = '_note'
        if moves:
            json.dump({note: 'made by gen57_pack.py from the player\'s own dump: game data, never in the repository', 'add': {'moves': moves}},
                      open(os.path.join(tmp, folder, '01_moves.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        json.dump({note: 'made by gen57_pack.py from the player\'s own dump: game data, never in the repository', 'add': {'pokemon': species}},
                  open(os.path.join(tmp, folder, '02_species.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    os.makedirs(os.path.join(tmp, 'battle'), exist_ok=True)
    json.dump({'_note': 'made by gen57_pack.py: what the new moves do (docs/modding/battle-effects.md)', 'moves': mbattle},
              open(os.path.join(tmp, 'battle', 'moves.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(mreport_moves, open(os.path.join(tmp, 'moves_report.json'), 'w'), indent=1)
    fairy = [r['name'] for r in mreport_moves if r['type'] == 17]
    abil_ids = list(range(124, u.ability_count()))
    adefs, areport = AB.build_all(u)
    open(os.path.join(tmp, 'abilities.txt'), 'w', encoding='utf-8').write(ability_lines(u, abil_ids, {v['id'] for v in adefs.values()}))
    json.dump({'_note': 'made by gen57_pack.py: what the new abilities do (docs/modding/battle-effects.md)', 'abilities': adefs},
              open(os.path.join(tmp, 'battle', 'abilities.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(areport, open(os.path.join(tmp, 'abilities_report.json'), 'w'), indent=1)
    HID.write(u, tmp)
    open(os.path.join(tmp, 'tables.txt'), 'w').write('\n'.join(['format 1', 'pkxcount 419'] + pkx_rows) + '\n')
    json.dump(dict(id='ultra-sun-moon-gen57', version=PACK_VERSION, species=len(plan['species']), moves=len(mreport_moves), fairy_moves=fairy,
                   abilities=len(abil_ids), game='GXXE01', generated_from=u.prod, needs='gen4-rows'),
              open(os.path.join(tmp, 'gen57.json'), 'w'), indent=1)
    json.dump(report, open(os.path.join(tmp, 'report.json'), 'w'), indent=1)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.rename(tmp, out)
    print('pack', out, len(plan['species']), 'species', len(mreport_moves), 'moves', len(abil_ids), 'abilities')


if __name__ == '__main__':
    main()
