"""Triangle / texture / joint ratios between the roster files of roster.py (read only).

Usage: python tools/pbr_survey/ratios.py <roster_pbr.tsv> <roster_xd.tsv> [archive names to print...]
"""
import statistics, sys

pbr = [l.rstrip('\n').split('\t') for l in open(sys.argv[1])]
xd = {a[1]: a for a in (l.rstrip('\n').split('\t') for l in open(sys.argv[2]))}
r = []
for a in pbr:
    if a[2].startswith('ERROR'):
        print('unread:', a[:3])
        continue
    x = xd.get(a[2])
    if x:
        r.append((int(a[6]) / int(x[5]), a[2], int(x[5]), int(a[6]), int(a[10]), int(x[9]), int(a[4]), int(x[3]), int(a[3])))
b = [0, 0, 0, 0, 0]
for t in r:
    q = t[0]
    b[0 if q < 0.9 else 1 if q <= 1.1 else 2 if q <= 1.5 else 3 if q <= 2.5 else 4] += 1
print('triangles PBR / XD: under 0.9: %d, 0.9 to 1.1: %d, 1.1 to 1.5: %d, 1.5 to 2.5: %d, over 2.5: %d (of %d)' % (*b, len(r)))
print('median triangle ratio %.2f' % statistics.median(t[0] for t in r))
print('median texture byte ratio %.1f; PBR texture bytes not above XD (+5%%): %d' % (
    statistics.median(t[4] / max(t[5], 1) for t in r), sum(1 for t in r if t[4] <= t[5] * 1.05)))
print('triangles within 10%% and textures not bigger: %d' % sum(1 for t in r if 0.9 <= t[0] <= 1.1 and t[4] <= t[5] * 1.05))
print('triangles within 10%% and textures bigger: %d' % sum(1 for t in r if 0.9 <= t[0] <= 1.1 and t[4] > t[5] * 1.05))
print('largest ratios:', ', '.join('%s %d->%d' % (t[1], t[2], t[3]) for t in sorted(r, reverse=True)[:8]))
print('PBR nodes minus XD joints: median %d; equal in %d' % (statistics.median(t[8] - t[7] for t in r), sum(1 for t in r if t[8] == t[7])))
g4 = [a for a in pbr if not a[2].startswith('ERROR') and a[2] not in xd]
print('models with no XD namesake %d: triangles median %d, max %d; nodes max %d; texture bytes median %d' % (
    len(g4), statistics.median(int(a[6]) for a in g4), max(int(a[6]) for a in g4), max(int(a[3]) for a in g4),
    statistics.median(int(a[10]) for a in g4)))
print('most nodes:', max((int(a[3]), a[2]) for a in pbr if not a[2].startswith('ERROR')))
for a in pbr:
    if a[0] in sys.argv[3:] and len(a) > 11:
        print(a[0], 'members', a[1], a[2], 'nodes', a[3], 'verts', a[5], 'tris', a[6], a[9], a[11][:160], '| sizes', a[13][:120] if len(a) > 13 else '')
