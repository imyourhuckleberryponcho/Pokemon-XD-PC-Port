"""Where one model conversion spends its time, summed over a mix of species (cProfile, one pass each, no ladder repeats).
usage: python profile_mix.py <rom> <xd disc> nat[:start] ..."""
import collections
import cProfile
import os
import pstats
import sys

N3DS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, N3DS)
import gen57_all as A
import gen57_data as D
import gen57_speed as SP
import gf_to_xd
import make_proto as M
import noddisc
import pbr_models as PM
import usum_models as UM

rom, disc = sys.argv[1], sys.argv[2]
PM.find_nod()
u = D.Usum(rom)
d = PM.open_disc(disc, 'GXXE01', 'XD')
xdmap = UM.xd_species_models(d)
standins = A.pick_standins(u, xdmap)
mn, cw = A.load_tables()
want = {'_sample_at': 'pose sampling (sample_at)', 'skin_points': 'skinning (skin_points)', 'decimate': 'mesh slimming (decimate)',
        'make_images': 'pictures (make_images)', 'write_archive': 'file writer (hsd_write)', 'ground_offsets': 'ground offsets (skinning again)',
        'convert': 'whole convert'}
tot = collections.Counter()
for arg in sys.argv[3:]:
    nat, _, start = arg.partition(':')
    nat, start = int(nat), int(start or 0)
    arch, idx, _n = xdmap[standins[nat]]
    data = d.file(arch)
    ents = [e for e in noddisc.fsys_entries(data) if e.type == 15]
    xdb = noddisc.extract(data, ents[0])
    gm, texs, mots, grp = M.load_species(u.r, A.model_number(nat, mn), None)
    p = u.personal(nat)
    pr = cProfile.Profile()
    pr.enable()
    SP.convert_ladder(gf_to_xd.convert, gm, texs, mots, xdb, p.height_cm, start=start)
    pr.disable()
    st = pstats.Stats(pr)
    for (f, l, fn), (cc, nc, tt, ct, callers) in st.stats.items():
        if fn in want:
            tot[fn] += ct
    print(nat, 'profiled seconds', round(st.total_tt, 1), flush=True)
for k, v in tot.most_common():
    print('%-34s %6.1f s  %4.0f %%' % (want[k], v, 100 * v / tot['convert']))
