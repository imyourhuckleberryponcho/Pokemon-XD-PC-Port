"""Unit tests for usum_iris (made-up pictures, no game data).   python -I tools/n3ds/test_usum_iris.py"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usum_iris as U


def coord(sx, sy, tx, ty, wu, wv, unit=0, name='x'):
    return dict(name=name, unit=unit, scale=(sx, sy), trans=(tx, ty), wrap=(wu, wv))


class Bake(unittest.TestCase):
    def setUp(self):

        self.eye = np.full((256, 128, 4), (100, 100, 100, 255), np.uint8)
        self.tile = np.zeros((64, 64, 4), np.uint8)
        self.tile[..., 0] = 255
        self.tile[..., 3] = 255
        self.mask = np.zeros((256, 256, 4), np.uint8)
        self.mask[192 + 20:192 + 44, 64 + 20:64 + 44, 3] = 255
        self.ir = dict(tile=self.tile, co=coord(4, 4, 1.0, 0.0, 2, 2), mask=self.mask, mco=coord(1, 1, 1.75, 0.0, 2, 2, 2), bbox=(1.0056, 1.244, 2.795, 2.966))
        self.eco = coord(2, 1, 1.0, 0.0, 3, 2)

    def test_iris_only_where_the_mask_opens(self):
        out = U.bake(self.eye, self.eco, [self.ir])
        red = (out[..., 0] > 150) & (out[..., 1] < 80)
        self.assertGreater(red.sum(), 50)
        ys, xs = np.nonzero(red)
        self.assertTrue((ys >= 192).all() and (xs < 64).all())
        self.assertTrue((out[~red][..., 3] == 255).all())

    def test_without_a_mask_the_iris_shows_everywhere_the_plate_reaches(self):
        self.ir['mask'] = None
        out = U.bake(self.eye, self.eco, [self.ir])
        self.assertGreater(((out[..., 0] > 150) & (out[..., 1] < 80)).sum(), 50)

    def test_deterministic_and_input_kept(self):
        before = self.eye.copy()
        a = U.bake(self.eye, self.eco, [self.ir])
        b = U.bake(self.eye, self.eco, [self.ir])
        self.assertTrue((a == b).all())
        self.assertTrue((self.eye == before).all())

    def test_round5_is_what_the_decoder_makes(self):
        px = np.zeros((1, 3, 4), np.uint8)
        px[0, :, 0] = (0, 100, 255)
        px[0, :, 3] = (0, 128, 255)
        out = U.round5(px)
        self.assertEqual(list(out[0, :, 0]), [0, (12 << 3) | (12 >> 2), 255])
        self.assertTrue((out[..., 3] == 255).all())


if __name__ == '__main__':
    unittest.main()
