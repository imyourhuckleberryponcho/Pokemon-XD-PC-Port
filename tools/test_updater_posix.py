r"""End-to-end test of the launcher's updater on Linux and the Mac (docs/UPDATER.md) with a LOCAL feed: nothing goes to
the internet (the launcher's test switches XD_UPDATE_FEED / XD_UPDATE_KEY accept only a feed on this computer).
The partner of tools/test_updater.py (Windows).

    python3 tools/test_updater_posix.py --system linux --build <build root>/linux-release/bin [--work <folder>]
    python3 tools/test_updater_posix.py --system mac --app "<...>/Pokemon XD.app" [--work <folder>]

Makes a throwaway signing key and the update files of all three systems with tools/publish/sign_update.py pack
(the running system's package is real, the others are small stand-ins), serves them from a tiny HTTP server on
127.0.0.1, then runs the launcher (window off screen, no disc, never Play) through:
  - the first start: the question, and nothing sent before it is answered; "no" = never online;
  - "yes": the check, the offer (version, what's new), "Update now": downloaded, checked, installed; the player's data
    unchanged; Linux: game-0.0.2 + game_folder.txt, the start script opens it, file modes kept, Go back and forward;
    Mac: the new app waits (the running one untouched), a restart puts it in place by the helper, the old one kept,
    Go back the same way, and an update waiting at the next start is put in place by that start;
  - tampered: a changed manifest (bad signature), another key, another SHA-256, a package larger than said: each
    refused, nothing installed, nothing left behind;
  - "Skip this version".
Exit code 0 = everything passed. Logs and screenshots in <work>/logs and <work>/shots.
"""
import argparse
import glob
import io
import os
import secrets
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'publish'))
sys.dont_write_bytecode = True
import sign_update  # noqa: E402
import test_updater as tu  # noqa: E402  (the Windows test: the local feed server and helpers)

check = tu.check
APP = 'Pokemon XD.app'
FOLDER = 'Pokemon XD PC'


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f'{cmd} failed:\n{r.stdout}\n{r.stderr}')
    return r.stdout


def mode_of(path):
    return stat.S_IMODE(os.stat(path).st_mode)


class Run:
    """One launcher run with its own HOME / data folders, settings and card, and the test feed."""

    def __init__(self, a, name, feed, pub, shots):
        self.a, self.feed, self.pub, self.shots, self.name = a, feed, pub, shots, name
        self.data = os.path.join(a.work, 'data_' + name)
        self.home = os.path.join(self.data, 'home')
        self.xdg = os.path.join(self.data, 'xdg')
        self.settings = os.path.join(self.data, 'settings')
        self.card = os.path.join(self.data, 'card')
        for d in (self.home, self.xdg, self.settings, self.card):
            os.makedirs(d, exist_ok=True)

    def staging_candidates(self):
        c = [os.path.join(self.xdg, 'Pokemon XD', 'updates'), os.path.join(self.home, '.local', 'share', 'Pokemon XD', 'updates')]
        if self.a.system == 'mac':  # (SDL's preference folder: the real home of this account, whatever HOME says)
            c.append(os.path.join(os.path.expanduser('~'), 'Library', 'Application Support', 'Pokemon XD', 'updates'))
            c.append(os.path.join(self.home, 'Library', 'Application Support', 'Pokemon XD', 'updates'))
        return c

    def staging(self):
        for c in self.staging_candidates():
            if os.path.isdir(c):
                return c
        return None

    def forget_state(self):
        """The updater's own memory (asked / skipped) of the previous run: gone (Mac runs share one account)."""
        for c in self.staging_candidates():
            shutil.rmtree(c, ignore_errors=True)

    def env(self, script, extra=None):
        e = dict(os.environ)
        e.update({'HOME': self.home, 'XDG_DATA_HOME': self.xdg, 'XDG_CONFIG_HOME': os.path.join(self.home, '.config'),
                  'XD_SETTINGS_DIR': self.settings, 'XD_CARD_DIR': self.card, 'XD_WINDOW_OFFSCREEN': '1',
                  'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0', 'XD_LAUNCHER_RENDER': '0', 'XD_NO_MESSAGE_BOXES': '1',
                  'SDL_AUDIO_DRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'XD_UPDATE_NO_OPEN': '1',
                  'XD_UPDATE_FEED': self.feed.url('update.json'), 'XD_UPDATE_KEY': self.pub,
                  'XD_LAUNCHER_SCRIPT': script.replace('{shots}', self.shots)})
        if self.a.system == 'mac':
            e['SDL_RENDER_DRIVER'] = 'software'
        e.pop('XD_UPDATE_FEED_URL', None)
        if extra:
            e.update(extra)
        return e

    def launcher(self, exe, script, label, extra=None, timeout=180):
        log = os.path.join(self.a.work, 'logs', label + '.log')
        os.makedirs(os.path.dirname(log), exist_ok=True)
        before = len(self.feed.requests)
        with open(log, 'w', encoding='utf-8', errors='replace') as f:
            try:
                r = subprocess.run([exe], env=self.env(script, extra), stdout=f, stderr=subprocess.STDOUT,
                                   timeout=timeout, cwd=os.path.dirname(exe))
                code = r.returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
        text = open(log, encoding='utf-8', errors='replace').read()
        return code, text, self.feed.requests[before:]


