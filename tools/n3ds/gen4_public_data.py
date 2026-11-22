"""gen4_public_data: builds the Gen 4 numbers and learn-sets of the USUM Gen 4 pack from PUBLIC data only (maintainer tool, not run by players).

Writes two generated modules next to it:
    usum_gen4_values.py     Diamond/Pearl numbers that differ from Generation 7's (species rows, move rows, King's Rock moves)
    usum_gen4_learnsets.py  level-up lists and machine (TM/HM) lists of the 107 Gen 4 species

Sources (all public, nothing from any game disc):
  * PokeAPI public data, https://github.com/PokeAPI/pokeapi  (data/v2/csv, raw.githubusercontent.com): base stats and types (with
    their "past" tables), moves and their changelog, level-up and machine learn-sets per version group.
  * pokemondb.net species pages (https://pokemondb.net/pokedex/<name>): the sentences "In Generation 4, X has a base experience
    yield of N" and "From Pokemon Diamond & Pearl ... X has a catch rate of N" (PokeAPI keeps no past base experience / catch rate).
  * Pokemon Showdown (MIT), https://github.com/smogon/pokemon-showdown data/mods/gen4/items.ts: the list of moves King's Rock affects in Gen 4;
    data/mods/gen4/moves.ts: the flags (protect / reflectable (Magic Coat) / snatch / mirror) of the moves Generation 4 differs on.
  * PokeAPI pokemon_items.csv (version diamond) + item_game_indices.csv (generation 4 numbers): wild held items.
Version group for learn-sets and move numbers: see LEARN_VG / MOVE_VG below (reason in docs/GEN4_USUM.md).

Usage:  python tools/n3ds/gen4_public_data.py <cache folder> [--vg diamond-pearl]
The cache folder keeps the downloads (re-runs do not fetch again). Fetching is polite: one pokemondb page a second, an honest User-Agent.
"""
import csv
import html
import os
import re
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = 'https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/%s.csv'
CSVS = ['pokemon_items', 'items', 'item_game_indices','pokemon', 'pokemon_stats', 'pokemon_stats_past', 'pokemon_types', 'pokemon_types_past', 'moves', 'move_changelog',
        'pokemon_moves', 'version_groups', 'pokemon_species']
PS_MOVES = 'https://raw.githubusercontent.com/smogon/pokemon-showdown/master/data/mods/gen4/moves.ts'
PS_ITEMS ='https://raw.githubusercontent.com/smogon/pokemon-showdown/master/data/mods/gen4/items.ts'
POKEMONDB = 'https://pokemondb.net/pokedex/%s'
UA = 'recomp-gen4-table/1.0 (personal hobby project; one request a second)'
FIRST, LAST = 387, 493
LAST_MOVE = 467
STAT = {1: 'hp', 2: 'atk', 3: 'def', 4: 'spa', 5: 'spd', 6: 'spe'}
LEARN_VG = 'diamond-pearl'
MOVE_VG = 'diamond-pearl'


def get(url, path):
    if not os.path.exists(path):
        req = urllib.request.Request(url, headers={'User-Agent': UA})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        with open(path, 'wb') as f:
            f.write(data)
    with open(path, 'rb') as f:
        return f.read()


def rows(cache, name):
    get(RAW % name, os.path.join(cache, name + '.csv'))
    with open(os.path.join(cache, name + '.csv'), encoding='utf8') as f:
        return list(csv.DictReader(f))


def gen4_type(t):
    """PokeAPI type id -> Gen 4 type number (0 Normal .. 8 Steel, 9 ???, 10 Fire .. 17 Dark)."""
    return t - 1 if t <= 9 else t


