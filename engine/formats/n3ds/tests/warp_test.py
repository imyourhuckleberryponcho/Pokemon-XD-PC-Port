"""C++ motion energy + role retime (n3ds_tool warp) against gf_to_xd._motion_energy / role_warp: whole lists compared by CRC of their bits.
  python -I warp_test.py --tool <n3ds_tool> --dump <usum.cci> [--groups ...] [--slot 0] [--xd-len 60]"""
import argparse
import os
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'tools', 'n3ds')))
import ncch, garc, gamedetect, gfpack, gf_read, gf_pose, gf_to_xd   # noqa: E402


def crc(v):
    v = [float(x) for x in v]
    return '%08x' % zlib.crc32(struct.pack('<%dd' % len(v), *v) if v else b'')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tool', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--groups', default='37,731,1000,1036,551,725,909,4,48,7,204,211,640,1133,9,100,300,500,700')
    ap.add_argument('--slot', type=int, default=0)
    ap.add_argument('--xd-len', type=int, default=60)
    a = ap.parse_args()
    r, prod = ncch.open_rom(a.dump)
    g = gamedetect.models_garc(r)
    bad = 0
    for grp in [int(x) for x in a.groups.split(',')]:
        base = 1 + 9 * grp
        gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(base)))[0])
        mo = gf_read.parse_motion(gfpack.pc_split(garc.maybe(g.read(base + 4)))[a.slot])
        sk = gf_pose.Skeleton(gm['bones'])
        t0 = time.time()
        e = gf_to_xd._motion_energy(sk, mo)
        want = ['energy %d %s' % (len(e), crc(e))]
        for role in ('idle', 'phys', 'special', 'hit', 'faint'):
            f = gf_to_xd.role_warp(role, max(mo['frames'], 1), a.xd_len, e)
            want.append('%s %d %s' % (role, len(f), crc(f)))
        tp = time.time() - t0
        c = subprocess.run([a.tool, 'warp', a.dump, str(grp), str(a.slot), str(a.xd_len)], capture_output=True, text=True)
        got = c.stdout.replace('\r', '').strip().split('\n')
        note = ''
        if got[0] != want[0] and got[1].startswith('values'):
            # the energy may differ in the last bits (numpy's own pairwise order for a different pose noise); it must be close, and everything made from it equal
            gv = [float(x) for x in got[1].split()[1:]]
            rel = max(abs(x - y) / max(abs(y), 1e-30) for x, y in zip(gv, e))
            if rel < 1e-9:
                got[0] = want[0]
                note = ' (energy equal to %.0e relative, not to the bit)' % rel
        got = [x for x in got if not x.startswith('values')]
        ok = got == want
        bad += not ok
        print('group %d %s%s  python %.2fs, c++ %s' % (grp, 'IDENTICAL' if ok else 'DIFFERENT', note, tp, c.stderr.strip()))
        if not ok:
            for w, x in zip(want, got):
                if w != x:
                    print('   python:', w, '| c++:', x)
    print('FAILED %d' % bad if bad else 'all identical')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
