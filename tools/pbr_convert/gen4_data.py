"""Gen 4 (Diamond/Pearl) species data from the player's Pokemon Battle Revolution disc -> Pokemon XD species records.

  python tools/pbr_convert/gen4_data.py make <out folder> --pbr <PBR disc> --xd <XD disc> [--plan <plan.tsv>] [--exe <xd_data_mods.exe>]
  python tools/pbr_convert/gen4_data.py check <out folder> --pbr <PBR disc> --xd <XD disc>      (5 known species, read back)

Output (game data: keep the folder under build/, never in the repository):
  <out>/species/<dex>_<NAME>.json     one file per Gen 4 species (107): the exact 292-byte XD record ("record_hex",
                                      the GameCube layout games/xd/src/data/common_rel.cpp describes), the same fields
                                      by the data-mod layer's names ("fields"), and everything that has no field in
                                      XD's record ("extra"): egg groups, hatch cycles, the Gen 4 abilities, notes
  <out>/species_all.json              the same in one file, in the order of the new ids (the hand-off for "add entries")
  <out>/existing_patches.json         evolutions INTO Gen 4 species for existing XD species (optional patch)
  <out>/summary.json, summary.tsv     one line per species
  <out>/move_map.tsv ability_map.tsv item_map.tsv machine_map.tsv species_ids.tsv   the substitution tables

Layouts (PBR, all big-endian) are in docs/GEN4.md; the rules for what XD lacks are in gen4_rules.py. Clean room: our
own code, the layouts read from the game's data and the decomp (read only).
"""
import argparse, collections, json, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import noddisc
import gen4_rules as R

TYPE_NAMES = ['Normal', 'Fighting', 'Flying', 'Poison', 'Ground', 'Rock', 'Bug', 'Ghost', 'Steel', '???', 'Fire',
              'Water', 'Grass', 'Electric', 'Psychic', 'Ice', 'Dragon', 'Dark']
GROWTH_DS = ['Medium Fast', 'Erratic', 'Fluctuating', 'Medium Slow', 'Fast', 'Slow']
EGG_GROUPS = ['', 'Monster', 'Water 1', 'Bug', 'Flying', 'Field', 'Fairy', 'Grass', 'Human-Like', 'Water 3', 'Mineral',
              'Amorphous', 'Water 2', 'Ditto', 'Dragon', 'Undiscovered']
GEN4_FIRST, GEN4_LAST = 387, 493
BONSLY, MUNCHLAX = 438, 446
XD_BONSLY, XD_MUNCHLAX = 413, 414
FIRST_NEW_ID = 415
FIRST_NEW_PKX = 419
FIRST_NEW_NAME_MSG = 1391
FLAG_BASE = 2324
REC = 292
DEFAULT_EXE = ''


def norm(s):
    return re.sub(r'[^A-Z0-9]', '', s.upper().replace('\u00e9', 'E'))


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]



