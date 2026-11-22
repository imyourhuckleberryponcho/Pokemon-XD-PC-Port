"""Segment scale compensation (gf_pose.Skeleton.ssc_to_plain): a stretched chain keeps its size.  python -I tools/n3ds/test_usum_anim.py"""
import os, sys, unittest
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gf_pose


def chain(flags):
    bones = [dict(name='b%d' % i, parent='b%d' % (i - 1) if i else 'b0', flags=flags[i], scale=(1, 1, 1), rot=(0, 0, 0), pos=(0, 0, 0 if i == 0 else 1)) for i in range(4)]
    return gf_pose.Skeleton(bones)


class Ssc(unittest.TestCase):
    def test_stretch_does_not_compound(self):
        sk = chain([0, 1, 1, 1])
        s = np.ones((4, 3)); r = np.zeros((4, 3)); t = sk.raw_t.copy()
        s[1:] = (3.0, 1.0, 1.0)
        s2, r2, t2 = sk.ssc_to_plain(s, r, t)
        W = sk.world(s2, r2, t2)

        self.assertAlmostEqual(np.linalg.norm(W[3][:3, 0]), 3.0, places=5)
        plain = sk.world(s, r, t)
        self.assertAlmostEqual(np.linalg.norm(plain[3][:3, 0]), 27.0, places=5)

    def test_unit_scales_unchanged(self):
        sk = chain([0, 1, 1, 1])
        s = np.ones((4, 3)); r = np.full((4, 3), 0.2); t = sk.raw_t.copy()
        s2, r2, t2 = sk.ssc_to_plain(s, r, t)
        self.assertTrue(np.array_equal(s2, s) and np.array_equal(r2, r) and np.array_equal(t2, t))

    def test_flag_zero_inherits(self):
        sk = chain([0, 0, 0, 0])
        s = np.ones((4, 3)); s[1] = 2.0
        s2, _, _ = sk.ssc_to_plain(s, np.zeros((4, 3)), sk.raw_t.copy())
        self.assertTrue(np.array_equal(s2, s))


if __name__ == '__main__':
    unittest.main()
