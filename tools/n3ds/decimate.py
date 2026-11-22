"""Mesh slimming for one drawing piece: quadric edge collapse that keeps the silhouette, UVs and skin weights.

decimate(pos, uv, wts, tris, target) -> (kept vertex indices, new triangles over positions in that list)
- pos (n,3) float, uv (n,2) float or None, wts: list of tuples ((joint, weight), ...) per vertex, tris: list of (a, b, c).
- Half-edge collapse a -> b (a takes b's position, UV, normal, weights: nothing new is invented, so seams stay as they are).
- A vertex on the piece's border (an edge with one face: the open rim, every UV seam / skin-piece cut is one, because the
  vertices there are duplicated) is never removed, so the piece's outline, its UV islands and its joins to the next piece stay.
- Cost = quadric error of the position + a UV stretch term + a skin-weight difference term; a collapse that flips a face
  or makes a non-manifold edge is refused.
Pure numpy + heapq; a 1,000-triangle piece takes well under a second. No game data in this file.
"""
import heapq
import numpy as np


def _plane_quadrics(pos, tris):
    n = len(pos)
    Q = np.zeros((n, 4, 4))
    for a, b, c in tris:
        pa, pb, pc = pos[a], pos[b], pos[c]
        nn = np.cross(pb - pa, pc - pa)
        area = np.linalg.norm(nn)
        if area < 1e-12:
            continue
        nn = nn / area
        p = np.array([nn[0], nn[1], nn[2], -float(nn @ pa)])
        K = np.outer(p, p) * (area * 0.5)
        Q[a] += K
        Q[b] += K
        Q[c] += K
    return Q


def decimate(pos, uv, wts, tris, target, uv_weight=4.0, skin_weight=2.0):
    pos = np.asarray(pos, float)
    tris = [tuple(int(x) for x in t) for t in tris]
    if len(tris) <= target or len(tris) < 8:
        return list(range(len(pos))), tris
    n = len(pos)
    scale = float(np.linalg.norm(pos.max(0) - pos.min(0))) or 1.0
    Q = _plane_quadrics(pos, tris)
    uvn = None if uv is None else np.asarray(uv, float)
    wd = [dict(w) if w else {} for w in wts]
    faces = {i: t for i, t in enumerate(tris)}
    vf = {}
    edge_faces = {}
    for i, t in faces.items():
        for k in range(3):
            vf.setdefault(t[k], set()).add(i)
            e = tuple(sorted((t[k], t[(k + 1) % 3])))
            edge_faces[e] = edge_faces.get(e, 0) + 1
    locked = set()
    for (a, b), c in edge_faces.items():
        if c != 2:
            locked.add(a)
            locked.add(b)

    alive = len(faces)
    ver = [0] * n

    def wdiff(a, b):
        da, db = wd[a], wd[b]
        keys = set(da) | set(db)
        return sum(abs(da.get(k, 0.0) - db.get(k, 0.0)) for k in keys)

    def cost(a, b):
        v = np.array([pos[b][0], pos[b][1], pos[b][2], 1.0])
        c = float(v @ (Q[a] + Q[b]) @ v) / (scale * scale)
        if uvn is not None:
            c += uv_weight * float(np.sum((uvn[a] - uvn[b]) ** 2)) * 0.01
        c += skin_weight * wdiff(a, b) * 0.001
        return max(c, 0.0)

    def neighbours(a):
        s = set()
        for f in vf.get(a, ()):
            s.update(faces[f])
        s.discard(a)
        return s

    heap = []

    def push(a):
        if a in locked:
            return
        for b in neighbours(a):
            heapq.heappush(heap, (cost(a, b), ver[a], a, b))

    for a in range(n):
        if a in vf:
            push(a)
    while alive > target and heap:
        c, v, a, b = heapq.heappop(heap)
        if a not in vf or not vf[a] or ver[a] != v or b not in vf or not vf[b]:
            continue
        shared = [f for f in vf[a] if b in faces[f]]
        if not shared:
            continue

        common = neighbours(a) & neighbours(b)
        apex = set()
        for f in shared:
            apex.update(x for x in faces[f] if x != a and x != b)
        if common != apex:
            continue

        bad = False
        for f in vf[a]:
            if f in shared:
                continue
            t = faces[f]
            q = tuple(b if x == a else x for x in t)
            p0 = np.cross(pos[t[1]] - pos[t[0]], pos[t[2]] - pos[t[0]])
            p1 = np.cross(pos[q[1]] - pos[q[0]], pos[q[2]] - pos[q[0]])
            n0, n1 = np.linalg.norm(p0), np.linalg.norm(p1)
            if n1 < 1e-12 or n0 < 1e-12 or float(p0 @ p1) / (n0 * n1) < 0.3:
                bad = True
                break
        if bad:
            continue
        for f in shared:
            for x in faces[f]:
                vf[x].discard(f)
            del faces[f]
            alive -= 1
        for f in list(vf[a]):
            t = faces[f]
            faces[f] = tuple(b if x == a else x for x in t)
            vf[b].add(f)
        vf[a] = set()
        Q[b] = Q[b] + Q[a]
        ver[b] += 1
        for x in neighbours(b):
            ver[x] += 1
        for x in neighbours(b) | {b}:
            push(x)
    keep = sorted({x for t in faces.values() for x in t})
    idx = {v: k for k, v in enumerate(keep)}
    return keep, [tuple(idx[x] for x in t) for t in faces.values()]
