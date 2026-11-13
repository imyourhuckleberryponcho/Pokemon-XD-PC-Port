r"""Canonical text summary of a Battle Revolution model, read by the PYTHON reader (tools/pbr_convert/sdr_read.py).
pbr_sdr_dump prints the same text from the C++ reader; the two must be identical (diff them).
Usage: python dump_sdr.py <model.sdr>   (doubles are printed as the 16 hex digits of their bits)"""
import os, struct, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', '..', '..', 'tools', 'pbr_convert'))
import sdr_read


def D(x):
    return struct.pack('>d', x).hex()


def esc(s):
    out = []
    for c in s.encode('latin1'):
        out.append(chr(c) if 0x20 <= c < 0x7f and c != 0x5c else '\\x%02x' % c)
    return ''.join(out)


def fnv(b):
    h = 0xcbf29ce484222325
    for c in b:
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return '%016x' % h


def curve(o, c):
    o.append(' curve %d %d %d nv %d%s' % (c.what, c.axis, c.fps, len(c.values), ''.join(' ' + D(v) for v in c.values)))
    o.append('  slopes %d%s' % (len(c.slopes), ''.join(' ' + D(v) for v in c.slopes)))
    if c.keys is None:
        o.append('  keys none')
    else:
        o.append('  keys %d%s' % (len(c.keys), ''.join(' %d,%d,%d,%d,%s' % (k[0], k[1], k[2], k[3], D(k[4])) for k in c.keys)))


def anim(o, a, tag):
    for act, cs in a.items():
        o.append(' %s act %d n %d' % (tag, act, len(cs)))
        for c in cs:
            curve(o, c)


def opt(v):
    return -1 if v is None else v


def main():
    s = sdr_read.read(open(sys.argv[1], 'rb').read())
    o = []
    o.append('name ' + esc(s.name))
    for w in s.warnings:
        o.append('warn ' + esc(w))
    for i, a in enumerate(s.actions):
        o.append('action %d %s %s %s %s' % (i, esc(a[0]), D(a[1]), D(a[2]), D(a[3])))
    for i, t in enumerate(s.textures):
        o.append('tex %d %d %d code %d fmt %d wrap %d %d pal %d %d levels %d len %d fnv %s' % (
            i, t['width'], t['height'], t['code'], opt(t['format']), t['wrap_s'], t['wrap_t'], t['palette_format'],
            t['palette_at'], t['levels'], len(t['data']), fnv(t['data'])))
    for i, m in enumerate(s.materials):
        o.append('mat %d %s tex %d amb %s dif %s raw %s' % (i, esc(m['name']), opt(m['texture']), ','.join(map(str, m['ambient'])),
                                                           ','.join(map(str, m['diffuse'])), m['raw'].hex()))
        for k, L in enumerate(m['layers']):
            o.append(' layer %d tex %d uv %d uvset %d off %s %s rot %s scale %s %s raw %s' % (
                k, opt(L['texture']), 1 if L['kind'] == 'uv' else 0, opt(L['uvset']), D(L['offset'][0]), D(L['offset'][1]),
                D(L['rotation']), D(L['scale'][0]), D(L['scale'][1]), L['raw'].hex()))
        for k, blocks in m['anims'].items():
            o.append(' manim layer %d' % k)
            anim(o, blocks, 'mblock')
        o.append(' mfirst %d' % (list(m['anims'].keys())[0] if m['anim'] else -1))
    for i, n in enumerate(s.nodes):
        o.append('node %d parent %d kind %d %s num %d flags %d pos %s rot %s scale %s' % (
            i, n.parent, n.kind, esc(n.name), n.number, n.flags, ' '.join(map(D, n.pos)), ' '.join(map(D, n.rot)),
            ' '.join(map(D, n.scale))))
        o.append(' pivots ' + ('-' if n.pivots is None else ' '.join(map(D, n.pivots))))
        o.append(' joint %d orient %s inv %s' % (n.jflags, ' '.join(map(D, n.orient)), '-' if n.inv_bind is None else ' '.join(map(D, n.inv_bind))))
        anim(o, n.anim, 'nanim')
        if n.mesh is not None:
            me = n.mesh
            o.append(' mesh nv %d' % len(me['verts']))
            for v in me['verts']:
                o.append('  v ' + ' '.join(map(D, list(v[0]) + list(v[1]))))
            o.append(' uvsets %d' % len(me['uvsets']))
            for k, st in enumerate(me['uvsets']):
                o.append('  uvset %d n %d' % (k, len(st)))
                for u in st:
                    o.append('   uv %s %s' % (D(u[0]), D(u[1])))
            if me['weights'] is None:
                o.append(' weights none')
            else:
                o.append(' weights %d' % len(me['weights']))
                for d in me['weights']:
                    o.append('  w %d%s' % (len(d), ''.join(' %d:%s' % (k, D(x)) for k, x in d.items())))
            o.append(' parts %d' % len(me['parts']))
            for p in me['parts']:
                o.append('  part mat %d tris %d' % (opt(p['material']), len(p['tris'])))
                for t in p['tris']:
                    o.append('   t ' + ' '.join(','.join(str(opt(x)) for x in c) for c in t))
    o.append('by_number %d%s' % (len(s.by_number), ''.join(' %d:%d' % kv for kv in sorted(s.by_number.items()))))
    sys.stdout.write('\n'.join(o) + '\n')


main()
