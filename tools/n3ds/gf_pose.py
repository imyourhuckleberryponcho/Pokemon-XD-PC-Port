"""Poses of a Gen 6/7 model: sampling a GF motion into per-bone scale / rotation / translation per frame and
skinning a model with it (used for previews and for the converter's own checks). numpy only."""
import math
import numpy as np


def rot_mat(rx, ry, rz):
    """Rz * Ry * Rx (HSD's and the 3DS skeleton's Euler order)."""
    sx, cx, sy, cy, sz, cz = math.sin(rx), math.cos(rx), math.sin(ry), math.cos(ry), math.sin(rz), math.cos(rz)
    return np.array([[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
                     [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
                     [-sy, sx * cy, cx * cy]])


def axis_angle_mat(v):
    """The 3DS stores 'axis angle' rotation as the axis scaled by half the angle."""
    ang = float(np.linalg.norm(v)) * 2.0
    if ang <= 1e-12:
        return np.eye(3)
    a = np.asarray(v) / (ang / 2.0)
    x, y, z = a
    c, s = math.cos(ang), math.sin(ang)
    t = 1 - c
    return np.array([[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
                     [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
                     [t * x * z - s * y, t * y * z + s * x, t * z * z + c]])


def euler_from_mat(R):
    """Inverse of rot_mat (Rz Ry Rx) -> (rx, ry, rz)."""
    r20 = R[2, 0]
    if abs(r20) < 0.999999:
        ry = -math.asin(r20)
        rx = math.atan2(R[2, 1], R[2, 2])
        rz = math.atan2(R[1, 0], R[0, 0])
    else:
        ry = -math.copysign(math.pi / 2, r20)
        rz = 0.0
        rx = math.atan2(-R[1, 2], R[1, 1])
    return rx, ry, rz


class Skeleton:
    """A bone with flag bit 0 set (Maya's "segment scale compensate", the 3DS game's own rule) does not inherit its parent's
    scale: world = parent world * parent scale^-1 * (parent scale * T) * R (the child's own scale S last). The converted
    model (HSD) multiplies plain, so every pose that leaves this class (bind pose, sample_motion) is turned into plain locals
    (ssc_to_plain) that give the same world matrices. Without it a stretching tail (Marill, Lileep, Tentacruel, Hitmonlee...)
    multiplied its scales down the chain: models 40 times too big for a few frames."""

    def __init__(self, bones):
        self.bones = bones
        self.names = [b['name'] for b in bones]
        idx = {n: i for i, n in enumerate(self.names)}
        self.parent = [idx.get(b['parent'], -1) if b['parent'] != b['name'] else -1 for b in bones]
        self.ssc = [bool(b.get('flags', 0) & 1) and self.parent[i] >= 0 for i, b in enumerate(bones)]
        self.ssc_kids = np.array([i for i in range(len(bones)) if self.ssc[i]], int)
        self.ssc_par = np.array([self.parent[i] for i in self.ssc_kids], int)
        self.bind_s = np.array([b['scale'] for b in bones], float)
        self.bind_r = np.array([b['rot'] for b in bones], float)
        self.bind_t = np.array([b['pos'] for b in bones], float)
        self.raw_s, self.raw_r, self.raw_t = self.bind_s, self.bind_r, self.bind_t
        self.bind_s, self.bind_r, self.bind_t = self.ssc_to_plain(self.bind_s, self.bind_r, self.bind_t)
        self.bind_world = self.world(self.bind_s, self.bind_r, self.bind_t)
        self.inv_bind = np.linalg.inv(self.bind_world)

    def needs_ssc(self, s):
        """Does any compensating bone have a parent whose scale is not 1 (else plain and compensated poses are equal)?"""
        return len(self.ssc_kids) > 0 and bool((np.abs(s[self.ssc_par] - 1.0) > 1e-4).any())

    def ssc_to_plain(self, s, r, t):
        """Euler locals with segment scale compensation -> Euler locals of the plain hierarchy with the same world matrices
        (a rare shear, when a parent's scale is not uniform and the child is turned, is dropped: the bone keeps its columns' lengths)."""
        if not self.needs_ssc(s):
            return s, r, t





        n = len(self.bones)
        s2, r2, t2 = s.copy(), r.copy(), t.copy()
        Wt = np.empty((n, 4, 4))
        Wa = np.empty((n, 4, 4))

        def local4(sc_, r_, t_):
            L = np.eye(4)
            L[:3, :3] = rot_mat(*r_) * sc_[None, :]
            L[:3, 3] = t_
            return L

        def split(A):
            sc = np.linalg.norm(A, axis=0)
            sc = np.where(sc > 1e-9, sc, 1e-9)
            R = A / sc[None, :]
            if np.linalg.det(R) < 0:
                R[:, 0] = -R[:, 0]
                sc[0] = -sc[0]
            return sc, euler_from_mat(R)

        for i in range(n):
            p = self.parent[i]
            L = local4(s[i], r[i], t[i])
            if p < 0:
                Wt[i] = Wa[i] = L
                continue
            sp = s[p]
            if self.ssc[i] and np.abs(sp).min() > 0.05:
                Lt = np.eye(4)
                Lt[:3, 3] = sp * t[i]
                Lr = np.eye(4)
                Lr[:3, :3] = L[:3, :3]
                Wt[i] = Wt[p] @ np.diag([1 / sp[0], 1 / sp[1], 1 / sp[2], 1.0]) @ Lt @ Lr
                M = (rot_mat(*r[i]) * s[i][None, :]) / sp[:, None]
                cf = split(M)
                ci = t[i]
                try:
                    D = np.linalg.inv(Wa[p]) @ Wt[i]
                    ex = split(D[:3, :3])
                    ok = bool(np.isfinite(D).all()) and np.abs(ex[0]).max() <= 2.0 * max(np.abs(cf[0]).max(), 1.0)
                except np.linalg.LinAlgError:
                    ok = False
                if ok:
                    s2[i], r2[i], t2[i] = ex[0], ex[1], D[:3, 3]
                else:
                    s2[i], r2[i], t2[i] = cf[0], cf[1], ci
            else:
                Wt[i] = Wt[p] @ L
            Wa[i] = Wa[p] @ local4(s2[i], r2[i], t2[i])
        return s2, r2, t2

    def local(self, s, r, t, axis=None):
        n = len(self.bones)
        L = np.tile(np.eye(4), (n, 1, 1))
        for i in range(n):
            R = axis_angle_mat(r[i]) if (axis is not None and axis[i]) else rot_mat(*r[i])
            L[i, :3, :3] = R * s[i][None, :]
            L[i, :3, 3] = t[i]
        return L

    def world(self, s, r, t, axis=None):
        L = self.local(s, r, t, axis)
        W = np.empty_like(L)
        for i in range(len(self.bones)):
            p = self.parent[i]
            W[i] = L[i] if p < 0 else W[p] @ L[i]
        return W


def sample_motion(sk, mo, frame):
    """-> (s, r, t, axis): per bone arrays for one frame of a parsed motion (bind values where a channel has none)."""
    from gf_read import herp
    s, r, t = sk.raw_s.copy(), sk.raw_r.copy(), sk.raw_t.copy()
    axis = np.zeros(len(sk.names), bool)
    for bn, d in mo['bones'].items():
        try:
            i = sk.names.index(bn)
        except ValueError:
            continue
        axis[i] = d['axis']
        if d['axis']:


            r[i] = 0.0
        for k in range(9):
            v = herp(d['ch'][k], frame)
            if v is None:
                continue
            (s, r, t)[k // 3][i, k % 3] = v
    if sk.needs_ssc(s):
        for i in range(len(r)):
            if axis[i]:
                r[i] = euler_from_mat(axis_angle_mat(r[i]))
        axis = np.zeros(len(sk.names), bool)
        s, r, t = sk.ssc_to_plain(s, r, t)
    return s, r, t, axis


def skin_points(sk, W, bone_idx_table, verts):
    """Skinned positions of one submesh. verts = dict from gf_read.decode_verts; bone_idx_table = the submesh's list."""
    pos = verts['pos']
    n = len(pos)
    if 'bidx' not in verts:
        b = bone_idx_table[0] if bone_idx_table else 0
        M = W[b] @ sk.inv_bind[b]
        return pos @ M[:3, :3].T + M[:3, 3]
    out = np.zeros((n, 3))
    bi = verts['bidx'].astype(int)
    bw = verts['bw']
    for k in range(bi.shape[1]):
        w = bw[:, k] if k < bw.shape[1] else np.zeros(n)
        if not w.any():
            continue
        tab = np.array(list(bone_idx_table) + [0] * 256)
        bones = tab[bi[:, k]]
        for b in np.unique(bones):
            sel = bones == b
            M = W[b] @ sk.inv_bind[b]
            out[sel] += (pos[sel] @ M[:3, :3].T + M[:3, 3]) * w[sel, None]
    return out
