"""The Gen 5-7 pack made by the launcher (docs/GEN57.md "Making the pack"): the stages of usum_models --gen57-pack, one after the other,
on the player's PC, from the player's own Ultra Sun / Ultra Moon dump and own XD disc. Nothing from the games is in the repository.

  usum_models --rom <dump> --xd-disc <XD> --out <models> --gen4-pack <gen4 folder> --gen57-pack <folder> --xd-exe <xd.exe> [--pictures-out <folder>]

What it runs (the same programs the hand-run mkpack.sh used, now in one program with a protocol the launcher draws):
  tables    XD's move rows, TM and tutor lists, read once through xd_data_mods (shipped beside the converter)
  models    gen57_all: 314 species models (parallel, each kept as it is made), then rows, moves, abilities, cries, Poke Spots
  forms     gen57_forms: Alolan forms, Mega Evolutions, Primal Reversions, battle forms (on a copy of the pack)
  faces     gen57_faces_fast: the round faces, drawn by the game's own icon renderer (a few xd.exe runs, not one per dozen models)
  entries   usum_dex_entries: the Pokedex's written entries (Ultra Sun / Moon, six languages) into <data folder>/extra_assets/pokedex
  pictures  the Pokedex pictures of the rows (one xd.exe run: tools/pokedex_pictures.py's way)
  check     check_pack_move_rows, check_pack_models and counts; the pack is only put in place when they pass

Protocol (stdout, read by the launcher): `stage <id> <from %> <to %>` when a stage starts (the percent range of the whole run it fills),
`progress <done> <total> <what>` inside it, `error <plain words>` (exit 1), and the caller's `done` line at the end. Everything else goes
to <pack>.work/gen57.log. The work folder <pack>.work holds what is done so far: a stop (window closed, power off) loses at most the
model or picture being made; the next run continues. It is deleted when the pack is in place, and wiped when the dump or this program's
version differs from what it was started with (state.json).
"""
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


BASE_END = 8
STAGES = [('tables', 8, 9), ('models', 9, 44), ('data', 44, 47), ('forms', 47, 55), ('faces', 55, 76), ('entries', 76, 78), ('pictures', 78, 95), ('check', 95, 100)]

GEN57_VERSION = 2
FIRST_PICTURE_ROW = 1


class Fail(Exception):
    """A stop with words for people (the launcher shows them as they are)."""


_real_out = sys.stdout
_log = None
_last_progress = [0.0]


def say(*a):
    print(*a, file=_real_out, flush=True)


def log(*a):
    if _log:
        _log.write(' '.join(str(x) for x in a) + '\n')
        _log.flush()


def stage(sid, lo, hi, label=''):
    log('== stage', sid)
    say('stage %s %d %d' % (sid, lo, hi))


def progress(done, total, what=''):
    now = time.time()
    if done != total and now - _last_progress[0] < 0.25:
        return
    _last_progress[0] = now
    say('progress %d %d %s' % (done, total, what))


def low_priority():
    """Below normal: the player keeps playing / using the PC while this runs; children (the xd.exe runs, the model workers) inherit it."""
    try:
        if sys.platform == 'win32':
            import ctypes
            ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x00004000)
        else:
            os.nice(8)
    except Exception:
        pass


def fingerprint(path):
    """The dump's size and a hash of its first and last megabyte (a few ms: the dump is gigabytes)."""
    size = os.path.getsize(path)
    h = hashlib.sha1(str(size).encode())
    with open(path, 'rb') as f:
        h.update(f.read(1 << 20))
        f.seek(max(0, size - (1 << 20)))
        h.update(f.read(1 << 20))
    return '%d-%s' % (size, h.hexdigest()[:16])


class Capture:
    """sys.stdout while a library module runs: its progress prints become protocol lines, the rest goes to the log."""

    def __init__(self, patterns):
        self.patterns = patterns
        self.buf = ''

    def write(self, s):
        self.buf += s
        while '\n' in self.buf:
            line, self.buf = self.buf.split('\n', 1)
            log(line)
            for rx, label in self.patterns:
                m = rx.match(line)
                if m:
                    progress(int(m.group(1)), int(m.group(2)), label)
                    break
        return len(s)

    def flush(self):
        pass


