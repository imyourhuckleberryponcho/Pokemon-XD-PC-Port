"""The whole Pokedex picture stage (gen57_faces_fast.make_pictures, as the converter runs it) drawn twice from the same finished packs; the two sets of
pictures compared pixel by pixel. usage: pictures_full_repeat.py <xd.exe> <XD disc> <gen4 pack> <gen57 pack> <out folder>"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_faces_fast as FF


def main():
    xd, disc, gen4, pack, out = sys.argv[1:6]
    a = type('NS', (), {'xd_exe': xd, 'xd_disc': disc})()
    for tag in ('one', 'two'):
        FF.make_pictures(a, pack, gen4, os.path.join(out, tag), lambda *x: None, lambda s: print(s, flush=True), 1)
    same = diff = 0
    worst = []
    for f in sorted(os.listdir(os.path.join(out, 'one'))):
        p2 = os.path.join(out, 'two', f)
        if not f.endswith('.png') or not os.path.exists(p2):
            continue
        x = np.asarray(Image.open(os.path.join(out, 'one', f)).convert('RGBA')).astype(int)
        y = np.asarray(Image.open(p2).convert('RGBA')).astype(int)
        n = int((np.abs(x - y).max(axis=2) > 0).sum())
        if n:
            diff += 1
            worst.append((n, f))
        else:
            same += 1
    print('two full runs of the picture stage: %d pictures identical, %d different' % (same, diff))
    print('largest differences:', sorted(worst, reverse=True)[:5])


if __name__ == '__main__':
    main()
