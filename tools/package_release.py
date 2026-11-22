r"""Assembles the player's folder from a build: dist\Pokemon XD PC\ (README in launcher/README.md, "The player's folder").

    python tools/package_release.py                      # from build\release\bin
    python tools/package_release.py --build build\release\bin --out "dist\Pokemon XD PC"

The folder holds only what a player needs:

    Pokemon XD PC\
      Pokemon XD Launcher.exe      start here (starts game\xd_launcher.exe; tiny, needs no libraries)
      Read me.txt
      game\               the launcher, the game, their libraries, the launcher's font, licences

No test tools, no debug files (.pdb/.ilk), nothing from the player's data folder and nothing from a disc: the
script refuses to finish if anything else ends up in the folder. The Microsoft C++ runtime libraries are copied
next to the programs (Visual Studio's redistributable files), so no separate install is needed.
Build first (build.bat release). The old folder is replaced. Doesn't make shortcuts: the launcher has buttons
for those (Disc page), and a player's own shortcuts are theirs.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import readme_text

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDER_NAME = 'Pokemon XD PC'
PBR_MODELS = os.path.join(ROOT, 'build', 'pbr_models_dist', 'pbr_models')


USUM_MODELS = os.path.join(ROOT, 'build', 'usum_models_dist', 'usum_models')


PROGRAMS = ['xd_launcher.exe', 'xd.exe']


PIPELINE_LIST = 'initial_pipeline_cache.db'
LIBRARIES = ['SDL3.dll', 'webgpu_dawn.dll', 'nod.dll', 'dxcompiler.dll', 'dxil.dll', 'zlib1.dll', 'libpng16.dll']
RUNTIME = ['msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll',
           'vcruntime140_1.dll']
FONTS = ['Inter-Regular.ttf', 'Inter-SemiBold.ttf', 'InterDisplay-Bold.ttf', 'OFL.txt', 'NotoSansJP-Regular-subset.ttf',
         'OFL-NotoSansJP.txt']

READ_ME_TEMPLATE = r"""Pokemon XD PC port @VERSION@
========================

Public beta: thank you for trying it! Expect bugs, and keep a copy of your save now and then.
Questions, bug reports, other players: @DISCORD@

What you need
-------------
- Your own copy of Pokemon XD: Gale of Darkness (USA version, game ID GXXE01) as a disc image file
  (.iso, .rvz, .gcm, ...). Nothing from the game comes with this program, and your disc image is never changed.
- Windows 10 or 11, 64-bit, and a graphics card that supports Direct3D 12 (most cards of the last ten years). If
  Direct3D 12 doesn't work, the game tries Vulkan and then Direct3D 11 by itself; those are less tested.
  Older or laptop graphics cards are untested: please tell us how it runs on yours.

How to start
------------
1. Start "Pokemon XD Launcher.exe". It opens the launcher.
2. Disc page: choose your disc image. It is checked (a minute the first time) and remembered.
3. The first time, the launcher asks how you want to play (see the presets below) and prepares the game's
   graphics for your PC in the background; you can press Play meanwhile.
4. Press Play. Controls, your saves and bug reports have pages of their own in the launcher.

@PRESETS@
@HOWTO@
@NEW@
Where your things are kept
--------------------------
Settings, mods, the memory card (your save), logs and the icon made from your disc are kept in
    @DATA@
(type that into the address bar of a File Explorer window to open it). Deleting this program's folder
doesn't touch them.

Portable use (everything inside this folder instead, for example on a USB stick): the launcher asks at its first start
where your data should be kept, and its Save page ("Where your data is kept") moves it later, either way, without losing
anything. By hand it is a file named data_location.json next to "Pokemon XD Launcher.exe" containing
    {"version": 1, "mode": "portable"}
Your things are then kept in the "portable" folder next to it. (The same file inside the "game"
folder works too; its portable folder is then game\portable.)

Your save and its backups
-------------------------
Your first save is the memory card in
    @CARD@
