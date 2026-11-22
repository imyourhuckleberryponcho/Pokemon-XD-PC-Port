"""One line of numbers per Pokemon model, for the whole roster of either game (read only, slow: one core).

  python tools/pbr_survey/roster.py pbr <folder of extracted PBR files> <out.tsv>
  python tools/pbr_survey/roster.py xd  <XD disc image> <out.tsv>

pbr: member 0 of every pkx_*.fsys -> archive, members in the archive, model name, nodes, joints (kind 2),
     vertices, triangles, materials, textures, largest texture, texture bytes, animations as name:frames.
xd:  every type-15 member -> name, id, joints, vertices, triangles, materials, textures, largest texture,
     animations as frames, archive bytes.
Lines are written as they are made (a stopped run keeps what it has; a second run skips finished lines).
The output holds game file names: keep it under build/.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import noddisc, sdr, xd_pkx

BPP = {'C4': 4, 'C8': 8, 'C14X2': 16, 'I4': 4, 'IA4': 8, 'I8': 8, 'IA8': 16, 'RGB565': 16, 'RGBA8': 32, 'RGB5A3': 16,
       'CMPR': 4, 'CI4': 4, 'CI8': 8, 'CI14X2': 16}


def done_keys(path):
    if not os.path.exists(path):
        return set()
    return set(l.split('\t')[0] for l in open(path))


def tex_stats(tex):
    if not tex:
        return 0, '', 0
    big = max(tex, key=lambda t: t[0] * t[1])
    return len(tex), '%dx%d %s' % (big[0], big[1], big[2]), sum(t[0] * t[1] * BPP.get(t[2], 16) // 8 for t in tex)


def pbr(root, out_path):
    have = done_keys(out_path)
    with open(out_path, 'a') as out:
        for fn in sorted(os.listdir(root)):
            if not (fn.startswith('pkx_') and fn.endswith('.fsys')) or fn in have:
                continue
            data = open(os.path.join(root, fn), 'rb').read()
            ents = noddisc.fsys_entries(data)
            try:
                r = sdr.parse(noddisc.extract(data, ents[0]))
                md = r['models'][0]
                skin = [m for m in md['meshes'] if not m['col']]
                n, big, tb = tex_stats(r['textures'])
                row = [fn, len(ents), md['name'], len(md['nodes']), sum(1 for x in md['nodes'] if x[2] == 2),
                       sum(m['verts'] for m in skin), sum(m['tris'] for m in skin), r['materials'], n, big, tb,
                       ' '.join('%s:%d' % (a[0], round(a[3] * 30)) for a in md['anims']), ents[0].size,
                       ' '.join('%d' % e.size for e in ents)]
            except Exception as ex:
                row = [fn, len(ents), 'ERROR %s' % ex]
            out.write('\t'.join(str(x) for x in row) + '\n')
            out.flush()


def xd(disc_path, out_path):
    have = done_keys(out_path)
    d = noddisc.Disc(disc_path)
    with open(out_path, 'a') as out:
        for p in sorted(d.files):
            if not (p.lower().startswith('pkx_') and p.lower().endswith('.fsys')) or p in have:
                continue
            data = d.file(p)
            for e in noddisc.fsys_entries(data):
                if e.type != 15:
                    continue
                try:
                    b = noddisc.extract(data, e)
                    size, unk8, count = noddisc.be32(b, 0), noddisc.be32(b, 8), noddisc.be32(b, 0x10)
                    body = xd_pkx.align32(0x84 + count * 0xD0) + (xd_pkx.align32(unk8) if unk8 else 0)
                    r = xd_pkx.analyse(xd_pkx.Hsd(b[body:body + size]))
                    n, big, tb = tex_stats(list(r['textures'].values()))
                    row = [p, e.name, noddisc.be16(b, 0x18), r['joints'], sum(v + 1 for v in r['pos_arrays'].values()),
                           r['tris'], r['mobj'], n, big, tb, ' '.join('%d' % a[0] for a in r['anims']), e.size,
                           r['max_env'], r['max_weights'], r['pobj']]
                except Exception as ex:
                    row = [p, e.name, 'ERROR %s' % ex]
                out.write('\t'.join(str(x) for x in row) + '\n')
                out.flush()


if __name__ == '__main__':
    (pbr if sys.argv[1] == 'pbr' else xd)(sys.argv[2], sys.argv[3])
