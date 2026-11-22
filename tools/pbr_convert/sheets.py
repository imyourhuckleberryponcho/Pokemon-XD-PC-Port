"""Side-by-side pictures: XD's own model and the converted one, drawn by the game itself (XD_RENDER_ICON).

  python tools/pbr_convert/sheets.py <work folder> --exe <xd.exe> --names a,b,c [--tag t] [--per-sheet 8] [--size 384]
  python tools/pbr_convert/sheets.py <work folder> --exe <xd.exe> --names a,b --anims 0,1,2,3,5,6 [--solo] [--tag t]
        one row per model, one column per animation slot (the game's :anim<n>, stopped 20 frames in); with --solo
        only the converted model (a Gen 4 model sits in a host's file: XD's picture would be another Pokemon)

Two game runs (plain, and with the converted files of <work>/out as one file mod), each with folders of its
own under <work>/sheet_runs/<tag>_xd|_pbr, muted, off screen. Sheets: <work>/sheets/<tag>_<n>.png, per Pokemon
XD's picture on the left and the converted one on the right. Game pictures: never outside build/.
"""
import argparse, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def pairs(work):
    out = {}
    for l in open(os.path.join(work, 'pairs.tsv')):
        c = l.rstrip('\n').split('\t')
        if len(c) >= 5 and c[1] and c[2]:
            if c[4] in ('', '0'):
                n = int(c[0][4:7])
                c[4] = str(n) if n <= 251 else '0'
            out[c[1]] = c
    return out


def make_mod(settings, work, names, table, data=None):
    mods = os.path.join(settings, 'mods')
    if os.path.isdir(mods):
        shutil.rmtree(mods)
    os.makedirs(settings, exist_ok=True)
    with open(os.path.join(settings, 'settings.ini'), 'w') as f:
        f.write('format = 1\npreset = original\n')
    ini = os.path.join(settings, 'mods.ini')
    if not names and not data:
        if os.path.exists(ini):
            os.remove(ini)
        return
    mod = os.path.join(mods, 'pbrconv-test')
    os.makedirs(mod)
    with open(os.path.join(mod, 'mod.json'), 'w') as f:
        json.dump({'id': 'pbrconv-test', 'name': 'Converter test', 'version': '0', 'game': 'GXXE01',
                   'applies': 'new_playthrough'}, f)
    for n in names:
        c = table[n]
        dst = os.path.join(mod, 'fsys', c[2])
        os.makedirs(dst, exist_ok=True)
        shutil.copyfile(os.path.join(work, 'out', n + '.pkx'), os.path.join(dst, '#%s' % c[3]))
    if data:
        os.makedirs(os.path.join(mod, 'data'), exist_ok=True)
        shutil.copyfile(data, os.path.join(mod, 'data', os.path.basename(data)))
    with open(ini, 'w') as f:
        f.write('profile = default\n\n[profile default]\npbrconv-test = on\n')


def run_game(exe, run, pairs_, timeout, cache):
    for old in ('out.log', 'err.log', 'exit.txt'):
        p = os.path.join(run, old)
        if os.path.exists(p):
            os.remove(p)
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(REPO, 'tools', 'linux', 'win_run.ps1'),
           exe, run, 'TIMEOUT=%d' % timeout, 'CACHE=' + cache] + pairs_
    return subprocess.run(cmd).returncode


