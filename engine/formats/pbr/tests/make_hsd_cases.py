r"""Makes the reference files for pbr_hsd_write_test: reads an XD model (PKX, game data: pass files from under build\)
with the Python reader (hsd_read), writes the neutral model as plain tokens (model_io.cpp reads them back) and the archive
the PYTHON writer (hsd_write.write_archive) makes from it.
Usage: python make_hsd_cases.py <out folder> <model.pkx>...      writes <name>.model.txt and <name>.arc.bin"""
import os, struct, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', '..', '..', 'tools', 'pbr_convert'))
import hsd_read, hsd_write


class Out:
    def __init__(self):
        self.t = []

    def i(self, v):
        self.t.append(str(int(v)))

    def f(self, v):
        self.t.append(struct.pack('>d', float(v)).hex())

    def fs(self, vs):
        for v in vs:
            self.f(v)

    def b(self, v):
        self.t.append('-' if v is None else 'x' + bytes(v).hex())

    def s(self, v):
        self.t.append(str(v))

    def opt_i(self, v):
        if v is None:
            self.t.append('N')
        else:
            self.t.append('Y'); self.i(v)

    def opt_fs(self, v):
        if v is None:
            self.t.append('N')
        else:
            self.t.append('Y'); self.fs(v)


def aobj(o, a):
    if a is None:
        o.s('N'); return
    o.s('Y'); o.i(a.flags); o.f(a.end); o.i(len(a.tracks))
    for t in a.tracks:
        o.i(t.kind); o.f(t.start)
        sf = getattr(t, 'src_format', None)
        if sf is None:
            o.s('N')
        else:
            o.s('Y'); o.i(sf[0]); o.i(sf[1])
        o.i(len(t.keys))
        for op, v, s, w in t.keys:
            assert w == int(w)
            o.i(op); o.f(v); o.f(s); o.i(w)


def dump(m, path):
    o = Out()
    o.i(len(m.joints))
    for j in m.joints:
        o.i(j.parent); o.opt_i(j.flags); o.fs(j.rot); o.fs(j.scale); o.fs(j.pos)
        o.opt_fs(j.inv_bind); o.i(1 if j.hidden else 0); o.i(getattr(j, 'billboard', 0))
    o.i(m.skeleton_root)
    o.i(len(m.meshes))
    for me in m.meshes:
        o.i(me.joint); o.opt_i(me.material); o.i(1 if me.rigid else 0); o.opt_i(me.pobj_flags)
        o.i(len(me.verts))
        for v in me.verts:
            o.fs(v.pos); o.opt_fs(v.nrm); o.opt_fs(v.uv)
            if v.color is None:
                o.s('N')
            else:
                o.s('Y'); o.i(len(v.color))
                for c in v.color:
                    o.i(c)
            if v.weights is None:
                o.s('N')
            else:
                o.s('Y'); o.i(len(v.weights))
                for j, w in v.weights:
                    o.i(j); o.f(w)
            if v.uvx is None:
                o.s('N')
            else:
                o.s('Y'); o.i(len(v.uvx))
                for x in v.uvx:
                    o.opt_fs(x)
        o.i(len(me.tris))
        for t in me.tris:
            o.i(t[0]); o.i(t[1]); o.i(t[2])
    o.i(len(m.materials))
    for ma in m.materials:
        o.s(getattr(ma, 'draw_class', 'opa'))
        o.i(ma.rendermode); o.fs(ma.ambient); o.fs(ma.diffuse); o.fs(ma.specular)
        o.f(ma.alpha); o.f(ma.shininess)
        o.i(len(ma.textures))
        for t in ma.textures:
            o.opt_i(t.image); o.opt_i(t.tlut); o.i(t.tex_id); o.i(t.src)
            o.fs(t.rot); o.fs(t.scale); o.fs(t.trans)
            o.i(t.wrap_s); o.i(t.wrap_t); o.i(t.repeat_s); o.i(t.repeat_t); o.i(t.flags); o.f(t.blending); o.i(t.mag_filter)
            o.b(t.lod); o.b(t.tev)
        o.b(ma.pe)
    o.i(len(m.images))
    for im in m.images:
        o.i(im.width); o.i(im.height); o.i(im.format); o.i(im.mipmap); o.f(im.min_lod); o.f(im.max_lod); o.b(im.data)
    o.i(len(m.tluts))
    for t in m.tluts:
        o.i(t.format); o.i(t.name); o.i(t.count); o.b(t.data)
    o.i(len(m.animations))
    for an in m.animations:
        o.i(len(an.joints))
        for k, a in an.joints.items():
            o.i(k); aobj(o, a)
        o.i(len(an.joint_flags))
        for k, v in an.joint_flags.items():
            o.i(k); o.i(v)
        o.i(len(an.materials))
        for k, lst in an.materials.items():
            o.i(k)
            if lst is None:
                o.s('N'); continue
            o.s('Y'); o.i(len(lst))
            for ma in lst:
                if ma is None:
                    o.s('N'); continue
                o.s('Y'); aobj(o, ma.aobj); o.i(len(ma.texanims))
                for ta in ma.texanims:
                    o.i(ta.tex_id); aobj(o, ta.aobj)
                    o.i(len(ta.images))
                    for x in ta.images:
                        o.i(x)
                    o.i(len(ta.tluts))
                    for x in ta.tluts:
                        o.i(x)
        if an.bounds is None:
            o.s('N')
        else:
            o.s('Y'); o.i(len(an.bounds))
            for box in an.bounds:
                o.fs(box)
    if m.bound_head is None:
        o.s('N')
    else:
        o.s('Y'); o.i(len(m.bound_head))
        for x in m.bound_head:
            o.i(x)
    text = ' '.join(o.t) + '\n'
    if path is None:
        return text
    with open(path, 'w') as f:
        f.write(text)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for p in sys.argv[2:]:
        name = os.path.basename(p).rsplit('.', 1)[0]
        pkx, model, warns = hsd_read.read_pkx(open(p, 'rb').read())
        dump(model, os.path.join(out, name + '.model.txt'))
        data, report = hsd_write.write_archive(model)
        open(os.path.join(out, name + '.arc.bin'), 'wb').write(data)
        print(name, len(data), report)


if __name__ == "__main__":
    main()
