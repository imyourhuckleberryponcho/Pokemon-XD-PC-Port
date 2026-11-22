"""Audit table of the effect parts of every Gen 1-7 species in the dump (markdown).
  python -I fx_table.py --audit audit_raw.json --xd xd_scan.json --out audit_table.md
audit_raw.json comes from fx_audit.py, xd_scan.json from fx_xd_scan.py (both made from the player's own files)."""
import argparse
import collections
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = open(os.path.join(HERE, 'dexnames.txt')).read().split()


def nm(pm):
    return '%s (#%d)' % (NAMES[pm - 1], pm) if 1 <= pm <= len(NAMES) else '#%d' % pm


def classes(m):
    s, n = m['shader'], m['name']
    c = []
    if 'Fire' in s or 'Fire' in n:
        c.append('fire')
    if 'Inc' in s or any('Inc' in t and 'Dummy' not in t for t in m['tex'][1:]):
        c.append('inc')
    if 'Neon' in s:
        c.append('neon')
    if 'Add' in s:
        c.append('add')
    if 'Env' in s:
        c.append('env')
    if 'Ice' in s:
        c.append('ice')
    if 'Wing' in s:
        c.append('wing')
    if 'Cell' in s:
        c.append('cell')
    if 'Eff' in s or 'Eff' in n:
        c.append('eff')
    if 'Hair' in s:
        c.append('hair')
    if 'Leaf' in s:
        c.append('leaf')
    return c


