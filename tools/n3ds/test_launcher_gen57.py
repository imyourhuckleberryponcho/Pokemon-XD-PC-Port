r"""The launcher making the Gen 5-7 pack from the player's own Ultra Sun / Ultra Moon dump, scripted (docs/USUM_MODELS.md "The Gen 5-7 pack too").

  python tools/n3ds/test_launcher_gen57.py --bin <bin folder with xd_launcher.exe> --game-exe <xd.exe> --converter <usum_models folder>
         --pbr-models <pbr_models.exe> --out <folder> --disc <XD iso> --dump <Ultra Sun .cci> --phase warn|start|resume|again|full

Builds a game folder under <out>\game (the launcher, the game program, the converters side by side, as the package has them), an
own data folder <out>\data (APPDATA, XD_SETTINGS_DIR: never the player's) and runs the scripted launcher:
  warn    the XD disc, the dump, Make: the "this takes a while" question (a picture), then "Not now"; nothing made
  start   Make + the question answered, a picture of each stage as it is reached, then the launcher is CLOSED while it works (a stop)
  resume  a new launcher on the same data folder: the page says "Continue" (a picture), Make, the run goes on to the end (pictures)
  again   a third start: the page says ready, Make asks nothing and the converter does nothing (the packs' files are compared)
  full    warn + start + resume in one run, without the stop (a clean run to the end)
Own XD_LAUNCHER_SCRIPT; every run muted and off screen. Pictures in <out>\shots.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import savepoints

KEEP = ('.dll', '.exe', '.db')


def assemble(a, game):
    if not os.path.isdir(game):
        os.makedirs(game)
        for f in os.listdir(a.bin):
            p = os.path.join(a.bin, f)
            if os.path.isfile(p) and f.lower().endswith(KEEP) and not f.endswith('_test.exe') and f != 'pbr_models_standin.exe' and f != 'xd.exe':
                shutil.copy2(p, game)
        shutil.copytree(os.path.join(a.bin, 'fonts'), os.path.join(game, 'fonts'))
        shutil.copy2(a.game_exe, os.path.join(game, 'xd.exe'))
        db = os.path.join(os.path.dirname(a.game_exe), 'initial_pipeline_cache.db')
        if os.path.isfile(db):
            shutil.copy2(db, game)
        os.makedirs(os.path.join(game, 'pbr_models'))
        for f in ('pbr_models.exe', 'xd_data_mods.exe', 'nod.dll', 'SDL3.dll', 'webgpu_dawn.dll', 'libpng16.dll', 'zlib1.dll'):
            src = os.path.join(a.bin, f)
            if f == 'pbr_models.exe':
                src = a.pbr_models
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(game, 'pbr_models'))
        shutil.copytree(a.converter, os.path.join(game, 'usum_models'))


def run_launcher(a, steps, log_name, timeout):
    out = os.path.abspath(a.out)
    game = os.path.join(out, 'game')
    data = os.path.join(out, 'appdata', 'Pokemon XD')
    shots = os.path.join(out, 'shots')
    os.makedirs(shots, exist_ok=True)
    os.makedirs(data, exist_ok=True)
    ini = os.path.join(data, 'settings.ini')
    if not os.path.exists(ini):
        with open(ini, 'w', encoding='utf-8') as f:
            f.write('format = 1\npreset = original\n')
    env = savepoints.base_env('launcher gen57', os.path.join(out, 'unused_card'))
    env.pop('XD_CARD_DIR', None)
    env.update({'APPDATA': os.path.join(out, 'appdata'), 'XD_SETTINGS_DIR': data, 'XD_LAUNCHER_RENDER': '0', 'XD_LAUNCHER_SHADERS': '0', 'XD_WINDOW_OFFSCREEN': '1',
                'XD_SKIP_DISC_HASH': '1'})
    env['XD_LAUNCHER_SCRIPT'] = '|'.join(s.replace('%S%', shots) for s in steps)
    log = os.path.join(out, log_name)
    t = time.time()
    code = savepoints.run([os.path.join(game, 'xd_launcher.exe')], env, log, timeout)
    print('%s: exit %s, %.0f s' % (log_name, code, time.time() - t))
    return log


def tree_state(folder):
    r = {}
    for d, _, fs in os.walk(folder):
        for f in fs:
            if f.endswith('_version.txt') or f.endswith('.log'):
                continue
            p = os.path.join(d, f)
            r[os.path.relpath(p, folder)] = (os.path.getsize(p), os.path.getmtime(p))
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', default='')
    ap.add_argument('--game-exe', default='')
    ap.add_argument('--converter', default='')
    ap.add_argument('--pbr-models', default='')
    ap.add_argument('--game-folder', default='', help='a packaged game folder (package_release.py): used as it is instead of assembling one from --bin ...')
    ap.add_argument('--out', required=True)
    ap.add_argument('--disc', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--phase', required=True, choices=['warn', 'start', 'resume', 'again', 'full'])
    ap.add_argument('--timeout', type=int, default=6 * 3600)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    if a.game_folder:
        if not os.path.isdir(os.path.join(out, 'game')):
            shutil.copytree(a.game_folder, os.path.join(out, 'game'))
    else:
        assemble(a, os.path.join(out, 'game'))
    S = '%S%/'
    setup = ['disc:' + a.disc, 'wait:disc', 'update:no', 'page:extra', 'size:1100x1400', 'extratab:models', 'extradisc:usum=' + a.dump, 'wait:extradisc']
    stages = ['base', 'models', 'forms', 'faces', 'pictures', 'check']
    if a.phase == 'warn':
        run_launcher(a, setup + ['shot:' + S + '1_card_before.png', 'extramake:usum', 'page:extra', 'page:extra', 'shot:' + S + '2_warning.png', 'quit'], 'warn.log', 600)
    elif a.phase in ('start', 'full'):
        steps = setup + ['shot:' + S + '1_card_before.png', 'extramake:usum', 'page:extra', 'page:extra', 'shot:' + S + '2_warning.png', 'extraconfirm:usum']
        for st in stages:
            steps += ['wait:extrastage=' + st, 'shot:' + S + '3_stage_' + st + '.png']
        if a.phase == 'start':
            steps = setup + ['extramake:usum', 'extraconfirm:usum', 'wait:extrastage=models', 'wait:extraprogress', 'shot:' + S + '3_stage_models.png', 'quit']
        else:
            steps += ['wait:extra', 'shot:' + S + '4_done.png', 'quit']
        run_launcher(a, steps, a.phase + '.log', a.timeout)
    elif a.phase == 'resume':
        steps = setup + ['shot:' + S + '5_card_unfinished.png', 'extramake:usum', 'page:extra', 'page:extra', 'shot:' + S + '6_warning_again.png', 'extraconfirm:usum']
        for st in stages[1:]:
            steps += ['wait:extrastage=' + st, 'shot:' + S + '7_stage_' + st + '.png']
        steps += ['wait:extra', 'shot:' + S + '8_done.png', 'quit']
        run_launcher(a, steps, 'resume.log', a.timeout)
    elif a.phase == 'again':
        data = os.path.join(out, 'appdata', 'Pokemon XD', 'extra_assets')
        before = tree_state(data)
        run_launcher(a, setup + ['shot:' + S + '9_card_ready.png', 'extramake:usum', 'wait:extra', 'shot:' + S + '9_second_start.png', 'quit'], 'again.log', 600)
        after = tree_state(data)
        print('second start: %d files before, %d after, %s' % (len(before), len(after), 'NOTHING CHANGED' if before == after else 'CHANGED: %s' % sorted(set(before) ^ set(after))[:10]))


if __name__ == '__main__':
    main()
