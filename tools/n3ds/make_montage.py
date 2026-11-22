"""Side-by-side montage of the prototype clips (made by tools/showcase.py from make_plan.py's plan).

    python -I make_montage.py <config.json> <out.mp4>

config: {"clips_dir": "...", "labels": {"xd": "XD (native)", ...}, "grid": [["xd","usum_native","x_native"], ["br","usum_xd","x_xd"]],
         "species": {"machamp": {"clip_prefix": "3ds_machamp_", "moments": [{"t": 12.0, "dur": 6.0, "crop": [cx, cy], "title": "idle"}]}},
         "align": true}
Every panel is a 640x540 piece of the 1920x1080 picture (pixels 1:1), the same piece in every clip, so the Pokemon are
compared at the size they have in the game. Clips are aligned to the XD clip by the game's own sound (cross correlation of
the loudness envelope) because the 3DS motions' lengths move later events.
Game footage: the output stays under build\\ (never in the repository)."""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

PW, PH = 640, 540


def ff(args):
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y'] + args, check=True)


def envelope(path, rate=200):
    """Loudness envelope (rate values a second) of a clip's sound."""
    tmp = tempfile.mktemp(suffix='.raw')
    ff(['-i', path, '-vn', '-ac', '1', '-ar', '8000', '-f', 'f32le', tmp])
    a = np.fromfile(tmp, dtype='<f4')
    os.remove(tmp)
    n = 8000 // rate
    a = np.abs(a[:len(a) // n * n]).reshape(-1, n).mean(axis=1)
    return a


def lag_of(ref, other, t, dur, rate=200, span=8.0):
    """Seconds to add to t in `other` so that its sound lines up with `ref` at t (+/- span)."""
    i0, i1 = int(max(t - 1.0, 0) * rate), int((t + dur + 1.0) * rate)
    w = ref[i0:i1]
    w = w - w.mean()
    best, bl = -1e18, 0
    for s in range(-int(span * rate), int(span * rate) + 1, 2):
        j0 = i0 + s
        if j0 < 0 or j0 + len(w) > len(other):
            continue
        x = other[j0:j0 + len(w)]
        x = x - x.mean()
        c = float((w * x).sum() / (np.linalg.norm(w) * np.linalg.norm(x) + 1e-9))
        if c > best:
            best, bl = c, s
    return bl / rate, best



def region_masks(path, region, fps=10, tmax=60):
    """Bright-text masks (1 bit, 155x68) of a screen region of a clip, one per 1/fps second."""
    x, y, w, h = region
    cmd = ['ffmpeg', '-loglevel', 'error', '-t', str(tmax), '-i', path, '-vf', 'fps=%d,crop=%d:%d:%d:%d,scale=155:68,format=gray' % (fps, w, h, x, y),
           '-f', 'rawvideo', '-pix_fmt', 'gray', '-']
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    a = np.frombuffer(raw, np.uint8)
    n = len(a) // (155 * 68)
    return (a[:n * 155 * 68].reshape(n, 68, 155) > 215)


def find_anchor(path, tpl_path, tpl_time, region, fps=10, thr=0.5, after=0.0):
    """Time in `path` at which the same message box text first shows (>= `after`): by the overlap of the bright pixels."""
    tpl = region_masks(tpl_path, region, fps, tpl_time + 0.3)[int(round(tpl_time * fps))]
    ms = region_masks(path, region, fps)
    best, bt = 0.0, None
    for i, m in enumerate(ms):
        if i / fps < after:
            continue
        inter = float((m & tpl).sum())
        uni = float((m | tpl).sum()) or 1.0
        iou = inter / uni
        if iou > thr:
            return i / fps, iou
        if iou > best:
            best, bt = iou, i / fps
    return bt, best


def label_png(text, path, sub='', pw=PW, ph=PH):
    im = Image.new('RGBA', (pw, ph), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    try:
        f = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 26)
        f2 = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 18)
    except Exception:
        f = f2 = ImageFont.load_default()
    d.rectangle([0, 0, pw, 40], fill=(0, 0, 0, 170))
    d.text((10, 6), text, font=f, fill=(255, 255, 255, 255))
    if sub:
        d.text((pw - 10 - d.textlength(sub, font=f2), 12), sub, font=f2, fill=(220, 220, 220, 255))
    im.save(path)


def card_png(lines, path, size=(1920, 1080)):
    im = Image.new('RGB', size, (14, 14, 18))
    d = ImageDraw.Draw(im)
    y = size[1] // 2 - 40 * len(lines) // 2 - 40
    for k, ln in enumerate(lines):
        try:
            f = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf' if k == 0 else 'C:/Windows/Fonts/arial.ttf', 64 if k == 0 else 34)
        except Exception:
            f = ImageFont.load_default()
        w = d.textlength(ln, font=f)
        d.text(((size[0] - w) / 2, y), ln, font=f, fill=(245, 245, 245))
        y += 90 if k == 0 else 52
    im.save(path)


def main():
    cfg = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    work = os.path.join(os.path.dirname(os.path.abspath(out)), 'montage_work')
    os.makedirs(work, exist_ok=True)
    cdir = cfg['clips_dir']
    grid = cfg['grid']
    labels = cfg['labels']
    parts = []
    env_cache = {}
    n = 0
    for card in cfg.get('cards_first', []):
        p = os.path.join(work, 'card_%d.png' % n)
        card_png(card['lines'], p)
        seg = os.path.join(work, 'seg_%03d.mkv' % n)
        ff(['-loop', '1', '-t', str(card.get('dur', 4)), '-i', p, '-vf', 'fps=59.94,format=yuv420p', '-c:v', 'libx264', '-crf', '14', seg])
        parts.append(seg)
        n += 1
    for sp, spec in cfg['species'].items():
        pre = spec['clip_prefix']
        sgrid = spec.get('grid', grid)
        names = [v for row in sgrid for v in row]
        single = len(names) == 1
        pw, ph = (960, 540) if single else (PW, PH)
        for m in spec['moments']:
            inputs, filt = [], []
            labs = []
            ref_path = os.path.join(cdir, pre + 'xd' + m.get('suffix', spec.get('clip_suffix', '')) + '.mkv')
            if cfg.get('align', True) and ref_path not in env_cache and os.path.exists(ref_path):
                env_cache[ref_path] = envelope(ref_path)
            lags = {}
            anc = {}
            if 'anchor' in m:
                a = m['anchor']
                region = a.get('region', [250, 790, 620, 270])
                for v in set(names):
                    pth = os.path.join(cdir, pre + v + m.get('suffix', spec.get('clip_suffix', '')) + '.mkv')
                    anc[v] = find_anchor(pth, ref_path, a['time'], region, after=a.get('after', 0.0))
                print(sp, m.get('title'), 'anchors', {k: (round(t, 1) if t is not None else None, round(sc, 2)) for k, (t, sc) in anc.items()})
            for k, v in enumerate(names):
                path = os.path.join(cdir, pre + v + m.get('suffix', spec.get('clip_suffix', '')) + '.mkv')
                if cfg.get('align', True) and v != 'xd' and 'anchor' not in m:
                    if path not in env_cache:
                        env_cache[path] = envelope(path)
                    lag, score = lag_of(env_cache[ref_path], env_cache[path], m['t'], m['dur'])
                else:
                    lag, score = 0.0, 1.0
                if v in anc and anc[v][0] is not None:
                    t0 = max(anc[v][0] - m.get('before', 1.0), 0)
                    lag, score = t0 - m.get('t', t0), anc[v][1]
                else:
                    t0 = max(m['t'] + lag, 0)
                lags[v] = (lag, score)
                if v in anc and (anc[v][0] is None or anc[v][1] < 0.5):
                    inputs += ['-f', 'lavfi', '-t', '%.3f' % m['dur'], '-i', 'color=c=0x202020:s=1920x1080:r=59.94']
                    labels_override = spec.get('labels', labels)[v] + ' (not in this run)'
                else:
                    inputs += ['-ss', '%.3f' % t0, '-t', '%.3f' % m['dur'], '-i', path]
                    labels_override = spec.get('labels', labels)[v]
                lp = os.path.join(work, 'lab_%s_%d_%s.png' % (sp, n, v))
                label_png(labels_override, lp, '%s' % (sp if v == names[0] else ''), pw, ph)
                labs.append(lp)
            for lp in labs:
                inputs += ['-loop', '1', '-t', str(m['dur']), '-i', lp]
            cx, cy = m['crop']
            x0 = int(min(max(cx - pw // 2, 0), 1920 - pw))
            y0 = int(min(max(cy - ph // 2, 0), 1080 - ph))
            for k in range(len(names)):
                filt.append('[%d:v]crop=%d:%d:%d:%d,setpts=PTS-STARTPTS,fps=59.94,format=yuv420p[c%d]' % (k, pw, ph, x0, y0, k))
                filt.append('[c%d][%d:v]overlay=0:0:shortest=1[p%d]' % (k, len(names) + k, k))
            cols = len(sgrid[0])
            layout = '|'.join('%d_%d' % ((k % cols) * pw, (k // cols) * ph) for k in range(len(names)))
            if single:
                filt.append('[p0]scale=1920:1080:flags=lanczos,format=yuv420p[v]')
            else:
                filt.append(''.join('[p%d]' % k for k in range(len(names))) + 'xstack=inputs=%d:layout=%s:shortest=1,pad=1920:1080:0:(oh-ih)/2:black,format=yuv420p[v]' % (len(names), layout))
            seg = os.path.join(work, 'seg_%03d.mkv' % n)
            ff(inputs + ['-filter_complex', ';'.join(filt), '-map', '[v]', '-t', str(m['dur']), '-c:v', 'libx264', '-crf', '14', '-preset', 'fast', seg])
            print(sp, m.get('title'), {k: (round(a, 2), round(b, 2)) for k, (a, b) in lags.items()})
            parts.append(seg)
            n += 1

    lst = os.path.join(work, 'list.txt')
    with open(lst, 'w') as f:
        for p in parts:
            f.write("file '%s'\n" % p.replace('\\', '/'))
    ff(['-f', 'concat', '-safe', '0', '-i', lst, '-c:v', 'libx264', '-crf', '18', '-preset', 'slow', '-pix_fmt', 'yuv420p', '-r', '59.94', out])
    print('wrote', out)


if __name__ == '__main__':
    main()