def run_module(main, argv, patterns):
    """Calls a module's main() as if started with argv; its prints are read for progress."""
    old_argv, old_out = sys.argv, sys.stdout
    sys.argv = ['x'] + argv
    sys.stdout = Capture(patterns)
    try:
        return main()
    finally:
        sys.stdout, sys.argv = old_out, old_argv




def parse_show(text):
    """`xd_data_mods --show` text of one entry -> (index, dict of fields, name)."""
    d = {}
    lines = text.splitlines()
    head = re.match(r'\S+ #(\d+)(?: \((.*)\))?\s*$', lines[0])
    for ln in lines[1:]:
        mm = re.match(r'\s*(\w+)\s+(.*?)\s*$', ln)
        if mm:
            v = re.match(r'^(\[.*\]|-?\d+)', mm.group(2))
            d[mm.group(1)] = json.loads(v.group(1)) if v else mm.group(2)
    d['_name'] = (head.group(2) or '') if head else ''
    return int(head.group(1)) if head else -1, d


def show_range(exe, disc, table, first, last, env):
    p = subprocess.run([exe, disc, '--show-range', table, str(first), str(last)], capture_output=True, text=True, env=env, timeout=600)
    if p.returncode:
        raise Fail("XD's tables could not be read from your XD disc (xd_data_mods, exit %d: %s)." % (p.returncode, (p.stderr or p.stdout)[-200:].strip()))
    out, cur = {}, None
    for ln in p.stdout.splitlines():
        if ln and not ln[0].isspace():
            if cur:
                i, d = parse_show('\n'.join(cur))
                out[i] = d
            cur = [ln]
        elif cur is not None:
            cur.append(ln)
    if cur:
        i, d = parse_show('\n'.join(cur))
        out[i] = d
    return out


def make_tables(a, work):
    mc, tm = os.path.join(work, 'xd_moves_cache.json'), os.path.join(work, 'xd_machines.json')
    if os.path.exists(mc) and os.path.exists(tm):
        return mc, tm
    exe = a.data_mods_exe
    if not exe or not os.path.isfile(exe):
        raise Fail('xd_data_mods (it ships beside usum_models) was not found, so XD\'s tables cannot be read.')
    env = dict(os.environ, XD_SETTINGS_DIR=os.path.join(work, 'dm_settings'))
    os.makedirs(env['XD_SETTINGS_DIR'], exist_ok=True)
    moves = show_range(exe, a.xd_disc, 'waza_data', 0, 374, env)
    tms = show_range(exe, a.xd_disc, 'tm_moves', 0, 57, env)
    tut = show_range(exe, a.xd_disc, 'wazaoshie_data', 0, 11, env)
    if len(moves) != 375 or len(tms) != 58 or len(tut) != 12:
        raise Fail("XD's move tables read from your XD disc have an unexpected size (%d / %d / %d)." % (len(moves), len(tms), len(tut)))
    json.dump(dict(xd={str(k): v for k, v in sorted(moves.items())}), open(mc + '.t', 'w'))
    json.dump(dict(tm_moves=[tms[i]['wazaID'] for i in range(58)], tutors=[tut[i]['id'] for i in range(12)]), open(tm + '.t', 'w'))
    os.replace(mc + '.t', mc)
    os.replace(tm + '.t', tm)
    return mc, tm




def wipe(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path, ignore_errors=True)


def replace_dir(src, dst):
    """src -> dst, the old dst out of the way first and deleted after (a stop in between leaves the old one as dst.old)."""
    old = dst + '.old'
    wipe(old)
    if os.path.isdir(dst):
        os.rename(dst, old)
    os.rename(src, dst)
    wipe(old)


def done_marks(work):
    p = os.path.join(work, 'state.json')
    try:
        return json.load(open(p))
    except Exception:
        return None


def save_state(work, st):
    json.dump(st, open(os.path.join(work, 'state.json.t'), 'w'))
    os.replace(os.path.join(work, 'state.json.t'), os.path.join(work, 'state.json'))