# ── The install under test, per system ──

class Install:
    """A player's installation of version `version`: Linux = a folder with the start script and game/; Mac = the app."""

    def __init__(self, a, name, version):
        self.a, self.version = a, version
        self.root = os.path.join(a.work, 'install_' + name, FOLDER)
        if a.system == 'linux':
            self.game = os.path.join(self.root, 'game')
            self.exe = os.path.join(self.game, 'xd_launcher')
            self.starter = os.path.join(self.root, 'Pokemon XD.sh')
        else:
            self.app = os.path.join(self.root, APP)
            self.macos = os.path.join(self.app, 'Contents', 'MacOS')
            self.exe = os.path.join(self.macos, 'xd_launcher')
            self.versionfile = os.path.join(self.app, 'Contents', 'Resources', 'version.txt')

    def make(self):
        a = self.a
        os.makedirs(self.root, exist_ok=True)
        if a.system == 'linux':
            make_linux_tree(a, self.root, self.version)
        else:
            shutil.copytree(a.app, self.app, symlinks=True)
            set_mac_version(self.app, self.version)
        return self

    def current_version(self):
        p = os.path.join(self.game, 'version.txt') if self.a.system == 'linux' else self.versionfile
        return open(p).read().strip() if os.path.isfile(p) else None

    def listing(self):
        return sorted(os.listdir(self.root))


def make_linux_tree(a, root, version):
    """A Linux player folder like tools/package_linux.py's: Pokemon XD.sh + game/ (launcher, libraries, version.txt)."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import package_linux  # noqa: E402
    game = os.path.join(root, 'game')
    os.makedirs(game)
    build = os.path.abspath(a.build)
    shutil.copy2(os.path.join(build, 'xd_launcher'), game)
    for f in glob.glob(os.path.join(build, '*.so*')):
        shutil.copy2(f, game, follow_symlinks=True)
    if os.path.isfile(os.path.join(build, 'icon.png')):
        shutil.copy2(os.path.join(build, 'icon.png'), game)
    if os.path.isdir(os.path.join(build, 'fonts')):
        shutil.copytree(os.path.join(build, 'fonts'), os.path.join(game, 'fonts'))
    with open(os.path.join(game, 'xd'), 'w') as f:  # (the game itself is never started here)
        f.write('#!/bin/sh\necho stand-in game\n')
    os.chmod(os.path.join(game, 'xd'), 0o755)
    with open(os.path.join(game, 'version.txt'), 'w') as f:
        f.write(version + '\n')
    with open(os.path.join(root, 'Pokemon XD.sh'), 'w', newline='\n') as f:
        f.write(package_linux.START_SCRIPT)
    os.chmod(os.path.join(root, 'Pokemon XD.sh'), 0o755)
    with open(os.path.join(root, 'Read me.txt'), 'w') as f:
        f.write('test\n')


def set_mac_version(app, version):
    """The app's version text changed (it is a symlink in MacOS/), then signed ad hoc again as package_mac.sh does."""
    with open(os.path.join(app, 'Contents', 'Resources', 'version.txt'), 'w') as f:
        f.write(version + '\n')
    link = os.path.join(app, 'Contents', 'MacOS', 'version.txt')
    if not os.path.lexists(link):
        os.symlink('../Resources/version.txt', link)
    if shutil.which('codesign'):
        subprocess.run(['codesign', '--force', '--sign', '-', app], capture_output=True)


