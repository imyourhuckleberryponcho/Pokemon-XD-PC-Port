"""Gen 5 / 6 / 7 data from the player's own Ultra Sun / Ultra Moon dump, as plain Python values (docs/GEN57.md).

Reads only. Nothing read here is stored in the repository; the pack builder (gen57_pack.py) writes it under the player's data folder.

  rom = Usum(path_to_decrypted_cci)
  rom.species_name(506, 'en')  -> 'Lillipup'
  rom.personal(506)            -> Personal (stats, types, abilities, ...)
  rom.levelup(506)             -> [(level, move), ...]
  rom.evolutions(506)          -> [Evo(method, arg, species, form, level), ...]
  rom.move(85)                 -> Move (type, category, power, ...)

Text languages: ja (kana), jak (kanji), en, fr, it, de, es, ko (Ultra Sun / Moon have all of them in a/0/3/N, N = 0..7).
The archive numbers come from gamedetect (found by content); the text entries by the constants below, checked by test_gen57_data.py
against names that must be there (Pikachu, Intimidate, ...).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import garc
import gamedetect

LANG_INDEX = {'ja': 0, 'jak': 1, 'en': 2, 'fr': 3, 'it': 4, 'de': 5, 'es': 6, 'ko': 7}

T_SPECIES, T_ABILITY, T_ABILITY_DESC, T_ITEM, T_ITEM_DESC, T_MOVE, T_MOVE_DESC, T_CLASS = 60, 101, 102, 40, 39, 118, 117, 121


TYPE_XD = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 10, 10: 11, 11: 12, 12: 13, 13: 14, 14: 15, 15: 16, 16: 17, 17: None}


def gen_of(n):
    """The generation of a national number."""
    return 1 if n <= 151 else 2 if n <= 251 else 3 if n <= 386 else 4 if n <= 493 else 5 if n <= 649 else 6 if n <= 721 else 7


def decode_text(d):
    """One text entry (a Gen 6/7 message file): the list of its lines (the per-line XOR key chain of the format)."""
    nsec, nlines, total, key0, off = struct.unpack_from('<HHIII', d, 0)
    out = []
    for i in range(nlines):
        o, ln = struct.unpack_from('<iH', d, off + 4 + i * 8)
        key = (0x7C89 + 0x2983 * i) & 0xffff
        seg = d[off + o: off + o + ln * 2]
        ch = []
        for k in range(ln):
            c = struct.unpack_from('<H', seg, k * 2)[0] ^ key
            key = ((key << 3) | (key >> 13)) & 0xffff
            ch.append(c)
        out.append(_chars(ch))
    return out


def _chars(ch):
    """UTF-16 units of a line -> text. 0x10 starts a variable (3 more units, the last one the argument count)."""
    s = []
    i = 0
    while i < len(ch):
        c = ch[i]
        if c == 0:
            break
        if c == 0x10 and i + 3 < len(ch):
            s.append('{var}')
            i += 4 + ch[i + 3]
            continue
        s.append(chr(c))
        i += 1
    return ''.join(s)


class Personal:
    """The 84-byte record (Gen 7). Names are the fields' plain meanings; all numbers are as the game holds them."""

    def __init__(self, b):
        self.raw = b
        self.hp, self.atk, self.defn, self.spe, self.spa, self.spd = b[0:6]
        self.type1, self.type2 = b[6], b[7]
        self.catch_rate = b[8]
        ev = struct.unpack_from('<H', b, 0x0A)[0]
        self.ev = {'hp': ev & 3, 'atk': (ev >> 2) & 3, 'def': (ev >> 4) & 3, 'spe': (ev >> 6) & 3, 'spa': (ev >> 8) & 3, 'spd': (ev >> 10) & 3}
        self.items = struct.unpack_from('<3H', b, 0x0C)
        self.gender = b[0x12]
        self.hatch = b[0x13]
        self.friendship = b[0x14]
        self.growth = b[0x15]
        self.egg_groups = (b[0x16], b[0x17])
        self.abilities = (b[0x18], b[0x19], b[0x1A])
        self.form_stats_index = struct.unpack_from('<H', b, 0x1C)[0]
        self.form_count = b[0x20]
        self.base_exp = struct.unpack_from('<H', b, 0x22)[0]
        self.height_cm = struct.unpack_from('<H', b, 0x24)[0]
        self.weight_hg = struct.unpack_from('<H', b, 0x26)[0]
        self.tm_flags = b[0x28:0x28 + 16]

    def bst(self):
        return self.hp + self.atk + self.defn + self.spe + self.spa + self.spd


