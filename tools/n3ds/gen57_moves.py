"""The Gen 5-7 moves as rows and battle effects for the Gen 5-7 pack (docs/GEN57_MOVES.md).

  rows, battle, report = build_all(u)        u = gen57_data.Usum(<the player's own Ultra Sun dump>)

What it makes for each of the 261 new moves (USUM ids 468..728; XD row = id + 20, as for the Gen 4 pack):
  - a move ROW (the "add" format of docs/modding): type, power, accuracy, PP, priority, category (physical / special), the
    contact / protect / magic coat / snatch / mirror move / sound flags, the target kind, the effect chance, an effect donor (an XD move
    whose own battle script is reused: multi hits, high critical rate...), an ANIMATION (the closest existing XD move per piece) and the names and
    descriptions in every language (read from the dump by the caller);
  - a BATTLE entry (battle/moves.json of the mod-battle system, docs/modding/battle-effects.md): the effect blocks that say what the
    move does besides the hit (stat changes, status, heal, recoil, drain, switching...), worked out from the move's own record
    in the dump, plus the hand-written ones of gen57_move_plan.py for the moves whose effect is not just data;
  - a REPORT line: how far the move is built ("full", "partial", "damage only", "name only") and what is missing.
Nothing here is game data of its own: the numbers come from the player's dump when the pack is made.
"""
import re

import gen57_data as D
import gen57_move_plan as P

ROW_SHIFT = 20
FIRST, LAST = 468, 728
STAT = {1: 'atk', 2: 'def', 3: 'spa', 4: 'spd', 5: 'spe', 6: 'acc', 7: 'eva'}
INFLICT = {1: 'paralysis', 2: 'sleep', 3: 'freeze', 4: 'burn', 5: 'poison', 6: 'confusion'}






RANGE = {0: 0, 1: 5, 2: 5, 3: 0, 4: 6, 5: 4, 6: 5, 7: 5, 8: 5, 9: 3, 10: 5, 11: 7, 12: 5, 13: 1}


ANIM_PHYS = {0: 'Body Slam', 1: 'Mega Kick', 2: 'Wing Attack', 3: 'Poison Tail', 4: 'Bonemerang', 5: 'Rock Throw', 6: 'Fury Cutter',
             7: 'Shadow Punch', 8: 'Metal Claw', 9: 'Fire Punch', 10: 'Waterfall', 11: 'Vine Whip', 12: 'Thunderpunch', 13: 'Extrasensory',
             14: 'Ice Punch', 15: 'Dragon Claw', 16: 'Crunch', 17: 'Body Slam'}
ANIM_SPEC = {0: 'Hyper Voice', 1: 'Aerial Ace', 2: 'Gust', 3: 'Sludge Bomb', 4: 'Mud Shot', 5: 'Ancientpower', 6: 'Signal Beam',
             7: 'Shadow Ball', 8: 'Mist Ball', 9: 'Flamethrower', 10: 'Water Pulse', 11: 'Magical Leaf', 12: 'Thunderbolt', 13: 'Psychic',
             14: 'Ice Beam', 15: 'Dragonbreath', 16: 'Faint Attack', 17: 'Magical Leaf'}
ANIM_STATUS_SELF_UP = 'Swords Dance'
ANIM_STATUS_FOE_DOWN = 'Screech'
ANIM_STATUS_NONE = 'Splash'


def upper(s):
    s = re.sub(r'\{var\}', '', s).replace('’', "'").replace('�', "'")
    return ' '.join(s.split()).upper() if s and ord(s[0]) < 0x2E80 else s


def desc(s):
    return re.sub(r'\{var\}', '', s).replace('’', "'").replace('�', "'").replace('\n', '{00}')


def sig_groups(u):
    g = {}
    for i in range(1, 355):
        g.setdefault(u.move(i).signature(), []).append(i)
    return g


def pick_donor(m, group, cache):
    """The XD move with the same effect data (signature) and the most alike: the same effect chance and flags."""
    def score(i):
        x = cache[str(i)]
        s = 0
        if x['addFightKoukaAvg'] == m.inflict_percent or x['addFightKoukaAvg'] == (m.flinch or 0) or any(p and x['addFightKoukaAvg'] == p for _, _, p in m.stats):
            s += 4
        if x['dageki'] == (1 if m.contact() else 0):
            s += 2
        if D.TYPE_XD.get(m.type) == x['zokuseiDataId']:
            s += 1
        if (x['pad37'] or 0) == 0 and m.category == (1 if x['zokuseiDataId'] in range(0, 9) else 2):
            s += 0.5
        return s - i / 10000.0
    return max(group, key=score)


