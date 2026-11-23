"""Which way of doing LU / inverse reproduces numpy's inverse bit for bit (OpenBLAS getrf + getrs)? Exact-fma emulation with fractions."""
import itertools
from fractions import Fraction

import numpy as np


def fma(a, b, c):
    return float(Fraction(a) * Fraction(b) + Fraction(c))


def mul_add(use_fma, a, b, c):
    return fma(a, b, c) if use_fma else a * b + c


def dot(use_fma, xs, ys, order):
    if not xs:
        return 0.0
    idx = range(len(xs)) if order == 'fwd' else range(len(xs) - 1, -1, -1)
    acc = 0.0
    first = True
    for k in idx:
        acc = xs[k] * ys[k] if first else mul_add(use_fma, xs[k], ys[k], acc)
        first = False
    return acc


def inverse(A, left, fm, recip_scale, recip_solve, dotorder, sub_style, fm2=None):
    fm2 = fm if fm2 is None else fm2
    n = 4
    a = [list(map(float, r)) for r in A]
    piv = [0] * n
    if left:

        for j in range(n):
            for i in range(j):
                if piv[i] != i:
                    a[i][j], a[piv[i]][j] = a[piv[i]][j], a[i][j]
            for i in range(1, j):
                d = dot(fm, [a[i][k] for k in range(i)], [a[k][j] for k in range(i)], dotorder)
                a[i][j] = a[i][j] - d
            for i in range(j, n):
                d = dot(fm, [a[i][k] for k in range(j)], [a[k][j] for k in range(j)], dotorder)
                a[i][j] = a[i][j] - d if sub_style == 'dot' else mul_add(fm, -1.0, d, a[i][j])
            jp = max(range(j, n), key=lambda i: (abs(a[i][j]), -i))
            piv[j] = jp
            if a[jp][j] == 0:
                return None
            if jp != j:
                for c in range(j + 1):
                    a[j][c], a[jp][c] = a[jp][c], a[j][c]
            r = 1.0 / a[j][j]
            for i in range(j + 1, n):
                a[i][j] = a[i][j] * r if recip_scale else a[i][j] / a[j][j]
    else:
        for k in range(n):
            p = max(range(k, n), key=lambda i: (abs(a[i][k]), -i))
            piv[k] = p
            if a[p][k] == 0:
                return None
            if p != k:
                a[k], a[p] = a[p], a[k]
            r = 1.0 / a[k][k]
            for i in range(k + 1, n):
                a[i][k] = a[i][k] * r if recip_scale else a[i][k] / a[k][k]
            for i in range(k + 1, n):
                for j in range(k + 1, n):
                    a[i][j] = mul_add(fm, -a[i][k], a[k][j], a[i][j])
    B = [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for k in range(n):
        if piv[k] != k:
            B[k], B[piv[k]] = B[piv[k]], B[k]
    for c in range(n):
        for i in range(n):
            if sub_style == 'dot' or left:
                d = dot(fm2, [a[i][k] for k in range(i)], [B[k][c] for k in range(i)], dotorder)
                B[i][c] = B[i][c] - d
            else:
                for k in range(i):
                    B[i][c] = mul_add(fm2, -a[i][k], B[k][c], B[i][c])
        for i in range(n - 1, -1, -1):
            if sub_style == 'dot' or left:
                d = dot(fm2, [a[i][k] for k in range(i + 1, n)], [B[k][c] for k in range(i + 1, n)], dotorder)
                v = B[i][c] - d
            else:
                v = B[i][c]
                for k in range(i + 1, n):
                    v = mul_add(fm2, -a[i][k], B[k][c], v)
            B[i][c] = v * (1.0 / a[i][i]) if recip_solve else v / a[i][i]
    return np.array(B)


def main():
    rng = np.random.default_rng(7)
    mats = []
    for _ in range(300):
        a = rng.normal(size=(4, 4))
        a[3] = [0, 0, 0, 1]
        mats.append(a)
    res = []
    for fm, fm2, order, sub in itertools.product((True, False), (True, False), ('fwd', 'bwd'), ('dot', 'seq')):
        ok = sum(np.array_equal(inverse(a, True, fm, True, True, order, sub, fm2), np.linalg.inv(a)) for a in mats)
        res.append((ok, fm, fm2, order, sub))
    for r in sorted(res, reverse=True)[:8]:
        print(r)


if __name__ == '__main__':
    main()
