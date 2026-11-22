"""The Gen 5-7 pack for ALL 314 species (494..807), made from the player's own Ultra Sun / Moon dump (docs/GEN57.md).

  python -I gen57_all.py --rom <usum.cci> --xd-disc <XD iso> --gen4-pack <extra_assets/pbr_gen4> --out <pack folder>
         [--jobs 16] [--only 498,506] [--no-models] [--defined-moves defined.json] [--cries]

Rules (numbers only, no game data in the repository):
  * species row = national number + 26 (Gen 5 starts at 520, 494 + 26), model row = national number + 31 (Gen 5 starts at 525).
  * model number: species below 650 use their national number, 650 and up use tools/n3ds/gen57_model_numbers.json (the game's own
    model / icon numbering); the 3DS model group is found by that number.
  * moves: USUM ids 1..354 are XD's, 355..467 the Gen 4 pack's (+20), 468..728 = XD row id + 20 (gen57_moves.py writes all 261 of them, in
    USUM order, into the same pack: data/01_moves.json, battle/moves.json, battle/abilities.json, abilities.txt for the 109 abilities).
    --defined-moves <json list of USUM ids> narrows the ids that count as written (default: all 468..728); a learnset move whose row is
    not written is left out of the species row and listed in pending_moves.json.
  * evolutions: level / friendship / trade as XD's records know them; every other method becomes a level (see EVO_NOTES) and is listed
    in report.json. A target outside the pack (no Gen 7 form of an older species) is left out.
  * stand-in species: a Gen 1-2 species of the same type and the nearest height whose XD model is the shell the converted model is built in
    and whose row is copied (the cry stays the stand-in's unless --cries, the Dex marks too).
  * faces: drawn from the converted models (gen57_faces.py: the game's icon renderer, head-centred, round, 84 x 84; needs xd.exe: --xd-exe);
    the box icons of the dump (GARC a/2/5/7, 64 x 32, stored upside down) cut and enlarged stay as the fallback (faces.json says who used what).
Models are converted in parallel (--jobs processes, never more than 16) and kept in <out>.tmp\files so an interrupted run resumes.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_pack as P
import gen57_data as D
import gen57_moves as MV
import gen57_abilities as AB

FIRST, LAST = 494, 807
SLOW_FIRST = {655, 724, 727, 764, 780, 791, 805, 806}
ROW_SHIFT = 26
PKX_SHIFT = 31
FAIRY_SPECIES_OLD = {35, 36, 39, 40, 173, 174, 175, 176, 183, 184, 209, 210, 122, 468}
EVO_NOTES = ('USUM methods: 1-3 friendship (kept, 1), 4 level (kept), 5 trade (kept), 9-11 stat comparison (kept as 8-10), '
             '12 / 13 Silcoon / Cascoon, 14 Ninjask, 15 Shedinja, 16 beauty (kept as the Gen 3 kinds), 6 trade with item, 7 trade for species, '
             '8 / 17 / 18 / 19 / 20 use item (stones), 21 knows a move, 22 species in the party, 23 / 24 level by gender, 25-... places and '
             'special rules: XD cannot do the item / place ones, so they become LEVEL 36 (22 for a first stage, 40 for a second): '
             'the Pokemon evolves by level in Orre instead')


def model_number(nat, table):
    return nat if nat < 650 else int(table[str(nat)])


def load_tables():
    mn = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json')))['model_number']
    cw = json.load(open(os.path.join(HERE, 'gen57_cry_waves.json')))
    return mn, cw


def pick_standins(u, xdmap):
    """{nat: standin nat}: same type (XD's, Fairy -> the other type), nearest height among Gen 1-2 species with an XD model."""
    pool = {}
    for n in range(1, 252):
        if n not in xdmap or n in FAIRY_SPECIES_OLD:
            continue
        p = u.personal(n)
        t = D.TYPE_XD.get(p.type1)
        if t is None:
            continue
        pool.setdefault(t, []).append((p.height_cm, n))
    out = {}
    for nat in range(FIRST, LAST + 1):
        p = u.personal(nat)
        t = D.TYPE_XD.get(p.type1)
        if t is None:
            t = D.TYPE_XD.get(p.type2)
        if t is None:
            t = 0
        cands = pool.get(t) or pool[0]
        out[nat] = min(cands, key=lambda c: (abs(c[0] - p.height_cm), c[1]))[1]
    return out




def move_row(usum_id, defined):
    if 1 <= usum_id <= P.GEN3_MOVES:
        return usum_id
    if P.GEN3_MOVES < usum_id <= P.GEN4_LAST:
        return usum_id + P.GEN4_SHIFT
    if 468 <= usum_id <= 728 and usum_id in defined:
        return usum_id + 20
    return None


def evo_pairs(u, nat, pre, rec=None):
    out, notes = [], []
    for e in u.evolutions(nat if rec is None else rec):
        tname = u.species_name(e.species)
        m = e.method
        if m in (1, 2, 3):
            kind, buff = 1, 0
        elif m == 4:
            kind, buff = 4, e.level
        elif m == 5:
            kind, buff = 5, 0
        elif m in (9, 10, 11):
            kind, buff = m - 1, e.arg
        elif m in (12, 13):
            kind, buff = m - 1, e.level or 20
        else:
            lvl = e.level or (22 if nat not in pre else 40)
            kind, buff = 4, lvl
            notes.append('evolution to %s: USUM method %d (%s) has no XD kind -> level %d' % (tname, m, e.arg, lvl))
        out.append((kind, buff, tname))
    return out[:5], notes


def build_row(u, nat, standin, defined, tm_moves, tutors, pre, pending, record=None, pkx_row=None):
    """One species row. `record` = the dump's own personal / learn-set / evolution record of an alternate form (Alolan, Mega: gen57_forms.py);
    `nat` stays the base species' number. `pkx_row` = the model row (default national + 31)."""
    rec = nat if record is None else record
    p = u.personal(rec)
    notes = []
    t1, t2 = D.TYPE_XD.get(p.type1), D.TYPE_XD.get(p.type2)
    if t1 is None:
        t1 = t2 if t2 is not None else 0
        notes.append('Fairy type: shown as the other type (Normal when Fairy only); Modern types shows the real one')
    if t2 is None:
        t2 = t1
    ab = [p.abilities[0], p.abilities[1] if p.abilities[1] else 0]
    learn, seen = [], set()
    for lv, mv in u.levelup(rec):
        row = move_row(mv, defined)
        if row is None:
            if mv >= 468:
                pending.append((nat, max(lv, 1), mv))
            continue
        if row in seen:
            continue
        seen.add(row)
        learn.append((max(lv, 1), row))
    learn.sort()
    learn = learn[:20]
    ev = p.ev
    evo, enotes = evo_pairs(u, nat, pre, rec)
    notes += enotes
    evo += [(0, 0, 0)] * (5 - len(evo))
    wazaMcn, teach = P.machine_bits({m for _, m in learn}, tm_moves, tutors)
    row = {
        'copy': standin,
        'growDataId': p.growth, 'get': p.catch_rate, 'sexRatio': p.gender, 'giveExp': min(p.base_exp, 255), 'initFriend': p.friendship,
        'height': max(1, round(p.height_cm / 10)), 'weight': p.weight_hg,
        'numPokemon': nat, 'numZukan': nat,
        'zokuseiDataId': [t1, t2], 'tokuseiDataId': ab,
        'wazaMcn': wazaMcn, 'teachWaza': teach,
        'itemDataId': [0, 0],
        'basisStatus': [p.hp, p.atk, p.defn, p.spa, p.spd, p.spe, ev['hp'], ev['atk'], ev['def'], ev['spa'], ev['spd'], ev['spe']],
        'sinka.kind': [x[0] for x in evo], 'sinka.buff': [x[1] for x in evo], 'sinka.pokemonDataId': [x[2] for x in evo],
        'waza.level': [lv for lv, _ in learn] + [0] * (20 - len(learn)), 'waza.dataId': [mv for _, mv in learn] + [0] * (20 - len(learn)),
        'pkxDataId': nat + PKX_SHIFT if pkx_row is None else pkx_row,
    }
    return row, dict(types=(p.type1, p.type2), abilities=list(p.abilities), mapped=ab, bst=p.bst(), moves=learn, notes=notes)




def icon_faces(u, out_dir, mn):
    """textures/faces/<nat>.png for every species: the dump's box icon (GARC a/2/5/7, 64x32 RGBA5551, upside down)."""
    import numpy as np
    from PIL import Image
    import garc
    import gf_read
    g = u._archive('a/2/5/7')
    num2nat = {model_number(n, mn): n for n in range(FIRST, LAST + 1)}
    best = {}
    for i in range(len(g.entries)):
        b = garc.maybe(g.read(i))
        m = re.search(rb'poke_icon_(\d{3})_([a-z0-9_]*?)_001', b[:0x80])
        if not m or int(m.group(1)) not in num2nat:
            continue
        rank = 0 if m.group(2) in (b'm', b'f') else 1 if m.group(2).startswith(b'normal') else 2
        nat = num2nat[int(m.group(1))]
        if nat in best and best[nat][0] <= rank:
            continue
        w, h, fmt = struct.unpack_from('<HHH', b, 0x68)
        best[nat] = (rank, gf_read.decode_texture(dict(w=w, h=h, fmt=fmt, data=b[0x80:]))[2][::-1].copy())
    got = {n: v[1] for n, v in best.items()}
    os.makedirs(out_dir, exist_ok=True)
    made = {}
    for nat, a in got.items():
        im = Image.fromarray(a)
        box = im.getbbox()
        if box is None:
            continue
        im = im.crop(box)
        side = max(im.size) + 2
        sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
        sq.paste(im, ((side - im.size[0]) // 2, (side - im.size[1]) // 2))
        sq.resize((84, 84), Image.LANCZOS).save(os.path.join(out_dir, '%d.png' % nat))
        made[nat] = 'textures/faces/%d.png' % nat
    return made




SND_DATA_COUNT, SND_FILE_COUNT, WAVE_OLD, CRY_GROUP = 1507, 516, 389, 1966


def xd_voices(d):
    """{national number <= 251: the stand-in's snd_data row (the species record's voice field)}"""
    import noddisc
    import usum_models as UM
    data = d.file('common.fsys')
    ent = [e for e in noddisc.fsys_entries(data) if e.name == 'common_rel'][0]
    rel = noddisc.extract(data, ent)
    sig = struct.pack('>6H', 35, 55, 30, 50, 40, 90)
    base = rel.find(sig) - 0x8E - 25 * UM.REC
    return {n: struct.unpack_from('>H', rel, base + n * UM.REC + 0x0C)[0] for n in range(1, 252)}


def make_cries(u, d, tmp, a, nats, standins, cw, rows_out, species_edit):
    """Cry streams (header + body members) in a copy of the Gen 4 pack's cry archive, snd rows after the Gen 4 ones, the PDA waveforms."""
    import noddisc
    import n3ds_cries as C
    import gen4_cry
    import gen4_pack as G4
    g4 = open(os.path.join(a.gen4_pack, 'tables.txt')).read().splitlines()
    n4 = sum(1 for l in g4 if l.startswith('row snd_file '))
    w4 = sum(1 for l in g4 if l.startswith('wave '))
    waves = C.cry_waves(a.rom)
    voices = xd_voices(d)
    cry_name = 'stm_se_nakigoe_archive.fsys'
    g4cry = open(os.path.join(a.gen4_pack, 'files', cry_name), 'rb').read()
    old = noddisc.fsys_entries(g4cry)
    ents = [(e.word0, e.type, e.name, noddisc.stored(g4cry, e), e.size, e.packed) for e in old]
    used = {e.word0 >> 16 for e in old}
    new_h, new_b, notes = [], [], {}
    j = 0
    for nat in nats:
        w = cw.get(str(nat))
        if w is None:
            continue
        hdr, body, nt = gen4_cry.build_xd(C.parse_cwav(waves[w]))
        n = n4 + j
        hid, bid = 0x2000 + 2 * j, 0x2001 + 2 * j
        assert hid not in used and bid not in used, 'cry member id clash'
        new_h.append(((hid << 16) | (20 << 9), 20, '(null)', hdr))
        new_b.append(((bid << 16) | (21 << 9), 21, '(null)', body))
        rows_out.append('row snd_file %d 1 -1 0:4:%d 4:4:%d 8:4:%d' % (SND_FILE_COUNT + n, CRY_GROUP, (hid << 16) | (20 << 9), (bid << 16) | (21 << 9)))
        rows_out.append('row snd_data %d 1 %d 6:2:%d' % (SND_DATA_COUNT + n, voices[standins[nat]], SND_FILE_COUNT + n))
        rows_out.append('wave %d %s' % (nat, gen4_cry.wave_disp(hdr, body).hex()))
        species_edit[nat] = dict(nakigoeId=WAVE_OLD + w4 + j, voice=SND_DATA_COUNT + n)
        notes[nat] = dict(wave=w, notes=nt)
        j += 1
    hdrs = [e for e in ents if e[1] == 20] + new_h
    bods = [e for e in ents if e[1] == 21] + new_b
    rest = [e for e in ents if e[1] not in (20, 21)]
    open(os.path.join(tmp, 'files', cry_name), 'wb').write(G4.build_fsys(struct.unpack_from('>I', g4cry, 8)[0], hdrs + bods + rest))
    return notes




SPOTS57 = {
    'esabapokemondata_E1': [524, 529, 551, 610, 498],
    'esabapokemondata_E2': [540, 548, 506, 722, 495],
    'esabapokemondata_E3': [527, 532, 595, 633, 501],
}


NL = chr(10)


def spots_files(tmp):
    import gen4_spots as S4
    data = {'_note': 'Poke Spot odds with the Gen 4 and Gen 5-7 sets: the originals 45 %, the five Gen 4 entries 5 % each, the five Gen 5-7 entries 6 % each'}
    rows = []
    for table, (_p, adds, top) in S4.SPOTS.items():
        orig = [24, 16, 5]
        assert sum(orig) + 5 * len(adds) + 6 * 5 == 100
        d = {'#%d' % i: {'prob, rate': [orig[i], S4.ORIGINAL_RATES[table][i]]} for i in range(3)}
        for k, (_n, _pr, rate_of) in enumerate(adds):
            d['#%d' % (3 + k)] = {'prob, rate': [5, S4.ORIGINAL_RATES[table][rate_of]]}
        data[table] = d
        for k, nat in enumerate(SPOTS57[table]):
            rate_of = (0, 1, 2, 2, 2)[k]
            rows.append('row %s %d 1 %d 0:1:10 1:1:%d 2:2:%d 4:4:%d 8:4:%d' % (table, 8 + k, rate_of, top, nat + ROW_SHIFT, 6, S4.ORIGINAL_RATES[table][rate_of]))
    os.makedirs(os.path.join(tmp, 'optional'), exist_ok=True)
    json.dump(data, open(os.path.join(tmp, 'optional', 'spots.json'), 'w'), indent=1)
    open(os.path.join(tmp, 'optional', 'spots_tables.txt'), 'w').write(NL.join(['format 1'] + rows) + NL)


LEGEND57 = {494} | set(range(638, 650)) | set(range(716, 722)) | {772, 773} | set(range(785, 808))


def randomizer_lines(u, report, rows_by_nat):
    """randomizer.txt (the Gen 4 pack's format, games/xd/randomizer/xd_randomizer.cpp LoadSpeciesFile) + the species lines for the names."""
    out, names = [], []
    for r in sorted(report, key=lambda x: x['nat']):
        nat = r['nat']
        row = rows_by_nat[nat]
        evo = []
        for k in range(5):
            tgt = row['sinka.pokemonDataId'][k]
            tn = next((n for n in range(FIRST, LAST + 1) if u.species_name(n, 'en') == tgt), 0) if tgt else 0
            evo.append(tn + ROW_SHIFT if tn else 0)
        lv = row['waza.level']
        mv = row['waza.dataId']
        out.append('sp %d %d %d %s | %s | %s | %s | %s | %s' % (
            nat + ROW_SHIFT, nat, 1 if nat in LEGEND57 else 0, ' '.join(map(str, row['basisStatus'][:6])),
            ' '.join(map(str, row['zokuseiDataId'])), ' '.join(map(str, row['tokuseiDataId'])), ' '.join(map(str, evo)),
            ' '.join(map(str, lv)), ' '.join(map(str, mv))))
        names.append('species %d %d %s' % (nat, nat + ROW_SHIFT, r['name'].replace(' ', '_')))
    return out, names




_W = {}


def _init(rom, xd_disc):
    import pbr_models as PM
    import usum_models as UM
    PM.find_nod()
    _W['u'] = D.Usum(rom)
    _W['d'] = PM.open_disc(xd_disc, 'GXXE01', 'XD')
    _W['xdmap'] = UM.xd_species_models(_W['d'])


def _convert_one(job):
    nat, standin, mnum, tmp, k = job
    import noddisc
    import gf_to_xd
    import make_proto as M
    import gen4_pack as G4
    u, d, xdmap = _W['u'], _W['d'], _W['xdmap']
    name = 'n3_%d' % nat
    fpath = os.path.join(tmp, 'files', 'pkx_%s.fsys' % name)
    rpath = os.path.join(tmp, 'reports', '%d.json' % nat)
    if os.path.exists(fpath) and os.path.exists(rpath):
        return nat, json.load(open(rpath))
    arch, idx, _n = xdmap[standin]
    data = d.file(arch)
    ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
    xd_bytes = noddisc.extract(data, ents[0])
    try:
        grp = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json'))).get('group_override', {}).get(str(nat))
        try:
            gm, texs, mots, group = M.load_species(u.r, mnum, grp)
        except SystemExit:
            raise
        p = u.personal(nat)


        for side, dec, atol, ctol, quant in ((256, 2600, 1, 1, 0), (256, 2600, 3, 2, 0), (128, 2600, 3, 2, 0), (128, 2600, 8, 3, 0), (128, 2600, 16, 3, 32),
                                             (128, 2600, 16, 4, 16), (128, 2000, 24, 4, 16), (64, 1600, 24, 5, 12), (64, 1200, 32, 6, 8), (64, 900, 32, 6, 8), (64, 600, 32, 6, 8)):
            pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lambda t: None,
                                         opts=dict(ground=True, decimate=dec, fire=True, light=1.0, eyes=True, size_rule='mainline', max_side=side,
                                                   height_cm=p.height_cm, hover_rule='model', match=False, anim_tol=atol, chan_tol=ctol, quant=quant), hover=None)
            if len(pkx) <= 614400:
                break
    except BaseException as e:
        rep = dict(error=repr(e)[:200], model_number=mnum)
        json.dump(rep, open(rpath, 'w'))
        return nat, rep
    gid = P.PKX_GROUP_FIRST + k
    open(fpath, 'wb').write(G4.build_fsys(gid, [(P.PKX_RES, 15, name, pkx)]))
    rep = dict(tris=info['tris'], size=info['size'], group=group, model_number=mnum, gid=gid)
    json.dump(rep, open(rpath, 'w'))
    return nat, rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--gen4-pack', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--only', default='')
    ap.add_argument('--no-models', action='store_true')
    ap.add_argument('--defined-moves', default='')
    ap.add_argument('--tm-cache', default='')
    ap.add_argument('--cries', action='store_true')
    ap.add_argument('--xd-moves-cache', default='', help='json of XD move rows (effect donors); default: xd_moves_cache.json next to --out')
    ap.add_argument('--xd-exe', default='', help='xd.exe of a build with the face renderer: draws the faces from the models (else box icons only)')
    ap.add_argument('--faces-work', default='', help='folder for the face renderer runs (default <out>.faces)')
    a = ap.parse_args()
    jobs = min(a.jobs, 16)
    u = D.Usum(a.rom)
    mn, cw = load_tables()
    defined = set(json.load(open(a.defined_moves))) if a.defined_moves else set(range(468, 729))
    cache = a.tm_cache or os.path.join(os.path.dirname(os.path.abspath(a.out)), 'xd_machines.json')
    c = json.load(open(cache))
    tm_moves, tutors = c['tm_moves'], c['tutors']
    nats = [int(x) for x in a.only.split(',')] if a.only else list(range(FIRST, LAST + 1))
    out = os.path.abspath(a.out)
    tmp = out + '.tmp'
    for sub in ('files', 'data', 'reports', 'textures/faces'):
        os.makedirs(os.path.join(tmp, sub), exist_ok=True)

    import pbr_models as PM
    import usum_models as UM
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, 'GXXE01', 'XD')
    xdmap = UM.xd_species_models(d)
    standins = pick_standins(u, xdmap)
    pre = {e.species for n in range(1, LAST + 1) for e in u.evolutions(n)}

    mreport = {}
    if not a.no_models:
        todo = [(n, standins[n], model_number(n, mn), tmp, n - FIRST) for n in nats]
        todo.sort(key=lambda j: j[0] not in SLOW_FIRST)
        with cf.ProcessPoolExecutor(max_workers=jobs, initializer=_init, initargs=(a.rom, a.xd_disc)) as ex:
            futs = [ex.submit(_convert_one, j) for j in todo]
            for i, f in enumerate(cf.as_completed(futs)):
                nat, rep = f.result()
                mreport[nat] = rep
                print('model %d/%d: %d %s' % (i + 1, len(todo), nat, 'ERROR ' + rep['error'] if 'error' in rep else '%d tris %d bytes' % (rep['tris'], rep['size'])), flush=True)
    toc_name = [k for k in d.files if k.lower() == 'gsfsys.toc'][0]
    import gen4_pack as G4
    g4toc = open(os.path.join(a.gen4_pack, 'files', 'GSfsys.toc'), 'rb').read()
    ok = [n for n in nats if os.path.exists(os.path.join(tmp, 'files', 'pkx_n3_%d.fsys' % n))]
    for n in list(ok):
        r = mreport.get(n) or json.load(open(os.path.join(tmp, 'reports', '%d.json' % n)))
        mreport[n] = r
        if r.get('size', 0) > 614400:
            ok.remove(n)
            r['left_out'] = 'over the 614400-byte file cap (%d bytes): stand-in model' % r['size']
    toc_rows = [(P.PKX_GROUP_FIRST + n - FIRST, 'pkx_n3_%d' % n) for n in ok]
    open(os.path.join(tmp, 'files', toc_name), 'wb').write(G4.extend_toc(g4toc, toc_rows))
    for n in ok:
        pass

    pkx_rows = ['pkx %d %d %d' % (n + PKX_SHIFT, P.PKX_GROUP_FIRST + n - FIRST, P.PKX_RES) if n in ok else 'pkx %d 0 0' % (n + PKX_SHIFT) for n in range(FIRST, LAST + 1)]

    cry_rows, cry_edit, cry_notes = [], {}, {}
    if a.cries:
        cry_notes = make_cries(u, d, tmp, a, nats, standins, cw, cry_rows, cry_edit)
    faces = icon_faces(u, os.path.join(tmp, 'textures', 'faces'), mn)
    pending, report, rows_us = [], [], {}
    mcache = json.load(open(a.xd_moves_cache or os.path.join(os.path.dirname(out), 'xd_moves_cache.json')))['xd']
    mbattle, mreport_moves = None, []
    for lg in ['us'] + list(P.LANG_DIRS):
        dump_lang = 'en' if lg == 'us' else P.LANG_DIRS[lg]
        folder = 'data' if lg == 'us' else 'data_' + lg
        os.makedirs(os.path.join(tmp, folder), exist_ok=True)
        rws, bt, rp = MV.build_all(u, mcache, langs=(dump_lang,))
        moves = {k: {kk: vv for kk, vv in v.items() if not kk.startswith('_')} for k, v in rws[dump_lang].items()}
        if lg == 'us':
            mbattle, mreport_moves = bt, rp
        if moves:
            json.dump({'_note': "made by gen57_all.py from the player's own dump: game data, never in the repository", 'add': {'moves': moves}},
                      open(os.path.join(tmp, folder, '01_moves.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        species = {}
        for nat in nats:
            pend = []
            row, rep = build_row(u, nat, standins[nat], defined, tm_moves, tutors, pre, pend)
            row['text'] = P.text_for(u, nat, dump_lang)
            if nat in faces:
                row['face'] = faces[nat]
            if nat in cry_edit:
                row.update(cry_edit[nat])
            species[u.species_name(nat, 'en')] = row
            if lg == 'us':
                rows_us[nat] = row
            if lg == 'us':
                pending += pend
                rep.update(nat=nat, name=u.species_name(nat, 'en'), row=nat + ROW_SHIFT, standin=standins[nat], model=mreport.get(nat),
                           cry_wave=cw.get(str(nat)), cry=cry_notes.get(nat))
                report.append(rep)
        json.dump({'_note': "made by gen57_all.py from the player's own dump: game data, never in the repository", 'add': {'pokemon': species}},
                  open(os.path.join(tmp, folder, '02_species.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    if len(nats) == LAST - FIRST + 1:
        spots_files(tmp)
    rz_lines, name_lines = randomizer_lines(u, report, rows_us)
    open(os.path.join(tmp, 'randomizer.txt'), 'w').write(NL.join(rz_lines) + NL)
    os.makedirs(os.path.join(tmp, 'battle'), exist_ok=True)
    json.dump({'_note': 'made by gen57_all.py: what the new moves do (docs/modding/battle-effects.md)', 'moves': mbattle},
              open(os.path.join(tmp, 'battle', 'moves.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(mreport_moves, open(os.path.join(tmp, 'moves_report.json'), 'w'), indent=1)
    fairy = [r['name'] for r in mreport_moves if r['type'] == 17]
    abil_ids = list(range(124, u.ability_count()))
    adefs, areport = AB.build_all(u)
    open(os.path.join(tmp, 'abilities.txt'), 'w', encoding='utf-8').write(P.ability_lines(u, abil_ids, {v['id'] for v in adefs.values()}))
    json.dump({'_note': 'made by gen57_all.py: what the new abilities do (docs/modding/battle-effects.md)', 'abilities': adefs},
              open(os.path.join(tmp, 'battle', 'abilities.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(areport, open(os.path.join(tmp, 'abilities_report.json'), 'w'), indent=1)
    open(os.path.join(tmp, 'tables.txt'), 'w').write('\n'.join(['format 1', 'pkxcount 419'] + cry_rows + pkx_rows + name_lines) + '\n')
    json.dump([dict(species=n, level=lv, usum_move=m, xd_row=m + 20) for n, lv, m in pending], open(os.path.join(tmp, 'pending_moves.json'), 'w'))
    json.dump(dict(id='ultra-sun-moon-gen57', version=P.PACK_VERSION, species=len(nats), moves=len(mreport_moves), fairy_moves=fairy, abilities=len(abil_ids), game='GXXE01',
                   generated_from=u.prod, needs='gen4-rows', evolution_rule=EVO_NOTES),
              open(os.path.join(tmp, 'gen57.json'), 'w'), indent=1)
    json.dump(report, open(os.path.join(tmp, 'report.json'), 'w'), indent=1)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.rename(tmp, out)
    if a.xd_exe:
        import gen57_faces as GF
        work = os.path.abspath(a.faces_work or out + '.faces')
        views, why = GF.views_for(u, out, nats, lambda n: model_number(n, mn))
        todo = [n for n in sorted(views) if not os.path.exists(os.path.join(work, 'render', '%d.png' % n))]
        for i in range(0, len(todo), 12):
            part = todo[i:i + 12]
            GF.run_renderer(a.xd_exe, a.xd_disc, out, a.gen4_pack, work, [GF.face_item(n, views[n]) for n in part], 'p%d' % (i // 12))
            print('faces drawn:', [n for n in part if os.path.exists(os.path.join(work, 'render', '%d.png' % n))], flush=True)
        GF.cut(out, work, nats, why, log=print)
    print('pack', out, len(nats), 'species', len(ok), 'models', len(pending), 'pending move entries', len(abil_ids), 'abilities')


if __name__ == '__main__':
    main()
