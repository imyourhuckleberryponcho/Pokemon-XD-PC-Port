"""usum-broken fixes: per-species recolour, the colour boost option, the axis-angle default, hover threshold.  python -I tools/n3ds/test_usum_broken.py"""
import os, sys, unittest
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gf_pose, gf_to_xd, usum_recolour


def flat(rgb, n=4):
    im = np.zeros((n, n, 4), np.uint8)
    im[..., :3] = rgb
    im[..., 3] = 255
    return im


class Recolour(unittest.TestCase):
    def test_voltorb_salmon_becomes_red(self):
        fn = usum_recolour.fix_for(100, 'BodySpc')[0]
        out = fn(flat((231, 145, 135)))[0, 0, :3].astype(int)
        self.assertGreater(out[0], 180)
        self.assertLess(out[1], 80)
        self.assertLess(out[2], 80)

    def test_voltorb_white_half_stays(self):
        fn = usum_recolour.fix_for(100, 'BodySpc')[0]
        out = fn(flat((222, 226, 238)))[0, 0, :3].astype(int)
        self.assertGreater(out.min(), 200)

    def test_houndour_grey_becomes_dark_cream_stays(self):
        fn = usum_recolour.fix_for(228, 'Body00')[0]
        self.assertLess(fn(flat((95, 92, 88)))[0, 0, :3].max(), 60)
        self.assertGreater(fn(flat((225, 223, 215)))[0, 0, :3].min(), 200)

    def test_gligar_pink_becomes_purple(self):
        fn = usum_recolour.fix_for(207, 'Body')[0]
        r, g, b = [int(x) for x in fn(flat((222, 150, 200)))[0, 0, :3]]
        self.assertGreater(b, g + 20)
        self.assertGreater(b, r - 5)

    def test_jumpluff_cotton_white_body_untouched(self):
        self.assertTrue(usum_recolour.fix_for(189, 'BodyAWatacco_Cotton'))
        self.assertFalse(usum_recolour.fix_for(189, 'BodyA'))
        out = usum_recolour.fix_for(189, 'BodyAWatacco_Cotton')[0](flat((229, 222, 201)))[0, 0, :3]
        self.assertGreater(out.min(), 220)

    def test_other_species_untouched(self):
        self.assertFalse(usum_recolour.fix_for(25, 'Body'))


class Boost(unittest.TestCase):
    def test_sat_boost_raises_colour_keeps_grey(self):
        im = flat((200, 150, 150))
        out = gf_to_xd.sat_boost(im, 0.25)[0, 0, :3].astype(int)
        self.assertGreater(out[0] - out[1], 50 * 1.2)
        g = gf_to_xd.sat_boost(flat((120, 120, 120)), 0.25)[0, 0, :3].astype(int)
        self.assertEqual(len(set(g)), 1)


class Axis(unittest.TestCase):
    def test_unkeyed_axis_angle_channel_is_zero(self):
        bones = [dict(name='root', parent='root', flags=0, scale=(1, 1, 1), rot=(0, 0, 1.57), pos=(0, 0, 0))]
        sk = gf_pose.Skeleton(bones)
        mo = dict(bones={'root': dict(axis=True, ch=[[]] * 9)}, frames=1, vis={}, mats=[])
        s, r, t, ax = gf_pose.sample_motion(sk, mo, 0)
        self.assertTrue(ax[0])
        self.assertAlmostEqual(float(abs(r[0]).max()), 0.0)


if __name__ == '__main__':
    unittest.main()
