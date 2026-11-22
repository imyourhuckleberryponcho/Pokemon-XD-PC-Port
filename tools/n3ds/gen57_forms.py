"""Alternate forms for the Gen 5-7 pack (docs/GEN57_FORMS.md): Alolan forms, Mega Evolutions, Primal Reversions, battle forms.

  python -I gen57_forms.py --rom <decrypted Ultra Sun / Moon .cci> --xd-disc <XD iso> --pack <a pack folder made by gen57_pack.py>
                           [--list gen57_forms.json] [--no-models] [--only alola,mega,...]

It ADDS to a pack folder that gen57_pack.py made (in place): a data file `03_forms.json` (items + species rows) in every language
folder, model rows in tables.txt, model archives + the extended GSfsys.toc, the abilities the forms use in abilities.txt, and
`forms.txt` (the form table the game reads: which row is a form of which base species, which Mega Stone turns which row into
which Mega). Everything it writes is game data made from the player's own dump and stays under the player's data folder.

A form is a hidden extra species row: its own stats, types, abilities, learn-set and model; it COPIES its base species' row, so
the Pokedex marks, the cry and the face are the base species'. The list (gen57_forms.json) holds numbers and words only.
"""


STONE_COUPON = 12000
CHARM_COUPON = 5000

import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
for sub in ('pbr_survey', 'pbr_convert'):
    for base in (os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub), os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub)):
        if os.path.isdir(base) and base not in sys.path:
            sys.path.insert(0, base)

import gen57_data as D
import gen57_pack as P
import gen57_all as A
import concurrent.futures as cf

FORMS_VERSION = 1
PKX_GROUP_FIRST = 4600
MODEL_FORM_CODE = {'alola': '61', 'mega': '5%d', 'primal': '5%d'}


def form_key(name, kind, form, nat, label=None):
    """The row's key (also its label part): 'Raichu (Alola)', 'Charizard (Mega X)'."""
    if kind == 'alola':
        return '%s (Alola)' % name
    if kind == 'mega':
        suffix = ''
        if nat in (6, 150):
            suffix = ' X' if form == 1 else ' Y'
        return '%s (Mega%s)' % (name, suffix)
    if kind == 'primal':
        return '%s (Primal)' % name
    return '%s (%s)' % (name, label or kind)


def shown_form(name, kind, nat, form, label=None):
    """The words the game shows for the row: Megas read 'MEGA X'; the other forms keep the species' own name."""
    n = P.shown(name)
    if kind == 'mega':
        suffix = ''
        if nat in (6, 150):
            suffix = ' X' if form == 1 else ' Y'
        return 'MEGA ' + n + suffix
    if kind == 'primal':
        return 'PRIMAL ' + n
    return n


def stone_for(u, items, name, form, nat):
    """The Mega Stone of a Mega: the item whose name starts like the species' (Charizardite X / Y: the order)."""
    cands = []
    for i, nm in items.items():
        base = re.sub(r'ite( [XY])?$', '', nm)
        k = 0
        while k < len(base) and k < len(name) and base[k].lower() == name[k].lower():
            k += 1
        if k >= 4:
            cands.append((k, i, nm))
    if nat in (6, 150):
        want = ' X' if form == 1 else ' Y'
        cands = [c for c in cands if c[2].endswith(want)]
    cands.sort(reverse=True)
    return (cands[0][1], cands[0][2]) if cands else (None, None)




STONE_DOC = {
    'en': '{S} Mega{00}Evolves when it{00}holds this.',
    'fr': "{S} peut{00}méga-évoluer{00}s'il le tient.",
    'de': 'Mega-Entwicklung{00}für {S}{00}beim Tragen.',
    'it': 'Megaevoluzione{00}di {S}{00}se tenuta.',
    'es': 'Megaevolución{00}de {S}{00}si lo lleva.',
    'ja': '{S}が{00}メガシンカできる{00}持たせると',
}
CHARM_DOC = {
    'en': ('ALOLA CHARM', 'Held at evolution,{00}it picks the{00}Alolan form.'),
    'fr': ('AMULETTE ALOLA', "Portée à l'évolution,{00}elle donne la forme{00}d'Alola."),
    'de': ('ALOLA-AMULETT', 'Beim Entwickeln{00}getragen: wählt die{00}Alola-Form.'),
    'it': ('TALISMANO ALOLA', "Tenuto all'evoluzione{00}dà la forma{00}di Alola."),
    'es': ('AMULETO ALOLA', 'Si lo lleva al{00}evolucionar, da la{00}forma de Alola.'),
    'ja': ('アローラのおまもり', '進化するとき{00}持たせると{00}アローラの姿になる。'),
}


