"""A Battle Revolution model posed by its own rules (numpy): the reference the converted file is checked against,
and the source of the matrices the converter writes.

Rules (each checked on the disc's files; see docs/BATTLE_REVOLUTION.md "The converter, second pass"):
- joint (kind 2):   local = T * [1 / parent's own scale, when the joint compensates] * JointOrient * Rz Ry Rx * S
- any other node:   local = Tr(T + v2) * Rz Ry Rx * Tr(v1) * S * Tr(v0)   (v0, v1, v2 = the node's pivot block:
                    v0 = minus the pivot, v2 = the pivot with Maya's keep-in-place shift already added, v1 = 0 in
                    every file of the disc)
- skinned vertex:   sum of weight * World(joint) * InverseBind(joint) * position (positions are model space)
- rigid mesh:       World(its node) * position
- a node's "visibility" curve below 0.5 switches the node and everything under it off.
The static translate / rotate / scale of a node is NOT the bind pose (the inverse bind matrices are).
"""
import numpy as np

FPS = 30.0


def rot_matrices(r):
    """Euler angles [..., 3] -> rotation matrices [..., 3, 3] (Rz * Ry * Rx)."""
    sx, cx = np.sin(r[..., 0]), np.cos(r[..., 0])
    sy, cy = np.sin(r[..., 1]), np.cos(r[..., 1])
    sz, cz = np.sin(r[..., 2]), np.cos(r[..., 2])
    m = np.empty(r.shape[:-1] + (3, 3))
    m[..., 0, 0] = cy * cz
    m[..., 0, 1] = sx * sy * cz - cx * sz
    m[..., 0, 2] = cx * sy * cz + sx * sz
    m[..., 1, 0] = cy * sz
    m[..., 1, 1] = sx * sy * sz + cx * cz
    m[..., 1, 2] = cx * sy * sz - sx * cz
    m[..., 2, 0] = -sy
    m[..., 2, 1] = sx * cy
    m[..., 2, 2] = cx * cy
    return m


def euler_of(m):
    """Rotation matrices [..., 3, 3] -> Euler angles [..., 3] of Rz * Ry * Rx."""
    r20 = np.clip(m[..., 2, 0], -1.0, 1.0)
    ry = -np.arcsin(r20)
    plain = np.abs(r20) < 0.999999
    rx = np.where(plain, np.arctan2(m[..., 2, 1], m[..., 2, 2]), np.arctan2(-m[..., 1, 2], m[..., 1, 1]))
    rz = np.where(plain, np.arctan2(m[..., 1, 0], m[..., 0, 0]), 0.0)
    return np.stack([rx, ry, rz], axis=-1)


def compose(s, r, t):
    """scale, Euler, translate [..., 3] -> [..., 4, 4]."""
    m = np.zeros(s.shape[:-1] + (4, 4))
    m[..., :3, :3] = rot_matrices(r) * s[..., None, :]
    m[..., :3, 3] = t
    m[..., 3, 3] = 1.0
    return m


class Pose:
    """One action sampled at whole frames: local 4x4 matrices [frames, nodes], rotation-only part, the scales the
    file gave, and which nodes are switched on."""
    pass


