"""Which XD species spawn particle generators from their animations (a kind-40 track), and on which joints.
  python -I fx_xd_scan.py --xd-disc <iso> --out <json>      (reads the player's disc; output is game data: keep it under build\)"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(REPO, 'tools', 'pbr_survey'), os.path.join(REPO, 'tools', 'pbr_convert')):
    sys.path.insert(0, p)
import usum_models as U
import hsd_read
from model import pose_at


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--xd-disc', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    import pbr_models as PM
    import noddisc
    PM.find_nod()
    d = PM.open_disc(a.xd_disc, U.XD_ID, 'XD')
    xdm = U.xd_species_models(d)
    out = {}
    for nat in sorted(xdm):
        arch, idx, name = xdm[nat]
        data = d.file(arch)
        ent = [e for e in noddisc.fsys_entries(data) if e.index == idx][0]
        b = noddisc.extract(data, ent)
        try:
            p, m, w = hsd_read.read_pkx(b, meshes=True)
        except Exception as ex:
            out[nat] = dict(name=name, error=str(ex))
            continue
        pt = {}
        for si, an in enumerate(m.animations):
            for j, ao in an.joints.items():
                for t in ao.tracks:
                    if t.kind == 40:
                        pt.setdefault(j, []).append(si)
        xw = pose_at(m, m.animations[0], 0.0) if m.animations else None

        cnt, dobj = {}, []
        for me in m.meshes:
            dobj.append(cnt.get(me.joint, 0))
            cnt[me.joint] = dobj[-1] + 1
        slide = []
        for k, me in enumerate(m.meshes):
            for si, an in enumerate(m.animations):
                lst = an.materials.get(me.joint) or []
                mm = lst[dobj[k]] if len(lst) > dobj[k] else None
                if mm and mm.texanims:
                    slide.append((k, me.joint, len(me.verts)))
                    break
        out[nat] = dict(name=name, bank=len(p.particles), ptcl_joints={str(j): len(s) for j, s in pt.items()},
                        ptcl_pos={str(j): [round(xw[j][i], 1) for i in (3, 7, 11)] for j in pt} if xw else {},
                        slide_meshes=slide, njoints=len(m.joints))
    json.dump(out, open(a.out, 'w'), indent=0)
    print('species', len(out), 'with ptcl', sum(1 for v in out.values() if v.get('ptcl_joints')))


main()
