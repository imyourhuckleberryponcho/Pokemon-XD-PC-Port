"""Compares the C++ pose sampler (pose_sample.dll) with the converter's Python one, on real models of the player's dump, and runs whole
conversions with the C++ sampler plugged in (a monkey patch, only here) to compare the model files byte for byte.

  python compare.py <dll> <rom> <xd disc> <nat> [<nat> ...] [--convert]
Prints per species: frames sampled, bit-equal arrays, largest difference, Python and C++ seconds; with --convert the two model files' sizes
and whether they are byte-identical.
"""
import ctypes
import hashlib
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
N3DS = os.path.dirname(HERE)
sys.path.insert(0, N3DS)
import gen57_all as A
import gen57_data as D
import gen57_speed as SP
import gf_pose
import gf_to_xd
import make_proto as M
import noddisc
import pbr_models as PM
import usum_models as UM

dll = ctypes.CDLL(sys.argv[1])
P = ctypes.c_void_p
dll.sample_at.argtypes = [ctypes.c_int, P, P, P, P, P, ctypes.c_int, P, P, P, P, P, P, P, P, ctypes.c_int, P, P, P, P]


def ptr(a):
    return a.ctypes.data_as(P)


def sample_at_cpp(sk, mo, frames):
    nb = len(sk.names)
    parent = np.array(sk.parent, np.int32)
    ssc = np.array(sk.ssc, np.uint8)
    mb_index, mb_axis, ch_off, ch_cnt = [], [], [], []
    kf, kv, ks, khs = [], [], [], []
    for bn, d in mo['bones'].items():
        mb_index.append(sk.names.index(bn) if bn in sk.names else -1)
        mb_axis.append(1 if d['axis'] else 0)
        for k in range(9):
            keys = d['ch'][k] or []
            ch_off.append(len(kf))
            ch_cnt.append(len(keys))
            for key in keys:
                kf.append(float(key[0]))
                kv.append(float(key[1]))
                ks.append(0.0 if key[2] is None else float(key[2]))
                khs.append(0 if key[2] is None else 1)
    mb_index, mb_axis = np.array(mb_index, np.int32), np.array(mb_axis, np.uint8)
    ch_off, ch_cnt = np.array(ch_off, np.int32), np.array(ch_cnt, np.int32)
    kf, kv, ks, khs = (np.array(x or [0], t) for x, t in ((kf, float), (kv, float), (ks, float), (khs, np.uint8)))
    fr = np.array(frames, float)
    S, R, T = (np.zeros((len(frames), nb, 3)) for _ in range(3))
    raw_s, raw_r, raw_t = (np.ascontiguousarray(x, float) for x in (sk.raw_s, sk.raw_r, sk.raw_t))
    dll.sample_at(nb, ptr(parent), ptr(ssc), ptr(raw_s), ptr(raw_r), ptr(raw_t), len(mb_index), ptr(mb_index), ptr(mb_axis), ptr(ch_off), ptr(ch_cnt),
                  ptr(kf), ptr(kv), ptr(ks), ptr(khs), len(frames), ptr(fr), ptr(S), ptr(R), ptr(T))
    return S, R, T


def main():
    args = [x for x in sys.argv[2:] if not x.startswith('--')]
    rom, disc, nats = args[0], args[1], [int(x) for x in args[2:]]
    do_convert = '--convert' in sys.argv
    PM.find_nod()
    u = D.Usum(rom)
    d = PM.open_disc(disc, 'GXXE01', 'XD')
    xdmap = UM.xd_species_models(d)
    standins = A.pick_standins(u, xdmap)
    mn, _cw = A.load_tables()
    py_sample = gf_to_xd.sample_at
    tot_py = tot_cpp = 0.0
    for nat in nats:
        gm, texs, mots, grp = M.load_species(u.r, A.model_number(nat, mn), None)
        sk = gf_pose.Skeleton(gm['bones'])
        for role, mo in sorted(mots.items()):
            frames = [float(k) for k in range(max(mo['frames'], 1) + 1)] + [k * 0.37 for k in range(40)]
            t0 = time.perf_counter()
            S0, R0, T0 = py_sample(sk, mo, frames)
            t1 = time.perf_counter()
            S1, R1, T1 = sample_at_cpp(sk, mo, frames)
            t2 = time.perf_counter()
            tot_py += t1 - t0
            tot_cpp += t2 - t1
            eq = [bool(np.array_equal(a, b)) for a, b in ((S0, S1), (R0, R1), (T0, T1))]
            diff = max(float(np.abs(a - b).max()) for a, b in ((S0, S1), (R0, R1), (T0, T1)))
            print('%d %-8s frames %3d bones %3d  bit-equal S/R/T %s  max diff %.3g  python %.3f s  C++ %.4f s  x%.0f' % (
                nat, role, len(frames), len(sk.names), eq, diff, t1 - t0, t2 - t1, (t1 - t0) / max(t2 - t1, 1e-9)), flush=True)
        if do_convert:
            arch, idx, _n = xdmap[standins[nat]]
            data = d.file(arch)
            ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
            xdb = noddisc.extract(data, ents[0])
            p = u.personal(nat)
            t0 = time.perf_counter()
            a, _i, st = SP.convert_ladder(gf_to_xd.convert, gm, texs, mots, xdb, p.height_cm)
            t1 = time.perf_counter()
            gf_to_xd.sample_at = lambda sk_, mo_, fr_: sample_at_cpp(sk_, mo_, fr_)
            b, _i, st2 = SP.convert_ladder(gf_to_xd.convert, gm, texs, mots, xdb, p.height_cm)
            t2 = time.perf_counter()
            gf_to_xd.sample_at = py_sample
            print('CONVERT %d: python %.1f s (step %d, %d bytes)  with C++ sampler %.1f s (step %d, %d bytes)  byte-identical: %s' % (
                nat, t1 - t0, st, len(a), t2 - t1, st2, len(b), a == b), flush=True)
    print('total sampling: python %.2f s, C++ %.3f s, ratio x%.0f' % (tot_py, tot_cpp, tot_py / max(tot_cpp, 1e-9)))


if __name__ == '__main__':
    main()