def stone_doc(u, i, lang, nat=None):
    return STONE_DOC[lang].replace('{S}', P.shown(u.species_name(nat, lang)))


_W = {}


def _init(rom, xd_disc):
    import pbr_models as PM
    import usum_models as UM
    PM.find_nod()
    _W['u'] = D.Usum(rom)
    _W['d'] = PM.open_disc(xd_disc, 'GXXE01', 'XD')
    _W['xdmap'] = UM.xd_species_models(_W['d'])
    _W['by'], _W['dexmap'] = groups_by_pm(_W['u'])


def _convert_form(job):
    """One form's model (a process of the pool): (status, info). status: 'ok' | 'nogroup' | 'noshell' | 'error'."""
    nat, form, code, record, sh, mnum, nm, gid, tmp, mn = job
    import noddisc
    import gf_to_xd
    import make_proto as M
    import gen4_pack as G4
    u, d, xdmap, by = _W['u'], _W['d'], _W['xdmap'], _W['by']
    gs = by.get((mnum, code))
    if not gs:
        return 'nogroup', None
    arch, idx, _n = xdmap.get(sh) or xdmap.get(nat) or (None, None, None)
    if arch is None:
        return 'noshell', None
    fpath = os.path.join(tmp, 'files', 'pkx_%s.fsys' % nm)
    if os.environ.get('GEN57_FORMS_RESUME') == '1' and os.path.exists(fpath + '.ok'):
        return 'ok', dict(tris=0, size=os.path.getsize(fpath), group=0)
    try:
        data = d.file(arch)
        ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
        xd_bytes = noddisc.extract(data, ents[0])
        gm, texs, mots, group = M.load_species(u.r, mnum, gs[0])
        p = u.personal(record)


        for side, dec, atol, ctol, quant in ((256, 2600, 1, 1, 0), (256, 2600, 3, 2, 0), (128, 2600, 3, 2, 0), (128, 2600, 8, 3, 0), (128, 2600, 16, 3, 32),
                                             (128, 2600, 16, 4, 16), (128, 2000, 24, 4, 16), (64, 1600, 24, 5, 12), (64, 1200, 32, 6, 8), (64, 900, 32, 6, 8), (64, 600, 32, 6, 8)):
            pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lambda t: None,
                                         opts=dict(ground=True, decimate=dec, fire=True, light=1.0, eyes=True, size_rule='mainline', max_side=side,
                                                   height_cm=p.height_cm, hover_rule='model', match=False, anim_tol=atol, chan_tol=ctol, quant=quant), hover=None)
            if len(pkx) <= 614400:
                break
        if len(pkx) > 614400:
            return 'overcap', dict(size=len(pkx))
    except BaseException as e:
        import traceback
        return 'error', repr(e)[:200] + ' | ' + traceback.format_exc()[-600:].replace(chr(10), ' / ')
    open(fpath, 'wb').write(G4.build_fsys(gid, [(P.PKX_RES, 15, nm, pkx)]))
    open(fpath + '.ok', 'w').write('ok')
    return 'ok', dict(tris=info['tris'], size=info['size'], group=group)


ROTOM_SIGNATURE = {1: 315, 2: 56, 3: 59, 4: 403, 5: 437}


def add_signature_move(row, move_row):
    """Puts `move_row` into the first free slot (move 0) of the row's level-up list at level 1 (nothing if it is there or there is no free slot)."""
    if not move_row or move_row in row['waza.dataId']:
        return
    for i, m in enumerate(row['waza.dataId']):
        if m == 0:
            row['waza.dataId'] = list(row['waza.dataId'])
            row['waza.level'] = list(row['waza.level'])
            row['waza.dataId'][i] = move_row
            row['waza.level'][i] = 1
            return


