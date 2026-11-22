"""Scan every model group of the player's Ultra Sun / Moon dump for effect parts: materials with special shaders,
texture-scrolling (UV) channels in the idle / other motions, visibility-switched meshes.
  python -I fx_audit.py --rom <decrypted .cci> --out <audit.json>
The result (a json list, one row per group) is what fx_table.py turns into the audit table."""
import argparse
import json
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ncch, garc, gfpack, gf_read, gamedetect


def scan(rom, out):
    r, _ = ncch.open_rom(rom)
    g = gamedetect.models_garc(r)
    n = (len(g.entries) - 1) // 9
    rows = []
    for k in range(n):
        bb = garc.maybe(g.read(1 + 9 * k))
        if len(bb) < 1000:
            continue
        try:
            base = 1 + 9 * k
            gm = gf_read.parse_model(gfpack.pc_split(bb)[0])
            tex0 = gm['tex_names'][0] if gm['tex_names'] else ''
            if not (tex0.startswith('pm') and tex0[2:6].isdigit()):
                continue
            row = dict(group=k, pm=int(tex0[2:6]), form=tex0[7:9] if len(tex0) >= 9 and tex0[6] == '_' else '00',
                       mats=[dict(name=m['name'], shader=m['shader'], frag=m['frag_shader'], emission=list(m['colors']['emission']),
                                  blend=list(m['colors']['blend']), c0=list(m['colors']['c0']),
                                  tex=[c['name'] for c in m['coords']], units=len(m['coords'])) for m in gm['materials']],
                       meshes=[me['name'] for me in gm['meshes']], bones=[b['name'] for b in gm['bones']])
            subs = gfpack.pc_split(garc.maybe(g.read(base + 4)))
            uvm, vis = set(), {}
            for slot, sb in enumerate(subs):
                if not sb:
                    continue
                try:
                    mo = gf_read.parse_motion(sb)
                except Exception:
                    continue
                if not mo:
                    continue
                for ch in mo['mats']:

                    uvm.add((ch['name'], ch['unit'], slot))
                for nm, vals in mo['vis'].items():
                    vis.setdefault(nm, set()).add(slot)
            row['uv_channels'] = sorted(uvm)
            row['vis_slots'] = {k_: sorted(v) for k_, v in vis.items()}
            rows.append(row)
        except Exception as ex:
            rows.append(dict(group=k, error='%s: %s' % (type(ex).__name__, ex)))
    json.dump(rows, open(out, 'w'))
    print('groups', len(rows))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    scan(a.rom, a.out)
