"""Battle Revolution Pokemon model -> Pokemon XD battle model (PKX + HSD archive). First proof; see
docs/BATTLE_REVOLUTION.md ("The converter").

  python tools/pbr_convert/convert.py roundtrip <xd.pkx> <out.pkx>
        read an XD model into the neutral model and write it back with our own writer (proves the writer)
  python tools/pbr_convert/convert.py pbr <pbr.sdr> <xd.pkx of the same Pokemon> <out.pkx> [options]
        the PBR model in XD's container: XD's own PKX header and sections are kept (which animation plays for
        which kind of move), re-pointed at the new skeleton and animation list
  python tools/pbr_convert/convert.py check <a.pkx> <b.pkx>
        compare two XD models as drawn (our own evaluation of joints, skin and animations)

Inputs and outputs are game data: keep them under build/, never in the repository.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hsd_read
import hsd_write
from model import pose_at, skin_positions


def roundtrip(src, dst):
    b = open(src, 'rb').read()
    pkx, model, warnings = hsd_read.read_pkx(b)
    for w in warnings:
        print('NOTE:', w)

    same = diff = 0
    for an in model.animations:
        objs = list(an.joints.values())
        for lst in an.materials.values():
            for ma in lst:
                if ma.aobj:
                    objs.append(ma.aobj)
                objs += [t.aobj for t in ma.texanims if t.aobj]
        for o in objs:
            for t in o.tracks:
                mine = hsd_write.write_fobj_keys(t.keys, *t.src_format)
                if mine == t.src_bytes or (t.src_bytes.startswith(mine) and not any(t.src_bytes[len(mine):])):
                    same += 1
                else:
                    diff += 1
                    if diff <= 3:
                        print('track differs: kind %d format %02X/%02X\n  file %s\n  mine %s' % (
                            t.kind, t.src_format[0], t.src_format[1], t.src_bytes[:48].hex(' '), mine[:48].hex(' ')))
    print('animation tracks written back byte for byte: %d, different: %d' % (same, diff))
    archive, report = hsd_write.write_archive(model)
    pkx.archive = archive
    out = pkx.build()
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    open(dst, 'wb').write(out)
    print('written %s: %d bytes (source %d); %s' % (dst, len(out), len(b), report))
    return 0


def drawn(model, anim, frame):
    world = pose_at(model, model.animations[anim] if anim is not None else None, frame)
    return skin_positions(model, world)


def check(a_path, b_path):
    """Largest distance between the two files' drawn triangle corners (mesh by mesh, triangle by triangle)."""
    _, a, _ = hsd_read.read_pkx(open(a_path, 'rb').read())
    _, b, _ = hsd_read.read_pkx(open(b_path, 'rb').read())
    print('joints %d / %d, meshes %d / %d, animations %d / %d' % (
        len(a.joints), len(b.joints), len(a.meshes), len(b.meshes), len(a.animations), len(b.animations)))
    if len(a.meshes) != len(b.meshes) or len(a.animations) != len(b.animations):
        print('DIFFERENT shape of model')
        return 1
    worst = 0.0
    for an in [None] + list(range(len(a.animations))):
        end = a.animations[an].end() if an is not None else 0
        frames = sorted(set([0.0, end * 0.37, end * 0.5, end]))
        for f in frames:
            pa, pb = drawn(a, an, f), drawn(b, an, f)
            for mi, (ma, mb) in enumerate(zip(a.meshes, b.meshes)):
                if len(ma.tris) != len(mb.tris):
                    print('mesh %d: %d / %d triangles' % (mi, len(ma.tris), len(mb.tris)))
                    return 1
                if not pa[mi]:
                    continue
                ka = sorted(tuple(sorted(tuple(round(c, 3) for c in pa[mi][i]) for i in t)) for t in ma.tris)
                kb = sorted(tuple(sorted(tuple(round(c, 3) for c in pb[mi][i]) for i in t)) for t in mb.tris)
                for ta, tb in zip(ka, kb):
                    for ca, cb in zip(ta, tb):
                        d = max(abs(x - y) for x, y in zip(ca, cb))
                        worst = max(worst, d)
    print('largest corner distance over all animations (4 frames each): %.5f' % worst)
    return 0 if worst < 0.01 else 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == 'roundtrip':
        return roundtrip(sys.argv[2], sys.argv[3])
    if cmd == 'check':
        return check(sys.argv[2], sys.argv[3])
    if cmd == 'pbr':
        import pbr_to_xd
        return pbr_to_xd.main(sys.argv[2:])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
