"""Faces (the round head picture in the party list, the summary, the PC boxes, the battle name plate ...) for the Gen 5-7 pack's species.

XD's own faces are round close-ups of the head, rendered from the 3D model: 42 x 42 pixels, a black background, a hard circle. The Gen 5-7
pack makes its faces the same way, at pack time, on the player's PC, from the player's own dump (nothing is stored in the repository):

  1. gen57_facecam.py finds each head in the dump's skeleton and skin weights and says where the camera looks (a point and a radius in the
     converted model's space);
  2. the game's own icon renderer (xd.exe with XD_RENDER_ICON=pokemon:<model row>:face=x/y/z/r/yaw/pitch:<name>, render_icon.cpp: the
     game's draw call, our camera and lights, 1024 x 1024 with the transparency worked out from a black and a white picture) draws it;
  3. round_face() puts it on black, shrinks it to 84 x 84 (Lanczos, never nearest-neighbour) and cuts the circle; the game shrinks that to
     its 42 x 42 / 256 colours (xd_faces.cpp).

A species whose head cannot be found, whose model is not in the pack or whose render fails keeps the box icon of the dump (64 x 32 pixels,
made by gen57_all.icon_faces): faces.json in the pack says which species used what.

  python tools/n3ds/gen57_faces.py render <pack> --rom <USUM cci> --xd-exe <xd.exe> --disc <XD iso> --gen4-pack <folder> --work <folder> [--only 506,507]
  python tools/n3ds/gen57_faces.py cut <pack> <work folder>        (the pictures of an earlier render run into the pack)
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SIZE = 84
RENDER = 512
PKX_SHIFT = 31


def round_face(path_or_img, size=SIZE):
    """A rendered head (RGBA, transparent around it) -> the game's kind of face: on black, circle cut, `size` x `size`."""
    im = path_or_img if isinstance(path_or_img, Image.Image) else Image.open(path_or_img)
    a = np.asarray(im.convert('RGBA'), np.float32) / 255.0
    rgb = a[..., :3] * a[..., 3:4]
    big = Image.fromarray(np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB')
    small = np.asarray(big.resize((size, size), Image.LANCZOS), np.uint8)
    ss = 8
    yy, xx = np.mgrid[0:size * ss, 0:size * ss]
    d = np.hypot((xx + 0.5) / ss - size / 2, (yy + 0.5) / ss - size / 2)
    mask = (d <= size / 2).reshape(size, ss, size, ss).mean(axis=(1, 3))
    out = np.dstack([small, np.clip(mask * 255 + 0.5, 0, 255).astype(np.uint8)])
    return Image.fromarray(out, 'RGBA')




def face_ok(path, lo=0.35, hi=0.995):
    """Is the picture a usable face: the picture's middle (inside the circle) is mostly the model, not empty black."""
    try:
        a = np.asarray(Image.open(path).convert('RGBA'))[..., 3] > 128
    except Exception:
        return False
    h, w = a.shape
    yy, xx = np.mgrid[0:h, 0:w]
    inside = np.hypot((xx + 0.5) / w - 0.5, (yy + 0.5) / h - 0.5) <= 0.5
    cov = float(a[inside].mean())
    return lo <= cov <= hi


def head_from_silhouette(path, view, share=0.22):
    """A wide picture (`view` = its camera) -> a new face view looking at the top part of the model's silhouette (the head of an upright
    model): the rows from the top down to where `share` of the drawn area is reached. Found in the picture itself, so it is right wherever
    the bones put it. None when nothing is drawn."""
    import math
    a = np.asarray(Image.open(path).convert('RGBA'))[..., 3] > 128
    if a.sum() < 50:
        return None
    h, w = a.shape
    rows = a.sum(axis=1).astype(float)
    cum = np.cumsum(rows) / rows.sum()
    r0 = int(np.nonzero(rows > 0)[0][0])
    r1 = int(np.searchsorted(cum, share)) + 1
    r1 = max(r1, r0 + 8)
    sub = a[r0:r1]
    ys, xs = np.nonzero(sub)
    lo_x, hi_x = np.percentile(xs, 4), np.percentile(xs, 96)
    cx, cy = (lo_x + hi_x) * 0.5, r0 + (ys.min() + ys.max()) * 0.5
    half_px = max(hi_x - lo_x, ys.max() - ys.min()) * 0.5 * 1.2
    k = view['r'] / math.cos(math.radians(15.0))
    u, v = (cx / w - 0.5) * 2.0, (0.5 - cy / h) * 2.0
    yaw, pitch = math.radians(view['yaw']), math.radians(view['pitch'])
    right = np.array([math.cos(yaw), 0.0, -math.sin(yaw)])
    look = -np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)])
    up = np.cross(right, look)
    p = np.array([view['x'], view['y'], view['z']]) + right * u * k + up * v * k
    return dict(x=float(p[0]), y=float(p[1]), z=float(p[2]), r=float(max(half_px / (w / 2.0) * k, 0.05)), yaw=view['yaw'], pitch=view['pitch'],
                how='silhouette top part')




