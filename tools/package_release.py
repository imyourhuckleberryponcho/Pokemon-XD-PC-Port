r"""Assembles the player's folder from a build: dist\Pokemon XD PC\ (README in launcher/README.md, "The player's folder").

    python tools/package_release.py                      # from build\release\bin
    python tools/package_release.py --build build\release\bin --out "dist\Pokemon XD PC"

The folder holds only what a player needs:

    Pokemon XD PC\
      Pokemon XD.exe      start here (starts game\xd_launcher.exe; tiny, needs no libraries)
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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDER_NAME = 'Pokemon XD PC'

# The programs and libraries in game\ (next to each other: xd.exe and xd_launcher.exe load the same ones).
PROGRAMS = ['xd_launcher.exe', 'xd.exe']
# The shipped pipeline list (games/xd/shaders/pipelines.txt, built by tools/pipeline_list.py): drawing settings
# only, no game data. Aurora reads it next to xd.exe; the launcher prepares those shaders once per PC.
PIPELINE_LIST = 'initial_pipeline_cache.db'
LIBRARIES = ['SDL3.dll', 'webgpu_dawn.dll', 'nod.dll', 'dxcompiler.dll', 'dxil.dll', 'zlib1.dll', 'libpng16.dll']
RUNTIME = ['msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll',
           'vcruntime140_1.dll']
FONTS = ['Inter-Regular.ttf', 'Inter-SemiBold.ttf', 'InterDisplay-Bold.ttf', 'OFL.txt']

READ_ME = """Pokemon XD PC port
==================

Start "Pokemon XD.exe". It opens the launcher: choose your disc image, how you want to play, your
controls and mods, then press Play.

You need your own copy of Pokemon XD: Gale of Darkness (USA version, game ID GXXE01) as a disc image
file (.iso, .rvz, .gcm, ...). Nothing from the game comes with this program, and your disc image is
never changed.

The "Original" preset plays exactly like the GameCube game. Every improvement is a setting you can
switch on in the launcher (Settings page).

Where your things are kept
--------------------------
Settings, mods, the memory card (your save), logs and the icon made from your disc are kept in
    %APPDATA%\\Pokemon XD
(type that into the address bar of a File Explorer window to open it). Deleting this program's folder
doesn't touch them.

Portable use (everything inside this folder instead, for example on a USB stick): put a file named
data_location.json next to "Pokemon XD.exe" containing
    {"version": 1, "mode": "portable"}
Your things are then kept in the "portable" folder next to it. (The same file inside the "game"
folder works too; its portable folder is then game\\portable.)

Shortcuts
---------
The launcher's Disc page has "Create Start menu shortcut" and "Create desktop shortcut". They point to
"Pokemon XD.exe" and use an icon made from your own disc. If you move this folder, make them again.

The "game" folder
-----------------
Holds the game (xd.exe), the launcher (xd_launcher.exe), the libraries they use, the launcher's font
and the licences of the parts made by others (game\\licenses). Keep it next to "Pokemon XD.exe".
You can also start xd.exe directly by dragging your disc image onto it; it uses the same settings.

Updates
-------
The launcher can look for a newer version (Disc page, "Version and updates"), but only if you want it
to: it asks you once, and even then it asks again before downloading anything. A new version is
installed into its own folder next to "game" (for example "game-1.2.0"); the one you had stays, and
the Disc page can go back to it. Your saves, settings and mods are not touched by an update.

Problems
--------
In the game, F10 writes a bug report (screenshot, log, settings, the button presses that led there) into
%APPDATA%\\Pokemon XD\\reports. A crash or a freeze writes one by itself. The launcher's Reports page
lists them; "Export..." saves one as a .zip file, with a name and in a place you choose, to send
yourself. Nothing is ever sent anywhere by the program.
"""

# Appended for test builds (--tester): what a tester needs to know.
TESTER_NOTES = """
Public beta {version}
=====================
Thank you for trying it! This is a beta: expect bugs, and keep a copy of your save.
Questions, bug reports, other players: https://discord.gg/XVbDVXH4Pn

New in 0.3.6 (fixes after the first days in public)
---------------------------------------------------
- Mac: the game stopped after a few minutes of play ("no memory below 2 GB for a task's stack"). Its
  working memory is now kept for it from the start. Quitting no longer ends in a crash message.
- Disc images of the shrunk ".nkit.iso" kind no longer get the "content is different from a known-good
  copy" warning: the game says that such a copy cannot be compared, and starts.
- The log no longer says "STUCK" while a message window is waiting for you.

New in 0.3.5
------------
- Several saves and a save manager: the launcher's Save page keeps more than one playthrough. The circle
  in front of a save chooses the one the game loads; click a save to see what is on it, rename it, look
  at its backups and restore one.
- Slow PCs: the sound engine needs about a quarter of the processor time it did; the sound holds up
  better when pictures take long; fast-forward switches itself off (with a line on the screen) when the
  PC cannot keep up. Play logs now say how hard the PC is working: if the game runs slowly on yours,
  a report (F10) after a few minutes of play helps us a lot.
