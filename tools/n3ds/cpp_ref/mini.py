import os, sys, subprocess, shutil

tag, pack, gen4, items = sys.argv[1:5]
B = ''
DISC = ''
base = os.path.abspath('mini_' + tag); shutil.rmtree(base, ignore_errors=True)
for d in ('card', 'settings', 'cache', 'appdata', 'out'): os.makedirs(os.path.join(base, d))
open(os.path.join(base, 'settings', 'settings.ini'), 'w').write('format = 1\npreset = enhanced\n')
os.makedirs(os.path.join(base, 'settings', 'extra_assets'))
links = []
for src, sub in ((gen4, 'pbr_gen4'), (pack, 'usum_gen57')):
    l = os.path.join(base, 'settings', 'extra_assets', sub)
    subprocess.run(['cmd', '/c', 'mklink', '/J', l, os.path.abspath(src)], check=True, stdout=subprocess.DEVNULL); links.append(l)
env = {k: v for k, v in os.environ.items() if not k.startswith('XD_')}
env.update({'XD_NO_MESSAGE_BOXES': '1', 'XD_WINDOW_OFFSCREEN': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0', 'XD_RECORD': '0', 'SDL_AUDIO_DRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
            'XD_CARD_DIR': base + r'\card', 'XD_SETTINGS_DIR': base + r'\settings', 'XD_CACHE_DIR': base + r'\cache', 'XD_RUN_LABEL': 'mini ' + tag, 'XD_DETERMINISTIC': '1',
            'XD_CHALLENGES': 'gen4_pokemon,gen57_pokemon', 'XD_RENDER_ICON_DIR': base + r'\out', 'XD_RENDER_ICON_SIZE': '256', 'APPDATA': base + r'\appdata', 'XD_RENDER_ICON': items})
subprocess.run([B, DISC], env=env, stdout=open(base + r'\run.log', 'w'), stderr=subprocess.STDOUT, timeout=1800)
for l in links: os.rmdir(l)
print(sorted(os.listdir(base + r'\out')))
