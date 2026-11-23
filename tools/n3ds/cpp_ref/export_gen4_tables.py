"""Writes tools/n3ds/gen4_tables.json: the tables of usum_gen4.py / usum_gen4_values.py / usum_gen4_learnsets.py / usum_gen4_donors.py for the C++
data build (usum_gen4.cpp). Public-data numbers and our own picks only. Run after changing those files; --check compares."""
import json
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.dirname(HERE)
sys.path.insert(0, N3)
import usum_gen4 as G
import usum_gen4_values as V
import usum_gen4_learnsets as L
import usum_gen4_donors as D


def build():
    return dict(tm=G.GEN4_TM, tm7=G.GEN7_TM_NAMES, species={str(k): v for k, v in V.SPECIES.items()}, moves=V.MOVES, kings_rock=V.KINGS_ROCK,
                move_flags={k: list(v) for k, v in V.MOVE_FLAGS.items()}, held_items={str(k): list(v) for k, v in V.HELD_ITEMS.items()},
                levelup={str(k): [list(x) for x in v] for k, v in L.LEVELUP.items()}, machines={str(k): list(v) for k, v in L.MACHINES.items()},
                donor_of=D.DONOR_OF)


if __name__ == '__main__':
    out = os.path.join(N3, 'gen4_tables.json')
    text = json.dumps(build(), indent=None, ensure_ascii=True, separators=(',', ':')) + '\n'
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        sys.exit(0 if open(out, encoding='utf8').read() == text else 'gen4_tables.json is out of date: run cpp_ref/export_gen4_tables.py')
    open(out, 'w', encoding='utf8', newline='\n').write(text)
    print('wrote', out, len(text))
