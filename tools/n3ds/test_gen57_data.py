"""Unit tests for gen57_data / gen57_pack on made-up bytes (no dump needed): python -I tools/n3ds/test_gen57_data.py"""
import os
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_data as D
import gen57_pack as P
import gen57_moves as MV
import gen57_move_plan as MP
import gen57_abilities as AB


def personal_bytes():
    b = bytearray(84)
    b[0:6] = bytes([45, 60, 45, 55, 25, 45])
    b[6] = b[7] = 0
    b[8] = 255
    struct.pack_into('<H', b, 0x0A, 1 << 2)
    b[0x12] = 127
    b[0x14] = 70
    b[0x15] = 3
    b[0x18:0x1B] = bytes([72, 53, 50])
    struct.pack_into('<H', b, 0x22, 55)
    struct.pack_into('<HH', b, 0x24, 40, 41)
    return bytes(b)


class Fake:
    def __init__(self):
        self.p = D.Personal(personal_bytes())

    def personal(self, n): return self.p
    def levelup(self, n): return [(1, 33), (1, 10), (8, 44), (10, 608), (36, 363), (40, 416), (22, 488), (5, 33)]
    def evolutions(self, n): return [D.Evo(struct.pack('<HHHBB', 4, 0, 507, 255, 16))]
    def species_name(self, n, lang='en'): return {507: 'Herdier'}.get(n, 'Lillipup')


def move_bytes(typ=9, quality=7, cat=1, power=50, acc=100, pp=20, pri=0, hits=0, inflict=0, inflict_pct=0, flinch=0, recoil=0, heal=0, target=0,
               stats=((0, 0, 0), (0, 0, 0), (0, 0, 0)), flags=0x49):
    b = bytearray(40)
    b[0:7] = bytes([typ, quality, cat, power, acc, pp, pri & 255])
    b[7] = hits
    struct.pack_into('<H', b, 8, inflict)
    b[0x0A] = inflict_pct
    b[0x0F] = flinch
    b[0x12] = recoil & 255
    b[0x13] = heal & 255
    b[0x14] = target
    for k, (s, g, pc) in enumerate(stats):
        b[0x15 + k], b[0x18 + k], b[0x1B + k] = s, g & 255, pc
    struct.pack_into('<H', b, 0x24, flags)
    return D.Move(488, bytes(b))


class Tests(unittest.TestCase):
    def test_move_flags_and_blocks(self):
        m = move_bytes(stats=((5, 1, 100), (0, 0, 0), (0, 0, 0)))
        self.assertTrue(m.contact())
        self.assertEqual(MV.flags_of(m)['mamoru'], 1)
        blocks, _ = MV.derive_blocks(m, 'Flame Charge')
        self.assertEqual(blocks, [{'stat': 'spe', 'stages': 1, 'target': 'user'}])
        s = move_bytes(quality=4, power=80, inflict=4, inflict_pct=30)
        blocks, _ = MV.derive_blocks(s, 'Scald')
        self.assertEqual(blocks, [{'status': 'burn', 'target': 'target', 'chance': 30}])
        w = move_bytes(power=90, recoil=-25)
        self.assertEqual(MV.derive_blocks(w, 'Wild Charge')[0], [{'recoil': 25}])
        d = move_bytes(quality=8, power=50, recoil=75)
        self.assertEqual(MV.derive_blocks(d, 'Draining Kiss')[0], [{'drain': 75}])
        self.assertEqual(MV.ROW_SHIFT, 20)

    def test_plan_and_abilities(self):
        self.assertTrue(MP.is_z('Catastropika', 658))
        self.assertFalse(MP.is_z('Moonblast', 585))
        for name, plan in MP.PLAN.items():
            for b in plan.get('blocks', []):
                self.assertTrue(isinstance(b, dict), name)
        ids = [v for v in AB.TABLE]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(124 <= i <= 232 for i in ids))
        for i, (name, d, status, note) in AB.TABLE.items():
            self.assertIn(status, ('full', 'partial'), name)


    def test_personal(self):
        p = D.Personal(personal_bytes())
        self.assertEqual((p.hp, p.atk, p.spe), (45, 60, 55))
        self.assertEqual(p.ev['atk'], 1)
        self.assertEqual(p.abilities, (72, 53, 50))
        self.assertEqual((p.height_cm, p.weight_hg, p.base_exp), (40, 41, 55))

    def test_text_roundtrip(self):

        key = 0x7C89
        units = []
        for c in 'Hi':
            units.append(ord(c) ^ key)
            key = ((key << 3) | (key >> 13)) & 0xffff
        body = b''.join(struct.pack('<H', x) for x in units)
        head = struct.pack('<HHIII', 1, 1, 0, 0, 16)
        data = head + struct.pack('<I', 0) + struct.pack('<iHH', 12, 2, 0) + body
        self.assertEqual(D.decode_text(data), ['Hi'])

    def test_gen_of(self):
        self.assertEqual([D.gen_of(n) for n in (493, 494, 649, 650, 721, 722, 807)], [4, 5, 5, 6, 6, 7, 7])
        self.assertEqual(sum(1 for n in range(1, 808) if D.gen_of(n) >= 5), 314)

    def test_move_rows(self):
        self.assertEqual(P.move_row_id(85, {}), 85)
        self.assertEqual(P.move_row_id(394, {}), 414)
        self.assertEqual(P.move_row_id(488, {}), 508)
        self.assertIsNone(P.move_row_id(729, {}))

    def test_species_row(self):
        notes = []
        row, rep = P.species_row(Fake(), 506, 228, 525, None, [33, 44] + [0] * 56, [10] + [0] * 11, {506, 507}, {}, notes)
        self.assertEqual(row['basisStatus'][:6], [45, 60, 45, 25, 45, 55])
        self.assertEqual(row['height'], 4)
        self.assertEqual(row['sinka.kind'][0], 4)
        self.assertEqual(row['sinka.buff'][0], 16)
        self.assertEqual(row['sinka.pokemonDataId'][0], 'Herdier')
        pairs = list(zip(row['waza.level'], row['waza.dataId']))
        self.assertIn((1, 33), pairs)
        self.assertIn((22, 508), pairs)
        self.assertIn(628, row['waza.dataId'])
        self.assertEqual(row['wazaMcn'][0], 1)
        self.assertEqual(row['wazaMcn'][1], 1)
        self.assertEqual(row['tokuseiDataId'], [72, 53])

    def test_evolution_left_out(self):
        notes = []
        row, _ = P.species_row(Fake(), 506, 228, 525, None, [0] * 58, [0] * 12, {506}, {}, notes)
        self.assertEqual(row['sinka.kind'][0], 0)
        self.assertTrue(any('not in the pack' in n for n in notes))


if __name__ == '__main__':
    unittest.main()
