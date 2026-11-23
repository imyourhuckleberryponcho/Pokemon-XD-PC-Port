import sys, os, json
WT = ''
for p in (WT + r'\tools\n3ds', WT + r'\tools\pbr_survey', WT + r'\tools\pbr_convert'):
    sys.path.insert(0, p)
import ncch, usum_gen4, usum_gen4_values
ROM = ''
out, cpp = sys.argv[1], sys.argv[2]
r, prod = ncch.open_rom(ROM)
us = usum_gen4.Usum(r, prod)
usum_gen4.write_source(us, out, usum_gen4_values.Values(True), log=lambda s: None)
plan = json.load(open(WT + r'\tools\n3ds\usum_gen4_plan.json'))['plan']
with open(os.path.join(out, 'plan.tsv'), 'w') as f:
    for dex_s, (model, standin) in sorted(plan.items(), key=lambda kv: int(kv[0])):
        if os.path.exists(cpp + '/gen4src/models/%s.pkx' % model):
            f.write('%s\t%s\t%d\n' % (dex_s, model, standin))
usum_gen4.write_extras(us, out, log=lambda s: None)
open(os.path.join(out, 'gen4src.txt'), 'w').write('usum_gen4 source: %s, gen4 values %s\n' % (us.det['name'], 'on'))
