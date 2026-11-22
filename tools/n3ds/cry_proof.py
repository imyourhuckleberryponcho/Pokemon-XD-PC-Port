"""Did the game play the Gen 5-7 pack's cry?  python -I cry_proof.py <dump.wav> <usum cry wav folder> <species wave json> [species ...]

The game's sound dump (XD_AUDIO_DUMP) is cut into bursts of sound; each burst is compared (long-term average spectrum, correlation) with every
cry wave of the dump folder (cry/usum/NNN.wav made by n3ds_cries.py; build folder only, never the repository). Prints the best waves per burst and
whether the wave the table names for each given species is among the top 3. Uses numpy and scipy."""
import json
import sys
import wave

import numpy as np
import scipy.signal as sg


def load(p):
    w = wave.open(p)
    n, ch, r = w.getnframes(), w.getnchannels(), w.getframerate()
    a = np.frombuffer(w.readframes(n), np.int16).astype(float)
    w.close()
    if ch > 1:
        a = a.reshape(-1, ch).mean(1)
    return a, r


def ltas(a, r):
    if r != 16000:
        a = sg.resample_poly(a, 16000, r)
    a = a - a.mean()
    e = np.abs(a)
    idx = np.where(e > 0.05 * e.max())[0]
    if len(idx) < 400:
        return None
    a = a[idx[0]:idx[-1] + 1]
    _f, P = sg.welch(a, 16000, nperseg=512)
    P = np.log(P[2:128] + 1e-3)
    P = sg.savgol_filter(P, 5, 2)
    P = P - P.mean()
    return P / np.linalg.norm(P)


def bursts(a, r, gap=0.25, thr=0.08):
    env = np.abs(a)
    win = int(r * 0.02)
    env = np.convolve(env, np.ones(win) / win, 'same')
    on = env > thr * env.max()
    out, start, last = [], None, None
    for i in np.where(on)[0]:
        if start is None:
            start = last = i
        elif i - last > gap * r:
            out.append((start, last))
            start = last = i
        else:
            last = i
    if start is not None:
        out.append((start, last))
    return [(s, e) for s, e in out if e - s > 0.15 * r]


def main(argv):
    dump, folder, table = argv[1], argv[2], argv[3]
    tab = json.load(open(table))
    refs = {}
    for i in range(965):
        a, r = load('%s/%03d.wav' % (folder, i))
        f = ltas(a, r)
        if f is not None:
            refs[i] = f
    ks = sorted(refs)
    M = np.array([refs[k] for k in ks])
    a, r = load(dump)
    bs = bursts(a, r)
    print('%d bursts in %.1f s' % (len(bs), len(a) / r))
    want = {s: tab.get(s) for s in argv[4:]}
    seen = set()
    for s, e in bs:
        f = ltas(a[s:e], r)
        if f is None:
            continue
        sc = M @ f
        o = np.argsort(-sc)[:3]
        top = [(ks[j], round(float(sc[j]), 2)) for j in o]
        print('  %.2f-%.2f s: best waves %s' % (s / r, e / r, top))
        for sp, w in want.items():
            if w is not None and w in [t[0] for t in top]:
                seen.add(sp)
    for sp, w in want.items():
        print('species %s: wave %s %s' % (sp, w, 'HEARD' if sp in seen else 'not among the top 3 of any burst'))
    return 0 if all(sp in seen for sp in want) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
