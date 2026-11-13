"""GameCube texture pictures: decode to RGBA, resize, encode (our own code, from the GX format layouts).

Formats (GX numbers): 0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR. Pictures are stored in
tiles (blocks) row by row; inside a tile pixels row by row:
  I4 8x8 (4 bits), I8 8x4, IA4 8x4 (alpha high nibble), IA8 4x4 (alpha byte first), RGB565 / RGB5A3 4x4 (16 bit
  big-endian), RGBA8 4x4 in two 32-byte halves (A R pairs, then G B pairs), CMPR 8x8 tiles of four 4x4 sub-blocks
  (top left, top right, bottom left, bottom right), each sub-block: colour 0, colour 1 (RGB565 big-endian), then
  four bytes of 2-bit indices, a row a byte, leftmost pixel in the top bits. Colour 0 > colour 1: four colours
  (c0, c1, 2/3 c0 + 1/3 c1, 1/3 c0 + 2/3 c1); otherwise three and index 3 = see-through black.
Used by the converter to bring Battle Revolution's large pictures down to XD's sizes (pbr_to_xd.fit_textures).
Self-test: python gx_tex.py
"""
import numpy as np

TILE = {0: (8, 8), 1: (8, 4), 2: (8, 4), 3: (4, 4), 4: (4, 4), 5: (4, 4), 6: (4, 4), 14: (8, 8)}


def _untile(px, w, h, bw, bh):
    """px [tiles, bh, bw, ...] in tile order -> [ph, pw, ...] cropped to h, w."""
    tx, ty = (w + bw - 1) // bw, (h + bh - 1) // bh
    rest = px.shape[3:]
    a = px.reshape((ty, tx, bh, bw) + rest).swapaxes(1, 2).reshape((ty * bh, tx * bw) + rest)
    return a[:h, :w]


def _tile(img, bw, bh):
    """[h, w, ...] -> [tiles, bh, bw, ...] (padded with the edge pixels)."""
    h, w = img.shape[:2]
    tx, ty = (w + bw - 1) // bw, (h + bh - 1) // bh
    pad = [(0, ty * bh - h), (0, tx * bw - w)] + [(0, 0)] * (img.ndim - 2)
    a = np.pad(img, pad, mode='edge')
    rest = a.shape[2:]
    return a.reshape((ty, bh, tx, bw) + rest).swapaxes(1, 2).reshape((ty * tx, bh, bw) + rest)


def _565(v):
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1).astype(np.uint8)


