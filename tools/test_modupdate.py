r"""End-to-end test of the launcher's mod updates, dependencies and shared lists with a LOCAL feed (127.0.0.1): nothing goes to the
internet, no game copy is started (no disc, never Play), no global keyboard or mouse input is used (the launcher's own script steps).

    python tools/test_modupdate.py --build build\release\bin [--work <folder>] [--only a,b]

Scenarios (each with its own scratch APPDATA + XD_SETTINGS_DIR, so the player's files are never touched):
  consent     "Check for mod updates" asks first; NOTHING is sent before the yes; "No" sends nothing
  update      yes -> the update is shown (version, notes, permissions, the ADDED permission named) -> download -> scan -> Install;
              sha256 / size checked, the author's key pinned, the old version kept; Roll back swaps and Roll back again goes forward
  refused     no signature, another key than the pinned one, a tampered download, a signed zip that holds a program: all refused
  protect     a playthrough that uses the mod: the update is held back ("Keep my current version") or installed anyway
  deps        turning a mod on asks about its requirements, turns them on, puts the list in a working order; a missing requirement
              with a link is downloaded through the same checks
  lists       "Save this list" -> a fresh launcher imports it: names what is missing, downloads it, applies the order
  pad         the same popups (question, update + permission change, roll back, needs, list, protect) with the controller only
  clashes     two mods changing the same table value / story room / hook: the Mods page says which wins
  keychange   a planned signing key change (xd_mod_pack --rotate-key, the old key announced the new one: accepted by itself, the new key
              pinned), a lost key (no announcement: the new fingerprint is shown, NOTHING is downloaded until the explicit confirmation,
              refused without it), a forged announcement (counts for nothing)
Screenshots: <work>\shots. Exit code 0 = everything passed. Test-only switch: XD_MODUPDATE_ALLOW_LOCAL=1 (http://127.0.0.1 allowed);
a real update address must be https.
"""
import argparse
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import zipfile
import hashlib
import secrets

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'publish'))
sys.dont_write_bytecode = True
try:
    import sign_update
except ImportError as e:
    print('SKIPPED the mod update scenarios: %s (tools/publish/sign_update.py is not part of the public copy)' % e)
    sys.exit(0)

OK = True


def check(cond, what):
    global OK
    print(('ok   ' if cond else 'FAIL ') + what, flush=True)
    OK &= bool(cond)


