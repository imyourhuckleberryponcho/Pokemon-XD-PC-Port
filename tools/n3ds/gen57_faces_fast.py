"""The faces and the Pokedex pictures of the launcher's Gen 5-7 build, drawn the fast way.

The first way (gen57_faces.py render / gen57_all --xd-exe) started xd.exe for every dozen models and worked each camera out one species at a
time in one process (~10 s a model to load): hours. Here
  * the cameras are worked out in parallel (a process pool, one dump reader a process),
  * every picture of a stage is asked of ONE xd.exe run (as tools/pokedex_pictures.py does: 1743 pictures in minutes); a run only
    splits when the list would not fit the 32 KB limit of an environment variable,
  * a stop loses nothing: pictures already there (and whole: checked) are not drawn again.
The cameras, the renderer, the mending of empty faces and the round cut are gen57_faces.py's own: the faces look the same.
"""
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ENV_LIMIT = 24000

_W = {}


def whole_png(path):
    """A picture that is there and complete (a render killed half way may leave a short file)."""
    try:
        from PIL import Image
        with Image.open(path) as im:
            im.load()
        return True
    except Exception:
        return False


def _init(rom):
    import gen57_data as D
    _W['u'] = D.Usum(rom)


def _view_one(job):
    nat, mnum, grp, fsys, wide = job
    import make_proto as M
    import noddisc
    import gen57_facecam as FC
    try:
        gm, _t, _m, _g = M.load_species(_W['u'].r, mnum, grp)
        data = open(fsys, 'rb').read()
        pk = noddisc.extract(data, [e for e in noddisc.fsys_entries(data) if e.type == 15][0])
        v = FC.face_view(gm, pk, nat, wide=wide)
    except BaseException as e:
        return nat, None, 'camera: ' + repr(e)[:120]
    return nat, v, ('no head bone' if v is None else '')


def file_hash(path):
    import hashlib
    with open(path, 'rb') as f:
        return hashlib.sha1(f.read()).hexdigest()[:20]


def views_parallel(a, A, pack, nats, jobs, progress, wide=False, label='faces', table=None):
    """-> ({nat: view}, {nat: why not}). `table` (gen57_speed.load(dump)['views' / 'wide']): numbers of an earlier search over the very same model
    (key = the model file's hash + the rule), used as they are; every other species is searched."""
    import gen57_facecam as FC
    nums = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json')))
    mn, _cw = A.load_tables()
    todo, why, views = [], {}, {}
    table = table if (table and not os.environ.get('FACE_FRAME')) else {}
    hits = 0
    for nat in nats:
        f = os.path.join(pack, 'files', 'pkx_n3_%d.fsys' % nat)
        if not os.path.exists(f):
            why[nat] = 'model not in the pack'
            continue
        ent = table.get(str(nat))
        if ent and ent.get('key') == FC.view_key(nat, file_hash(f), wide):
            hits += 1
            if ent.get('view') is None:
                why[nat] = 'no head bone'
            else:
                views[nat] = ent['view']
            continue
        todo.append((nat, A.model_number(nat, mn), nums.get('group_override', {}).get(str(nat)), f, wide))
    if hits:
        progress(hits, hits + len(todo), 'face cameras')
    done = hits
    if not todo:
        return views, why
    with cf.ProcessPoolExecutor(max_workers=max(1, min(jobs, len(todo))), initializer=_init, initargs=(a.rom,)) as ex:
        for nat, v, w in ex.map(_view_one, todo):
            done += 1
            progress(done, hits + len(todo), 'face cameras')
            if v is None:
                why[nat] = w
            else:
                views[nat] = v
    return views, why


def chunks(items):
    cur, size = [], 0
    for it in items:
        if cur and size + len(it) + 1 > ENV_LIMIT:
            yield cur
            cur, size = [], 0
        cur.append(it)
        size += len(it) + 1
    if cur:
        yield cur


def watch_count(folder, names, total, progress, label, stop, base=0):
    """While a run draws: count the pictures that are there and say so."""
    while not stop.is_set():
        n = sum(1 for x in names if os.path.exists(os.path.join(folder, x)))
        progress(min(base + n, total), total, label)
        stop.wait(1.0)