def decode(fmt, w, h, data):
    """-> uint8 [h, w, 4] RGBA."""
    d = np.frombuffer(data, dtype=np.uint8)
    bw, bh = TILE[fmt]
    nt = ((w + bw - 1) // bw) * ((h + bh - 1) // bh)
    out = np.zeros((nt, bh, bw, 4), dtype=np.uint8)
    if fmt == 0:
        b = d[:nt * 32].reshape(nt, 8, 4)
        v = np.stack([b >> 4, b & 15], axis=-1).reshape(nt, 8, 8) * 17
        out[..., 0] = out[..., 1] = out[..., 2] = out[..., 3] = v
    elif fmt == 1:
        v = d[:nt * 32].reshape(nt, 4, 8)
        out[..., 0] = out[..., 1] = out[..., 2] = out[..., 3] = v
    elif fmt == 2:
        v = d[:nt * 32].reshape(nt, 4, 8)
        out[..., 3] = (v >> 4) * 17
        out[..., 0] = out[..., 1] = out[..., 2] = (v & 15) * 17
    elif fmt == 3:
        v = d[:nt * 32].reshape(nt, 4, 4, 2)
        out[..., 3] = v[..., 0]
        out[..., 0] = out[..., 1] = out[..., 2] = v[..., 1]
    elif fmt == 4:
        v = d[:nt * 32].view('>u2').reshape(nt, 4, 4).astype(np.int32)
        out[..., :3] = _565(v)
        out[..., 3] = 255
    elif fmt == 5:
        v = d[:nt * 32].view('>u2').reshape(nt, 4, 4).astype(np.int32)
        opaque = (v & 0x8000) != 0
        r5, g5, b5 = (v >> 10) & 31, (v >> 5) & 31, v & 31
        a3, r4, g4, b4 = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
        out[..., 0] = np.where(opaque, (r5 << 3) | (r5 >> 2), r4 * 17)
        out[..., 1] = np.where(opaque, (g5 << 3) | (g5 >> 2), g4 * 17)
        out[..., 2] = np.where(opaque, (b5 << 3) | (b5 >> 2), b4 * 17)
        out[..., 3] = np.where(opaque, 255, (a3 << 5) | (a3 << 2) | (a3 >> 1))
    elif fmt == 6:
        v = d[:nt * 64].reshape(nt, 2, 16, 2)
        out[..., 3] = v[:, 0, :, 0].reshape(nt, 4, 4)
        out[..., 0] = v[:, 0, :, 1].reshape(nt, 4, 4)
        out[..., 1] = v[:, 1, :, 0].reshape(nt, 4, 4)
        out[..., 2] = v[:, 1, :, 1].reshape(nt, 4, 4)
    elif fmt == 14:
        sb = d[:nt * 32].reshape(nt, 4, 8)                    # tile, sub-block, 8 bytes
        c0 = sb[..., 0].astype(np.int32) << 8 | sb[..., 1]
        c1 = sb[..., 2].astype(np.int32) << 8 | sb[..., 3]
        p0, p1 = _565(c0).astype(np.int32), _565(c1).astype(np.int32)
        four = (c0 > c1)[..., None]
        p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
        p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
        pal = np.stack([p0, p1, p2, p3], axis=-2)              # [nt, 4, 4 colours, 3]
        alpha = np.ones((nt, 4, 4), dtype=np.uint8) * 255
        alpha[..., 3] = np.where(four[..., 0], 255, 0)
        idx = np.stack([(sb[..., 4:8] >> s) & 3 for s in (6, 4, 2, 0)], axis=-1)   # [nt, 4 sub, 4 rows, 4 cols]
        ti = np.arange(nt)[:, None, None, None]
        si = np.arange(4)[None, :, None, None]
        col = pal[ti, si, idx]                                 # [nt, 4 sub, 4 rows, 4 cols, 3]
        al = alpha[ti, si, idx]
        sub = np.concatenate([col, al[..., None]], axis=-1).astype(np.uint8)     # [nt, 4 sub, 4, 4, 4]
        out[:, 0:4, 0:4] = sub[:, 0]
        out[:, 0:4, 4:8] = sub[:, 1]
        out[:, 4:8, 0:4] = sub[:, 2]
        out[:, 4:8, 4:8] = sub[:, 3]
    else:
        raise ValueError('texture format %d: no decoder' % fmt)
    return _untile(out, w, h, bw, bh)


def _to565(rgb):
    rgb = rgb.astype(np.int32)
    return ((rgb[..., 0] * 31 + 127) // 255) << 11 | ((rgb[..., 1] * 63 + 127) // 255) << 5 | ((rgb[..., 2] * 31 + 127) // 255)


def encode_cmpr(img):
    """uint8 [h, w, 4] -> CMPR bytes. Pixels with alpha < 128 become see-through (three-colour blocks)."""
    h, w = img.shape[:2]
    tiles = _tile(img, 8, 8)                                   # [nt, 8, 8, 4]
    nt = tiles.shape[0]
    subs = np.stack([tiles[:, 0:4, 0:4], tiles[:, 0:4, 4:8], tiles[:, 4:8, 0:4], tiles[:, 4:8, 4:8]], axis=1)
    px = subs.reshape(nt * 4, 16, 4).astype(np.float64)
    rgb, clear = px[..., :3], px[..., 3] < 128
    # end colours: the two pixels furthest apart along the block's main colour direction
    solid = ~clear
    cnt = np.maximum(solid.sum(1, keepdims=True), 1)
    mean = (rgb * solid[..., None]).sum(1, keepdims=True) / cnt[..., None]
    dev = (rgb - mean) * solid[..., None]
    cov = np.einsum('bpi,bpj->bij', dev, dev)
    axis = np.ones((len(px), 3))
    for _ in range(6):
        axis = np.einsum('bij,bj->bi', cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-9)
    proj = np.einsum('bpi,bi->bp', rgb - mean, axis)
    proj = np.where(solid, proj, np.nan)
    with np.errstate(all='ignore'):
        lo = np.nan_to_num(np.nanargmin(np.where(solid.any(1, keepdims=True), proj, 0.0), axis=1))
        hi = np.nan_to_num(np.nanargmax(np.where(solid.any(1, keepdims=True), proj, 0.0), axis=1))
    a = rgb[np.arange(len(px)), hi]
    b = rgb[np.arange(len(px)), lo]
    c0, c1 = _to565(a), _to565(b)
    has_clear = clear.any(1)
    # four-colour blocks need c0 > c1, three-colour (see-through) blocks c0 <= c1
    swap = np.where(has_clear, c0 > c1, c0 < c1)
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    same = (c0 == c1) & ~has_clear
    c0 = np.where(same & (c1 < 0xFFFF), c0 + 1, c0)
    c1 = np.where(same & (c1 == 0xFFFF), c1 - 1, c1)
    p0, p1 = _565(c0).astype(np.float64), _565(c1).astype(np.float64)
    four = (c0 > c1)[:, None]
    pal = np.stack([p0, p1, np.where(four, (2 * p0 + p1) / 3, (p0 + p1) / 2), np.where(four, (p0 + 2 * p1) / 3, 1e9)], axis=1)
    dist = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)    # [b, 16, 4]
    idx = dist.argmin(-1)
    idx = np.where(clear, 3, idx)
    bits = idx.reshape(-1, 4, 4)
    rows = (bits[..., 0] << 6) | (bits[..., 1] << 4) | (bits[..., 2] << 2) | bits[..., 3]
    out = np.zeros((len(px), 8), dtype=np.uint8)
    out[:, 0], out[:, 1] = c0 >> 8, c0 & 255
    out[:, 2], out[:, 3] = c1 >> 8, c1 & 255
    out[:, 4:8] = rows
    return out.tobytes()


def encode_rgb5a3(img):
    tiles = _tile(img, 4, 4).astype(np.int32)
    r, g, b, a = tiles[..., 0], tiles[..., 1], tiles[..., 2], tiles[..., 3]
    opaque = a >= 0xE0
    v = np.where(opaque, 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3),
                 ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
    return v.astype('>u2').tobytes()


def encode_ia8(img):
    tiles = _tile(img, 4, 4)
    lum = tiles[..., :3].astype(np.int32).mean(-1).round().astype(np.uint8)
    return np.stack([tiles[..., 3], lum], axis=-1).tobytes()


def resize(img, w, h):
    """Box-filtered resize (premultiplied by alpha so see-through texels don't bleed their colour)."""
    from PIL import Image
    a = img.astype(np.float32) / 255.0
    pre = np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], axis=-1)
    chans = [np.asarray(Image.fromarray(pre[..., c]).resize((w, h), Image.BOX)) for c in range(4)]
    pre = np.stack(chans, axis=-1)
    al = pre[..., 3:4]
    rgb = np.where(al > 1e-6, pre[..., :3] / np.maximum(al, 1e-6), 0.0)
    return (np.clip(np.concatenate([rgb, al], axis=-1), 0, 1) * 255 + 0.5).astype(np.uint8)


