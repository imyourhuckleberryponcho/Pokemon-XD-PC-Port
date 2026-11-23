"""Compares a Python-made Gen 5-7 pack folder with a C++-made one (round 4): every file by SHA-256, after the Windows Python's line ends / code page are
undone on the Python side (CRLF -> LF; tables.txt cp1252 -> UTF-8) when --py-windows is given. The files the game draws (faces, Pokedex pictures) are not compared.
  python compare_data_packs.py <python pack> <cpp pack> [--py-windows] [--ignore a,b]"""
import hashlib
import os
import sys


def tree(root):
    out = {}
    for d, _x, fs in os.walk(root):
        for f in fs:
            p = os.path.join(d, f)
            out[os.path.relpath(p, root).replace(chr(92), '/')] = p
    return out


def norm(p, rel, py_windows):
    b = open(p, 'rb').read()
    if py_windows and not rel.endswith(('.fsys', '.toc', '.png')):
        b = b.replace(b'\r\n', b'\n')
        if rel == 'tables.txt':
            b = b.decode('cp1252').encode('utf-8')
    return hashlib.sha256(b).hexdigest()


def main():
    a, b = sys.argv[1], sys.argv[2]
    pyw = '--py-windows' in sys.argv
    ign = set(sys.argv[sys.argv.index('--ignore') + 1].split(',')) if '--ignore' in sys.argv else set()
    ta, tb = tree(a), tree(b)
    skip = lambda k: k.startswith('textures/faces/') or k.endswith('.ok') or k in ign or k == 'faces.json'
    ka, kb = {k for k in ta if not skip(k)}, {k for k in tb if not skip(k)}
    same = 0
    diff = []
    for k in sorted(ka & kb):
        if norm(ta[k], k, pyw) == norm(tb[k], k, False):
            same += 1
        else:
            diff.append(k)
    print('files: python %d, cpp %d; identical %d, different %d, only python %d, only cpp %d' % (len(ka), len(kb), same, len(diff), len(ka - kb), len(kb - ka)))
    for k in diff[:30]:
        print('  different:', k)
    for k in sorted(ka - kb)[:30]:
        print('  only python:', k)
    for k in sorted(kb - ka)[:30]:
        print('  only cpp:', k)
    return 0 if not (diff or ka - kb or kb - ka) else 1


if __name__ == '__main__':
    sys.exit(main())
