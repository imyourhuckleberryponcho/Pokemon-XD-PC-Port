"""The reference set of the whole usum pack models: every Gen 1-3 XD species and every Gen 4 species the Gen 4 plan makes.

  python -I make_refset_xdg4.py <usum.cci> <XD iso> <out folder>

Writes <out>/refset_xdg4.json (for make_refs.py --set; the Python makes the files, about 1 hour of CPU) and <out>/plan_xdg4.tsv (the plan
usum_models_cpp --plan reads: name, group, stand-in .pkx, height, ladder start, archive id, resource id, kind). The stand-in .pkx files
(game data) go to <out>/standin: keep <out> under build\\."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, N3)
import make_refs as M

rom, disc, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(os.path.join(out, 'standin'), exist_ok=True)
M.init(rom, disc, out)
W = M.W
import usum_models as UM
import usum_gen4


def xdfile(nat):
    p = os.path.join(out, 'standin', '%d.pkx' % nat)
    if not os.path.exists(p):
        open(p, 'wb').write(M.xd_bytes_of(nat))
    return p


jobs, lines = [], []
plan = json.load(open(os.path.join(N3, 'usum_gen4_plan.json')))['plan']
us = usum_gen4.Usum(W['u'].r, W['u'].prod)
for nat in sorted(W['xdmap']):
    if nat in W['table']:
        jobs.append(dict(name='xd_%03d' % nat, kind='xd', nat=nat))
        lines.append('\t'.join(map(str, ['xd_%03d' % nat, W['table'][nat], xdfile(nat), 0, 0, 0, 0, 'xd'])))
for k, (model, standin) in sorted(plan.items(), key=lambda kv: int(kv[0])):
    d = int(k)
    if d in W['table'] and standin in W['xdmap']:
        jobs.append(dict(name='g4_%03d' % d, kind='g4', nat=d))
        lines.append('\t'.join(map(str, ['g4_%03d' % d, W['table'][d], xdfile(standin), UM.u16(us.personal(d), 0x24), 0, 0, 0, 'g4'])))
json.dump(dict(jobs=jobs), open(os.path.join(out, 'refset_xdg4.json'), 'w'))
open(os.path.join(out, 'plan_xdg4.tsv'), 'w').write('\n'.join(lines) + '\n')
print(len(jobs), 'models')
