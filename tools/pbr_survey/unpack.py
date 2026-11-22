"""Unpack members of FSYS archives (read only; output is game data: only under build/).

Usage: python tools/pbr_survey/unpack.py <out folder> <archive.fsys>[:index,index] ...
Writes <out>/<archive name>_<index>_t<type>_<id>.bin for each member (all members without :indices).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import noddisc

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for arg in sys.argv[2:]:
    path, _, idx = arg.rpartition(':') if (':' in arg[2:]) else (arg, '', '')
    if not path:
        path, idx = arg, ''
    want = set(int(x) for x in idx.split(',') if x) if idx else None
    data = open(path, 'rb').read()
    base = os.path.splitext(os.path.basename(path))[0]
    for e in noddisc.fsys_entries(data):
        if want is not None and e.index not in want:
            continue
        dst = os.path.join(out, '%s_%d_t%d_%04X.bin' % (base, e.index, e.type, e.id))
        if os.path.exists(dst) and os.path.getsize(dst) == e.size:
            continue
        body = noddisc.extract(data, e)
        open(dst, 'wb').write(body)
        print(dst, len(body))
