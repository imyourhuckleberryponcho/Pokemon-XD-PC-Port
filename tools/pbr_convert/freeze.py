"""The frozen answer set of the Python model converter (docs/PBR_CONVERTER_FROZEN.md).

  python tools/pbr_convert/freeze.py make  <work388> <workgen4> <frozen folder>
        copies every converted model of both runs (<work>/out/*.pkx: batch.py's 388 XD counterparts and gen4.py's 106
        Gen 4 models) into <frozen>/xd/ and <frozen>/gen4/ and writes <frozen>/manifest.tsv (set, file, bytes,
        SHA-256) and <frozen>/VERSION.txt (CONVERTER_VERSION).
  python tools/pbr_convert/freeze.py check <work388> <workgen4> <frozen folder>
        compares the runs' outputs with the manifest: every file's SHA-256, missing and extra files; exit 1 on any
        difference. This is the test a C++ port of the converter must pass (its output folders in place of the work
        folders' out/).

Everything in the frozen folder is game data made from the player's discs: it lives under build/, never in the repository.
"""
import hashlib
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def sets(w388, wgen4):
    return (('xd', os.path.join(w388, 'out')), ('gen4', os.path.join(wgen4, 'out')))


def make(w388, wgen4, frozen):
    import pbr_models
    rows = []
    for name, src in sets(w388, wgen4):
        dst = os.path.join(frozen, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        os.makedirs(dst)
        for f in sorted(os.listdir(src)):
            if f.endswith('.pkx'):
                shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
                rows.append((name, f, os.path.getsize(os.path.join(dst, f)), sha(os.path.join(dst, f))))
    with open(os.path.join(frozen, 'manifest.tsv'), 'w', newline='\n') as f:
        f.write('set\tfile\tbytes\tsha256\n')
        for r in rows:
            f.write('%s\t%s\t%d\t%s\n' % r)
    with open(os.path.join(frozen, 'VERSION.txt'), 'w', newline='\n') as f:
        f.write('CONVERTER_VERSION %s\n' % pbr_models.CONVERTER_VERSION)
    print('frozen: %d files (%s)' % (len(rows), ', '.join('%s %d' % (n, sum(1 for r in rows if r[0] == n)) for n, _ in sets(w388, wgen4))))
    return 0


def check(w388, wgen4, frozen):
    want = {}
    for l in open(os.path.join(frozen, 'manifest.tsv')).read().split('\n')[1:]:
        c = l.split('\t')
        if len(c) == 4:
            want[(c[0], c[1])] = (int(c[2]), c[3])
    bad = 0
    seen = set()
    for name, src in sets(w388, wgen4):
        for f in sorted(os.listdir(src)):
            if not f.endswith('.pkx'):
                continue
            seen.add((name, f))
            w = want.get((name, f))
            if w is None:
                print('EXTRA   %s/%s' % (name, f))
                bad += 1
            elif sha(os.path.join(src, f)) != w[1]:
                print('DIFFERS %s/%s' % (name, f))
                bad += 1
    for k in sorted(set(want) - seen):
        print('MISSING %s/%s' % k)
        bad += 1
    print('checked %d files against the manifest: %s' % (len(seen), 'all equal' if not bad else '%d problem(s)' % bad))
    return 1 if bad else 0


def main(a):
    if len(a) != 4 or a[0] not in ('make', 'check'):
        print(__doc__)
        return 2
    return (make if a[0] == 'make' else check)(*[os.path.abspath(x) for x in a[1:]])


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