class Move:
    """The 40-byte move record (Gen 7). signature() is everything that decides the move's EFFECT (not its numbers)."""

    def __init__(self, i, b):
        self.id = i
        self.raw = b
        self.type, self.quality, self.category, self.power, self.accuracy, self.pp = b[0:6]
        self.priority = b[6] - 256 if b[6] > 127 else b[6]
        self.hits = b[7]
        self.inflict = struct.unpack_from('<H', b, 8)[0]
        self.inflict_percent = b[0x0A]
        self.turn_min, self.turn_max = b[0x0C], b[0x0D]
        self.crit = b[0x0E]
        self.flinch = b[0x0F]
        self.effect_seq = struct.unpack_from('<H', b, 0x10)[0]
        self.recoil = b[0x12] - 256 if b[0x12] > 127 else b[0x12]
        self.heal = b[0x13] - 256 if b[0x13] > 127 else b[0x13]
        self.target = b[0x14]
        self.stats = [(b[0x15 + k], b[0x18 + k] - 256 if b[0x18 + k] > 127 else b[0x18 + k], b[0x1B + k]) for k in range(3)]
        self.flags = struct.unpack_from('<H', b, 0x24)[0] if len(b) >= 0x26 else 0

    def signature(self):
        return (self.quality, self.category == 0, self.hits, self.inflict, self.turn_min, self.turn_max, self.crit > 0, self.flinch > 0,
                self.effect_seq, self.recoil, self.heal, self.target, tuple(self.stats), self.priority)

    def contact(self):
        return bool(self.flags & 1)


class Evo:
    def __init__(self, b):
        self.method, self.arg, self.species = struct.unpack_from('<HHH', b, 0)
        self.form, self.level = b[6], b[7]


class Usum:
    def __init__(self, rom_path):
        import ncch
        self.r, prod = ncch.open_rom(rom_path)
        self.det = gamedetect.detect(self.r, prod)
        self.prod = prod
        self._g = {}
        self._text = {}
        self._moves = None

    def _archive(self, path):
        if path not in self._g:
            self._g[path] = gamedetect._garc(self.r, path)
        return self._g[path]

    def _kind(self, name):
        return self._archive(self.det[name])

    def lines(self, lang, entry):
        k = (lang, entry)
        if k not in self._text:
            g = self._archive('a/0/3/%d' % LANG_INDEX[lang])
            self._text[k] = decode_text(garc.maybe(g.read(entry)))
        return self._text[k]


    def species_count(self):
        return len(self.lines('en', T_SPECIES)) - 1

    def species_name(self, n, lang='en'):
        return self.lines(lang, T_SPECIES)[n]

    def species_class(self, n, lang='en'):
        return self.lines(lang, T_CLASS)[n]

    def personal(self, record):
        return Personal(garc.maybe(self._kind('personal').read(record)))

    def levelup(self, record):
        d = garc.maybe(self._kind('levelup').read(record))
        v = struct.unpack_from('<%dh' % (len(d) // 2), d)
        out = []
        for mv, lv in zip(v[0::2], v[1::2]):
            if mv == -1:
                break
            out.append((lv, mv))
        return out

    def evolutions(self, record):
        d = garc.maybe(self._archive('a/0/1/4').read(record))
        out = []
        for k in range(len(d) // 8):
            e = Evo(d[k * 8:k * 8 + 8])
            if e.method:
                out.append(e)
        return out


    def moves(self):
        if self._moves is None:
            d = garc.maybe(self._archive('a/0/1/1').read(0))
            n = struct.unpack_from('<H', d, 2)[0]
            offs = struct.unpack_from('<%dI' % (n + 1), d, 4)
            self._moves = [Move(i, d[offs[i]:offs[i + 1]]) for i in range(n)]
        return self._moves

    def move(self, i):
        return self.moves()[i]

    def move_name(self, i, lang='en'):
        return self.lines(lang, T_MOVE)[i]

    def move_desc(self, i, lang='en'):
        return self.lines(lang, T_MOVE_DESC)[i]


    def ability_name(self, i, lang='en'):
        return self.lines(lang, T_ABILITY)[i]

    def ability_desc(self, i, lang='en'):
        return self.lines(lang, T_ABILITY_DESC)[i]

    def ability_count(self):
        return len(self.lines('en', T_ABILITY)) - 1

    def item_name(self, i, lang='en'):
        return self.lines(lang, T_ITEM)[i]

    def item_desc(self, i, lang='en'):
        return self.lines(lang, T_ITEM_DESC)[i]

    def item_count(self):
        return len(self.lines('en', T_ITEM))
