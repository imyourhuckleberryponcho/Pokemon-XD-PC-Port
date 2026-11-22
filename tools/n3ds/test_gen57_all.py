"""Unit tests for gen57_all.py's rules (made-up inputs, no dump): python -I tools/n3ds/test_gen57_all.py"""
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_all as A


class Tables(unittest.TestCase):
    def test_model_numbers_are_a_bijection_from_650(self):
        mn, cw = A.load_tables()
        nums = [A.model_number(n, mn) for n in range(494, 808)]
        self.assertEqual(len(set(nums)), len(nums))
        self.assertEqual(A.model_number(722, mn), 841)
        self.assertEqual(A.model_number(650, mn), 720)
        self.assertEqual(A.model_number(506, mn), 506)

    def test_group_overrides_name_known_species(self):
        d = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json')))
        for k, g in d['group_override'].items():
            self.assertTrue(494 <= int(k) <= 807)
            self.assertTrue(0 <= g < 1172)

    def test_cry_waves_in_range_and_unique(self):
        mn, cw = A.load_tables()
        v = list(cw.values())
        self.assertTrue(all(0 <= w < 965 for w in v))
        self.assertEqual(len(v), len(set(v)))

    def test_move_rule(self):
        self.assertEqual(A.move_row(10, set()), 10)
        self.assertEqual(A.move_row(400, set()), 420)
        self.assertIsNone(A.move_row(488, set()))
        self.assertEqual(A.move_row(488, {488}), 508)
        self.assertIsNone(A.move_row(729, {729}))

    def test_rows(self):
        self.assertEqual(506 + A.ROW_SHIFT, 532)
        self.assertEqual(494 + A.ROW_SHIFT, 520)
        self.assertEqual(494 + A.PKX_SHIFT, 525)


if __name__ == '__main__':
    unittest.main()