class PBR:
    def __init__(self, path):
        self.disc = noddisc.Disc(path)
        common = self.disc.file('common.fsys')
        self.common = {e.index: noddisc.extract(common, e) for e in noddisc.fsys_entries(common)
                       if e.index in (8, 9, 29, 30, 32)}
        mes = self.disc.file('mes_common.fsys')
        ent = [e for e in noddisc.fsys_entries(mes) if e.index == 1][0]
        self.text = self._mesg(noddisc.extract(mes, ent))
        self.tmlist = [be16(self.common[29], 0x30 + 4 * i) for i in range(100)]

    @staticmethod
    def _mesg(d):
        assert d[:4] == b'MESG'
        n = struct.unpack_from('>I', d, 4)[0]
        offs = struct.unpack_from('>%dI' % n, d, 0x10)
        out = []
        for i, o in enumerate(offs):
            e = offs[i + 1] if i + 1 < n else len(d)
            s = ''
            raw = d[o:e]
            for k in range(0, len(raw) - 1, 2):
                c = be16(raw, k)
                if c == 0:
                    break
                s += chr(c) if (c >= 0x20 and c < 0xD800) else '\uffff'
            out.append(s.replace('\uffff', ''))
        return out

    def species_name(self, dex):
        return self.text[9 + dex]

    def move_name(self, m):
        return self.text[505 + m]

    def item_name(self, i):
        return self.text[1990 + i]

    def ability_name(self, a):
        return self.text[2989 + a] if a else ''

    def personal(self, dex):
        r = self.common[8][0x30 + dex * 52:0x30 + dex * 52 + 52]
        w = struct.unpack('>4I', r[0:16])
        ev = be16(r, 40)
        evs = [(ev >> (14 - 2 * k)) & 3 for k in range(6)]
        return dict(
            hp=r[30], atk=r[31], de=r[32], spe=r[33], spa=r[34], spd=r[35], t1=r[36], t2=r[37], catch=r[38], exp=r[39],
            ev=dict(zip(['hp', 'atk', 'def', 'spe', 'spa', 'spd'], evs)), gender=r[42], hatch=r[43], friend=r[44],
            growth=r[45], egg=(r[46], r[47]), ab=(r[48], r[49]), items=(be16(r, 16), be16(r, 18)),
            height=be16(r, 20), weight=be16(r, 22),
            tm=[k for k in range(100) if (w[k // 32] >> (k % 32)) & 1])

    def learnset(self, dex):
        r = self.common[32][0x30 + dex * 80:0x30 + dex * 80 + 80]
        out = []
        for k in range(20):
            mv, lv, pad = be16(r, 4 * k), r[4 * k + 2], r[4 * k + 3]
            if mv == 0 and lv == 0 and pad == 0xd0:
                break
            out.append((lv, mv))
        return out

    def evolutions(self, dex):
        r = self.common[9][0x30 + dex * 42:0x30 + dex * 42 + 42]
        out = []
        for k in range(7):
            m, p, s = struct.unpack_from('>HHH', r, 6 * k)
            if m:
                out.append((m, p, s))
        return out

    def move(self, m):
        r = self.common[30][0x40 + m * 24:0x40 + m * 24 + 24]
        return dict(effect=be16(r, 0), cls=['phys', 'spec', 'status'][r[14]] if r[14] < 3 else r[14], power=r[15],
                    type=r[16], acc=r[17], pp=r[18])



class XD:
    """XD's species table straight from the disc (common.fsys / common_rel), the names and the two machine lists
    through xd_data_mods (the port's own table reader), cached in <work>/xd_cache.json."""

    def __init__(self, disc_path, work, exe):
        self.disc = noddisc.Disc(disc_path)
        d = self.disc.file('common.fsys')
        ent = [e for e in noddisc.fsys_entries(d) if e.name == 'common_rel'][0]
        rel = noddisc.extract(d, ent)
        sig = struct.pack('>6H', 35, 55, 30, 50, 40, 90)
        i = rel.find(sig)
        assert i > 0, 'pokemon_data not found in common_rel'
        self.base = i - 0x8E - 25 * REC
        assert struct.unpack_from('>6H', rel, self.base + REC + 0x8E) == (45, 49, 49, 65, 65, 45)
        self.rel = rel
        self.count = 415
        os.makedirs(work, exist_ok=True)
        cache = os.path.join(work, 'xd_cache.json')
        if os.path.exists(cache):
            c = json.load(open(cache))
        else:
            c = self._query(disc_path, exe)
            json.dump(c, open(cache, 'w'))
        self.moves = {int(k): v for k, v in c['moves'].items()}
        self.items = {int(k): v for k, v in c['items'].items()}
        self.tm_moves = c['tm_moves']
        self.tutors = c['tutors']
        self.move_by_name = {norm(v): k for k, v in self.moves.items()}
        self.item_by_name = {norm(v): k for k, v in self.items.items()}
        self.nat2xd = {}
        for s in range(1, self.count):
            n = self.numPokemon(s)
            if n:
                self.nat2xd[n] = s

    @staticmethod
    def _run(exe, disc, *a):
        env = dict(os.environ, XD_SETTINGS_DIR=os.environ.get('XD_SETTINGS_DIR', ''))
        os.makedirs(env['XD_SETTINGS_DIR'], exist_ok=True)
        return subprocess.run([exe, disc] + list(a), capture_output=True, text=True, env=env, check=True).stdout

    def _query(self, disc_path, exe):
        def names(kind):
            out = {}
            for ln in self._run(exe, disc_path, '--names', kind).splitlines():
                m = re.match(r'\s*(\d+)\s+(.*\S)\s*$', ln)
                if m:
                    out[int(m.group(1))] = m.group(2)
            return out

        def show(table, i, field):
            for ln in self._run(exe, disc_path, '--show', table, '#%d' % i).splitlines():
                m = re.match(r'\s*%s\s+(\d+)' % field, ln)
                if m:
                    return int(m.group(1))
            raise KeyError((table, i, field))
        return dict(moves=names('move'), items=names('item'),
                    tm_moves=[show('tm_moves', i, 'wazaID') for i in range(58)],
                    tutors=[show('wazaoshie_data', i, 'id') for i in range(12)])

    def rec(self, s):
        return bytearray(self.rel[self.base + s * REC:self.base + (s + 1) * REC])

    def numPokemon(self, s):
        return be16(self.rec(s), 0x12)

    def sinka(self, s):
        r = self.rec(s)
        return [struct.unpack_from('>BBHH', r, 0xA6 + 6 * k) for k in range(5)]



def u8(r, o, v):
    r[o] = v


def put16(r, o, v):
    struct.pack_into('>H', r, o, v)


def put32(r, o, v):
    struct.pack_into('>I', r, o, v)


MOVE_BASE = 375 - 355


class Conv:
    def __init__(self, pbr, xd, plan, built=None):
        self.p, self.x, self.plan = pbr, xd, plan
        self.built = built or set()
        dexes = [d for d in range(GEN4_FIRST, GEN4_LAST + 1)]
        self.dex_to_id = {}
        nxt = FIRST_NEW_ID
        for d in dexes:
            if d == BONSLY:
                self.dex_to_id[d] = XD_BONSLY
            elif d == MUNCHLAX:
                self.dex_to_id[d] = XD_MUNCHLAX
            else:
                self.dex_to_id[d] = nxt
                nxt += 1
        self.new_ids = [d for d in dexes if d not in (BONSLY, MUNCHLAX)]
        self.pkx_index = {}
        k = 0
        for d in dexes:
            if d == BONSLY:
                continue
            self.pkx_index[d] = FIRST_NEW_PKX + k
            k += 1
        self.id_of = dict(self.x.nat2xd)
        self.id_of.update(self.dex_to_id)
        self.dex_of_id = {v: k for k, v in self.id_of.items()}

        self.move_rows = self._move_map()
        self.item_rows = self._item_map()
        self.machine_rows = self._machine_map()


    def _move_map(self):
        rows = {}
        by = self.x.move_by_name
        sub = {norm(k): v for k, v in R.MOVE_SUBSTITUTE.items()}
        for m in range(1, 468):
            name = self.p.move_name(m)
            pm = self.p.move(m)
            if m <= 354:
                xid = m if m in self.x.moves else 0
                rule = 'same id'
                if norm(self.x.moves.get(m, '')) != norm(name):
                    rule = 'same id (name differs: XD "%s")' % self.x.moves.get(m)
            elif name in self.built:
                xid, rule = m + MOVE_BASE, 'real move (Gen 4 moves mod)'
            else:
                if norm(name) not in sub:
                    raise KeyError('no substitute rule for Gen 4 move %d %s' % (m, name))
                tgt = sub[norm(name)]
                if tgt is None:
                    xid, rule = 0, 'dropped (no XD move with that job)'
                else:
                    xid = by[norm(tgt)]
                    rule = 'substitute'
            rows[m] = dict(pbr=m, name=name, type=TYPE_NAMES[pm['type']] if pm['type'] < 18 else pm['type'],
                           cls=pm['cls'], power=pm['power'], xd=xid, xd_name=self.x.moves.get(xid, ''), rule=rule)
        return rows


    def _item_map(self):
        rows = {}
        for i in range(1, 600):
            if 1990 + i >= len(self.p.text):
                break
            nm = self.p.item_name(i)
            xid = self.x.item_by_name.get(norm(nm), 0)
            rows[i] = dict(pbr=i, name=nm, xd=xid, xd_name=self.x.items.get(xid, ''))
        return rows

    def _machine_map(self):
        rows = []
        tm_index = {mv: k for k, mv in enumerate(self.p.tmlist)}
        for i, mv in enumerate(self.x.tm_moves):
            rows.append(dict(machine=i, xd_move=mv, xd_name=self.x.moves.get(mv, ''), pbr_bit=tm_index.get(mv),
                             pbr_label=self._tmlabel(tm_index.get(mv))))
        return rows

    @staticmethod
    def _tmlabel(k):
        if k is None:
            return ''
        return 'TM%02d' % (k + 1) if k < 92 else 'HM%02d' % (k - 91)


    def ability(self, a):
        if a == 0 or a <= 76:
            return a, ''
        if a in R.ABILITY_SUBSTITUTE:


            nm, x = R.ABILITY_SUBSTITUTE[a]
            return a, 'Gen 4 ability %d %s kept (XD stand-in was %d)' % (a, nm, x)
        return 0, 'Gen 4 ability %d (unknown) -> none' % a


    def evo_entry(self, src_dex, m, par, tgt, notes):
        """-> (kind, buff, XD species id) or None."""
        tid = self.id_of.get(tgt)
        if tid is None:
            notes.append('evolution target %d unknown' % tgt)
            return None
        src_name = self.p.species_name(src_dex).title()
        baby = norm(src_name) in {norm(b) for b in R.BABIES}
        fb = R.FALLBACK_LEVEL_BABY if baby else R.FALLBACK_LEVEL
        if m == 1:
            return 1, 0, tid
        if m in (2, 3):
            return 1, 0, tid
        if m == 4:
            return 4, par, tid
        if m == 5:
            return 5, 0, tid
        if m in (6, 7):
            xi = self.item_rows.get(par, {}).get('xd', 0)
            if xi:
                return m, xi, tid
            notes.append('evolution to %s: Gen 4 item "%s" is not in XD -> level %d' %
                         (self.p.species_name(tgt), self.p.item_name(par), fb))
            return 4, fb, tid
        if m in (8, 9, 10, 11, 12, 15):
            return m, par, tid
        if m == 13:
            return 4, par, tid
        if m == 14:
            return 14, par, tid
        if m == 22:
            return 12, par, tid
        if m == 23:
            return (11, par, tid) if src_dex == 412 else (4, par, tid)
        notes.append('evolution to %s: method %d has no XD counterpart -> level %d' % (self.p.species_name(tgt), m, fb))
        return 4, fb, tid


    def species(self, dex):
        p = self.p
        pr = p.personal(dex)
        sid = self.dex_to_id[dex]
        existing = sid in (XD_BONSLY, XD_MUNCHLAX)
        notes = []
        name = p.species_name(dex)
        stand = self.plan.get(dex)
        standin_id = None
        if stand:
            standin_id = stand['standin_id']
        sref = self.x.rec(standin_id) if standin_id else bytearray(REC)
        r = bytearray(REC)
        old = self.x.rec(sid) if existing else None

        r[0x00] = pr['growth'] if pr['growth'] < 6 else 0
        r[0x01] = pr['catch']
        r[0x02] = pr['gender']
        put16(r, 0x04, pr['exp'])
        put16(r, 0x06, pr['friend'])
        put16(r, 0x08, pr['height'])
        put16(r, 0x0A, pr['weight'])
        idx = self.new_ids.index(dex) if dex in self.new_ids else None
        order = list(range(GEN4_FIRST, GEN4_LAST + 1)).index(dex)

        if existing:
            for o in (0x0C, 0x10):
                put16(r, o, be16(old, o))
            put32(r, 0x18, struct.unpack_from('>I', old, 0x18)[0])
            name_msg = struct.unpack_from('>I', old, 0x18)[0]
        else:
            put16(r, 0x0C, be16(sref, 0x0C))
            put16(r, 0x10, be16(sref, 0x10))
            name_msg = FIRST_NEW_NAME_MSG + idx
            put32(r, 0x18, name_msg)
        put16(r, 0x12, dex)
        put16(r, 0x0E, dex)
        put16(r, 0x14, 387 + order)
        put32(r, 0x1C, struct.unpack_from('>I', sref, 0x1C)[0] if stand else 0)
        put32(r, 0x20, 0)
        if existing and False:
            pass
        mita, tuka = FLAG_BASE + order, FLAG_BASE + len(range(GEN4_FIRST, GEN4_LAST + 1)) + order
        put32(r, 0x24, mita)
        put32(r, 0x28, tuka)
        if sid == XD_BONSLY:
            put32(r, 0x2C, struct.unpack_from('>I', old, 0x2C)[0])
        else:
            put32(r, 0x2C, self.pkx_index[dex])
        r[0x30], r[0x31] = pr['t1'], pr['t2']
        a1, n1 = self.ability(pr['ab'][0])
        a2, n2 = self.ability(pr['ab'][1])
        if a2 == a1:
            a2 = 0
        r[0x32], r[0x33] = a1, a2
        for n in (n1, n2):
            if n:
                notes.append(n)

        learned = {mv for lv, mv in p.learnset(dex)}
        tmbits = set(pr['tm'])
        mach_notes = []
        for i, row in enumerate(self.machine_rows):
            k = row['pbr_bit']
            if k is not None:
                ok = k in tmbits
            else:
                ok = row['xd_move'] in learned
                if ok:
                    mach_notes.append(row['xd_name'])
            r[0x34 + i] = 1 if ok else 0
        if mach_notes:
            notes.append('machines with no Gen 4 TM/HM (set because the level-up list has the move): ' + ', '.join(mach_notes))



        tm_of = {m: k for k, m in enumerate(p.tmlist)}
        for i, mv in enumerate(self.x.tutors):
            r[0x6E + i] = 1 if (mv in learned or (mv in tm_of and tm_of[mv] in tmbits)) else 0

        held = []
        for k, it in enumerate(pr['items']):
            xi = self.item_rows.get(it, {}).get('xd', 0) if it else 0
            put16(r, 0x7A + 2 * k, xi)
            held.append((it, p.item_name(it) if it else '', xi))

        stats = [pr['hp'], pr['atk'], pr['de'], pr['spa'], pr['spd'], pr['spe']]
        for k, v in enumerate(stats):
            put16(r, 0x8E + 2 * k, v)
        ev = pr['ev']
        for k, key in enumerate(['hp', 'atk', 'def', 'spa', 'spd', 'spe']):
            put16(r, 0x9A + 2 * k, ev[key])

        evo = []
        for m, par, tgt in p.evolutions(dex):
            if tgt < GEN4_FIRST and tgt not in self.id_of:
                continue
            e = self.evo_entry(dex, m, par, tgt, notes)
            if e:
                evo.append(e)
        for k, (kind, buff, tid) in enumerate(evo[:5]):
            struct.pack_into('>BBHH', r, 0xA6 + 6 * k, kind, 0, buff, tid)
        if len(evo) > 5:
            notes.append('more than 5 evolutions: %d dropped' % (len(evo) - 5))

        ls, dropped, seen = [], [], set()
        for lv, mv in p.learnset(dex):
            row = self.move_rows[mv]
            if row['xd'] == 0:
                dropped.append('%d:%s' % (lv, row['name']))
                continue
            if row['xd'] in seen:
                if row['rule'] != 'same id':
                    dropped.append('%d:%s (same XD move as an earlier entry)' % (lv, row['name']))
                continue
            seen.add(row['xd'])
            ls.append((lv, row['xd']))
        ls.sort(key=lambda e: e[0])
        for k, (lv, xm) in enumerate(ls[:20]):
            r[0xC4 + 4 * k] = lv
            put16(r, 0xC6 + 4 * k, xm)
        if dropped:
            notes.append('learnset entries dropped: ' + ', '.join(dropped))

        for i in range(2):
            o = 0x114 + 8 * i
            if existing:
                r[o:o + 8] = old[o:o + 8]
            else:
                r[o:o + 8] = sref[o:o + 8]
        face = [be16(r, 0x116), be16(r, 0x11E)]
        fields = dict(
            growDataId=r[0], get=r[1], sexRatio=r[2], giveExp=be16(r, 4), initFriend=be16(r, 6), height=be16(r, 8),
            weight=be16(r, 0xA), voice=be16(r, 0xC), shout=be16(r, 0x10), numPokemon=be16(r, 0x12), numZukan=be16(r, 0x14),
            name=name_msg, typeName=struct.unpack_from('>I', r, 0x1C)[0], mitaFlag=mita, tukamaetaFlag=tuka,
            pkxDataId=struct.unpack_from('>I', r, 0x2C)[0], zokuseiDataId=[r[0x30], r[0x31]], tokuseiDataId=[r[0x32], r[0x33]],
            wazaMcn=list(r[0x34:0x6E]), teachWaza=list(r[0x6E:0x7A]), itemDataId=[be16(r, 0x7A), be16(r, 0x7C)],
            basisStatus=[be16(r, 0x8E + 2 * k) for k in range(12)],
            sinka=[dict(kind=k, buff=b, pokemonDataId=t) for k, b, t in evo[:5]],
            waza=[dict(level=lv, dataId=xm) for lv, xm in ls[:20]],
        )
        extra = dict(
            dex=dex, name=name, xd_species_id=sid, replaces_existing_xd_entry=existing, model=(stand or {}).get('model'),
            pkx_data_id=struct.unpack_from('>I', r, 0x2C)[0], standin=(stand or {}).get('standin'),
            standin_species_id=standin_id,
            name_message_id=name_msg, name_text=name,
            types=[TYPE_NAMES[r[0x30]], TYPE_NAMES[r[0x31]]], growth=GROWTH_DS[r[0]],
            abilities_gen4=[p.ability_name(a) for a in pr['ab']], abilities_gen4_ids=list(pr['ab']),
            abilities_xd_ids=[r[0x32], r[0x33]],
            egg_groups=[EGG_GROUPS[g] for g in pr['egg'] if g < len(EGG_GROUPS)], hatch_cycles=pr['hatch'],
            held_items_gen4=held, ev_yield=ev, base_stats_gen4=dict(hp=pr['hp'], atk=pr['atk'], de=pr['de'], spa=pr['spa'],
                                                                      spd=pr['spd'], spe=pr['spe']),
            learnset_pbr=[(lv, mv, p.move_name(mv)) for lv, mv in p.learnset(dex)],
            learnset_xd=[(lv, xm, self.x.moves.get(xm, '')) for lv, xm in ls[:20]],
            evolutions_xd=[dict(kind=k, buff=b, to=t, to_dex=self.dex_of_id.get(t)) for k, b, t in evo[:5]],
            face_sprite_ids_placeholder=face, notes=notes,
            dex_flags_proposed=dict(seen=mita, caught=tuka),
        )
        return dict(format='gen4_species/1', record_hex=bytes(r).hex(), fields=fields, extra=extra)


    def existing_patches(self):
        out = []
        for dex in range(1, GEN4_FIRST):
            if dex not in self.x.nat2xd:
                continue
            sid = self.x.nat2xd[dex]
            for m, par, tgt in self.p.evolutions(dex):
                if tgt < GEN4_FIRST or tgt > GEN4_LAST:
                    continue
                notes = []
                e = self.evo_entry(dex, m, par, tgt, notes)
                cur = self.x.sinka(sid)
                free = [k for k, (kind, _p, _b, t) in enumerate(cur) if kind == 0 and t == 0]
                item = dict(xd_species_id=sid, dex=dex, name=self.p.species_name(dex), to_dex=tgt,
                            to_name=self.p.species_name(tgt), pbr_method=m, pbr_param=par, notes=notes)
                if e is None:
                    item['skipped'] = 'no XD form'
                elif not free:
                    item['skipped'] = 'no free evolution slot (XD has 5)'
                elif m in (16, 17, 18, 19, 20, 21, 24, 25, 26) and any('->' in n and 'level' in n for n in notes):
                    item['optional_fallback'] = True
                    item.update(slot=free[0], kind=e[0], buff=e[1], pokemonDataId=e[2])
                else:
                    item.update(slot=free[0], kind=e[0], buff=e[1], pokemonDataId=e[2])
                out.append(item)
        return out



    def existing_machines(self):
        out = []
        for sid in range(1, 412):
            dex = self.x.numPokemon(sid)
            if not dex or dex >= GEN4_FIRST:
                continue
            r = self.x.rec(sid)
            tbits = set(self.p.personal(dex)['tm'])
            add = [i for i, row in enumerate(self.machine_rows) if row['pbr_bit'] is not None and row['pbr_bit'] in tbits and not r[0x34 + i]]
            if add:
                out.append(dict(xd_species_id=sid, dex=dex, name=self.p.species_name(dex), machines=add))
        return out


def load_plan(path, x, pbr):
    """plan.tsv of gen4.py (dex, model, stand-in model, ...) -> {dex: {model, standin, standin_id}}"""
    pairs = {}
    batch = os.path.join(os.path.dirname(path), '..', 'pbrconv2', 'batch.tsv')
    batch = ''
    for ln in open(batch).read().splitlines()[1:]:
        f = ln.split('\t')
        arch = f[1]
        m = re.match(r'pkx_(\d+)\.fsys', arch)
        nat = int(m.group(1)) if m else None
        pairs[f[0]] = (nat, int(f[2]) if f[2].isdigit() else 0)
    plan = {}
    for ln in open(path).read().splitlines():
        f = ln.split('\t')
        if len(f) < 3 or not f[0].isdigit():
            continue
        dex, model, st = int(f[0]), f[1], f[2]
        nat, xdsp = pairs.get(st, (None, 0))
        sid = x.nat2xd.get(nat) if nat else None
        if not sid and xdsp in range(1, 412):
            sid = xdsp
        plan[dex] = dict(model=model, standin=st, standin_id=sid)
    return plan


def make(a):
    os.makedirs(os.path.join(a.out, 'species'), exist_ok=True)
    pbr = PBR(a.pbr)
    x = XD(a.xd, a.out, a.exe)
    plan = load_plan(a.plan, x, pbr)
    built = set()
    if a.moves:
        for ln in open(os.path.join(a.moves, 'classification.tsv'), encoding='utf-8').read().splitlines()[1:]:
            f = ln.split('\t')
            if f[10] in ('a', 'b', 'c'):
                built.add(f[1])
    c = Conv(pbr, x, plan, built)
    allspec, summary = [], []
    for dex in range(GEN4_FIRST, GEN4_LAST + 1):
        s = c.species(dex)
        e = s['extra']
        fn = os.path.join(a.out, 'species', '%03d_%s.json' % (dex, re.sub(r'[^A-Za-z0-9]', '', e['name'])))
        json.dump(s, open(fn, 'w'), indent=1)
        allspec.append(s)
        summary.append(e)
    order = sorted(allspec, key=lambda s: s['extra']['xd_species_id'])
    json.dump(dict(format='gen4_species_all/1', first_new_id=FIRST_NEW_ID, count=len(order),
                   record_size=REC, species=order), open(os.path.join(a.out, 'species_all.json'), 'w'))
    json.dump(c.existing_patches(), open(os.path.join(a.out, 'existing_patches.json'), 'w'), indent=1)
    json.dump(c.existing_machines(), open(os.path.join(a.out, 'existing_machines.json'), 'w'), indent=1)
    json.dump(summary, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
    with open(os.path.join(a.out, 'summary.tsv'), 'w', encoding='utf-8') as f:
        f.write('dex\tname\txd_id\tpkx_id\ttypes\tstats(hp/atk/def/spa/spd/spe)\tabilities(xd)\tmoves\tmodel\tstandin\tnotes\n')
        for s in allspec:
            e, fl = s['extra'], s['fields']
            f.write('%d\t%s\t%d\t%d\t%s\t%s\t%s\t%d\t%s\t%s\t%d\n' % (
                e['dex'], e['name'], e['xd_species_id'], e['pkx_data_id'], '/'.join(e['types']),
                '/'.join(map(str, fl['basisStatus'][:6])), '/'.join(map(str, e['abilities_xd_ids'])),
                len(fl['waza']), e['model'], e['standin'], len(e['notes'])))
    with open(os.path.join(a.out, 'move_map.tsv'), 'w', encoding='utf-8') as f:
        f.write('pbr_id\tpbr_name\ttype\tclass\tpower\txd_id\txd_name\trule\n')
        for m in sorted(c.move_rows):
            r = c.move_rows[m]
            f.write('%d\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n' % (m, r['name'], r['type'], r['cls'], r['power'], r['xd'], r['xd_name'], r['rule']))
    with open(os.path.join(a.out, 'ability_map.tsv'), 'w', encoding='utf-8') as f:
        f.write('gen4_id\tgen4_name\txd_id\trule\n')
        for i in range(1, 124):
            xi, n = c.ability(i)
            f.write('%d\t%s\t%d\t%s\n' % (i, pbr.ability_name(i), xi, 'same id' if i <= 76 else 'substitute'))
    with open(os.path.join(a.out, 'item_map.tsv'), 'w', encoding='utf-8') as f:
        f.write('gen4_id\tgen4_name\txd_id\txd_name\n')
        for i, r in c.item_rows.items():
            f.write('%d\t%s\t%d\t%s\n' % (i, r['name'], r['xd'], r['xd_name']))
    with open(os.path.join(a.out, 'machine_map.tsv'), 'w', encoding='utf-8') as f:
        f.write('xd_machine\txd_move\txd_move_name\tgen4_bit\tgen4_label\n')
        for r in c.machine_rows:
            f.write('%d\t%d\t%s\t%s\t%s\n' % (r['machine'], r['xd_move'], r['xd_name'], r['pbr_bit'], r['pbr_label']))
    with open(os.path.join(a.out, 'species_ids.tsv'), 'w', encoding='utf-8') as f:
        f.write('dex\tname\txd_species_id\tpkx_data_id\tname_message\tseen_flag\tcaught_flag\n')
        for s in order:
            e = s['extra']
            f.write('%d\t%s\t%d\t%d\t%d\t%d\t%d\n' % (e['dex'], e['name'], e['xd_species_id'], e['pkx_data_id'],
                                                     e['name_message_id'], e['dex_flags_proposed']['seen'], e['dex_flags_proposed']['caught']))
    print('wrote %d species into %s' % (len(allspec), a.out))
    print('moves: %d dropped Gen 4 moves, %d substituted' % (
        sum(1 for r in c.move_rows.values() if r['xd'] == 0), sum(1 for r in c.move_rows.values() if r['rule'] == 'substitute')))



def decode(rec_hex):
    r = bytes.fromhex(rec_hex)
    g = lambda o: struct.unpack_from('>H', r, o)[0]
    return dict(stats=[g(0x8E + 2 * k) for k in range(6)], types=(r[0x30], r[0x31]), abil=(r[0x32], r[0x33]),
                catch=r[1], exp=g(4), sex=r[2], grow=r[0], friend=g(6),
                waza=[(r[0xC4 + 4 * k], g(0xC6 + 4 * k)) for k in range(20) if g(0xC6 + 4 * k)],
                sinka=[struct.unpack_from('>BBHH', r, 0xA6 + 6 * k) for k in range(5)], items=(g(0x7A), g(0x7C)))


def check(a):
    pbr = PBR(a.pbr)
    x = XD(a.xd, a.out, a.exe)
    ok = True
    for dex in (448, 445, 468, 392, 479):
        s = json.load(open([os.path.join(a.out, 'species', f) for f in os.listdir(os.path.join(a.out, 'species'))
                            if f.startswith('%03d_' % dex)][0]))
        d = decode(s['record_hex'])
        pr = pbr.personal(dex)

        want_stats = [pr['hp'], pr['atk'], pr['de'], pr['spa'], pr['spd'], pr['spe']]
        print('== %s (dex %d, XD id %d)' % (pbr.species_name(dex), dex, s['extra']['xd_species_id']))
        print('  PBR stats HP/Atk/Def/SpA/SpD/Spe', want_stats, ' XD record', d['stats'], 'OK' if want_stats == d['stats'] else 'MISMATCH')
        ok &= want_stats == d['stats']
        print('  types PBR', (TYPE_NAMES[pr['t1']], TYPE_NAMES[pr['t2']]), ' XD record', (TYPE_NAMES[d['types'][0]], TYPE_NAMES[d['types'][1]]))
        ok &= (pr['t1'], pr['t2']) == d['types']
        print('  abilities PBR', [pbr.ability_name(v) for v in pr['ab']], ' XD ids', d['abil'], s['extra']['notes'][:2])
        print('  catch/exp/gender/growth PBR', (pr['catch'], pr['exp'], pr['gender'], GROWTH_DS[pr['growth']]),
              ' XD', (d['catch'], d['exp'], d['sex'], GROWTH_DS[d['grow']]))
        print('  level-up (PBR): ', ' '.join('%d:%s' % (lv, pbr.move_name(mv)) for lv, mv in pbr.learnset(dex)))
        print('  level-up (XD):  ', ' '.join('%d:%s' % (lv, x.moves.get(mv, pbr.move_name(mv - MOVE_BASE) if mv >= 375 else '?')) for lv, mv in d['waza']))
        print('  evolutions XD', [(k, b, t) for k, _p, b, t in d['sinka'] if k or t])
    for dex, lvl in ((448, None),):
        pass
    print('ALL OK' if ok else 'PROBLEMS')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['make', 'check'])
    ap.add_argument('out')
    ap.add_argument('--pbr', default='')
    ap.add_argument('--xd', default='')
    ap.add_argument('--plan', default='')
    ap.add_argument('--exe', default=DEFAULT_EXE)
    ap.add_argument('--moves', help='the folder of gen4_moves.py (classification.tsv): learn-sets use the real Gen 4 moves it offers')
    a = ap.parse_args()
    return {'make': make, 'check': check}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main() or 0)
