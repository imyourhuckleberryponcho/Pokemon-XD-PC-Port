"""Bit comparison of the C++ 4x4 product / inverse with numpy's, on random matrices. usage: t_ops.py <folder with pose_sample_plain.dll / _fma.dll>"""
import ctypes
import sys

import numpy as np

for v in ('plain', 'fma'):
    dll = ctypes.CDLL('%s/pose_sample_%s.dll' % (sys.argv[1], v))
    P = ctypes.c_void_p
    rng = np.random.default_rng(1)
    bm = bi = 0
    N = 3000
    for _ in range(N):
        a = rng.normal(size=(4, 4))
        b = rng.normal(size=(4, 4))
        a[3] = [0, 0, 0, 1]
        b[3] = [0, 0, 0, 1]
        c = np.zeros((4, 4))
        dll.t_mul4(a.ctypes.data_as(P), b.ctypes.data_as(P), c.ctypes.data_as(P))
        bm += np.array_equal(c, a @ b)
        d = np.zeros((4, 4))
        dll.t_inv4(a.ctypes.data_as(P), d.ctypes.data_as(P))
        bi += np.array_equal(d, np.linalg.inv(a))
    print(v, 'product equal', bm, '/', N, ' inverse equal', bi, '/', N)
