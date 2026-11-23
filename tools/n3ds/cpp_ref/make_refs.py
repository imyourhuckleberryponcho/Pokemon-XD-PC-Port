"""Make the REFERENCE files of the C++ converter port with the PYTHON converter (the reference), for the species in refset.json.

  python -I make_refs.py --rom <usum.cci> --xd-disc <XD iso> --out <scratch folder> [--only name,name] [--jobs 3]

Writes <out>/<name>.pkx (the model file the converter makes), <name>.json (tris, size, ladder step, seconds) and
<name>.log. Game data: only under <out> (a build\\ folder), never the repository. Uses the same calls as gen57_all.py /
gen57_forms.py / usum_models.py (same options, same size ladder with the speed table's start step).
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.dirname(HERE)
sys.path.insert(0, N3)
REPO = os.path.dirname(os.path.dirname(N3))
for sub in ('pbr_survey', 'pbr_convert'):
    sys.path.insert(0, os.path.join(REPO, 'tools', sub))

W = {}


def init(rom, xd_disc, out):
    import pbr_models as PM
    import usum_models as UM
    import gen57_data as D
    import gen57_all as A
    import gen57_speed as SP
    import gen57_forms as F
    import gamedetect
    PM.find_nod()
    W['out'] = out
    W['u'] = D.Usum(rom)
    W['d'] = PM.open_disc(xd_disc, 'GXXE01', 'XD')
    W['xdmap'] = UM.xd_species_models(W['d'])
    W['standin'] = A.pick_standins(W['u'], W['xdmap'])
    W['mn'], _ = A.load_tables()
    W['sp'] = SP.load(rom)
    W['by'], W['dexmap'] = F.groups_by_pm(W['u'])
    g = gamedetect.models_garc(W['u'].r)
    W['table'] = UM.lookup_table(UM.scan_groups(g), UM.dex_map())


def xd_bytes_of(nat):
    import noddisc
    arch, idx, name = W['xdmap'][nat]
    data = W['d'].file(arch)
    ent = [e for e in noddisc.fsys_entries(data) if e.index == idx][0]
    return noddisc.extract(data, ent)


def run(job):
    import gf_to_xd
    import make_proto as M
    import gen57_speed as SP
    import gen57_all as A
    import gen57_forms as F
    t0 = time.time()
    lines = []
    rep = dict(name=job['name'], kind=job['kind'])
    try:
        k, nat = job['kind'], job['nat']
        step = None
        if k == 'xd':
            gm, texs, mots, grp = M.load_species(W['u'].r, nat, W['table'][nat])
            pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes_of(nat), mode='role', log=lines.append,
                                         opts=dict(ground=True, decimate=2600, fire=True, light=1.0, fx=True, bake_scale=True, eyes=True,
                                                   size_rule=True, eye_blend=True, ghost_alpha=0.6, steel=1.2, lure=True, cheeks=1.0, match=True))
        elif k == 'g57':
            mnum = A.model_number(nat, W['mn'])
            grp0 = json.load(open(os.path.join(N3, 'gen57_model_numbers.json'))).get('group_override', {}).get(str(nat))
            gm, texs, mots, grp = M.load_species(W['u'].r, mnum, grp0)
            pkx, info, step = SP.convert_ladder(gf_to_xd.convert, gm, texs, mots, xd_bytes_of(W['standin'][nat]), W['u'].personal(nat).height_cm,
                                                start=W['sp'].get('ladder', {}).get(str(nat), 0))
        elif k == 'form':
            plan = [f for f in json.load(open(os.path.join(N3, 'gen57_forms.json')))['forms'] if f['nat'] == nat and f['form'] == job['form']][0]
            kind = plan['kind']
            code = plan.get('model') or ((F.MODEL_FORM_CODE[kind] % job['form']) if '%d' in F.MODEL_FORM_CODE.get(kind, '') else F.MODEL_FORM_CODE.get(kind, '00'))
            mnum = A.model_number(nat, W['mn'])
            gs = W['by'].get((mnum, code))
            sh = W['standin'].get(nat, nat)
            if sh not in W['xdmap'] and nat not in W['xdmap']:
                sh = F.standin_old(W['u'], W['xdmap'], nat)
            xdb = xd_bytes_of(sh if sh in W['xdmap'] else nat)
            record = W['u'].personal(nat).form_stats_index + job['form'] - 1
            gm, texs, mots, grp = M.load_species(W['u'].r, mnum, gs[0])
            nm = 'n3f_%d_%d' % (nat, job['form'])
            pkx, info, step = SP.convert_ladder(gf_to_xd.convert, gm, texs, mots, xdb, W['u'].personal(record).height_cm,
                                                start=W['sp'].get('forms', {}).get(nm, 0))
        elif k == 'g4':
            import usum_gen4
            import usum_models as UM
            model, standin = json.load(open(os.path.join(N3, 'usum_gen4_plan.json')))['plan'][str(nat)]
            us = W.setdefault('us4', usum_gen4.Usum(W['u'].r, W['u'].prod))
            gm, texs, mots, grp = M.load_species(W['u'].r, nat, W['table'][nat])
            pkx, info = gf_to_xd.convert(gm, texs, mots, xd_bytes_of(standin), mode='role', log=lines.append,
                                         opts=dict(ground=True, decimate=2600, fire=True, light=1.0, fx=True, bake_scale=True, eyes=True,
                                                   size_rule='mainline', height_cm=UM.u16(us.personal(nat), 0x24), hover_rule='model',
                                                   eye_blend=True, ghost_alpha=0.6, steel=1.2, lure=True, cheeks=1.0))
        rep.update(tris=info['tris'], size=len(pkx), step=step, group=grp)
        open(os.path.join(W['out'], job['name'] + '.pkx'), 'wb').write(pkx)
    except BaseException as e:
        rep['error'] = repr(e)
        lines.append(traceback.format_exc())
    rep['secs'] = round(time.time() - t0, 1)
    json.dump(rep, open(os.path.join(W['out'], job['name'] + '.json'), 'w'))
    open(os.path.join(W['out'], job['name'] + '.log'), 'w').write('\n'.join(map(str, lines)))
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='')
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--set', default=os.path.join(HERE, 'refset.json'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    jobs = json.load(open(a.set))['jobs']
    if a.only:
        jobs = [j for j in jobs if j['name'] in a.only.split(',')]
    jobs.sort(key=lambda j: j['name'] not in ('g57_655_delphox', 'g57_724_decidueye', 'g57_500_emboar'))
    with cf.ProcessPoolExecutor(a.jobs, initializer=init, initargs=(a.rom, a.xd_disc, a.out)) as ex:
        for r in ex.map(run, jobs):
            print(json.dumps(r), flush=True)


if __name__ == '__main__':
    main()
