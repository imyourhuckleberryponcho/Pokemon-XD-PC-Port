"""Unit tests for the converter's speed-ups (made-up inputs, no dump): python -I tools/n3ds/test_gen57_speed.py"""
import os
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import garc
import gen57_speed as SP
import gf_to_xd


def lz11_compress(data):
    """A small LZ11 writer (the three back-reference forms) for the tests: greedy matches, up to 0x111 + 0xFFFF long."""
    out = bytearray([0x11, len(data) & 255, len(data) >> 8 & 255, len(data) >> 16 & 255])
    p, n = 0, len(data)
    while p < n:
        flags, chunk = 0, bytearray()
        for bit in range(7, -1, -1):
            if p >= n:
                break
            best_len, best_disp = 0, 0
            for disp in range(1, min(p, 4096) + 1):
                ln = 0
                while p + ln < n and ln < 0x110 and data[p + ln] == data[p + ln - disp]:
                    ln += 1
                if ln > best_len:
                    best_len, best_disp = ln, disp
            if best_len >= 3:
                flags |= 1 << bit
                d = best_disp - 1
                if best_len <= 16:
                    chunk += bytes([(best_len - 1) << 4 | d >> 8, d & 255])
                else:
                    ln = best_len - 0x11
                    chunk += bytes([ln >> 4, (ln & 15) << 4 | d >> 8, d & 255])
                p += best_len
            else:
                chunk.append(data[p])
                p += 1
        out.append(flags)
        out += chunk
    return bytes(out)


class Lz11(unittest.TestCase):
    def test_round_trip_with_overlapping_copies(self):
        rng = np.random.default_rng(3)
        data = bytes(rng.integers(0, 4, 3000, dtype=np.uint8)) + b'ab' * 700 + bytes(rng.integers(0, 256, 500, dtype=np.uint8)) + b'\x00' * 900
        packed = lz11_compress(data)
        self.assertEqual(garc.lz11(packed), data)
        self.assertEqual(garc.maybe(packed), data)

    def test_prefix_is_the_start_of_the_whole(self):
        data = (b'0123456789abcdef' * 200) + bytes(range(256)) * 8
        packed = lz11_compress(data)
        for lim in (1, 17, 100, 4096, 10 ** 6):
            part = garc.lz11(packed, lim)
            self.assertEqual(part, data[:len(part)])
            self.assertGreaterEqual(len(part), min(lim, len(data)))

    def test_back_reference_before_the_start_is_an_error(self):
        bad = bytes([0x11, 8, 0, 0, 0x40, 0x00, 0x05])
        with self.assertRaises(IndexError):
            garc.lz11(bad)


class Ladder(unittest.TestCase):
    def fake(self, sizes):
        calls = []

        def convert(gm, texs, mots, xd, mode, log, opts, hover):
            calls.append(opts['max_side'] * 10000 + opts['decimate'])
            k = len(calls) - 1
            return b'x' * sizes[min(k, len(sizes) - 1)], dict(tris=1, size=0)
        return convert, calls

    def test_starts_at_the_table_step_and_matches_the_climb(self):
        sizes = [SP.CAP + 5, SP.CAP + 1, 1000]
        conv, calls = self.fake(sizes)
        pkx, info, step = SP.convert_ladder(conv, None, None, None, None, 100)
        self.assertEqual((step, len(pkx), len(calls)), (2, 1000, 3))
        conv2, calls2 = self.fake([1000])
        pkx2, info2, step2 = SP.convert_ladder(conv2, None, None, None, None, 100, start=2)
        self.assertEqual((step2, pkx2, calls2), (2, pkx, calls[2:]))

    def test_start_beyond_the_ladder_does_the_last_step(self):
        conv, calls = self.fake([SP.CAP + 1])
        pkx, info, step = SP.convert_ladder(conv, None, None, None, None, 100, start=99)
        self.assertEqual((step, len(calls)), (len(SP.LADDER) - 1, 1))

    def test_nothing_fits_returns_the_last_step(self):
        conv, calls = self.fake([SP.CAP + 1])
        pkx, info, step = SP.convert_ladder(conv, None, None, None, None, 100)
        self.assertEqual((step, len(calls)), (len(SP.LADDER) - 1, len(SP.LADDER)))


