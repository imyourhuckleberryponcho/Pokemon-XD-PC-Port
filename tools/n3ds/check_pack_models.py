"""Is a Gen 5-7 pack's model table consistent with its model files? (the "invisible Incineroar / Decidueye" bug)

A pack's tables.txt has one line `pkx <row> <group> <resource>` per species model. `pkx <row> 0 0` means "no model of its own": the game
then has nothing to draw in battle (the trainer stands where the Pokemon should be). The builder writes 0 0 for a model that was left out
(over the file cap), and leaves it out of GSfsys.toc. When the model files of a pack are replaced by newer ones (a converter that fits them under the cap) WITHOUT making
the table again, those rows stay 0 0 and the Pokemon is invisible. This check finds that: a row with 0 0 although files/pkx_n3_<dex>.fsys
exists and fits, and a file with no row.

  python -I tools/n3ds/check_pack_models.py <pack folder> [--cap 614400]     exit 1 when something is wrong
"""
import argparse, os, re, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pack')
    ap.add_argument('--cap', type=int, default=614400, help='the cap is on the model inside; the .fsys wrapper adds 288 bytes')
    a = ap.parse_args()
    rows = {}
    for line in open(os.path.join(a.pack, 'tables.txt'), encoding='utf-8', errors='replace'):
        m = re.match(r'pkx (\d+) (\d+) (\d+)\s*$', line)
        if m:
            rows[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    files = {}
    fdir = os.path.join(a.pack, 'files')
    for f in os.listdir(fdir):
        m = re.match(r'pkx_n3_(\d+)\.fsys$', f)
        if m:
            files[int(m.group(1))] = os.path.getsize(os.path.join(fdir, f))
    zero = sorted(r for r, v in rows.items() if v == (0, 0))

    order = sorted(files)
    first = min(rows) if rows else 0
    bad = []
    for k, nat in enumerate(order):
        r = first + k
        if r in rows and rows[r] == (0, 0) and files[nat] <= a.cap:
            bad.append((r, nat, files[nat]))
    toc = open(os.path.join(fdir, 'GSfsys.toc'), 'rb').read() if os.path.exists(os.path.join(fdir, 'GSfsys.toc')) else (
        open(os.path.join(fdir, 'gsfsys.toc'), 'rb').read() if os.path.exists(os.path.join(fdir, 'gsfsys.toc')) else b'')
    notoc = [n for n in order if toc and re.search(rb'pkx_n3_%d(?!\d)' % n, toc) is None]
    over = [(n, s) for n, s in files.items() if s - 288 > a.cap]
    print('%d pkx rows, %d with no model (0 0), %d model files, %d over the cap' % (len(rows), len(zero), len(files), len(over)))
    for r, nat, s in bad:
        print('STALE: row %d (species %d) is "0 0" but files/pkx_n3_%d.fsys exists (%d bytes) - the table was not made again' % (r, nat, nat, s))
    for n in notoc:
        print('NO TOC ENTRY: files/pkx_n3_%d.fsys is not in the pack GSfsys.toc: the game cannot find it (black screen at the battle)' % n)
    for n, s in over:
        print('OVER: pkx_n3_%d.fsys is %d bytes (cap %d): the game cannot load it' % (n, s, a.cap))
    return 1 if bad or over or notoc else 0


if __name__ == '__main__':
    sys.exit(main())
