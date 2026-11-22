"""Tests of the Gen 4 pack from an Ultra Sun / Ultra Moon dump (usum_gen4.py). No game data needed; with USUM_ROM (a decrypted dump)
and PBR_DISC set the data is also compared with Battle Revolution's tables (the numbers must agree except the documented ones).
    python -I tools/n3ds/test_usum_gen4.py"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import usum_gen4 as U
import usum_gen4_values as V
import usum_gen4_donors as D

fails = []


def check(name, ok):
    print(('ok    ' if ok else 'FAIL  ') + name)
    if not ok:
        fails.append(name)


check('types: Gen 7 -> Gen 4 numbers', [U.type7to4(t) for t in (0, 8, 9, 12, 16)] == [0, 8, 10, 13, 17] and U.type7to4(17) is None)
check('evolution methods shift by one from "use item"', [U.evo_method7to4(m) for m in (1, 4, 6, 8, 20, 21, 24, 27, 28, 42)] == [1, 4, 6, 7, 19, 20, 23, 26, 0, 0])
check('Gen 4 machine list: 100 distinct moves', len(U.GEN4_TM) == 100 and len(set(U.GEN4_TM)) == 100)
check('Gen 7 machine list: 100 names', len(U.GEN7_TM_NAMES) == 100)
m = U.mesg(['', 'AB', 'caf\u00e9'])
check('message file layout', m[:4] == b'MESG' and struct.unpack_from('>I', m, 4)[0] == 3 and m[struct.unpack_from(">I", m, 0x18)[0]:].decode('utf-16-be').startswith('caf'))
f = U.build_fsys(5, [(0x1200, 0, 'a', b'12345'), (0x10200, 0, 'b', b'xyz')])
check('fsys container', f[:4] == b'FSYS' and f[-4:] == b'FSYS' and struct.unpack_from('>I', f, 0x20)[0] == len(f) and struct.unpack_from('>I', f, 0xC)[0] == 2)
plan = json.load(open(os.path.join(HERE, 'usum_gen4_plan.json')))['plan']
check('plan: 106 species 387..493 without Bonsly, unique model names', len(plan) == 106 and '438' not in plan and len({v[0] for v in plan.values()}) == 106)
check('donors: 57 Gen 4 moves with a Gen 1-3 donor', len(D.DONOR_OF) == 57 and all(1 <= v <= 354 for v in D.DONOR_OF.values()))
row = bytearray(24)
V.Values(True).apply_move('Draco Meteor', row)
check('Gen 4 values: Draco Meteor power 140', row[15] == 140)
row = bytearray(24)
V.Values(False).apply_move('Draco Meteor', row)
check('values off: power left alone', row[15] == 0)
rec = bytearray(52)
V.Values(True).apply_personal(445, rec, None)
check('Gen 4 values: Garchomp base experience 218', rec[39] == 218)
check('Gen 4 values: Roserade Defense 55, Starapter Sp. Def 50', V.SPECIES[407]['def'] == 55 and V.SPECIES[398]['spd'] == 50)

import usum_gen4_learnsets as L
check('learn-sets: 493 machine lists, 107 level-up lists, Garchomp level-1 moves are Tackle, Sand Attack, Dragon Rage, Sandstorm, Fire Fang',
      len(L.MACHINES) == 493 and len(L.LEVELUP) == 107 and {m for lv, m in L.LEVELUP[445] if lv == 1} >= {33, 28, 82, 201, 424})
check('learn-sets: every Gen 4 move number is at most 467', all(m <= 467 for v in L.LEVELUP.values() for _, m in v))
check('learn-sets: values off gives none', V.Values(False).levelup(445) is None and V.Values(False).machine_moves(445) is None)
check('machine bits follow the Diamond/Pearl list (Garchomp: Earthquake TM26, Dragon Claw TM02)',
      U.tm_bits_gen4(None, 445, set(), V.Values(True).machine_moves(445)) >> 25 & 1 == 1 and U.tm_bits_gen4(None, 445, set(), V.Values(True).machine_moves(445)) >> 1 & 1 == 1)
check('held items: Bibarel Oran + Sitrus (Gen 4 numbers 155, 158)', V.Values(True).held_items(400) == (155, 158))
row = bytearray(24)
V.Values(True).apply_move('Defog', row)
check('move flags: Gen 4 Defog is not reflectable by Magic Coat', row[21] >> 2 & 1 == 0 and row[21] >> 1 & 1 == 1)
rom, pbr = os.environ.get('USUM_ROM'), os.environ.get('PBR_DISC')
if rom and pbr:
    sys.path.insert(0, os.path.join(HERE, '..', 'pbr_survey'))
    sys.path.insert(0, os.path.join(HERE, '..', 'pbr_convert'))
    import ncch
    import gen4_data as G
    r, prod = ncch.open_rom(rom)
    us = U.Usum(r, prod)
    P = G.PBR(pbr)
    check('machine list equals Battle Revolution\'s', U.GEN4_TM == P.tmlist)
    bad = 0
    for dex in range(387, 494):
        a = U.personal_record(us, dex, V.Values(True))
        b = P.common[8][0x30 + dex * 52:0x30 + dex * 52 + 52]
        bad += a[30:49] != b[30:49]
        bad += U.evolution_record(us, dex) != P.common[9][0x30 + dex * 42:0x30 + dex * 42 + 42]
    check('personal numbers (stats, types, exp, EV, abilities...) and evolutions equal Battle Revolution\'s for 107 species', bad == 0)
print('FAILED: %d' % len(fails) if fails else 'ALL PASSED')
sys.exit(1 if fails else 0)
