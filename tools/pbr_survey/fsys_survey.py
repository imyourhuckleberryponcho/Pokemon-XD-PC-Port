"""Every member of every FSYS archive in a folder of extracted disc files (read only).

Usage: python tools/pbr_survey/fsys_survey.py <folder of extracted files> <out.tsv>
Writes one line per member: archive, index, id, type, unpacked size, stored size, packed (0/1),
full name, first 32 unpacked bytes in hex. Prints the header words of the first archive, the set of
header versions / flag words, and counts + bytes per type with the commonest first 4 bytes.
The .tsv holds game file names: keep it under build/.
"""
import collections, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import noddisc


def lzss_prefix(data, want):
    """First `want` bytes of an LZSS stream (same algorithm as noddisc.lzss_unpack)."""
    total = min(noddisc.be32(data, 4), want)
    out = bytearray(total)
    window = bytearray(0x1000)
    wpos, flags, dpos, i, n = 0xFEE, 0, 0, 16, len(data)
    while dpos < total and i < n:
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            b = data[i]
            i += 1
            out[dpos] = b
            dpos += 1
            window[wpos] = b
            wpos = (wpos + 1) & 0xFFF
        else:
            lo, hi = data[i], data[i + 1]
            i += 2
            off = lo | ((hi & 0xF0) << 4)
            for k in range((hi & 0xF) + 3):
                if dpos >= total:
                    break
                c = window[(off + k) & 0xFFF]
                out[dpos] = c
                dpos += 1
                window[wpos] = c
                wpos = (wpos + 1) & 0xFFF
    return bytes(out)


def main():
    root, out_path = sys.argv[1], sys.argv[2]
    versions = collections.Counter()
    by_type = collections.defaultdict(lambda: [0, 0, 0, collections.Counter(), 0])
    first = True
    n_arch = 0
    with open(out_path, 'w') as out:
        for dirpath, dirs, files in os.walk(root):
            for fn in sorted(files):
                if not fn.lower().endswith('.fsys'):
                    continue
                path = os.path.join(dirpath, fn)
                data = open(path, 'rb').read()
                if data[:4] != b'FSYS':
                    print('not FSYS:', fn)
                    continue
                n_arch += 1
                if first:
                    print('header of', fn, data[:0x30].hex(' ', 4))
                    first = False
                versions[(data[4:8].hex(), data[0x10:0x14].hex(), data[0x14:0x18].hex())] += 1
                for e in noddisc.fsys_entries(data):
                    s = noddisc.stored(data, e)
                    lz = s[:4] == b'LZSS'
                    head = lzss_prefix(s, 32) if (e.packed and lz) else s[:32]
                    t = by_type[e.type]
                    t[0] += 1
                    t[1] += e.size
                    t[2] += len(s)
                    t[3][head[:4]] += 1
                    t[4] += int(e.packed)
                    if e.packed and not lz:
                        print('packed but no LZSS magic:', fn, e.index, s[:8].hex())
                    out.write('\t'.join([fn, str(e.index), '%04X' % e.id, '%08X' % e.word0, str(e.type), str(e.size),
                                         str(len(s)), str(int(e.packed)), e.full, head.hex()]) + '\n')
    print(n_arch, 'archives; (version, word 0x10, word 0x14) -> archives:', dict(versions))
    print('type  members  packed  unpacked MB  stored MB  commonest first 4 bytes')
    for t in sorted(by_type):
        c, size, st, magic, pk = by_type[t]
        top = ', '.join('%s x%d' % (m.hex() + '/' + ''.join(chr(b) if 32 <= b < 127 else '.' for b in m), k)
                        for m, k in magic.most_common(4))
        print('%4d  %7d  %6d  %11.1f  %9.1f  %s' % (t, c, pk, size / 1048576, st / 1048576, top))


if __name__ == '__main__':
    main()
