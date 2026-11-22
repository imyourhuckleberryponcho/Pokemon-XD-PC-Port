"""Test of gltf_to_pkx.py with made-up data only (no game file): a three-joint donor, the block robot, the swap, and the result read back.

  python tools/model_convert/test_model_convert.py
"""
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_convert'))
sys.path.insert(0, HERE)

import hsd_read
import hsd_write
import make_test_model
import gltf_to_pkx
from model import Model, Joint, Mesh, Vertex, Material, Animation, AnimObj, Track, OP_LIN

fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        fails += 1


def donor():
    m = Model()
    for i, (parent, pos) in enumerate([(-1, (0, 0, 0)), (0, (0, 1.0, 0)), (1, (0, 0.8, 0))]):
        j = Joint()
        j.parent = parent
        j.pos = pos
        m.joints.append(j)
    me = Mesh()
    me.joint = 2
    me.rigid = True
    me.verts = [Vertex((0, 0, 0), (0, 0, 1), (0, 0)), Vertex((0.1, 0, 0), (0, 0, 1), (1, 0)), Vertex((0, 0.1, 0), (0, 0, 1), (0, 1))]
    me.tris = [(0, 1, 2)]
    m.meshes = [me]
    m.materials = [Material()]
    an = Animation()
    ao = AnimObj()
    ao.end = 10.0
    ao.tracks = [Track(6, [(OP_LIN, 0.0, 0.0, 5), (OP_LIN, 0.3, 0.0, 5)])]
    an.joints[1] = ao
    m.animations = [an]
    archive, _ = hsd_write.write_archive(m)
    header = bytearray(0x84)
    struct.pack_into('>H', header, 0x1A, 0xC)
    p = hsd_read.Pkx()
    p.header = bytes(header)
    p.sections = [bytes(0xD0)]
    p.archive = archive
    return p.build()


with tempfile.TemporaryDirectory() as d:
    donor_path = os.path.join(d, 'donor.pkx')
    open(donor_path, 'wb').write(donor())
    glb = os.path.join(d, 'robot.glb')
    make_test_model.main(glb)
    out = os.path.join(d, 'out.pkx')
    check(gltf_to_pkx.swap(donor_path, glb, out) == 0, 'the swap writes a model')
    pkx, model, warnings = hsd_read.read_pkx(open(out, 'rb').read())
    check(len(model.joints) == 3 and len(model.animations) == 1, "the donor's skeleton and animation are kept")
    check(len(model.meshes) >= 1 and sum(len(m.tris) for m in model.meshes) == 72, 'all 72 triangles of the robot are in the model')
    check(len(model.images) == 1 and model.images[0].format == 14, 'its picture is in the model (CMPR)')
    pts = hsd_read.__dict__ and None
    from model import pose_at, skin_positions
    world = pose_at(model, None, 0)
    drawn = skin_positions(model, world)
    ys = [p[1] for piece in drawn for p in piece]
    check(abs(min(ys) - 0.0) < 0.05 and abs(max(ys) - 1.8) < 0.1, 'it stands on the donor\'s lowest joint and is as tall as the donor (%.2f to %.2f)' % (min(ys), max(ys)))
    world2 = pose_at(model, model.animations[0], 5.0)
    drawn2 = skin_positions(model, world2)
    moved = max(abs(a[1] - b[1]) for pa, pb in zip(drawn, drawn2) for a, b in zip(pa, pb))
    check(moved > 0.05, 'the donor\'s animation moves the new mesh (largest move %.2f)' % moved)
    out2 = os.path.join(d, 'attach.pkx')
    check(gltf_to_pkx.swap(donor_path, glb, out2, attach=1) == 0 and len(hsd_read.read_pkx(open(out2, 'rb').read())[1].meshes) == 1, '--attach puts the whole mesh on one joint')

    skinned = os.path.join(d, 'skinned.glb')
    make_test_model.main(skinned, skinned=True)
    out3 = os.path.join(d, 'skin.pkx')
    check(gltf_to_pkx.swap(donor_path, skinned, out3, joint_map={'a': 1, 'b': 2}) == 0, '--joint-map: a skinned glTF is written with weights on the donor joints')
    _, m3, _ = hsd_read.read_pkx(open(out3, 'rb').read())
    wj = set(j for me in m3.meshes for v in me.verts if v.weights for j, w in v.weights)
    check(len(m3.meshes) == 1 and not m3.meshes[0].rigid and wj <= {1, 2} and len(wj) >= 1, 'its vertices are weighted to joints 1 and 2 of the donor (%s)' % sorted(wj))

print('ALL OK' if fails == 0 else 'FAILURES: %d' % fails)
sys.exit(1 if fails else 0)
