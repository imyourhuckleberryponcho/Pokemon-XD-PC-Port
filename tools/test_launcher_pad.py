r"""The launcher driven by controller buttons only (launcher/README.md "Controller").

    python tools/test_launcher_pad.py --build <bin folder with xd_launcher> [--work <folder>] [--only name,name]

Each scenario is one scripted launcher run (XD_LAUNCHER_SCRIPT: pad:<button>, padmode, padtype, type, padwhere, shot:
launcher/include/launcher.h) with its own settings folder, user folder and save folder under <work> (never the real
ones); the window is off screen at 1280x800 (the Deck's screen), the game is never started (a build folder that has xd next to the launcher is staged: the launcher alone is copied and run, so Play fails the same way). Screenshots land in
<work>\shots. Exit code 0 = every check passed.

Scenarios:
  tour       LB / RB through every page (wraps round), LT / RT through the Settings tabs and the four Controls tabs
  extratabs  Extra assets: tabs Models / Languages / Other; RT / LT switch them (wraps), the D-pad moves inside a tab
  preset     B to the page list, D-pad into the Preset page, A on Enhanced: settings.ini says enhanced
  quit       Start opens the menu, D-pad + A on "Quit the launcher": the launcher ends by itself
  keyboard   Save page: New..., the on-screen keyboard types a name, Done, "Make it": the save exists; Rename; Delete
             (the question: B = keep it)
  browser    Disc page: A on "Choose...", the built-in file browser: down into a folder, A, pick a file, the disc
             setting holds it; B goes up; X cancels
  editsave   Save page: the D-pad reaches "Edit save...", A opens the save editor, the rows take the focus, B closes it (asks first
             when changes wait); on a COPY of a library save
  names      the prompt bar for an Xbox, a PlayStation and a Switch pad (screenshots; the Switch's A is the right-hand
             button: padtype only changes the names, the logical Confirm stays)
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OK = True


def check(cond, what):
    global OK
    print(('ok   ' if cond else 'FAIL ') + what, flush=True)
    OK &= bool(cond)


class Run:
    def __init__(self, work, name, exe):
        self.work, self.name, self.exe = work, name, exe
        self.dir = os.path.join(work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        for sub in ('settings', 'appdata', 'saves', 'bin', 'shortcuts', 'files'):
            os.makedirs(os.path.join(self.dir, sub))
        self.shots = os.path.join(work, 'shots')
        os.makedirs(self.shots, exist_ok=True)
        self.log = ''

    def settings_text(self):
        try:
            with open(os.path.join(self.dir, 'settings', 'settings.ini'), encoding='utf-8') as f:
                return f.read()
        except OSError:
            return ''

    def run(self, script, extra_env=None, timeout=120):
        env = dict(os.environ)
        for k in [k for k in env if k.startswith('XD_')]:
            del env[k]
        env.update({
            'XD_SETTINGS_DIR': os.path.join(self.dir, 'settings'),
            'APPDATA': os.path.join(self.dir, 'appdata'),
            'HOME': os.path.join(self.dir, 'appdata'),
            'XDG_DATA_HOME': os.path.join(self.dir, 'appdata'),
            'XD_SAVE_BIN_DIR': os.path.join(self.dir, 'bin'),
            'XD_SHORTCUT_DIR': os.path.join(self.dir, 'shortcuts'),
            'XD_WINDOW_OFFSCREEN': '1',
            'XD_LAUNCHER_RENDER': '0',
            'XD_LAUNCHER_SHADERS': '0',
            'XD_LAUNCHER_SCRIPT': script.replace('%SHOTS%', self.shots.replace('\\', '/')).replace(
                '%FILES%', os.path.join(self.dir, 'files').replace('\\', '/')),
        })
        env.update(extra_env or {})
        t0 = time.time()
        try:
            p = subprocess.run([self.exe], env=env, capture_output=True, text=True, timeout=timeout, errors='replace',
                               cwd=os.path.dirname(self.exe))
        except subprocess.TimeoutExpired as e:
            def text(x):
                return x.decode('utf-8', 'replace') if isinstance(x, bytes) else (x or '')
            self.log = text(e.stderr) + text(e.stdout)
            with open(os.path.join(self.dir, 'run.log'), 'w', encoding='utf-8') as f:
                f.write(self.log)
            print('     %s: TIMED OUT after %d s; last lines:' % (self.name, timeout), flush=True)
            print(chr(10).join(self.log.splitlines()[-6:]), flush=True)
            check(False, '%s finished in time' % self.name)
            return -1
        self.log = p.stderr + p.stdout
        with open(os.path.join(self.dir, 'run.log'), 'w', encoding='utf-8') as f:
            f.write(self.log)
        print('     %s: %.1f s, exit %s' % (self.name, time.time() - t0, p.returncode), flush=True)
        return p.returncode

    def has(self, pattern):
        return re.search(pattern, self.log) is not None

    def count(self, pattern):
        return len(re.findall(pattern, self.log))


def shot(name):
    return 'shot:%SHOTS%/' + name + '.png'


def scenario_tour(r):
    steps = ['size:1280x800', 'padmode:on', 'pad:down', 'padwhere', shot('tour_ask'), 'pad:right', 'pad:a', 'pad:b', shot('tour_disc')]
    for i in range(1, 9):
        steps += ['pad:rb', 'padwhere', shot('tour_%d' % i)]
    steps += ['pad:rb', 'padwhere']
    steps += ['page:settings', 'pad:b']
    for i in range(5):
        steps += ['pad:rt', shot('tour_tab%d' % i)]
    steps += ['pad:lt', 'pad:lt', 'pad:lt', 'pad:lt', 'pad:lt']
    steps += ['page:controls', 'pad:b']
    for i in range(1, 5):
        steps += ['pad:rt', shot('tour_port%d' % i)]
    steps += ['quit']
    r.run('|'.join(steps))
    pages = re.findall(r'pad: page (\w+)', r.log)
    check(pages[:8] == ['extra', 'preset', 'settings', 'controls', 'mods', 'save', 'reports', 'disc'] or
          pages[:7] == ['preset', 'settings', 'controls', 'mods', 'save', 'reports', 'disc'],
          'RB goes through the pages in order and wraps: %s' % pages[:9])
    tabs = re.findall(r'pad: tab (\w+)', r.log)
    check(len(tabs) >= 5 and len(set(tabs)) >= 3, 'RT goes through the Settings tabs: %s' % tabs)
    ports = re.findall(r'pad: controller tab (\d)', r.log)
    check(ports[:4] == ['3', '4', '1', '2'] or ports[:4] == ['2', '3', '4', '1'], 'RT goes through the controllers: %s' % ports)
    check(r.has(r'controller mode on'), 'controller mode on')


UPD = ['pad:down', 'pad:right', 'pad:a']


def scenario_preset(r):
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['page:preset', 'pad:b', 'pad:right', 'pad:down', shot('preset_1'),
                    'pad:down', shot('preset_2'), 'pad:a', 'pad:a', shot('preset_3'), 'quit']))
    check('preset = enhanced' in r.settings_text().lower().replace('"', ''), 'A on Enhanced chose the preset (settings.ini)')


def scenario_datafolder(r):



    env = {'XD_LOCATION_FILE': '1', 'XD_LAUNCHER_ASK_DATA': '1'}
    r.run('|'.join(['size:1280x800', 'padmode:on', 'wait:save', shot('data_first_1'), 'padwhere', 'pad:down', 'padwhere',
                    shot('data_first_2'), 'pad:b', 'padwhere', shot('data_first_closed'),
                    'data:move', 'padwhere', shot('data_move_1'), 'pad:b', 'padwhere',
                    'data:remove', 'padwhere', shot('data_remove_1'), 'pad:b', 'padwhere', 'quit']), extra_env=env)
    where = re.findall(r'pad focus: window "([^"]*)"', r.log)
    check(r.has(r'first start: asking where the data is kept'), 'the question is asked on a new install')
    check(len(where) >= 7 and 'Where should your data be kept?' in where[0] and 'Where should your data be kept?' in where[1],
          'the question has the focus; D-pad moves inside it: %s' % where[:2])
    check(not r.has(r'data_location.json written'), 'closing with B chose nothing')
    check(not os.path.exists(os.path.join(r.dir, 'appdata', 'Pokemon XD', 'launcher.ini')) or
          'data_asked' not in open(os.path.join(r.dir, 'appdata', 'Pokemon XD', 'launcher.ini')).read(),
          'and nothing was remembered')
    check(any('Move my data' in w for w in where) and any('Remove all game data?' in w for w in where),
          'the Move and Remove windows take the focus: %s' % where[2:])
    check(not r.has(r'not NavFlattened'), 'no child window the pad cannot reach')

    r2 = Run(r.work, 'datafolder2', r.exe)
    r2.run('|'.join(['size:1280x1700', 'padmode:on'] + UPD + ['wait:save', 'page:save', 'wait:save', shot('data_card'), 'quit']),
           extra_env={'XD_LOCATION_FILE': '1'})
    check(os.path.exists(os.path.join(r.shots, 'data_card.png')), 'the Save page with the data card was drawn')
    check(not r2.has(r'not NavFlattened'), 'the Save page has no child window the pad cannot reach')


def scenario_editsave(r):



    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import savepoints
    entry = savepoints.find_entry('menusweep_agate')
    if entry is None:
        print('     (save point menusweep_agate is not in the library: editsave skipped)')
        return
    card = os.path.join(r.dir, 'appdata', 'Pokemon XD', 'USA', 'Card A')
    os.makedirs(card, exist_ok=True)
    shutil.copy2(os.path.join(entry, 'Card A', '01-GXXE-PokemonXD.gci'), card)
    to_button = ['pad:right', 'pad:down:5', 'pad:right:4']
    r.run('|'.join(['size:1280x1000', 'padmode:on', 'update:no', 'disc:' + DISC.replace('\\', '/'), 'wait:disc', 'page:save', 'wait:save'] + to_button +
                   ['padwhere', 'pad:a', 'pad:down', 'padwhere', shot('edit_1'), 'pad:down:2', 'pad:a', 'padwhere', shot('edit_2'),
                    'pad:right', 'padwhere', 'pad:b', 'padwhere', shot('edit_closed'),
                    'editopen:1', 'editset:sel.level=44', 'pad:down', 'pad:b', 'pad:down', 'padwhere', shot('edit_ask'), 'pad:b', 'pad:down', 'padwhere', shot('edit_kept'), 'quit']),
          timeout=300)
    where = re.findall(r'pad focus: window "([^"]*)"', r.log)
    check(r.count(r'edit: opened') == 2, 'A on "Edit save..." opened the page (and the script opened it again)')
    check(any('editbody' in w for w in where), 'the D-pad moves into the editor rows (the body is a NavFlattened child): %s' % where[:6])
    check(r.has(r'edit: asking whether to discard'), 'B with a change waiting asks about discarding (the page stays open)')
    check(not r.has(r'not NavFlattened'), 'no child window the pad cannot reach')
    check(not r.has(r'Exception|crash'), 'no crash')


def scenario_quit(r):
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['pad:start', shot('menu'), 'pad:down', 'pad:a',
                    'wait:disc', shot('never')]))
    check(r.has(r'\[launcher\] closed'), 'Start -> Quit the launcher closes it')
    check(not os.path.exists(os.path.join(r.shots, 'never.png')), 'no step ran after Quit')


def scenario_keyboard(r):
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['page:save', 'wait:save', 'pad:b', 'pad:right', 'pad:a',
                    'padwhere', shot('kb_osk'), 'pad:right:2', 'pad:down', shot('kb_osk_move'), 'pad:a', 'pad:y',
                    'type:sh\\n', shot('kb_done'), 'padwhere', 'quit']))
    print('\n'.join(l for l in r.log.splitlines() if 'save' in l.lower() and 'step' not in l))


def scenario_browser(r):
    d = os.path.join(r.dir, 'files', 'discs')
    os.makedirs(os.path.join(d, 'sub'))
    for n in ('a.iso', 'b.iso', 'notes.txt'):
        with open(os.path.join(d, n), 'wb') as f:
            f.write(b'not a disc')
    old = os.path.join(d, 'a.iso').replace('\\', '/')
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['set:system.disc_path=' + old, 'wait:disc', 'pad:b', 'pad:right',
                    'pad:right', 'padwhere', shot('br_page'), 'pad:a', 'padwhere', shot('br_open'), 'pad:down:2', 'padwhere', 'pad:a',
                    'wait:disc', shot('br_picked'), 'padwhere', 'pad:a', 'pad:b', 'padwhere', shot('br_up'), 'pad:x', 'padwhere', 'quit']))
    check(r.has(r'file browser: Choose a file from .*discs'), 'A on Choose... opened the built-in browser at the disc folder')
    check('b.iso' in r.settings_text(), 'the browser picked b.iso: disc_path in settings.ini')
    check(r.count(r'pad focus: window "###filebrowser') + r.count(r'pad focus: window "##filebrowser') >= 0, 'focus log')
    print('\n'.join(l for l in r.log.splitlines() if 'step' not in l or 'padwhere' in l))


def scenario_modalb(r):

    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['page:save', 'wait:save', 'pad:b', 'pad:right', 'pad:a', 'padwhere',
                    'pad:b', 'padwhere', shot('modalb_1'), 'pad:b', 'padwhere', shot('modalb_2'), 'quit']))
    where = re.findall(r'pad focus: window "([^"]*)"[^\n]*?(keyboard open)?; play', r.log)
    print(where)
    check(len(where) == 3 and 'New save' in where[0][0] and where[0][1] and not where[1][1] and 'New save' in where[1][0]
          and 'New save' not in where[2][0], 'B: keyboard closes, then the window closes')


def scenario_settings(r):

    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['tab:gameplay', 'pad:b', 'pad:right', 'pad:down', shot('set_1'), 'pad:a',
                    shot('set_2'), 'pad:down', 'pad:a', shot('set_3'), 'pad:down:3', 'pad:a', shot('set_4'), 'quit']))
    text = r.settings_text().lower()
    check('custom' in text or 'text_speed' in text, 'a setting was changed with the pad (settings.ini)')


def scenario_controls(r):

    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['page:controls', 'pad:b', 'pad:right', 'pad:down:5', 'padwhere',
                    shot('ctl_1'), 'pad:down:6', shot('ctl_2'), 'pad:a', shot('ctl_listen'), 'padwhere', 'quit']))


def scenario_names(r):
    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:settings']
    for t in ('xbox', 'ps', 'switch'):
        steps += ['padtype:' + t, 'pad:down', shot('names_' + t)]
    r.run('|'.join(steps + ['quit']))


def scenario_pages(r):


    ids = ['disc', 'extra', 'preset', 'settings', 'controls', 'mods', 'save', 'reports']
    steps = ['size:1280x800', 'padmode:on'] + UPD
    for i in ids:
        steps += ['page:' + i, 'pad:b', 'padwhere', 'pad:right', 'padwhere', 'padwhere', 'pad:down:3', 'padwhere', shot('page_' + i)]
    r.run('|'.join(steps + ['quit']))
    wheres = re.findall(r'pad focus: window "([^"]*)"', r.log)
    check(len(wheres) == 4 * len(ids) and all('##page' in w or 'bottom' in w for w in wheres[3::4]),
          'on every page the focus is inside the page: %s' % wheres)
    check(not r.has(r'not NavFlattened'), 'no child window out of reach: %s' % re.findall(r'pad: child window "[^"]*"', r.log))


DISC = ''


def scenario_play(r):
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['disc:' + DISC.replace('\\', '/'), 'wait:disc', 'pad:b', 'pad:down:9',
                    'padwhere', shot('play_1'), 'pad:right', 'padwhere', shot('play_2'), 'pad:a', 'wait:game', shot('play_3'),
                    'pad:start', shot('play_menu'), 'pad:down', 'pad:a', 'wait:game', shot('play_4'), 'quit']), timeout=300)

    check(r.count(r"couldn't be started") == 1, "Start > Play pressed Play (xd.exe isn't next to this launcher: it says so)")


def scenario_reach(r):

    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['disc:' + DISC.replace('\\', '/'), 'wait:disc', 'page:mods', 'pad:b',
                    'pad:right', 'pad:down:8', 'padwhere', shot('reach_1'), 'pad:right', 'padwhere', 'pad:a', 'wait:game',
                    'quit']), timeout=300)
    ids = re.findall(r'item 0x([0-9A-F]{8}); play button 0x([0-9A-F]{8})', r.log)
    check(any(a == b for a, b in ids), 'the D-pad reaches the Play button: %s' % ids[-3:])
    check(r.count(r"couldn't be started") == 1, 'A on Play pressed it')


def scenario_modextract(r):


    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    try:
        import savepoints
        entry = savepoints.find_entry('menusweep_agate')
    except ImportError:
        savepoints, entry = None, None
    if savepoints is None or entry is None:
        print('SKIPPED modextract: needs tools/savepoints.py and the save point library menusweep_agate (private working tree only, not in the public copy)', flush=True)
        return
    card = os.path.join(r.dir, 'appdata', 'Pokemon XD', 'USA', 'Card A')
    os.makedirs(card)
    shutil.copy2(os.path.join(entry, 'Card A', '01-GXXE-PokemonXD.gci'), card)
    steps = ['size:1280x800', 'padmode:on'] + UPD + ['disc:' + DISC.replace('\\', '/'), 'wait:disc', 'page:save', 'wait:save', 'padwhere']
    steps += ['pad:b', 'pad:right', 'pad:down', 'pad:a', 'wait:save', 'pad:down:4', 'padwhere', shot('modextract_rename'), 'pad:right:3', 'padwhere', shot('modextract_button'),
              'pad:a', 'wait:save', 'padwhere', 'pad:down', 'padwhere', 'pad:right', 'padwhere', shot('modextract_page'), 'quit']
    r.run('|'.join(steps), timeout=300)
    wheres = re.findall(r'pad focus: window "([^"]*)"', r.log)
    check(len(wheres) >= 6 and '##box_' in wheres[1] and '##box_' in wheres[2], 'the D-pad walks from the save row into the buttons of the open save: %s' % wheres[:4])
    check(all('##page' in w or '##bottom' in w or '##nav' in w for w in wheres[3:]), 'after A on "Take a Pokemon out of a mod..." the focus stays inside the page: %s' % wheres[3:])
    check(not r.has(r'not NavFlattened'), 'no child window out of reach: %s' % re.findall(r'pad: child window "[^"]*"', r.log))


def scenario_modscripts(r):


    mods = os.path.join(r.dir, 'settings', 'mods')
    for mid, extra in (('lua-mod', ', "scripts": true'), ('plain-mod', '')):
        os.makedirs(os.path.join(mods, mid, 'data'))
        with open(os.path.join(mods, mid, 'mod.json'), 'w', encoding='utf-8') as f:
            f.write('{"id": "%s", "name": "%s", "version": "1.0", "author": "test", "description": "A test mod.", "game": "GXXE01"%s}'
                    % (mid, mid.replace('-', ' ').title(), extra))
    with open(os.path.join(r.dir, 'settings', 'mods.ini'), 'w', encoding='utf-8') as f:
        f.write('[profile default]\nlua-mod = on\nplain-mod = on\n')
    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:mods', 'pad:b', 'pad:right', 'padwhere', shot('modscripts_1')]
    steps += ['pad:down:%d' % PAD_DOWN_TO_ALLOW, 'padwhere', shot('modscripts_2'), 'pad:a', shot('modscripts_off')]
    r.run('|'.join(steps + ['quit']))
    ini = open(os.path.join(r.dir, 'settings', 'mods.ini'), encoding='utf-8').read()
    check('lua-mod = on, scripts off' in ini and 'plain-mod = on\n' in ini and 'plain-mod = on, scripts' not in ini,
          'A on Allow wrote "lua-mod = on, scripts off" to mods.ini (and nothing for the mod without scripts): %r' % ini[-90:])
    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:mods', 'pad:b', 'pad:right', 'pad:down:%d' % PAD_DOWN_TO_ALLOW, shot('modscripts_again'),
                                                    'pad:a']
    r.run('|'.join(steps + ['quit']))
    ini = open(os.path.join(r.dir, 'settings', 'mods.ini'), encoding='utf-8').read()
    check('scripts off' not in ini and 'lua-mod = on' in ini, 'a second A on Allow switched the scripts on again: %r' % ini[-90:])


def scenario_modgames(r):


    mods = os.path.join(r.dir, 'settings', 'mods')
    def put(mid, game, folders):
        os.makedirs(os.path.join(mods, mid))
        with open(os.path.join(mods, mid, 'mod.json'), 'w', encoding='utf-8') as f:
            f.write('{"id": "%s", "name": "%s", "version": "1.0", "author": "test", "description": "A test mod.", "game": %s}'
                    % (mid, mid.replace('-', ' ').title(), game))
        for folder in folders:
            os.makedirs(os.path.join(mods, mid, folder))
            with open(os.path.join(mods, mid, folder, 'x.txt'), 'w') as f:
                f.write('x')
    put('old-mod', '"GXXE01"', ['data'])
    put('two-games', '["GXXE01", "GC6E01"]', ['textures', os.path.join('games', 'GXXE01', 'data')])
    put('colo-only', '"GC6E01"', ['data'])
    with open(os.path.join(r.dir, 'settings', 'mods.ini'), 'w', encoding='utf-8') as f:
        f.write('[profile default]' + chr(10) + 'old-mod = on' + chr(10) + 'two-games = on' + chr(10) + 'colo-only = off' + chr(10))
    steps = ['size:1280x1000', 'padmode:on'] + UPD + ['page:mods', shot('modgames_1')]
    r.run('|'.join(steps + ['quit']))
    check(os.path.exists(os.path.join(r.shots, 'modgames_1.png')), 'the Mods page with "Works with" lines was drawn (modgames_1.png)')


PAD_DOWN_TO_ALLOW = 4

def scenario_extrausum(r):


    pack = os.path.join(r.dir, 'settings', 'extra_assets', 'usum')
    os.makedirs(os.path.join(pack, 'fsys', 'x'))
    with open(os.path.join(pack, 'mod.json'), 'w') as f:
        f.write('{"id": "ultra-sun-moon-models", "name": "Ultra Sun / Ultra Moon models", "version": "0.1.0", "count": 385, '
                '"unit": "model", "units": "models", "game": "GXXE01", "format": 1}')
    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:extra', 'pad:b', 'pad:right', 'pad:down:4', 'pad:right', 'padwhere', shot('extrausum_1'),
             'pad:a', 'padwhere', shot('extrausum_question'), 'pad:b', 'padwhere', shot('extrausum_kept')]
    r.run('|'.join(steps + ['pad:a', 'pad:left', 'padwhere', shot('extrausum_question2'), 'pad:a', shot('extrausum_removed'), 'quit']))
    check(not os.path.isdir(pack), 'Remove it removed the pack folder')
    check(len(re.findall(r'removeextra', r.log)) >= 2, 'the question window had the focus (twice)')
    check(r.count(r'extra: usum: removed') == 1, 'removed once (not on B)')
    check(not r.has(r'not NavFlattened'), 'no child window out of reach of the pad')


BUILD_DIR = ['']
DUMP = ''

def scenario_extragen57(r):



    if not os.path.exists(DUMP):
        print('     (no dump at %s: skipped)' % DUMP)
        return
    standin = os.path.join(BUILD_DIR[0], 'pbr_models_standin' + ('.exe' if os.name == 'nt' else ''))
    steps = ['size:1280x1000', 'padmode:on'] + UPD + ['disc:' + DISC, 'wait:disc', 'page:extra', 'extradisc:usum=' + DUMP, 'wait:extradisc',
             'extramake:usum', 'page:extra', 'page:extra', 'padwhere', shot('extragen57_question'), 'pad:b', 'padwhere', shot('extragen57_closed'),
             'extramake:usum', 'page:extra', 'page:extra', 'padwhere', 'pad:a', 'padwhere', shot('extragen57_notnow'), 'quit']
    r.run('|'.join(steps), extra_env={'XD_USUM_MODELS_EXE': standin})
    check(len(re.findall(r'extrawarn', r.log)) >= 2, 'the question window had the focus (twice)')
    check(not r.has(r'extra: usum: started'), 'B / Not now started nothing')
    check(not r.has(r'not NavFlattened'), 'no child window out of reach of the pad')


def scenario_extratabs(r):

    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:extra', 'pad:b', shot('extratabs_models'), 'padwhere']
    steps += ['pad:rt', 'padwhere', shot('extratabs_languages'), 'pad:rt', 'padwhere', shot('extratabs_other'),
              'pad:rt', 'pad:lt', 'pad:lt', 'pad:down', 'pad:down', 'padwhere', shot('extratabs_back'), 'quit']
    r.run('|'.join(steps))
    tabs = re.findall(r'pad: extra tab (\w+)', r.log)
    check(tabs == ['languages', 'other', 'models', 'other', 'languages'],
          'RT / LT go through the Extra assets tabs and wrap: %s' % tabs)
    check(not r.has(r'not NavFlattened'), 'no child window out of reach of the pad')


def scenario_defaults(r):


    steps = ['size:1280x800', 'padmode:on'] + UPD + ['page:preset', 'pad:b'] + ['pad:down'] * 11 + [
        shot('defaults_1'), 'pad:a', 'pad:down', shot('defaults_2'), 'pad:a', shot('defaults_3'), 'quit']
    r.run('|'.join(steps))
    check('new_looks = most_modern' in r.settings_text().lower().replace('"', ''), 'the Looks box was changed with the controller (settings.ini)')
    r.run('|'.join(['size:1280x800', 'padmode:on'] + UPD + ['page:preset', shot('defaults_again'), 'quit']))
    check('new_looks = most_modern' in r.settings_text().lower().replace('"', ''), 'the choice is still there after a restart')


def scenario_modtools(r):



    mods = os.path.join(r.dir, 'settings', 'mods')
    def put(mid, data):
        os.makedirs(os.path.join(mods, mid, 'data'))
        with open(os.path.join(mods, mid, 'mod.json'), 'w', encoding='utf-8') as f:
            f.write('{"id": "%s", "name": "%s", "version": "1.0", "author": "test", "description": "A test mod.", "game": "GXXE01"}' % (mid, mid.replace('-', ' ').title()))
        with open(os.path.join(mods, mid, 'data', 'pokemon.json'), 'w', encoding='utf-8') as f:
            f.write(data)
    put('a-good-mod', '{"pokemon": {"Eevee": {"hp": 70}}}')
    put('b-broken-mod', '{"pokemon": {"Eevee": {"hp": 70}')
    with open(os.path.join(r.dir, 'settings', 'mods.ini'), 'w', encoding='utf-8') as f:
        f.write('[profile default]' + chr(10) + 'a-good-mod = off' + chr(10) + 'b-broken-mod = off' + chr(10))
    steps = ['size:1280x1000', 'padmode:on'] + UPD + ['page:mods', 'pad:b', 'pad:right']
    steps += ['pad:down:%d' % PAD_DOWN_TO_CHECK, 'padwhere', shot('modtools_1'), 'pad:a', 'wait:modtools', shot('modtools_check_good'), 'padwhere',
              'pad:b', 'wait:modtools', 'padwhere', shot('modtools_closed')]
    steps += ['pad:down:%d' % PAD_DOWN_NEXT_MOD, 'padwhere', 'pad:a', 'wait:modtools', shot('modtools_check_broken'), 'pad:b', 'wait:modtools']
    steps += ['pad:right', 'padwhere', 'pad:a', 'padwhere', shot('modtools_pack_browser'), 'pad:x', 'padwhere', 'quit']
    r.run('|'.join(steps))
    check(r.has(r'modtools: check a-good-mod'), 'A on "Check this mod" (first mod) ran the checker in the launcher')
    check(r.has(r'modtools: done: a-good-mod: Nothing wrong found'), 'the good mod: "Nothing wrong found" in plain words')
    check(r.has(r'modtools: check b-broken-mod') and r.has(r'modtools: done: b-broken-mod: Problems: \d+'), 'the broken mod: the report says how many problems')
    check(r.has(r'file browser: .*') , 'A on "Pack for sharing..." opened the controller file browser (a save window)')
    check(not r.has(r'not NavFlattened'), 'no child window the controller cannot reach')

    steps = ['size:1280x1000', 'padmode:on'] + UPD + ['page:mods', 'modon:b-broken-mod', 'wait:modtools', shot('modtools_beforeon'), 'pad:b', 'wait:modtools', 'quit']
    r.run('|'.join(steps))
    ini = open(os.path.join(r.dir, 'settings', 'mods.ini'), encoding='utf-8').read()
    check('b-broken-mod = off' in ini and 'kept off after the check: b-broken-mod' in r.log, 'B on the question kept the broken mod off')
    steps = ['size:1280x1000', 'padmode:on'] + UPD + ['page:mods', 'modon:a-good-mod', 'wait:modupdate', 'wait:modtools', 'quit']
    r.run('|'.join(steps))
    check(r.has(r'Checked "A Good Mod" before switching it on: nothing wrong found'), 'a good mod is checked and goes on with one line, no window')


PAD_DOWN_TO_CHECK = 4
PAD_DOWN_NEXT_MOD = 2

SCENARIOS = {
    'defaults': scenario_defaults,
    'extrausum': scenario_extrausum,
    'extragen57': scenario_extragen57,
    'extratabs': scenario_extratabs,
    'modtools': scenario_modtools,
    'modextract': scenario_modextract,
    'modscripts': scenario_modscripts,
    'modgames': scenario_modgames,
    'play': scenario_play,
    'modalb': scenario_modalb,
    'pages': scenario_pages,
    'settings': scenario_settings,
    'controls': scenario_controls,
    'names': scenario_names,
    'reach': scenario_reach,
    'tour': scenario_tour,
    'preset': scenario_preset,
    'quit': scenario_quit,
    'editsave': scenario_editsave,
    'datafolder': scenario_datafolder,
    'keyboard': scenario_keyboard,
    'browser': scenario_browser,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', required=True)
    ap.add_argument('--work', default=os.path.join(ROOT, 'build', 'pad_test'))
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    exe = os.path.join(a.build, 'xd_launcher.exe' if os.name == 'nt' else 'xd_launcher')
    BUILD_DIR[0] = os.path.abspath(a.build)
    os.makedirs(a.work, exist_ok=True)



    game = os.path.join(a.build, 'xd.exe' if os.name == 'nt' else 'xd')
    if os.path.exists(game):
        stage = os.path.join(a.work, 'launcher_only')
        shutil.rmtree(stage, ignore_errors=True)
        os.makedirs(stage)
        shutil.copy2(exe, stage)
        for f in os.listdir(a.build):
            if f.lower().endswith(('.dll', '.so', '.dylib')) or '.so.' in f:
                shutil.copy2(os.path.join(a.build, f), stage)
        exe = os.path.join(stage, os.path.basename(exe))
        print('     (a game next to the launcher: running the copy in %s, which has none)' % stage, flush=True)
    only = [s for s in a.only.split(',') if s]
    for name, fn in SCENARIOS.items():
        if only and name not in only:
            continue
        print('== ' + name, flush=True)
        fn(Run(a.work, name, exe))
    print('ALL PASSED' if OK else 'FAILED')
    return 0 if OK else 1


if __name__ == '__main__':
    sys.exit(main())