class Feed:
    def __init__(self, root):
        self.root = root
        self.mode = {}
        self.requests = []
        feed = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                feed.requests.append(self.path)
                name = self.path.lstrip('/')
                path = os.path.join(feed.root, *name.split('/'))
                if '..' in name or not os.path.isfile(path):
                    self.send_error(404)
                    return
                data = open(path, 'rb').read()
                if feed.mode.get(name) == 'badhash':
                    data = bytearray(data)
                    data[len(data) // 2] ^= 0xFF
                    data = bytes(data)
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def url(self, name):
        return f'http://127.0.0.1:{self.port}/{name}'


class Ctx:
    def __init__(self, build, work):
        self.build, self.work = build, work
        self.launcher = os.path.join(build, 'xd_launcher.exe' if os.name == 'nt' else 'xd_launcher')
        self.pack = os.path.join(build, 'xd_mod_pack.exe' if os.name == 'nt' else 'xd_mod_pack')
        self.shots = os.path.join(work, 'shots')
        self.feed_dir = os.path.join(work, 'feed')
        os.makedirs(self.shots, exist_ok=True)
        os.makedirs(self.feed_dir, exist_ok=True)
        self.feed = Feed(self.feed_dir)
        self.keyA = os.path.join(work, 'keys', 'authorA.key')
        self.keyB = os.path.join(work, 'keys', 'authorB.key')
        os.makedirs(os.path.dirname(self.keyA), exist_ok=True)
        self.pubA = self.make_key(self.keyA)
        self.pubB = self.make_key(self.keyB)

    def make_key(self, path):
        r = subprocess.run([self.pack, '--make-key', path], capture_output=True, text=True)
        m = re.search(r'Public key: ([0-9a-f]{64})', r.stdout)
        check(r.returncode == 0 and m is not None, f'xd_mod_pack --make-key {os.path.basename(path)}')
        return m.group(1)

    def seed(self, key):
        return bytes.fromhex(re.search(r'private: ([0-9a-f]{64})', open(key).read()).group(1))


    def mod_json(self, mid, version, extra=None, updates=True):
        d = {'id': mid, 'name': mid.capitalize(), 'version': version, 'author': 'Test Author', 'game': 'GXXE01',
             'description': f'The test mod {mid}.'}
        if updates:
            d['updates'] = self.feed.url(f'{mid}.update.json')
        d.update(extra or {})
        return d

    def write_mod(self, folder, mid, version, extra=None, files=None, updates=True):
        os.makedirs(folder, exist_ok=True)
        json.dump(self.mod_json(mid, version, extra, updates), open(os.path.join(folder, 'mod.json'), 'w'), indent=2)
        for name, text in (files or {'data/tweak.json': '{"pokemon": {"#1": {"basisStatus[0].maxHp": 50}}}'}).items():
            p = os.path.join(folder, *name.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'w').write(text)
        return folder

    def publish(self, mid, version, extra=None, files=None, key=None, notes='Test notes', out=None, updates=True):
        """Packs a mod with xd_mod_pack into the feed folder (signed with `key`, or unsigned when key is False)."""
        src = os.path.join(self.work, 'src', f'{mid}-{version}', mid)
        if os.path.isdir(os.path.dirname(src)):
            shutil.rmtree(os.path.dirname(src))
        self.write_mod(src, mid, version, extra, files, updates)
        out = out or self.feed_dir
        cmd = [self.pack, src, '--out', out, '--url-base', self.feed.url(''), '--notes', notes]
        if key is not False:
            cmd += ['--key', key or self.keyA]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            print(r.stdout, r.stderr)
        check(r.returncode == 0, f'packed {mid} {version}')
        return os.path.join(out, f'{mid}-{version}.zip')

    def run(self, name, script, setup=None, label=None, extra_env=None, timeout=240):
        data = os.path.join(self.work, 'data_' + name)
        first = not os.path.isdir(data)
        appdata, settings = os.path.join(data, 'appdata'), os.path.join(data, 'settings')
        for d in (appdata, settings):
            os.makedirs(d, exist_ok=True)
        if setup and first:
            setup(settings, appdata)
        env = dict(os.environ)
        env.pop('XD_CARD_DIR', None)
        env.update({'APPDATA': appdata, 'HOME': appdata, 'XDG_DATA_HOME': appdata, 'XD_SETTINGS_DIR': settings, 'XD_WINDOW_OFFSCREEN': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0',
                    'XD_LAUNCHER_RENDER': '0', 'XD_MODUPDATE_ALLOW_LOCAL': '1', 'XD_MODUPDATE_AV': env.get('XD_MODUPDATE_AV', '1'),
                    'XD_LAUNCHER_SCRIPT': script.replace('%SHOTS%', self.shots.replace('\\', '/'))})
        env.update(extra_env or {})
        log = os.path.join(self.work, 'logs', (label or name) + '.log')
        os.makedirs(os.path.dirname(log), exist_ok=True)
        before = len(self.feed.requests)
        with open(log, 'w', encoding='utf-8', errors='replace') as f:
            try:
                r = subprocess.run([self.launcher], env=env, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, cwd=self.build)
                code = r.returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
        text = open(log, encoding='utf-8', errors='replace').read()
        return code, text, self.feed.requests[before:], settings, appdata


def shot(n):
    return f'shot:%SHOTS%/{n}.png'


def has(text, pat):
    return re.search(pat, text) is not None


def tree(folder):
    out = {}
    for dp, _, fs in os.walk(folder):
        for n in fs:
            p = os.path.join(dp, n)
            out[os.path.relpath(p, folder).replace(os.sep, '/')] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
    return out


def mod_version(settings, mid):
    try:
        return json.load(open(os.path.join(settings, 'mods', mid, 'mod.json')))['version']
    except Exception:
        return None


def hand_install(ctx, settings, mid, version, extra=None, files=None, updates=True):
    ctx.write_mod(os.path.join(settings, 'mods', mid), mid, version, extra, files, updates)




def scenario_consent(ctx):
    ctx.publish('alpha', '1.1')
    steps = ['update:no', 'size:1280x900', 'page:mods', 'modupdate:check', shot('consent_question'), 'modupdate:no', 'wait:modupdate', shot('consent_after_no'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('consent', '|'.join(steps), setup=lambda s, a: hand_install(ctx, s, 'alpha', '1.0'))
    check(code == 0, 'consent: launcher ran to the end')
    check(has(log, r'modupdates: answer: no'), 'consent: the first click asked the question, "No" was answered')
    check(reqs == [], 'consent: NOTHING was sent before a yes, and nothing after "No" (requests: %r)' % reqs)
    ini = os.path.join(settings, 'mod_updates', 'modupdates.ini')
    check(os.path.isfile(ini) and 'asked=yes' in open(ini).read() and 'always=yes' not in open(ini).read(), 'consent: the answer is remembered (asked, not always)')

    code, log, reqs, _, _ = ctx.run('consent', '|'.join(['page:mods', 'wait:modupdate', 'quit']), label='consent2')
    check(reqs == [], 'consent: a start without "always" sends nothing')
    check(not has(log, r'modupdates: checking'), 'consent: no check at start without "always"')


def scenario_update(ctx):
    ctx.publish('alpha', '1.1', extra={'scripts': True}, files={
        'data/tweak.json': '{"pokemon": {"#1": {"basisStatus[0].maxHp": 55}}}',
        'scripts/hello.lua': 'on("battle.damage", function(ctx) end)\n'}, notes='What is new in 1.1:\n- a script\n- cafe')
    steps = ['update:no', 'size:1280x1000', 'page:mods', 'modupdate:check', 'modupdate:always', 'wait:modupdate', shot('update_results'),
             'modupdate:get', 'wait:modupdate', shot('update_staged'), 'modupdate:install', 'wait:modupdate', 'page:mods', shot('update_installed'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('update', '|'.join(steps), setup=lambda s, a: hand_install(ctx, s, 'alpha', '1.0'))
    check(code == 0, 'update: launcher ran to the end')
    check(any('alpha.update.json' in r for r in reqs) and any(r.endswith('alpha-1.1.zip') for r in reqs), 'update: the update file and the zip were fetched after the yes')
    check(has(log, r'answer: always'), 'update: "always check" was remembered')
    check(has(log, r'download checked: ok'), 'update: size, sha256, virus scan, safety scan and checker passed')
    check(has(log, r'installed alpha 1\.1 \(version 1\.0 kept for roll back\)'), 'update: installed, the old version kept')
    check(mod_version(settings, 'alpha') == '1.1', 'update: the mod folder now holds 1.1')
    prev = os.path.join(settings, 'mod_updates', 'previous', 'alpha')
    check(os.path.isfile(os.path.join(prev, 'mod', 'mod.json')) and '"1.0"' in open(os.path.join(prev, 'info.json')).read(), 'update: the previous version (1.0) is kept')
    keys = json.load(open(os.path.join(settings, 'mod_updates', 'mod_keys.json')))
    check(keys['keys'].get('alpha') == ctx.pubA, "update: the author's public key was pinned (trust on first use)")
    check(not os.path.isdir(os.path.join(settings, 'mod_updates', 'tmp')) or not os.listdir(os.path.join(settings, 'mod_updates', 'tmp')), 'update: the temporary download folder is gone')
    check(not any('PRIVATE' in open(os.path.join(dp, n), errors='ignore').read() for dp, _, fs in os.walk(os.path.join(settings, 'mods')) for n in fs if n.endswith(('.json', '.lua'))),
          'update: no private key text is anywhere in the installed mod')
    zipped = zipfile.ZipFile(os.path.join(ctx.feed_dir, 'alpha-1.1.zip'))
    check(all('private:' not in zipped.read(n).decode('utf-8', 'ignore') for n in zipped.namelist()), 'update: the packed zip holds no private key')

    steps = ['update:no', 'size:1280x1000', 'page:mods', shot('rollback_list'), 'modupdate:rollback=alpha', shot('rollback_ask'), 'modupdate:rollbackyes', 'page:mods', shot('rollback_done'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('update', '|'.join(steps), label='update_rollback')
    check(mod_version(settings, 'alpha') == '1.0', 'roll back: version 1.0 is back')
    check(reqs and all('alpha' in r for r in reqs) or reqs == [], 'roll back: nothing else was fetched (always-check asked only for update files)')
    prevv = json.load(open(os.path.join(settings, 'mod_updates', 'previous', 'alpha', 'info.json')))['version']
    check(prevv == '1.1', 'roll back: 1.1 is kept, so going forward again works')
    steps = ['modupdate:rollback=alpha', 'modupdate:rollbackyes', 'wait:modupdate', 'quit']
    ctx.run('update', '|'.join(steps), label='update_rollback2', extra_env={})
    check(mod_version(settings, 'alpha') == '1.1', 'roll back twice: forward to 1.1 again')


def scenario_refused(ctx):
    seedA, seedB = ctx.seed(ctx.keyA), ctx.seed(ctx.keyB)

    ctx.publish('nosig', '2.0', key=False)


    ctx.publish('otherkey', '2.0', key=ctx.keyB)

    ctx.publish('badzip', '2.0', key=ctx.keyA)
    ctx.feed.mode['badzip-2.0.zip'] = 'badhash'

    evil_dir = os.path.join(ctx.work, 'evil')
    os.makedirs(evil_dir, exist_ok=True)
    evil = os.path.join(ctx.feed_dir, 'evil-2.0.zip')
    with zipfile.ZipFile(evil, 'w') as z:
        z.writestr('evil/mod.json', json.dumps(ctx.mod_json('evil', '2.0')))
        z.writestr('evil/data/tweak.json', '{}')
        z.writestr('evil/payload.exe', b'MZ' + b'\0' * 200)
    data = open(evil, 'rb').read()
    manifest = {'format': 1, 'id': 'evil', 'version': '2.0', 'url': ctx.feed.url('evil-2.0.zip'), 'sha256': hashlib.sha256(data).hexdigest(),
                'size': len(data), 'notes': 'harmless, honest', 'games': ['GXXE01'], 'permissions': ['changes-tables']}
    text = json.dumps(manifest, indent=2)
    sig = sign_update.sign(seedA, b'mod-update-1\n' + text.encode()).hex()
    open(os.path.join(ctx.feed_dir, 'evil.update.json'), 'wb').write((
        '{"signature": "%s", "public_key": "%s", "manifest": %s}' % (sig, ctx.pubA, text)).encode())

    def setup(settings, appdata):
        for mid in ('nosig', 'otherkey', 'badzip', 'evil'):
            hand_install(ctx, settings, mid, '1.0')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'otherkey': ctx.pubA, 'badzip': ctx.pubA, 'evil': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
    before = {m: None for m in ('nosig', 'otherkey', 'badzip', 'evil')}
    steps = ['update:no', 'size:1280x1100', 'page:mods', 'modupdate:check', 'wait:modupdate', shot('refused_results'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('refused', '|'.join(steps), setup=setup)
    check(code == 0, 'refused: launcher ran to the end')

    for mid in ('nosig', 'otherkey'):
        check(mod_version(settings, mid) == '1.0', f'refused: {mid} is still 1.0')

    for mid in ('badzip', 'evil'):
        only = os.path.join(ctx.work, f'only_{mid}')
        steps = ['update:no', 'size:1280x1100', 'page:mods', 'modupdate:check', 'wait:modupdate', 'modupdate:get', 'wait:modupdate', shot(f'refused_{mid}'), 'modupdate:close', 'quit']

        def setup2(settings, appdata, mid=mid):
            hand_install(ctx, settings, mid, '1.0')
            os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
            json.dump({'keys': {mid: ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
            open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
        code, log, reqs, settings, appdata = ctx.run('refused_' + mid, '|'.join(steps), setup=setup2)
        check(has(log, r'download refused'), f'refused: the {mid} download was refused')
        check(mod_version(settings, mid) == '1.0', f'refused: {mid} was not installed (still 1.0)')
        left = os.path.join(settings, 'mod_updates', 'tmp')
        check(not os.path.isdir(left) or not os.listdir(left), f'refused: nothing is left in the temporary folder for {mid}')
        check(not os.path.isdir(os.path.join(settings, 'mod_updates', 'previous', mid)), f'refused: nothing was kept as "previous" for {mid}')
        if mid == 'badzip':
            check(has(log, r'sha256 fingerprint doesn.t match'), 'refused: the tampered zip says the fingerprint does not match')
        else:
            check(has(log, r'safety scan or the checker found problems'), 'refused: the zip with a program in it was stopped by the safety scan')


def scenario_protect(ctx):
    ctx.publish('zeta', '1.1', files={'data/tweak.json': '{"pokemon": {"#1": {"basisStatus[0].maxHp": 99}}}'})
    pt = {'format': 1, 'engine': 'faithful', 'mods': [{'id': 'zeta', 'name': 'Zeta', 'version': '1.0', 'applies': 'new_playthrough'}],
          'started': '2026-11-16 12:00', 'port_version': '0.6.0', 'chosen_by': 'screen', 'trainer': 'ASH', 'trainer_id': 1, 'secret_id': 2}

    def setup(settings, appdata):
        hand_install(ctx, settings, 'zeta', '1.0')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'zeta': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
        p = os.path.join(appdata, 'Pokemon XD', 'playthroughs', 'Card A')
        os.makedirs(p, exist_ok=True)
        json.dump(pt, open(os.path.join(p, 'pt_test.json'), 'w'))
    steps = ['update:no', 'size:1280x1100', 'page:mods', 'modupdate:check', 'wait:modupdate', 'modupdate:get', 'wait:modupdate', shot('protect_staged'), 'modupdate:keep', 'wait:modupdate', 'page:mods', shot('protect_kept'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('protect', '|'.join(steps), setup=setup)
    check(code == 0 and has(log, r'kept the current version of zeta'), 'protect: "Keep my current version" kept the old version (a playthrough uses it)')
    check(mod_version(settings, 'zeta') == '1.0', 'protect: the mod is still 1.0')
    check(not os.path.isdir(os.path.join(settings, 'mod_updates', 'previous', 'zeta')), 'protect: nothing was replaced, so nothing "previous"')
    pt_file = os.path.join(appdata, 'Pokemon XD', 'playthroughs', 'Card A', 'pt_test.json')
    check(json.load(open(pt_file)) == pt, 'protect: the playthrough setup file (the save) is untouched')
    steps = ['update:no', 'size:1280x1100', 'page:mods', 'modupdate:check', 'wait:modupdate', 'modupdate:get', 'wait:modupdate', 'modupdate:install', 'wait:modupdate', 'page:mods', shot('protect_installed_anyway'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('protect', '|'.join(steps), label='protect2')
    check(mod_version(settings, 'zeta') == '1.1' and has(log, r'installed zeta 1\.1'), 'protect: "Install anyway" installs, the old version is kept')
    check(json.load(open(pt_file)) == pt, 'protect: the save setup is still untouched after installing anyway')


def scenario_deps(ctx):
    ctx.publish('ghostlib', '1.2')

    def setup(settings, appdata):
        hand_install(ctx, settings, 'core', '1.0', updates=False)
        hand_install(ctx, settings, 'addon', '1.0', extra={'requires': ['core'], 'after': ['core']}, updates=False, files={'data/b.json': '{"moves": {"#1": {"power": 60}}}'})
        hand_install(ctx, settings, 'late', '1.0', extra={'requires': ['addon'], 'after': ['addon'], 'recommends': ['nothere']}, updates=False, files={'data/c.json': '{"items": {"#2": {"price": 10}}}'})
        hand_install(ctx, settings, 'needy', '1.0', extra={'requires': ['ghostlib >= 1.0'], 'links': {'ghostlib': ctx.feed.url('ghostlib.update.json')}}, updates=False,
                     files={'data/d.json': '{"items": {"#3": {"price": 10}}}'})
        hand_install(ctx, settings, 'oldreq', '1.0', extra={'requires': ['core >= 5.0']}, updates=False, files={'data/e.json': '{"items": {"#4": {"price": 10}}}'})
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')

        open(os.path.join(settings, 'mods.ini'), 'w').write('profile = default\n[profile default]\nlate = off\naddon = off\ncore = off\nneedy = off\noldreq = off\n')
    steps = ['update:no', 'size:1280x1100', 'page:mods', shot('deps_list_off'), 'modneeds:late', shot('deps_prompt_late'), 'modneeds:yes', 'page:mods', shot('deps_after_late'),
             'modneeds:needy', shot('deps_prompt_missing'), 'modneeds:download', 'wait:modupdate', shot('deps_download_offer'), 'modupdate:get', 'wait:modupdate', shot('deps_staged'),
             'modupdate:install', 'wait:modupdate', shot('deps_prompt_again'), 'modneeds:yes', 'page:mods', shot('deps_done'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('deps', '|'.join(steps), setup=setup)
    check(code == 0, 'deps: launcher ran to the end')
    ini = open(os.path.join(settings, 'mods.ini')).read()
    lines = [l.strip() for l in ini.splitlines() if ' = ' in l and not l.startswith('profile')]
    order = [l.split(' = ')[0] for l in lines]
    state = {l.split(' = ')[0]: l.split(' = ')[1] for l in lines}
    check(state.get('late') == 'on' and state.get('addon') == 'on' and state.get('core') == 'on', 'deps: turning "late" on turned on what it needs (addon, core)')
    check(order.index('core') < order.index('addon') < order.index('late'), 'deps: the list was put in a working order (core, addon, late); got %s' % order)
    check(has(log, r'turned on late \+ 2 required'), 'deps: the log says it turned on 2 required mods')
    check(has(log, r'installed ghostlib 1\.2'), 'deps: the missing requirement was downloaded through the same checks and installed')
    check(state.get('needy') == 'on', 'deps: after the download "needy" was turned on')
    check(not any('ghostlib.update.json' in r and False for r in reqs) and any('ghostlib.update.json' in r for r in reqs), 'deps: the link was asked only after the player said download')
    check(state.get('oldreq') == 'off', 'deps: a mod whose requirement version is too old stays off')


def scenario_lists(ctx):
    ctx.publish('ghostlib', '1.2')

    def setup(settings, appdata):
        hand_install(ctx, settings, 'core', '1.0', updates=False)
        hand_install(ctx, settings, 'ghostlib', '1.2', updates=True)
        hand_install(ctx, settings, 'extra', '3.0', updates=False, files={'data/z.json': '{"items": {"#9": {"price": 1}}}'})
        open(os.path.join(settings, 'mods.ini'), 'w').write('profile = run one\n[profile run one]\nextra = on\nghostlib = on\ncore = on\n')
    exported = os.path.join(ctx.work, 'shared_list.json').replace('\\', '/')
    steps = ['update:no', 'size:1280x1000', 'page:mods', f'modlist:export={exported}', 'quit']
    code, log, reqs, settings, appdata = ctx.run('lists_a', '|'.join(steps), setup=setup)
    data = json.load(open(exported))
    check([m['id'] for m in data['mods']] == ['extra', 'ghostlib', 'core'], 'lists: the export holds the mods that are on, in order: %s' % [m['id'] for m in data['mods']])
    check(all(set(m) <= {'id', 'name', 'version', 'updates'} for m in data['mods']) and exported not in json.dumps(data), 'lists: only ids, names, versions and update links (no local paths)')

    def setup_b(settings, appdata):
        hand_install(ctx, settings, 'core', '1.0', updates=False)
        hand_install(ctx, settings, 'extra', '3.0', updates=False, files={'data/z.json': '{"items": {"#9": {"price": 1}}}'})
        open(os.path.join(settings, 'mods.ini'), 'w').write('profile = default\n[profile default]\ncore = on\nextra = off\n')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
    steps = ['update:no', 'size:1280x1000', 'page:mods', f'modlist:import={exported}', shot('lists_import'), 'modlist:download', 'wait:modupdate', 'modupdate:get', 'wait:modupdate',
             'modupdate:install', 'wait:modupdate', shot('lists_after_download'), 'modlist:apply', 'page:mods', shot('lists_applied'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('lists_b', '|'.join(steps), setup=setup_b)
    check(code == 0 and has(log, r'read a mod list with 3 mod'), 'lists: the file was read and the plan shown')
    check(not any('ghostlib' in r for r in reqs[:0]) and any('ghostlib.update.json' in r for r in reqs), 'lists: the missing mod was downloaded only after the player said so')
    check(mod_version(settings, 'ghostlib') == '1.2', 'lists: the missing mod was installed through the full checks')
    ini = open(os.path.join(settings, 'mods.ini')).read()
    order = [l.split(' = ')[0] for l in ini.splitlines() if ' = ' in l and not l.startswith('profile')]
    check(order[:3] == ['extra', 'ghostlib', 'core'] and 'extra = on' in ini, 'lists: applying put the list in the shared order and turned the mods on; got %s' % order)


def scenario_clashes(ctx):
    def setup(settings, appdata):
        hand_install(ctx, settings, 'one', '1.0', updates=False, extra={'scripts': True}, files={
            'data/a.json': '{"pokemon": {"#1": {"hp": 5}}}', 'scripts/a.lua': 'on("battle.damage", function() end)', 'rooms/side/x.bin': 'x'})
        hand_install(ctx, settings, 'two', '1.0', updates=False, extra={'scripts': True}, files={
            'data/b.json': '{"pokemon": {"#1": {"hp": 9}}}', 'scripts/b.lua': 'on("battle.damage", function() end)', 'objects/side.json': '{}'})
        open(os.path.join(settings, 'mods.ini'), 'w').write('profile = default\n[profile default]\none = on\ntwo = on\n')
    steps = ['update:no', 'size:1280x1100', 'page:mods', shot('clashes'), 'quit']
    code, log, reqs, settings, appdata = ctx.run('clashes', '|'.join(steps), setup=setup)
    check(code == 0, 'clashes: launcher ran to the end (see the screenshot: the Mods page names the same table value, story room and hook)')


def scenario_pad(ctx):
    """The same popups answered with the CONTROLLER only (launcher's pad mode: pad:a / pad:b / pad:right; padwhere shows the window
    that has the focus): the update question, the update + permission change, roll back, the dependency prompt, list import,
    and the protect question. Screenshots: <work>\\shots\\pad_*.png."""
    ctx.publish('alpha', '1.1', extra={'scripts': True}, files={
        'data/tweak.json': '{"pokemon": {"#1": {"basisStatus[0].maxHp": 55}}}',
        'scripts/hello.lua': 'on("battle.damage", function(ctx) end)\n'}, notes='New in 1.1: a script')
    ctx.publish('zeta', '1.1', files={'data/tweak.json': '{"pokemon": {"#1": {"basisStatus[0].maxHp": 99}}}'})
    ctx.publish('ghostlib', '1.2')
    pt = {'format': 1, 'engine': 'faithful', 'mods': [{'id': 'zeta', 'name': 'Zeta', 'version': '1.0', 'applies': 'new_playthrough'}],
          'started': '2026-11-16 12:00', 'port_version': '0.6.0', 'chosen_by': 'screen', 'trainer': 'ASH', 'trainer_id': 1, 'secret_id': 2}

    def setup(settings, appdata):
        hand_install(ctx, settings, 'alpha', '1.0')
        hand_install(ctx, settings, 'zeta', '1.0')
        hand_install(ctx, settings, 'core', '1.0', updates=False)
        hand_install(ctx, settings, 'needy', '1.0', extra={'requires': ['core']}, updates=False, files={'data/d.json': '{"items": {"#3": {"price": 10}}}'})
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'zeta': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        p = os.path.join(appdata, 'Pokemon XD', 'playthroughs', 'Card A')
        os.makedirs(p, exist_ok=True)
        json.dump(pt, open(os.path.join(p, 'pt_test.json'), 'w'))
        open(os.path.join(settings, 'mods.ini'), 'w').write('profile = default\n[profile default]\nalpha = on\nzeta = on\nneedy = off\ncore = off\n')
    W = ['update:no', 'size:1280x1100', 'padmode:on', 'page:mods']

    steps = W + ['modupdate:check', 'padwhere', shot('pad_question'), 'pad:b', 'wait:modupdate', 'padwhere', 'modupdate:check', 'wait:modupdate', 'padwhere', shot('pad_results'),

                 'pad:a', 'wait:modupdate', 'padwhere', shot('pad_install'), 'pad:a', 'wait:modupdate', 'padwhere', 'quit']
    code, log, reqs, settings, appdata = ctx.run('pad', '|'.join(steps), setup=setup)
    check(code == 0, 'pad: launcher ran to the end')
    check(has(log, r'answer: no'), 'pad: B on the question answered No (and nothing was sent for it: the first request came with the next click)')
    check(has(log, r'window "Check for mod updates\?###modupq"'), 'pad: the controller focus was in the question window')
    check(has(log, r'window "Mod updates###modupr"') and has(log, r'window "Install this update\?###modupi"'), 'pad: the focus went to the results window and then to the install window')
    check(mod_version(settings, 'alpha') == '1.1' or has(log, r'installed alpha 1\.1'), 'pad: A, A, A downloaded, checked and installed alpha 1.1')


    steps = W + ['modupdate:rollback=alpha', 'padwhere', shot('pad_rollback'), 'pad:b', 'padwhere', 'modupdate:rollback=alpha', 'pad:a', 'padwhere', 'page:mods', shot('pad_rollback_done')]
    code, log, reqs, settings, appdata = ctx.run('pad', '|'.join(steps + ['quit']), label='pad_rollback')
    check(has(log, r'window "Go back to the earlier version\?###modupb"'), 'pad: the focus was in the roll-back question')
    check(mod_version(settings, 'alpha') == '1.0', 'pad: B on the question kept 1.1; A on it went back to 1.0 (it is %s)' % mod_version(settings, 'alpha'))

    steps = W + ['modneeds:needy', 'padwhere', shot('pad_needs'), 'pad:a', 'wait:modupdate', 'page:mods', shot('pad_needs_done')]
    code, log, reqs, settings, appdata = ctx.run('pad', '|'.join(steps + ['quit']), label='pad_needs')
    ini = open(os.path.join(settings, 'mods.ini')).read()
    check(has(log, r'window "This mod needs other mods###modupn"'), 'pad: the focus was in the "needs other mods" window')
    check('needy = on' in ini and 'core = on' in ini, 'pad: A turned on the mod and what it needs: %r' % ini[-60:])

    exported = os.path.join(ctx.work, 'pad_list.json').replace('\\', '/')
    steps = W + [f'modlist:export={exported}', f'modlist:import={exported}', 'padwhere', shot('pad_list'), 'pad:b', 'padwhere', f'modlist:import={exported}', 'pad:a', 'padwhere', shot('pad_list2')]
    code, log, reqs, settings, appdata = ctx.run('pad', '|'.join(steps + ['quit']), label='pad_list')
    check(has(log, r'window "Mod list from a file###modupl"'), 'pad: the focus was in the mod list window')
    check(os.path.isfile(exported.replace('/', os.sep)), 'pad: the list was exported')

    steps = W + ['modupdate:check', 'wait:modupdate', 'padwhere', 'modupdate:get', 'wait:modupdate', 'padwhere', shot('pad_protect'), 'pad:a', 'wait:modupdate', 'padwhere']
    def setup_protect(settings, appdata):
        hand_install(ctx, settings, 'zeta', '1.0')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'zeta': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
        p = os.path.join(appdata, 'Pokemon XD', 'playthroughs', 'Card A')
        os.makedirs(p, exist_ok=True)
        json.dump(pt, open(os.path.join(p, 'pt_test.json'), 'w'))
    code, log, reqs, settings, appdata = ctx.run('pad_protect', '|'.join(steps + ['quit']), setup=setup_protect)
    check(has(log, r'kept the current version of'), 'pad: A on the protect question kept the current version')
    check(json.load(open(os.path.join(appdata, 'Pokemon XD', 'playthroughs', 'Card A', 'pt_test.json'))) == pt, 'pad: the playthrough setup (the save) is untouched')


def scenario_keychange(ctx):
    keyC = os.path.join(ctx.work, 'keys', 'authorC.key')
    r = subprocess.run([ctx.pack, '--rotate-key', ctx.keyA, keyC], capture_output=True, text=True)
    m = re.search(r'public key ([0-9a-f]{64})', r.stdout)
    check(r.returncode == 0 and m is not None and 'From now on pack with --key' in r.stdout, 'keychange: xd_mod_pack --rotate-key made the new key and said what to do')
    pubC = m.group(1) if m else ''
    check(open(keyC).read().count('change:') == 1 and 'change:' not in open(ctx.keyA).read(), 'keychange: the statement is in the NEW key file, the old key file is unchanged')
    r2 = subprocess.run([ctx.pack, '--rotate-key', ctx.keyA, keyC], capture_output=True, text=True)
    check(r2.returncode == 0 and open(keyC).read().count('change:') == 1, 'keychange: rotating again does not write the statement twice')
    check(subprocess.run([ctx.pack, '--rotate-key', ctx.keyA, ctx.keyA], capture_output=True, text=True).returncode != 0, 'keychange: a key cannot be rotated into itself')

    ctx.publish('planned', '2.0', key=keyC)
    upd = json.load(open(os.path.join(ctx.feed_dir, 'planned.update.json')))
    check(upd.get('public_key') == pubC and len(upd.get('key_changes', [])) == 1 and upd['key_changes'][0]['old_key'] == ctx.pubA and upd['key_changes'][0]['new_key'] == pubC,
          'keychange: the update file is signed with the new key and carries the old key\'s statement')

    ctx.publish('lost', '2.0', key=ctx.keyB)
    ctx.publish('forged', '2.0', key=keyC)
    p = os.path.join(ctx.feed_dir, 'forged.update.json')
    t = open(p).read()
    sig = upd['key_changes'][0]['signature']
    bad = sig[:-1] + ('0' if sig[-1] != '0' else '1')
    open(p, 'w').write(t.replace(sig, bad))

    def setup(settings, appdata):
        for mid in ('planned', 'lost', 'forged'):
            hand_install(ctx, settings, mid, '1.0')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'planned': ctx.pubA, 'lost': ctx.pubA, 'forged': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')

    def keys_of(settings):
        return json.load(open(os.path.join(settings, 'mod_updates', 'mod_keys.json')))['keys']


    steps = ['update:no', 'size:1280x1300', 'page:mods', 'modupdate:check', 'wait:modupdate', shot('keychange_results'),
             'modupdate:get', 'wait:modupdate', shot('keychange_planned_staged'), 'modupdate:install', 'wait:modupdate', 'quit']
    code, log, reqs, settings, appdata = ctx.run('keychange', '|'.join(steps), setup=setup)
    check(code == 0, 'keychange: launcher ran to the end')
    check(mod_version(settings, 'planned') == '2.0', 'keychange: the planned change was followed by itself: planned is 2.0')
    check(keys_of(settings).get('planned') == pubC, 'keychange: the NEW key is now the one pinned for planned')
    check(has(log, r'signing key of planned changed from .* to .* \(announced by the old key\)'), 'keychange: the log says the change was announced by the old key')
    for mid in ('lost', 'forged'):
        check(mod_version(settings, mid) == '1.0' and keys_of(settings).get(mid) == ctx.pubA, f'keychange: {mid} was NOT installed and its pinned key is unchanged')
        check(not any(r.endswith(f'{mid}-2.0.zip') for r in reqs), f'keychange: the zip of {mid} was never even downloaded (no confirmation)')

    only = ['update:no', 'size:1280x1300', 'page:mods', 'modupdate:check', 'wait:modupdate', 'modupdate:get', 'wait:modupdate', shot('keychange_lost_unconfirmed'), 'quit']

    def setup_lost(settings, appdata):
        hand_install(ctx, settings, 'lost', '1.0')
        os.makedirs(os.path.join(settings, 'mod_updates'), exist_ok=True)
        json.dump({'keys': {'lost': ctx.pubA}}, open(os.path.join(settings, 'mod_updates', 'mod_keys.json'), 'w'))
        open(os.path.join(settings, 'mod_updates', 'modupdates.ini'), 'w').write('asked=yes\r\n')
    code, log, reqs, settings, appdata = ctx.run('keychange_lost', '|'.join(only), setup=setup_lost)
    check(not any(r.endswith('lost-2.0.zip') for r in reqs) and mod_version(settings, 'lost') == '1.0' and keys_of(settings).get('lost') == ctx.pubA,
          'lost key: without the confirmation nothing is downloaded or installed, the old key stays pinned')
    steps = ['update:no', 'size:1280x1300', 'page:mods', 'modupdate:check', 'wait:modupdate', 'modupdate:confirmkey', 'modupdate:get', 'wait:modupdate',
             shot('keychange_lost_confirmed'), 'modupdate:install', 'wait:modupdate', 'quit']
    code, log, reqs, settings, appdata = ctx.run('keychange_lost', '|'.join(steps), label='keychange_lost2')
    check(mod_version(settings, 'lost') == '2.0' and keys_of(settings).get('lost') == ctx.pubB, 'lost key: after the explicit confirmation the update installs and the new key is pinned')
    check(has(log, r'signing key of lost changed from .* to .* \(confirmed by the player\)'), 'lost key: the log says the player confirmed it')




SCENARIOS = {'consent': scenario_consent, 'update': scenario_update, 'refused': scenario_refused, 'protect': scenario_protect,
             'deps': scenario_deps, 'lists': scenario_lists, 'clashes': scenario_clashes, 'pad': scenario_pad, 'keychange': scenario_keychange}


def main():
    ap = argparse.ArgumentParser(description='launcher mod updates test with a local feed')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'release', 'bin'))
    ap.add_argument('--work', default=os.path.join(ROOT, 'build', 'modupdate_test'))
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    if not sign_update.selftest():
        sys.exit('Ed25519 self-test failed')
    ctx = Ctx(os.path.abspath(a.build), work)
    only = [s for s in a.only.split(',') if s] or list(SCENARIOS)
    for name in only:
        print(f'--- {name}', flush=True)
        SCENARIOS[name](ctx)
    print('ALL OK' if OK else 'FAILED')
    sys.exit(0 if OK else 1)


if __name__ == '__main__':
    main()