def _link(link, target):
    link = os.path.abspath(link)
    if os.path.lexists(link):
        if sys.platform == 'win32':
            subprocess.run(['cmd', '/c', 'rmdir', link], capture_output=True)
        else:
            os.remove(link)
    if sys.platform == 'win32':
        subprocess.run(['cmd', '/c', 'mklink', '/J', link, target], capture_output=True, check=True)
    else:
        os.symlink(target, link)


def run_renderer(xd_exe, disc, pack, gen4_pack, work, items, tag, size=RENDER, timeout=900):
    """One xd.exe run (hidden, muted, off screen, its own folders): XD_RENDER_ICON items -> work/render/*.png."""
    out = os.path.join(work, 'render')
    st, ad = os.path.join(work, 'settings_' + tag), os.path.join(work, 'appdata_' + tag)
    for d in (out, os.path.join(st, 'extra_assets'), os.path.join(ad, 'Pokemon XD'), os.path.join(work, 'card_' + tag), os.path.join(work, 'cache_' + tag)):
        os.makedirs(d, exist_ok=True)
    _link(os.path.join(st, 'extra_assets', 'usum_gen57'), os.path.abspath(pack))
    if gen4_pack:
        _link(os.path.join(st, 'extra_assets', 'pbr_gen4'), os.path.abspath(gen4_pack))
    with open(os.path.join(st, 'settings.ini'), 'w') as f:
        f.write('format = 1\npreset = original\n')
    env = dict(os.environ, APPDATA=ad, XD_SETTINGS_DIR=st, XD_CARD_DIR=os.path.join(work, 'card_' + tag), XD_CACHE_DIR=os.path.join(work, 'cache_' + tag),
               XD_WINDOW_OFFSCREEN='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', XD_NO_MESSAGE_BOXES='1', SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               XD_DETERMINISTIC='1', XD_RENDER_ICON_POSE='0', XD_CHALLENGES='gen4_pokemon,gen57_pokemon', XD_RENDER_ICON=','.join(items), XD_RENDER_ICON_DIR=out, XD_RENDER_ICON_SIZE=str(size))
    try:
        with open(os.path.join(work, 'render_%s.log' % tag), 'w') as lg:
            subprocess.run([xd_exe, disc], env=env, stdout=lg, stderr=subprocess.STDOUT, timeout=timeout)
    finally:
        for n in ('usum_gen57', 'pbr_gen4'):
            lk = os.path.join(st, 'extra_assets', n)
            if os.path.lexists(lk):
                if sys.platform == 'win32':
                    subprocess.run(['cmd', '/c', 'rmdir', lk], capture_output=True)
                else:
                    os.remove(lk)


def face_item(nat, v, name=None):
    return 'pokemon:%d:face=%.4f/%.4f/%.4f/%.4f/%.1f/%.1f:%s' % (nat + PKX_SHIFT, v['x'], v['y'], v['z'], v['r'], v['yaw'], v['pitch'], name or nat)


def views_for(u, pack, nats, model_number_of, log=print, wide=False):
    """-> {nat: view dict} for the species whose model is in the pack and whose head is found; {nat: reason} for the others."""
    import make_proto as M
    import noddisc
    import gen57_facecam as FC
    nums = json.load(open(os.path.join(HERE, 'gen57_model_numbers.json')))
    views, why = {}, {}
    for nat in nats:
        f = os.path.join(pack, 'files', 'pkx_n3_%d.fsys' % nat)
        if not os.path.exists(f):
            why[nat] = 'model not in the pack'
            continue
        try:
            mn = model_number_of(nat)
            gm, _t, _m, _g = M.load_species(u.r, mn, nums.get('group_override', {}).get(str(nat)))
            data = open(f, 'rb').read()
            pk = noddisc.extract(data, [e for e in noddisc.fsys_entries(data) if e.type == 15][0])
            v = FC.face_view(gm, pk, nat, wide=wide)
        except BaseException as e:
            why[nat] = 'camera: ' + repr(e)[:120]
            continue
        if v is None:
            why[nat] = 'no head bone'
        else:
            views[nat] = v
    return views, why


