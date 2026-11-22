"""usum_match: the per-species colour match towards XD's own colours (made-up pictures only).  python -I tools/n3ds/test_usum_match.py"""
import os, sys, unittest
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usum_match as UM, gf_to_xd


def pool(parts):
    """parts: list of (rgb, share)"""
    p = UM.Pool()
    for rgb, share in parts:
        p.add(np.tile(np.array(rgb, float), (200, 1)), share)
    return p


def img(rgb, n=8):
    a = np.zeros((n, n, 4), np.uint8)
    a[..., :3] = rgb
    a[..., 3] = 255
    return a


class Match(unittest.TestCase):
    def test_pale_green_becomes_darker_richer(self):
        us = pool([((140, 200, 160), 0.7), ((235, 235, 235), 0.3)])
        xd = pool([((40, 140, 70), 0.7), ((230, 230, 225), 0.3)])
        rep = {}
        plan = UM.make_plan(us, xd, None, rep)
        self.assertIsNotNone(plan)
        self.assertLess(rep['de_after'], rep['de_before'] * 0.8)
        out = plan.apply(img((140, 200, 160)))[0, 0, :3].astype(int)
        self.assertLess(out[1], 198)
        self.assertGreater(out[1] - out[0], 50)

    def test_white_stays_white(self):
        us = pool([((140, 200, 160), 0.7), ((235, 235, 235), 0.3)])
        xd = pool([((40, 140, 70), 0.7), ((230, 230, 225), 0.3)])
        out = UM.make_plan(us, xd).apply(img((235, 235, 235)))[0, 0, :3].astype(int)
        self.assertGreater(out.min(), 200)

    def test_close_species_untouched(self):
        a = pool([((200, 120, 60), 1.0)])
        b = pool([((205, 118, 62), 1.0)])
        self.assertIsNone(UM.make_plan(a, b))

    def test_change_is_capped(self):
        us = pool([((120, 120, 200), 1.0)])
        xd = pool([((220, 40, 40), 1.0)])
        plan = UM.make_plan(us, xd, dict(max_match=200.0))
        if plan is not None:
            out = plan.apply(img((120, 120, 200)))[0, 0, :3].astype(int)
            self.assertGreater(out[2], out[0])
        lab0 = UM.rgb_to_lab(np.array([120, 120, 200.0]))
        if plan is not None:
            lab1 = UM.rgb_to_lab(plan.apply(img((120, 120, 200)))[0, 0, :3].astype(float))
            self.assertLessEqual(abs(lab1[0] - lab0[0]), 18.5)

    def test_far_group_not_matched(self):
        us = pool([((120, 120, 200), 1.0)])
        xd = pool([((220, 40, 40), 1.0)])
        self.assertIsNone(UM.make_plan(us, xd))

    def test_no_pictures_no_plan(self):
        self.assertIsNone(UM.make_plan(UM.Pool(), pool([((1, 2, 3), 1.0)])))

    def test_alpha_kept(self):
        us = pool([((140, 200, 160), 1.0)])
        xd = pool([((40, 140, 70), 1.0)])
        im = img((140, 200, 160))
        im[0, 0, 3] = 77
        self.assertEqual(UM.make_plan(us, xd).apply(im)[0, 0, 3], 77)

    def test_eye_plate_only_skin_moves(self):
        us = pool([((140, 200, 160), 1.0)])
        xd = pool([((40, 140, 70), 1.0)])
        plan = UM.make_plan(us, xd)
        a = np.zeros((2, 2, 4), np.uint8)
        a[..., 3] = 255
        a[0, 0, :3] = (140, 200, 160)
        a[0, 1, :3] = (10, 10, 10)
        a[1, 0, :3] = (245, 245, 245)
        out = plan.apply(a, skin_only=True)
        self.assertNotEqual(tuple(out[0, 0, :3]), (140, 200, 160))
        self.assertEqual(tuple(out[0, 1, :3]), (10, 10, 10))
        self.assertEqual(tuple(out[1, 0, :3]), (245, 245, 245))

    def test_lab_round_trip(self):
        rgb = np.array([[10, 200, 90], [250, 250, 250], [0, 0, 0], [120, 60, 200]], float)
        self.assertLess(np.abs(UM.lab_to_rgb(UM.rgb_to_lab(rgb)) - rgb).max(), 1.0)

    def test_pieces_not_changed(self):
        base = dict(name='BodyA', shader='PDC_Body', coords=[], )
        self.assertTrue(gf_to_xd.match_piece_ok(dict(base, name='BodyAInc00'), []))
        for nm in ('Iris', 'MouthA', 'LineA'):
            self.assertFalse(gf_to_xd.match_piece_ok(dict(base, name=nm), []))
        self.assertFalse(gf_to_xd.match_piece_ok(base, [object()]))
        self.assertFalse(gf_to_xd.match_piece_ok(dict(base, shader='PDC_Neon_Body'), []))


if __name__ == '__main__':
    unittest.main()
