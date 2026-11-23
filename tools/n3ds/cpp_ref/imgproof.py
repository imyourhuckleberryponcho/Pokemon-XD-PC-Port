import sys, os, subprocess, numpy as np
sys.path.insert(0, '')
from PIL import Image
import gen57_faces as GF
tool = ''
d = 'imgproof'; os.makedirs(d, exist_ok=True)
rng = np.random.default_rng(5)
def blob(n, s):
    yy, xx = np.mgrid[0:s, 0:s]; a = np.zeros((s, s, 4), np.float32)
    for k in range(n):
        cx, cy, r = rng.uniform(0.2, 0.8, 2).tolist() + [rng.uniform(0.1, 0.35)]
        m = np.clip(1 - np.hypot(xx / s - cx, yy / s - cy) / r, 0, 1)
        col = rng.uniform(0, 255, 3); a[..., :3] += m[..., None] * col; a[..., 3] = np.maximum(a[..., 3], m * 255)
    a[..., :3] = np.clip(a[..., :3] / np.maximum(a[..., 3:4] / 255, 0.01) * 0.6, 0, 255) * (a[..., 3:4] > 0)
    return a.clip(0, 255).astype(np.uint8)
bad = 0
for i in range(12):
    s = [512, 1024][i % 2]
    im = blob(4, s)
    Image.fromarray(im, 'RGBA').save(f'{d}/in{i}.png')
    subprocess.run([tool, 'round', f'{d}/in{i}.png', f'{d}/out{i}.png'], check=True)
    a = np.asarray(Image.open(f'{d}/out{i}.png').convert('RGBA')); b = np.asarray(GF.round_face(f'{d}/in{i}.png'))
    ok = np.array_equal(a, b); bad += not ok
    ok2 = subprocess.run([tool, 'ok', f'{d}/in{i}.png'], capture_output=True, text=True).stdout.strip() == ('1' if GF.face_ok(f'{d}/in{i}.png') else '0')
    print('round', i, s, 'same' if ok else 'DIFF %d' % (a != b).sum(), 'face_ok', ok2)

for i in range(12):
    ic = rng.integers(0, 256, (32, 64, 4), dtype=np.uint8)
    ic[..., 3] = np.where(rng.random((32, 64)) < 0.6, 0, ic[..., 3] // (1 + i % 3 * 80))
    ic[:4] = 0; ic[:, :6] = 0; ic[:, -9:] = 0; ic[-3:] = 0
    Image.fromarray(ic, 'RGBA').save(f'{d}/ic{i}.png')
    subprocess.run([tool, 'box', f'{d}/ic{i}.png', f'{d}/iout{i}.png'], check=True)
    img = Image.fromarray(ic); box = img.getbbox(); img = img.crop(box); side = max(img.size) + 2
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0)); sq.paste(img, ((side - img.size[0]) // 2, (side - img.size[1]) // 2))
    ref = np.asarray(sq.resize((84, 84), Image.LANCZOS)); a = np.asarray(Image.open(f'{d}/iout{i}.png').convert('RGBA'))
    ok = np.array_equal(a, ref); bad += not ok
    print('box', i, box, 'same' if ok else 'DIFF %d' % (a != ref).sum())
print('TOTAL DIFFERENT', bad)