class Public:
    def __init__(self, cache, learn_vg=LEARN_VG, move_vg=MOVE_VG):
        os.makedirs(cache, exist_ok=True)
        self.cache = cache
        self.R = {n: rows(cache, n) for n in CSVS}
        vgs = {r['identifier']: int(r['id']) for r in self.R['version_groups']}
        self.learn_vg, self.move_vg = vgs[learn_vg], vgs[move_vg]
        self.learn_vg_name = learn_vg
        self.slug = {int(r['id']): r['identifier'] for r in self.R['pokemon_species'] if int(r['id']) <= 493}
        self.mv = {int(r['id']): r for r in self.R['moves'] if int(r['id']) <= LAST_MOVE}
        self.mname = {i: r['identifier'] for i, r in self.mv.items()}


    def stats_at(self, dex, gen):
        """{hp, atk, def, spa, spd, spe} in generation `gen`: PokeAPI's "past" rows hold the values that applied up to and
        including their generation; the smallest such generation >= gen wins, else today's values."""
        cur = {STAT[int(r['stat_id'])]: int(r['base_stat']) for r in self.R['pokemon_stats'] if int(r['pokemon_id']) == dex}
        past = [r for r in self.R['pokemon_stats_past'] if int(r['pokemon_id']) == dex and int(r['generation_id']) >= gen]
        if past:
            g = min(int(r['generation_id']) for r in past)
            for r in past:
                if int(r['generation_id']) == g:
                    cur[STAT[int(r['stat_id'])]] = int(r['base_stat'])
        return cur

    def types_at(self, dex, gen):
        cur = {int(r['slot']): int(r['type_id']) for r in self.R['pokemon_types'] if int(r['pokemon_id']) == dex}
        past = [r for r in self.R['pokemon_types_past'] if int(r['pokemon_id']) == dex and int(r['generation_id']) >= gen]
        if past:
            g = min(int(r['generation_id']) for r in past)
            cur = {int(r['slot']): int(r['type_id']) for r in past if int(r['generation_id']) == g}
        t1 = cur[1]
        return t1, cur.get(2, t1)

    def page_numbers(self, dex):
        """(base exp, catch rate) of Generation 4 from the species page. Base exp: the sentence "In Generation 4, X has a base experience
        yield of N", else the plain sentence "X has a base experience yield of N", else None (then today's value stands: it never changed).
        Catch rate: "From Pokemon Diamond & Pearl to ... has a catch rate of N", else None."""
        path = os.path.join(self.cache, 'pokemondb_%s.html' % self.slug[dex])
        if not os.path.exists(path):
            time.sleep(1.0)
        txt = html.unescape(re.sub(r'<[^>]+>', ' ', get(POKEMONDB % self.slug[dex], path).decode('utf8', 'replace')))
        txt = re.sub(r'\s+', ' ', txt)
        m = re.search(r'In Generation 4 , [^.]*? has a base experience yield of (\d+)', txt) or             re.search(r'(?<![\w,]) has a base experience yield of (\d+)', txt)
        exp = int(m.group(1)) if m else None
        m = re.search(r'From Pok.mon Diamond (?:&|and) Pearl to [^.]*? has a catch rate of (\d+)', txt)
        catch = int(m.group(1)) if m else None
        return exp, catch

    def species_table(self):
        """{dex: {hp atk def spe spa spd t1 t2 catch exp: Generation 4 value}} for every one of the 107 species."""
        out = {}
        cur = {int(r['id']): r for r in self.R['pokemon_species']}
        base = {int(r['id']): int(r['base_experience'] or 0) for r in self.R['pokemon']}
        for dex in range(FIRST, LAST + 1):
            s4 = self.stats_at(dex, 4)
            t1, t2 = self.types_at(dex, 4)
            exp, catch = self.page_numbers(dex)
            out[dex] = dict(hp=s4['hp'], atk=s4['atk'], spe=s4['spe'], spa=s4['spa'], spd=s4['spd'], t1=gen4_type(t1), t2=gen4_type(t2),
                            catch=catch if catch is not None else int(cur[dex]['capture_rate']), exp=exp if exp is not None else base[dex])
            out[dex]['def'] = s4['def']
        return out


    def move_at(self, m, vg):
        """{power, acc, pp, chance} of move m in version group vg: PokeAPI's changelog rows hold the values valid BEFORE their version
        group, so the first row after vg that names a field gives it; else today's value."""
        cur = self.mv[m]
        out = {'power': cur['power'], 'acc': cur['accuracy'], 'pp': cur['pp'], 'chance': cur['effect_chance']}
        rs = sorted((r for r in self.R['move_changelog'] if int(r['move_id']) == m and int(r['changed_in_version_group_id']) > vg),
                    key=lambda r: int(r['changed_in_version_group_id']), reverse=True)
        for r in rs:
            for f, k in (('power', 'power'), ('accuracy', 'acc'), ('pp', 'pp'), ('effect_chance', 'chance')):
                if r[f] != '':
                    out[k] = r[f]
        return out

    def move_table(self, gen7_vg=18):
        """{move number: {field: Gen 4 value}} for the Gen 4 moves (355..467) whose power / accuracy / PP / effect chance in Gen 4
        (version group MOVE_VG) differs from Generation 7's (Ultra Sun / Moon, version group 18). An empty field = none (never misses)."""
        out = {}
        for m in range(355, LAST_MOVE + 1):
            a, b = self.move_at(m, self.move_vg), self.move_at(m, gen7_vg)
            d = {k: int(a[k] or 0) for k in a if a[k] != b[k]}
            if d:
                out[m] = d
        return out

    def names(self):
        """{move number: English display name} (PokeAPI move_names.csv, language 9)."""
        return {int(r['move_id']): r['name'] for r in rows(self.cache, 'move_names') if r['local_language_id'] == '9' and int(r['move_id']) <= LAST_MOVE}


    def kings_rock(self):
        txt = get(PS_ITEMS, os.path.join(self.cache, 'showdown_gen4_items.ts')).decode('utf8')
        blk = re.search(r'affectedByKingsRock = \[(.*?)\];', txt, re.S).group(1)
        ids = set(re.findall(r"'([a-z]+)'", blk))
        flat = {n.replace('-', ''): m for m, n in self.mname.items()}
        return sorted(flat[i] for i in ids if i in flat and flat[i] >= 355), sorted(i for i in ids if i not in flat)


    def held_items(self, dex, version=12):
        """[common, rare] held item of a wild Pokemon in Diamond (version 12), as Generation 4 item numbers (0 = none): the table's numbering, the item of 50 % / 100 % first, the item of 5 % second; a 100 % item fills both."""
        g3 = {}
        for r in self.R['item_game_indices']:
            if r['generation_id'] == '4':
                g3[r['item_id']] = int(r['game_index'])
        common = rare = 0
        for r in self.R['pokemon_items']:
            if int(r['pokemon_id']) == dex and int(r['version_id']) == version:
                n = g3.get(r['item_id'], 0)
                if r['rarity'] in ('50', '100'):
                    common = n
                if r['rarity'] in ('5', '100'):
                    rare = n
        return [common, rare]

    def move_flags(self):
        """{move number: (protect, reflectable, snatch, mirror)} for the Gen 4 moves the Showdown Gen 4 data gives flags of their own."""
        txt = get(PS_MOVES, os.path.join(self.cache, 'showdown_gen4_moves.ts')).decode('utf8')
        parts = re.split(r'\n\t(\w+): \{\n', txt)
        flat = {n.replace('-', ''): m for m, n in self.mname.items()}
        out = {}
        for i in range(1, len(parts), 2):
            m = flat.get(parts[i])
            f = re.search(r'\n\t\tflags: \{([^}]*)\}', parts[i + 1])
            if m and m >= 355 and f:
                names = set(re.findall(r'(\w+): 1', f.group(1)))
                out[m] = tuple(int(k in names) for k in ('protect', 'reflectable', 'snatch', 'mirror'))
        return out


    def levelup(self, vg=None):
        vg = vg or self.learn_vg
        out = {d: [] for d in range(FIRST, LAST + 1)}
        for r in self.R['pokemon_moves']:
            d = int(r['pokemon_id'])
            if d < FIRST or d > LAST or int(r['version_group_id']) != vg or r['pokemon_move_method_id'] != '1':
                continue
            if int(r['move_id']) > LAST_MOVE:
                continue
            out[d].append((int(r['level']), int(r['order'] or 0), int(r['move_id'])))
        return {d: [(lv, m) for lv, o, m in sorted(v)] for d, v in out.items()}

    def machines(self, vg=None):
        """{dex: sorted move numbers learnt by machine (TM and HM) in the version group}."""
        vg = vg or self.learn_vg
        out = {d: set() for d in range(1, LAST + 1)}
        for r in self.R['pokemon_moves']:
            d = int(r['pokemon_id'])
            if 1 <= d <= LAST and int(r['version_group_id']) == vg and r['pokemon_move_method_id'] == '4' and int(r['move_id']) <= LAST_MOVE:
                out[d].add(int(r['move_id']))
        return {d: sorted(v) for d, v in out.items()}

    def tutors(self, vg=None):
        vg = vg or self.learn_vg
        out = {d: set() for d in range(FIRST, LAST + 1)}
        for r in self.R['pokemon_moves']:
            d = int(r['pokemon_id'])
            if FIRST <= d <= LAST and int(r['version_group_id']) == vg and r['pokemon_move_method_id'] == '3' and int(r['move_id']) <= LAST_MOVE:
                out[d].add(int(r['move_id']))
        return {d: sorted(v) for d, v in out.items()}


