"""pbr_models: Pokemon Battle Revolution models for Pokemon XD, made straight from the two disc images.

    pbr_models --pbr-disc <Battle Revolution image> --xd-disc <XD image> --out <folder> [--jobs N]

Reads both discs through nod.dll (the disc library the port ships; any .iso .rvz .wbfs ...), converts every
Pokemon model that Battle Revolution and XD both have (the pairing of batch.py, converter = pbr_to_xd.py), and
writes the extra-assets folder <folder> (the launcher passes <data folder>/extra_assets/pbr; built in, not a mod): mod.json (only its description file, the launcher reads the version from it; mods code never looks there), conversion_log.txt and fsys/<archive>/#<entry> files
(the layout the game loads, see engine/mods/include/mods.h). It is built in <folder>.tmp and renamed at the end.
The result is game data made from the player's own discs: it never goes into the repository.

What it prints (the launcher reads stdout line by line; nothing else goes to stdout):
    progress <done> <total> <model name>        one per finished model
    done <converted> <kept_xd> <total_bytes>    at the end; exit 0
    error <one line>                            exit 1; exit 2 when a disc is not the right one
nod.dll (libnod.so on Linux, libnod.dylib on a Mac) is looked for next to this program first, then in NOD_DLL, then at noddisc's default.
"""
import argparse
import json
import multiprocessing
import os
import shutil
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))

CONVERTER_VERSION = '1.1.0'   # bump when the converter's output changes
MOD_ID = 'battle-revolution-models'
PBR_ID = 'RPBE01'
XD_ID = 'GXXE01'


class Fail(Exception):
    def __init__(self, msg, code=1):
        Exception.__init__(self, msg)
        self.code = code


def say(*a):
    print(*a, flush=True)


def program_dir():
    return os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else HERE


def find_nod():
    """The disc library (noddisc.LIB_NAME: nod.dll / libnod.so / libnod.dylib) next to the program, then NOD_DLL,
    then whatever noddisc defaults to."""
    import noddisc
    cands = [os.path.join(program_dir(), noddisc.LIB_NAME), os.environ.get('NOD_DLL', ''), noddisc.DEFAULT_DLL]
    for c in cands:
        if c and os.path.isfile(c):
            os.environ['NOD_DLL'] = c
            if hasattr(os, 'add_dll_directory'):
                os.add_dll_directory(os.path.dirname(os.path.abspath(c)))
            return c
    raise Fail('The disc library %s was not found (looked next to the program and in NOD_DLL).' % noddisc.LIB_NAME)


def open_disc(path, want, what):
    import noddisc
    if not os.path.isfile(path):
        raise Fail('%s disc image not found: %s' % (what, path), 2)
    try:
        d = noddisc.Disc(path)
    except Exception as ex:
        raise Fail('%s cannot be read as a disc image (%s).' % (path, str(ex).strip()), 2)
    if d.game_id != want:
        raise Fail('The %s disc image is %s, but this needs %s (%s).' % (
            what, d.game_id, want, 'Pokemon Battle Revolution, USA' if want == PBR_ID else 'Pokemon XD, USA'), 2)
    return d


def read_pairs(pbr, xd):
    """[(name, pbr model bytes, xd model bytes, xd archive, xd entry index)], the pairing of batch.py extract."""
    import noddisc
    xdm = {}
    for p in sorted(xd.files):
        if not (p.lower().startswith('pkx_') and p.lower().endswith('.fsys')):
            continue
        data = xd.file(p)
        for e in noddisc.fsys_entries(data):
            if e.type == 15:
                xdm[e.name] = (p, e.index, noddisc.extract(data, e))
    rows, seen = [], set()
    for p in sorted(pbr.files):
        if not (p.startswith('pkx_') and p.endswith('.fsys')):
            continue
        data = pbr.file(p)
        ents = noddisc.fsys_entries(data)
        if not ents:
            continue
        try:
            b = noddisc.extract(data, ents[0])
            sk = noddisc.be32(b, noddisc.be32(b, 8))
            o = noddisc.be32(b, sk)
            name = b[o:b.index(b'\0', o)].decode('latin1')
        except Exception:
            continue
        x = xdm.get(name)
        if x and name not in seen:
            seen.add(name)
            rows.append((name, b, x[2], x[0], x[1]))
    return rows