def render_all(GF, a, pack, gen4, work, items, names, tag, progress, label, log, size=None):
    """items / names: parallel lists. One xd.exe run per chunk; the faces' run_renderer is the same one the first way used."""
    out = os.path.join(work, 'render')
    os.makedirs(out, exist_ok=True)
    need = [(it, nm) for it, nm in zip(items, names) if not whole_png(os.path.join(out, nm))]
    for it, nm in zip(items, names):
        p = os.path.join(out, nm)
        if os.path.exists(p) and not whole_png(p):
            os.remove(p)
    total = len(items)
    pieces = split_runs([it for it, _n in need], xd_runs())
    stop = threading.Event()
    th = threading.Thread(target=watch_count, args=(out, names, total, progress, label, stop, 0), daemon=True)
    th.start()

    def draw(k, piece):
        n = 0
        for part in chunks(piece):
            t0 = time.time()
            kw = dict(timeout=6 * 3600)
            if size:
                kw['size'] = size
            GF.run_renderer(a.xd_exe, a.xd_disc, pack, gen4, work, part, '%s%d_%d' % (tag, k, n), **kw)
            log('render %s%d.%d: %d pictures asked, %.0f s' % (tag, k, n, len(part), time.time() - t0))
            n += 1

    try:
        with cf.ThreadPoolExecutor(max_workers=max(1, len(pieces))) as ex:
            for f in [ex.submit(draw, k, p) for k, p in enumerate(pieces) if p]:
                f.result()
    finally:
        stop.set()
        th.join()
    progress(total, total, label)
    return [nm for nm in names if not os.path.exists(os.path.join(out, nm))]


def make_faces(a, A, pack, work, nats, jobs, progress, log):
    import gen57_faces as GF
    import gen57_facecam as FC
    gen4 = os.path.abspath(a.gen4_pack)
    os.makedirs(work, exist_ok=True)
    vf = os.path.join(work, 'views.json')
    import gen57_speed as SP
    sp = SP.load(a.rom)
    if os.path.exists(vf):
        d = json.load(open(vf))
        views, why = {int(k): v for k, v in d['views'].items()}, {int(k): v for k, v in d['why'].items()}
    else:
        views, why = views_parallel(a, A, pack, nats, jobs, progress, table=sp.get('views'))
        json.dump(dict(views={str(k): v for k, v in views.items()}, why={str(k): v for k, v in why.items()}), open(vf, 'w'), indent=1)
    order = sorted(views)
    missing = render_all(GF, a, pack, gen4, work, [GF.face_item(n, views[n]) for n in order], ['%d.png' % n for n in order], 'f', progress, 'faces', log)
    if missing:
        log('faces not drawn: %s' % missing)

    out = os.path.join(work, 'render')
    bad = [n for n in order if not FC.FRAMING.get(n, {}).get('noauto') and ('share' in FC.FRAMING.get(n, {}) or not GF.face_ok(os.path.join(out, '%d.png' % n)))]
    if bad:
        log('mending: %s' % bad)
        wf = os.path.join(work, 'wide_views.json')
        wide = {int(k): v for k, v in json.load(open(wf)).items()} if os.path.exists(wf) else {}
        need = [n for n in bad if n not in wide]
        if need:
            got, _w = views_parallel(a, A, pack, need, jobs, progress, wide=True, table=sp.get('wide'))
            wide.update(got)
            json.dump({str(k): v for k, v in wide.items()}, open(wf, 'w'), indent=1)
        wl = [n for n in bad if n in wide]
        render_all(GF, a, pack, gen4, work, [GF.face_item(n, wide[n], 'w%d' % n) for n in wl], ['w%d.png' % n for n in wl], 'w', progress, 'faces (second look)', log)
        nv = {}
        for n in wl:
            p = os.path.join(out, 'w%d.png' % n)
            v = GF.head_from_silhouette(p, wide[n], FC.FRAMING.get(n, {}).get('share', 0.22)) if os.path.exists(p) else None
            if v:
                nv[n] = v
        mf = os.path.join(work, 'mended.json')
        done_m = {int(k) for k in json.load(open(mf))} if os.path.exists(mf) else set()
        for n in sorted(nv):
            if n not in done_m and os.path.exists(os.path.join(out, '%d.png' % n)):
                os.remove(os.path.join(out, '%d.png' % n))
        render_all(GF, a, pack, gen4, work, [GF.face_item(n, nv[n]) for n in sorted(nv)], ['%d.png' % n for n in sorted(nv)], 'm', progress, 'faces (second look)', log)
        json.dump(sorted(nv), open(mf, 'w'))
    GF.cut(pack, work, nats, why, log=log)


def xd_runs(jobs=0):
    """How many xd.exe runs may draw at once: 2 on a PC with 4 or more processors and the memory for them (each holds ~0.8 GB), else 1.
    GEN57_XD_RUNS=<n> overrides (tests)."""
    v = os.environ.get('GEN57_XD_RUNS')
    if v and v.isdigit():
        return max(1, int(v))
    import gen57_memory
    avail = gen57_memory.available_memory_mb()
    if gen57_memory.cpu_count() < 4 or (avail is not None and avail < 4500):
        return 1
    return 2


