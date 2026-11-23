"""usum_models: Ultra Sun / Ultra Moon Pokemon models for Pokemon XD, made from the player's own 3DS dump and own XD disc.

  python -I usum_models.py --rom <decrypted Ultra Sun / Ultra Moon .cci or .cia> --xd-disc <XD image> --out <folder>
                           [--jobs N] [--only 6,25,...] [--decimate 2600]

The models-only pack: every Gen 1-3 Pokemon XD has a model for gets the Ultra Sun / Ultra Moon model of the same species, in
the layout of the extra-assets layer <folder> (the launcher puts it at <data folder>/extra_assets/usum): mod.json,
fsys/<archive>/#<entry> (the file XD loads in place of its own), conversion_log.txt, usum_report.json (counts, failures,
triangles, time, and the species -> model lookup this run built). Same contract as pbr_models: stdout lines
    progress <done> <total> <name>      one per model
    done <converted> <kept_xd> <bytes>  at the end
    error <one line>                    exit 1
Everything is read from the player's own files at run time: the species -> XD model archive pairing (XD's species table and
model table, as the Battle Revolution converter reads them), the species -> 3DS model group lookup (the model archive's own
texture names, for the species whose models carry their national number; see lookup_table). Nothing from the games is in the
repository; the output is game data, keep it under build\\.
"""
import argparse
import json
import multiprocessing
import os
import shutil
import struct
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
for sub in ('pbr_survey', 'pbr_convert'):
    p = os.path.join(REPO, 'tools', sub)
    if os.path.isdir(p) and p not in sys.path:
        sys.path.insert(0, p)

import ncch, garc, gfpack, gf_read, gf_to_xd, gamedetect
import make_proto as M

CONVERTER_VERSION = '0.2.0'
MOD_ID = 'ultra-sun-moon-models'
XD_ID = 'GXXE01'
REC = 292
PKX_TABLE = 0x8040D0A8
PKX_ROWS = 419
GEN3_LAST = 386
BONSLY = 438
GEN4_FIRST, GEN4_LAST = 387, 493
CONVERTER_VERSION_GEN4 = 2


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def dol_read(dol, addr, n):
    """bytes at a memory address of a GameCube main.dol (7 text + 11 data sections)."""
    for i in range(18):
        off, a, sz = be32(dol, i * 4), be32(dol, 0x48 + i * 4), be32(dol, 0x90 + i * 4)
        if sz and a <= addr and addr + n <= a + sz:
            return dol[off + addr - a: off + addr - a + n]
    raise KeyError(hex(addr))


def xd_species_models(disc):
    """{national number: (archive file name, entry index, entry name)} for every species with a model in XD."""
    import noddisc
    d = disc.file('common.fsys')
    ent = [e for e in noddisc.fsys_entries(d) if e.name == 'common_rel'][0]
    rel = noddisc.extract(d, ent)
    sig = struct.pack('>6H', 35, 55, 30, 50, 40, 90)
    i = rel.find(sig)
    assert i > 0, 'species table not found in common_rel'
    base = i - 0x8E - 25 * REC
    assert struct.unpack_from('>6H', rel, base + REC + 0x8E) == (45, 49, 49, 65, 65, 45)
    table = dol_read(disc.dol(), PKX_TABLE, PKX_ROWS * 8)
    by_group = {}
    for p in sorted(disc.files):
        if p.lower().startswith('pkx_') and p.lower().endswith('.fsys'):
            data = disc.file(p)
            if data[:4] != b'FSYS':
                continue
            ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
            if ents:
                by_group[be32(data, 8)] = (p, ents[0].index, ents[0].name)
    out = {}
    for s in range(1, 415):
        r = rel[base + s * REC: base + (s + 1) * REC]
        nat = be16(r, 0x12)
        row = be32(r, 0x2C)
        if not (1 <= nat <= GEN3_LAST or nat == BONSLY) or row >= PKX_ROWS:
            continue
        grp = be32(table, row * 8)
        if grp in by_group and nat not in out:
            out[nat] = by_group[grp]
    return out