def mark(work, st, name):
    if name not in st['done']:
        st['done'].append(name)
    save_state(work, st)


def pack_is_current(out, version, fp):
    try:
        j = json.load(open(os.path.join(out, 'gen57.json')))
    except Exception:
        return False
    return j.get('converter') == version and j.get('dump') == fp and j.get('make') == GEN57_VERSION and j.get('complete') is True


def make_gen57(a, converter_version):
    """The whole Gen 5-7 pack. Returns (species, forms, bytes). Raises Fail for plain-words stops."""
    global _log
    import gen57_all as A
    import gen57_forms as F
    import gen57_faces_fast as FF
    out = os.path.abspath(a.gen57_pack)
    work = out + '.work'
    fp = fingerprint(a.rom)
    if pack_is_current(out, converter_version, fp) and not a.gen57_only:
        log('the pack is already made for this dump and this converter')
        return None
    os.makedirs(work, exist_ok=True)
    _log = open(os.path.join(work, 'gen57.log'), 'a', encoding='utf-8')
    log('==== run', time.strftime('%Y-%m-%d %H:%M:%S'), 'converter', converter_version, 'dump', fp)
    st = done_marks(work)
    if not st or st.get('converter') != converter_version or st.get('dump') != fp or st.get('make') != GEN57_VERSION or st.get('only') != a.gen57_only:
        log('a new start (the work folder was for another dump, converter or subset): wiped')
        for n in os.listdir(work):
            p = os.path.join(work, n)
            if n != 'gen57.log':
                (wipe(p) if os.path.isdir(p) else os.remove(p))
        st = dict(converter=converter_version, dump=fp, make=GEN57_VERSION, only=a.gen57_only, done=[])
        save_state(work, st)
    low_priority()
    skip = set(x for x in a.gen57_skip.split(',') if x)
    nats = [int(x) for x in a.gen57_only.split(',')] if a.gen57_only else list(range(A.FIRST, A.LAST + 1))
    jobs = a.jobs or max(1, min(8, (os.cpu_count() or 2) - 2))
    pack = os.path.join(work, 'pack')
    pack_f = os.path.join(work, 'pack_forms')
    gen4 = os.path.abspath(a.gen4_pack)
    if not os.path.isfile(os.path.join(gen4, 'files', 'GSfsys.toc')):
        raise Fail('The Gen 4 pack is not there yet (%s): it is made first.' % gen4)


    s = dict((n, (lo, hi)) for n, lo, hi in STAGES)
    stage('tables', *s['tables'])
    mc, tm = make_tables(a, work)


    if 'pack' not in st['done'] and os.path.isfile(os.path.join(pack, 'gen57.json')):
        mark(work, st, 'pack')
    if 'pack' not in st['done']:
        stage('models', *s['models'])
        argv = ['--rom', a.rom, '--xd-disc', a.xd_disc, '--gen4-pack', gen4, '--out', pack, '--jobs', str(jobs), '--cries',
                '--tm-cache', tm, '--xd-moves-cache', mc]
        if a.gen57_only:
            argv += ['--only', a.gen57_only]
        wipe(pack)
        run_module(A.main, argv, [(re.compile(r'model (\d+)/(\d+):'), 'models')])
        stage('data', *s['data'])
        if not os.path.isfile(os.path.join(pack, 'gen57.json')):
            raise Fail('The Pokemon models and rows were not made (no gen57.json).')
        wipe(pack + '.tmp')
        mark(work, st, 'pack')

    if 'forms' not in st['done']:
        stage('forms', *s['forms'])
        if 'forms' in skip:
            log('forms skipped (test run)')
            wipe(pack_f)
            shutil.copytree(pack, pack_f)
        else:
            wipe(pack_f)
            shutil.copytree(pack, pack_f)
            run_module(F.main, ['--rom', a.rom, '--xd-disc', a.xd_disc, '--pack', pack_f, '--jobs', str(jobs), '--tm-cache', tm],
                       [(re.compile(r'form model (\d+)/(\d+):'), 'forms')])
        mark(work, st, 'forms')
        wipe(pack)

    if 'faces' not in st['done']:
        stage('faces', *s['faces'])
        if 'faces' in skip:
            log('faces skipped (test run)')
        else:
            FF.make_faces(a, A, pack_f, os.path.join(work, 'faces'), nats, jobs, progress, log)
        mark(work, st, 'faces')

    ent = os.path.join(work, 'entries')
    if 'entries' not in st['done']:
        stage('entries', *s['entries'])
        import usum_dex_entries as UE
        wipe(ent)
        os.makedirs(ent)
        if run_module(lambda: UE.main(), ['--rom', a.rom, '--out', ent], []):
            raise Fail('The Pokedex entry texts could not be read from your dump.')
        progress(1, 1, 'entries')
        mark(work, st, 'entries')

    pics = os.path.join(work, 'pictures')
    if 'pictures' not in st['done']:
        stage('pictures', *s['pictures'])
        if 'pictures' in skip:
            log('pictures skipped (test run)')
        else:
            FF.make_pictures(a, pack_f, gen4, pics, progress, log, FIRST_PICTURE_ROW)
        mark(work, st, 'pictures')

    stage('check', *s['check'])
    problems = check_pack(pack_f, nats, skip)
    if problems:
        raise Fail('The finished pack did not pass its own checks: ' + ' '.join(problems[:4]))
    info_path = os.path.join(pack_f, 'gen57.json')
    info = json.load(open(info_path))
    info.update(converter=converter_version, dump=fp, make=GEN57_VERSION, complete=True)
    json.dump(info, open(info_path, 'w'), indent=1)

    replace_dir(pack_f, out)
    for f in os.listdir(ent):
        dst = os.path.join(os.path.dirname(out), 'pokedex')
        os.makedirs(dst, exist_ok=True)
        os.replace(os.path.join(ent, f), os.path.join(dst, f))
    n_pics = 0
    if a.pictures_out and os.path.isdir(pics):
        os.makedirs(a.pictures_out, exist_ok=True)
        for f in os.listdir(pics):
            if f.endswith('.png'):
                os.replace(os.path.join(pics, f), os.path.join(a.pictures_out, f))
                n_pics += 1
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    log('done: %d species, %d forms, %d bytes, %d pictures' % (info.get('species', 0), info.get('forms', 0), size, n_pics))
    _log.close()
    _log = None
    wipe(work)
    return info.get('species', 0), info.get('forms', 0), size


