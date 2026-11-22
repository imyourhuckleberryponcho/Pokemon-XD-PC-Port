"""Your own 3D model (glTF / GLB) in place of a character of Pokemon XD: a trainer's battle model or a person's model.

  python tools/model_convert/gltf_to_pkx.py extract <disc.iso> <archive.fsys> <entry number> <out>
        takes one model out of YOUR disc (a GameCube disc image): the number is the entry's place in the archive (0 for a trainer's pkx_<name>.fsys;
        `list` shows them). The file is the game's data: keep it under build/, never in the repository or in a mod you share
  python tools/model_convert/gltf_to_pkx.py list <disc.iso> <archive.fsys>
        the entries of an archive: number, id, type, size, name (a model is type 15 (a battle model) or type 2 (a person's or object's model))
  python tools/model_convert/gltf_to_pkx.py skeleton <donor.pkx> <out.json>
        what the donor model's skeleton looks like (joint numbers, parents, rest positions, which joints the animations move), to rig against
  python tools/model_convert/gltf_to_pkx.py swap <donor.pkx> <model.glb|gltf> <out.pkx> [--height H] [--flip-winding] [--no-texture]
                                            [--joint-map map.json] [--attach N] [--texture-size 128] [--turn DEGREES]
        the donor character with your mesh and texture put in place of its own: the donor's skeleton, animations and PKX header stay, so every
        animation the game plays for that character (stand, walk, the throw, the win pose) still works with your model

How your mesh is attached to the donor's skeleton (the "retargeting"):
  - with --joint-map (a JSON file {"<glTF joint name or node name>": <donor joint number>}) and a skinned glTF: each vertex follows the donor
    joints its glTF weights name (up to 4);
  - otherwise (default) the mesh is split by the nearest donor bone: every triangle follows the bone closest to its centre ("auto fit"). That moves
    the limbs with the animations without any rigging, but gives hard seams where two bones meet. Good enough for a block character or a robot,
    rough for a smooth one. Put the model in the donor's rest pose (see `skeleton`) for the best fit;
  - with --attach N the whole mesh follows donor joint N only (a prop, or a character that should only bob with the body).

A trainer's or Pokemon's battle model (PKX) and a person's model in a room (a bare HSD archive, entry type 2) are both read; the output has the same kind.
The donor is a model from YOUR disc: the output contains its skeleton and animations, so it is for your own use (and for tests); it is not
something to share. Keep inputs and outputs under build/, never in the repository.
Needs Python 3 with numpy and Pillow (the same as tools/pbr_convert).
"""
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_convert'))

import numpy as np

import hsd_read
import hsd_write
import gx_tex
from model import Mesh, Vertex, Material, Texture, Image, mat_mul, mat_inv, mat_point, mat_vec




def _be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def _unlzss(data):
    total = _be32(data, 4)
    out = bytearray()
    win = bytearray(0x1000)
    w, i, fl = 0xFEE, 16, 0
    while len(out) < total:
        fl >>= 1
        if not fl & 0x100:
            fl = data[i] | 0xFF00
            i += 1
        if fl & 1:
            b = data[i]
            i += 1
            out.append(b)
            win[w] = b
            w = (w + 1) & 0xFFF
        else:
            lo, hi = data[i], data[i + 1]
            i += 2
            off, ln = lo | ((hi & 0xF0) << 4), (hi & 0xF) + 3
            for k in range(ln):
                if len(out) >= total:
                    break
                c = win[(off + k) & 0xFFF]
                out.append(c)
                win[w] = c
                w = (w + 1) & 0xFFF
    return bytes(out)


def disc_file(iso_path, name):
    """The bytes of one file of a GameCube disc image, found by name (the root folder and every other folder)."""
    with open(iso_path, 'rb') as f:
        f.seek(0x420)
        hdr = f.read(0x20)
        fst_off, fst_size = _be32(hdr, 4), _be32(hdr, 8)
        f.seek(fst_off)
        fst = f.read(fst_size)
        count = _be32(fst, 8)
        names = fst[count * 12:]
        want = name.lower().replace('\\', '/')
        stack = [(count, '')]
        for i in range(1, count):
            while stack and i >= stack[-1][0]:
                stack.pop()
            prefix = stack[-1][1] if stack else ''
            w0, a, b = struct.unpack_from('>III', fst, i * 12)
            nm = names[w0 & 0xFFFFFF:names.index(b'\0', w0 & 0xFFFFFF)].decode('latin1')
            if w0 >> 24:
                stack.append((b, prefix + nm + '/'))
            elif (prefix + nm).lower() == want or nm.lower() == want:
                f.seek(a)
                return f.read(b)
    raise SystemExit('%s is not on this disc image' % name)


