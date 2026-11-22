"""Is every move number a Gen 5-7 pack's species file uses a move the pack's move file really adds?

  python tools/n3ds/check_pack_move_rows.py <pack folder> [--first-row 488]

The game numbers the rows of a data file's "add" block one after the other, so the number of the Nth move is first + N. The species
learnsets are written for the numbers moves_report.json lists ("row"). When the move file holds fewer rows than the report (two moves
with one name collapsed into one key: the 18 doubled Z-moves, found 2026-11-20) every move after the first doubled one sits on a wrong
number and the last numbers do not exist ("[mods] problem ... is move #735, but the game's move numbers end at ..."). Exit 1 on any
mismatch. Game data stays in the pack folder; this file holds none.
"""
import json
import os
import sys


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    pack = sys.argv[1]
    first = 488
    if '--first-row' in sys.argv:
        first = int(sys.argv[sys.argv.index('--first-row') + 1])
    moves = list(json.load(open(os.path.join(pack, 'data', '01_moves.json'), encoding='utf-8'))['add']['moves'])
    report = json.load(open(os.path.join(pack, 'moves_report.json'), encoding='utf-8'))
    problems = []
    if len(moves) != len(report):
        problems.append('the move file adds %d moves, the report has %d' % (len(moves), len(report)))
    for i, key in enumerate(moves):
        if i < len(report) and report[i]['row'] != first + i:
            problems.append('move %d (%s) gets number %d, the report says %d' % (i, key, first + i, report[i]['row']))
            if len(problems) > 5:
                break
    last = first + len(moves) - 1
    species = json.load(open(os.path.join(pack, 'data', '02_species.json'), encoding='utf-8'))['add']['pokemon']
    outside = set()
    for row in species.values():
        for v in row.get('waza.dataId', []) or []:
            if isinstance(v, int) and v > last and v >= first:
                outside.add(v)
    if outside:
        problems.append('learnsets use move numbers the move file does not add: %s' % sorted(outside))
    for p in problems:
        print('PROBLEM:', p)
    print('%s: %d moves, rows %d..%d, %s' % (pack, len(moves), first, last, 'OK' if not problems else '%d problem(s)' % len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
