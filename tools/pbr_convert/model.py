"""The converter's neutral in-memory model: what a reader (XD's HSD archive, Battle Revolution's sdr) fills in
and what the HSD writer turns into bytes. No file layout lives here, only the model and a little matrix maths.

Conventions
- Matrices are 3x4, row-major lists of 12 floats, column vectors (p' = M p), as on the GameCube.
- A joint's local matrix is T * Rz * Ry * Rx * S (HSD's order; rotations are Euler angles in radians).
- `Model.skeleton_root` is the joint whose space the skin lives in: every skinned vertex position is given in
  that joint's space in the bind pose, and every bound joint's `inv_bind` maps that space into the joint's own
  (bind pose).  skinned(t) = sum over weights of  World_j(t) * inv_bind_j * position.
- A rigid mesh (Mesh.rigid) has no weights; its positions are in its owner joint's own space.
- Animation tracks keep HSD's key model (it is the richer of the two formats): a list of keys
  (op, value, slope, wait) where `op` says how the stretch from this key to the next is filled in and `wait`
  is that stretch's length in frames.
"""
import math

# key ops (HSD's numbers)
OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_SLP, OP_KEY = 1, 2, 3, 4, 5, 6
# joint track kinds (HSD's numbers)
J_ROTX, J_ROTY, J_ROTZ, J_PATH, J_TRAX, J_TRAY, J_TRAZ, J_SCAX, J_SCAY, J_SCAZ = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10

IDENT = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0]


def mat_mul(a, b):
    r = [0.0] * 12
    for i in range(3):
        a0, a1, a2, a3 = a[i * 4], a[i * 4 + 1], a[i * 4 + 2], a[i * 4 + 3]
        r[i * 4] = a0 * b[0] + a1 * b[4] + a2 * b[8]
        r[i * 4 + 1] = a0 * b[1] + a1 * b[5] + a2 * b[9]
        r[i * 4 + 2] = a0 * b[2] + a1 * b[6] + a2 * b[10]
        r[i * 4 + 3] = a0 * b[3] + a1 * b[7] + a2 * b[11] + a3
    return r


def mat_inv(m):
    a, b, c, d, e, f, g, h, i = m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1e-30:
        raise ValueError('matrix cannot be inverted')
    k = 1.0 / det
    r = [(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, 0,
         (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k, 0,
         (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k, 0]
    tx, ty, tz = m[3], m[7], m[11]
    r[3] = -(r[0] * tx + r[1] * ty + r[2] * tz)
    r[7] = -(r[4] * tx + r[5] * ty + r[6] * tz)
    r[11] = -(r[8] * tx + r[9] * ty + r[10] * tz)
    return r


def mat_point(m, p):
    return (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
            m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11])


def mat_vec(m, p):
    return (m[0] * p[0] + m[1] * p[1] + m[2] * p[2],
            m[4] * p[0] + m[5] * p[1] + m[6] * p[2],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2])


def mat_normal(m, n):
    """A normal through m (inverse transpose of the 3x3), normalised."""
    inv = mat_inv(m)
    x = inv[0] * n[0] + inv[4] * n[1] + inv[8] * n[2]
    y = inv[1] * n[0] + inv[5] * n[1] + inv[9] * n[2]
    z = inv[2] * n[0] + inv[6] * n[1] + inv[10] * n[2]
    l = math.sqrt(x * x + y * y + z * z)
    return (x / l, y / l, z / l) if l > 1e-20 else (0.0, 1.0, 0.0)


def mat_rot(rx, ry, rz):
    """Rz * Ry * Rx as a 3x4."""
    sx, cx, sy, cy, sz, cz = math.sin(rx), math.cos(rx), math.sin(ry), math.cos(ry), math.sin(rz), math.cos(rz)
    return [cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, 0,
            cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, 0,
            -sy, sx * cy, cx * cy, 0]


def mat_srt(scale, rot, pos):
    m = mat_rot(*rot)
    for r in range(3):
        m[r * 4] *= scale[0]
        m[r * 4 + 1] *= scale[1]
        m[r * 4 + 2] *= scale[2]
    m[3], m[7], m[11] = pos
    return m


def mat_decompose(m):
    """(scale, Euler rotation, translation) of a matrix made by mat_srt (no shear handled; a mirrored matrix
    gets a negative x scale)."""
    sx = math.sqrt(m[0] ** 2 + m[4] ** 2 + m[8] ** 2)
    sy = math.sqrt(m[1] ** 2 + m[5] ** 2 + m[9] ** 2)
    sz = math.sqrt(m[2] ** 2 + m[6] ** 2 + m[10] ** 2)
    det = (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]))
    if det < 0:
        sx = -sx
    r00, r10, r20 = (m[0] / sx, m[4] / sx, m[8] / sx) if sx else (1, 0, 0)
    r21, r22 = (m[9] / sy if sy else 0), (m[10] / sz if sz else 1)
    r11, r12 = (m[5] / sy if sy else 1), (m[6] / sz if sz else 0)
    if abs(r20) < 0.999999:
        ry = -math.asin(r20)
        rx = math.atan2(r21, r22)
        rz = math.atan2(r10, r00)
    else:
        ry = -math.copysign(math.pi / 2, r20)
        rz = 0.0
        rx = math.atan2(-r12, r11)
    return (sx, sy, sz), (rx, ry, rz), (m[3], m[7], m[11])


