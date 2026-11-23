"""Does the Pokedex picture run draw the same picture every time? Draws a few rows twice from the same packs (gen57_faces_fast.make_pictures's own
run, a short list) and compares the pictures pixel by pixel. usage: pictures_repeat.py <xd.exe> <XD disc> <gen4 pack> <gen57 pack> <out folder> row [row ...]"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image


def run(xd, disc, gen4, pack, out, rows, tag):
    work = os.path.join(out, 'work_' + tag)
    dst = os.path.join(out, tag)
    for d in ('card', 'settings', 'cache', 'appdata'):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(work, 'settings', 'settings.ini'), 'w') as f:
        f.write('format = 1\npreset = enhanced\n')
    ea = os.path.join(work, 'settings', 'extra_assets')
    os.makedirs(ea, exist_ok=True)
    for src, sub in ((gen4, 'pbr_gen4'), (pack, 'usum_gen57')):
        link = os.path.abspath(os.path.join(ea, sub))
        if not os.path.exists(link):
            subprocess.run(['cmd', '/c', 'mklink', '/J', link, os.path.abspath(src)], check=True, stdout=subprocess.DEVNULL)
    env = {k: v for k, v in os.environ.items() if not k.startswith('XD_')}
    items = []
    for r in rows:
        items += ['row:%s' % r, 'row:%s:shiny' % r]
    env.update({'XD_NO_MESSAGE_BOXES': '1', 'XD_WINDOW_OFFSCREEN': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0', 'XD_RECORD': '0',
                'SDL_AUDIO_DRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'XD_CARD_DIR': os.path.join(work, 'card'),
                'XD_SETTINGS_DIR': os.path.join(work, 'settings'), 'XD_CACHE_DIR': os.path.join(work, 'cache'),
                'XD_RUN_LABEL': 'pictures repeat', 'XD_CHALLENGES': 'gen4_pokemon,gen57_pokemon', 'XD_RENDER_ICON_DIR': dst,
                'XD_RENDER_ICON_SIZE': '256', 'APPDATA': os.path.join(work, 'appdata'), 'XD_RENDER_ICON': ','.join(items)})
    for kv in filter(None, os.environ.get('PICREP_ENV', '').split(',')):
        env[kv.split('=', 1)[0]] = kv.split('=', 1)[1]
    subprocess.run([xd, disc], env=env, stdout=open(os.path.join(work, 'run.log'), 'w'), stderr=subprocess.STDOUT, timeout=3600)
    for sub in ('pbr_gen4', 'usum_gen57'):
        os.rmdir(os.path.join(ea, sub))
    return dst


def main():
    xd, disc, gen4, pack, out = sys.argv[1:6]
    rows = sys.argv[6:]
    a = run(xd, disc, gen4, pack, out, rows, 'one')
    b = run(xd, disc, gen4, pack, out, rows, 'two')
    for f in sorted(os.listdir(a)):
        if not f.endswith('.png') or not os.path.exists(os.path.join(b, f)):
            continue
        x = np.asarray(Image.open(os.path.join(a, f)).convert('RGBA')).astype(int)
        y = np.asarray(Image.open(os.path.join(b, f)).convert('RGBA')).astype(int)
        d = np.abs(x - y).max(axis=2)
        print('%s: %s, differing pixels %d, largest %d' % (f, 'IDENTICAL' if not d.max() else 'different', int((d > 0).sum()), int(d.max())))


if __name__ == '__main__':
    main()
