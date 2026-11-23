"""Compare the plan usum_models_cpp makes itself (--plan-only --write-plan cplan.tsv) with a plan made from the Python tables.

  python -I compare_plan.py <cplan.tsv> <reference plan.tsv> [<reference plan.tsv> ...]

Per name: the model group, the stand-in species (the number in the file name / "xd:<n>"), the height in cm (not for the Gen 1-3 species, whose
size comes from XD's box) and the size-ladder start must be equal. The reference plans are the ones make_refset_xdg4.py and the round 2
export_plan.py write (columns: name, group, stand-in, height, ladder, archive id, resource id[, kind]). Exit 1 on any difference."""
import re
import sys


def load(p):
    d = {}
    for line in open(p):
        if line.startswith('#') or not line.strip():
            continue
        c = line.rstrip('\n').split('\t')
        d[c[0]] = c
    return d


def standin(x):
    m = re.search(r'(\d+)\.pkx$', x)
    return m.group(1) if m else x.split(':')[1]


cpp = load(sys.argv[1])
bad = 0
for ref in sys.argv[2:]:
    r = load(ref)
    for k, v in r.items():
        if k not in cpp:
            print('missing in the C++ plan:', k)
            bad += 1
            continue
        w = cpp[k]
        a = [v[1], standin(v[2]), str(int(float(v[3]))), v[4]]
        b = [w[1], standin(w[2]), str(int(float(w[3]))), w[4]]
        if len(v) > 7 and v[7] == 'xd':
            a[2] = b[2] = '0'
        if a != b:
            print(ref, k, a, b)
            bad += 1
    print(ref, len(r), 'names compared')
print('differences:', bad)
sys.exit(1 if bad else 0)