def mend(u, a, work, views, model_number_of, tag):
    """Faces that came out empty / tiny: a wide picture of the whole model, the head found in its silhouette, drawn again."""
    import gen57_facecam as FC
    out = os.path.join(work, 'render')
    bad = [n for n in sorted(views) if not FC.FRAMING.get(n, {}).get('noauto') and (a.remend or 'share' in FC.FRAMING.get(n, {}) or not face_ok(os.path.join(out, '%d.png' % n)))]
    if not bad:
        return
    print('mending (empty or tiny faces):', bad, flush=True)
    wf = os.path.join(work, 'wide_views.json')
    cache = {int(k): v for k, v in json.load(open(wf)).items()} if os.path.exists(wf) else {}
    need = [n for n in bad if n not in cache or not os.path.exists(os.path.join(out, 'w%d.png' % n))]
    if need:
        got, _w = views_for(u, a.pack, need, model_number_of, wide=True)
        for i in range(0, len(need), a.batch):
            part = [n for n in need[i:i + a.batch] if n in got]
            run_renderer(a.xd_exe, a.disc, a.pack, a.gen4_pack, work, [face_item(n, got[n], 'w%d' % n) for n in part], '%s_w%d' % (tag, i // a.batch))
        cache.update(got)
        json.dump({str(k): v for k, v in cache.items()}, open(wf, 'w'), indent=1)
    wv = cache
    nv = {}
    for n in bad:
        p = os.path.join(out, 'w%d.png' % n)
        v = head_from_silhouette(p, wv[n], FC.FRAMING.get(n, {}).get('share', 0.22)) if n in wv and os.path.exists(p) else None
        if v:
            nv[n] = v
    for i in range(0, len(nv), a.batch):
        part = sorted(nv)[i:i + a.batch]
        for n in part:
            if os.path.exists(os.path.join(out, '%d.png' % n)):
                os.remove(os.path.join(out, '%d.png' % n))
        run_renderer(a.xd_exe, a.disc, a.pack, a.gen4_pack, work, [face_item(n, nv[n]) for n in part], '%s_m%d' % (tag, i // a.batch))
    views.update(nv)
    json.dump({str(k): v for k, v in views.items()}, open(os.path.join(work, 'views_%s.json' % tag), 'w'), indent=1)
    print('mended', sorted(nv), flush=True)


def cut(pack, work, nats, why=None, log=print):
    """The rendered pictures of `work` into <pack>/textures/faces; faces.json = what each species used."""
    out = os.path.join(pack, 'textures', 'faces')
    os.makedirs(out, exist_ok=True)
    rep = {}
    for nat in nats:
        src = os.path.join(work, 'render', '%d.png' % nat)
        if os.path.exists(src):
            try:
                round_face(src).save(os.path.join(out, '%d.png' % nat))
                rep[nat] = 'rendered'
                continue
            except Exception as e:
                why = dict(why or {}, **{nat: 'cut: ' + repr(e)[:100]})
        rep[nat] = 'box icon (%s)' % ((why or {}).get(nat) or 'render missing')
    json.dump({str(k): v for k, v in rep.items()}, open(os.path.join(pack, 'faces.json'), 'w'), indent=1)
    fb = sorted(n for n, v in rep.items() if v != 'rendered')
    log('faces: %d rendered, %d from the box icon%s' % (len(rep) - len(fb), len(fb), (': ' + ', '.join(str(n) for n in fb)) if fb else ''))
    return rep


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['render', 'cut'])
    ap.add_argument('pack')
    ap.add_argument('work', nargs='?')
    ap.add_argument('--rom')
    ap.add_argument('--xd-exe')
    ap.add_argument('--disc')
    ap.add_argument('--gen4-pack', default='')
    ap.add_argument('--only', default='')
    ap.add_argument('--batch', type=int, default=12)
    ap.add_argument('--tag', default='f1')
    ap.add_argument('--remend', action='store_true', help='mend every listed species again (from the kept wide pictures)')
    a = ap.parse_args(argv[1:])
    import gen57_all as A
    work = os.path.abspath(a.work or a.pack + '.faces')
    nats = [int(x) for x in a.only.split(',')] if a.only else list(range(A.FIRST, A.LAST + 1))
    why = {}
    if a.cmd == 'render':
        import gen57_data as D
        u = D.Usum(a.rom)
        mn, _cw = A.load_tables()
        views, why = views_for(u, a.pack, nats, lambda n: A.model_number(n, mn))
        os.makedirs(work, exist_ok=True)
        json.dump({str(k): v for k, v in views.items()}, open(os.path.join(work, 'views_%s.json' % a.tag), 'w'), indent=1)
        todo = [n for n in sorted(views) if not os.path.exists(os.path.join(work, 'render', '%d.png' % n))]
        for i in range(0, len(todo), a.batch):
            part = todo[i:i + a.batch]
            run_renderer(a.xd_exe, a.disc, a.pack, a.gen4_pack, work, [face_item(n, views[n]) for n in part], '%s_%d' % (a.tag, i // a.batch))
            print('rendered', [n for n in part if os.path.exists(os.path.join(work, 'render', '%d.png' % n))], flush=True)
        mend(u, a, work, views, lambda n: A.model_number(n, mn), a.tag)
    cut(a.pack, work, nats, why)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
