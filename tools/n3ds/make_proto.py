"""Make the prototype layers: python -I make_proto.py --rom <3DS dump (decrypted .cci)> --game usum|x --out <folder>
        --xd-disc <XD iso> [--mode native|xd] [--species machamp,arcanine,charizard]

Writes <folder>/mod.json and <folder>/fsys/pkx_<name>.fsys/#0 (the layout of the extra-assets layer "pbr": the game
loads it in place of XD's own model when the setting "Battle Revolution models" is on). Output is game data made from
the player's own files: keep it under build\\, never commit it."""
import argparse
import struct
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'tools', 'pbr_survey'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'pbr_convert'))

import ncch, garc, gfpack, gf_read, gf_to_xd, bch_read, bch_model, gf1_motion


SPECIES = {'machamp': ('kairiky', 68), 'arcanine': ('windie', 59), 'charizard': ('lizardon', 6),
           'dragonite': ('kairyu', 149), 'bulbasaur': ('fushigidane', 1),
           'gengar': ('gangar', 94), 'venusaur': ('fushigibana', 3), 'gyarados': ('gyarados', 130), 'butterfree': ('butterfree', 12),
           'blastoise': ('kamex', 9)}


def _texture_name_of(bb, strict=False):
    """The first texture name of a model group's first entry `bb` (a model package, whole or only its first bytes). strict: the bytes may
    be cut short, so anything that reads past them (or stops inside a name) is an error instead of a wrong answer."""
    sub = gfpack.pc_split(bb)[0]
    x = gf_read.R(sub)
    x.u32(); x.u32(); x.pad16(); x.section()
    tex = ''
    for t in range(4):
        c = x.u32()
        names = [(x.u32(), x.pstr(0x40))[1] for _ in range(c)]
        if t == 1:
            tex = names[0] if names else ''
    if strict and x.p > len(sub):
        raise ValueError('cut short')
    return tex


_TEX_CACHE = {}


def model_texture_name(g, k):
    """The first texture name of model group k, or None when the group is too small to be a model (what find_group / scan_groups read of
    every group). Only the first bytes of the compressed entry are decompressed (the names are in the first few KB; a whole model is
    ~100 times that and was decompressed one byte at a time, ~0.2 s each, hundreds of groups for every species): the answer is the same.
    Kept for the next call in this process."""
    key = (id(g), k)
    if key in _TEX_CACHE:
        return _TEX_CACHE[key]
    raw = g.read(1 + 9 * k)
    res = False
    if raw[:1] == b'':
        size = raw[1] | raw[2] << 8 | raw[3] << 16
        if size == 0:
            size = int.from_bytes(raw[4:8], 'little')
        if size < 1000:
            res = None
        else:
            for lim in (4096, 32768):
                if lim >= size:
                    break
                try:
                    res = _texture_name_of(garc.lz11(raw, lim), strict=True)
                    break
                except Exception:
                    res = False
    if res is False:
        bb = garc.maybe(raw)
        res = None if len(bb) < 1000 else _texture_name_of(bb)
    _TEX_CACHE[key] = res
    return res


def find_group(g, pm, form='00'):
    """(group number) of the 3DS model pmNNNN_FF: read the model's first texture name of each group, scanning from
    a guess (groups follow the national numbers closely)."""
    mp = os.environ.get('N3DS_MAP')
    if mp and os.path.exists(mp):
        m = json.load(open(mp))
        for k, v in sorted(m.items(), key=lambda kv: int(kv[0])):
            if v.get('pm') == '%04d' % pm and v.get('form') == form:
                return int(k)
        for k, v in sorted(m.items(), key=lambda kv: int(kv[0])):
            if v.get('pm') == '%04d' % pm:
                return int(k)
    want = 'pm%04d_%s' % (pm, form)
    n = (len(g.entries) - 1) // 9
    k0 = min(pm, n - 1)
    for d in range(0, n):
        for k in (k0 + d, k0 - d):
            if 0 <= k < n:
                tex = model_texture_name(g, k)
                if tex is not None and tex.startswith(want):
                    return k
    raise SystemExit('model %s not found' % want)


def load_species(r, pm, group=None):
    import gamedetect
    g = gamedetect.models_garc(r)
    if group is None:
        group = find_group(g, pm)
    base = 1 + 9 * group
    gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(base)))[0])
    texs = {}
    for t in gfpack.pc_split(garc.maybe(g.read(base + 1))):
        T = gf_read.parse_texture(t)
        texs[T['name']] = gf_read.decode_texture(T)
    subs = gfpack.pc_split(garc.maybe(g.read(base + 4)))
    mots = {}
    for role, slot in gf_to_xd.GF_SLOT.items():
        if slot < len(subs) and subs[slot]:
            mo = gf_read.parse_motion(subs[slot])
            if mo and mo['bones']:
                mots[role] = mo
    for slot, sb in enumerate(subs):
        if slot in gf_to_xd.GF_SLOT.values() or not sb:
            continue
        try:
            mo = gf_read.parse_motion(sb)
        except Exception:
            continue
        if mo and not mo['bones'] and mo['loop'] and any(c['tx'] or c['ty'] for c in mo['mats'] if 'Fire' in c['name']):
            mots['_fx'] = mo
            break
    if 'idle' not in mots:
        raise SystemExit('no idle motion')

    for slot in (1, 6, 3, 5, 9, 12):
        if slot < len(subs) and subs[slot]:
            try:
                mo = gf_read.parse_motion(subs[slot])
            except Exception:
                mo = None
            if mo and any(mm['unit'] == 0 and mm['ty'] and 'Eye' in mm['name'] for mm in mo['mats']):
                mots['_blink'] = mo
                break

    for role in ('hit', 'faint', 'phys', 'special'):
        mots.setdefault(role, mots.get('phys') or mots.get('special') or mots['idle'])
    return gm, texs, mots, group