def derive_blocks(m, name):
    """The effect blocks a move's own record implies (stat changes, status, heal, drain, recoil, flinch), and notes."""
    blocks, notes = [], []
    status = m.power == 0
    for stat, stages, pct in m.stats:
        if stat == 0 or stages == 0:
            continue
        if stat not in STAT:
            notes.append('stat group %d is not data-driven' % stat)
            continue
        user = (m.quality in (7,) or (status and m.target in (6, 7))) and not (m.quality == 6)
        b = {'stat': STAT[stat], 'stages': stages, 'target': 'user' if user else 'target'}
        if 0 < pct < 100:
            b['chance'] = pct
        blocks.append(b)
    if m.inflict in INFLICT and m.quality in (1, 4, 5):
        b = {'status': INFLICT[m.inflict], 'target': 'target'}
        if 0 < m.inflict_percent < 100:
            b['chance'] = m.inflict_percent
        blocks.append(b)
    if m.flinch:
        b = {'status': 'flinch', 'target': 'target'}
        if m.flinch < 100:
            b['chance'] = m.flinch
        blocks.append(b)
    if m.heal > 0 and status:
        blocks.append({'heal': m.heal, 'target': 'user' if m.target in (6, 7) else 'target'})
    if m.recoil < 0:
        blocks.append({'recoil': -m.recoil})
    elif m.recoil > 0 and not status:
        blocks.append({'drain': m.recoil})
    return blocks, notes


def anim_for(name, m, cache, status_kind):
    if name in P.ANIM:
        return P.ANIM[name]
    if m.power == 0:
        return status_kind
    t = D.TYPE_XD.get(m.type)
    tt = m.type if m.type != 17 else 17
    table = ANIM_PHYS if m.category == 1 else ANIM_SPEC
    return table.get(tt, table[0])


def flags_of(m):
    f = m.flags
    return dict(dageki=int(bool(f & 1)), mamoru=int(bool(f & 8)), majikku=int(bool(f & 0x10)), yokodori=int(bool(f & 0x20)),
                oumugaesi=int(bool(f & 0x40)), bouon=int(bool(f & 0x100)),
                oujanosirusi=int(m.power > 0 and m.flinch == 0))