def check_pack(pack, nats, skip):
    """The pack's own checks (the ones once run by hand): returns plain-words problems, [] = fine."""
    import check_pack_move_rows as C1
    import check_pack_models as C2
    problems = []
    for name, main in (('move rows', C1.main), ('models', C2.main)):
        if name == 'models' and len(nats) != 314:
            continue
        old = sys.argv, sys.stdout
        sys.argv = ['x', pack]
        cap = Capture([])
        sys.stdout = cap
        try:
            code = main()
        except SystemExit as e:
            code = e.code
        except Exception as e:
            code = 1
            log('check %s raised %r' % (name, e))
        finally:
            sys.argv, sys.stdout = old
        log('check', name, 'exit', code)
        if code:
            problems.append('(%s check failed, see gen57.log)' % name)
    sp = json.load(open(os.path.join(pack, 'data', '02_species.json'), encoding='utf-8'))['add']['pokemon']
    if len(sp) != len(nats):
        problems.append('(%d species rows instead of %d)' % (len(sp), len(nats)))
    for d in ('data_fr', 'data_ge', 'data_it', 'data_sp', 'data_jp'):
        if not os.path.isfile(os.path.join(pack, d, '02_species.json')):
            problems.append('(no %s)' % d)
    if 'faces' not in skip:
        fj = json.load(open(os.path.join(pack, 'faces.json')))
        bad = [k for k, v in fj.items() if v != 'rendered']
        if len(fj) != len(nats) or len(bad) > max(3, len(nats) // 50):
            problems.append('(faces: %d of %d drawn)' % (len(fj) - len(bad), len(nats)))
        for n in nats[:1]:
            if not os.path.isfile(os.path.join(pack, 'textures', 'faces', '%d.png' % n)):
                problems.append('(no face pictures)')
    return problems