def fsys_entries(data):
    if data[:4] != b'FSYS':
        raise SystemExit('this is not an archive')
    n = _be32(data, 0x0C)
    table = _be32(data, _be32(data, 0x18))
    out = []
    for k in range(n):
        h = _be32(data, table + 4 * k)
        no = _be32(data, h + 0x24)
        out.append(dict(index=k, id=_be32(data, h) >> 16, offset=_be32(data, h + 4), size=_be32(data, h + 8), packed=bool(_be32(data, h + 0xC) >> 31),
                        packed_size=_be32(data, h + 0x14), type=_be32(data, h + 0x20), name=data[no:data.index(b'\0', no)].decode('latin1') if no < len(data) else ''))
    return out


def fsys_extract(data, e):
    raw = data[e['offset']:e['offset'] + (e['packed_size'] if e['packed'] else e['size'])]
    return _unlzss(raw) if e['packed'] else raw




COMP = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


class Gltf:
    def __init__(self, path):
        self.dir = os.path.dirname(os.path.abspath(path))
        raw = open(path, 'rb').read()
        self.bin = b''
        if raw[:4] == b'glTF':
            _, _, total = struct.unpack_from('<III', raw, 0)
            at = 12
            while at < min(total, len(raw)):
                n, kind = struct.unpack_from('<II', raw, at)
                chunk = raw[at + 8:at + 8 + n]
                if kind == 0x4E4F534A:
                    self.doc = json.loads(chunk.decode('utf-8'))
                elif kind == 0x004E4942 and not self.bin:
                    self.bin = chunk
                at += 8 + ((n + 3) & ~3)
        else:
            self.doc = json.loads(raw.decode('utf-8'))
        self.buffers = {}

    def buffer(self, i):
        if i in self.buffers:
            return self.buffers[i]
        b = self.doc['buffers'][i]
        uri = b.get('uri')
        if uri is None:
            data = self.bin
        elif uri.startswith('data:'):
            import base64
            data = base64.b64decode(uri.split(',', 1)[1])
        else:
            data = open(os.path.join(self.dir, uri), 'rb').read()
        self.buffers[i] = data
        return data

    def accessor(self, i):
        a = self.doc['accessors'][i]
        fmt, size = COMP[a['componentType']]
        n = NCOMP[a['type']]
        count = a['count']
        if 'bufferView' not in a:
            return np.zeros((count, n), dtype=np.float64)
        bv = self.doc['bufferViews'][a['bufferView']]
        data = self.buffer(bv['buffer'])
        stride = bv.get('byteStride') or size * n
        base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        out = np.zeros((count, n), dtype=np.float64)
        for k in range(count):
            out[k] = struct.unpack_from('<' + fmt * n, data, base + k * stride)
        if a.get('normalized'):
            out /= float(2 ** (8 * size) - 1) if fmt in 'BH' else float(2 ** (8 * size - 1) - 1)
        return out

    def image_bytes(self, i):
        im = self.doc['images'][i]
        if 'bufferView' in im:
            bv = self.doc['bufferViews'][im['bufferView']]
            at = bv.get('byteOffset', 0)
            return self.buffer(bv['buffer'])[at:at + bv['byteLength']]
        uri = im['uri']
        if uri.startswith('data:'):
            import base64
            return base64.b64decode(uri.split(',', 1)[1])
        return open(os.path.join(self.dir, uri), 'rb').read()


def node_matrix(n):
    if 'matrix' in n:
        m = n['matrix']
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    t = n.get('translation', [0, 0, 0])
    q = n.get('rotation', [0, 0, 0, 1])
    s = n.get('scale', [1, 1, 1])
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[0][0] * s[0], r[0][1] * s[1], r[0][2] * s[2], t[0]],
            [r[1][0] * s[0], r[1][1] * s[1], r[1][2] * s[2], t[1]],
            [r[2][0] * s[0], r[2][1] * s[1], r[2][2] * s[2], t[2]],
            [0, 0, 0, 1]]