class Joint:
    def __init__(self):
        self.name = None
        self.parent = -1
        self.flags = None        # HSD joint flags when read from an HSD file (the writer works them out if None)
        self.rot = (0.0, 0.0, 0.0)
        self.scale = (1.0, 1.0, 1.0)
        self.pos = (0.0, 0.0, 0.0)
        self.inv_bind = None     # 3x4 or None
        self.hidden = False
        self.note = ''           # where it came from (PBR node kind...), not written


class Vertex:
    __slots__ = ('pos', 'nrm', 'uv', 'color', 'weights', 'uvx')

    def __init__(self, pos, nrm=None, uv=None, color=None, weights=None, uvx=None):
        self.pos, self.nrm, self.uv, self.color, self.weights = pos, nrm, uv, color, weights
        self.uvx = uvx           # more coordinate sets (GX TEX1, TEX2): tuple of (u, v) or None each, or None

    def key(self):
        return (self.pos, self.nrm, self.uv, self.color, self.weights, self.uvx)


class Mesh:
    def __init__(self):
        self.joint = 0           # owner joint (index)
        self.material = 0        # index into Model.materials
        self.rigid = False
        self.verts = []          # Vertex; weights = tuple of (joint index, weight), None when rigid
        self.tris = []           # (a, b, c) into verts, GX winding
        self.pobj_flags = None   # HSD polygon-object flags as read (culling bits...), None = writer's default
        self.name = ''


class Image:
    def __init__(self):
        self.width = self.height = 0
        self.format = 0          # GX texture format number
        self.mipmap = 0
        self.min_lod = self.max_lod = 0.0
        self.data = b''


class Tlut:
    def __init__(self):
        self.format = 0
        self.name = 0
        self.count = 0
        self.data = b''


class Texture:
    """One texture layer of a material (HSD's TObj)."""
    def __init__(self):
        self.image = None        # index into Model.images
        self.tlut = None         # index into Model.tluts
        self.tex_id = 0
        self.src = 4             # GX_TG_TEX0
        self.rot = (0.0, 0.0, 0.0)
        self.scale = (1.0, 1.0, 1.0)
        self.trans = (0.0, 0.0, 0.0)
        self.wrap_s = self.wrap_t = 1    # 0 clamp, 1 repeat, 2 mirror
        self.repeat_s = self.repeat_t = 1
        self.flags = 0x00030010          # UV coordinates, diffuse light map, colour "blend" (XD's usual)
        self.blending = 1.0
        self.mag_filter = 1
        self.lod = None          # raw 16 bytes or None
        self.tev = None          # raw 32 bytes or None


class Material:
    def __init__(self):
        self.name = ''
        self.rendermode = 0x2015
        self.ambient = (0x80, 0x80, 0x80, 0xFF)
        self.diffuse = (0xFF, 0xFF, 0xFF, 0xFF)
        self.specular = (0xFF, 0xFF, 0xFF, 0xFF)
        self.alpha = 1.0
        self.shininess = 50.0
        self.textures = []       # Texture
        self.pe = None           # raw 12 bytes or None


class Track:
    def __init__(self, kind, keys=None, start=0.0):
        self.kind = kind         # HSD track number (J_* for joints; material / texture numbers elsewhere)
        self.start = start
        self.keys = keys or []   # (op, value, slope, wait)


class AnimObj:
    """HSD's AObj: a set of tracks with an end frame."""
    def __init__(self):
        self.flags = 0
        self.end = 0.0
        self.tracks = []


class TexAnim:
    def __init__(self):
        self.tex_id = 0
        self.aobj = None
        self.images = []         # indices into Model.images
        self.tluts = []


class MatAnim:
    def __init__(self):
        self.aobj = None
        self.texanims = []
        self.has_render_anim = False


class Animation:
    def __init__(self):
        self.name = ''
        self.joints = {}         # joint index -> AnimObj
        self.joint_flags = {}    # joint index -> AnimJoint flags as read
        self.materials = {}      # joint index -> list (one per display object of the joint) of MatAnim or None
        self.has_material_tree = False
        self.bounds = None       # list of (minx, miny, minz, maxx, maxy, maxz), one per frame, or None

    def end(self):
        e = 0.0
        for a in self.joints.values():
            e = max(e, a.end)
        return e


