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


def views_parallel(a, A, pack, nats, jobs, progress, wide=False, label='faces'):
    """-> ({nat: view}, {nat: why not})."""
    nums = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json')))
    mn, _cw = A.load_tables()
    todo, why, views = [], {}, {}
    for nat in nats:
        f = os.path.join(pack, 'files', 'pkx_n3_%d.fsys' % nat)
        if not os.path.exists(f):
            why[nat] = 'model not in the pack'
            continue
        todo.append((nat, A.model_number(nat, mn), nums.get('group_override', {}).get(str(nat)), f, wide))
    done = 0
    with cf.ProcessPoolExecutor(max_workers=max(1, min(jobs, len(todo) or 1)), initializer=_init, initargs=(a.rom,)) as ex:
        for nat, v, w in ex.map(_view_one, todo):
            done += 1
            progress(done, len(todo), 'face cameras')
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
    base = total - len(need)
    k = 0
    for part in chunks([it for it, _n in need]):
        pnames = [nm for it, nm in need if it in part]
        stop = threading.Event()
        th = threading.Thread(target=watch_count, args=(out, names, total, progress, label, stop, 0), daemon=True)
        th.start()
        try:
            t0 = time.time()
            kw = dict(timeout=6 * 3600)
            if size:
                kw['size'] = size
            GF.run_renderer(a.xd_exe, a.xd_disc, pack, gen4, work, part, '%s%d' % (tag, k), **kw)
            log('render %s%d: %d pictures asked, %.0f s' % (tag, k, len(part), time.time() - t0))
        finally:
            stop.set()
            th.join()
        k += 1
    progress(total, total, label)
    return [nm for nm in names if not os.path.exists(os.path.join(out, nm))]


def make_faces(a, A, pack, work, nats, jobs, progress, log):
    import gen57_faces as GF
    import gen57_facecam as FC
    gen4 = os.path.abspath(a.gen4_pack)
    os.makedirs(work, exist_ok=True)
    vf = os.path.join(work, 'views.json')
    if os.path.exists(vf):
        d = json.load(open(vf))
        views, why = {int(k): v for k, v in d['views'].items()}, {int(k): v for k, v in d['why'].items()}
    else:
        views, why = views_parallel(a, A, pack, nats, jobs, progress)
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
            got, _w = views_parallel(a, A, pack, need, jobs, progress, wide=True)
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


def make_pictures(a, pack, gen4, out, progress, log, first_row):
    """The Pokedex's pictures of the species rows from `first_row` up that have a model, shiny too (tools/pokedex_pictures.py's run)."""
    os.makedirs(out, exist_ok=True)
    last = 0
    for ln in open(os.path.join(pack, 'tables.txt'), encoding='utf-8', errors='replace'):
        pass

    info = json.load(open(os.path.join(pack, 'gen57.json')))
    last = 494 + 26 + int(info.get('species', 314)) + int(info.get('forms', 0)) + 4
    names, items = ['michael.png'], ['michael']
    for r in range(first_row, last + 1):
        items += ['row:%d' % r, 'row:%d:shiny' % r]
        names += ['pokemon_%d.png' % r, 'pokemon_%d_shiny.png' % r]
    have = {f for f in os.listdir(out) if f.endswith('.png') and not whole_png(os.path.join(out, f))}
    for f in have:
        os.remove(os.path.join(out, f))
    work = tempfile.mkdtemp(prefix='dexpics_')
    try:
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
        env = {k: v for k, v in os.environ.items() if not k.startswith('XD_')}
        env.update({'XD_NO_MESSAGE_BOXES': '1', 'XD_WINDOW_OFFSCREEN': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0', 'XD_RECORD': '0',
                    'SDL_AUDIO_DRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'XD_CARD_DIR': os.path.join(work, 'card'),
                    'XD_SETTINGS_DIR': os.path.join(work, 'settings'), 'XD_CACHE_DIR': os.path.join(work, 'cache'),
                    'XD_RUN_LABEL': 'pokedex pictures', 'XD_CHALLENGES': 'gen4_pokemon,gen57_pokemon', 'XD_RENDER_ICON_DIR': out,
                    'XD_RENDER_ICON_SIZE': '256', 'APPDATA': os.path.join(work, 'appdata')})
        todo = [(it, nm) for it, nm in zip(items, names) if not os.path.exists(os.path.join(out, nm))]
        total = len(items)
        k = 0
        for part in chunks([it for it, _n in todo]):
            e = dict(env, XD_RENDER_ICON=','.join(part))
            stop = threading.Event()
            th = threading.Thread(target=watch_count, args=(out, names, total, progress, 'pictures', stop), daemon=True)
            th.start()
            t0 = time.time()
            try:
                with open(os.path.join(work, 'run%d.log' % k), 'w') as lg:
                    subprocess.run([a.xd_exe, a.xd_disc], env=e, stdout=lg, stderr=subprocess.STDOUT, timeout=6 * 3600)
            finally:
                stop.set()
                th.join()
            log('pictures run %d: %d asked, %.0f s' % (k, len(part), time.time() - t0))
            k += 1
        made = [nm for nm in names if os.path.exists(os.path.join(out, nm))]
        log('pictures: %d of %d' % (len(made), total))

        if len(made) < total // 3:
            raise RuntimeError('the Pokedex pictures were not drawn (%d of %d); see gen57.log' % (len(made), total))
    finally:
        for sub in ('pbr_gen4', 'usum_gen57'):
            link = os.path.join(work, 'settings', 'extra_assets', sub)
            if os.path.lexists(link):
                try:
                    os.rmdir(link) if os.name == 'nt' else os.unlink(link)
                except OSError:
                    pass
        shutil.rmtree(work, ignore_errors=True)