class Eval:
    def __init__(self, sdr, compensate='joint8_under_joint'):
        self.sdr = sdr
        nodes = sdr.nodes
        self.n = len(nodes)
        self.parent = [x.parent for x in nodes]
        self.is_joint = np.array([x.kind == 2 for x in nodes])
        self.orient = rot_matrices(np.array([x.orient for x in nodes], dtype=float))
        self.comp = np.array([self._compensates(x, compensate, nodes[x.parent] if x.parent >= 0 else None) for x in nodes])
        piv = np.zeros((self.n, 9))
        for i, x in enumerate(nodes):
            if x.pivots and x.kind != 2:
                piv[i] = x.pivots[:9]
        self.piv = piv
        self.static = (np.array([x.pos for x in nodes], dtype=float), np.array([x.rot for x in nodes], dtype=float),
                       np.array([x.scale for x in nodes], dtype=float))
        self.inv_bind = np.zeros((self.n, 4, 4))
        self.inv_bind[:] = np.eye(4)
        for i, x in enumerate(nodes):
            if x.inv_bind is not None:
                self.inv_bind[i, :3, :] = np.array(x.inv_bind).reshape(3, 4)
        # meshes that are drawn (kind 3)
        self.meshes = []
        for i, x in enumerate(nodes):
            if x.kind != 3 or x.mesh is None or x.name.startswith('col_'):   # col_: touch boxes, never drawn
                continue
            me = x.mesh
            pos = np.array([v[0] for v in me['verts']], dtype=float).reshape(-1, 3)
            if me['weights'] is None:
                self.meshes.append((i, pos, None))
                continue
            terms = {}
            for vi, w in enumerate(me['weights']):
                for num, amount in w.items():
                    if amount > 0:
                        terms.setdefault(sdr.by_number[num], []).append((vi, amount))
            self.meshes.append((i, pos, terms))

    @staticmethod
    def _compensates(x, rule, parent=None):
        if x.kind != 2:
            return False
        if rule == 'joint8_under_joint':
            return bool(x.jflags & 8) and parent is not None and parent.kind == 2
        if rule == 'under_joint':
            return parent is not None and parent.kind == 2
        if rule == 'not_joint8_under_joint':
            return not (x.jflags & 8) and parent is not None and parent.kind == 2
        if rule == 'never':
            return False
        if rule == 'always':
            return True
        if rule == 'node8':
            return bool(x.flags & 8)
        return bool(x.jflags & 8)

    def frames_of(self, act):
        return max(int(round(self.sdr.actions[act][3] * FPS)), 1)

    def sample(self, act, frames=None):
        """Pose of action `act` (None = the static values) at frames 0..N."""
        nodes = self.sdr.nodes
        if act is None:
            nf = 1
        else:
            nf = (self.frames_of(act) if frames is None else frames) + 1
        pos = np.repeat(self.static[0][None], nf, axis=0)
        rot = np.repeat(self.static[1][None], nf, axis=0)
        sca = np.repeat(self.static[2][None], nf, axis=0)
        vis = np.ones((nf, self.n), dtype=bool)
        animated = np.zeros(self.n, dtype=bool)
        if act is not None:
            times = [f / FPS for f in range(nf)]
            for i, x in enumerate(nodes):
                curves = x.anim.get(act)
                if not curves:
                    continue
                for c in curves:
                    if not c.values:
                        continue
                    if c.what == 3:
                        vis[:, i] = np.array([c.at(t) for t in times]) >= 0.5
                    elif c.what in (0, 1, 2) and 1 <= c.axis <= 3:
                        (pos, rot, sca)[c.what][:, i, c.axis - 1] = [c.at(t) for t in times]
                        animated[i] = True
        p = Pose()
        p.frames = nf
        p.scale = sca
        p.animated = animated
        rm = rot_matrices(rot)
        rm = np.where(self.is_joint[None, :, None, None], np.einsum('nij,fnjk->fnik', self.orient, rm), rm)
        p.rot_only = rm
        m = np.zeros((nf, self.n, 4, 4))
        m[..., 3, 3] = 1.0
        m3 = rm * sca[:, :, None, :]
        t = pos.copy()
        # pivots (non-joints): Tr(T + v2) * R * Tr(v1) * S * Tr(v0)
        v0, v1, v2 = self.piv[:, 0:3], self.piv[:, 3:6], self.piv[:, 6:9]
        t = t + v2[None] + np.einsum('fnij,nj->fni', rm, v1) + np.einsum('fnij,nj->fni', m3, v0)
        # scale compensation (joints): 1 / the parent's own scale in front
        p.compensated = np.zeros(self.n, dtype=bool)
        for i in range(self.n):
            if self.comp[i] and self.parent[i] >= 0:
                ps = sca[:, self.parent[i], :]
                if np.any(np.abs(ps - 1.0) > 1e-5):
                    safe = np.where(np.abs(ps) < 1e-8, 1.0, ps)
                    m3[:, i] = m3[:, i] / safe[:, :, None]
                    p.compensated[i] = True
        m[..., :3, :3] = m3
        m[..., :3, 3] = t
        p.local = m
        eff = vis.copy()
        for i in range(self.n):
            if self.parent[i] >= 0:
                eff[:, i] &= eff[:, self.parent[i]]
        p.visible = eff
        p.own_visible = vis
        return p

    def world(self, local):
        """local [frames, nodes, 4, 4] -> world (parents come before children in the node list)."""
        w = np.empty_like(local)
        for i in range(self.n):
            pa = self.parent[i]
            w[:, i] = local[:, i] if pa < 0 else w[:, pa] @ local[:, i]
        return w

    def points(self, world, visible=None, node_map=None):
        """Drawn vertex positions per mesh for world matrices [frames, nodes, 4, 4]: list of (node, [frames, verts, 3]).
        Meshes switched off in a frame give NaN there."""
        out = []
        for node, pos, terms in self.meshes:
            hp = np.concatenate([pos, np.ones((len(pos), 1))], axis=1)
            if terms is None:
                pts = np.einsum('fij,vj->fvi', world[:, node, :3, :], hp)
            else:
                pts = np.zeros((world.shape[0], len(pos), 3))
                for j, lst in terms.items():
                    vi = np.array([a for a, _ in lst])
                    wt = np.array([b for _, b in lst])
                    sk = world[:, j] @ self.inv_bind[j]
                    pts[:, vi] += np.einsum('fij,vj->fvi', sk[:, :3, :], hp[vi]) * wt[None, :, None]
            if visible is not None:
                pts = np.where(visible[:, node][:, None, None], pts, np.nan)
            out.append((node, pts))
        return out


