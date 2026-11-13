"""Builds build/pbr_models_dist/pbr_models/pbr_models(.exe): pbr_models.py frozen with PyInstaller (one-folder mode:
fewer antivirus false alarms and a faster start than one-file), with the disc library (nod.dll) copied next to it.
No Python needed to run it.

    python tools/pbr_convert/package_pbr_models.py [--nod <library>] [--dist <folder>] [--venv <folder>]

Windows: build/pbr_models_dist/pbr_models/pbr_models.exe + nod.dll.
Linux and Mac: the same script, run ON that system (PyInstaller cannot cross-build; WSL for Linux, the Mac workflow
for the Mac): build/pbr_models_dist_linux/pbr_models/pbr_models + libnod.so, or ..._mac with libnod.dylib. --nod names
the library; by default it is taken from the Linux / Mac build's _deps/nod_prebuilt-src (lib or bin).

PyInstaller lives in a venv under build/pbr_models_venv[_linux|_mac] (made on first run; numpy and Pillow are what
the converter needs). The result is a program, no game data, but it stays under build/ like other build output.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# The main checkout when this one is a git worktree (<main>/<tools folder>/worktrees/<name>): its build folder has the nod
# library when this tree was not built itself. XD_MAIN_ROOT overrides.
MAIN_ROOT = os.environ.get('XD_MAIN_ROOT') or (
    os.path.dirname(os.path.dirname(os.path.dirname(ROOT))) if os.path.basename(os.path.dirname(ROOT)) == 'worktrees'
    else ROOT)
BUILD = os.path.join(ROOT, 'build')
# WHY a suffix per system: the Windows, Linux (WSL) and Mac outputs can live in one shared build folder and must not
# overwrite each other; the Windows names stay as they were.
SUFFIX = '' if sys.platform == 'win32' else '_mac' if sys.platform == 'darwin' else '_linux'
VENV = os.path.join(BUILD, 'pbr_models_venv' + SUFFIX)
WORK = os.path.join(BUILD, 'pbr_models_work' + SUFFIX)
DIST = os.path.join(BUILD, 'pbr_models_dist' + SUFFIX)
LIB = 'nod.dll' if sys.platform == 'win32' else 'libnod.dylib' if sys.platform == 'darwin' else 'libnod.so'
EXCLUDES = ['tkinter', 'scipy', 'matplotlib', 'pandas', 'IPython', 'pytest', 'unittest', 'test', 'setuptools',
            'pip', 'distutils', 'pydoc', 'doctest', 'lib2to3', 'sqlite3', 'PyQt5', 'PyQt6', 'PySide6',
            'PIL.ImageTk', 'PIL._avif', 'PIL._webp', 'PIL._imagingcms', 'PIL._imagingtk', 'ssl', '_ssl', 'bz2', 'lzma', 'ctypes.test', 'curses', 'asyncio', 'email', 'http', 'html', 'xmlrpc']


def run(cmd, **kw):
    print('>', ' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def find_nod():
    cands = []
    for base in (ROOT, MAIN_ROOT):
        if sys.platform == 'win32':
            cands.append(os.path.join(base, 'build', 'release', 'bin', LIB))
        else:
            # build.sh: build/linux/linux-release, build_mac.sh: build/mac/mac-release; Aurora unpacks the prebuilt
            # nod package under _deps (the library itself is in lib/).
            for tree in ('linux/linux-release', 'mac/mac-release'):
                for sub in ('_deps/nod_prebuilt-src/lib', 'bin', '_deps/nod_prebuilt-src/bin'):
                    cands.append(os.path.join(base, 'build', tree, sub, LIB))
    return next((c for c in cands if os.path.isfile(c)), None)


def main():
    global DIST, VENV
    ap = argparse.ArgumentParser()
    ap.add_argument('--nod', default=None, help='the disc library to ship (%s)' % LIB)
    ap.add_argument('--dist', default=None, help='output folder (default build/pbr_models_dist%s)' % SUFFIX)
    ap.add_argument('--venv', default=None, help='PyInstaller venv (default build/pbr_models_venv%s)' % SUFFIX)
    a = ap.parse_args()
    if a.dist:
        DIST = os.path.abspath(a.dist)
    if a.venv:
        VENV = os.path.abspath(a.venv)
    nod = a.nod or find_nod()
    if not nod or not os.path.isfile(nod):
        sys.exit('%s not found: build the port first or pass --nod' % LIB)
    py = os.path.join(VENV, 'Scripts', 'python.exe') if sys.platform == 'win32' else os.path.join(VENV, 'bin', 'python')
    if not os.path.isfile(py):
        run([sys.executable, '-m', 'venv', VENV])
        run([py, '-m', 'pip', 'install', '--quiet', 'pyinstaller', 'numpy', 'pillow'])
    for d in (WORK, os.path.join(DIST, 'pbr_models')):
        if os.path.isdir(d):
            shutil.rmtree(d)
    cmd = [py, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'pbr_models',
           '--distpath', DIST, '--workpath', WORK, '--specpath', WORK,
           '--paths', HERE, '--paths', os.path.join(os.path.dirname(HERE), 'pbr_survey'),
           '--hidden-import', 'pbr_to_xd', '--hidden-import', 'noddisc']
    for m in EXCLUDES:
        cmd += ['--exclude-module', m]
    cmd.append(os.path.join(HERE, 'pbr_models.py'))
    run(cmd)
    out = os.path.join(DIST, 'pbr_models')
    shutil.copy2(nod, os.path.join(out, LIB))
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print('%s: %.1f MB, %d files' % (out, total / 1e6, sum(len(f) for _, _, f in os.walk(out))))


if __name__ == '__main__':
    main()
