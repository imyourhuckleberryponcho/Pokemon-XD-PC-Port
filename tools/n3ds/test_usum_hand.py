"""usum_hand: the hand-set colour table (made-up pictures only).  python -I tools/n3ds/test_usum_hand.py"""
import os, sys, unittest
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usum_hand as H, usum_match as UM


def img(rgb, n=6):
    a = np.zeros((n, n, 4), np.uint8)
    a[..., :3] = rgb
    a[..., 3] = 200
    return a


def lab(px):
    return UM.rgb_to_lab(np.array(px, float))


class Hand(unittest.TestCase):
    def test_table_is_complete(self):
        for nat, row in H.SPECIES.items():
            self.assertIn('name', row)
            self.assertIn('note', row)
        for nat, key, who, what in H.RULES:
            self.assertIn(nat, H.SPECIES)
            self.assertTrue(set(who) <= {'hue', 'L', 'C'})
            self.assertTrue(set(what) <= {'dL', 'gain', 'dh'})

    def test_match_switch(self):
        self.assertFalse(H.match_enabled(216))
        self.assertTrue(H.match_enabled(25))
        self.assertTrue(H.match_enabled(1))

    def test_rules_select_by_material(self):
        self.assertEqual(len(H.rules_for(25, 'BodyB')), 1)
        self.assertEqual(len(H.rules_for(25, 'Mouth')), 0)
        self.assertEqual(len(H.rules_for(216, 'Iris')), 0)
        self.assertEqual(len(H.rules_for(216, 'Body')), 1)
        self.assertEqual(len(H.rules_for(1, 'Body')), 0)
        self.assertTrue(H.plates_follow_match(25, 'Mouth'))
        self.assertTrue(H.plates_follow_match(25, 'BodyB'))
        self.assertFalse(H.plates_follow_match(25, 'BodyA'))

    def test_rule_moves_only_its_colour_group(self):
        rules = [(dict(hue=(250, 290), C=(8, 70)), dict(dL=-10, gain=1.5, dh=0))]
        blue, red = (110, 160, 230), (230, 90, 70)
        out_b = H.tune(img(blue), rules)[0, 0, :3]
        out_r = H.tune(img(red), rules)[0, 0, :3]
        self.assertTrue((out_r == np.array(red)).all() or abs(lab(out_r)[0] - lab(red)[0]) < 0.6)
        lb0, lb1 = lab(blue), lab(out_b)
        self.assertLess(lb1[0], lb0[0] - 5)
        self.assertGreater(np.hypot(lb1[1], lb1[2]), np.hypot(lb0[1], lb0[2]) * 1.25)

    def test_alpha_kept_and_empty_rules(self):
        a = img((200, 100, 90))
        self.assertTrue((H.tune(a, [])[..., 3] == 200).all())
        self.assertTrue((H.tune(a, [(dict(), dict(dL=-5))])[..., 3] == 200).all())

    def test_hue_wrap_range(self):
        rules = [(dict(hue=(-20, 35), C=(15, 70)), dict(dL=0, gain=1.5, dh=0))]
        pink = (230, 100, 140)
        l0, l1 = lab(pink), lab(H.tune(img(pink), rules)[0, 0, :3])
        self.assertGreater(np.hypot(l1[1], l1[2]), np.hypot(l0[1], l0[2]) * 1.2)

    def test_default_hue_dark_grey_goes_darker(self):
        r = H.rules_for(25, 'BodyA')
        g = (75, 75, 75)
        self.assertLess(lab(H.tune(img(g), r)[0, 0, :3])[0], lab(g)[0] - 10)
        y = (255, 215, 40)
        self.assertLess(abs(lab(H.tune(img(y), r)[0, 0, :3])[0] - lab(y)[0]), 1.0)


if __name__ == '__main__':
    unittest.main()
