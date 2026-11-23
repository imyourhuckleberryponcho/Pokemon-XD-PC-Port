"""Makes (or updates) gen57_speed_numbers.json: the numbers of gen57_speed.py for one dump. Developer tool, run on a finished pack.

  GEN57_RECORD_STEPS=<steps dir> usum_models ... (a run that makes the models and forms: every model's ladder step lands in <steps dir>)
  python gen57_speed_table.py --rom <dump> --pack <the finished Gen 5-7 pack> --steps <steps dir> [--name "Ultra Sun (USA)"] [--jobs 8]

Writes the ladder steps (only the models that needed more than step 0) and the face cameras of the pack's species (searched here, with the
table switched off: the numbers are what the search finds), keyed by the dump's fingerprint, into gen57_speed_numbers.json next to this file.
Numbers only: no textures, models, sounds or text of the game.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ['GEN57_NO_SPEED_TABLE'] = '1'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--steps', default='')
    ap.add_argument('--name', default='')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--out', default=os.path.join(HERE, 'gen57_speed_numbers.json'))
    a = ap.parse_args()
    import gen57_all as A
    import gen57_faces_fast as FF
    import gen57_facecam as FC
    import gen57_speed as SP
    fp = SP.dump_fingerprint(a.rom)
    t = json.load(open(a.out, encoding='utf-8')) if os.path.exists(a.out) else {}
    t.setdefault('comment', "Numbers only, made by gen57_speed_table.py from a finished pack: which step of the size ladder each model fits, and the face camera "
                 "of each species (used only for the very same dump and the very same converted model; gen57_speed.py)")
    t['version'] = SP.VERSION
    d = t.setdefault('dumps', {}).setdefault(fp, {})
    if a.name:
        d['name'] = a.name
    if a.steps:
        lad, forms = {}, {}
        for f in sorted(os.listdir(a.steps)):
            j = json.load(open(os.path.join(a.steps, f)))
            kind, name = f[:-5].split('_', 1)
            if kind == 'sp' and j['step']:
                lad[name] = j['step']
            elif kind == 'form' and j['step']:
                forms[name] = j['step']
        d['ladder'], d['forms'] = dict(sorted(lad.items(), key=lambda kv: int(kv[0]))), dict(sorted(forms.items()))
    nats = [n for n in range(A.FIRST, A.LAST + 1) if os.path.exists(os.path.join(a.pack, 'files', 'pkx_n3_%d.fsys' % n))]
    ns = type('NS', (), {'rom': a.rom})()
    for kind, wide in (('views', False), ('wide', True)):
        t0 = time.time()
        views, why = FF.views_parallel(ns, A, a.pack, nats, a.jobs, lambda *x: None, wide=wide)
        ent = {}
        for n in nats:
            key = FC.view_key(n, FF.file_hash(os.path.join(a.pack, 'files', 'pkx_n3_%d.fsys' % n)), wide)
            if n in views:
                ent[str(n)] = dict(key=key, view=views[n])
            elif why.get(n) == 'no head bone':
                ent[str(n)] = dict(key=key, view=None)
            else:
                print('not stored:', n, why.get(n))
        d[kind] = ent
        print(kind, len(ent), 'of', len(nats), round(time.time() - t0), 's')
    json.dump(t, open(a.out, 'w', encoding='utf-8'), indent=0, sort_keys=False)
    print('wrote', a.out, 'ladder', len(d.get('ladder', {})), 'forms', len(d.get('forms', {})))


if __name__ == '__main__':
    import multiprocessing
    multiprocessing.freeze_support()
    main()
