"""Converted Pokemon in a real battle: the opening's simulation battle with its two Pokemon replaced.

  python tools/pbr_convert/battle.py <work folder> --exe <xd.exe> --name <run> --mine <model>:<SPECIES>
         --foe <model>:<SPECIES> [--plain] [--heap 7900-7902] [--timeout 170]

<model> is the converted file's name (<work>/out/<model>.pkx), <SPECIES> the game's species name for the data
mod (PIDGEOT). The run replays tests/recordings/tutorial_battle_win_det.txt on the virtual clock (so the same
presses fight the battle), with a mod of its own made of the two converted files and a data mod that makes the
simulation battle's two Pokemon these species. --plain: the same battle with XD's own models (to compare).
Pictures at fixed game frames -> <work>/battles/<run>/shots and one sheet <work>/battles/<run>.png.
The run is ended by the timeout (exit 124 is expected). Game pictures: never outside build/.
"""
import argparse, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sheets

FRAMES = [7400, 7600, 7800, 7900, 8000, 8100, 8200, 8400, 8600, 8800, 9000, 9200, 9400, 9700, 10000, 10400]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('work')
    ap.add_argument('--exe', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--mine', required=True)
    ap.add_argument('--foe', required=True)
    ap.add_argument('--plain', action='store_true')
    ap.add_argument('--heap', default='7900-7902')
    ap.add_argument('--timeout', type=int, default=170)
    ap.add_argument('--frames', help='picture frames A-B/step (default: the fixed list)')
    a = ap.parse_args()
    frames = FRAMES
    if a.frames:
        span, _, step = a.frames.partition('/')
        lo, _, hi = span.partition('-')
        frames = list(range(int(lo), int(hi) + 1, int(step or 1)))
    work = os.path.abspath(a.work)
    table = sheets.pairs(work)
    (m1, s1), (m2, s2) = a.mine.split(':'), a.foe.split(':')
    run = os.path.join(work, 'battles', a.name)
    os.makedirs(run, exist_ok=True)
    data = os.path.join(run, 'sim_species.json')
    with open(data, 'w') as f:
        json.dump({'_note': 'test only: the simulation battle\'s two Pokemon',
                   'virtual_pokemon': {'#1': {'pokemonID': s1}, '#2': {'pokemonID': s2}}}, f)
    sheets.make_mod(os.path.join(run, 'settings'), work, [] if a.plain else sorted(set([m1, m2])), table, data)
    shots = os.path.join(run, 'shots')
    os.makedirs(shots, exist_ok=True)
    for old in os.listdir(shots):
        os.remove(os.path.join(shots, old))
    code = sheets.run_game(a.exe, run, [
        'XD_REPLAY=' + os.path.join(REPO, 'tests', 'recordings', 'tutorial_battle_win_det.txt').replace(os.sep, '/'),
        'XD_DETERMINISTIC=1', 'XD_DISC_TIMING=fast', 'XD_TURBO=4', 'XD_HEAP_STATS=' + a.heap,
        'XD_SHOT_DIR=' + shots.replace(os.sep, '/'), 'XD_SHOT_FRAMES=' + ','.join(str(f) for f in frames)],
        a.timeout, os.path.join(work, 'cache'))
    print('exit', code)
    from PIL import Image
    pics = sorted(p for p in os.listdir(shots) if p.endswith('.png'))
    if pics:
        w, h = 320, 240
        cols = 4
        rows = (len(pics) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * w, rows * h))
        for i, p in enumerate(pics):
            sheet.paste(Image.open(os.path.join(shots, p)).convert('RGB').resize((w, h)), ((i % cols) * w, (i // cols) * h))
        sheet.save(run + '.png')
        print(run + '.png', len(pics), 'pictures')
    log = open(os.path.join(run, 'out.log'), errors='replace').read() + open(os.path.join(run, 'err.log'), errors='replace').read()
    for l in log.split('\n'):
        if 'heap 3' in l or 'STUCK' in l or 'crash' in l.lower() or 'assert' in l.lower():
            print(l[:220])


if __name__ == '__main__':
    sys.exit(main())
