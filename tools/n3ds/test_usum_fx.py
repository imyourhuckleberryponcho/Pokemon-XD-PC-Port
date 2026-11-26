"""Unit tests of the fire / glow stage (no game data needed):  python -I tools/n3ds/test_usum_fx.py"""
import os
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(REPO, 'tools', 'pbr_convert')):
    sys.path.insert(0, p)

import usum_fx
from model import Model, Joint, Animation, AnimObj, Track


class FakeSk:
    def __init__(self, names):
        self.names = names


def fake_model(names, tail_pos):
    m = Model()
    m.joints = [Joint(), Joint()]
    for k, n in enumerate(names):
        j = Joint()
        j.name = n
        j.parent = 1
        j.pos = tail_pos[k]
        m.joints.append(j)
    return m


class Tests(unittest.TestCase):
    def test_fire_material(self):
        self.assertTrue(usum_fx.is_fire_material({'shader': 'FireSten_lizardonGRE', 'name': 'FireStenA'}))
        self.assertTrue(usum_fx.is_fire_material({'shader': 'X', 'name': 'FireCoreB1'}))
        self.assertFalse(usum_fx.is_fire_material({'shader': 'PDC_Body_ShadowALW', 'name': 'BodyA'}))

    def test_species(self):
        self.assertEqual(usum_fx.species_of({'bones': [{'name': 'pm0006_00'}]}), 6)
        self.assertEqual(usum_fx.species_of({'bones': [{'name': 'Origin'}]}), 0)

    def test_fire_alpha_dark_is_clear_bright_is_solid(self):
        import gf_to_xd
        grey = np.zeros((4, 4, 4), np.uint8)
        grey[..., :3] = 0
        grey[0, 0, :3] = 255
        grey[..., 3] = 255
        out = usum_fx.fire_alpha(gf_to_xd.fire_tint(grey))
        self.assertLess(int(out[1, 1, 3]), 40)
        self.assertGreater(int(out[0, 0, 3]), 200)

    def test_xd_has_particles(self):
        m = Model()
        an = Animation()
        ao = AnimObj()
        ao.tracks.append(Track(5, []))
        an.joints[3] = ao
        m.animations.append(an)
        self.assertFalse(usum_fx.xd_has_particles(m))
        ao.tracks.append(Track(40, [(6, 1e-41, 0.0, 0)]))
        self.assertTrue(usum_fx.xd_has_particles(m))

    def test_particle_tracks_copied_to_named_bone(self):

        xd = Model()
        xd.joints = [Joint() for _ in range(5)]
        xan = Animation()
        xo = AnimObj()
        xo.end = 30.0
        xo.tracks.append(Track(40, [(6, 1e-41, 0.0, 0)]))
        xan.joints[54] = xo
        xd.animations.append(xan)
        xwm = [[0.0] * 12 for _ in range(60)]
        names = ['Hips', 'TailA01', 'LFoot']
        m = fake_model(names, [(0, 5, 0), (0, 1, -9), (3, 0, 0)])
        m.animations.append(Animation())
        m.animations[0].name = 'idle'
        jn = {0: 2, 1: 3, 2: 4}
        gm = {'bones': [{'name': 'pm0006_00'}]}
        n = usum_fx.add_particle_tracks(m, xd, xwm, jn, FakeSk(names), gm, lambda s: None)
        self.assertEqual(n, 1)
        self.assertIn(3, m.animations[0].joints)
        self.assertEqual([t.kind for t in m.animations[0].joints[3].tracks], [40])

        self.assertIsNot(m.animations[0].joints[3].tracks[0], xo.tracks[0])

    def test_none_slots_get_no_particles(self):
        xd = Model()
        xd.joints = [Joint() for _ in range(3)]
        xan = Animation()
        xo = AnimObj()
        xo.tracks.append(Track(40, [(6, 1e-41, 0.0, 0)]))
        xan.joints[1] = xo
        xd.animations.append(xan)
        m = fake_model(['A'], [(0, 0, 0)])
        an = Animation()
        an.name = 'none'
        m.animations.append(an)
        xwm = [[0.0] * 12 for _ in range(3)]
        n = usum_fx.add_particle_tracks(m, xd, xwm, {0: 2}, FakeSk(['A']), {'bones': [{'name': 'pm0999_00'}]}, lambda s: None)
        self.assertEqual(n, 0)

    def test_fire_scroll_samples_the_motion(self):
        import gf_read

        ch_ty = [(0, 0.0, 0.05), (40, 2.0, 0.05), (200, 2.0, 0.05)]
        v = [gf_read.herp(ch_ty, f) for f in (0, 20, 40)]
        self.assertAlmostEqual(v[0], 0.0, 3)
        self.assertAlmostEqual(v[2], 2.0, 3)
        self.assertTrue(0.5 < v[1] < 1.5)

    def test_fire_scroll_writes_material_animation(self):
        from model import Mesh
        m = Model()
        m.joints = [Joint(), Joint(), Joint()]
        me = Mesh()
        me.joint = 2
        me.material = 0
        m.meshes = [me]
        an = Animation()
        an.name = 'idle'
        ao = AnimObj()
        ao.end = 30.0
        an.joints[1] = ao
        m.animations = [an]
        gm = {'materials': [{'name': 'FireCoreA', 'shader': 'FireCore_FireWingGRE', 'coords': [{'unit': 0, 'scale': (1.0, 1.0)}]}]}
        fx = {'frames': 200, 'mats': [{'name': 'FireCoreA', 'unit': 0, 'tx': [], 'sx': [], 'sy': [], 'rot': [],
                                       'ty': [(0, 0.0, 0.05), (20, 1.0, 0.05), (200, 1.0, 0.05)]}]}
        n = usum_fx.add_fire_scroll(m, gm, fx, lambda s: None)
        self.assertEqual(n, 1)
        ma = an.materials[2][0]
        kinds = sorted(t.kind for t in ma.texanims[0].aobj.tracks)
        self.assertEqual(kinds, [usum_fx.T_TRAU, usum_fx.T_TRAV])
        self.assertTrue(an.has_material_tree)

    def test_fire_scroll_fallback_without_motion(self):
        from model import Mesh
        m = Model()
        m.joints = [Joint(), Joint(), Joint()]
        me = Mesh()
        me.joint = 2
        me.material = 0
        m.meshes = [me]
        an = Animation()
        an.name = 'idle'
        ao = AnimObj()
        ao.end = 20.0
        an.joints[1] = ao
        m.animations = [an]
        gm = {'materials': [{'name': 'FireStenA', 'shader': 'FireSten_x', 'coords': [{'unit': 0, 'scale': (1.0, 1.0)}]}]}
        self.assertEqual(usum_fx.add_fire_scroll(m, gm, None, lambda s: None), 1)

    def test_emission_map_found_and_baked(self):
        mt = {'coords': [{'unit': 0, 'name': 'a1.tga'}, {'unit': 1, 'name': 'a_BodyInc.tga'}, {'unit': 2, 'name': 'nor.tga'}]}
        texs = {'a1.tga': (2, 2, np.zeros((2, 2, 4), np.uint8)), 'a_BodyInc.tga': (1, 1, np.full((1, 1, 4), 255, np.uint8))}
        self.assertEqual(usum_fx.emission_map(mt, texs), 'a_BodyInc.tga')
        mt2 = {'coords': [{'unit': 0, 'name': 'a1.tga'}, {'unit': 1, 'name': 'DummyTex.tga'}]}
        self.assertIsNone(usum_fx.emission_map(mt2, texs))
        base = np.full((2, 2, 4), 100, np.uint8)
        out = usum_fx.bake_emission(base, texs['a_BodyInc.tga'])
        self.assertEqual(out.shape, base.shape)
        self.assertGreater(int(out[0, 0, 0]), 150)
        sparse = np.zeros((4, 4, 4), np.uint8)
        sparse[0, 0] = 255
        self.assertEqual(int(usum_fx.bake_emission(np.full((4, 4, 4), 100, np.uint8), (4, 4, sparse))[0, 0, 0]), 255)
        dark = (1, 1, np.zeros((1, 1, 4), np.uint8))
        self.assertEqual(int(usum_fx.bake_emission(base, dark)[0, 0, 0]), 100)

    def test_emission_mask_in_normal_map_alpha(self):

        mt = {'coords': [{'unit': 0, 'name': 'b1.tga'}, {'unit': 1, 'name': 'b_Inc.tga'}, {'unit': 2, 'name': 'b_Nor.tga'}]}
        nor = np.full((4, 4, 4), 128, np.uint8)
        nor[..., 3] = 0
        nor[0, 0, 3] = 255
        texs = {'b1.tga': (4, 4, np.zeros((4, 4, 4), np.uint8)), 'b_Inc.tga': (4, 4, np.full((4, 4, 4), 255, np.uint8)), 'b_Nor.tga': (4, 4, nor)}
        self.assertEqual(usum_fx.emission_mask(mt, texs), 'b_Nor.tga')
        base = np.full((4, 4, 4), 100, np.uint8)
        out = usum_fx.bake_emission(base, texs['b_Inc.tga'], mask=texs['b_Nor.tga'])
        self.assertEqual(int(out[0, 0, 0]), 255)
        self.assertEqual(int(out[3, 3, 0]), 100)

        texs['b_Nor.tga'] = (4, 4, np.full((4, 4, 4), 255, np.uint8))
        self.assertIsNone(usum_fx.emission_mask(mt, texs))
        self.assertIsNone(usum_fx.emission_mask({'coords': [{'unit': 1, 'name': 'b_Inc.tga'}]}, texs))

    def test_register_to_colour(self):

        mask = np.zeros((2, 8, 4), np.uint8)
        mask[:, :4, 3] = 255
        c0 = {'scale': (2.0, 1.0), 'trans': (0.0, 0.0), 'wrap': (3, 2)}
        ck = {'scale': (1.0, 1.0), 'trans': (0.0, 0.0), 'wrap': (2, 2)}
        out = usum_fx.register_to_colour(mask, ck, c0, 4, 2)
        self.assertEqual(out.shape, (2, 4, 4))
        self.assertTrue((out[..., 3] == 255).all())
        same = usum_fx.register_to_colour(mask, c0, c0, 4, 2)
        self.assertIs(same, mask)
        ck2 = {'scale': (1.0, 1.0), 'trans': (0.5, 0.0), 'wrap': (2, 2)}
        self.assertTrue((usum_fx.register_to_colour(mask, ck2, c0, 4, 2)[..., 3] == 0).all())

    def test_no_fire_no_change(self):
        m = Model()
        gm = {'materials': [{'name': 'BodyA', 'shader': 'PDC_Body_ShadowALW', 'coords': []}]}
        self.assertEqual(usum_fx.add_fire_scroll(m, gm, None, lambda s: None), 0)

    def test_emission_copy_of_colour_adds_nothing(self):

        col = np.full((8, 8, 4), 200, np.uint8)
        col[..., 3] = 255
        out = usum_fx.bake_emission(col, (8, 8, col.copy()), raw=col)
        self.assertEqual(int(out[0, 0, 0]), 200)
        glow = col.copy()
        glow[..., :3] = 255
        self.assertGreater(int(usum_fx.bake_emission(np.full((8, 8, 4), 20, np.uint8), (8, 8, glow), raw=np.full((8, 8, 4), 20, np.uint8))[0, 0, 0]), 60)

    def test_effect_layer_and_steel_and_ghost(self):
        import gf_to_xd as G
        mk = lambda n0, sh: {'name': 'X', 'shader': sh, 'coords': [{'unit': 0, 'name': n0}]}
        self.assertTrue(G.is_effect_layer(mk('pm0088_00_BetoMask.tga', 'PDC_pm_betbeton_BetoGRE')))
        self.assertTrue(G.is_effect_layer(mk('pm0183_00_Pokabu_Tail00Env.tga', 'PDC_pm_pokabu_TailGRE')))
        self.assertFalse(G.is_effect_layer(mk('pm0025_00_BodyA1.tga', 'PDC_Body_ShadowALW')))
        grey = np.full((2, 2, 4), 128, np.uint8)
        t = G.steel_tint(grey, 1.0)
        self.assertGreater(int(t[0, 0, 2]), int(t[0, 0, 0]))
        red = np.zeros((2, 2, 4), np.uint8)
        red[..., 0] = 200
        self.assertEqual(G.steel_tint(red, 1.0)[0, 0, 0], 200)

    def test_patch_spot_uses_mask_unit(self):
        import gf_to_xd as G
        mt = {'name': 'APatch', 'shader': 'PDC_pm_patcheel_ABCDPatchGRE',
              'coords': [{'unit': 0, 'name': 'Patch.tga'}, {'unit': 1, 'name': 'PachiAA.tga', 'scale': (8.0, 8.0)}]}
        self.assertEqual(G.colour_coord(mt)['name'], 'PachiAA.tga')
        self.assertFalse(G.is_effect_layer(mt))
        plain = {'name': 'Body', 'shader': 'PDC_Body_ShadowALW', 'coords': [{'unit': 0, 'name': 'B.tga'}, {'unit': 1, 'name': 'D.tga'}]}
        self.assertEqual(G.colour_coord(plain)['name'], 'B.tga')
        mask = np.zeros((8, 8, 4), np.uint8)
        mask[2:6, 2:6, :3] = 255
        mask[..., 3] = 255
        gm = {'materials': [mt]}
        texs = {'PachiAA.tga': (8, 8, mask), 'Patch.tga': (8, 8, np.full((8, 8, 4), (214, 110, 108, 255), np.uint8))}
        imgs, per, cls = G.make_images(gm, texs, lambda *a: None, opts={})
        self.assertIn(cls[per[0][0]], ('xlu', 'texedge'))

    def test_lure_and_cheek(self):
        import gf_to_xd as G
        self.assertTrue(G.is_lure_glow({'shader': 'PDC_pm_chonchie_GlowGRE'}))
        self.assertFalse(G.is_lure_glow({'shader': 'PDC_Body_ShadowALW'}))
        orange = np.full((4, 4, 4), (200, 90, 20, 255), np.uint8)
        f = G.lure_fill(orange)
        self.assertTrue((f[..., 0] > 240).all() and (f[..., 1] > 230).all())

        plate = np.full((16, 16, 4), (244, 228, 130, 255), np.uint8)
        plate[4:8, 4:8, :3] = (236, 176, 173)
        out = G.cheek_boost(plate)
        self.assertLess(int(out[5, 5, 1]), 170)
        self.assertEqual(tuple(int(x) for x in out[12, 12, :3]), (244, 228, 130))
        red = plate.copy()
        red[4:8, 4:8, :3] = (235, 40, 30)
        self.assertLessEqual(abs(int(G.cheek_boost(red)[5, 5, 0]) - 235), 3)


if __name__ == '__main__':
    unittest.main()
