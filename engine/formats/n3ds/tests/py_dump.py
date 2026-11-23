"""The Python side of n3ds_tool's tests: prints the same canonical text from tools/n3ds (the reference).
  python -I py_dump.py list|detect|garc|lz11 <dump> [path|prefix] [n]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'tools', 'n3ds'))
sys.path.insert(0, N3)
import ncch   # noqa: E402
import garc   # noqa: E402
import gamedetect   # noqa: E402


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    cmd, dump = sys.argv[1], sys.argv[2]
    r, prod = ncch.open_rom(dump)
    if cmd == 'list':
        pre = sys.argv[3] if len(sys.argv) > 3 else ''
        print(prod, len(r.files), 'files')
        for k, (o, s) in sorted(r.files.items()):
            if k.startswith(pre):
                print('%10d %s' % (s, k))
    elif cmd == 'detect':
        d = gamedetect.detect(r, prod)
        for k in ('models', 'personal', 'levelup', 'text'):
            print(k, d[k])
    else:
        g = gamedetect._garc(r, sys.argv[3])
        n = len(g.entries)
        if cmd == 'lz11' and len(sys.argv) > 4:
            n = min(n, int(sys.argv[4]))
        print('entries', len(g.entries))
        for i in range(n):
            parts = [str(i)]
            for s in range(32):
                if not g.entries[i][s]:
                    continue
                raw = g.read(i, s)
                d = garc.maybe(raw)
                t = ' %d:%d:%016x' % (s, len(d), fnv(d))
                if cmd == 'lz11' and raw and raw[0] == 0x11:
                    t += ':p%016x' % fnv(garc.lz11(raw, 64))
                parts.append(t)
            print(parts[0] + ''.join(parts[1:]))


if __name__ == '__main__':
    main()
