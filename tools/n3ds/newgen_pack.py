"""Test-only "new generation" demo pack: a few Gen 5 / 6 / 7 Pokemon as NEW species rows in XD, from the player's own
Ultra Sun / Ultra Moon dump (never committed; output goes under build\\).

  python -I newgen_pack.py --rom <decrypted Ultra Sun / Ultra Moon .cci> --xd-disc <XD iso> --out <pack folder> [--species a,b]

The pack folder uses the layout of the Gen 4 pack's folder (docs/GEN4.md; games/xd/src/gen4_pack.cpp reads it, as the extra
assets layer "pbr_gen4", when the rule gen4_rules is on): files/GSfsys.toc (+ model archives), data/02_species.json (the
"add" block, docs/modding/add-pokemon-moves-items.md), tables.txt (model rows), gen4.json. Put it in a SCRATCH settings folder as
extra_assets/pbr_gen4 (never over a player's real Gen 4 pack) and run with XD_CHALLENGES=gen4_rules.

What a row has: the model (converted, improved pass), base stats, types, up to two abilities, catch rate, growth, base
experience, height, weight, a level-up move list; no evolutions, no TM / tutor flags beyond the stand-in's, cry / face /
Pokedex entry of the stand-in (the row copies an XD species). Moves are the Gen 1-3 moves XD has (ids up to 354): newer
moves in the learn-set are dropped and a type-based set fills it. Abilities above XD's 76 become one XD already has.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE))) if False else os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
for sub in ('pbr_survey', 'pbr_convert'):
    for base in (os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub), os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', sub)):
        if os.path.isdir(base) and base not in sys.path:
            sys.path.insert(0, base)

import ncch, garc, gf_to_xd, gamedetect
import make_proto as M


SPECIES = {



    497: ('Serperior', 'arbok', 24, False, None),
    503: ('Samurott', 'kamex', 9, False, None),
    571: ('Zoroark', 'kairiky', 68, False, None),
    635: ('Hydreigon', 'kairyu', 149, True, 878),
    658: ('Greninja', 'kairiky', 68, False, 912),
    655: ('Delphox', 'kairiky', 68, False, 909),
    652: ('Chesnaught', 'kairiky', 68, False, 906),
    778: ('Mimikyu', 'pikachu', 25, False, 1133),
}

TYPE_XD = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 10, 10: 11, 11: 12, 12: 13, 13: 14, 14: 15, 15: 16, 16: 17, 17: None}

FILL = {0: [33, 98, 129, 63], 1: [2, 67, 238, 280], 2: [16, 17, 332, 19], 3: [40, 124, 188, 92], 4: [189, 91, 89, 89],
        5: [88, 157, 246, 157], 6: [141, 324, 318, 210], 7: [122, 247, 101, 325], 8: [232, 231, 211, 309],
        10: [52, 53, 83, 126], 11: [55, 61, 57, 56], 12: [22, 75, 72, 76], 13: [84, 85, 86, 87], 14: [93, 94, 60, 347],
        15: [181, 58, 8, 59], 16: [82, 337, 239, 200], 17: [44, 228, 242, 185]}
MAX_MOVE = 354
MAX_ABILITY = 76
FALLBACK_ABILITY = 46


def decode_text(d):
    nsec, nlines, total, key0, off = struct.unpack_from('<HHIII', d, 0)
    base = off
    out = []
    for i in range(nlines):
        o, ln = struct.unpack_from('<iH', d, base + 4 + i * 8)
        key = (0x7C89 + 0x2983 * i) & 0xffff
        seg = d[base + o: base + o + ln * 2]
        ch = []
        for k in range(ln):
            c = struct.unpack_from('<H', seg, k * 2)[0] ^ key
            key = ((key << 3) | (key >> 13)) & 0xffff
            ch.append(c)
        out.append(''.join(chr(c) for c in ch if c))
    return out


def species_row(info, pm, label, standin_id, model_row, tables):
    pers = tables['personal'](pm)
    hp, atk, de, spe, spa, spd = pers[0:6]
    t1, t2 = TYPE_XD.get(pers[6]), TYPE_XD.get(pers[7])
    if t1 is None:
        t1 = t2 if t2 is not None else 0
    if t2 is None:
        t2 = t1
    ev = struct.unpack_from('<H', pers, 0x0A)[0]
    evs = [(ev >> s) & 3 for s in (0, 2, 4, 8, 10, 6)]
    abil = [a for a in pers[0x18:0x1B]]
    mapped = []
    for a in abil[:2]:
        mapped.append(a if 0 < a <= MAX_ABILITY else 0)
    if mapped[0] == 0:
        mapped[0] = mapped[1] or FALLBACK_ABILITY

    pairs = tables['learn'](pm)
    moves = []
    for mv, lv in pairs:
        if 0 < mv <= MAX_MOVE and lv >= 1 and mv not in [m for _, m in moves]:
            moves.append((lv, mv))
    fill = FILL.get(t1, FILL[0]) + FILL.get(t2, [])
    lvl = 1
    for k, mv in enumerate(fill):
        if mv not in [m for _, m in moves] and len(moves) < 12:
            moves.append((min(1 + 12 * k, 60) if k else 1, mv))
    moves.sort()
    moves = moves[:20]
    waza_level = [lv for lv, _ in moves] + [0] * (20 - len(moves))
    waza_id = [mv for _, mv in moves] + [0] * (20 - len(moves))
    row = {
        'copy': standin_id,
        'growDataId': pers[0x15], 'get': pers[8], 'sexRatio': pers[0x12],
        'giveExp': struct.unpack_from('<H', pers, 0x22)[0], 'initFriend': pers[0x14],
        'height': struct.unpack_from('<H', pers, 0x24)[0], 'weight': struct.unpack_from('<H', pers, 0x26)[0],
        'zokuseiDataId': [t1, t2], 'tokuseiDataId': mapped,
        'itemDataId': [0, 0],
        'basisStatus': [hp, atk, de, spa, spd, spe] + evs,
        'sinka.kind': [0] * 5, 'sinka.buff': [0] * 5, 'sinka.pokemonDataId': [0] * 5,
        'waza.level': waza_level, 'waza.dataId': waza_id,
        'pkxDataId': model_row,
        'text': {'name': label.upper()},
    }
    return row, dict(types=(pers[6], pers[7]), xd_types=(t1, t2), abilities=abil, mapped=mapped, bst=sum(pers[0:6]), moves=moves)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--species', default=','.join(str(k) for k in SPECIES))
    ap.add_argument('--decimate', type=int, default=2600)
    a = ap.parse_args()
    import pbr_models as PM
    import noddisc
    import gen4_pack as G4
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, 'GXXE01', 'XD')
    r, prod = ncch.open_rom(a.rom)
    det = gamedetect.detect(r, prod)
    print('source game:', det['name'], det['status'])
    pg = gamedetect.archive(r, det, 'personal')
    lg = gamedetect.archive(r, det, 'levelup')
    tg = gamedetect.archive(r, det, 'text')
    names = decode_text(garc.maybe(tg.read(det['text_species'])))
    tables = {'personal': lambda n: garc.maybe(pg.read(n)),
              'learn': lambda n: list(zip(*[iter(struct.unpack_from('<%dH' % (len(garc.maybe(lg.read(n))) // 2), garc.maybe(lg.read(n))))] * 2))}
    out = os.path.abspath(a.out)
    tmp = out + '.tmp'
    import shutil
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'files'))
    os.makedirs(os.path.join(tmp, 'data'))
    toc_name = [k for k in d.files if k.lower() == 'gsfsys.toc'][0]
    toc = d.file(toc_name)
    toc_rows, add, pkx_rows, report = [], {}, [], []
    for k, pm in enumerate(int(x) for x in a.species.split(',')):
        label, arch, standin, hov, grp = SPECIES[pm]
        assert names[pm].lower().startswith(label.lower()[:5]), (names[pm], label)
        data = d.file('pkx_%s.fsys' % arch)
        ent = [e for e in noddisc.fsys_entries(data) if e.type == 15][0]
        xd_bytes = noddisc.extract(data, ent)
        gm, texs, mots, group = M.load_species(r, pm, grp)
        print(label, 'group', group, 'roles', {kk: v['frames'] for kk, v in mots.items()})
        pers = tables['personal'](pm)
        height_cm = struct.unpack_from('<H', pers, 0x24)[0]

        pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lambda s: print('   ' + s) if s.startswith(('INFO: height', 'INFO: size', 'INFO: grounding')) else None,
                                     opts=dict(ground=True, decimate=a.decimate, fire=True, light=1.0, eyes=True,
                                               size_rule='mainline', height_cm=height_cm, hover_rule='model'),
                                     hover=None)
        name = 'n3_' + label.lower()
        gid = G4.PKX_GROUP_FIRST + k
        fs = G4.build_fsys(gid, [(G4.PKX_RES, 15, name, pkx)])
        open(os.path.join(tmp, 'files', 'pkx_%s.fsys' % name), 'wb').write(fs)
        toc_rows.append((gid, 'pkx_' + name))
        row_no = G4.PKX_COUNT + k
        pkx_rows.append('pkx %d %d %d' % (row_no, gid, G4.PKX_RES))
        row, rep = species_row(info, pm, label, standin, row_no, tables)
        add[label] = row
        rep.update(label=label, pm=pm, tris=info['tris'], file=info['size'], new_number=415 + k)
        report.append(rep)
        print('  ', rep)
    open(os.path.join(tmp, 'files', toc_name), 'wb').write(G4.extend_toc(toc, toc_rows))
    json.dump({'_note': 'newgen_pack.py demo: game data made from the player\'s own dump, never in the repository',
               'add': {'pokemon': add}}, open(os.path.join(tmp, 'data', '02_species.json'), 'w'), indent=1)
    open(os.path.join(tmp, 'tables.txt'), 'w').write('\n'.join(['format 1', 'pkxcount %d' % G4.PKX_COUNT] + pkx_rows) + '\n')
    json.dump(dict(id='battle-revolution-gen4', version=G4.PACK_VERSION, species=len(add), models=len(add), cries=0,
                   game='GXXE01', generated_from='CTR-P-A2AA', demo=True), open(os.path.join(tmp, 'gen4.json'), 'w'), indent=1)
    json.dump(report, open(os.path.join(tmp, 'report.json'), 'w'), indent=1)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.rename(tmp, out)
    print('pack', out, len(add), 'species')


if __name__ == '__main__':
    main()
