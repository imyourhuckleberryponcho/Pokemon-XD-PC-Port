"""The whole faces stage (gen57_faces_fast.make_faces) twice on copies of the same finished pack; the drawn pictures (work/render) compared pixel by
pixel. usage: faces_full_repeat.py <xd.exe> <XD disc> <dump> <gen4 pack> <gen57 pack> <out folder>"""
import os
import shutil
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_all as A
import gen57_faces_fast as FF


def main():
    xd, disc, rom, gen4, pack, out = sys.argv[1:7]
    out = os.path.abspath(out)
    a = type('NS', (), {'xd_exe': xd, 'xd_disc': disc, 'rom': rom, 'gen4_pack': gen4})()
    nats = list(range(A.FIRST, A.LAST + 1))
    for tag in ('one', 'two'):
        cp = os.path.join(out, 'pack_' + tag)
        if not os.path.isdir(cp):
            shutil.copytree(pack, cp)
        FF.make_faces(a, A, cp, os.path.join(out, 'work_' + tag), nats, 8, lambda *x: None, lambda s: print(s, flush=True))
    same = diff = 0
    for f in sorted(os.listdir(os.path.join(out, 'work_one', 'render'))):
        p2 = os.path.join(out, 'work_two', 'render', f)
        if not f.endswith('.png') or not os.path.exists(p2):
            continue
        x = np.asarray(Image.open(os.path.join(out, 'work_one', 'render', f)).convert('RGBA')).astype(int)
        y = np.asarray(Image.open(p2).convert('RGBA')).astype(int)
        if (np.abs(x - y).max(axis=2) > 0).any():
            diff += 1
        else:
            same += 1
    print('two full runs of the faces stage: %d drawn pictures identical, %d different' % (same, diff))


if __name__ == '__main__':
    main()