def tar_of_linux_tree(a, root, out):
    """The Linux tar.gz in package_linux.py's layout from a player folder (modes kept)."""
    top = f'Pokemon XD PC {os.path.basename(out)} - Linux (test)'
    with tarfile.open(out, 'w:gz') as t:
        t.add(root, arcname=top)


def stand_in_windows(work, version):
    folder = os.path.join(work, 'standin_windows', FOLDER)
    game = os.path.join(folder, 'game')
    os.makedirs(game, exist_ok=True)
    for n in ('xd_launcher.exe', 'xd.exe'):
        open(os.path.join(game, n), 'wb').write(b'MZ stand-in ' + version.encode())
    open(os.path.join(game, 'version.txt'), 'w').write(version + '\r\n')
    return folder


def stand_in_mac_zip(work, version):
    path = os.path.join(work, 'standin_mac', f'Pokemon XD PC {version} - Mac (test).zip')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    top = f'Pokemon XD PC {version} - Mac (test)/{APP}/Contents/'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in (('MacOS/xd_launcher', b'stand-in', 0o100755), ('MacOS/xd', b'stand-in', 0o100755),
                                 ('Resources/version.txt', (version + '\n').encode(), 0o100644)):
            i = zipfile.ZipInfo(top + name)
            i.create_system = 3
            i.external_attr = mode << 16
            z.writestr(i, data)
    return path


def stand_in_linux_tar(work, version):
    path = os.path.join(work, 'standin_linux', f'Pokemon XD PC {version} - Linux (test).tar.gz')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with tarfile.open(path, 'w:gz') as t:
        for name, data, mode in (('xd_launcher', b'stand-in', 0o755), ('xd', b'stand-in', 0o755), ('version.txt', (version + '\n').encode(), 0o644)):
            i = tarfile.TarInfo(f'Pokemon XD PC {version} - Linux (test)/game/{name}')
            i.size, i.mode = len(data), mode
            t.addfile(i, io.BytesIO(data))
    return path