one .gci file, in the same format Dolphin uses. In the launcher's Save page you can keep several saves
side by side: the circle in front of a save chooses the one the game loads, and every save after the
first one is kept in its own folder under
    @SAVES@
(Pokemon XG has a set of its own under @XG@.)
Each time the game saves, it first keeps a copy of the save as it was: the newest 10, in the "save_backups"
folder next to that save's card (for your first save: @BACKUPS@). To go back to one, open the
launcher's Save page, pick the save, choose a backup under "Backups" and press "Restore..." (with the game
closed); the save it replaces is backed up first. Still, copy the folder @CARDTOP@
somewhere safe now and then.

Settings inside the game
------------------------
- In the field menu (POKeMON / P*DA / ITEMS / SAVE / EXIT), press down on EXIT: SETTINGS, then A.
- On the title screen's menu, press left on OPTIONS: EXTRAS (ONLINE, MODS, SETTINGS), then A on SETTINGS.
While the settings page is open the game is paused completely. Changes take effect at once and are kept
(the launcher's Settings page has them too). Alt+Enter switches full screen on and off.

Playing faster (not in the "Original" preset)
---------------------------------------------
- Fast-forward: hold Tab (or click the right stick of a PC controller) to run the game 4 times as
  fast. The key, hold/toggle and the speed (2x-16x) can be changed: launcher Controls page and the
  settings page. Fast-forward doesn't change the game: battles and everything else play out exactly as
  at normal speed, only quicker.
- Text speed: Normal, 2x, 3x or Instant.
- Fast battle animations: move animations run at fast-forward speed.
Runs that used these are marked in their reports, so please leave them on if you like: just say so.

Shortcuts
---------
The launcher's Disc page has "Create Start menu shortcut" and "Create desktop shortcut". They point to
"Pokemon XD Launcher.exe" and use an icon made from your own disc. If you move this folder, make them again.

The "game" folder
-----------------
Holds the game (xd.exe), the launcher (xd_launcher.exe), the libraries they use, the launcher's fonts,
the converters (pbr_models, xd_languages) and the licences of the parts made by others (game\licenses).
Keep it next to "Pokemon XD Launcher.exe". You can also start xd.exe directly by dragging your disc image
onto it; it uses the same settings.

Updates
-------
The launcher can look for a newer version (Disc page, "Version and updates"), but only if you want it
to: it asks you once, and even then it asks again before downloading anything. A new version is
installed into its own folder next to "game" (for example "game-1.2.0"); the one you had stays, and
the Disc page can go back to it. Your saves, settings and mods are not touched by an update.

When something goes wrong
-------------------------
- Press F10 at the moment you see it (wrong picture, wrong text, odd sound, stuck). It saves a report
  with a picture and the button presses that led there, so the problem can be replayed exactly. It is
  kept in @DATA@\reports.
- After a crash or a freeze a report is made by itself (a freeze: after about a minute; close the game then).
- Open the launcher's Reports page, pick the report, press "Export..." and post the .zip file in the
  Discord's bug-reports channel (@DISCORD@), with a sentence about what you were doing.
  "Hide my Windows user name" is on by default; leave the crash dump unticked unless you are asked for it.
  Nothing is ever sent anywhere by the program itself.
- No report? (The game froze and you closed it, or it closed by itself.) Open the launcher's Reports
  page and press "Save my last play session...". It saves the last time you played as one .zip file:
  what the game wrote about itself, your button presses and your memory card. Post that file instead.
  You can also pick an older session from the list there.

@KNOWN@
Developer menu (cheats)
-----------------------
For jumping ahead in the story or testing a place: Settings page, System tab, "Developer menu
(cheats)"; then F9 in the game. Runs that used it are marked in their reports. Not available with the
Original preset.

Licence
-------
The port's own code is under the MIT licence (earlier releases stay CC0). The decomp code it builds on is CC0.
The parts made by others keep their own licences (game\licenses).
"""


def read_me(version):
    """The Windows Read me (the shared sections are in tools/readme_text.py, so the three systems read the same)."""
    rt = readme_text
    p = rt.paths('windows')
    text = READ_ME_TEMPLATE
    for key, value in (('@PRESETS@', rt.presets()), ('@HOWTO@', rt.howto('windows')), ('@NEW@', rt.new_in()),
                       ('@KNOWN@', rt.known_issues('windows')), ('@CARDTOP@', p['data'] + '\\USA'),
                       ('@CARD@', p['card']), ('@SAVES@', p['saves']), ('@XG@', p['xg']),
                       ('@BACKUPS@', p['backups']), ('@DATA@', p['data']), ('@DISCORD@', rt.DISCORD_URL),
                       ('@VERSION@', version)):
        text = text.replace(key, value)
    return text


NOTICES = """Parts made by others, used by this program
==========================================

Their licences are in this folder where the project ships them; otherwise see the links.

SDL 3                  zlib licence                         https://libsdl.org
Dear ImGui             MIT licence                          https://github.com/ocornut/imgui
Aurora                 MIT licence                          https://github.com/encounter/aurora
Borealis               see its licence                      https://github.com/encounter/borealis
Dawn (WebGPU)          BSD 3-clause licence                 https://dawn.googlesource.com/dawn
nod (disc images)      MIT / Apache 2.0                     https://github.com/encounter/nod
DirectX Shader Compiler (dxcompiler.dll, dxil.dll)  University of Illinois/NCSA licence + Microsoft
                       https://github.com/microsoft/DirectXShaderCompiler
libpng                 libpng licence                       http://www.libpng.org
zlib                   zlib licence                         https://zlib.net
fmt, abseil, nlohmann json, xxHash, zstd, FreeType   their licences (files here)
Inter font             SIL Open Font License 1.1 (fonts\\OFL.txt)   https://rsms.me/inter
Noto Sans JP font      SIL Open Font License 1.1 (fonts\\OFL-NotoSansJP.txt)   https://fonts.google.com/noto/specimen/Noto+Sans+JP
Microsoft C++ runtime (msvcp140*.dll, vcruntime140*.dll)  Visual Studio redistributable files
"""


def vc_redist():
    """Visual Studio's folder with the C++ runtime DLLs (Redist\\MSVC\\<version>\\x64\\Microsoft.VC143.CRT)."""
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    roots = []
    if os.path.isfile(vswhere):
        out = subprocess.run([vswhere, '-latest', '-property', 'installationPath'], capture_output=True, text=True)
        roots += [l.strip() for l in out.stdout.splitlines() if l.strip()]
    for root in roots:
        found = sorted(glob.glob(os.path.join(root, 'VC', 'Redist', 'MSVC', '*', 'x64', 'Microsoft.VC*.CRT')))
        if found:
            return found[-1]
    return None


def licence_files(build_bin):
    """(name in licenses\\, source file) for the third-party parts that ship a licence file."""
    build = os.path.dirname(build_bin)
    out = []
    for name, rel in (('SDL3', 'third_party/SDL/LICENSE.txt'), ('Aurora', 'third_party/aurora/LICENSE'),
                      ('Borealis', 'third_party/borealis/LICENSE')):
        p = os.path.join(ROOT, rel)
        if os.path.isfile(p):
            out.append((name + '.txt', p))
    for dep, name in (('imgui-src', 'Dear ImGui'), ('png-src', 'libpng'), ('zlib-src', 'zlib'), ('fmt-src', 'fmt'),
                      ('abseil-cpp-src', 'abseil'), ('nlohmann_json-src', 'nlohmann json'),
                      ('xxhash-src', 'xxHash'), ('zstd-src', 'zstd'), ('freetype-src', 'FreeType')):
        for cand in glob.glob(os.path.join(build, '_deps', dep, 'LICENSE*')):
            out.append((name + '.txt', cand))
            break
    return out


def project_version():
    """The version in the top CMakeLists.txt's project(... VERSION x.y.z ...)."""
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    m = re.search(r'project\(\s*\w+\s+VERSION\s+([0-9.]+)', text)
    return m.group(1) if m else '0.0.0'


def main():
    ap = argparse.ArgumentParser(description='Assemble the player folder (dist\\Pokemon XD PC) from a build.')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'release', 'bin'), help='the build\'s bin folder')
    ap.add_argument('--out', default=os.path.join(ROOT, 'dist', FOLDER_NAME))
    ap.add_argument('--version', default=None,
                    help='the version written to game\\version.txt (default: project() VERSION in CMakeLists.txt); '
                         'the launcher shows it and the updater compares with it (docs/UPDATER.md)')
    ap.add_argument('--tester', action='store_true', help='(kept for old scripts: the Read me is the same for every build)')
    a = ap.parse_args()
    version = a.version or project_version()
    if not re.fullmatch(r'\d+(\.\d+){0,3}', version):
        sys.exit(f'Not a version: {version} (numbers with dots, like 1.2.0)')
    build = os.path.abspath(a.build)
    out = os.path.abspath(a.out)
    missing = [f for f in ['Pokemon XD Launcher.exe'] + PROGRAMS + LIBRARIES + [PIPELINE_LIST]
               if not os.path.isfile(os.path.join(build, f))]
    missing += ['fonts/' + f for f in FONTS if not os.path.isfile(os.path.join(build, 'fonts', f))]
    if missing:
        sys.exit(f'Missing in {build}: {", ".join(missing)}\nBuild first: build.bat release')

    if os.path.isdir(out):
        shutil.rmtree(out)
    game = os.path.join(out, 'game')
    os.makedirs(os.path.join(game, 'fonts'))
    os.makedirs(os.path.join(game, 'licenses'))
    shutil.copy2(os.path.join(build, 'Pokemon XD Launcher.exe'), out)
    for f in PROGRAMS + LIBRARIES + [PIPELINE_LIST]:
        shutil.copy2(os.path.join(build, f), game)
    for f in FONTS:
        shutil.copy2(os.path.join(build, 'fonts', f), os.path.join(game, 'fonts'))




    crt = vc_redist()
    pbr_dir = os.path.join(game, 'pbr_models')
    if os.path.isfile(os.path.join(build, 'pbr_models.exe')):
        os.makedirs(pbr_dir)
        for f in ('pbr_models.exe', 'nod.dll'):
            shutil.copy2(os.path.join(build, f), pbr_dir)


        for f in ('SDL3.dll', 'webgpu_dawn.dll', 'libpng16.dll', 'zlib1.dll'):
            if os.path.isfile(os.path.join(build, f)):
                shutil.copy2(os.path.join(build, f), pbr_dir)
        for f in RUNTIME if crt else []:
            if os.path.isfile(os.path.join(crt, f)):
                shutil.copy2(os.path.join(crt, f), pbr_dir)
        print('pbr_models: the C++ converter (game/pbr_models)')


        if os.path.isfile(os.path.join(build, 'colo_models.exe')):
            shutil.copy2(os.path.join(build, 'colo_models.exe'), pbr_dir)
            print('colo_models: the Colosseum characters converter (game/pbr_models)')

        if os.path.isfile(os.path.join(build, 'xd_data_mods.exe')):
            shutil.copy2(os.path.join(build, 'xd_data_mods.exe'), pbr_dir)
            print('xd_data_mods: reads XD tables for the Gen 5-7 build (game/pbr_models)')
    elif os.path.isfile(os.path.join(PBR_MODELS, 'pbr_models.exe')):
        shutil.copytree(PBR_MODELS, pbr_dir)
        print('pbr_models: the frozen Python converter (game/pbr_models)')
    if os.path.isfile(os.path.join(USUM_MODELS, 'usum_models.exe')):
        shutil.copytree(USUM_MODELS, os.path.join(game, 'usum_models'))
        print('usum_models: the frozen Python converter (game/usum_models)')


    lang_dir = os.path.join(game, 'xd_languages')
    if os.path.isfile(os.path.join(build, 'xd_languages.exe')):
        os.makedirs(lang_dir)
        for f in ('xd_languages.exe', 'nod.dll'):
            shutil.copy2(os.path.join(build, f), lang_dir)
        for f in RUNTIME if crt else []:
            if os.path.isfile(os.path.join(crt, f)):
                shutil.copy2(os.path.join(crt, f), lang_dir)
        print('xd_languages: the language pack maker (game/xd_languages)')
    if crt:
        for f in RUNTIME:
            if os.path.isfile(os.path.join(crt, f)):
                shutil.copy2(os.path.join(crt, f), game)
    else:
        print('note: Visual Studio\'s C++ runtime files not found; players then need the "Microsoft Visual C++ '
              'Redistributable" installed')
    for name, src in licence_files(build):
        shutil.copy2(src, os.path.join(game, 'licenses', name))
    shutil.copy2(os.path.join(ROOT, 'third_party', 'fonts', 'inter', 'OFL.txt'),
                 os.path.join(game, 'licenses', 'Inter font (OFL).txt'))
    shutil.copy2(os.path.join(ROOT, 'third_party', 'fonts', 'noto', 'OFL.txt'),
                 os.path.join(game, 'licenses', 'Noto Sans JP font (OFL).txt'))
    with open(os.path.join(game, 'licenses', 'Third-party notices.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(NOTICES)


    with open(os.path.join(game, 'version.txt'), 'w', encoding='ascii', newline='\r\n') as f:
        f.write(version + '\n')


    shutil.copy2(os.path.join(ROOT, 'recomplauncher.json'), out)
    with open(os.path.join(out, 'Read me.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(read_me(version))


    allowed_top = {'Pokemon XD Launcher.exe', 'Read me.txt', 'recomplauncher.json', 'game'}
    allowed_ext = {'.exe', '.dll', '.ttf', '.txt'}
    allowed_exe = {'Pokemon XD Launcher.exe', 'xd.exe', 'xd_launcher.exe'}
    bad = [n for n in os.listdir(out) if n not in allowed_top]
    total = 0
    for dirpath, _, files in os.walk(out):
        for n in files:
            p = os.path.join(dirpath, n)
            total += os.path.getsize(p)
            ext = os.path.splitext(n)[1].lower()
            if n == 'recomplauncher.json' and os.path.dirname(p) == out:
                continue
            if n == PIPELINE_LIST and os.path.dirname(p) == game:
                continue
            if os.path.relpath(p, game).startswith('xd_languages' + os.sep) and n in ('xd_languages.exe', 'nod.dll') + tuple(RUNTIME):
                continue
            if os.path.relpath(p, game).startswith('usum_models' + os.sep):
                continue
            if os.path.relpath(p, game).startswith('pbr_models' + os.sep):
                continue
            if ext not in allowed_ext or (ext == '.exe' and n not in allowed_exe):
                bad.append(os.path.relpath(p, out))
    if bad:
        sys.exit('Unexpected files in the player folder: ' + ', '.join(bad))
    print(f'{out}: version {version}, {sum(len(f) for _, _, f in os.walk(out))} files, {total / 1e6:.1f} MB')
    if not crt:
        print('  (without the C++ runtime files)')




    symbols = os.path.join(ROOT, 'build', 'symbols', version)
    os.makedirs(symbols, exist_ok=True)
    kept = 0
    for n in os.listdir(a.build):
        if os.path.splitext(n)[1].lower() in ('.pdb', '.exe', '.dll'):
            shutil.copy2(os.path.join(a.build, n), os.path.join(symbols, n))
            kept += 1
    print(f'  symbols: {kept} files kept in {symbols}')


if __name__ == '__main__':
    main()
