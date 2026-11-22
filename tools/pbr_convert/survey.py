"""What the Battle Revolution models hold that a converter must handle: one line per model + totals.

  python tools/pbr_convert/survey.py <work folder>     (the folder batch.py extract filled) -> <work>/survey.tsv
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdr_read


def main(work):
    tot = {}
    rows = []
    for fn in sorted(os.listdir(os.path.join(work, 'pbr'))):
        name = fn[:-4]
        try:
            s = sdr_read.read(open(os.path.join(work, 'pbr', fn), 'rb').read())
        except Exception as ex:
            rows.append((name, 'ERROR %s' % ex))
            continue
        f = {}
        def add(k, n=1):
            f[k] = f.get(k, 0) + n
        hdr = s.b[0:0x20].hex()
        add('hdr4=%s' % s.b[4:8].hex())
        for n in s.nodes:
            add('kind%d' % n.kind)
            add('nflags=%04X' % n.flags)
            if n.kind == 2:
                add('jflags=%X' % n.jflags)
                if any(abs(a) > 1e-5 for a in n.orient):
                    add('orient')
            if n.pivots and any(abs(p) > 1e-6 for p in n.pivots):
                add('pivots')
                if n.kind == 2:
                    add('pivots_on_joint')
            if n.mesh:
                add('mesh_rigid' if n.mesh['weights'] is None else 'mesh_skinned')
                if n.mesh['weights'] is None and n.anim:
                    add('rigid_animated')
            sc = n.scale
            if max(sc) - min(sc) > 1e-4:
                add('static_nonuniform_scale')
            for act, curves in n.anim.items():
                for c in curves:
                    if c.what == 3:
                        add('vis_curve')
                        if c.values and min(c.values) < 0.5:
                            add('vis_off')
                    elif c.what == 2:
                        add('scale_curve')
                        if n.kind == 2:
                            add('scale_curve_joint')
                    if c.what > 3:
                        add('curve_what%d' % c.what)
                    if c.keys is None:
                        add('curve_fps%d' % c.fps)
        for t in s.textures:
            add('tex_code=%02X' % t['code'])
            add('tex_levels=%d' % t['levels'])
        for w in s.warnings:
            add('warn:' + w[:40])
        for a in s.actions:
            add('act:' + a[0])
        rows.append((name, ' '.join('%s:%d' % kv for kv in sorted(f.items()) if not kv[0].startswith('act:'))))
        for k in f:
            tot.setdefault(k, []).append(name)
    with open(os.path.join(work, 'survey.tsv'), 'w') as o:
        for r in rows:
            o.write('\t'.join(r) + '\n')
        o.write('\nTOTALS (models with the feature)\n')
        for k, v in sorted(tot.items(), key=lambda kv: -len(kv[1])):
            o.write('%4d %s  e.g. %s\n' % (len(v), k, ' '.join(v[:5])))
    for k, v in sorted(tot.items(), key=lambda kv: -len(kv[1])):
        if not k.startswith('act:'):
            print('%4d %s  e.g. %s' % (len(v), k, ' '.join(v[:4])))


if __name__ == '__main__':
    main(sys.argv[1])
