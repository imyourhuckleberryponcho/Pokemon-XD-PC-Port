"""Two Gen 4 packs equal? The Python pack (gen4_pack.py make) against the C++ one (pbr_models --gen4-pack).

  python tools/pbr_convert/compare_packs.py <python pack> <C++ pack>

Every file of both folders, byte for byte; the text files (.json .txt .toc-less lists) after the carriage returns are taken out (the
Python writes CR LF on Windows, the C++ always LF: docs/PBR_CONVERTER_CPP.md). Exit 0 = all equal.
"""
import os
import sys


def files(root):
    out = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(d, f)
            out[os.path.relpath(p, root).replace(os.sep, '/')] = p
    return out


def main():
    a, b = files(sys.argv[1]), files(sys.argv[2])
    bad = 0
    exact = text = 0
    for k in sorted(set(a) | set(b)):
        if k not in a or k not in b:
            print('only in one:', k, '(python)' if k in a else '(c++)')
            bad += 1
            continue
        x, y = open(a[k], 'rb').read(), open(b[k], 'rb').read()
        if x == y:
            exact += 1
        elif k.endswith(('.json', '.txt')) and x.replace(b'\r\n', b'\n') == y.replace(b'\r\n', b'\n'):
            text += 1
        else:
            bad += 1
            n = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
            print('DIFFERENT:', k, len(x), len(y), 'first difference at byte', n)
    print('%d files: %d byte for byte, %d equal after CR removal, %d different' % (len(set(a) | set(b)), exact, text, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
