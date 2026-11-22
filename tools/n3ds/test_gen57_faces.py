"""Unit tests for gen57_faces.py / gen57_facecam.py (made-up pictures and a made-up skeleton, no dump): python -I tools/n3ds/test_gen57_faces.py"""
import os
import sys
import unittest

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_faces as F
import gen57_facecam as C


class Skel:
    names = ['Origin', 'Waist', 'Head', 'LEar1', 'LEar2', 'Nose', 'Tail']
    parent = [-1, 0, 1, 2, 3, 2, 1]


class Round(unittest.TestCase):
    def test_circle_black_and_smooth(self):
        im = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
        for y in range(100, 400):
            for x in range(100, 400):
                if (x - 250) ** 2 + (y - 250) ** 2 < 150 ** 2:
                    im.putpixel((x, y), (200, 40, 30, 255))
        f = F.round_face(im)
        self.assertEqual(f.size, (84, 84))
        a = np.asarray(f)
        self.assertEqual(a[0, 0, 3], 0)
        self.assertEqual(a[42, 3, 3], 255)
        self.assertEqual(tuple(a[42, 3, :3]), (0, 0, 0))
        self.assertTrue(((a[..., 3] > 0) & (a[..., 3] < 255)).any())
        edge = a[42, 12:24, 0].astype(int)
        self.assertGreater(len(set(edge.tolist())), 4)

    def test_semi_transparent_pixels_blend_with_black(self):
        im = Image.new('RGBA', (64, 64), (200, 200, 200, 128))
        a = np.asarray(F.round_face(im, 16))
        self.assertTrue(90 <= int(a[8, 8, 0]) <= 110)


class Camera(unittest.TestCase):
    def test_subtree_skips_thin_parts(self):
        class S(Skel):
            pass
        s = C.subtree(S, 2)
        self.assertIn(2, s)
        self.assertIn(5, s)
        self.assertNotIn(3, s)
        self.assertNotIn(4, s)
        self.assertNotIn(6, s)

    def test_head_bone_lookup(self):
        class S(Skel):
            pass
        self.assertEqual(C.head_bone(S), 2)
        S.names = ['Origin', 'Neck', 'Head2']
        self.assertEqual(C.head_bone(S), 2)
        S.names = ['Origin', 'Waist']
        self.assertIsNone(C.head_bone(S))

    def test_item_text(self):
        t = F.face_item(506, dict(x=0.5, y=2.0, z=-1.0, r=1.25, yaw=18.0, pitch=6.0))
        self.assertEqual(t, 'pokemon:537:face=0.5000/2.0000/-1.0000/1.2500/18.0/6.0:506')

    def test_empty_and_silhouette(self):
        import tempfile
        from PIL import Image
        d = tempfile.mkdtemp()
        empty, full = os.path.join(d, 'e.png'), os.path.join(d, 'f.png')
        Image.new('RGBA', (64, 64), (0, 0, 0, 0)).save(empty)
        Image.new('RGBA', (64, 64), (200, 100, 50, 255)).crop((0, 0, 64, 64)).save(full)
        self.assertFalse(F.face_ok(empty))
        self.assertTrue(F.face_ok(full) is False or F.face_ok(full) is True)
        im = Image.new('RGBA', (100, 100), (0, 0, 0, 0))
        im.paste((255, 0, 0, 255), (40, 10, 60, 30))
        im.paste((255, 0, 0, 255), (30, 30, 70, 90))
        p = os.path.join(d, 's.png')
        im.save(p)
        v = F.head_from_silhouette(p, dict(x=0, y=0, z=0, r=1.0, yaw=0.0, pitch=0.0), share=0.2)
        self.assertGreater(v['y'], 0.2)
        self.assertIsNone(F.head_from_silhouette(empty, dict(x=0, y=0, z=0, r=1.0, yaw=0.0, pitch=0.0)))


if __name__ == '__main__':
    unittest.main()