def anim_sheets(a, work, table, names, slots):
    """Per model a row per picture source, per animation slot a column. Sources: XD's own model (unless --solo),
    --vs <other work folder>'s converted file ("before"), and this work folder's converted file."""
    from PIL import Image, ImageDraw
    srcs = []
    if not a.solo:
        srcs.append(('xd', None))
    if a.vs:
        srcs.append(('before', os.path.abspath(a.vs)))
    srcs.append(('conv', work))
    dirs = {}
    for label, w in srcs:
        run = os.path.join(work, 'sheet_runs', '%s_%s' % (a.tag, label))
        icons = os.path.join(run, 'icons')
        dirs[label] = icons
        os.makedirs(icons, exist_ok=True)
        code = 0
        for n in names:

            make_mod(os.path.join(run, 'settings'), w or work, [n] if w else [], table)
            spec = ','.join('pokemon:%s:%s_a%d:anim%d' % (table[n][4], n, k, k) for k in slots)
            code |= run_game(a.exe, run, ['XD_RENDER_ICON=' + spec, 'XD_RENDER_ICON_DIR=' + icons.replace(os.sep, '/'),
                                          'XD_RENDER_ICON_SIZE=%d' % a.size], 60 + 25 * len(slots), os.path.join(work, 'cache'))
        print(label, 'exit', code)
    s = a.size
    os.makedirs(os.path.join(work, 'sheets'), exist_ok=True)
    tint = {'xd': (70, 70, 80, 255), 'before': (80, 70, 60, 255), 'conv': (60, 80, 70, 255)}
    for k0 in range(0, len(names), a.per_sheet):
        part = names[k0:k0 + a.per_sheet]
        rows = len(part) * len(srcs)
        sheet = Image.new('RGB', (len(slots) * s, rows * (s + 16)), (40, 40, 48))
        dr = ImageDraw.Draw(sheet)
        r = 0
        for n in part:
            for label, _ in srcs:
                for c, k in enumerate(slots):
                    x, y = c * s, r * (s + 16)
                    p = os.path.join(dirs[label], '%s_a%d.png' % (n, k))
                    if os.path.exists(p):
                        im = Image.open(p).convert('RGBA').resize((s, s))
                        bg = Image.new('RGBA', (s, s), tint[label])
                        bg.alpha_composite(im)
                        sheet.paste(bg.convert('RGB'), (x, y + 16))
                    else:
                        dr.text((x + 8, y + 40), 'NO PICTURE', fill=(255, 80, 80))
                    dr.text((x + 4, y + 2), '%s %s slot %d' % (n, label, k), fill=(255, 255, 255))
                r += 1
        out = os.path.join(work, 'sheets', '%s_%d.png' % (a.tag, k0 // a.per_sheet + 1))
        sheet.save(out)
        print(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('work')
    ap.add_argument('--exe', required=True)
    ap.add_argument('--names', required=True)
    ap.add_argument('--tag', default='sheet')
    ap.add_argument('--per-sheet', type=int, default=8)
    ap.add_argument('--size', type=int, default=384)
    ap.add_argument('--only', default='')
    ap.add_argument('--anims', default='', help='slot numbers: a sheet row per model, a column per slot')
    ap.add_argument('--solo', action='store_true', help='with --anims: the converted side only')
    ap.add_argument('--vs', default='', help='with --anims: another work folder whose converted files are drawn as a "before" row')
    ap.add_argument('--species', default='', help='name=number,...: XD species (model) numbers for models whose '
                    'header holds none (Hoenn ones: XD numbers differ from the national ones; xd_data_mods --show '
                    'pokemon_data N, field pkxDataId)')
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    table = pairs(work)
    for kv in filter(None, a.species.split(',')):
        k, v = kv.split('=')
        if k in table:
            table[k][4] = v
    names = [n for n in a.names.split(',') if n in table and table[n][4] != '0' and os.path.exists(os.path.join(work, 'out', n + '.pkx'))]
    print('left out:', [n for n in a.names.split(',') if n not in names])
    spec = ','.join('pokemon:%s:%s' % (table[n][4], n) for n in names)
    if a.anims:
        return anim_sheets(a, work, table, names, [int(x) for x in a.anims.split(',')])
    dirs = {}
    for side in ('xd', 'pbr'):
        run = os.path.join(work, 'sheet_runs', '%s_%s' % (a.tag, side))
        icons = os.path.join(run, 'icons')
        dirs[side] = icons
        if a.only and a.only != side:
            continue
        os.makedirs(icons, exist_ok=True)

        code = 0
        for k in range(0, len(names), 6):
            part = names[k:k + 6]
            make_mod(os.path.join(run, 'settings'), work, part if side == 'pbr' else [], table)
            spec = ','.join('pokemon:%s:%s' % (table[n][4], n) for n in part)
            code |= run_game(a.exe, run, ['XD_RENDER_ICON=' + spec, 'XD_RENDER_ICON_DIR=' + icons.replace(os.sep, '/'),
                                          'XD_RENDER_ICON_SIZE=%d' % a.size], 60 + 25 * len(part), os.path.join(work, 'cache'))
            shutil.copyfile(os.path.join(icons, 'render_icon.txt'), os.path.join(icons, 'render_icon_%d.txt' % (k // 6)))
        print(side, 'exit', code)
    from PIL import Image, ImageDraw
    os.makedirs(os.path.join(work, 'sheets'), exist_ok=True)
    s = a.size
    for k in range(0, len(names), a.per_sheet):
        part = names[k:k + a.per_sheet]
        cols = 2
        rows = (len(part) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * 2 * s, rows * (s + 18)), (40, 40, 48))
        dr = ImageDraw.Draw(sheet)
        for i, n in enumerate(part):
            x, y = (i % cols) * 2 * s, (i // cols) * (s + 18)
            for q, side in enumerate(('xd', 'pbr')):
                p = os.path.join(dirs[side], n + '.png')
                if os.path.exists(p):
                    im = Image.open(p).convert('RGBA').resize((s, s))
                    bg = Image.new('RGBA', (s, s), (70, 70, 80, 255) if q == 0 else (60, 80, 70, 255))
                    bg.alpha_composite(im)
                    sheet.paste(bg.convert('RGB'), (x + q * s, y + 18))
                else:
                    dr.text((x + q * s + 8, y + 40), 'NO PICTURE', fill=(255, 80, 80))
            dr.text((x + 4, y + 3), '%s (species %s): XD left, converted right' % (n, table[n][4]), fill=(255, 255, 255))
        out = os.path.join(work, 'sheets', '%s_%d.png' % (a.tag, k // a.per_sheet + 1))
        sheet.save(out)
        print(out)


if __name__ == '__main__':
    sys.exit(main())