def write_modules(pub, out_dir, move_names):
    sp = pub.species_table()
    mv = pub.move_table()
    kr, unknown = pub.kings_rock()
    src = '"""Diamond/Pearl numbers for the USUM Gen 4 pack (the "Gen 4 values" switch): stats, types, catch rate, base experience, a few moves, King\'s Rock.\n\n' \
          'GENERATED by tools/n3ds/gen4_public_data.py from public data only; do not edit by hand.\n' \
          'Sources: PokeAPI public data (https://github.com/PokeAPI/pokeapi, data/v2/csv: pokemon_stats(_past), pokemon_types(_past), moves,\n' \
          'move_changelog; version group %s) for stats, types and move numbers; pokemondb.net species pages (https://pokemondb.net/pokedex/<name>)\n' \
          'for Generation 4 base experience and catch rate; Pokemon Showdown data/mods/gen4/items.ts (MIT) for the King\'s Rock move list.\n' \
          'Numbers only. Types are Gen 4 type numbers (0 Normal .. 8 Steel, 9 ???, 10 Fire .. 17 Dark); fields: hp atk def spe spa spd t1 t2 catch exp.\n' \
          'Switch: `usum_models --gen4-values on|off` (default ON, see docs/GEN4_USUM.md)."""\n\n' % MOVE_VG
    src += 'SPECIES = {\n' + ''.join(' %d: %r,\n' % (d, dict(sorted(v.items()))) for d, v in sorted(sp.items())) + '}    # national number -> {field: Gen 4 value}\n\n'
    src += 'MOVES = {\n' + ''.join(' %r: %r,\n' % (move_names[m], dict(sorted(v.items()))) for m, v in sorted(mv.items())) + '}    # move name -> {field: Gen 4 value}; fields power acc pp chance\n\n'
    src += 'KINGS_ROCK = %r\n\n' % [move_names[m] for m in kr]
    held = {d: pub.held_items(d) for d in range(FIRST, LAST + 1)}
    src += 'HELD_ITEMS = {\n' + ''.join(' %d: %r,\n' % (d, v) for d, v in sorted(held.items()) if v != [0, 0]) + \
           '}    # national number -> [common, rare] held item (Generation 4 item numbers)\n\n'
    src += 'MOVE_FLAGS = {\n' + ''.join(' %r: %r,\n' % (move_names[m], v) for m, v in sorted(pub.move_flags().items())) + \
           '}    # move name -> (protect, magic coat, snatch, mirror move)\n'
    src += tail
    with open(os.path.join(out_dir, 'usum_gen4_values.py'), 'w', encoding='utf8') as f:
        f.write(src)
    lv, mc = pub.levelup(), pub.machines()
    ls = ('"""Diamond/Pearl learn-sets of the 107 Gen 4 species (the USUM Gen 4 pack uses them instead of Generation 7\'s).\n\n'
          'GENERATED by tools/n3ds/gen4_public_data.py from public data only; do not edit by hand.\n'
          'Source: PokeAPI public data (https://github.com/PokeAPI/pokeapi, data/v2/csv/pokemon_moves.csv), version group %s\n'
          '(level-up and machine methods). Numbers only: move numbers are the national move numbers.\n'
          'LEVELUP: national number -> [(level, move)] in the game\'s order; MACHINES: national number (1..493) -> [moves learnt by TM / HM]."""\n\n'
          % pub.learn_vg_name)
    ls += 'LEVELUP = {\n' + ''.join(' %d: %r,\n' % (d, v) for d, v in sorted(lv.items())) + '}\n\n'
    ls += 'MACHINES = {\n' + ''.join(' %d: %r,\n' % (d, v) for d, v in sorted(mc.items())) + '}\n'
    with open(os.path.join(out_dir, 'usum_gen4_learnsets.py'), 'w', encoding='utf8') as f:
        f.write(ls)
    return sp, mv, kr, unknown


