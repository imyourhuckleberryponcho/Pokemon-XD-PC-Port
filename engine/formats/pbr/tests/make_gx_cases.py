r"""Makes the reference file for pbr_gx_test: runs the Python codec (tools/pbr_convert/gx_tex.py) on every texture of the
given Battle Revolution model files (game data: pass files from under build\) plus random pictures, and writes the answers.
Usage: python make_gx_cases.py <out.bin> <model.sdr>...
Record (little endian): i32 fmt (-1 = raw RGBA input), w, h, u32 len, data; then blobs (u32 len + bytes):
decoded RGBA, cmpr, rgb5a3, ia8, [i32 nw, nh + resized RGBA], cmpr of the resized picture."""
import os, struct, sys
import numpy as np
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', '..', '..', 'tools', 'pbr_convert'))
import gx_tex, sdr_read


def blob(f, b):
    f.write(struct.pack('<I', len(b))); f.write(b)


def record(f, fmt, w, h, data, img):
    f.write(struct.pack('<iiiI', fmt, w, h, len(data))); f.write(data)
    blob(f, img.tobytes())
    blob(f, gx_tex.encode_cmpr(img))
    blob(f, gx_tex.encode_rgb5a3(img))
    blob(f, gx_tex.encode_ia8(img))
    nw, nh = max(w // 2, 1), max(h // 2, 1)
    if (w, h) in ((1, 1),):
        nw, nh = 1, 1
    r = gx_tex.resize(img, nw, nh)
    f.write(struct.pack('<ii', nw, nh)); blob(f, r.tobytes())
    blob(f, gx_tex.encode_cmpr(r))


def main():
    out = sys.argv[1]
    n = 0
    with open(out, 'wb') as f:
        for p in sys.argv[2:]:
            s = sdr_read.read(open(p, 'rb').read())
            for t in s.textures:
                if t['format'] not in gx_tex.TILE:
                    continue
                img = gx_tex.decode(t['format'], t['width'], t['height'], t['data'])
                record(f, t['format'], t['width'], t['height'], t['data'], img)
                n += 1
        rng = np.random.default_rng(7)
        for i, (w, h) in enumerate([(37, 21), (64, 64), (5, 3), (16, 16), (8, 8), (130, 66), (9, 17)] * 4):
            kind = i % 4
            if kind == 0:
                img = rng.integers(0, 256, (h, w, 4), dtype=np.uint8)
            elif kind == 1:
                img = rng.integers(0, 256, (h, w, 4), dtype=np.uint8); img[..., 3] = np.where(rng.random((h, w)) < .3, 0, 255)
            elif kind == 2:
                base = rng.integers(0, 256, 3); img = np.zeros((h, w, 4), np.uint8); img[..., :3] = base; img[..., 3] = 255
                img[..., :3] = np.clip(img[..., :3].astype(int) + rng.integers(-3, 4, (h, w, 3)), 0, 255)
            else:
                img = np.zeros((h, w, 4), np.uint8); img[..., 3] = 255; img[::2, ::2, :3] = 255
            record(f, -1, w, h, img.tobytes(), img)
            n += 1
        # random picture bytes in every format (formats the real models do not use are covered this way too)
        for fmt in sorted(gx_tex.TILE):
            for (w, h) in [(16, 16), (37, 21), (9, 5)]:
                bw, bh = gx_tex.TILE[fmt]
                nt = ((w + bw - 1) // bw) * ((h + bh - 1) // bh)
                data = rng.integers(0, 256, nt * (64 if fmt == 6 else 32), dtype=np.uint8).tobytes()
                record(f, fmt, w, h, data, gx_tex.decode(fmt, w, h, data))
                n += 1
    print('wrote', n, 'cases to', out)

main()