def pack_split(d):
    n = struct.unpack_from('<H', d, 2)[0]
    offs = struct.unpack_from('<%dI' % (n + 1), d, 4)
    return [d[offs[i]:offs[i + 1]] for i in range(n)]


X_ROLE_NAME = {'idle': 'ba10_waitA', 'phys': 'ba20_buturi', 'special': 'ba21_tokusyu', 'hit': 'ba30_damageS',
               'faint': 'ba41_down'}


def find_group_x(g, pm, form='00'):
    mp = os.environ.get('N3DS_MAP')
    m = json.load(open(mp))
    for k, v in sorted(m.items(), key=lambda kv: int(kv[0])):
        if v.get('pm') == '%04d' % pm and v.get('form') == form:
            return int(k)
    raise SystemExit('model pm%04d_%s not found in the map' % (pm, form))


def load_species_x(r, pm, group=None):
    """Pokemon X / Y: model BCH (entry 4 + 8k), textures (+2), battle animations (+5, a pack with the motions and
    the named visibility animations)."""
    o, s = r.files['a/0/0/7']
    g = garc.Garc(r.f, r.l3 + r.fdo + o, s)
    if group is None:
        group = find_group_x(g, pm)
    e = 4 + 8 * group
    gm = bch_model.parse_model(bch_read.Bch(pack_split(garc.maybe(g.read(e)))[0]))
    for mt in gm['materials']:
        if 'Iris' in mt['name']:
            mt['shader'] = 'Tra'
    texs = bch_model.parse_textures(bch_read.Bch(pack_split(garc.maybe(g.read(e + 2)))[0]))
    pb = pack_split(garc.maybe(g.read(e + 5)))
    motions, skel = gf1_motion.parse_pack(pb[0])
    by_name = {}
    for k in range(2, len(pb)):
        if pb[k][:4] == b'BCH\0':
            bb = bch_read.Bch(pb[k])
            vp, cnt, nm = bb.contents_dicts()[10]
            if cnt:
                by_name[nm[0].split('_', 2)[2]] = (k, bb)
    mots = {}
    for role, nmx in X_ROLE_NAME.items():
        for full, (k, bb) in by_name.items():
            if full.startswith(nmx):
                mo = motions.get(k - 1)
                if mo is not None:
                    mo = dict(mo)
                    mo['vis'] = bch_model.parse_vis_anim(bb)
                    mo['name'] = full
                    mots[role] = mo
                break
    if 'idle' not in mots:
        raise SystemExit('no idle motion')
    for role in ('hit', 'faint', 'phys', 'special'):
        mots.setdefault(role, mots.get('phys') or mots.get('special') or mots['idle'])
    return gm, texs, mots, group


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--game', default='usum')
    ap.add_argument('--out', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--mode', default='native')
    ap.add_argument('--species', default='machamp,arcanine,charizard')
    ap.add_argument('--flip', action='store_true')
    ap.add_argument('--yaw', type=float, default=0.0)
    ap.add_argument('--improve', action='store_true', help='role retime (mode role), grounding, slimming, flame tint, light tweak')
    ap.add_argument('--decimate', type=int, default=2600)
    a = ap.parse_args()
    import pbr_models as PM
    import noddisc
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, 'GXXE01', 'XD')
    r, prod = ncch.open_rom(a.rom)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"id": "battle-revolution-models", "name": "3DS prototype models", "version": "1.2.0", "author": "prototype",
               "description": "prototype", "game": "GXXE01", "generated_from": "RPBE01", "applies": "restart", "format": 1},
              open(os.path.join(a.out, 'mod.json'), 'w'), indent=2)
    for sp in a.species.split(','):
        arch, pm = SPECIES[sp]
        data = d.file('pkx_%s.fsys' % arch)
        ent = [e for e in noddisc.fsys_entries(data) if e.type == 15][0]
        xd_bytes = noddisc.extract(data, ent)
        gm, texs, mots, group = (load_species_x if a.game == 'x' else load_species)(r, pm)
        print(sp, 'group', group, 'roles', {k: v['frames'] for k, v in mots.items()})
        out, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role' if a.improve else a.mode, flip=a.flip, yaw=a.yaw, log=lambda s: print('  ' + s),
                                     opts=dict(ground=True, decimate=a.decimate, fire=True, light=1.0) if a.improve else None)
        dst = os.path.join(a.out, 'fsys', 'pkx_%s.fsys' % arch)
        os.makedirs(dst, exist_ok=True)
        open(os.path.join(dst, '#%d' % ent.index), 'wb').write(out)


if __name__ == '__main__':
    main()
