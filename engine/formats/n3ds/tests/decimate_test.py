"""C++ mesh slimming (n3ds_tool decimate) against tools/n3ds/decimate.py on the player's own dump: the kept vertices and the
triangle list must be IDENTICAL (they are whole numbers; the cost terms are floating point, so a near-tie can break the other way:
reported, not hidden).
  python -I decimate_test.py --tool <n3ds_tool> --dump <usum.cci> [--groups 37,731,...] [--ratio 0.5]"""
import argparse
import os
import struct
import subprocess
import sys
import time
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'tools', 'n3ds')))
import ncch, garc, gamedetect, gfpack, gf_read, decimate   # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tool', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--groups', default='37,731,1000,1036,551,725,909,4,48')
    ap.add_argument('--ratio', type=float, default=0.5)
    a = ap.parse_args()
    r, prod = ncch.open_rom(a.dump)
    g = gamedetect.models_garc(r)
    same = diff = 0
    for grp in [int(x) for x in a.groups.split(',')]:
        gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(1 + 9 * grp)))[0])
        want = []
        t0 = time.time()
        si = 0
        for me in gm['meshes']:
            for sm in me['subs']:
                si += 1
                tl = gf_read.triangles(sm)
                if len(tl) <= 16:
                    continue
                V = sm['verts']
                uv = V['uv0'][:, :2] if 'uv0' in V else None
                wts = []
                for i in range(V['n']):
                    w = {}
                    if 'bidx' in V and 'bw' in V:
                        for k in range(min(V['bidx'].shape[1], V['bw'].shape[1])):
                            bidx = int(V['bidx'][i, k])
                            wk = float(V['bw'][i, k])
                            if wk > 0 and bidx < len(sm['bone_idx']):
                                j = sm['bone_idx'][bidx]
                                w[j] = w.get(j, 0.0) + wk
                    wts.append(tuple(w.items()))
                keep, tris = decimate.decimate(V['pos'], uv, wts, [tuple(t) for t in tl], int(len(tl) * a.ratio))
                flat = [float(keep[x]) for t in tris for x in t]
                want.append('sub %d %d -> %d %d %08x' % (si, len(tl), len(keep), len(tris), zlib.crc32(struct.pack('<%dd' % len(flat), *flat) if flat else b'')))
        tp = time.time() - t0
        t0 = time.time()
        c = subprocess.run([a.tool, 'decimate', a.dump, str(grp), str(a.ratio)], capture_output=True, text=True)
        tc = time.time() - t0
        got = c.stdout.strip().split('\n') if c.stdout.strip() else []
        ok = got == want
        same += ok
        diff += not ok
        print('group %d %s  %d pieces  python %.2fs, c++ %s' % (grp, 'IDENTICAL' if ok else 'DIFFERENT', len(want), tp, c.stderr.strip().split('\n')[-1] if c.stderr else '?'))
        if not ok:
            for w, gg in zip(want, got):
                if w != gg:
                    print('   python:', w)
                    print('   c++   :', gg)
    print('identical %d, different %d' % (same, diff))
    sys.exit(1 if diff else 0)


if __name__ == '__main__':
    main()
