"""Which 3DS Pokemon game is this dump, and where do its model / data archives live?

Supported sources (all Gen 7 engine, GFModel / GFMotion, the same file formats):
  Ultra Sun  (product code CTR-P-A2A?)   tested
  Ultra Moon (product code CTR-P-A2B?)   see STUDY.md (tested when a dump was available)
  Sun / Moon (CTR-P-BND? / BNE?)         same engine, 802 species; expected to work, untested
The archives are found by what they hold, not by a fixed path, so a layout shift between the games does not matter:
  models   : the big GARC whose entries are 1 + 9 per Pokemon form (first group's first member is a "PC" pack)
  personal : the small GARC of 84-byte records where record 25 starts with Pikachu's stats 35/55/40/90/50/50
  levelup  : the GARC next to it (same entry count minus / plus a few) where record 25 learns Thunder Shock (84) at level 1
  text     : the language GARC under a/0/3/ whose entry 60 (species names) line 25 is "Pikachu"
Reads only; no game data is stored by this module (the product code and file paths are all it keeps).
"""
import struct

import garc

KNOWN = (('CTR-P-A2A', 'Pokemon Ultra Sun'), ('CTR-P-A2B', 'Pokemon Ultra Moon'),
         ('CTR-P-BND', 'Pokemon Sun'), ('CTR-P-BNE', 'Pokemon Moon'))
TESTED = ('Pokemon Ultra Sun',)
PIKACHU = bytes([35, 55, 40, 90, 50, 50])


def _garc(r, path):
    o, s = r.files[path]
    return garc.Garc(r.f, r.l3 + r.fdo + o, s)


def decode_text(d):
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
        out.append(''.join(chr(c) for c in ch if c))
    return out


def detect(r, prod):
    det = {'prod': prod, 'name': 'unknown 3DS game', 'status': 'not recognised by its product code (found by content)'}
    for code, name in KNOWN:
        if prod.startswith(code):
            det['name'] = name
            det['status'] = 'tested' if name in TESTED else 'expected to work (same engine), untested'
    cands = sorted(p for p in r.files if p.startswith('a/') and p.count('/') == 3)
    models = personal = levelup = text = None
    for p in cands:
        size = r.files[p][1]
        try:
            if size > 400_000_000 and models is None:
                g = _garc(r, p)
                if len(g.entries) > 5000 and (len(g.entries) - 1) % 9 == 0:
                    models = p
            elif size < 2_000_000 and personal is None and p.startswith('a/'):
                g = _garc(r, p)
                if 700 <= len(g.entries) <= 1300:
                    d = garc.maybe(g.read(25))
                    if len(d) == 84 and d[:6] == PIKACHU:
                        personal = p
        except Exception:
            continue
    if personal:
        n = len(_garc(r, personal).entries)
        for p in cands:
            if p == personal or r.files[p][1] > 8_000_000:
                continue
            try:
                g = _garc(r, p)
                if abs(len(g.entries) - n) <= 8:
                    d = garc.maybe(g.read(25))
                    v = struct.unpack_from('<%dH' % (len(d) // 2), d)
                    pairs = list(zip(v[0::2], v[1::2]))
                    if (84, 1) in pairs and len(d) < 400:
                        levelup = p
                        break
            except Exception:
                continue
    for p in cands:
        if not p.startswith('a/0/3/'):
            continue
        try:
            g = _garc(r, p)
            for e in (60, 61, 59, 62):
                if e < len(g.entries):
                    t = decode_text(garc.maybe(g.read(e)))
                    if len(t) > 800 and t[25] == 'Pikachu':
                        text, det['text_species'] = p, e
                        break
            if text:
                break
        except Exception:
            continue
    det.update(models=models, personal=personal, levelup=levelup, text=text)
    missing = [k for k in ('models', 'personal', 'levelup', 'text') if not det[k]]
    if missing:
        raise SystemExit('3DS dump: could not find %s (is it a decrypted Pokemon Sun / Moon / Ultra Sun / Ultra Moon image?)' % ', '.join(missing))
    return det


def archive(r, det, kind):
    return _garc(r, det[kind])


_CACHE = {}


def models_garc(r):
    """The model GARC of a dump (found once per ROM object)."""
    if id(r) not in _CACHE:
        _CACHE[id(r)] = detect(r, '')
    return _garc(r, _CACHE[id(r)]['models'])