def scan_groups(g):
    """{group: (pm number, form)} from the first texture name of every model group (read from the player's archive)."""
    n = (len(g.entries) - 1) // 9
    out = {}
    for k in range(n):
        try:
            tex = M.model_texture_name(g, k)
            if tex is not None and tex.startswith('pm') and tex[2:6].isdigit():
                out[k] = (int(tex[2:6]), tex[7:9] if len(tex) >= 9 and tex[6] == '_' else '00')
        except Exception:
            pass
    return out


def dex_map():
    """pm number -> national number for the Kalos / Alola models (usum_dex_map.json: plain numbers, no game data)."""
    return {int(k): v for k, v in json.load(open(os.path.join(HERE, 'usum_dex_map.json')))['kalos_alola'].items()}


def lookup_table(groups, extra=None):
    """national number -> group (the first form 00 when it exists). Gen 1-5 models carry the national number in their texture
    name (pm0001-pm0649); the Kalos / Alola ones carry the game's own numbers (STUDY.md section 8), translated by `extra`
    (pm number -> national number, dex_map())."""
    extra = extra or {}
    t = {}
    for k in sorted(groups):
        pm, form = groups[k]
        nat = pm if 1 <= pm <= 649 else extra.get(pm)
        if nat and (nat not in t or (form == '00' and groups[t[nat]][1] != '00')):
            t[nat] = k
    return t


_state = {}


def _init(rom):
    _state['r'], _ = ncch.open_rom(rom)


def _fx_opts():
    import json as _j
    return _j.loads(os.environ.get('N3DS_FX_OPTS') or '{}')


def convert_one(job):
    nat, name, arch, idx, xd_bytes, group, decimate, out = job
    t0 = time.time()
    lines = []
    res = dict(nat=nat, name=name, state='converted', group=group)
    try:
        gm, texs, mots, _g = M.load_species(_state['r'], nat, group)
        pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lines.append,
                                     opts=dict(dict(ground=True, decimate=decimate, fire=True, light=1.0, fx=True, bake_scale=True, eyes=True, size_rule=True, eye_blend=True, ghost_alpha=0.6, steel=1.2, lure=True, cheeks=1.0, match=True), **_fx_opts()))
        dst = os.path.join(out, 'fsys', arch)
        os.makedirs(dst, exist_ok=True)
        open(os.path.join(dst, '#%d' % idx), 'wb').write(pkx)
        res.update(tris=info['tris'], size=len(pkx), xd_size=len(xd_bytes), scale=info['scale'],
                   roles={k: v['frames'] for k, v in mots.items()})
    except BaseException as ex:
        res['state'] = 'kept_xd'
        lines.append('REFUSED: %s: %s' % (type(ex).__name__, ex))
        lines.append(traceback.format_exc())
        res['error'] = '%s: %s' % (type(ex).__name__, ex)
    res['secs'] = round(time.time() - t0, 1)
    return res, lines


def convert_gen4(job):
    """One Gen 4 species: the dump's model in the container of a stand-in XD species (the way the Battle Revolution models without an XD
    twin are made); written into the usum layer as the archive the Gen 4 pack uses (pkx_g4_<name>.fsys, member 0) and into the source."""
    dex, model, standin, xd_bytes, group, decimate, height_cm, out, src = job
    t0 = time.time()
    lines = []
    res = dict(nat=dex, name=model, state='converted', group=group)
    try:
        gm, texs, mots, _g = M.load_species(_state['r'], dex, group)
        pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes, mode='role', log=lines.append,
                                     opts=dict(dict(ground=True, decimate=decimate, fire=True, light=1.0, fx=True, bake_scale=True, eyes=True,
                                                    size_rule='mainline', height_cm=height_cm, hover_rule='model', eye_blend=True,
                                                    ghost_alpha=0.6, steel=1.2, lure=True, cheeks=1.0), **_fx_opts()))
        dst = os.path.join(out, 'fsys', 'pkx_g4_%s.fsys' % model)
        os.makedirs(dst, exist_ok=True)
        open(os.path.join(dst, '#0'), 'wb').write(pkx)
        os.makedirs(os.path.join(src, 'models'), exist_ok=True)
        open(os.path.join(src, 'models', model + '.pkx'), 'wb').write(pkx)
        res.update(tris=info['tris'], size=len(pkx), xd_size=len(xd_bytes), scale=info['scale'], standin=standin)
    except BaseException as ex:
        res['state'] = 'kept_xd'
        lines.append('REFUSED: %s: %s' % (type(ex).__name__, ex))
        lines.append(traceback.format_exc())
        res['error'] = '%s: %s' % (type(ex).__name__, ex)
    res['secs'] = round(time.time() - t0, 1)
    return res, lines


