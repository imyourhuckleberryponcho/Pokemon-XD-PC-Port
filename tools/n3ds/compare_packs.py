"""File by file comparison of two folders (the proof that a faster way of making a pack makes the same pack).

  python compare_packs.py <folder A> <folder B> [--name label] [--max-list 20]

Every file in A must be in B with the same bytes (SHA-256). A PNG whose bytes differ is compared by its decoded pixels (so a different
compressor setting is told apart from a different picture). Prints counts and the first differing files; exit 0 only when the folders are the
same (bytes, or for PNGs pixels).
"""
import argparse
import hashlib
import os
import sys


def tree(root):
    out = {}
    for d, _dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            out[os.path.relpath(p, root).replace('\\', '/')] = p
    return out


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def pixels_equal(a, b):
    try:
        from PIL import Image
        import numpy as np
        with Image.open(a) as x, Image.open(b) as y:
            x, y = x.convert('RGBA'), y.convert('RGBA')
            return x.size == y.size and bool((np.asarray(x) == np.asarray(y)).all())
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--name', default='')
    ap.add_argument('--max-list', type=int, default=20)
    a = ap.parse_args()
    ta, tb = tree(a.a), tree(a.b)
    only_a = sorted(set(ta) - set(tb))
    only_b = sorted(set(tb) - set(ta))
    same = png_pixels = 0
    diff = []
    nbytes = 0
    for k in sorted(set(ta) & set(tb)):
        nbytes += os.path.getsize(ta[k])
        if os.path.getsize(ta[k]) == os.path.getsize(tb[k]) and sha(ta[k]) == sha(tb[k]):
            same += 1
        elif k.lower().endswith('.png') and pixels_equal(ta[k], tb[k]):
            png_pixels += 1
        else:
            diff.append(k)
    print('%s: %d files in A, %d in B; byte-identical %d, PNG same pixels but other bytes %d, different %d, only in A %d, only in B %d (%.1f MB compared)' % (
        a.name or a.a, len(ta), len(tb), same, png_pixels, len(diff), len(only_a), len(only_b), nbytes / 1e6))
    for label, lst in (('different', diff), ('only in A', only_a), ('only in B', only_b)):
        for k in lst[:a.max_list]:
            print('  %s: %s' % (label, k))
        if len(lst) > a.max_list:
            print('  ... and %d more %s' % (len(lst) - a.max_list, label))
    return 0 if not (diff or only_a or only_b) else 1


if __name__ == '__main__':
    sys.exit(main())
