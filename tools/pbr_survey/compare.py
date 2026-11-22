"""Compare the two roster files made by roster.py (read only).

Usage: python tools/pbr_survey/compare.py <roster_pbr.tsv> <roster_xd.tsv>
Models are matched by name (the model name inside the PBR file against XD's PKX member name).
Prints: totals, how PBR's models split into "same mesh as XD", "changed", "not in XD"; size distributions;
animation name counts; how PBR's animation lists line up with XD's (same frame counts in the same order).
"""
import collections, statistics, sys


def q(v):
    v = sorted(v)
    return 'min %d, median %d, 90%% %d, max %d' % (v[0], statistics.median(v), v[int(len(v) * 0.9)], v[-1])


pbr = [l.rstrip('\n').split('\t') for l in open(sys.argv[1])]
xd = {a[1]: a for a in (l.rstrip('\n').split('\t') for l in open(sys.argv[2])) if 'ERROR' not in a[2]}
bad = [a for a in pbr if a[2].startswith('ERROR')]
pbr = [a for a in pbr if not a[2].startswith('ERROR')]
print('PBR archives read %d (errors %d: %s); XD models %d' % (len(pbr), len(bad), [a[0] for a in bad][:8], len(xd)))
print('members per PBR archive:', sorted(collections.Counter(int(a[1]) for a in pbr).items()))
names = collections.Counter()
for a in pbr:
    for x in a[11].split():
        names[x.rsplit(':', 1)[0]] += 1
print('animation names over all PBR models (name x models):')
print('  ' + ', '.join('%s x%d' % kv for kv in names.most_common()))
print('animations per PBR model:', q([len(a[11].split()) for a in pbr]))
print('PBR vertices:', q([int(a[5]) for a in pbr]), '| triangles:', q([int(a[6]) for a in pbr]))
print('PBR nodes:', q([int(a[3]) for a in pbr]), '| skin joints (kind 2):', q([int(a[4]) for a in pbr]))
print('PBR texture bytes per model:', q([int(a[10]) for a in pbr]), '| unpacked file bytes:', q([int(a[12]) for a in pbr]))
print('PBR largest texture:', collections.Counter(a[9].split()[0] for a in pbr if a[9]).most_common(8))
xs = [a for a in xd.values() if int(a[2]) > 0]
print('XD Pokemon models (id > 0) %d: vertices %s | triangles %s' % (len(xs), q([int(a[4]) for a in xs]), q([int(a[5]) for a in xs])))
print('XD joints:', q([int(a[3]) for a in xs]), '| texture bytes:', q([int(a[9]) for a in xs]), '| file bytes:', q([int(a[11]) for a in xs]))
print('XD largest texture:', collections.Counter(a[8].split()[0] for a in xs if a[8]).most_common(8))
print('XD most skinning matrices in one polygon object:', collections.Counter(int(a[12]) for a in xs).most_common(4),
      '| most weights:', collections.Counter(int(a[13]) for a in xs).most_common(5))
same = changed = absent = 0
anim_same = anim_prefix = 0
tex_same = 0
rows = []
for a in pbr:
    x = xd.get(a[2])
    if not x:
        absent += 1
        rows.append((a[0], a[2], 'not in XD', a[5], a[6], '', ''))
        continue
    pv, pt, xv, xt = int(a[5]), int(a[6]), int(x[4]), int(x[5])
    close = abs(pv - xv) <= max(8, xv * 0.03)
    same += close
    changed += not close
    pf = [int(s.rsplit(':', 1)[1]) for s in a[11].split() if not s.startswith('run:')]
    xf = [int(float(s)) for s in x[10].split()]
    if pf == xf:
        anim_same += 1
    elif sorted(pf) == sorted(xf) or set(xf) <= set(pf):
        anim_prefix += 1
    tex_same += int(a[10]) == int(x[9])
    rows.append((a[0], a[2], 'same mesh' if close else 'changed', pv, pt, xv, xt))
print('matched by name: %d; vertex count within 3%% of XD: %d; different: %d; no XD model of that name: %d' % (
    same + changed, same, changed, absent))
print('of the matched: animation frame lists identical in order (ignoring PBR "run"): %d; same set / XD set contained: %d'
      % (anim_same, anim_prefix))
print('of the matched: same total texture bytes as XD: %d' % tex_same)
ch = [r for r in rows if r[2] == 'changed']
if ch:
    print('changed models: PBR vertices %s | XD vertices of the same %s' % (q([r[3] for r in ch]), q([r[5] for r in ch])))
    print('  examples:', ', '.join('%s %d/%d' % (r[1], r[5], r[3]) for r in ch[:12]))
ab = [r for r in rows if r[2] == 'not in XD']
print('not in XD by archive number:', ' '.join(r[0][4:7] for r in ab)[:1500])