def mul4(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def gltf_meshes(g):
    """-> list of dicts: pos (N,3) in model space, nrm, uv, idx (M,3), material index, joints/weights (or None), node name."""
    doc = g.doc
    nodes = doc.get('nodes', [])
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []):
            parent[c] = i
    world = {}

    def wm(i):
        if i in world:
            return world[i]
        m = node_matrix(nodes[i])
        world[i] = mul4(wm(parent[i]), m) if i in parent else m
        return world[i]

    out = []
    for ni, n in enumerate(nodes):
        if 'mesh' not in n:
            continue
        M = np.array(wm(ni))
        N = np.linalg.inv(M[:3, :3]).T
        skin = doc['skins'][n['skin']] if 'skin' in n else None
        for prim in doc['meshes'][n['mesh']]['primitives']:
            if prim.get('mode', 4) != 4:
                continue
            at = prim['attributes']
            pos = g.accessor(at['POSITION'])
            pos = pos @ M[:3, :3].T + M[:3, 3]
            nrm = g.accessor(at['NORMAL']) @ N.T if 'NORMAL' in at else None
            if nrm is not None:
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
            uv = g.accessor(at['TEXCOORD_0']) if 'TEXCOORD_0' in at else None
            idx = g.accessor(prim['indices']).astype(int).reshape(-1) if 'indices' in prim else np.arange(len(pos))
            joints = g.accessor(at['JOINTS_0']).astype(int) if 'JOINTS_0' in at else None
            weights = g.accessor(at['WEIGHTS_0']) if 'WEIGHTS_0' in at else None
            names = None
            if skin is not None and joints is not None:
                names = [nodes[j].get('name', 'node%d' % j) for j in skin['joints']]
            out.append(dict(pos=pos, nrm=nrm, uv=uv, idx=idx.reshape(-1, 3), material=prim.get('material'), joints=joints, weights=weights,
                            joint_names=names, name=n.get('name', 'mesh%d' % ni)))
    return out


def gltf_texture(g, material_index, size):
    """The base colour picture of a material as RGBA uint8 (size x size), or None."""
    if material_index is None:
        return None
    m = g.doc.get('materials', [])[material_index]
    pbr = m.get('pbrMetallicRoughness', {})
    tex = pbr.get('baseColorTexture')
    if tex is None:
        c = pbr.get('baseColorFactor', [1, 1, 1, 1])
        px = np.zeros((size, size, 4), dtype=np.uint8)
        px[...] = [int(round(v * 255)) for v in c]
        return px
    src = g.doc['textures'][tex['index']]['source']
    from PIL import Image as PILImage
    import io
    im = PILImage.open(io.BytesIO(g.image_bytes(src))).convert('RGBA')
    return gx_tex.resize(np.asarray(im), size, size)




def donor_world(model):
    return model.world_static()


def bone_segments(model, world):
    """(joint, head, tail): every joint with a child is a bone from its position to the child's (the mean of the children); a leaf is a short stub."""
    segs = []
    for j in range(len(model.joints)):
        head = np.array([world[j][3], world[j][7], world[j][11]])
        kids = model.children(j)
        if kids:
            tails = [np.array([world[k][3], world[k][7], world[k][11]]) for k in kids]
            tail = np.mean(tails, axis=0)
        else:
            tail = head + np.array([0.0, 0.01, 0.0])
        segs.append((j, head, tail))
    return segs


def nearest_bone(points, segs, allowed):
    """For each point the joint whose bone segment is closest."""
    best = np.full(len(points), -1)
    bestd = np.full(len(points), 1e30)
    for j, a, b in segs:
        if j not in allowed:
            continue
        ab = b - a
        L = float(ab @ ab)
        t = np.clip(((points - a) @ ab) / L, 0, 1) if L > 1e-12 else np.zeros(len(points))
        d = np.linalg.norm(points - (a + np.outer(t, ab)), axis=1)
        better = d < bestd
        best[better] = j
        bestd[better] = d[better]
    return best


