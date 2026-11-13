r"""Python side of tests/pbr_tool.cpp: prints the same canonical text from the Python converter.
  python py_dump.py hsd <model.pkx> [nomesh]"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', '..', '..', 'tools', 'pbr_convert'))
sys.path.insert(0, here)
import hsd_read
from make_hsd_cases import dump


def main(a):
    if a[0] == 'hsd':
        meshes = not (len(a) > 2 and a[2] == 'nomesh')
        pkx, model, warns = hsd_read.read_pkx(open(a[1], 'rb').read(), meshes)
        sys.stdout.write(dump(model, None))
        return 0
    if a[0] == 'convdump':
        import sdr_read, pbr_to_xd
        sp = a[1]
        i = sp.rfind('pbr')
        xp = sp[:i] + 'xd' + sp[i + 3:-4] + '.pkx'
        sdr = sdr_read.read(open(sp, 'rb').read())
        xd_pkx, xd_model, w = hsd_read.read_pkx(open(xp, 'rb').read(), meshes=False)
        step = float(a[2]) if len(a) > 2 else 0.1
        m, pkx = pbr_to_xd.convert(sdr, xd_pkx, xd_model, step, log=lambda s: None)
        sys.stdout.write(dump(m, None))
        sys.stdout.write((pkx.header + b''.join(pkx.sections)).hex() + '\n')
        return 0
    if a[0] == 'eval':
        import struct
        import numpy as np
        import sdr_read, pbr_eval
        sdr = sdr_read.read(open(a[1], 'rb').read())
        ev = pbr_eval.Eval(sdr)

        def dumpd(label, arr):
            flat = np.asarray(arr, dtype=float).reshape(-1)
            sys.stdout.write('%s %d\n' % (label, len(flat)))
            sys.stdout.write(''.join(struct.pack('>d', float(v)).hex() + '\n' for v in flat))
        acts = [None]
        na = len(sdr.actions)
        for x in (0, 1, 2, na - 1):
            if 0 <= x < na and x not in acts:
                acts.append(x)
        for act in acts:
            sys.stdout.write('action %d\n' % (-1 if act is None else act))
            p = ev.sample(act)
            dumpd('local', p.local); dumpd('rot_only', p.rot_only); dumpd('scale', p.scale); dumpd('dcomp', p.dcomp)
            dumpd('visible', p.visible.astype(float)); dumpd('compensated', p.compensated.astype(float))
            w = ev.world(p.local)
            dumpd('world', w)
            pts = ev.points(w, p.visible)
            for _, q in pts:
                dumpd('points', q)
            if pts:
                dumpd('boxes', pbr_eval.boxes(pts))
            s, r, t = pbr_eval.decompose(p, ev)
            dumpd('dec_s', s); dumpd('dec_r', r); dumpd('dec_t', t)
            pairs = set()
            for i in range(p.local.shape[1]):
                if len(pairs) < 2 and p.compensated[i]:
                    pairs.add(i)
            s, r, t = pbr_eval.decompose(p, ev, pairs=pairs)
            dumpd('pair_s', s); dumpd('pair_r', r); dumpd('pair_t', t)
            dumpd('compose', pbr_eval.compose_local(s, r, t, ev.n, pairs))
            dumpd('shear', pbr_eval.shear_error(p, ev))
        return 0
    if a[0] == 'pose':
        import struct
        pkx, model, warns = hsd_read.read_pkx(open(a[1], 'rb').read(), False)
        from model import pose_at

        def pr(w):
            for m in w:
                sys.stdout.write(''.join(struct.pack('>d', float(v)).hex() + ' ' for v in m) + '\n')
        pr(pose_at(model, None, 0.0))
        for an in model.animations:
            e = an.end()
            for f in (0.0, 1.0, 2.5, e / 2, e - 0.25, e):
                sys.stdout.write('frame %.17g\n' % f)
                pr(pose_at(model, an, f))
        return 0
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
