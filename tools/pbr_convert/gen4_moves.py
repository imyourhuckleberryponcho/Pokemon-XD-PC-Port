"""The 113 Gen 4 moves from Pokemon Battle Revolution as rows for Pokemon XD (docs/GEN4.md, "Phase 3: moves").

  python tools/pbr_convert/gen4_moves.py dump <work>   --pbr <PBR disc> --xd <XD disc> [--exe <xd_data_mods.exe>]
  python tools/pbr_convert/gen4_moves.py plan <work>   classification.tsv + summary (which class each move is)
  python tools/pbr_convert/gen4_moves.py make <work>   the mod folder <work>/mods/gen4-moves (rows in the "add" format of
                                                       docs/MODDING.md) + a test mod per move (an existing row rewritten)
  python tools/pbr_convert/gen4_moves.py check <work>  every Gen 1-3 move's row rebuilt from PBR's numbers vs XD's own row

Output is game data (numbers read from the player's own discs): keep <work> under build/, never in the repository.

How a row is made (rules in gen4_move_rules.py): the Gen 4 move's PBR "battle effect" number is looked up among the Gen 1-3
moves (ids 1..354 are the same moves in both games); a Gen 1-3 move with that effect is the DONOR and its XD row is copied
(effect script, extra-effect id, range, AI row). Then the Gen 4 numbers go over it: type, power, accuracy (PBR 0 = never
misses = 100 in XD), PP, priority, effect chance, the six flags (contact, Protect, Magic Coat, Snatch, Mirror Move, King's
Rock), the animation of a similar XD move, and the physical/special category in the byte XD's rows leave unused (pad37: 1 =
physical, 2 = special; games/xd/src/move_split.cpp reads it).
"""
import argparse, collections, concurrent.futures as cf, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import gen4_rules as R
import gen4_move_rules as M
import gen4_text_i18n as TI
import gen4_move_text as MT

TYPE_NAMES = ['Normal', 'Fighting', 'Flying', 'Poison', 'Ground', 'Rock', 'Bug', 'Ghost', 'Steel', '???', 'Fire',
              'Water', 'Grass', 'Electric', 'Psychic', 'Ice', 'Dragon', 'Dark']
CLASS_NAMES = ['physical', 'special', 'status']
FIRST, LAST = 355, 467
FLAG_FIELDS = ['dageki', 'mamoru', 'majikku', 'yokodori', 'oumugaesi', 'oujanosirusi']
DEF_XD = ''
DEF_PBR = ''
DEF_EXE = ''


def norm(s):
    return re.sub(r'[^A-Z0-9]', '', s.upper().replace('\u00e9', 'E'))



def dump(a):
    import gen4_data as G
    os.makedirs(a.work, exist_ok=True)
    pbr = G.PBR(a.pbr)
    prow = {}
    for m in range(1, LAST + 1):
        raw = pbr.common[30][0x40 + m * 24:0x40 + m * 24 + 24]
        prow[m] = dict(name=pbr.move_name(m), hex=raw.hex())
    env = dict(os.environ, XD_SETTINGS_DIR=os.environ.get('XD_SETTINGS_DIR', os.path.join(a.work, 'settings')))
    os.makedirs(env['XD_SETTINGS_DIR'], exist_ok=True)

    def one(i):
        t = subprocess.run([a.exe, a.xd, '--show', 'waza_data', '#%d' % i], capture_output=True, text=True, env=env).stdout
        d = {}
        for ln in t.splitlines()[1:]:
            mm = re.match(r'\s*(\w+)\s+(.*?)\s*$', ln)
            if mm:
                v = re.match(r'^(\[.*\]|-?\d+)', mm.group(2))
                d[mm.group(1)] = json.loads(v.group(1)) if v else mm.group(2)
        nm = re.search(r'\((.*)\)', t.splitlines()[0])
        d['_name'] = nm.group(1) if nm else ''
        return i, d
    with cf.ThreadPoolExecutor(6) as ex:
        xrow = dict(ex.map(one, range(0, 375)))
    json.dump(dict(pbr=prow, xd=xrow), open(os.path.join(a.work, 'moves_cache.json'), 'w'))
    print('dumped %d PBR moves and %d XD rows into %s' % (len(prow), len(xrow), a.work))


class Data:
    def __init__(self, work):
        c = json.load(open(os.path.join(work, 'moves_cache.json')))
        self.xd = {int(k): v for k, v in c['xd'].items()}
        self.by_name = {norm(v['_name']): k for k, v in self.xd.items() if v['_name']}
        self.pbr = {}
        for k, v in c['pbr'].items():
            b = bytes.fromhex(v['hex'])
            self.pbr[int(k)] = dict(name=v['name'], eff=(b[0] << 8) | b[1], cls=b[14], power=b[15], type=b[16], acc=b[17],
                                    pp=b[18], chance=b[19], pri=b[20] - 256 if b[20] > 127 else b[20], flags=b[21])
        self.groups = collections.defaultdict(list)
        for m in range(1, 355):
            self.groups[self.pbr[m]['eff']].append(m)

    def xid(self, name):
        return self.by_name[norm(name)]



