import sys, os, shutil, argparse, time, json
WT = ''
for p in (WT + r'\tools\n3ds', WT + r'\tools\pbr_survey', WT + r'\tools\pbr_convert'):
    sys.path.insert(0, p)
if __name__ == "__main__":
    out = sys.argv[1]; cpp = sys.argv[2]
    ROM = ''
    DISC = ''
    G4 = ''
    import gen57_all as A
    import gen57_forms as F
    mn = json.load(open(WT + r'\tools\n3ds\gen57_model_numbers.json'))['model_number']
    os.makedirs(out, exist_ok=True)
    for f in ('xd_moves_cache.json', 'xd_machines.json'):
        shutil.copy(os.path.join('py', f), os.path.join(out, f))
    pack = os.path.join(out, 'pack'); tmp = pack + '.tmp'
    for d in (pack, tmp):
        if os.path.isdir(d): shutil.rmtree(d)
    os.makedirs(tmp + '/files'); os.makedirs(tmp + '/reports')
    for n in range(494, 808):
        src = cpp + '/gen57/files/pkx_n3_%d.fsys' % n
        rp = json.load(open(cpp + '/gen57/reports/n3_%d.json' % n))
        if 'error' in rp or not os.path.exists(src):
            print('skip', n, rp); continue
        shutil.copy(src, tmp + '/files/')
        json.dump(dict(tris=rp['tris'], size=rp['size'], group=rp['group'], model_number=(n if n < 650 else int(mn[str(n)])), gid=rp['gid']), open(tmp + '/reports/%d.json' % n, 'w'))
    sys.argv = ['x', '--rom', ROM, '--xd-disc', DISC, '--gen4-pack', G4, '--out', pack, '--jobs', '2', '--cries', '--tm-cache', out + '/xd_machines.json', '--xd-moves-cache', out + '/xd_moves_cache.json']
    t = time.time(); A.main(); print('all', time.time() - t)
    packf = os.path.join(out, 'pack_forms')
    if os.path.isdir(packf): shutil.rmtree(packf)
    shutil.copytree(pack, packf)
    ft = packf + '.forms.tmp'
    if os.path.isdir(ft): shutil.rmtree(ft)
    os.makedirs(ft + '/files')
    plan = json.load(open(WT + r'\tools\n3ds\gen57_forms.json'))['forms']
    for f in plan:
        nm = 'n3f_%d_%d' % (f['nat'], f['form'])
        src = cpp + '/gen57/files/pkx_%s.fsys' % nm
        if os.path.exists(src):
            shutil.copy(src, ft + '/files/')
            open(ft + '/files/pkx_%s.fsys.ok' % nm, 'w').write('ok')
    os.environ['GEN57_FORMS_RESUME'] = '1'
    sys.argv = ['x', '--rom', ROM, '--xd-disc', DISC, '--pack', packf, '--jobs', '2', '--tm-cache', out + '/xd_machines.json']
    t = time.time(); F.main(); print('forms', time.time() - t)
    