def skeleton_json(model):
    world = donor_world(model)
    anim_joints = set()
    for an in model.animations:
        anim_joints.update(an.joints.keys())
    pts = np.array([[w[3], w[7], w[11]] for w in world])
    return {
        'note': 'The skeleton of a model of the game: joint numbers, parents and rest positions (model units). Rig your glTF against it (joint names = "j<number>"), or use --joint-map.',
        'joints': [{'name': 'j%d' % i, 'number': i, 'parent': j.parent, 'rest_position': [round(float(x), 4) for x in pts[i]], 'animated': i in anim_joints}
                   for i, j in enumerate(model.joints)],
        'animations': len(model.animations),
        'bounds': {'min': [round(float(x), 4) for x in pts.min(0)], 'max': [round(float(x), 4) for x in pts.max(0)]},
    }




def load_donor(path):
    """-> (Pkx or None, Model). A trainer's or Pokemon's battle model has the PKX header; a person's model in a room is a bare HSD archive."""
    raw = open(path, 'rb').read()
    try:
        pkx, model, _ = hsd_read.read_pkx(raw, meshes=False)
        return pkx, model
    except ValueError:
        r = hsd_read.Reader(raw)
        return None, r.read(False)


def swap(donor_path, gltf_path, out_path, height=None, flip=False, texture=True, joint_map=None, attach=None, tex_size=128, turn=0.0):
    pkx, model = load_donor(donor_path)
    world = donor_world(model)
    pts = np.array([[w[3], w[7], w[11]] for w in world])
    g = Gltf(gltf_path)
    meshes = gltf_meshes(g)
    if not meshes:
        raise SystemExit('the glTF has no triangle meshes')
    allp = np.concatenate([m['pos'] for m in meshes])
    lo, hi = allp.min(0), allp.max(0)

    donor_h = float(pts[:, 1].max() - pts[:, 1].min()) if height is None else float(height)
    scale = donor_h / max(float(hi[1] - lo[1]), 1e-6)
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    ang = math.radians(turn)
    ca, sa = math.cos(ang), math.sin(ang)
    base = np.array([float(np.median(pts[:, 0])), float(pts[:, 1].min()), float(np.median(pts[:, 2]))])

    def to_donor(p):
        q = (p - np.array([cx, lo[1], cz])) * scale
        x, z = q[:, 0] * ca + q[:, 2] * sa, -q[:, 0] * sa + q[:, 2] * ca
        return np.stack([x, q[:, 1], z], axis=1) + base

    model.meshes = []
    model.materials = []
    model.images = []
    model.tluts = []
    mat_of = {}
    img_of = {}
    allowed = set(range(len(model.joints)))
    segs = bone_segments(model, world)
    inv_world = [mat_inv(w) for w in world]
    tris_total = 0
    for gm in meshes:
        pos = to_donor(gm['pos'])
        nrm = gm['nrm']
        if nrm is not None:
            nrm = np.stack([nrm[:, 0] * ca + nrm[:, 2] * sa, nrm[:, 1], -nrm[:, 0] * sa + nrm[:, 2] * ca], axis=1)
        uv = gm['uv']
        idx = gm['idx']

        key = gm['material']
        if key not in mat_of:
            mat = Material()
            mat.name = 'mod%d' % len(model.materials)
            if texture:
                px = gltf_texture(g, key, tex_size)
                if px is not None:
                    im = Image()
                    im.width = im.height = tex_size
                    im.format = 14
                    im.data = gx_tex.encode_cmpr(px)
                    model.images.append(im)
                    t = Texture()
                    t.image = len(model.images) - 1
                    t.tex_id = 0
                    mat.textures = [t]
            model.materials.append(mat)
            mat_of[key] = len(model.materials) - 1
        material = mat_of[key]

        if attach is not None:
            owner = np.full(len(idx), attach)
        elif joint_map and gm['joints'] is not None:
            owner = None
        else:
            centres = pos[idx].mean(axis=1)
            owner = nearest_bone(centres, segs, allowed)
        if owner is None:

            me = Mesh()
            me.joint = model.skeleton_root
            me.material = material
            me.name = gm['name']
            wmap = []
            for v in range(len(pos)):
                ws = []
                for jn, wt in zip(gm['joints'][v], gm['weights'][v]):
                    name = gm['joint_names'][jn]
                    if wt > 0 and name in joint_map:
                        ws.append((int(joint_map[name]), float(wt)))
                tot = sum(w for _, w in ws)
                ws = [(j, w / tot) for j, w in ws] if tot > 0 else [(model.skeleton_root, 1.0)]
                wmap.append(tuple(ws))
            for v in range(len(pos)):
                me.verts.append(Vertex(tuple(float(x) for x in pos[v]), tuple(float(x) for x in nrm[v]) if nrm is not None else None,
                                       (float(uv[v][0]), float(uv[v][1])) if uv is not None else None, None, wmap[v]))
            for jj in set(j for ws in wmap for j, _ in ws):
                if model.joints[jj].inv_bind is None:
                    model.joints[jj].inv_bind = inv_world[jj]
            me.tris = [tuple(int(i) for i in (t[::-1] if flip else t)) for t in idx]
            model.meshes.append(me)
            tris_total += len(idx)
            continue
        for j in sorted(set(int(o) for o in owner)):
            sel = idx[owner == j]
            me = Mesh()
            me.joint = j
            me.material = material
            me.rigid = True
            me.name = '%s_j%d' % (gm['name'], j)
            remap = {}
            for t in sel:
                tri = []
                for c in t:
                    if c not in remap:
                        p = mat_point(inv_world[j], tuple(float(x) for x in pos[c]))
                        n = None
                        if nrm is not None:
                            n = tuple(float(x) for x in mat_vec(inv_world[j], tuple(float(x) for x in nrm[c])))
                        remap[c] = len(me.verts)
                        me.verts.append(Vertex(tuple(p), n, (float(uv[c][0]), float(uv[c][1])) if uv is not None else None))
                    tri.append(remap[c])
                me.tris.append(tuple(tri[::-1]) if flip else tuple(tri))
            model.meshes.append(me)
            tris_total += len(sel)

    for jt in model.joints:
        jt.flags = None
    model.prune_material_trees = True
    archive, report = hsd_write.write_archive(model)
    if pkx is not None:
        pkx.archive = archive
        out = pkx.build()
    else:
        out = archive
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, 'wb').write(out)
    print('written %s: %d bytes, %d triangles in %d mesh pieces, %d material(s), %d picture(s); donor %d joints, %d animations; scale %.4f; %s' % (
        out_path, len(out), tris_total, len(model.meshes), len(model.materials), len(model.images), len(model.joints), len(model.animations), scale, report))
    return 0


