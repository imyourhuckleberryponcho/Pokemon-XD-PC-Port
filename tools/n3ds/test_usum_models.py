"""Unit tests for tools/n3ds/usum_models.py's pure parts (no game data): python -I test_usum_models.py"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usum_models as U


def fake_dol(addr, payload):
    """A 1-section main.dol: text section 0 at file offset 0x100, load address addr."""
    d = bytearray(0x100 + len(payload) + 16)
    struct.pack_into('>I', d, 0, 0x100)
    struct.pack_into('>I', d, 0x48, addr)
    struct.pack_into('>I', d, 0x90, len(payload))
    d[0x100:0x100 + len(payload)] = payload
    return bytes(d)


class T(unittest.TestCase):
    def test_dol_read(self):
        dol = fake_dol(0x80400000, bytes(range(64)))
        self.assertEqual(U.dol_read(dol, 0x80400010, 4), bytes([16, 17, 18, 19]))
        with self.assertRaises(KeyError):
            U.dol_read(dol, 0x80500000, 4)
        with self.assertRaises(KeyError):
            U.dol_read(dol, 0x8040003E, 8)

    def test_lookup_prefers_form_00(self):
        groups = {0: (1, '00'), 1: (3, '51'), 2: (3, '00'), 3: (3, '01'), 4: (650, '00'), 5: (701, '00'), 6: (0, '00')}
        t = U.lookup_table(groups)
        self.assertEqual(t[1], 0)
        self.assertEqual(t[3], 2)
        self.assertNotIn(650, t)
        self.assertNotIn(701, t)
        t2 = U.lookup_table(groups, {701: 676})
        self.assertEqual(t2[676], 5)
        self.assertNotIn(0, t)

    def test_lookup_first_when_no_form_00(self):
        t = U.lookup_table({4: (479, '13'), 5: (479, '16')})
        self.assertEqual(t[479], 4)


    def test_dex_map_covers_kalos_alola_once(self):
        m = U.dex_map()
        self.assertEqual(sorted(set(m.values())), list(range(650, 808)))
        self.assertEqual(len(m), 158)

if __name__ == '__main__':
    unittest.main()
