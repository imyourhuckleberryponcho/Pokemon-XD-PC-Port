"""Builds build/usum_models_dist[_linux|_mac]/usum_models/usum_models(.exe): usum_models.py (the Ultra Sun / Ultra Moon model
converter, docs/USUM_MODELS.md) frozen with PyInstaller in one-folder mode, with the disc library (nod.dll) next to it, so a
player needs no Python. The launcher's Extra assets page runs it (game/usum_models/usum_models).

    python tools/n3ds/package_usum_models.py [--nod <library>] [--dist <folder>] [--venv <folder>]

It is the same recipe as tools/pbr_convert/package_pbr_models.py (that script's venv, with PyInstaller, numpy and Pillow,
is reused when it is there): run it ON each system (PyInstaller cannot cross-build): Windows, WSL for Linux, the Mac workflow
for the Mac. The result is a program, no game data; it stays under build/ like other build output.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'pbr_convert'))
import package_pbr_models as PP

SUFFIX = PP.SUFFIX
BUILD = os.path.join(ROOT, 'build')
WORK = os.path.join(BUILD, 'usum_models_work' + SUFFIX)
DIST = os.path.join(BUILD, 'usum_models_dist' + SUFFIX)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nod', default=None, help='the disc library to ship (%s)' % PP.LIB)
    ap.add_argument('--dist', default=None, help='output folder (default build/usum_models_dist%s)' % SUFFIX)
    ap.add_argument('--venv', default=None, help='PyInstaller venv (default: the one of package_pbr_models.py)')
    a = ap.parse_args()
    dist = os.path.abspath(a.dist) if a.dist else DIST
    venv = os.path.abspath(a.venv) if a.venv else PP.VENV
    nod = a.nod or PP.find_nod()
    if not nod or not os.path.isfile(nod):
        sys.exit('%s not found: build the port first or pass --nod' % PP.LIB)
    py = os.path.join(venv, 'Scripts', 'python.exe') if sys.platform == 'win32' else os.path.join(venv, 'bin', 'python')
    if not os.path.isfile(py):
        PP.run([sys.executable, '-m', 'venv', venv])
        PP.run([py, '-m', 'pip', 'install', '--quiet', 'pyinstaller', 'numpy', 'pillow'])
    for d in (WORK, os.path.join(dist, 'usum_models')):
        if os.path.isdir(d):
            shutil.rmtree(d)
    cmd = [py, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'usum_models',
           '--distpath', dist, '--workpath', WORK, '--specpath', WORK,
           '--paths', HERE, '--paths', os.path.join(ROOT, 'tools', 'pbr_convert'), '--paths', os.path.join(ROOT, 'tools', 'pbr_survey'),
           '--hidden-import', 'pbr_to_xd', '--hidden-import', 'noddisc',
           '--hidden-import', 'usum_gen4', '--hidden-import', 'usum_gen4_values', '--hidden-import', 'usum_gen4_learnsets', '--hidden-import', 'usum_gen4_donors',

           '--hidden-import', 'usum_gen57_make', '--hidden-import', 'usum_dex_entries', '--hidden-import', 'gen57_all', '--hidden-import', 'gen57_forms', '--hidden-import', 'gen57_faces',
           '--hidden-import', 'gen57_faces_fast', '--hidden-import', 'gen57_facecam', '--hidden-import', 'gen57_data', '--hidden-import', 'gen57_pack',
           '--hidden-import', 'gen57_moves', '--hidden-import', 'gen57_move_plan', '--hidden-import', 'gen57_abilities',
           '--hidden-import', 'check_pack_move_rows', '--hidden-import', 'check_pack_models', '--hidden-import', 'n3ds_cries',
           '--hidden-import', 'gen4_pack', '--hidden-import', 'gen4_cry', '--hidden-import', 'gen4_spots', '--hidden-import', 'gen4_icon',
           '--hidden-import', 'gen4_text_i18n', '--hidden-import', 'make_proto', '--hidden-import', 'gf_to_xd', '--hidden-import', 'gf_pose',
           '--hidden-import', 'hsd_read', '--hidden-import', 'model', '--hidden-import', 'concurrent.futures.process',

           '--add-data', os.path.join(HERE, 'usum_dex_map.json') + os.pathsep + '.',

           '--add-data', os.path.join(HERE, 'usum_gen4_plan.json') + os.pathsep + '.']

    for f in sorted(os.listdir(HERE)):
        if (f.startswith('gen57_') and f.endswith('.json')) or f == 'dexnames.txt':
            cmd += ['--add-data', os.path.join(HERE, f) + os.pathsep + '.']
    cmd += ['--add-data', os.path.join(ROOT, 'tools', 'pbr_convert', 'gen4_text_i18n.tsv') + os.pathsep + '.']
    for m in PP.EXCLUDES:
        cmd += ['--exclude-module', m]
    cmd.append(os.path.join(HERE, 'usum_models.py'))
    PP.run(cmd)
    out = os.path.join(dist, 'usum_models')
    shutil.copy2(nod, os.path.join(out, PP.LIB))
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print('%s: %.1f MB, %d files' % (out, total / 1e6, sum(len(f) for _, _, f in os.walk(out))))


if __name__ == '__main__':
    main()
