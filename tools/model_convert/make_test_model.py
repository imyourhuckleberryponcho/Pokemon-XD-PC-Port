"""Makes a small made-up character as a .glb (a block robot with a checkered picture), for testing gltf_to_pkx.py. Nothing from any game.

  python tools/model_convert/make_test_model.py <out.glb> [skinned]
Y up, feet at y = 0, facing +Z, in the usual standing pose (arms down).
"""
import io
import json
import struct
import sys

import numpy as np
from PIL import Image


def box(cx, cy, cz, sx, sy, sz, tile):
    """8-corner box as 24 vertices (flat normals), 12 triangles (counter-clockwise from outside); uv = a tile of a 2x2 sheet."""
    faces = [((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]), ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
             ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]), ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
             ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]), ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)])]
    u0, v0 = (tile % 2) * 0.5, (tile // 2) * 0.5
    pos, nrm, uv, idx = [], [], [], []
    for n, corners in faces:
        base = len(pos)
        for k, c in enumerate(corners):
            pos.append((cx + c[0] * sx / 2, cy + c[1] * sy / 2, cz + c[2] * sz / 2))
            nrm.append(n)
            uv.append((u0 + (0.02 if k in (0, 3) else 0.48), v0 + (0.48 if k in (0, 1) else 0.02)))
        idx += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return pos, nrm, uv, idx


def main(out, skinned=False):
    parts = [box(0, 1.55, 0, 0.5, 0.5, 0.5, 0),
             box(0, 1.05, 0, 0.7, 0.8, 0.4, 1),
             box(-0.5, 1.05, 0, 0.25, 0.8, 0.3, 2), box(0.5, 1.05, 0, 0.25, 0.8, 0.3, 2),
             box(-0.2, 0.35, 0, 0.3, 0.7, 0.35, 3), box(0.2, 0.35, 0, 0.3, 0.7, 0.35, 3)]
    pos, nrm, uv, idx = [], [], [], []
    for p, n, u, i in parts:
        b = len(pos)
        pos += p
        nrm += n
        uv += u
        idx += [(a + b, c + b, d + b) for a, c, d in i]
    pos = np.array(pos, dtype='<f4')
    nrm = np.array(nrm, dtype='<f4')
    uv = np.array(uv, dtype='<f4')
    idx = np.array(idx, dtype='<u2').reshape(-1)
    img = np.zeros((64, 64, 4), dtype=np.uint8)
    colours = [(240, 200, 60), (60, 120, 220), (220, 70, 70), (70, 190, 90)]
    for t, c in enumerate(colours):
        x0, y0 = (t % 2) * 32, (t // 2) * 32
        for y in range(32):
            for x in range(32):
                chk = ((x // 8) + (y // 8)) % 2
                img[y0 + y, x0 + x] = (*(int(v * (0.75 if chk else 1.0)) for v in c), 255)
    if skinned:
        joints = np.zeros((len(pos), 4), dtype='<u2')
        weights = np.zeros((len(pos), 4), dtype='<f4')
        for i, p in enumerate(pos):
            t = min(1.0, max(0.0, (p[1] - 0.8) / 0.4))
            joints[i] = (0, 1, 0, 0)
            weights[i] = (1.0 - t, t, 0, 0)
    png = io.BytesIO()
    Image.fromarray(img).save(png, 'PNG')
    blobs = [pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes(), png.getvalue()]
    if skinned:
        blobs += [joints.tobytes(), weights.tobytes()]
    views, binary = [], b''
    for b in blobs:
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(b)})
        binary += b + b'\0' * (-len(b) % 4)
    doc = {
        'asset': {'version': '2.0', 'generator': 'make_test_model.py'},
        'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0, 'name': 'robot'}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'material': 0}]}],
        'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}],
        'textures': [{'source': 0}], 'images': [{'bufferView': 4, 'mimeType': 'image/png'}],
        'buffers': [{'byteLength': len(binary)}], 'bufferViews': views,
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3', 'min': pos.min(0).tolist(), 'max': pos.max(0).tolist()},
            {'bufferView': 1, 'componentType': 5126, 'count': len(nrm), 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': len(uv), 'type': 'VEC2'},
            {'bufferView': 3, 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'}],
    }
    if skinned:
        doc['nodes'] = [{'mesh': 0, 'name': 'robot', 'skin': 0, 'children': [1, 2]}, {'name': 'a'}, {'name': 'b'}]
        doc['skins'] = [{'joints': [1, 2]}]
        doc['meshes'][0]['primitives'][0]['attributes'].update({'JOINTS_0': 4, 'WEIGHTS_0': 5})
        doc['accessors'] += [{'bufferView': 5, 'componentType': 5123, 'count': len(pos), 'type': 'VEC4'}, {'bufferView': 6, 'componentType': 5126, 'count': len(pos), 'type': 'VEC4'}]
    j = json.dumps(doc).encode()
    j += b' ' * (-len(j) % 4)
    out_bytes = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(j) + 8 + len(binary)) + struct.pack('<II', len(j), 0x4E4F534A) + j + struct.pack('<II', len(binary), 0x004E4942) + binary
    open(out, 'wb').write(out_bytes)
    print('wrote', out, len(out_bytes), 'bytes,', len(idx) // 3, 'triangles')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1], skinned=len(sys.argv) > 2)