def standin_old(u, xdmap, nat):
    """The stand-in shell for a species the disc has no model of and gen57_all's list does not cover (Gen 4): same type, nearest height
    among the Gen 1-2 species with an XD model (the rule of gen57_all.pick_standins)."""
    pool = {}
    for n in range(1, 252):
        if n not in xdmap or n in A.FAIRY_SPECIES_OLD:
            continue
        p = u.personal(n)
        t = D.TYPE_XD.get(p.type1)
        if t is not None:
            pool.setdefault(t, []).append((p.height_cm, n))
    p = u.personal(nat)
    t = D.TYPE_XD.get(p.type1)
    if t is None:
        t = D.TYPE_XD.get(p.type2)
    cands = pool.get(t or 0) or pool[0]
    return min(cands, key=lambda c: (abs(c[0] - p.height_cm), c[1]))[1]


def groups_by_pm(u):
    import usum_models as UM
    import gamedetect
    g = gamedetect.models_garc(u.r)
    gr = UM.scan_groups(g)
    by = {}
    for k, (pm, form) in gr.items():
        by.setdefault((pm, form), []).append(k)
    return by, UM.dex_map()


def model_group(by, dexmap, nat, code):
    pm = nat if nat <= 649 else {v: k for k, v in dexmap.items()}.get(nat)
    gs = by.get((pm, code))
    return gs[0] if gs else None


