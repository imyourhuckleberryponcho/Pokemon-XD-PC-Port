"""Proof helper: the face cameras the OLD converter code finds (a tools folder given as <old tools root>) for some species, compared with the numbers
in gen57_speed_numbers.json. usage: views_vs_table.py <old tools root> <rom> <pack> <table> nat [nat ...]  (run with the old code's own python path)"""
import json
import multiprocessing
import os
import sys


def main():
    old, rom, pack, table = sys.argv[1:5]
    nats = [int(x) for x in sys.argv[5:]]
    sys.path.insert(0, os.path.join(old, 'tools', 'n3ds'))
    import gen57_all as A
    import gen57_faces_fast as FF
    ns = type('NS', (), {'rom': rom})()
    views, why = FF.views_parallel(ns, A, pack, nats, 8, lambda *x: None)
    t = json.load(open(table, encoding='utf-8'))
    d = list(t['dumps'].values())[0]['views']
    same = 0
    for n in nats:
        e = d[str(n)]['view']
        if (views.get(n) == e):
            same += 1
        else:
            print('DIFFERENT', n, views.get(n), e)
    print('old search vs table: %d of %d species give exactly the same numbers' % (same, len(nats)))


if __name__ == '__main__':
    multiprocessing.freeze_support()
    main()
