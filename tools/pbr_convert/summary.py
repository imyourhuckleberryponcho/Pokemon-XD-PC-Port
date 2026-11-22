"""Print what a reader got out of a model file (neutral model: joints, meshes, materials, animations).

Usage: python tools/pbr_convert/summary.py <unpacked XD pkx | PBR sdr> [--tracks] [--joints]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def load(path):
    b = open(path, 'rb').read()
    if b[:4] == b'\x01\x00\x00\x04':
        import sdr_read
        model, warnings = sdr_read.read_sdr(b)
        return None, model, warnings
    import hsd_read
    return hsd_read.read_pkx(b)


def describe(model, warnings, tracks=False, joints=False):
    print('joints %d, skeleton root %d, with a bind matrix %d' % (
        len(model.joints), model.skeleton_root, sum(1 for j in model.joints if j.inv_bind)))
    if joints:
        depth = {}
        for i, j in enumerate(model.joints):
            depth[i] = depth[j.parent] + 1 if j.parent >= 0 else 0
            print('  %3d %s%s flags %s rot (%.3f %.3f %.3f) scale (%.3f %.3f %.3f) pos (%.3f %.3f %.3f)%s %s' % (
                i, '  ' * depth[i], j.name or '-', ('%08X' % j.flags) if j.flags is not None else '-',
                j.rot[0], j.rot[1], j.rot[2], j.scale[0], j.scale[1], j.scale[2], j.pos[0], j.pos[1], j.pos[2],
                ' bind' if j.inv_bind else '', j.note))
    print('meshes %d, materials %d, images %d, palettes %d' % (
        len(model.meshes), len(model.materials), len(model.images), len(model.tluts)))
    for i, me in enumerate(model.meshes):
        used = set()
        most = 0
        for v in me.verts:
            if v.weights:
                most = max(most, len(v.weights))
                for j, w in v.weights:
                    used.add(j)
        print('  mesh %d on joint %d%s: material %s, %d vertices, %d triangles, %s, flags %s' % (
            i, me.joint, ' (hidden)' if model.joints[me.joint].hidden else '', me.material, len(me.verts), len(me.tris),
            'rigid' if me.rigid else 'skinned to %d joints, at most %d a vertex' % (len(used), most),
            ('%04X' % me.pobj_flags) if me.pobj_flags is not None else '-'))
    for i, ma in enumerate(model.materials):
        print('  material %d %s: mode %08X, %s' % (i, ma.name, ma.rendermode, ', '.join(
            'image %s (%dx%d format %d) flags %08X wrap %d/%d' % (
                t.image, model.images[t.image].width, model.images[t.image].height, model.images[t.image].format,
                t.flags, t.wrap_s, t.wrap_t) for t in ma.textures if t.image is not None) or 'no texture'))
    print('animations %d' % len(model.animations))
    for i, a in enumerate(model.animations):
        nk = sum(len(t.keys) for o in a.joints.values() for t in o.tracks)
        print('  animation %d %s: %g frames, %d joints animated, %d keys, material animations on %d joints%s' % (
            i, a.name, a.end(), len(a.joints), nk, len(a.materials), ', bound boxes %d' % len(a.bounds) if a.bounds else ''))
        if tracks:
            for j in sorted(a.joints):
                o = a.joints[j]
                print('    joint %d: flags %08X end %g: %s' % (j, o.flags, o.end, ', '.join(
                    '%d[%d keys, ops %s%s]' % (t.kind, len(t.keys), sorted(set(k[0] for k in t.keys)),
                                             ' fmt %02X/%02X' % t.src_format if hasattr(t, 'src_format') else '')
                    for t in o.tracks)))
            for j in sorted(a.materials):
                for k, m in enumerate(a.materials[j]):
                    print('    material animation joint %d object %d: %s; textures: %s' % (
                        j, k, 'tracks %s' % [(t.kind, len(t.keys)) for t in m.aobj.tracks] if m.aobj else 'no tracks',
                        '; '.join('id %d images %s end %s tracks %s' % (
                            t.tex_id, t.images, t.aobj.end if t.aobj else None,
                            [(x.kind, x.keys) for x in t.aobj.tracks] if t.aobj else None) for t in m.texanims)))
    for w in warnings:
        print('NOTE:', w)


def main():
    pkx, model, warnings = load(sys.argv[1])
    if pkx:
        print('PKX: %d sections, particle bank %d bytes, archive %d bytes, head joint %d' % (
            len(pkx.sections), len(pkx.particles), len(pkx.archive), pkx.head_joint()))
    describe(model, warnings, '--tracks' in sys.argv, '--joints' in sys.argv)


if __name__ == '__main__':
    main()