def split_runs(items, runs):
    """`items` in `runs` contiguous pieces of about the same size."""
    n = len(items)
    runs = max(1, min(runs, n or 1))
    return [items[n * k // runs:n * (k + 1) // runs] for k in range(runs)]


def make_pictures(a, pack, gen4, out, progress, log, first_row):
    """The Pokedex's pictures of the species rows from `first_row` up that have a model, shiny too (tools/pokedex_pictures.py's run). One xd.exe run
    draws a part of the list; xd_runs() of them go side by side (each with its own card, settings and cache folders)."""
    os.makedirs(out, exist_ok=True)
    info = json.load(open(os.path.join(pack, 'gen57.json')))



    last = 494 + 26 + int(info.get('species', 314)) + int(info.get('forms', 0)) - 1
    gen4_rows = set()
    try:
        for ln in open(os.path.join(gen4, 'tables.txt'), encoding='utf-8', errors='replace'):
            p_ = ln.split()
            if len(p_) >= 3 and p_[0] == 'species' and p_[1].isdigit() and p_[2].isdigit():
                gen4_rows.add(int(p_[2]))
    except OSError:
        pass

    def has_row(r):
        if 252 <= r <= 276 or r == 412:
            return False
        if 412 <= r < 520:
            return not gen4_rows or r in gen4_rows
        return True
    names, items = ['michael.png'], ['michael']
    for r in range(first_row, last + 1):
        if not has_row(r):
            continue
        items += ['row:%d' % r, 'row:%d:shiny' % r]
        names += ['pokemon_%d.png' % r, 'pokemon_%d_shiny.png' % r]
    have = {f for f in os.listdir(out) if f.endswith('.png') and not whole_png(os.path.join(out, f))}
    for f in have:
        os.remove(os.path.join(out, f))
    base_work = tempfile.mkdtemp(prefix='dexpics_')
    links = []

    def setup(k):
        work = os.path.join(base_work, 'run%d' % k)
        for d in ('card', 'settings', 'cache', 'appdata'):
            os.makedirs(os.path.join(work, d))
        with open(os.path.join(work, 'settings', 'settings.ini'), 'w') as f:
            f.write('format = 1\npreset = enhanced\n')
        os.makedirs(os.path.join(work, 'settings', 'extra_assets'))
        for src, sub in ((gen4, 'pbr_gen4'), (pack, 'usum_gen57')):
            link = os.path.join(work, 'settings', 'extra_assets', sub)
            if os.name == 'nt':
                subprocess.run(['cmd', '/c', 'mklink', '/J', link, os.path.abspath(src)], check=True, stdout=subprocess.DEVNULL)
            else:
                os.symlink(os.path.abspath(src), link)
            links.append(link)
        env = {k_: v for k_, v in os.environ.items() if not k_.startswith('XD_')}
        env.update({'XD_NO_MESSAGE_BOXES': '1', 'XD_WINDOW_OFFSCREEN': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0', 'XD_RECORD': '0',
                    'SDL_AUDIO_DRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'XD_CARD_DIR': os.path.join(work, 'card'),
                    'XD_SETTINGS_DIR': os.path.join(work, 'settings'), 'XD_CACHE_DIR': os.path.join(work, 'cache'),
                    'XD_RUN_LABEL': 'pokedex pictures %d' % k, 'XD_DETERMINISTIC': '1', 'XD_RENDER_ICON_POSE': '0', 'XD_CHALLENGES': 'gen4_pokemon,gen57_pokemon', 'XD_RENDER_ICON_DIR': out,
                    'XD_RENDER_ICON_SIZE': '256', 'APPDATA': os.path.join(work, 'appdata')})
        if os.environ.get('GEN57_REAL_CLOCK') == '1':
            env.pop('XD_DETERMINISTIC', None)
        return work, env

    def draw(k, part):
        work, env = setup(k)
        n = 0
        for sub in chunks(part):
            e = dict(env, XD_RENDER_ICON=','.join(sub))
            t0 = time.time()
            with open(os.path.join(work, 'run%d.log' % n), 'w') as lg:
                subprocess.run([a.xd_exe, a.xd_disc], env=e, stdout=lg, stderr=subprocess.STDOUT, timeout=6 * 3600)
            log('pictures run %d.%d: %d asked, %.0f s' % (k, n, len(sub), time.time() - t0))
            n += 1

    try:
        todo = [(it, nm) for it, nm in zip(items, names) if not os.path.exists(os.path.join(out, nm))]
        total = len(items)
        pieces = [[it for it, _n in p] for p in split_runs(todo, xd_runs())]
        stop = threading.Event()
        th = threading.Thread(target=watch_count, args=(out, names, total, progress, 'pictures', stop), daemon=True)
        th.start()
        try:
            log('pictures: %d to draw in %d xd runs' % (len(todo), len(pieces)))
            with cf.ThreadPoolExecutor(max_workers=len(pieces)) as ex:
                for f in [ex.submit(draw, k, p) for k, p in enumerate(pieces) if p]:
                    f.result()
        finally:
            stop.set()
            th.join()
        made = [nm for nm in names if os.path.exists(os.path.join(out, nm))]
        log('pictures: %d of %d' % (len(made), total))


        if len(made) < total:
            log('pictures MISSING: %d of %d (their models did not load); the pack is finished without them: make it again to draw them' % (total - len(made), total))
    finally:
        for link in links:
            if os.path.lexists(link):
                try:
                    os.rmdir(link) if os.name == 'nt' else os.unlink(link)
                except OSError:
                    pass
        shutil.rmtree(base_work, ignore_errors=True)
