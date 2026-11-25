"""3DS (Ultra Sun / Ultra Moon) Pokemon cries as XD cry streams: a reusable module (Gen 5-7 pack, and the USUM-built Gen 4 pack).

What is known (measured on the player's dump, docs/GEN57.md sections 10-11):
  * `data/sound/niji_sound.bcsar` has no name table, and its own 2183 wave archives are sound effects.
  * The GARC `a/0/7/3` has 5333 entries; entries 4368..5332 are 965 BCWAR files, each ONE mono DSP-ADPCM wave (32728 Hz mostly, 0.4-1.7 s): the cries.
    `cry_waves(rom)` lists them (index 0..964).
  * Known since 2026-11-19: the species -> wave table (gen57_cry_waves.json, found from the sequences' labels).
  * (older note) which index belongs to which species / form. `a/1/1/8` (951 x {count, first}) and `a/0/1/8` (family root) were checked and are not it.
    Until it is found, a mapping is an INPUT: a text file `mapping.txt` with lines `<species key> <cry index>` (see load_mapping), so a later discovery, or a
    hand-made list, drops in without code changes.

  python -I tools/n3ds/n3ds_cries.py list <rom>                 index, rate, samples, loop of the 965 cry waves
  python -I tools/n3ds/n3ds_cries.py mapping <out.txt> [first last]   species -> wave lines from gen57_cry_waves.json (species 1..807)
  python -I tools/n3ds/n3ds_cries.py make <rom> <mapping.txt> <out folder>   <key>.hdr + <key>.body (XD cry format, as tools/pbr_convert/gen4_cry.py makes)
  A prepared pair (hdr, body) from any other source can be given to the pack builders directly; this module only produces them.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_convert'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))

CRY_GARC = 'a/0/7/3'
FIRST_CRY_ENTRY = 4368


def parse_cwar(w):
    assert w[:4] == b'CWAR'
    _t, _p, io, _isz = struct.unpack_from('<HHII', w, 0x14)
    _t, _p, fo, _fsz = struct.unpack_from('<HHII', w, 0x20)
    n = struct.unpack_from('<I', w, io + 8)[0]
    out = []
    for i in range(n):
        _t, _p, off, size = struct.unpack_from('<HHII', w, io + 12 + 12 * i)
        out.append(w[fo + 8 + off:fo + 8 + off + size])
    return out


def parse_cwav(v):
    """-> neutral record of tools/pbr_convert/gen4_cry.py (rate, nsamp, loop, chans[coefs, ...], data[...]); DSP-ADPCM only."""
    assert v[:4] == b'CWAV'
    _t, _p, io, _isz = struct.unpack_from('<HHII', v, 0x14)
    _t, _p, do, dsz = struct.unpack_from('<HHII', v, 0x20)
    enc, loop, _x = struct.unpack_from('<BBH', v, io + 8)
    rate, lstart, lend = struct.unpack_from('<III', v, io + 12)
    if enc != 2:
        raise ValueError('not DSP-ADPCM (encoding %d): encode it with engine/formats/dsp_adpcm instead' % enc)
    nch = struct.unpack_from('<I', v, io + 0x1c)[0]
    chans, data = [], []
    nframes = (lend + 13) // 14
    for c in range(nch):
        _ty, _pd, off = struct.unpack_from('<HHI', v, io + 0x20 + 8 * c)
        ci = io + 0x1c + off

        _t0, _p0, samp_off = struct.unpack_from('<HHI', v, ci)
        _t1, _p1, codec_off = struct.unpack_from('<HHI', v, ci + 8)
        coefs = struct.unpack_from('<16h', v, ci + codec_off)
        chans.append(dict(coefs=coefs, gain=0, ps=0, h1=0, h2=0, lps=0, lh1=0, lh2=0))
        start = do + 8 + samp_off
        data.append(v[start:start + nframes * 8])
    return dict(rate=rate, nsamp=lend, loop=loop, chans=chans, data=data)


def cry_waves(rom):
    import ncch, garc, gamedetect
    r, _prod = ncch.open_rom(rom)
    g = gamedetect._garc(r, CRY_GARC)
    out = []
    for i in range(FIRST_CRY_ENTRY, len(g.entries)):
        out.append(parse_cwar(g.read(i))[0])
    return out


def load_mapping(path):
    m = {}
    for line in open(path, encoding='utf-8'):
        line = line.split('#')[0].split()
        if len(line) == 2:
            m[line[0]] = int(line[1])
    return m


def table(first=1, last=807):
    """{national number: wave index} from gen57_cry_waves.json (the labels PVnnn / PVXYnnn / PVNJ_nnn of the cry sequences, matched in order
    and checked by spectrum against public cries; 13 species have none: 23 71 125 348 453-456 523 527 580 596 770)."""
    import json
    d = json.load(open(os.path.join(HERE, 'gen57_cry_waves.json')))
    return {int(k): v for k, v in d.items() if first <= int(k) <= last}


def main(argv):
    if argv[1] == 'mapping':
        first = int(argv[3]) if len(argv) > 3 else 1
        last = int(argv[4]) if len(argv) > 4 else 807
        open(argv[2], 'w').write(chr(10).join('%d %d' % kv for kv in sorted(table(first, last).items())) + chr(10))
        return 0
    cmd, rom = argv[1], argv[2]
    waves = cry_waves(rom)
    if cmd == 'list':
        for i, w in enumerate(waves):
            p = parse_cwav(w)
            print(i, p['rate'], p['nsamp'], p['loop'])
        return 0
    if cmd == 'make':
        import gen4_cry
        mapping, out = load_mapping(argv[3]), argv[4]
        os.makedirs(out, exist_ok=True)
        for key, idx in mapping.items():
            h, b, notes = gen4_cry.build_xd_3ds(parse_cwav(waves[idx]))
            open(os.path.join(out, key + '.hdr'), 'wb').write(h)
            open(os.path.join(out, key + '.body'), 'wb').write(b)
            print(key, idx, '; '.join(notes))
        return 0
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