def boxes(point_sets):
    """[(node, [frames, verts, 3])] -> [frames, 6] (min xyz, max xyz) over all meshes, ignoring switched-off ones."""
    allp = np.concatenate([p for _, p in point_sets], axis=1)
    with np.errstate(all='ignore'):
        lo = np.nanmin(allp, axis=1)
        hi = np.nanmax(allp, axis=1)
    return np.nan_to_num(np.concatenate([lo, hi], axis=1))


def continuous_euler(rm):
    """Rotation matrices [frames, nodes, 3, 3] -> Euler angles that never jump between frames: of the two angle
    triples that give one rotation (and their full turns) the one nearest to the frame before is taken."""
    e = euler_of(rm)
    out = np.empty_like(e)
    out[0] = e[0]
    two = 2 * np.pi
    for f in range(1, e.shape[0]):
        prev = out[f - 1]
        a = e[f]
        b = np.stack([a[..., 0] + np.pi, np.pi - a[..., 1], a[..., 2] + np.pi], axis=-1)
        a = a - np.round((a - prev) / two) * two
        b = b - np.round((b - prev) / two) * two
        da = np.abs(a - prev).sum(axis=-1)
        db = np.abs(b - prev).sum(axis=-1)
        out[f] = np.where((db < da)[..., None], b, a)
    return out


def decompose(pose, ev, file_rot=None):
    """A sampled pose -> (scale, Euler, translate) [frames, nodes, 3] for HSD joints without scale compensation
    ("classical" joints: the game multiplies T * R * S down the tree).  Where a node has no compensation the
    scale is the file's own (exact); where it has, the 3x3 is taken apart (a shear is lost)."""
    local = pose.local
    t = local[..., :3, 3].copy()
    r = continuous_euler(pose.rot_only)
    s = pose.scale.copy()
    for i in np.nonzero(pose.compensated)[0]:
        m = local[:, i, :3, :3]
        sc = np.linalg.norm(m, axis=1)
        det = np.linalg.det(m)
        ok = np.all(sc > 1e-8, axis=1)
        sc2 = sc.copy()
        sc2[:, 0] = np.where(det < 0, -sc[:, 0], sc[:, 0])
        rr = m / np.where(sc2 == 0, 1.0, sc2)[:, None, :]
        r[:, i] = np.where(ok[:, None], continuous_euler(rr[:, None])[:, 0], r[:, i])
        s[:, i] = np.where(ok[:, None], sc2, s[:, i])
    return s, r, t
