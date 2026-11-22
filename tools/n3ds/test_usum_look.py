"""Unit tests for usum_look (no game data needed).   python -I tools/n3ds/test_usum_look.py"""
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usum_look as U


def motion(frames_v, name='Eye'):
    """A fake motion: material `name`, unit 0, V channel holding frames_v (one value per frame, linear keys)."""
    ch = [(f, v, None) for f, v in enumerate(frames_v)]
    return {'frames': len(frames_v) - 1, 'mats': [{'name': name, 'unit': 0, 'tx': None, 'ty': ch}], 'vis': {}}


class WinkPattern(unittest.TestCase):
    def test_wink_four_frames(self):
        self.assertEqual(U.wink_pattern(4, 'wink', 0.0, 0.25, 0.5), [0.0, 0.25, 0.5, 0.25, 0.0])

    def test_wink_length_and_ends_open(self):
        for n in (1, 2, 3, 4, 8, 20):
            v = U.wink_pattern(n, 'wink', 0.0, 0.25, 0.5)
            self.assertEqual(len(v), n + 1)
            self.assertEqual(v[-1], 0.0)
        self.assertIn(0.5, U.wink_pattern(8, 'wink', 0.0, 0.25, 0.5))

    def test_sleep_goes_shut_and_stays(self):
        v = U.wink_pattern(9, 'sleep', 0.0, 0.25, 0.5)
        self.assertEqual((v[0], v[-1]), (0.0, 0.5))
        self.assertEqual(v, sorted(v))

    def test_wake_is_sleep_reversed(self):
        self.assertEqual(U.wink_pattern(9, 'wake', 0.0, 0.25, 0.5), U.wink_pattern(9, 'sleep', 0.0, 0.25, 0.5)[::-1])


class ShutLevels(unittest.TestCase):
    def test_own_blink_rows(self):
        mots = {'idle': motion([0.0] * 3), '_blink': motion([0.0, 0.25, 0.5, 0.25, 0.0])}
        self.assertEqual(U.shut_levels(mots, 'Eye', (0.0, 0.0)), (0.0, 0.25, 0.5))

    def test_pained_row_is_not_shut(self):
        mots = {'idle': motion([0.0] * 3), 'hit': motion([0.0, 0.75, 0.75, 0.5, 0.0])}
        op, half, shut = U.shut_levels(mots, 'Eye', (0.0, 0.0))
        self.assertEqual((op, shut), (0.0, 0.5))

    def test_no_closed_row_half_way_fallback(self):
        mots = {'idle': motion([0.0] * 3), '_blink': motion([0.0, 0.0])}
        op, half, shut = U.shut_levels(mots, 'Eye', (0.0, 0.0))
        self.assertEqual((op, half, shut), (0.0, 0.0, 0.0))


class StepKeys(unittest.TestCase):
    def test_runs_hold_until_change(self):
        keys = U.step_keys([0.0, 0.0, 0.5, 0.5, 0.5, 0.0])
        self.assertEqual([(k[1], k[3]) for k in keys[:3]], [(0.0, 2), (0.5, 3), (0.0, 1)])
        self.assertTrue(all(k[0] == U.OP_CON for k in keys))

    def test_constant_value_two_keys(self):
        keys = U.step_keys([0.25] * 5)
        self.assertEqual(len(keys), 2)
        self.assertEqual(keys[0][1], 0.25)
        self.assertEqual(keys[-1][3], 0)


class Sizes(unittest.TestCase):
    def test_fit_matches_formula(self):
        for cm in (30, 100, 170, 350):
            self.assertAlmostEqual(U.mainline_box_height(cm), math.exp(-1.5484 + 0.8229 * math.log(cm)))

    def test_monotonic_and_clamped(self):
        self.assertLess(U.mainline_box_height(50), U.mainline_box_height(200))
        self.assertEqual(U.mainline_box_height(1), U.mainline_box_height(10))

    def test_charizard_value(self):
        self.assertAlmostEqual(U.mainline_box_height(170), 14.55, places=2)


class Hover(unittest.TestCase):
    def test_walker_stays_grounded(self):
        self.assertEqual(U.model_hover(0.05, 1.0, 5.0), 0.0)

    def test_floater_hovers_scaled(self):
        self.assertAlmostEqual(U.model_hover(0.3, 1.0, 5.0), 1.5)

    def test_threshold_edge_and_zero_height(self):
        self.assertEqual(U.model_hover(0.12, 1.0, 5.0), 0.0)
        self.assertEqual(U.model_hover(0.5, 0.0, 5.0), 0.0)


if __name__ == '__main__':
    unittest.main()