def main(argv):
    if len(argv) < 2 or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == 'list' and len(argv) == 3:
        data = disc_file(argv[1], argv[2])
        for e in fsys_entries(data):
            print('%3d  id %5d  type %2d  %8d bytes  %s' % (e['index'], e['id'], e['type'], e['size'], e['name']))
        return 0
    if cmd == 'extract' and len(argv) == 5:
        data = disc_file(argv[1], argv[2])
        entries = fsys_entries(data)
        e = entries[int(argv[3])]
        os.makedirs(os.path.dirname(os.path.abspath(argv[4])), exist_ok=True)
        open(argv[4], 'wb').write(fsys_extract(data, e))
        print('wrote %s: entry %d of %s (%s, type %d, %d bytes)' % (argv[4], e['index'], argv[2], e['name'], e['type'], e['size']))
        return 0
    if cmd == 'skeleton' and len(argv) == 3:
        _, model = load_donor(argv[1])
        json.dump(skeleton_json(model), open(argv[2], 'w'), indent=1)
        print('skeleton of %s: %d joints -> %s' % (argv[1], len(model.joints), argv[2]))
        return 0
    if cmd == 'swap' and len(argv) >= 4:
        opts = argv[4:]
        kw = {}
        i = 0
        while i < len(opts):
            o = opts[i]
            if o == '--height':
                kw['height'] = float(opts[i + 1]); i += 1
            elif o == '--flip-winding':
                kw['flip'] = True
            elif o == '--no-texture':
                kw['texture'] = False
            elif o == '--joint-map':
                kw['joint_map'] = json.load(open(opts[i + 1])); i += 1
            elif o == '--attach':
                kw['attach'] = int(opts[i + 1]); i += 1
            elif o == '--texture-size':
                kw['tex_size'] = int(opts[i + 1]); i += 1
            elif o == '--turn':
                kw['turn'] = float(opts[i + 1]); i += 1
            else:
                print('unknown option', o)
                return 2
            i += 1
        return swap(argv[1], argv[2], argv[3], **kw)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