def best_donor(d, m):
    """A Gen 1-3 move with the same PBR effect, the most alike: the XD effect script most of them use, the same effect
    chance, the same contact flag, the same class."""
    p = d.pbr[m]
    cands = d.groups[p['eff']]
    modal = collections.Counter(d.xd[c]['seqId'] for c in cands).most_common(1)[0][0]

    def score(c):
        q = d.pbr[c]
        return ((d.xd[c]['seqId'] == modal) * 8 + (q['chance'] == p['chance']) * 4 + ((q['flags'] & 1) == (p['flags'] & 1)) * 2
                + (q['cls'] == p['cls']) - c / 10000.0)
    return max(cands, key=score)


def classify(d, m):
    p = d.pbr[m]
    name = p['name']
    if p['eff'] in d.groups:
        donor = best_donor(d, m)
        return 'a', donor, 'same effect as %s' % d.pbr[donor]['name']
    kind, donor_name, note = M.MOVE_PLAN[name]
    if kind == 'b' and name not in M.IMPLEMENTED:
        if p['cls'] == 2:
            return 'd', None, 'effect not built yet'
        return 'c', d.xid('Pound'), note + ' (not built yet: plain damage)'
    if kind == 'd':
        return 'd', None, 'not offered'
    return kind, d.xid(donor_name), note


def anim_row(d, name, donor):
    """The XD move whose animation the row borrows."""
    sub = {norm(k): v for k, v in R.MOVE_SUBSTITUTE.items()}
    for src in (M.ANIM_DONOR.get(name), sub.get(norm(name))):
        if src:
            return d.xid(src)
    return donor


def make_row(d, m, donor):
    """The XD waza_data fields (names of docs/MODDING.md) of the Gen 4 move m, copying the XD move id `donor`."""
    p, x = d.pbr[m], d.xd[donor]
    name = p['name']
    power = p['power']
    if name in M.FIXED_POWER:
        power = M.FIXED_POWER[name]
    elif x['iryoku'] <= 1 and power <= 1:
        power = x['iryoku']
    elif p['cls'] != 2 and power <= 1:
        power = 60
    f = collections.OrderedDict()
    f['pp'] = p['pp']
    f['zokuseiDataId'] = p['type']
    f['iryoku'] = power
    f['avg'] = p['acc'] if p['acc'] else 100
    f['pri'] = p['pri'] & 0xFF
    if x['addFightKoukaAvg']:
        f['addFightKoukaAvg'] = p['chance'] if p['chance'] else x['addFightKoukaAvg']
    if name in M.CHANCE and 'addFightKoukaAvg' in f:
        f['addFightKoukaAvg'] = M.CHANCE[name]
    for bit, fld in enumerate(FLAG_FIELDS):
        f[fld] = (p['flags'] >> bit) & 1
    if name in M.IGNORES_PROTECT:
        f['mamoru'] = 0
    f['wazawzxdataId'] = d.xd[anim_row(d, name, donor)]['wazawzxdataId']
    f['pad37'] = 1 if p['cls'] == 0 else 2 if p['cls'] == 1 else 0
    f['unk16'] = [0xA4, 1 if name in M.PUNCH else 0]
    if name in M.EFFECT_SLOTS and name in M.IMPLEMENTED:
        f['seqId'] = M.EFFECT_SLOTS[name]
    return f


def plan(d):
    rows = []
    for m in range(FIRST, LAST + 1):
        p = d.pbr[m]
        kind, donor, note = classify(d, m)
        rows.append(dict(id=m, name=p['name'], type=TYPE_NAMES[p['type']], cls=CLASS_NAMES[p['cls']] if p['cls'] < 3 else '?',
                         power=p['power'], acc=p['acc'], pp=p['pp'], pri=p['pri'], chance=p['chance'], eff=p['eff'],
                         kind=kind, donor=donor, donor_name=d.xd[donor]['_name'] if donor else '', note=note))
    return rows


def cmd_plan(a):
    d = Data(a.work)
    rows = plan(d)
    with open(os.path.join(a.work, 'classification.tsv'), 'w', encoding='utf-8') as f:
        f.write('pbr_id\tname\ttype\tclass\tpower\tacc\tpp\tpri\tchance\teffect\tkind\tdonor\tnote\n')
        for r in rows:
            f.write('%d\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n' % (
                r['id'], r['name'], r['type'], r['cls'], r['power'], r['acc'], r['pp'], r['pri'], r['chance'], r['eff'],
                r['kind'], r['donor_name'], r['note']))
    c = collections.Counter(r['kind'] for r in rows)
    print('classification:', dict(sorted(c.items())), '->', os.path.join(a.work, 'classification.tsv'))
    for k, title in (('a', 'a: data only (an existing effect)'), ('b', 'b: new effect, built'), ('c', 'c: approximated'),
                     ('d', 'd: not offered')):
        print('%s  (%d): %s' % (title, c[k], ', '.join(r['name'] for r in rows if r['kind'] == k)))