class Memo(unittest.TestCase):
    def test_pose_memo_gives_copies(self):
        class Sk:
            names = ['a']
            bones = ['x']
        calls = []
        old = gf_to_xd._sample_at
        gf_to_xd._sample_at = lambda sk, mo, frames: (calls.append(1), (np.zeros((2, 1, 3)), np.zeros((2, 1, 3)), np.zeros((2, 1, 3))))[1]
        try:
            mo = {}
            a = gf_to_xd.sample_at(Sk, mo, [0.0, 1.0])
            a[2][:, 0, 1] += 5.0
            b = gf_to_xd.sample_at(Sk, mo, [0.0, 1.0])
            self.assertEqual(len(calls), 1)
            self.assertEqual(float(np.abs(b[2]).max()), 0.0)
            gf_to_xd.sample_at(Sk, mo, [0.0, 2.0])
            self.assertEqual(len(calls), 2)
        finally:
            gf_to_xd._sample_at = old


class Table(unittest.TestCase):
    def test_table_is_plain_numbers_and_keyed_by_dump(self):
        import json
        if not os.path.isfile(SP.TABLE_FILE):
            self.skipTest('no table yet')
        t = json.load(open(SP.TABLE_FILE, encoding='utf-8'))
        self.assertEqual(t['version'], SP.VERSION)
        for fp, d in t['dumps'].items():
            self.assertRegex(fp, r'^\d+-[0-9a-f]{16}$')
            for k, v in d.get('ladder', {}).items():
                self.assertTrue(494 <= int(k) <= 807 and 0 < v < len(SP.LADDER))
            for kind in ('views', 'wide'):
                for k, e in d.get(kind, {}).items():
                    self.assertTrue(494 <= int(k) <= 807)
                    self.assertEqual(set(e), {'key', 'view'})
                    if e['view'] is not None:
                        self.assertTrue({'x', 'y', 'z', 'r'} <= set(e['view']))

    def test_unknown_dump_or_switch_gives_nothing(self):
        self.assertEqual(SP.load(None), {})
        os.environ['GEN57_NO_SPEED_TABLE'] = '1'
        try:
            self.assertEqual(SP.load(__file__), {})
        finally:
            del os.environ['GEN57_NO_SPEED_TABLE']
        self.assertEqual(SP.load(__file__), {})


class Workers(unittest.TestCase):
    def test_memory_cuts_the_workers(self):
        import gen57_memory as GM
        old = os.environ.get('GEN57_MEMORY_MB')
        try:
            os.environ['GEN57_MEMORY_MB'] = '16000'
            self.assertEqual(GM.workers_for(6), 6)
            os.environ['GEN57_MEMORY_MB'] = '5000'
            self.assertEqual(GM.workers_for(6), 2)
            os.environ['GEN57_MEMORY_MB'] = '1000'
            self.assertEqual(GM.workers_for(6), 1)
        finally:
            if old is None:
                os.environ.pop('GEN57_MEMORY_MB', None)
            else:
                os.environ['GEN57_MEMORY_MB'] = old

    def test_free_memory_is_told_on_this_system(self):
        import gen57_memory as GM
        os.environ.pop('GEN57_MEMORY_MB', None)
        self.assertGreater(GM.available_memory_mb() or 1, 0)

    def test_split_runs(self):
        import gen57_faces_fast as FF
        self.assertEqual([len(p) for p in FF.split_runs(list(range(10)), 3)], [3, 3, 4])
        self.assertEqual(sum(FF.split_runs(list(range(7)), 2), []), list(range(7)))
        self.assertEqual(FF.split_runs([], 2), [[]])
        self.assertEqual(len(FF.split_runs([1], 3)), 1)


if __name__ == '__main__':
    unittest.main()