class Model:
    def __init__(self):
        self.joints = []         # depth-first order = HSD's joint numbering
        self.skeleton_root = 0
        self.meshes = []         # in drawing order per joint
        self.materials = []
        self.images = []
        self.tluts = []
        self.animations = []
        self.bound_head = None   # the two 16-bit numbers before each animation's boxes, as read

    def children(self, j):
        return [i for i, x in enumerate(self.joints) if x.parent == j]

    def local(self, j):
        x = self.joints[j]
        return mat_srt(x.scale, x.rot, x.pos)

    def world_static(self):
        out = []
        for i, x in enumerate(self.joints):
            l = mat_srt(x.scale, x.rot, x.pos)
            out.append(l if x.parent < 0 else mat_mul(out[x.parent], l))
        return out


# ---- animation evaluation (used to check the converter's own output and to make bound boxes) ----

def _hermite(t, dur, p0, p1, d0, d1):
    s = t / dur
    s2, s3 = s * s, s * s * s
    return ((2 * s3 - 3 * s2 + 1) * p0 + (-2 * s3 + 3 * s2) * p1 + (s3 - 2 * s2 + s) * dur * d0 + (s3 - s2) * dur * d1)


def track_segments(track):
    """A track as stretches (start, length, op, p0, p1, d0, d1), by HSD's rules (fobj.cpp): the stretch between
    two keys is filled in by the op read last before the second key (the first key's own, or SLP if a slope
    record sits between them); SLP only replaces the slope the stretch starts from."""
    segs = []
    p1 = d0 = d1 = 0.0
    last_op = 0
    pos = 0.0
    wait = 0.0
    first = True
    for op, v, s, w in track.keys:
        if op == OP_SLP:
            d0, d1 = d1, s
            last_op = OP_SLP
            continue
        intrp = last_op
        p0 = p1
        p1 = v
        if op == OP_SPL:
            d0, d1 = d1, s
        elif op == OP_SPL0 or intrp != OP_SLP:
            d0, d1 = d1, 0.0
        if not first:
            segs.append((pos, wait, intrp, p0, p1, d0, d1))
            pos += wait
        first = False
        wait = w
        last_op = op
    segs.append((pos, 0.0, 0, p1, p1, 0.0, 0.0))
    return segs


def track_value(track, frame):
    segs = getattr(track, '_segs', None)
    if segs is None:
        segs = track._segs = track_segments(track)
    t = frame - track.start
    for start, dur, op, p0, p1, d0, d1 in segs:
        if t < start + dur:
            tt = t - start
            if tt <= 0:
                return p0
            if op in (OP_CON, OP_KEY):
                return p0
            if op == OP_LIN:
                return p0 + (p1 - p0) * tt / dur
            return _hermite(tt, dur, p0, p1, d0, d1)
    return segs[-1][4]


def pose_at(model, anim, frame):
    """World matrices of every joint at a frame of an animation (None = the file's static pose)."""
    out = []
    for i, x in enumerate(model.joints):
        s, r, p = list(x.scale), list(x.rot), list(x.pos)
        a = anim.joints.get(i) if anim else None
        if a:
            f = min(frame, a.end)
            for tr in a.tracks:
                k = tr.kind
                if not tr.keys:
                    continue
                v = track_value(tr, f)
                if J_ROTX <= k <= J_ROTZ:
                    r[k - J_ROTX] = v
                elif J_TRAX <= k <= J_TRAZ:
                    p[k - J_TRAX] = v
                elif J_SCAX <= k <= J_SCAZ:
                    s[k - J_SCAX] = v
        l = mat_srt(s, r, p)
        out.append(l if x.parent < 0 else mat_mul(out[x.parent], l))
    return out


def skin_positions(model, world):
    """Every mesh's vertex positions in model space for the given joint matrices: list (per mesh) of lists."""
    root_inv = None
    skin = {}
    res = []
    for me in model.meshes:
        if model.joints[me.joint].hidden:
            res.append([])
            continue
        if me.rigid:
            m = world[me.joint]
            res.append([mat_point(m, v.pos) for v in me.verts])
            continue
        pts = []
        for v in me.verts:
            x = y = z = 0.0
            for j, w in v.weights:
                m = skin.get(j)
                if m is None:
                    m = skin[j] = mat_mul(world[j], model.joints[j].inv_bind)
                px, py, pz = mat_point(m, v.pos)
                x += px * w
                y += py * w
                z += pz * w
            pts.append((x, y, z))
        res.append(pts)
    return res
