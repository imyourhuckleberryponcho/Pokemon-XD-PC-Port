"""The Python side of species_dump.h: the same canonical text for one model group of the player's dump (the reference).
  python -I py_species.py <dump> <group>"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'tools', 'n3ds'))
sys.path.insert(0, N3)
import ncch      # noqa: E402
import garc      # noqa: E402
import gamedetect   # noqa: E402
import gfpack    # noqa: E402
import gf_read   # noqa: E402

COLS = ('c0', 'c1', 'c2', 'c3', 'c4', 'c5', 'spec0', 'spec1', 'blend', 'emission', 'ambient', 'diffuse')


def crcd(vals):
    vals = list(vals)
    return '%08x' % zlib.crc32(struct.pack('<%dd' % len(vals), *vals) if vals else b'')


def crcch(ch):
    v = []
    for k in ch:
        v += [float(k[0]), k[1], k[2]]
    return crcd(v)


def dump_species(r, group):
    d = gamedetect.detect(r, '')
    g = gamedetect._garc(r, d['models'])
    base = 1 + 9 * group
    gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(base)))[0])
    P = print
    P('bones %d' % len(gm['bones']))
    for b in gm['bones']:
        P('bone %s|%s|%d s:%s r:%s t:%s' % (b['name'], b['parent'], b['flags'], crcd(b['scale']), crcd(b['rot']), crcd(b['pos'])))
    P('tex_names ' + ' '.join(gm['tex_names']))
    for mt in gm['materials']:
        P('mat %s|%s|%s|%s %s' % (mt['name'], mt['shader'], mt['vtx_shader'], mt['frag_shader'], ''.join('%02x%02x%02x%02x' % mt['colors'][k] for k in COLS)))
        for c in mt['coords']:
            P(' coord %s %d %d %s %d %d' % (c['name'], c['unit'], c['mapping'], crcd([c['scale'][0], c['scale'][1], c['rot'], c['trans'][0], c['trans'][1]]),
                                            c['wrap'][0], c['wrap'][1]))
    for me in gm['meshes']:
        P('mesh ' + me['name'])
        for s in me['subs']:
            ix = [float(x) for x in s['indices']]
            P(' sub %s bi %s stride %d prim %d raw %d:%08x idx %d:%s tri %d' % (
                s['name'], ' '.join(str(x) for x in s['bone_idx']), s['stride'], s['prim'], len(s['raw']), zlib.crc32(s['raw']), len(ix), crcd(ix),
                len(gf_read.triangles(s))))
            for a in s['attrs']:
                P('  attr %s %d %d' % (a['name'], a['fmt'], a['n']))
            for nm, v in s['fixed']:
                P('  fixed %s %s' % (nm, crcd(list(v))))
            V = s['verts']
            P('  n %d' % V['n'])
            for k in sorted(k for k in V if k != 'n'):
                a = V[k]
                P('  v %s %dx%d %s' % (k, a.shape[0] if a.ndim == 2 else 1, a.shape[1] if a.ndim == 2 else len(a), crcd(a.reshape(-1).tolist())))
    for t in gfpack.pc_split(garc.maybe(g.read(base + 1))):
        T = gf_read.parse_texture(t)
        w, h, img = gf_read.decode_texture(T)
        P('tex %s %d %d %d %d %d %08x' % (T['name'], T['w'], T['h'], T['fmt'], T['mip'], len(T['data']), zlib.crc32(img.tobytes())))
    subs = gfpack.pc_split(garc.maybe(g.read(base + 4)))
    for slot, sb in enumerate(subs):
        if not sb:
            continue
        try:
            mo = gf_read.parse_motion(sb)
        except Exception:
            P('slot %d error' % slot)
            continue
        if mo is None:
            P('slot %d none' % slot)
            continue
        P('motion slot%d frames %d loop %d' % (slot, mo['frames'], int(mo['loop'])))
        for nm, b in mo['bones'].items():
            P(' b %s %d ' % (nm, int(b['axis'])) + ' '.join('%d:%s' % (len(c), crcch(c)) for c in b['ch']))
        for nm, v in mo['vis'].items():
            P(' vis %s %s' % (nm, ''.join('1' if x else '0' for x in v)))
        for c in mo['mats']:
            P(' mat %s %d %s %s %s %s %s' % (c['name'], c['unit'], crcch(c['sx']), crcch(c['sy']), crcch(c['rot']), crcch(c['tx']), crcch(c['ty'])))


if __name__ == '__main__':
    r, prod = ncch.open_rom(sys.argv[1])
    dump_species(r, int(sys.argv[2]))