def description(r):
    parts = ['%s, %s.' % (r['type'], r['cls'])]
    if r['kind'] == 'c':
        parts.append('(' + r['note'].replace('(not built yet: plain damage)', '').strip() + ')')
    return ' '.join(parts)


def cmd_make(a):
    d = Data(a.work)
    every = plan(d)
    rows = [r for r in every if r['kind'] != 'd']
    add = collections.OrderedDict()
    table = collections.OrderedDict()


    for r in every:
        row = collections.OrderedDict()
        if r['kind'] == 'd':
            p = d.pbr[r['id']]
            row['copy'] = d.xd[d.xid('Splash')]['_name']
            row['pp'] = p['pp']
            row['zokuseiDataId'] = p['type']
            row['unk16'] = [0xA4, 0]
            row['text'] = TI.not_built_text(r['name'])
        else:
            row['copy'] = d.xd[r['donor']]['_name']
            row.update(make_row(d, r['id'], r['donor']))
            row['text'] = TI.move_text(r['name'])
        table[r['name']] = row
    add['moves'] = table
    mod = os.path.join(a.work, 'mods', 'gen4-moves')
    os.makedirs(os.path.join(mod, 'data'), exist_ok=True)
    json.dump({'id': 'gen4-moves', 'name': 'Gen 4 moves', 'version': '0.1', 'author': 'generated from your own disc',
               'description': 'The Diamond/Pearl moves (%d of 113) as new moves; see docs/GEN4.md' % len(rows),
               'game': 'GXXE01', 'applies': 'new_playthrough'}, open(os.path.join(mod, 'mod.json'), 'w'), indent=1)
    json.dump({'_note': 'generated by tools/pbr_convert/gen4_moves.py from the PBR disc: game data, keep out of the repository',
               'add': add}, open(os.path.join(mod, 'data', 'moves.json'), 'w'), indent=1)
    print('wrote %d moves (%d built, %d "does nothing") into %s: numbers 375..%d' % (len(every), len(rows), len(every) - len(rows), mod,
                                                                                    374 + len(every)))


    tdir = os.path.join(a.work, 'mods_test')
    os.makedirs(tdir, exist_ok=True)
    carrier = 'Sketch'
    cid = d.xid(carrier)
    for r in rows:
        mf = os.path.join(tdir, re.sub(r'[^a-z0-9]+', '-', r['name'].lower()))
        os.makedirs(os.path.join(mf, 'data'), exist_ok=True)
        fields = make_row(d, r['id'], r['donor'])

        full = {k: v for k, v in d.xd[r['donor']].items() if not k.startswith('_') and k not in ('name', 'doc', 'fightAttackMsgId',
                                                                                              'fightAttackTunagiMsgId')}
        full.update(fields)
        json.dump({'id': os.path.basename(mf), 'name': 'Gen 4 test: ' + r['name'], 'version': '0',
                   'game': 'GXXE01', 'applies': 'new_playthrough'}, open(os.path.join(mf, 'mod.json'), 'w'))
        json.dump({'_note': 'test only: row of %s replaced by %s (donor %s)' % (carrier, r['name'], d.xd[r['donor']]['_name']),
                   'waza_data': {'#%d' % cid: full}}, open(os.path.join(mf, 'data', 'row.json'), 'w'), indent=1)
    print('wrote %d test mods into %s' % (len(rows), tdir))



def cmd_check(a):
    """The rules applied to the Gen 1-3 moves themselves: rebuild XD's own row of each from PBR's numbers (donor = the
    move itself) and compare. Shows how far 'PBR numbers over an XD row' reproduces what XD has."""
    d = Data(a.work)
    diff = collections.Counter()
    shown = 0
    for m in range(1, 355):
        if m not in d.xd or not d.xd[m]['_name']:
            continue
        mk = make_row(d, m, m)
        for k, v in mk.items():
            if k in ('wazawzxdataId', 'seqId', 'pad37', 'unk16'):
                continue
            if d.xd[m][k] != v:
                diff[k] += 1
                if shown < 25 and k in ('zokuseiDataId', 'iryoku', 'pp', 'pri'):
                    shown += 1
                    print('  %s %s: XD %s, rebuilt %s' % (d.xd[m]['_name'], k, d.xd[m][k], v))
    print('differences over 354 moves, by field:', dict(diff))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['dump', 'plan', 'make', 'check'])
    ap.add_argument('work')
    ap.add_argument('--pbr', default=DEF_PBR)
    ap.add_argument('--xd', default=DEF_XD)
    ap.add_argument('--exe', default=DEF_EXE)
    a = ap.parse_args()
    {'dump': dump, 'plan': cmd_plan, 'make': cmd_make, 'check': cmd_check}[a.cmd](a)


if __name__ == '__main__':
    main()