tail = '''
_OFFSET = dict(hp=30, atk=31, spe=33, spa=34, spd=35, t1=36, t2=37, catch=38, exp=39)
_OFFSET['def'] = 32
_MOVE_OFFSET = dict(power=15, acc=17, pp=18, chance=19)


class Values:
    def __init__(self, on=True):
        self.on = on      # False: Gen 7 numbers (King's Rock stays: Gen 7 has no such list)

    def held_items(self, dex):
        """(common, rare) wild held items, Generation 4 numbers, or None when the switch is off."""
        return tuple(HELD_ITEMS.get(dex, (0, 0))) if self.on else None

    def apply_personal(self, dex, rec, gen7):
        if not self.on:
            return
        for k, v in SPECIES.get(dex, {}).items():
            rec[_OFFSET[k]] = min(v, 255)

    def levelup(self, dex):
        """[(level, move number)] of Diamond/Pearl, or None when the switch is off."""
        if not self.on:
            return None
        import usum_gen4_learnsets
        return usum_gen4_learnsets.LEVELUP.get(dex)

    def machine_moves(self, dex):
        """Set of move numbers learnt by machine (TM / HM) in Diamond/Pearl, or None when the switch is off."""
        if not self.on:
            return None
        import usum_gen4_learnsets
        return set(usum_gen4_learnsets.MACHINES.get(dex, ()))

    def apply_move(self, name, row):
        for k, v in (MOVES.get(name, {}).items() if self.on else ()):
            row[_MOVE_OFFSET[k]] = v
        fl = MOVE_FLAGS.get(name) if self.on else None
        if fl is not None:                      # flag bits: 1 protect, 2 magic coat, 3 snatch, 4 mirror move
            for bit, v in zip((1, 2, 3, 4), fl):
                row[21] = (row[21] & ~(1 << bit)) | (v << bit)
        if name in KINGS_ROCK:
            row[21] |= 32
        else:
            row[21] &= ~32
'''


def main(argv):
    if not argv or argv[0].startswith('-'):
        raise SystemExit(__doc__)
    cache = argv[0]
    vg = argv[argv.index('--vg') + 1] if '--vg' in argv else LEARN_VG
    out = argv[argv.index('--out') + 1] if '--out' in argv else HERE
    pub = Public(cache, learn_vg=vg)
    r = write_modules(pub, out, pub.names())
    print("species %d, moves %d, King's Rock moves %d; written to %s" % (len(r[0]), len(r[1]), len(r[2]), out))


if __name__ == '__main__':
    main(sys.argv[1:])