def dispatch(job):
    return convert_gen4(job[1]) if job and job[0] == 'g4' else convert_one(job)


def say(*a):
    print(*a, flush=True)


def base_is_made(a):
    """Models and Gen 4 pack of this converter version are already there (and for this dump): a Make that only wants the Gen 5-7 pack
    does not convert them again. Packs made before the dump was written into mod.json count when the version is the same."""
    try:
        out = os.path.abspath(a.out)
        m = json.load(open(os.path.join(out, 'mod.json')))
        if m.get('version') != CONVERTER_VERSION or m.get('count', 0) < 1:
            return False
        if m.get('dump') and m['dump'] != dump_fingerprint(a.rom):
            return False
        if a.gen4_pack:
            g = json.load(open(os.path.join(os.path.abspath(a.gen4_pack), 'gen4.json')))
            if g.get('version') != CONVERTER_VERSION_GEN4:
                return False
        return True
    except Exception:
        return False


def dump_fingerprint(path):
    import usum_gen57_make as G57
    return G57.fingerprint(path)


def run(a):
    if a.gen57_pack and base_is_made(a):
        say('stage base 0 8')
        say('progress 1 1 models and Gen 4 pack are already made')
        return
    say('stage base 0 8')
    import pbr_models as PM
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, XD_ID, 'XD')
    r, prod = ncch.open_rom(a.rom)
    g = gamedetect.models_garc(r)
    t0 = time.time()
    xdm = xd_species_models(d)
    groups = scan_groups(g)
    table = lookup_table(groups, dex_map())
    say('lookup: %d XD species with a model, %d model groups scanned, %d species with a direct model' % (len(xdm), len(groups), len(table)))
    import noddisc
    only = set(int(x) for x in a.only.split(',')) if a.only else None
    jobs_l, missing = [], []
    for nat in sorted(xdm):
        if only and nat not in only:
            continue
        arch, idx, name = xdm[nat]
        if nat not in table:
            missing.append(nat)
            continue
        data = d.file(arch)
        ent = [e for e in noddisc.fsys_entries(data) if e.index == idx][0]
        jobs_l.append((nat, name, arch, idx, noddisc.extract(data, ent), table[nat], a.decimate, None))
    out = os.path.abspath(a.out)
    tmp = out + '.tmp'
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'fsys'))
    jobs_l = [j[:-1] + (tmp,) for j in jobs_l]
    src = None
    if a.gen4_pack:
        import usum_gen4
        us = usum_gen4.Usum(r, prod)
        src = out + '.gen4src'
        if os.path.isdir(src):
            shutil.rmtree(src)
        os.makedirs(src)
        plan = json.load(open(os.path.join(HERE, 'usum_gen4_plan.json')))['plan']
        for dex_s, (model, standin) in sorted(plan.items(), key=lambda kv: int(kv[0])):
            dex = int(dex_s)
            if (only and dex not in only) or dex not in table or standin not in xdm:
                continue
            arch, idx, _n = xdm[standin]
            data = d.file(arch)
            ent = [e for e in noddisc.fsys_entries(data) if e.index == idx][0]
            height_cm = u16(us.personal(dex), 0x24)
            jobs_l.append(('g4', (dex, model, standin, noddisc.extract(data, ent), table[dex], a.decimate, height_cm, tmp, src)))
    del d
    import gen57_memory
    nj = a.jobs or gen57_memory.workers_for(max(1, min(6, gen57_memory.cpu_count() - 2)))
    results, logs = {}, {}
    total = len(jobs_l)

    def fin(x):
        res, lines = x
        results[res['nat']] = res
        logs[res['nat']] = lines
        say('progress %d %d %s' % (len(results), total, res['name']))
    if nj <= 1:
        _init(a.rom)
        for j in jobs_l:
            fin(dispatch(j))
    else:
        with multiprocessing.Pool(nj, initializer=_init, initargs=(a.rom,)) as pool:
            for x in pool.imap_unordered(dispatch, jobs_l):
                fin(x)
    conv = [x for x in results.values() if x['state'] == 'converted']
    nbytes = sum(x['size'] for x in conv)
    secs = time.time() - t0
    json.dump({'id': MOD_ID, 'name': 'Ultra Sun / Ultra Moon models', 'version': CONVERTER_VERSION,
               'author': 'made on your PC from your own 3DS dump',
               'description': "Pokemon Ultra Sun / Ultra Moon's Pokemon models in Pokemon XD, converted from your own dump.",
               'dump': dump_fingerprint(a.rom), 'game': XD_ID, 'generated_from': prod if isinstance(prod, str) else 'CTR', 'applies': 'restart', 'format': 1,
               'count': len(conv), 'unit': 'model', 'units': 'models'},
              open(os.path.join(tmp, 'mod.json'), 'w'), indent=2)
    tris = [x['tris'] for x in conv]
    report = dict(version=CONVERTER_VERSION, seconds=round(secs, 1), jobs=nj, species_in_xd=len(xdm), converted=len(conv),
                  failed=[dict(nat=x['nat'], name=x['name'], error=x.get('error')) for x in results.values() if x['state'] != 'converted'],
                  no_model_in_dump=missing, bytes=nbytes, tris_min=min(tris) if tris else 0, tris_max=max(tris) if tris else 0,
                  tris_mean=round(sum(tris) / len(tris)) if tris else 0, species={str(k): v for k, v in sorted(results.items())},
                  lookup={str(k): v for k, v in sorted(table.items())})
    json.dump(report, open(os.path.join(tmp, 'usum_report.json'), 'w'), indent=1)
    with open(os.path.join(tmp, 'conversion_log.txt'), 'w') as f:
        f.write('usum_models %s: %d models, %d converted, %d kept from XD, %d bytes, %.0f s, %d jobs\n' % (
            CONVERTER_VERSION, total, len(conv), total - len(conv), nbytes, secs, nj))
        for k in sorted(results):
            x = results[k]
            f.write('\n== %s (#%d): %s, %s bytes, %.1f s\n' % (x['name'], k, x['state'], x.get('size', 0), x['secs']))
            for l in logs[k]:
                f.write(l + '\n')
    old = out + '.old'
    if os.path.isdir(old):
        shutil.rmtree(old)
    if os.path.isdir(out):
        os.rename(out, old)
    os.rename(tmp, out)
    if os.path.isdir(old):
        shutil.rmtree(old, ignore_errors=True)
    if a.gen4_pack:
        make_gen4_pack(a, us, src, results)
    if not a.gen57_pack:
        say('done %d %d %d' % (len(conv), total - len(conv), nbytes))


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def make_gen4_pack(a, us, src, results):
    """The source folder (Battle Revolution's table layouts from the dump + the converted models), then the pack by `pbr_models`."""
    import subprocess
    import usum_gen4
    import usum_gen4_values
    usum_gen4.write_source(us, src, usum_gen4_values.Values(a.gen4_values != 'off'), log=lambda s: None)
    plan = json.load(open(os.path.join(HERE, 'usum_gen4_plan.json')))['plan']
    with open(os.path.join(src, 'plan.tsv'), 'w') as f:
        for dex_s, (model, standin) in sorted(plan.items(), key=lambda kv: int(kv[0])):
            if int(dex_s) in results and results[int(dex_s)]['state'] == 'converted':
                f.write('%s\t%s\t%d\n' % (dex_s, model, standin))
    usum_gen4.write_extras(us, src, log=lambda s: None)
    open(os.path.join(src, 'gen4src.txt'), 'w').write('usum_gen4 source: %s, gen4 values %s\n' % (us.det['name'], a.gen4_values))
    stem = 'pbr_models.exe' if os.name == 'nt' else 'pbr_models'
    here = program_dir()
    exe = a.pbr_models
    for c in (os.path.join(here, stem), os.path.join(os.path.dirname(here), 'pbr_models', stem), os.path.join(os.path.dirname(here), stem)):
        if not exe and os.path.isfile(c):
            exe = c
    if not exe or not os.path.isfile(exe):
        raise RuntimeError('the Gen 4 pack builder (pbr_models) was not found next to this program')
    cmd = [exe, '--gen4-source', src, '--xd-disc', a.xd_disc, '--gen4-pack', os.path.abspath(a.gen4_pack)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    for ln in p.stdout.splitlines():
        if ln.startswith('error'):
            raise RuntimeError(ln)
    if p.returncode:
        raise RuntimeError('the Gen 4 pack builder stopped (exit %d): %s' % (p.returncode, p.stderr[-300:]))
    shutil.rmtree(src, ignore_errors=True) if not os.environ.get('USUM_KEEP_SOURCE') else None


def faces_only(a):
    """usum_models --faces-only <pack>: the faces stage of usum_gen57_make on its own, for the C++ converter (usum_models_cpp), which makes
    everything else itself. Protocol lines: `progress <done> <total> <what>`; `error <words>` (exit 1)."""
    import usum_gen57_make as G57
    import gen57_all as A
    import gen57_faces_fast as FF
    try:
        G57.low_priority()
        pack = os.path.abspath(a.faces_only)
        nats = [int(x) for x in a.only.split(',')] if a.only else list(range(A.FIRST, A.LAST + 1))
        import gen57_memory
        jobs = a.jobs or gen57_memory.workers_for(max(1, min(8, gen57_memory.cpu_count() - 2)), lambda *x: None)
        work = os.path.abspath(a.faces_work or pack + '.faces')
        os.makedirs(work, exist_ok=True)
        lg = open(os.path.join(work, 'faces.log'), 'a', encoding='utf-8')
        FF.make_faces(a, A, pack, work, nats, jobs, G57.progress, lambda *x: (lg.write(' '.join(str(v) for v in x) + chr(10)), lg.flush()))
        return 0
    except Exception as ex:
        traceback.print_exc()
        say('error The faces could not be drawn (%s: %s).' % (type(ex).__name__, ' '.join(str(ex).split())[:160]))
        return 1


def program_dir():
    return os.path.dirname(os.path.abspath(sys.argv[0] if getattr(sys, 'frozen', False) else __file__))


def main():



    if sys.argv[1:] == ['--version']:
        say('version ' + CONVERTER_VERSION)
        return
    if sys.argv[1:] == ['--selfcheck']:
        import importlib
        for m in ('usum_gen57_make', 'gen57_all', 'gen57_forms', 'gen57_faces', 'gen57_faces_fast', 'gen57_facecam', 'gen57_speed', 'gen57_memory', 'gen57_data', 'gen57_pack', 'gen57_moves',
                  'gen57_move_plan', 'gen57_abilities', 'usum_dex_entries', 'check_pack_move_rows', 'check_pack_models', 'n3ds_cries', 'gen4_pack', 'gen4_cry', 'gen4_spots',
                  'make_proto', 'gf_to_xd', 'gf_pose', 'hsd_read', 'model', 'noddisc', 'numpy', 'PIL.Image'):
            importlib.import_module(m)
        import gen57_all
        gen57_all.load_tables()
        for f in ('gen57_forms.json', 'gen57_model_numbers.json', 'gen57_cry_waves.json', 'gen57_speed_numbers.json', 'usum_dex_map.json', 'usum_gen4_plan.json'):
            if not os.path.isfile(os.path.join(HERE, f)):
                raise SystemExit('selfcheck: missing ' + f)
        say('selfcheck ok')
        return
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--only', default='')
    ap.add_argument('--decimate', type=int, default=2600)
    ap.add_argument('--gen4-pack', default='', help='also make the Gen 4 pack (needs pbr_models next to this program)')
    ap.add_argument('--gen4-values', default='on', choices=['on', 'off'], help='Diamond/Pearl numbers where Gen 7 differs (docs/GEN4_USUM.md)')
    ap.add_argument('--gen57-pack', default='', help='also make the Gen 5-7 pack here (usum_gen57_make.py; needs --gen4-pack and --xd-exe)')
    ap.add_argument('--xd-exe', default='', help='the game program (draws the faces and the Pokedex pictures)')
    ap.add_argument('--data-mods-exe', default='', help="xd_data_mods (reads XD's tables; default: next to this program)")
    ap.add_argument('--pictures-out', default='', help='the folder for the Pokedex pictures (<data folder>/icons/pokedex)')
    ap.add_argument('--data-folder', default='', help='the data folder: the Pokedex pictures go to <it>/icons/pokedex (unless --pictures-out)')
    ap.add_argument('--gen57-only', default='', help='test: only these national numbers')
    ap.add_argument('--gen57-skip', default='', help='test: skip stages (forms,faces,pictures)')
    ap.add_argument('--pbr-models', default='', help='the pack builder program (default: next to this program)')
    ap.add_argument('--faces-only', default='', help='the C++ converter (usum_models_cpp) asks for the round faces of the Gen 5-7 pack made in this folder (stage faces only; --faces-work, --jobs)')
    ap.add_argument('--faces-work', default='', help='work folder of --faces-only')
    a = ap.parse_args()
    if a.faces_only:
        sys.exit(faces_only(a))
    try:
        if a.data_folder and not a.pictures_out:
            a.pictures_out = os.path.join(a.data_folder, 'icons', 'pokedex')
        if a.gen57_pack:
            if not a.gen4_pack:
                raise RuntimeError('the Gen 5-7 pack needs the Gen 4 pack (--gen4-pack)')
            if not a.data_mods_exe:
                stem = 'xd_data_mods.exe' if os.name == 'nt' else 'xd_data_mods'
                for c in (os.path.join(program_dir(), stem), os.path.join(os.path.dirname(program_dir()), 'pbr_models', stem),
                          os.path.join(os.path.dirname(program_dir()), stem)):
                    if os.path.isfile(c):
                        a.data_mods_exe = c
                        break
            if not a.xd_exe or not os.path.isfile(a.xd_exe):
                raise RuntimeError('the game program (xd) was not found next to the launcher, so the faces and pictures cannot be drawn')
        run(a)
        if a.gen57_pack:
            import usum_gen57_make as G57
            try:
                r = G57.make_gen57(a, CONVERTER_VERSION)
            except G57.Fail as ex:
                say('error ' + ' '.join(str(ex).split()))
                sys.exit(1)
            except Exception as ex:
                traceback.print_exc()
                say('error The Gen 5-7 pack stopped (%s: %s). The log is in %s; Make again continues where it stopped.' % (
                    type(ex).__name__, ' '.join(str(ex).split())[:160], os.path.join(os.path.abspath(a.gen57_pack) + '.work', 'gen57.log')))
                sys.exit(1)
            say('done %d %d %d' % r if r else 'done 0 0 0')
    except SystemExit as ex:
        if isinstance(ex.code, str):
            say('error ' + ' '.join(ex.code.split()))
            sys.exit(2)
        raise
    except AssertionError as ex:
        say('error The file is not a decrypted 3DS cartridge dump the converter can read (%s).' % (' '.join(str(ex).split()) or 'unexpected layout'))
        traceback.print_exc()
        sys.exit(2)
    except Exception as ex:
        say('error ' + ' '.join(('%s' % ex).split()))
        traceback.print_exc()
        sys.exit(2 if type(ex).__name__ == 'Fail' and getattr(ex, 'code', 1) == 2 else 1)


if __name__ == '__main__':
    multiprocessing.freeze_support()
    main()
