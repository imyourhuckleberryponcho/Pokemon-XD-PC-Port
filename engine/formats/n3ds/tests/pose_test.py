"""C++ pose sampling / skinning / ground offsets (n3ds_tool pose) against the Python (numpy) on the player's own dump.
  python -I pose_test.py --tool <n3ds_tool> --dump <usum.cci> --scratch <folder> [--groups 37,731,...] [--slot 0] [--stretch 0.7]
Prints per group: speed of both, and for each array the largest difference (absolute, and relative to the array's largest value),
how many numbers are bit-identical. PASS = every difference below --tol (default 1e-9 relative to the array's size): the stated rule
(n3ds_pose.h): double precision, no fused multiply-add, own summation order and inverse; numpy's own order differs in the last bits."""
import argparse
import os
import subprocess
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'tools', 'n3ds')))
import ncch, garc, gamedetect, gfpack, gf_read, gf_pose, gf_to_xd   # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tool', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--scratch', required=True)
    ap.add_argument('--groups', default='37,731,1000,1036,551,725,909,4,48')
    ap.add_argument('--slot', type=int, default=0)
    ap.add_argument('--stretch', type=float, default=0.7)
    ap.add_argument('--tol', type=float, default=1e-9)
    a = ap.parse_args()
    os.makedirs(a.scratch, exist_ok=True)
    r, prod = ncch.open_rom(a.dump)
    g = gamedetect.models_garc(r)
    bad = 0
    for grp in [int(x) for x in a.groups.split(',')]:
        base = 1 + 9 * grp
        gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(base)))[0])
        subs = gfpack.pc_split(garc.maybe(g.read(base + 4)))
        mo = gf_read.parse_motion(subs[a.slot])
        out = os.path.join(a.scratch, 'pose_%d.bin' % grp)
        t0 = time.time()
        c = subprocess.run([a.tool, 'pose', a.dump, str(grp), str(a.slot), str(a.stretch), out], capture_output=True, text=True)
        tc = time.time() - t0
        if c.returncode:
            print('group %d C++ failed: %s' % (grp, c.stderr.strip()))
            bad += 1
            continue
        cpp_sec = c.stderr.strip().split('\n')[-1]
        t0 = time.time()
        sk = gf_pose.Skeleton(gm['bones'])
        ns = int(mo['frames'] / a.stretch) + 1
        S, R, T = gf_to_xd._sample_at(sk, mo, [k * a.stretch for k in range(ns)])
        tp_sample = time.time() - t0
        pts = [(sm['bone_idx'], sm['verts']) for me in gm['meshes'] for sm in me['subs']]
        parts = {'S': S, 'R': R, 'T': T}
        t0 = time.time()
        for name, k in (('skin_first', 0), ('skin_last', ns - 1)):
            W = sk.world(S[k], R[k], T[k])
            parts[name] = np.concatenate([gf_pose.skin_points(sk, W, bi, v) for bi, v in pts])
        tp_skin = time.time() - t0
        t0 = time.time()
        parts['ground'] = gf_to_xd.ground_offsets(sk, S, R, T, pts, 0.25, 0.0)
        tp_ground = time.time() - t0
        want = np.concatenate([np.asarray(v, float).reshape(-1) for v in parts.values()])
        got = np.fromfile(out, '<f8')
        os.remove(out)
        if got.shape != want.shape:
            print('group %d SIZE %s vs %s' % (grp, got.shape, want.shape))
            bad += 1
            continue
        pos = 0
        line = []
        ok = True
        for name, v in parts.items():
            n = np.asarray(v).size
            w, gg = want[pos:pos + n], got[pos:pos + n]
            pos += n
            d = np.abs(w - gg)
            turns = 0
            if name == 'R':    # an angle at the +-pi edge: atan2 of a +0 / -0 noise value gives +pi or -pi, the same rotation; counted, not failed
                k2 = np.round(d / (2 * np.pi))
                turns = int((k2 != 0).sum())
                d = np.abs(d - k2 * 2 * np.pi)
            scale = max(float(np.abs(w).max()), 1.0)
            rel = float(d.max()) / scale if n else 0.0
            same = int((w == gg).sum())
            line.append('%s %.1e (%d%% bit-equal%s)' % (name, rel, 100 * same // max(n, 1), ', %d numbers a whole turn apart' % turns if turns else ''))
            if not rel <= a.tol:
                ok = False
        print('group %d %s  python: sample %.2fs skin %.2fs ground %.2fs | c++ %s (process %.2fs)' % (grp, 'PASS' if ok else 'FAIL', tp_sample, tp_skin, tp_ground, cpp_sec, tc))
        print('    ' + ' | '.join(line))
        bad += not ok
    print('FAILED %d' % bad if bad else 'all passed')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