- Sound: the GameCube sound chip's built-in routines were compared with a real console's and are equal.
- More of the GameCube's own timing is modelled (how long the console takes to load and set up a scene).
  It is NOT switched on yet: it stays off until it is proven.
- Linux and Mac builds exist as separate downloads (Linux: test build; Mac: untested).

Earlier (0.3.0)
---------------
- New Game setup screen (not in the "Original" preset): after the game's NEW GAME you choose how this
  playthrough is played - Faithful or Enhanced, a randomizer, challenge rules. The choice is kept with
  the save. If a save's choice and your settings differ later, CONTINUE asks which one you want.
- Randomizer: Light, Classic or Chaos, or Custom with every switch its own row (wild Pokemon, trainers,
  Shadow Pokemon, items, which move each TM teaches, trainers' levels / held items / natures ...). Each
  randomized game has a code you can share: the same code makes the same game. The game starts again
  by itself once when you pick one.
- Challenge runs: "No items in battle", Nuzlocke ("fainted is lost": it goes to the last PC box and
  can't come back; one catch per area) and level caps (no experience past the next boss's highest level).
- Frame rate 60 (Settings; 30 is the default): in-between pictures make 3D motion smoother. The game
  itself still runs exactly as at 30. Menus and text are not smoothed yet.
- Fixed: memory use now follows the GameCube's in more places (four real bugs found on the way).
Known in the new parts: a few randomized TM descriptions are cut off at the box's edge.

Earlier (0.2.2)
---------------
- Fixed: a Shadow Pokemon snagged from a trainer (Teddiursa at the HQ Lab) vanished after the battle
  instead of joining your party or PC box.
- Fixed: objects, people or the sky missing the first time you saw a place (fine after a restart). The
  launcher now prepares the game's shaders once for your PC when you choose the disc: the first time this
  takes a few minutes (progress bar on the Disc page; Play waits for it). After a graphics driver update
  or a new version it runs again.
- Faster on PCs with few processor cores (laptops): the game no longer keeps a whole core busy while it
  waits for the picture. Each play log now notes how hard the graphics chip works: if the game runs
  slowly, please send a report (F10) after a few minutes of play.
- Widescreen: menus and the battle boxes that sit at the screen's sides now sit at the sides of the wide
  picture; people at the wide sides no longer vanish; the opening's starry background is fixed.
- Reports exported from the launcher are about 8 times smaller.
- Settings: a new settings screen in the game's own style on the title screen, the game's own menu
  sounds, music fades while a settings page is open, and button pictures for Xbox / PlayStation
  controllers and the keyboard (Settings, Controls).

Earlier (0.2.1)
---------------
- Fixed: the game could freeze for good while drawing (a bug in the graphics library on Direct3D 12).
- Fixed: a crash when a menu was drawn while the game changed places.
- Fixed: a crash when a wild Pokemon fainted at the Cave Poke Spot (Spinda).
- Fixed: healing items such as Antidote cured the wrong conditions (Antidote cured burns).
- Fixed: one of the game's echo (reverb) effects was never switched on.
- Separate volumes for music, sound effects and Pokemon cries (Settings).
- The launcher's Reports page has "Save my last play session...": use it when the game froze or was
  closed without a report.
- The game's own data tables are now read from your disc instead of being built into the program.

What your PC needs
------------------
- Windows 10 or 11, 64-bit.
- A graphics card that supports Direct3D 12 (most cards of the last ten years). If Direct3D 12 doesn't work,
  the game tries Vulkan and then Direct3D 11 by itself; those are less tested. Without any of them the
  game can't show a picture.
- Older or laptop graphics cards are untested: please tell us how it runs on yours.

Your save and its backups
-------------------------
Each time the game saves, it first keeps a copy of your save as it was: the newest 10, in
%APPDATA%\\Pokemon XD\\save_backups. To go back to one, open the launcher's Save page, pick it under
"Backups" and press "Restore..." (with the game closed); the save it replaces is backed up first. Still,
copy the folder %APPDATA%\\Pokemon XD\\USA somewhere safe now and then.

Settings inside the game
------------------------
- In the field menu (POKeMON / P*DA / ITEMS / SAVE / EXIT), press down on EXIT: SETTINGS, then A.
- On the title screen's menu, press left on OPTIONS: SETTINGS, then A.
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

When something goes wrong
-------------------------
- Press F10 at the moment you see it (wrong picture, wrong text, odd sound, stuck). It saves a report
  with a picture and the button presses that led there, so the problem can be replayed exactly.
- After a crash or a freeze a report is made by itself.
- Open the launcher's Reports page, pick the report, press "Export..." and post the .zip file in the
  Discord's bug-reports channel (https://discord.gg/XVbDVXH4Pn), with a sentence about what you were doing. "Hide my Windows user name" is on by default; leave the crash
  dump unticked unless you are asked for it.
- No report? (The game froze and you closed it, or it closed by itself.) Open the launcher's Reports
  page and press "Save my last play session...". It saves the last time you played as one .zip file:
  what the game wrote about itself, your button presses and your memory card. Post that file instead.
  You can also pick an older session from the list there.

Known issues in this build
--------------------------
- Timing is a few frames off the GameCube in some loads and scene starts (not visible in normal play;
  it matters for speedruns). The "Original" preset is not yet frame-exact.
- Sound is very close to the GameCube, but not bit-for-bit identical.
- Widescreen (an improvement, off in Original): people standing in the wide side areas keep still until
  they come back into the middle (the GameCube never animates them there); some lists and popups don't
  move to the sides yet. Please press F10 when something looks wrong.
- One tester got stuck at the HQ Lab when Professor Krane should hand over the Snag Machine (new game).
  We could not make it happen yet: if it happens to you, press F10 right there and send the report.
- If the game ever stops responding, it saves a report by itself after a minute (close the game then).
  Please send that report, or use the Reports page's "Save my last play session...".
- Many improvements in the Settings page say "not yet": they are planned and do nothing so far.

Developer menu (cheats)
-----------------------
For jumping ahead in the story or testing a place: Settings page, System tab, "Developer menu
(cheats)"; then F9 in the game. Runs that used it are marked in their reports. Not available with the
Original preset.
"""

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
    ap.add_argument('--tester', action='store_true',
                    help='a test build: the Read me gets the testers\' notes (known issues, how to report)')
    a = ap.parse_args()
    version = a.version or project_version()
    if not re.fullmatch(r'\d+(\.\d+){0,3}', version):
        sys.exit(f'Not a version: {version} (numbers with dots, like 1.2.0)')
    build = os.path.abspath(a.build)
    out = os.path.abspath(a.out)
    missing = [f for f in ['Pokemon XD.exe'] + PROGRAMS + LIBRARIES + [PIPELINE_LIST]
               if not os.path.isfile(os.path.join(build, f))]
    missing += ['fonts/' + f for f in FONTS if not os.path.isfile(os.path.join(build, 'fonts', f))]
    if missing:
        sys.exit(f'Missing in {build}: {", ".join(missing)}\nBuild first: build.bat release')

    if os.path.isdir(out):
        shutil.rmtree(out)
    game = os.path.join(out, 'game')
    os.makedirs(os.path.join(game, 'fonts'))
    os.makedirs(os.path.join(game, 'licenses'))
    shutil.copy2(os.path.join(build, 'Pokemon XD.exe'), out)
    for f in PROGRAMS + LIBRARIES + [PIPELINE_LIST]:
        shutil.copy2(os.path.join(build, f), game)
    for f in FONTS:
        shutil.copy2(os.path.join(build, 'fonts', f), os.path.join(game, 'fonts'))
    crt = vc_redist()
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
    with open(os.path.join(game, 'licenses', 'Third-party notices.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(NOTICES)
    # The version, next to the programs: the launcher shows it, and its updater compares the published one with it
    # (an update is a new game-<version> folder beside this one: docs/UPDATER.md).
    with open(os.path.join(game, 'version.txt'), 'w', encoding='ascii', newline='\r\n') as f:
        f.write(version + '\n')
    with open(os.path.join(out, 'Read me.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(READ_ME)
        if a.tester:
            f.write(TESTER_NOTES.format(version=version))

    # Nothing else may be in it: no debug files, test tools, data folder files, disc pictures or game data.
    allowed_top = {'Pokemon XD.exe', 'Read me.txt', 'game'}
    allowed_ext = {'.exe', '.dll', '.ttf', '.txt'}
    allowed_exe = {'Pokemon XD.exe', 'xd.exe', 'xd_launcher.exe'}
    bad = [n for n in os.listdir(out) if n not in allowed_top]
    total = 0
    for dirpath, _, files in os.walk(out):
        for n in files:
            p = os.path.join(dirpath, n)
            total += os.path.getsize(p)
            ext = os.path.splitext(n)[1].lower()
            if n == PIPELINE_LIST and os.path.dirname(p) == game:
                continue
            if ext not in allowed_ext or (ext == '.exe' and n not in allowed_exe):
                bad.append(os.path.relpath(p, out))
    if bad:
        sys.exit('Unexpected files in the player folder: ' + ', '.join(bad))
    print(f'{out}: version {version}, {sum(len(f) for _, _, f in os.walk(out))} files, {total / 1e6:.1f} MB')
    if not crt:
        print('  (without the C++ runtime files)')
    # The symbols of exactly this build, for testers' crash dumps: build\symbols\<version> (never in the player
    # folder). Kept by this tool since 0.3.0, whose own were lost when build\release was built again before anyone
    # copied them (they were rebuilt from the tag: build\symbols\0.3.0\NOTE.txt). A folder that is already
    # there is replaced: the player folder made last is the one that ships.
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