def build_all(u, cache, langs=('en',)):
    """Returns (rows, battle, report). rows[lang] = {english name: row dict}; battle = {english name: battle entry}."""
    groups = sig_groups(u)
    rows = {l: {} for l in langs}
    battle = {}
    report = []
    for mid in range(FIRST, LAST + 1):
        m = u.move(mid)
        name = u.move_name(mid, 'en')
        name = re.sub(r'[^\x20-\x7e]', "'", name)



        key, again = name, 2
        while key in rows[langs[0]]:
            key = '%s (%d)' % (name, again)
            again += 1
        plan = P.PLAN.get(name, {})
        zmove = P.is_z(name, mid)
        blocks, notes = derive_blocks(m, name)
        donor = None
        status = 'full'
        entry = {}

        sig = m.signature()
        if 'donor' in plan:
            donor = plan['donor']
        elif sig in groups and m.power > 0 and not plan.get('blocks') and not zmove and m.recoil >= 0:
            donor = cache[str(pick_donor(m, groups[sig], cache))]['_name']
            blocks = []
        elif m.power > 0 and m.hits in (0x52, 82):
            donor = 'Fury Swipes'
            blocks = []
        elif m.power > 0 and m.hits in (0x22, 34):
            donor = 'Double Kick'
            blocks = []
        elif m.power > 0:
            donor = 'Pound'
        else:
            donor = plan.get('donor_status', P.DONOR_STATUS_DEFAULT)
        if 'blocks' in plan:
            blocks = plan['blocks']
        if plan.get('no_blocks'):
            blocks = []

        need = set(plan.get('needs', []))
        missing = sorted(need - P.FEATURES_DONE)
        if missing:
            status = 'partial' if m.power > 0 else 'name only'
            if m.power > 0 and not blocks:
                status = 'damage only'
            notes.append('missing: ' + ', '.join(missing))
            blocks = [b for b in blocks if not (set(b.get('needs', [])) - P.FEATURES_DONE)]
        blocks = [{k: v for k, v in b.items() if k != 'needs'} for b in blocks]
        if zmove:
            status = 'damage only'
            notes.append('Z-move: not usable in this game (no Z-crystals); the row exists so move numbers stay in step')
        if notes and not zmove and plan.get('note'):
            notes.append(plan['note'])
        elif plan.get('note'):
            notes.append(plan['note'])
        if m.power == 0 and not blocks and not plan.get('own_effect') and 'donor' not in plan:
            status = 'name only'
        if name in P.FORCE_PARTIAL and status == 'full':
            status = 'partial'
            notes.append(P.FORCE_PARTIAL[name])
        if status == 'full' and plan.get('own_effect') and plan.get('note') and not zmove:
            status = 'partial'
        if name in P.PARTIAL and status in ('full', 'damage only', 'partial', 'name only') and not zmove:
            status = 'partial'
            notes[:] = [n for n in notes if not n.startswith('effect ')] + [P.PARTIAL[name]]
        elif name in P.OK_PURE_EXTRA and not zmove and status == 'damage only':
            status = 'full'
            notes[:] = [n for n in notes if not n.startswith('effect ')]
        matched = sig in groups and m.power > 0 and not plan.get('blocks') and m.recoil >= 0
        if status == 'full' and not zmove and not matched and not plan.get('ok') and name not in P.OK_PURE and name not in P.OK_PURE_EXTRA and m.effect_seq >= 277 and not (plan.get('needs') and not missing):
            status = 'partial' if blocks else ('damage only' if m.power > 0 else 'name only')
            notes.append('effect %d of the dump is not built' % m.effect_seq)
        if blocks or plan.get('power_rules') or plan.get('accuracy_rules') or plan.get('props'):
            e = {}
            if blocks:
                e['effects'] = blocks
            pr = [{k: v for k, v in r.items() if k != 'needs'} for r in plan.get('power_rules', []) if not (set(r.get('needs', [])) - P.FEATURES_DONE)]
            if pr:
                e['power_rules'] = pr
            acc = plan.get('accuracy_rules')
            if m.accuracy == 101 and m.power > 0 and not acc and not zmove and False:
                acc = [{'accuracy': 255}]
            if acc:
                e['accuracy_rules'] = acc
            for k, v in (plan.get('props') or {}).items():
                if k in P.FEATURE_OF_PROP and P.FEATURE_OF_PROP[k] not in P.FEATURES_DONE:
                    status = 'partial' if status == 'full' else status
                    notes.append('missing: ' + P.FEATURE_OF_PROP[k])
                    continue
                e[k] = v
            if e:
                battle[key] = e
        for l in langs:
            rows[l][key] = row_json(u, m, mid, name, l, donor, plan, cache, zmove)
        report.append(dict(id=mid, row=mid + ROW_SHIFT, name=key, type=m.type, power=m.power, status=status, donor=donor, effects=len(blocks), notes=notes))
    return rows, battle, report


def sig_unmatched_secondary(m, blocks, donor, plan):
    return False


def row_json(u, m, mid, name, lang, donor, plan, cache, zmove):
    q = D.TYPE_XD.get(m.type)
    d = {'copy': donor, 'pp': m.pp, 'zokuseiDataId': q if q is not None else 0, 'iryoku': m.power, 'avg': (0 if (m.power > 0 or m.target in (6, 7, 8, 10, 11, 12)) else 100) if m.accuracy in (0, 101) else m.accuracy,
        
         'pri': m.priority & 0xFF, 'pad37': {0: 0, 1: 1, 2: 2}[m.category], 'rangeId': RANGE.get(m.target, 0)}
    d.update(flags_of(m))
    chance = m.inflict_percent or m.flinch or max([p for _, _, p in m.stats] + [0])
    d['addFightKoukaAvg'] = chance if chance and m.power > 0 else 0
    if m.type == 17:
        d['_fairy'] = True
    if zmove:
        d['yubiwohuru'] = 0
    kind = ANIM_STATUS_SELF_UP if any(s > 0 for _, s, _ in m.stats) else ANIM_STATUS_FOE_DOWN if any(s < 0 for _, s, _ in m.stats) else ANIM_STATUS_NONE
    d['animation'] = anim_for(name, m, cache, plan.get('anim_status', kind))
    d['text'] = {'name': upper(u.move_name(mid, lang)), 'doc': desc(u.move_desc(mid, lang))}
    return d
