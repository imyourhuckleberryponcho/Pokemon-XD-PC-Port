"""Makes ONLY the battle files of a Gen 5-7 pack again with the real generators (0.7.1 round 3): battle/moves.json, battle/abilities.json,
battle/hidden_abilities.json, abilities.txt and the two report files. The same calls gen57_all.py makes, without the species and models (hours).

  python -I gen57_battle_files.py --rom <Ultra Sun .cci> --pack <a finished pack folder> [--xd-moves-cache xd_moves_cache.json]

Used to prove that the plan files (gen57_move_plan.py, gen57_abilities.py, gen57_hidden.py) give the pack content the game side expects.
Output is game data made from the player's dump: it stays in the pack folder.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_data as D
import gen57_pack as P
import gen57_moves as MV
import gen57_abilities as AB
import gen57_hidden as HID


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--xd-moves-cache', default=None)
    a = ap.parse_args()
    pack = os.path.abspath(a.pack)
    u = D.Usum(a.rom)
    mcache = json.load(open(a.xd_moves_cache or os.path.join(os.path.dirname(pack), 'xd_moves_cache.json')))['xd']
    _rws, mbattle, mreport = MV.build_all(u, mcache, langs=('en',))
    os.makedirs(os.path.join(pack, 'battle'), exist_ok=True)
    json.dump({'_note': 'made by gen57_all.py: what the new moves do (docs/modding/battle-effects.md)', 'moves': mbattle},
              open(os.path.join(pack, 'battle', 'moves.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(mreport, open(os.path.join(pack, 'moves_report.json'), 'w'), indent=1)
    adefs, areport = AB.build_all(u)
    abil_ids = list(range(124, u.ability_count()))
    open(os.path.join(pack, 'abilities.txt'), 'w', encoding='utf-8').write(P.ability_lines(u, abil_ids, {v['id'] for v in adefs.values()}))
    json.dump({'_note': 'made by gen57_all.py: what the new abilities do (docs/modding/battle-effects.md)', 'abilities': adefs},
              open(os.path.join(pack, 'battle', 'abilities.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(areport, open(os.path.join(pack, 'abilities_report.json'), 'w'), indent=1)
    HID.write(u, pack)
    print('battle files made again: %d moves, %d abilities' % (len(mbattle), len(adefs)))


if __name__ == '__main__':
    main()