def decode_c14x2(w, h, data, pal_format):
    """Palette picture (GX C14X2: one 16-bit index per pixel in 4x4 tiles; the palette, 16-bit colours, follows the
    pixels in the same block: Regigigas's body) -> uint8 [h, w, 4] RGBA. pal_format: 0 IA8, 1 RGB565, 2 RGB5A3."""
    tx, ty = (w + 3) // 4, (h + 3) // 4
    n = tx * ty * 16
    idx = np.frombuffer(data[:n * 2], dtype='>u2').astype(np.int32) & 0x3FFF
    pal = np.frombuffer(data[n * 2:], dtype='>u2').astype(np.int32)
    if len(pal) == 0:
        raise ValueError('palette picture without a palette')
    v = pal[np.minimum(idx, len(pal) - 1)]
    rgba = np.zeros(v.shape + (4,), dtype=np.uint8)
    if pal_format == 1:
        rgba[..., :3] = _565(v)
        rgba[..., 3] = 255
    elif pal_format == 2:
        opaque = (v & 0x8000) != 0
        r5, g5, b5 = (v >> 10) & 31, (v >> 5) & 31, v & 31
        a3, r4, g4, b4 = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
        rgba[..., 0] = np.where(opaque, (r5 << 3) | (r5 >> 2), r4 * 17)
        rgba[..., 1] = np.where(opaque, (g5 << 3) | (g5 >> 2), g4 * 17)
        rgba[..., 2] = np.where(opaque, (b5 << 3) | (b5 >> 2), b4 * 17)
        rgba[..., 3] = np.where(opaque, 255, (a3 << 5) | (a3 << 2) | (a3 >> 1))
    else:
        rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = v & 0xFF
        rgba[..., 3] = v >> 8
    return _untile(rgba.reshape(tx * ty, 4, 4, 4), w, h, 4, 4)


def _selftest():
    rng = np.random.default_rng(1)
    ok = True
    # a smooth picture with a see-through hole, 64x40
    y, x = np.mgrid[0:40, 0:64]
    img = np.stack([x * 4, y * 6, 255 - x * 3, np.where((x - 32) ** 2 + (y - 20) ** 2 < 64, 0, 255)], -1).astype(np.uint8)
    back = decode(14, 64, 40, encode_cmpr(img))
    solid = img[..., 3] == 255
    err = np.abs(back[..., :3].astype(int) - img[..., :3])[solid].mean()
    clear_ok = (back[..., 3][~solid] == 0).all() and (back[..., 3][solid] == 255).all()
    print('CMPR round trip: mean colour error %.2f, see-through kept %s' % (err, clear_ok))
    ok &= err < 6 and clear_ok
    for fmt, enc in ((5, encode_rgb5a3), (3, encode_ia8)):
        b2 = decode(fmt, 64, 40, enc(img))
        e2 = np.abs(b2.astype(int) - img)[..., 3].max()
        print('format %d round trip: alpha error max %d' % (fmt, e2))
        ok &= e2 <= 36
    r = resize(img, 32, 20)
    print('resize to 32x20: shape', r.shape)
    ok &= r.shape == (20, 32, 4)
    print('ALL OK' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(_selftest())
