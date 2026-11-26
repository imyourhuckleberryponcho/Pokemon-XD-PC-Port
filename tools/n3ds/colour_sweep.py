r"""colour_sweep: finds Ultra Sun / Ultra Moon models whose colours the converter changes too much (a sweep, docs/GEN57.md "Colours").

  python colour_sweep.py dump --rom <usum.cci> [--out report.json] [--limit 25]
      Every model group of the player's dump: for every material that has a glow ("Inc") picture, the average colour of the dump's own colour
      picture against the average colour the converter bakes (glow added the old way, without the mask, and the new way, with the mask of the
      normal map's alpha). Outliers = the colour moves by more than --limit (CIE76 dE of the two averages). Nothing of the game is kept.
  python colour_sweep.py pkx --rom <usum.cci> <folder with converted pkx / pack folders> [--out report.json] [--limit 25]
      The same check against the FINISHED model files: for every material the average colour of the picture in the .pkx (opaque pixels, as
      the game draws them) against the dump's own colour picture (the average of the colour picture of the same material). Outliers = dE over
      --limit. (XD's own colours are matched in on purpose for the species XD has, so a moderate dE is normal: look at the pictures.)
Exit 0. The list is for people to look at: contact sheets of every outlier (build\...).
"""
import argparse
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'tools', 'pbr_convert'))

import ncch, garc, gfpack, gf_read, gamedetect, usum_fx
import make_proto as M


def lab(rgb):
    c = np.asarray(rgb, float) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    x, y, z = (m @ c) / np.array([0.95047, 1.0, 1.08883])
    f = lambda t: np.cbrt(t) if t > 0.008856 else 7.787 * t + 16 / 116
    fx, fy, fz = f(x), f(y), f(z)
    return np.array([116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)])


def mean_rgb(a):
    """average colour of the pixels that are drawn (alpha over half)"""
    a = np.asarray(a).reshape(-1, 4)
    k = a[:, 3] > 127
    return a[k, :3].astype(float).mean(0) if k.any() else None


def de(a, b):
    return float(np.sqrt(((lab(a) - lab(b)) ** 2).sum()))


def species_groups(r):
    g = gamedetect.models_garc(r)
    n = (len(g.entries) - 1) // 9
    for k in range(n):
        try:
            tn = M.model_texture_name(g, k)
        except Exception:
            continue
        if tn and tn.startswith('pm') and tn[2:6].isdigit():
            yield g, k, tn[:9]


def load_group(g, k):
    base = 1 + 9 * k
    gm = gf_read.parse_model(gfpack.pc_split(garc.maybe(g.read(base)))[0])
    texs = {}
    for t in gfpack.pc_split(garc.maybe(g.read(base + 1))):
        T = gf_read.parse_texture(t)
        texs[T['name']] = gf_read.decode_texture(T)
    return gm, texs


def colour_name(mt):
    for c in mt['coords']:
        if c['unit'] == 0:
            return c['name']
    return None


def run_dump(rom, limit):
    r, _ = ncch.open_rom(rom)
    rows = []
    for g, k, name in species_groups(r):
        try:
            gm, texs = load_group(g, k)
        except Exception as ex:
            rows.append(dict(group=k, model=name, error=str(ex)))
            continue
        for mt in gm['materials']:
            inc = usum_fx.emission_map(mt, texs)
            nm = colour_name(mt)
            if not inc or nm not in texs:
                continue
            col = texs[nm][2]
            raw = mean_rgb(col)
            if raw is None:
                continue
            old = mean_rgb(usum_fx.bake_emission(col, texs[inc], raw=col))
            mk = usum_fx.emission_mask(mt, texs)
            new = mean_rgb(usum_fx.bake_emission(col, texs[inc], raw=col, mask=texs[mk] if mk else None))
            rows.append(dict(group=k, model=name, material=mt['name'], inc=inc, mask=mk, de_old=round(de(raw, old), 1), de_new=round(de(raw, new), 1)))
    return rows


def unfsys(b):
    h = struct.unpack_from('>I', b, 0x60)[0]
    off, size = struct.unpack_from('>II', b, h + 4)
    return b[off:off + size]