KINDS = [
    ('fire', 'Fire* (FireSten / FireCore / FireGeom)',
     'flame, gas, smoke, spirit-fire meshes: grey intensity pictures tinted by the shader, slid by a looping material motion (tx / ty keys)',
     'tinted once, still, solid pieces',
     "Gen 1-3 with an XD particle flame: XD's own particle spawn tracks copied to the flame / tail bone, the 3DS flame pieces dropped. "
     "Others: flame pieces kept, see-through, picture slides with the dump's own keys",
     'DONE (generic)'),
    ('inc', 'Inc (incandescence map)', 'glow areas: a second picture the shader adds to the colour (Reshiram, Zekrom, cheeks, tails, lures)',
     'dropped (only the colour picture)', 'glow map added into the colour picture (bake, opts fx)',
     'DONE (generic bake; XD lights vertices, so the glow is brighter paint, not light)'),
    ('neon', 'Neon', 'glowing stripes / spots (Lumineon, Pikachu line) through the Inc map', 'dropped', 'same bake as Inc where a *Inc map exists', 'DONE with Inc'),
    ('add', 'Add (additive blend)', 'translucent glowing bodies: gas, jewels, sheen, ghosts', 'drawn as normal opaque colour',
     'not done: needs an additive blend mode per material (XD materials have render flags for it)', 'OPEN: 1 session'),
    ('env', 'Env (environment / metal reflection map)', 'metal shine: Magnemite line, Voltorb, Steelix, Dialga ...', 'drawn flat',
     'not done: the Battle Revolution stage already puts reflection layers on XD materials (sphere map); same trick usable', 'OPEN: 1 session'),
    ('ice', 'Ice', 'ice crystal bodies', 'drawn opaque', 'not done: translucent look', 'OPEN: 0.5 session'),
    ('wing', 'Wing', 'translucent / veined wings', 'wing picture opaque', 'not done: alpha from the mask map', 'OPEN: 0.5 session'),
    ('cell', 'Cell', 'patterned (cell) bodies', 'drawn plain', 'no change needed', 'OK as is'),
    ('eff', 'Eff / EffA', 'effect planes (aura, rings)', 'dropped or opaque', 'not done: per species check', 'OPEN: per species'),
    ('hair', 'Hair', 'hair / fur strands, special lighting', 'drawn plain', 'no change needed (colour picture carries it)', 'OK as is'),
    ('leaf', 'Leaf', 'leaf material with back-light', 'drawn plain', 'no change needed', 'OK as is'),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--audit', required=True)
    ap.add_argument('--xd', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    rows = [r for r in json.load(open(a.audit)) if 'mats' in r and r['pm'] <= 807]
    xd = json.load(open(a.xd))
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in rows:
        for m in r['mats']:
            for c in classes(m):
                by[c][r['pm']].append((r['form'], m['name'], m['shader']))
    L = []
    w = L.append
    w('# Effect parts audit: Pokemon Ultra Sun / Moon, Gen 1-7 (807 species, %d model groups incl. forms)\n' % len(rows))
    w('Made by `tools/n3ds/fx_audit.py` (material shaders, texture units, motion channels of every model group in the dump) and')
    w("`tools/n3ds/fx_xd_scan.py` (what XD itself does for the Gen 1-3 species). \"Now\" = what the converter did before this work")
    w('(first picture only, flame tinted, no animation); "After" = with the usum-fx stage (`opts fx`).\n')
    w('## Kinds of effect part found\n')
    w('| kind | shader marker | species (forms) | what USUM does | what we did | fix | status |')
    w('|---|---|---|---|---|---|---|')
    for c, marker, does, now, fix, status in KINDS:
        sp = sorted(by[c])
        w('| %s | %s | %d (%d) | %s | %s | %s | %s |' % (c, marker, len(sp), sum(len(v) for v in by[c].values()), does, now, fix, status))
    w('')
    w('## Flame / gas / fire parts, species by species\n')
    w('| species | parts (materials) | USUM does | XD has | now | after |')
    w('|---|---|---|---|---|---|')
    for pm in sorted(by['fire']):
        parts = sorted({m for _f, m, _s in by['fire'][pm]})
        forms = sorted({f for f, _m, _s in by['fire'][pm]})
        x = xd.get(str(pm))
        if x and x.get('ptcl_joints'):
            xdtxt = 'particle effect on %d joint(s)' % len(x['ptcl_joints'])
            after = "XD's particle effect copied to the matching bone; 3DS flame pieces dropped"
        elif x:
            xdtxt = 'model only (no particle spawn)'
            after = '3DS flame pieces kept: see-through + sliding picture'
        else:
            xdtxt = 'no XD model (Gen 4+: Gen 4 pack / new-gen pack)'
            after = '3DS flame pieces kept: see-through + sliding picture'
        extra = '' if forms == ['00'] else ' +forms %s' % ','.join(f for f in forms if f != '00')
        w('| %s%s | %s | flame meshes, picture slid by a looping motion | %s | still tinted solid pieces | %s |' % (
            nm(pm), extra, ', '.join(parts[:6]) + (' ...' if len(parts) > 6 else ''), xdtxt, after))
    for c, title in (('inc', 'Glow maps (Inc)'), ('neon', 'Neon'), ('add', 'Additive'), ('env', 'Environment / metal'), ('ice', 'Ice'),
                     ('wing', 'Wing'), ('eff', 'Effect planes'), ('cell', 'Cell'), ('hair', 'Hair'), ('leaf', 'Leaf')):
        w('\n## %s: species list\n' % title)
        w(', '.join(nm(pm) + ('*' if any(f != '00' for f, _m, _s in by[c][pm]) else '') for pm in sorted(by[c])))
        w('\n(* = also on an alternate form)')
    w('\n## Flame species without an XD particle (use the generic slide)\n')
    nof = [pm for pm in sorted(by['fire']) if not (xd.get(str(pm)) or {}).get('ptcl_joints')]
    w(', '.join(nm(pm) for pm in nof))
    w('\n## XD species that spawn particles themselves (copied by the stage)\n')
    for k, v in sorted(xd.items(), key=lambda kv: int(kv[0])):
        if v.get('ptcl_joints'):
            w('- %s: joints %s' % (nm(int(k)), ', '.join(v['ptcl_joints'])))
    open(a.out, 'w').write('\n'.join(L) + '\n')
    print('wrote', a.out)


main()