def mac_zip_of_app(work, app, version):
    out = os.path.join(work, 'real_mac', f'Pokemon XD PC {version} - Mac (test).zip')
    folder = os.path.join(work, 'real_mac', f'Pokemon XD PC {version} - Mac (test)')
    os.makedirs(folder, exist_ok=True)
    shutil.copytree(app, os.path.join(folder, APP), symlinks=True)
    set_mac_version(os.path.join(folder, APP), version)
    open(os.path.join(folder, 'Read me.txt'), 'w').write('test\n')
    sh(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', folder, out])
    return out


def main():
    ap = argparse.ArgumentParser(description='launcher updater test on Linux / Mac with a local feed')
    ap.add_argument('--system', choices=('linux', 'mac'), required=True)
    ap.add_argument('--build', help='Linux: the build folder holding xd_launcher and its libraries')
    ap.add_argument('--app', help='Mac: the packaged Pokemon XD.app (any version)')
    ap.add_argument('--work', default=os.path.join(ROOT, 'build', 'updater_test_' + 'posix'))
    ap.add_argument('--keep', action='store_true')
    a = ap.parse_args()
    a.work = os.path.abspath(a.work)
    if a.system == 'linux' and not a.build:
        sys.exit('--build <folder with xd_launcher> is needed')
    if a.system == 'mac' and not a.app:
        sys.exit('--app <Pokemon XD.app> is needed')
    if not sign_update.selftest():
        sys.exit('Ed25519 self-test failed')
    if os.path.isdir(a.work):
        shutil.rmtree(a.work)
    os.makedirs(a.work)
    shots = os.path.join(a.work, 'shots')
    os.makedirs(shots)
    SYS = a.system
    TARNAME = 'Pokemon-XD-PC-0.0.2-linux.tar.gz'
    FILE = TARNAME if SYS == 'linux' else 'Pokemon-XD-PC-0.0.2-mac.zip'

    # The update 0.0.2 of all three systems, signed with a throwaway key.
    seed = secrets.token_bytes(32)
    pub = sign_update.public_key(seed).hex()
    keyfile = os.path.join(a.work, 'keys', 'test_key.txt')
    os.makedirs(os.path.dirname(keyfile))
    with open(keyfile, 'w', encoding='ascii') as f:
        f.write(f'# test only\nprivate: {seed.hex()}\npublic: {pub}\n')
    feed_dir = os.path.join(a.work, 'feed')
    feed = tu.Feed(feed_dir)
    if SYS == 'linux':
        new = Install(a, 'source002', '0.0.2').make()
        linux_pkg = os.path.join(a.work, 'src', 'Pokemon XD PC 0.0.2 - Linux (test).tar.gz')
        os.makedirs(os.path.dirname(linux_pkg))
        tar_of_linux_tree(a, new.root, linux_pkg)
        mac_pkg = stand_in_mac_zip(a.work, '0.0.2')
    else:
        linux_pkg = stand_in_linux_tar(a.work, '0.0.2')
        mac_pkg = mac_zip_of_app(a.work, a.app, '0.0.2')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'publish', 'sign_update.py'), 'pack',
                        '--windows', stand_in_windows(a.work, '0.0.2'), '--linux', linux_pkg, '--mac', mac_pkg,
                        '--url-base', feed.url('v{version}/'),
                        '--notes', 'Test notes for 0.0.2.\n- First change\n- Second change: café',
                        '--out', os.path.join(feed_dir, 'v0.0.2'), '--key', keyfile, '--public', pub],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    shutil.move(os.path.join(feed_dir, 'v0.0.2', 'update.json'), os.path.join(feed_dir, 'update.json'))
    manifest, error = sign_update.read_signed(os.path.join(feed_dir, 'update.json'), bytes.fromhex(pub))
    check(error is None and manifest['version'] == '0.0.2', 'sign_update.py pack: the manifest verifies in Python')
    systems = manifest.get('systems', {})
    check(sorted(systems) == ['linux', 'mac', 'windows'] and all(
        os.path.getsize(os.path.join(feed_dir, 'v0.0.2', e['file'])) == e['size'] and
        sign_update.sha256_of(os.path.join(feed_dir, 'v0.0.2', e['file'])) == e['sha256'] for e in systems.values()),
        'the manifest lists windows, linux and mac, each with its file, size and SHA-256')
    check((manifest['url'], manifest['size'], manifest['sha256']) ==
          (systems['windows']['url'], systems['windows']['size'], systems['windows']['sha256']),
          'the old top fields (what 0.4.0 launchers read) are the Windows entry')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'publish', 'sign_update.py'), 'verify',
                        os.path.join(feed_dir, 'update.json'), '--public', pub, '--dir', os.path.join(feed_dir, 'v0.0.2')],
                       capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.count('file ok') == 3, 'sign_update.py verify --dir: all three files')
    other = secrets.token_bytes(32)
    sign_update.write_signed(os.path.join(feed_dir, 'update_otherkey.json'), manifest, other)

    def installed_version(inst):
        return inst.current_version()

    # 1. The first start: the question, and nothing sent before it is answered. "No": never online.
    run = Run(a, 'no', feed, pub, shots)
    inst = Install(a, 'no', '0.0.1').make()
    code, log, reqs = run.launcher(inst.exe, 'shot:{shots}/1_first_question.png|quit', '1_first_start')
    check(code == 0 and 'version 0.0.1' in log, f'first start runs (version 0.0.1 from version.txt; exit {code})')
    check(reqs == [] and 'updates: checking' not in log, 'first start: nothing sent before the question is answered')
    code, log, reqs = run.launcher(inst.exe, 'update:no|quit', '1_answer_no')
    check('check at start: no' in log and reqs == [], '"No": nothing sent')
    code, log, reqs = run.launcher(inst.exe, 'quit', '1_after_no')
    check(reqs == [] and 'updates: checking' not in log, 'after "No": the next start sends nothing either')
    ini = os.path.join(run.staging() or '', 'updater.ini')
    check(os.path.isfile(ini) and 'asked=yes' in open(ini).read(), 'the answer is remembered (updater.ini)')
    code, log, reqs = run.launcher(inst.exe, 'update:check|wait:update|update:notnow|quit', '1_check_now_with_setting_off')
    check(reqs == ['/update.json'] and 'Version 0.0.2 is available' in log,
          '"Check now" works with the setting off (asks the feed once, nothing downloaded)')

    # 2. "Yes": the check at start, the offer, Update now.
    run = Run(a, 'yes', feed, pub, shots)
    run.forget_state()
    inst = Install(a, 'yes', '0.0.1').make()
    for rel in ('settings/mods/somemod/mod.json', 'card/Card A/marker.gci.txt'):
        p = os.path.join(run.data, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'w').write('player data ' + rel)
    data_before = tu.tree_digest(run.data, skip=('settings/settings.ini', 'xdg', 'home', 'settings/mods.ini'))
    before_tree = tu.tree_digest(inst.game if SYS == 'linux' else inst.app) if SYS == 'linux' else None
    code, log, reqs = run.launcher(
        inst.exe, 'update:yes|wait:update|shot:{shots}/2_offer.png|update:now|wait:update|shot:{shots}/2_installed.png|'
                  'update:notnow|size:1100x1500|page:disc|shot:{shots}/2_disc_card.png|quit', '2_update_now')
    check('check at start: yes' in log and 'Version 0.0.2 is available' in log, '"Yes": checked at once, 0.0.2 offered')
    check(reqs == ['/update.json', '/v0.0.2/' + FILE], 'Update now: the manifest, then this system\'s package only: ' + str(reqs))
    if SYS == 'linux':
        check('Version 0.0.2 is installed' in log, 'installed: ' + ' '.join(l for l in log.splitlines() if 'updates:' in l)[-300:])
        new = os.path.join(inst.root, 'game-0.0.2')
        check(open(os.path.join(new, 'version.txt')).read().strip() == '0.0.2' and os.path.isfile(os.path.join(new, 'xd')),
              'game-0.0.2 is complete')
        check(tu.read_pointer(inst.root) == {'current': 'game-0.0.2', 'previous': 'game'}, 'game_folder.txt: the start script will open game-0.0.2')
        check(tu.tree_digest(inst.game) == before_tree, 'the running version\'s folder is untouched')
        check(os.access(os.path.join(new, 'xd_launcher'), os.X_OK) and os.access(os.path.join(new, 'xd'), os.X_OK) and
              mode_of(os.path.join(new, 'xd_launcher')) == mode_of(os.path.join(inst.game, 'xd_launcher')),
              'the executable bits survived (xd_launcher, xd): ' + oct(mode_of(os.path.join(new, 'xd_launcher'))))
        check(os.path.isfile(inst.starter) and os.access(inst.starter, os.X_OK), 'the start script is still there and executable')
        check(not os.path.exists(os.path.join(inst.root, 'game-0.0.2.partial')), 'nothing half-installed')
    else:
        check('Version 0.0.2 is ready' in log, 'update ready: ' + ' '.join(l for l in log.splitlines() if 'updates:' in l)[-300:])
        pend = os.path.join(inst.root, '.Pokemon XD updates', 'pending', APP)
        check(os.path.isfile(os.path.join(pend, 'Contents', 'MacOS', 'xd_launcher')) and
              open(os.path.join(pend, 'Contents', 'Resources', 'version.txt')).read().strip() == '0.0.2',
              'the new app waits in the hidden folder')
        check(installed_version(inst) == '0.0.1', 'the running app is untouched')
        check(os.access(os.path.join(pend, 'Contents', 'MacOS', 'xd_launcher'), os.X_OK), 'the executable bit survived')
        link = os.path.join(pend, 'Contents', 'MacOS', 'version.txt')
        check(os.path.islink(link) or os.path.isfile(link), 'links inside the app were kept')
        if shutil.which('codesign'):
            r = subprocess.run(['codesign', '--verify', '--strict', pend], capture_output=True, text=True)
            check(r.returncode == 0, 'the waiting app\'s signature still verifies: ' + (r.stderr or '').strip()[:200])
    st = run.staging()
    check(st is not None and not any(n.endswith(('.zip', '.tar.gz', '.download')) for n in os.listdir(st)), 'the download is cleaned up')
    data_after = tu.tree_digest(run.data, skip=('settings/settings.ini', 'xdg', 'home', 'settings/mods.ini'))
    check(data_after == data_before, 'the player\'s data (settings folder, mods, card) unchanged')

    if SYS == 'linux':
        # The start script opens 0.0.2 (which itself checks at start: up to date).
        code, log, reqs = run.launcher(inst.starter, 'wait:update|shot:{shots}/2_new_version.png|quit', '2_starter_new')
        check("version 0.0.2 (player's folder, game-0.0.2)" in log, 'the start script opens version 0.0.2')
        check('You have the newest version (0.0.2)' in log, '0.0.2 checks at start: up to date')
        exe2 = os.path.join(inst.root, 'game-0.0.2', 'xd_launcher')
        code, log, reqs = run.launcher(exe2, 'update:back|quit', '2_go_back')
        check(tu.read_pointer(inst.root) == {'current': 'game', 'previous': 'game-0.0.2'}, 'Go back: the start script opens game again')
        code, log, reqs = run.launcher(inst.starter, 'wait:update|quit', '2_starter_old')
        check("version 0.0.1 (player's folder, game)" in log, 'the start script opens version 0.0.1 again')
        check('was skipped earlier: not asked' in log, 'the version left behind is not offered again at start')
        code, log, reqs = run.launcher(inst.exe, 'update:back|quit', '2_forward')
        check(tu.read_pointer(inst.root) == {'current': 'game-0.0.2', 'previous': 'game'}, '"Use version 0.0.2 again"')
    else:
        state = os.path.join(inst.root, '.Pokemon XD updates')
        # An update waiting when the launcher starts: that start puts it in place (and quits; the helper does it).
        code, log, reqs = run.launcher(inst.exe, 'wait:update|quit', '2_start_with_waiting_update')
        check('an update was waiting' in log, 'a start with an update waiting hands over to the helper: ' + ' '.join(log.splitlines()[-3:]))
        for _ in range(90):
            if installed_version(inst) == '0.0.2':
                break
            time.sleep(1)
        check(installed_version(inst) == '0.0.2', 'the helper put 0.0.2 in place')
        check(os.path.isfile(os.path.join(state, 'previous', APP, 'Contents', 'MacOS', 'xd_launcher')) and
              not os.path.exists(os.path.join(state, 'pending')), 'the old app is kept as "previous", nothing waits')
        check(not os.path.exists(os.path.join(state, 'swap.sh')), 'the helper removed itself')
        if shutil.which('codesign'):
            r = subprocess.run(['codesign', '--verify', '--strict', inst.app], capture_output=True, text=True)
            check(r.returncode == 0, 'the app in place verifies (ad hoc signature intact): ' + (r.stderr or '').strip()[:200])
        code, log, reqs = run.launcher(inst.exe, 'wait:update|size:1100x1500|page:disc|shot:{shots}/2_new_version.png|quit', '2_new_app')
        check('version 0.0.2' in log and 'You have the newest version (0.0.2)' in log, 'the new app runs: version 0.0.2, up to date')
        # Go back: previous becomes the waiting app; "Restart now" puts it in place.
        code, log, reqs = run.launcher(inst.exe, 'update:back|update:restart', '2_go_back')
        for _ in range(90):
            if installed_version(inst) == '0.0.1':
                break
            time.sleep(1)
        check(installed_version(inst) == '0.0.1', 'Go back + restart: 0.0.1 is in place again')
        code, log, reqs = run.launcher(inst.exe, 'wait:update|quit', '2_old_again')
        check('was skipped earlier: not asked' in log, 'the version left behind is not offered again at start')
        # Restart now straight after an update
        run2 = Run(a, 'yes2', feed, pub, shots)
        run2.forget_state()
        inst2 = Install(a, 'yes2', '0.0.1').make()
        code, log, reqs = run2.launcher(inst2.exe, 'update:yes|wait:update|update:now|wait:update|update:restart', '2_restart_now')
        for _ in range(90):
            if inst2.current_version() == '0.0.2':
                break
            time.sleep(1)
        check(inst2.current_version() == '0.0.2', '"Restart now": the helper puts the new app in place')

    # 3. Tampered feeds and packages: refused, nothing installed, nothing left.
    for mode, expect in (('badsig', 'signature is wrong'), ('wrongkey', 'signature is wrong'),
                         ('badhash', 'SHA-256 differs'), ('toobig', 'larger than expected')):
        feed.mode = mode
        run = Run(a, mode, feed, pub, shots)
        run.forget_state()
        inst = Install(a, mode, '0.0.1').make()
        listing = inst.listing()
        code, log, reqs = run.launcher(inst.exe, 'update:check|wait:update|update:now|wait:update|'
                                                 f'shot:{{shots}}/3_{mode}.png|quit', '3_' + mode)
        check(expect in log, f'{mode}: refused ({expect})')
        check(inst.listing() == listing and installed_version(inst) == '0.0.1', f'{mode}: nothing installed')
        st = run.staging()
        check(not st or not any(n.endswith(('.zip', '.tar.gz', '.download')) for n in os.listdir(st)), f'{mode}: no download left behind')
        if mode in ('badsig', 'wrongkey'):
            check(not any(r.endswith(('.zip', '.tar.gz')) for r in reqs), f'{mode}: the package was never downloaded')
    feed.mode = 'good'

    # 4. Skip this version.
    run = Run(a, 'skip', feed, pub, shots)
    run.forget_state()
    inst = Install(a, 'skip', '0.0.1').make()
    listing = inst.listing()
    run.launcher(inst.exe, 'update:yes|wait:update|update:skip|quit', '4_skip')
    code, log, reqs = run.launcher(inst.exe, 'wait:update|quit', '4_after_skip')
    check('was skipped earlier: not asked' in log and reqs == ['/update.json'], 'a skipped version is not asked about')
    code, log, reqs = run.launcher(inst.exe, 'wait:update|update:check|wait:update|quit', '4_check_now')
    check(log.count('Version 0.0.2 is available') == 2, '"Check now" still shows it')
    check(inst.listing() == listing, 'skip: nothing installed')

    # 5. An update that has no package for this system (an old, Windows-only manifest): nothing offered.
    only = os.path.join(a.work, 'feed_winonly')
    os.makedirs(only)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'publish', 'sign_update.py'), 'pack',
                        '--windows', stand_in_windows(a.work, '0.0.2'), '--url-base', feed.url('v{version}/'),
                        '--out', only, '--key', keyfile, '--public', pub], capture_output=True, text=True)
    check(r.returncode == 0 and 'no linux package given' in r.stdout, 'pack with a Windows package only works and says what is missing')
    shutil.copy(os.path.join(only, 'update.json'), os.path.join(feed_dir, 'update.json'))
    run = Run(a, 'winonly', feed, pub, shots)
    run.forget_state()
    inst = Install(a, 'winonly', '0.0.1').make()
    code, log, reqs = run.launcher(inst.exe, 'update:check|wait:update|quit', '5_windows_only')
    check('has no package for' in log and 'Version 0.0.2 is available' not in log and not any('.zip' in r or '.tar.gz' in r for r in reqs),
          'a Windows-only update is genuine but offered to nobody else')

    feed.server.shutdown()
    print('screenshots:', shots)
    if not a.keep and tu.OK:
        for d in os.listdir(a.work):
            if d not in ('shots', 'logs'):
                shutil.rmtree(os.path.join(a.work, d), ignore_errors=True)
    print('test_updater_posix: all passed' if tu.OK else 'test_updater_posix: FAILED')
    sys.exit(0 if tu.OK else 1)


if __name__ == '__main__':
    main()
