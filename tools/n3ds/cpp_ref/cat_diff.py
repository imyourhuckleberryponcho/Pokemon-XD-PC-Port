import os, hashlib, collections, sys
from PIL import Image
import numpy as np
A, B = sys.argv[1], sys.argv[2]
BS = chr(92)


def tree(r):
    o = {}
    for d, _, fs in os.walk(r):
        for f in fs:
            p = os.path.join(d, f)
            o[os.path.relpath(p, r).replace(BS, '/')] = p
    return o


ta, tb = tree(A), tree(B)


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


cat = collections.Counter()
ex = {}
for k in ta:
    if k not in tb:
        cat['onlyA'] += 1
        continue
    if sha(ta[k]) == sha(tb[k]):
        continue
    if k.endswith(('.json', '.txt')):
        def norm(p):
            b = open(p, 'rb').read().replace(bytes([13]), b'')
            try:
                return b.decode('utf-8')
            except UnicodeDecodeError:
                return b.decode('cp1252')
        if norm(ta[k]) == norm(tb[k]):
            cat['text same apart from line ends / cp1252'] += 1
            continue
    if k.endswith('.png'):
        x = np.asarray(Image.open(ta[k]).convert('RGBA')).astype(int)
        y = np.asarray(Image.open(tb[k]).convert('RGBA')).astype(int)
        if x.shape == y.shape and not np.abs(x - y).any():
            continue
        g = 'png:' + k.split('/')[1]
    else:
        g = k.split('/')[0] + ('/' + k.split('/')[1].split('_')[0] if k.startswith('files/') else '')
    cat[g] += 1
    ex.setdefault(g, []).append(k)
for k in tb:
    if k not in ta:
        cat['onlyB:' + ('.ok' if k.endswith('.ok') else k)] += 1
print(dict(cat))
for g, v in ex.items():
    print(g, v[:3])