def convert_one(job):
    name, sdr, pkx, arch, idx, work, mod = job
    import pbr_to_xd
    t0 = time.time()
    lines = []
    state, size = 'converted', 0
    d = os.path.join(work, name)
    try:
        os.makedirs(d, exist_ok=True)
        sp, xp, op = (os.path.join(d, name + e) for e in ('.sdr', '.xd.pkx', '.pkx'))
        open(sp, 'wb').write(sdr)
        open(xp, 'wb').write(pkx)
        size = pbr_to_xd.convert_file(sp, xp, op, log=lines.append)
        dst = os.path.join(mod, 'fsys', arch)
        os.makedirs(dst, exist_ok=True)
        shutil.move(op, os.path.join(dst, '#%d' % idx))
    except pbr_to_xd.KeepXD as ex:
        state = 'kept_xd'
        lines.append('REFUSED: %s' % ex)
    except Exception as ex:
        state = 'kept_xd'
        lines.append('REFUSED: %s: %s' % (type(ex).__name__, ex))
        lines.append(traceback.format_exc())
    shutil.rmtree(d, ignore_errors=True)
    return name, state, size, lines, time.time() - t0


def run(a):
    out = os.path.abspath(a.out)
    tmp = out + '.tmp'
    find_nod()
    pbr = open_disc(a.pbr_disc, PBR_ID, 'Battle Revolution')
    xd = open_disc(a.xd_disc, XD_ID, 'XD')
    rows = read_pairs(pbr, xd)
    del pbr, xd
    if not rows:
        raise Fail('No Battle Revolution model has an XD counterpart on these discs.')
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'fsys'))
    work = os.path.join(tmp, '_work')
    os.makedirs(work)
    jobs = a.jobs or max(1, min(8, (os.cpu_count() or 2) - 1))
    total = len(rows)
    todo = [(n, s, p, ar, i, work, tmp) for (n, s, p, ar, i) in rows]
    results = {}
    t0 = time.time()

    def finished(r):
        results[r[0]] = r
        say('progress %d %d %s' % (len(results), total, r[0]))
    if jobs <= 1:
        for j in todo:
            finished(convert_one(j))
    else:
        with multiprocessing.Pool(min(jobs, total)) as pool:
            for r in pool.imap_unordered(convert_one, todo):
                finished(r)
    shutil.rmtree(work, ignore_errors=True)
    conv = [r for r in results.values() if r[1] == 'converted']
    kept = total - len(conv)
    nbytes = sum(r[2] for r in conv)
    with open(os.path.join(tmp, 'mod.json'), 'w') as f:
        json.dump({'id': MOD_ID, 'name': 'Battle Revolution models', 'version': CONVERTER_VERSION,
                   'author': 'made on your PC from your own discs',
                   'description': "Pokemon Battle Revolution's Pokemon models in Pokemon XD, converted from your "
                                  "own discs.",
                   'game': XD_ID, 'generated_from': PBR_ID,
                   'applies': 'restart', 'format': 1}, f, indent=2)
    with open(os.path.join(tmp, 'conversion_log.txt'), 'w') as f:
        f.write('pbr_models %s: %d models, %d converted, %d kept from XD, %d bytes, %.0f s, %d jobs\n' % (
            CONVERTER_VERSION, total, len(conv), kept, nbytes, time.time() - t0, jobs))
        for name in sorted(results):
            _, state, size, lines, secs = results[name]
            f.write('\n== %s: %s, %d bytes, %.1f s\n' % (name, state, size, secs))
            for l in lines:
                f.write(l + '\n')
    old = out + '.old'
    if os.path.isdir(old):
        shutil.rmtree(old)
    if os.path.isdir(out):
        os.rename(out, old)
    os.rename(tmp, out)
    if os.path.isdir(old):
        shutil.rmtree(old, ignore_errors=True)
    say('done %d %d %d' % (len(conv), kept, nbytes))


def main():
    # `pbr_models --version` prints `version <CONVERTER_VERSION>`: the launcher compares it with an existing mod's
    # mod.json version to say "out of date: make again" after an update that changes the output.
    if sys.argv[1:] == ['--version']:
        say('version ' + CONVERTER_VERSION)
        return 0
    ap = argparse.ArgumentParser(prog='pbr_models', description=__doc__.split('\n\n')[0])
    ap.add_argument('--pbr-disc', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=0)
    try:
        a = ap.parse_args()
    except SystemExit as ex:
        if ex.code:
            say('error Wrong command line: use --pbr-disc, --xd-disc and --out.')
        return 1 if ex.code else 0
    try:
        run(a)
        return 0
    except Fail as ex:
        say('error ' + ' '.join(str(ex).split()))
        return ex.code
    except Exception as ex:
        say('error ' + ' '.join(('%s: %s' % (type(ex).__name__, ex)).split()))
        sys.stderr.write(traceback.format_exc())
        return 1


if __name__ == '__main__':
    multiprocessing.freeze_support()
    sys.exit(main())