def pkx_files(folder):
    for dp, _, fs in os.walk(folder):
        for f in fs:
            p = os.path.join(dp, f)
            if f.endswith('.pkx') or (f.startswith('pkx_') and f.endswith('.fsys') and os.path.isfile(p)) or f == '#0' and 'pkx' in dp:
                yield p


def picture_means(b):
    import hsd_read, gx_tex
    pk, m, _w = hsd_read.read_pkx(bytearray(b), meshes=False)
    out = {}
    for mi, ma in enumerate(m.materials):
        if not ma.textures:
            continue
        im = m.images[ma.textures[0].image]
        try:
            px = gx_tex.decode(im.format, im.width, im.height, im.data)
        except Exception:
            continue
        mean = mean_rgb(px)
        if mean is not None:
            out[mi] = mean
    return out


def run_pkx(rom, folder, limit):
    """Gen 4 pack models (pkx_g4_<model>.fsys/#0 or <model>.pkx under `folder`): picture average against the dump's colour picture, per material."""
    r, _ = ncch.open_rom(rom)
    plan = json.load(open(os.path.join(HERE, 'usum_gen4_plan.json')))['plan']
    nat_of = {v[0]: int(k) for k, v in plan.items()}
    groups = {}
    for g, k, name in species_groups(r):
        pm = int(name[2:6])
        if pm <= 649 and pm not in groups:
            groups[pm] = (g, k)
    rows = []
    for p in sorted(pkx_files(folder)):
        base = os.path.basename(os.path.dirname(p)) if os.path.basename(p) == '#0' else os.path.basename(p)
        model = base.replace('pkx_g4_', '').replace('.fsys', '').replace('.pkx', '')
        nat = nat_of.get(model)
        if nat is None or nat not in groups:
            continue
        b = open(p, 'rb').read()
        if b[:4] == b'FSYS':
            b = unfsys(b)
        try:
            means = picture_means(b)
            gm, texs = load_group(*groups[nat])
        except Exception as ex:
            rows.append(dict(model=model, nat=nat, error=str(ex)))
            continue
        for mi, mt in enumerate(gm['materials']):
            nm = colour_name(mt)
            if mi in means and nm in texs:
                d = mean_rgb(texs[nm][2])
                if d is not None:
                    rows.append(dict(model=model, nat=nat, material=mt['name'], de=round(de(d, means[mi]), 1)))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['dump', 'pkx'])
    ap.add_argument('--rom', required=True)
    ap.add_argument('folder', nargs='?')
    ap.add_argument('--out')
    ap.add_argument('--limit', type=float, default=25.0)
    a = ap.parse_args()
    if a.mode == 'dump':
        rows = run_dump(a.rom, a.limit)
        bad = [x for x in rows if x.get('de_old', 0) > a.limit]
        now = [x for x in rows if x.get('de_new', 0) > a.limit]
        print('materials with a glow picture: %d; outliers (dE > %g): old %d, new %d' % (len([x for x in rows if 'de_old' in x]), a.limit, len(bad), len(now)))
        for x in sorted(bad, key=lambda x: -x['de_old']):
            print('  %s %-28s old %5.1f  new %5.1f  mask %s' % (x['model'], x['material'], x['de_old'], x['de_new'], x['mask']))
        if a.out:
            json.dump(rows, open(a.out, 'w'), indent=1)
    else:
        rows = run_pkx(a.rom, a.folder, a.limit)
        if a.out:
            json.dump(rows, open(a.out, 'w'), indent=1, default=float)
        bad = [x for x in rows if x.get('de', 0) > a.limit]
        print('materials compared: %d; outliers (dE > %g): %d' % (len([x for x in rows if 'de' in x]), a.limit, len(bad)))
        worst = {}
        for x in bad:
            worst[x['model']] = max(worst.get(x['model'], 0), x['de'])
        for m, d in sorted(worst.items(), key=lambda t: -t[1]):
            print('  %-14s worst material dE %5.1f' % (m, d))


if __name__ == '__main__':
    main()
