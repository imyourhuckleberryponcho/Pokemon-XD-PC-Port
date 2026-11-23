"""Compare two folders of converter outputs (<name>.pkx), e.g. the Python references against the C++ port's files.

  python -I compare_pkx.py <reference folder> <new folder> [--tol 1e-5]

Per file: IDENTICAL (byte for byte), or a report of how the files differ: the sizes, how many bytes differ, and, treating the
files as big-endian 32-bit words, how many of those words differ as floats and the largest relative difference. A file
that differs ONLY in float words by at most --tol (relative) is "WITHIN TOLERANCE"; anything else is "DIFFERENT".
The tolerance is a stated rule, not a hope: float32 data made from double arithmetic may differ in the last bit (1.2e-7
relative) when two programs sum in another order; 1e-5 leaves room for a rounded value landing on the other side of a
quantisation step. Exit code 0 only when nothing is DIFFERENT.
"""
import argparse
import os
import struct
import sys


def words(b):
    n = len(b) // 4
    return struct.unpack('>%dI' % n, b[:n * 4]), struct.unpack('>%df' % n, b[:n * 4])


NOISE = 1e-9


def compare(a, b, tol):
    if a == b:
        return 'IDENTICAL', ''
    nbytes = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
    if len(a) != len(b):
        return 'DIFFERENT', 'sizes %d vs %d, %d bytes differ' % (len(a), len(b), nbytes)
    ai, af = words(a)
    bi, bf = words(b)
    bad = 0
    worst = 0.0
    nfloat = 0
    for x, y, fx, fy in zip(ai, bi, af, bf):
        if x == y:
            continue
        nfloat += 1
        if fx != fx or fy != fy:
            bad += 1
            continue
        d = abs(fx - fy)
        if abs(fx) < NOISE and abs(fy) < NOISE:
            continue
        rel = d / max(abs(fx), abs(fy), 1e-30)
        worst = max(worst, rel)
        if rel > tol or (fx == 0.0) != (fy == 0.0) and d > 1e-30:
            bad += 1
    tail = len(a) % 4 and a[-(len(a) % 4):] != b[-(len(a) % 4):]
    msg = '%d bytes, %d words differ, worst relative %.2e, %d of them beyond the tolerance' % (nbytes, nfloat, worst, bad)
    return ('WITHIN TOLERANCE' if not bad and not tail else 'DIFFERENT'), msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('new')
    ap.add_argument('--tol', type=float, default=1e-5)
    a = ap.parse_args()
    names = sorted(f[:-4] for f in os.listdir(a.ref) if f.endswith('.pkx'))
    counts = {}
    for n in names:
        p = os.path.join(a.new, n + '.pkx')
        if not os.path.exists(p):
            print('%-24s MISSING' % n)
            counts['MISSING'] = counts.get('MISSING', 0) + 1
            continue
        st, msg = compare(open(os.path.join(a.ref, n + '.pkx'), 'rb').read(), open(p, 'rb').read(), a.tol)
        counts[st] = counts.get(st, 0) + 1
        print('%-24s %s %s' % (n, st, msg))
    print(counts)
    sys.exit(1 if counts.get('DIFFERENT') or counts.get('MISSING') else 0)


if __name__ == '__main__':
    main()
