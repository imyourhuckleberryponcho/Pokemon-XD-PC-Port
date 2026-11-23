import sys, os, shutil, argparse, time
WT = ''
for p in (WT + r'\tools\n3ds', WT + r'\tools\pbr_survey', WT + r'\tools\pbr_convert'):
    sys.path.insert(0, p)
out = sys.argv[1]
ROM = ''
DISC = ''
G4 = ''
import usum_gen57_make as G
import gen57_all as A
import gen57_forms as F
os.makedirs(out, exist_ok=True)
a = argparse.Namespace(data_mods_exe='', xd_disc=DISC)
t = time.time()
mc, tm = G.make_tables(a, out)
print('tables', time.time() - t)
pack = os.path.join(out, 'pack')
if os.path.isdir(pack): shutil.rmtree(pack)
sys.argv = ['x', '--rom', ROM, '--xd-disc', DISC, '--gen4-pack', G4, '--out', pack, '--jobs', '2', '--cries', '--tm-cache', tm, '--xd-moves-cache', mc, '--no-models']
t = time.time(); A.main(); print('all', time.time() - t)
packf = os.path.join(out, 'pack_forms')
if os.path.isdir(packf): shutil.rmtree(packf)
shutil.copytree(pack, packf)
sys.argv = ['x', '--rom', ROM, '--xd-disc', DISC, '--pack', packf, '--jobs', '2', '--tm-cache', tm, '--no-models']
t = time.time(); F.main(); print('forms', time.time() - t)
