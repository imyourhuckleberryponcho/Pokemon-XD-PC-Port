"""Run the existing game once with a converted model switched in as a file mod (development test).

  python tools/pbr_convert/test_in_game.py --exe <xd.exe> --out <folder under build> --name <run name>
         [--pkx <converted.pkx> --archive pkx_<name>.fsys [--entry "#0"]] [--data <data mod .json>]
         [--timeout 600] KEY=VALUE ...

Makes <out>/<name>/ with a settings folder of its own (Original settings.ini, mods.ini with the one test mod on,
mods/pbrconv-test/...), then starts the game through tools/linux/win_run.ps1: own card, settings, cache and data
folders, window off screen and unfocused, muted, no message boxes. KEY=VALUE pairs go to the game as
environment switches (XD_INPUT_SCRIPT, XD_SHOT_FRAMES...). Without --pkx the run is the plain game (the picture to
compare with). Everything written is game data: <out> must be under build/.
"""
import argparse, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--pkx')
    ap.add_argument('--archive')
    ap.add_argument('--entry', default='#0')
    ap.add_argument('--data', action='append', default=[])
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('pairs', nargs='*')
    a = ap.parse_args()
    run = os.path.join(os.path.abspath(a.out), a.name)
    settings = os.path.join(run, 'settings')
    mods = os.path.join(settings, 'mods')
    if os.path.isdir(mods):
        shutil.rmtree(mods)
    os.makedirs(settings, exist_ok=True)
    with open(os.path.join(settings, 'settings.ini'), 'w') as f:
        f.write('format = 1\npreset = original\n')
    ini = os.path.join(settings, 'mods.ini')
    if a.pkx or a.data:
        mod = os.path.join(mods, 'pbrconv-test')
        os.makedirs(mod)
        with open(os.path.join(mod, 'mod.json'), 'w') as f:
            json.dump({'id': 'pbrconv-test', 'name': 'Converter test', 'version': '0', 'game': 'GXXE01',
                       'applies': 'new_playthrough'}, f)
        if a.pkx:
            dst = os.path.join(mod, 'fsys', a.archive)
            os.makedirs(dst)
            shutil.copyfile(a.pkx, os.path.join(dst, a.entry))
        for d in a.data:
            os.makedirs(os.path.join(mod, 'data'), exist_ok=True)
            shutil.copyfile(d, os.path.join(mod, 'data', os.path.basename(d)))
        with open(ini, 'w') as f:
            f.write('profile = default\n\n[profile default]\npbrconv-test = on\n')
    elif os.path.exists(ini):
        os.remove(ini)
    for old in ('out.log', 'err.log', 'exit.txt'):
        p = os.path.join(run, old)
        if os.path.exists(p):
            os.remove(p)
    for pair in a.pairs:
        if pair.startswith('XD_SHOT_DIR='):
            os.makedirs(pair.split('=', 1)[1], exist_ok=True)
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(REPO, 'tools', 'linux', 'win_run.ps1'),
           a.exe, run, 'TIMEOUT=%d' % a.timeout, 'CACHE=' + os.path.join(os.path.abspath(a.out), 'cache')] + a.pairs
    r = subprocess.run(cmd)
    print('exit', r.returncode, 'logs in', run)
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