def read_tables(pack):
    pkx, other = [], []
    for ln in open(os.path.join(pack, 'tables.txt')).read().split('\n'):
        if ln.startswith('pkx '):
            pkx.append(ln)
        elif ln.strip() and not ln.startswith(('format', 'pkxcount')):
            other.append(ln)
    return pkx, other


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--list', default=os.path.join(HERE, 'gen57_forms.json'))
    ap.add_argument('--jobs', type=int, default=14)
    ap.add_argument('--no-models', action='store_true')
    ap.add_argument('--only', default='', help='kinds to build, comma separated (alola,mega,primal,battle)')
    ap.add_argument('--tm-cache', default=None)
    a = ap.parse_args()
    plan = json.load(open(a.list, encoding='utf-8'))
    only = set(x for x in a.only.split(',') if x)
    forms = [f for f in plan['forms'] if not only or f['kind'] in only]
    u = D.Usum(a.rom)
    pack = os.path.abspath(a.pack)
    cache = a.tm_cache or os.path.join(os.path.dirname(pack), 'xd_machines.json')
    c = json.load(open(cache))
    tm_moves, tutors = c['tm_moves'], c['tutors']
    mn, _cw = A.load_tables()
    defined = set(range(468, 729))
    in_pack = set(range(1, A.LAST + 1))
    pre = {e.species for n in range(1, A.LAST + 1) for e in u.evolutions(n)}
    items_usum = json.load(open(os.path.join(HERE, 'gen57_mega_items.json'))) if os.path.exists(os.path.join(HERE, 'gen57_mega_items.json')) else {}
    stone_items = {i: u.item_name(i, 'en') for i in range(u.item_count()) if re.search(r'ite( [XY])?$', u.item_name(i, 'en')) and 656 <= i <= 770}
    rows_all, report = [], []
    pkx_old, other_tables = read_tables(pack)
    first_pkx = max([int(x.split()[1]) for x in pkx_old] + [P.GEN4_PKX_END - 1]) + 1
    toc_rows, pkx_new, mreport = [], [], {}
    import pbr_models as PM
    import usum_models as UM
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, 'GXXE01', 'XD')
    xdmap = UM.xd_species_models(d)
    standin = A.pick_standins(u, xdmap)
    tmp = pack + '.forms.tmp'
    if os.path.isdir(tmp) and os.environ.get('GEN57_FORMS_RESUME') != '1':
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'files'), exist_ok=True)

    prepared, jobs = [], []
    for k, f in enumerate(forms):
        nat, form, kind = f['nat'], f['form'], f['kind']
        base = u.personal(nat)
        record = base.form_stats_index + form - 1
        name = u.species_name(nat, 'en')
        key = form_key(name, kind, form, nat, f.get('label'))
        notes = []
        sh = standin.get(nat, nat)
        if sh not in xdmap and nat not in xdmap:
            sh = standin_old(u, xdmap, nat)
        pend = []
        row, rep = A.build_row(u, nat, sh, defined, tm_moves, tutors, pre, pend, record=record, pkx_row=first_pkx + k)
        notes += rep.pop('notes', [])

        row['copy'] = name
        if kind == 'alt' and nat == 479 and form in ROTOM_SIGNATURE:
            add_signature_move(row, A.move_row(ROTOM_SIGNATURE[form], defined))
        prepared.append(dict(f=f, key=key, record=record, row=row, rep=rep, notes=notes, name=name, k=k))
        code = f.get('model') or ((MODEL_FORM_CODE[kind] % form) if '%d' in MODEL_FORM_CODE.get(kind, '') else MODEL_FORM_CODE.get(kind, '00'))
        f['_code'] = code
        nm = 'n3f_%d_%d' % (nat, form)
        gid = PKX_GROUP_FIRST + len(jobs)
        jobs.append((nat, form, code, record, sh, A.model_number(nat, mn), nm, gid, tmp, mn))

    statuses = {}
    if not a.no_models:
        with cf.ProcessPoolExecutor(max_workers=min(a.jobs, 14), initializer=_init, initargs=(a.rom, a.xd_disc)) as ex:
            futs = {ex.submit(_convert_form, j): j for j in jobs}
            for i, f in enumerate(cf.as_completed(futs)):
                job = futs[f]
                st, info = f.result()
                statuses[job[6]] = (st, info)
                print('form model %d/%d: %s %s' % (i + 1, len(jobs), job[6], st if st != 'ok' else '%d tris %d bytes' % (info['tris'], info['size'])), flush=True)
    for pr, job in zip(prepared, jobs):
        row, notes, key = pr['row'], pr['notes'], pr['key']
        nm, gid = job[6], job[7]
        if a.no_models:
            pkx_new.append('pkx %d 0 0' % row['pkxDataId'])
            continue
        st, info = statuses[nm]
        if st == 'ok':
            toc_rows.append((gid, 'pkx_' + nm))
            pkx_new.append('pkx %d %d %d' % (row['pkxDataId'], gid, P.PKX_RES))
            mreport[key] = info
        else:
            notes.append({'nogroup': 'no model group for %s form %s: the form keeps its base species model' % (pr['name'], job[2]),
                          'noshell': 'no XD model shell for %s: base model kept' % pr['name'],
                          'overcap': 'model over the 614400-byte file cap (%s bytes): base model kept' % (info.get('size') if isinstance(info, dict) else info),
                          'error': 'model conversion failed (%s): base model kept' % info}[st])
            row['pkxDataId'] = None

    form_lines = ['format 1']
    item_rows = {}
    ability_ids = set()
    for pr in prepared:
        f, key, row = pr['f'], pr['key'], pr['row']
        kind = f['kind']
        for x in row['tokuseiDataId']:
            if x > 123:
                ability_ids.add(x)
        if row.get('pkxDataId') is None:
            row.pop('pkxDataId')
        line = 'form %s|%s|%s' % (key, pr['name'], kind)
        if kind == 'mega':
            sid, sname = stone_for(u, stone_items, pr['name'], f['form'], f['nat'])
            if sid is None:
                pr['notes'].append('no Mega Stone found')
            else:
                item_rows[sname] = dict(copy=plan.get('stone_copy', 'Moon Stone'), price=plan.get('stone_price', 0),
                                        id=sid, nat=f['nat'], mega=True)
                line += '|' + sname
        elif kind == 'primal':
            oid = {382: 535, 383: 534}[f['nat']]
            oname = u.item_name(oid, 'en')
            item_rows[oname] = dict(copy=plan.get('stone_copy', 'Moon Stone'), price=0, id=oid, nat=f['nat'])
            line += '|' + oname
        form_lines.append(line)
    if any(pr['f']['kind'] == 'alola' for pr in prepared):

        item_rows['Alola Charm'] = dict(copy=plan.get('stone_copy', 'Moon Stone'), price=0, id=None, nat=0,
                                       text={'en': ('ALOLA CHARM', 'A charm from the Alola region.{00}Held by a Pokemon that is{00}about to evolve, it picks the{00}Alolan form.')})
        form_lines.append('charm Alola Charm')

    langs = ['us'] + list(P.LANG_DIRS)
    for lg in langs:
        dump_lang = 'en' if lg == 'us' else P.LANG_DIRS[lg]
        folder = 'data' if lg == 'us' else 'data_' + lg
        species, items = {}, {}
        for pr in prepared:
            f, key, row = pr['f'], pr['key'], dict(pr['row'])
            kind = f['kind']
            row['text'] = {'name': shown_form(u.species_name(f['nat'], dump_lang), kind, f['nat'], f['form']),
                           'typeName': P.text_for(u, f['nat'], dump_lang)['typeName']}
            if kind in ('alola', 'battle') and f.get('tag'):
                row['text']['typeName'] = row['text']['typeName']

            rec_evo = [e for e in u.evolutions(pr['record']) if e.species in in_pack | {x['nat'] for x in forms} or e.species <= 649]
            row['sinka.pokemonDataId'] = list(row['sinka.pokemonDataId'])
            slot = 0
            for e in u.evolutions(pr['record']):
                if slot >= 5 or row['sinka.kind'][slot] == 0:
                    break
                if e.form and kind == 'alola':
                    row['sinka.pokemonDataId'][slot] = form_key(u.species_name(e.species, 'en'), 'alola', e.form, e.species)
                else:
                    row['sinka.pokemonDataId'][slot] = u.species_name(e.species, 'en')
                slot += 1
            species[key] = row
        stone_names = {n for n, i in item_rows.items() if i.get('mega')}
        for nm, it in item_rows.items():
            if it['id'] is None:
                nm_t, doc = CHARM_DOC[dump_lang]
                items[nm] = {'copy': it['copy'], 'text': {'name': nm_t, 'doc': doc}}
                items[nm]['coupon'] = plan.get('charm_coupon', CHARM_COUPON)
                continue
            doc = stone_doc(u, it['id'], dump_lang, it['nat'])
            items[nm] = {'copy': it['copy'], 'text': {'name': P.shown(u.item_name(it['id'], dump_lang)), 'doc': doc}}
            if it['price']:
                items[nm]['price'] = it['price']
            if nm in stone_names:
                items[nm]['coupon'] = plan.get('stone_coupon', STONE_COUPON)
        os.makedirs(os.path.join(pack, folder), exist_ok=True)
        add = {}
        if items:
            add['items'] = items
        add['pokemon'] = species
        json.dump({'_note': "made by gen57_forms.py from the player's own dump: game data, never in the repository", 'add': add},
                  open(os.path.join(pack, folder, '03_forms.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)

    pk = ['format 1', 'pkxcount 419'] + pkx_old + [x for x in pkx_new if not x.endswith(' 0 0') or a.no_models] + other_tables
    open(os.path.join(pack, 'tables.txt'), 'w').write('\n'.join(pk) + '\n')
    if toc_rows:
        import gen4_pack as G4
        toc_path = [os.path.join(pack, 'files', n) for n in os.listdir(os.path.join(pack, 'files')) if n.lower() == 'gsfsys.toc'][0]
        old_toc = open(toc_path, 'rb').read()
        open(toc_path, 'wb').write(G4.extend_toc(old_toc, toc_rows))
        for n in os.listdir(os.path.join(tmp, 'files')):
            shutil.copy(os.path.join(tmp, 'files', n), os.path.join(pack, 'files', n))
    ab_path = os.path.join(pack, 'abilities.txt')
    have = set()
    lines = open(ab_path, encoding='utf-8').read().rstrip('\n').split('\n') if os.path.exists(ab_path) else ['format 1']
    for ln in lines[1:]:
        have.add(int(ln.split('\t')[0]))
    add_ids = sorted(ability_ids - have)
    if add_ids:
        lines += P.ability_lines(u, add_ids).split('\n')[1:-1]
        open(ab_path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    open(os.path.join(pack, 'forms.txt'), 'w', encoding='utf-8').write('\n'.join(form_lines) + '\n')
    info_path = os.path.join(pack, 'gen57.json')
    info = json.load(open(info_path))
    info['forms'] = len(prepared)
    info['forms_version'] = FORMS_VERSION
    json.dump(info, open(info_path, 'w'), indent=1)
    rep = [dict(key=pr['key'], record=pr['record'], kind=pr['f']['kind'], types=pr['rep']['types'], abilities=pr['rep']['abilities'],
                bst=pr['rep']['bst'], notes=pr['notes'], model=mreport.get(pr['key'])) for pr in prepared]
    json.dump(rep, open(os.path.join(pack, 'forms_report.json'), 'w'), indent=1)
    shutil.rmtree(tmp, ignore_errors=True)
    print('forms', len(prepared), 'rows;', len(item_rows), 'items;', len(toc_rows), 'models;', len(add_ids), 'new ability names')


if __name__ == '__main__':
    main()
