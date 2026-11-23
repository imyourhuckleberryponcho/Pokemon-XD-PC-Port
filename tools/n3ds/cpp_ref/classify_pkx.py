import os,struct,sys,json,collections,math
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), 'tools', 'pbr_convert'))
sys.path.insert(0, HERE)
import hsd_read, compare_pkx as C



pyd, cd = sys.argv[1], sys.argv[2]
def unfsys(b):
    h=struct.unpack_from('>I',b,0x60)[0]; off,size=struct.unpack_from('>II',b,h+4); return b[off:off+size]
def load(b):
    pk,m,w=hsd_read.read_pkx(bytearray(b),meshes=True); return pk,m
def ispi(v): return abs(abs(v)/math.pi-round(abs(v)/math.pi))<2e-3 and round(abs(v)/math.pi)>=1
res=collections.OrderedDict(); cats=collections.Counter()
names=sys.argv[3:] or sorted(f[:-4] for f in os.listdir(pyd) if f.endswith('.pkx'))
for pn in names:
    pp=os.path.join(pyd,pn+'.pkx')
    cp=os.path.join(cd,pn+'.pkx')
    if os.path.exists(cp): b=open(cp,'rb').read()
    elif os.path.exists(os.path.join(cd,'files','pkx_'+pn+'.fsys')): b=unfsys(open(os.path.join(cd,'files','pkx_'+pn+'.fsys'),'rb').read())
    else: cats['no cpp file']+=1; continue
    if not os.path.exists(pp): cats['no python file']+=1; continue
    a=open(pp,'rb').read()
    st,msg=C.compare(a,b,1e-5)
    if st!='DIFFERENT': cats[st]+=1; continue
    A_pk,A=load(a); B_pk,B=load(b)
    why=[]
    if A_pk.sections!=B_pk.sections: why.append('sections')
    if len(A.joints)!=len(B.joints) or len(A.meshes)!=len(B.meshes) or len(A.animations)!=len(B.animations): why.append('counts')
    else:
        if any(x.data!=y.data for x,y in zip(A.images,B.images)): why.append('IMAGES')
        tie=0; badmesh=0; tie2=0
        for x,y in zip(A.meshes,B.meshes):
            if len(x.verts)!=len(y.verts) or x.tris!=y.tris: tie+=1; continue
            nbv=0; twin=0
            for u,v in zip(x.verts,y.verts):
                if u.weights!=v.weights or max(abs(p-q) for p,q in zip(u.pos,v.pos))>1e-4 or (u.uv and v.uv and max(abs(p-q) for p,q in zip(u.uv,v.uv))>1e-4):
                    nbv+=1
                    if u.weights==v.weights and any(max(abs(abs(p)-abs(q)) for p,q in zip(u.pos,v.pos))<1e-4 for _ in [0]): twin+=1
            if nbv and ((nbv<=4 and nbv*100<=len(x.verts)) or (twin==nbv and nbv*20<=len(x.verts))): tie2+=1
            elif nbv: badmesh+=1
        if tie: why.append('decimation-tie x%d'%tie)
        if tie2: why.append('decimation-tie (same triangles, other kept vertex) x%d'%tie2)
        if badmesh: why.append('MESH-DATA x%d'%badmesh)
        pi=0; trk=0; bnd=0
        for x,y in zip(A.animations,B.animations):
            if x.bounds!=y.bounds:
                if x.bounds and y.bounds and len(x.bounds)==len(y.bounds) and max(abs(p-q) for r,s in zip(x.bounds,y.bounds) for p,q in zip(r,s))<1e-3: pass
                else: bnd+=1
            for j in set(x.joints)|set(y.joints):
                p_=x.joints.get(j); q=y.joints.get(j)
                if p_ is None or q is None: trk+=1; continue
                if len(p_.tracks)!=len(q.tracks): trk+=1; continue
                for t,u in zip(p_.tracks,q.tracks):
                    if t.kind!=u.kind: trk+=1
                    elif len(t.keys)!=len(u.keys):
                        vals=[k[1] for k in t.keys]+[k[1] for k in u.keys]
                        if any(ispi(v) for v in vals): pi+=1
                        else: trk+=1
                    else:
                        if max(abs(k[1]-l[1]) for k,l in zip(t.keys,u.keys))>1e-3 or [k[3] for k in t.keys]!=[l[3] for l in u.keys]:
                            vals=[k[1] for k in t.keys]
                            if any(ispi(v) for v in vals): pi+=1
                            else: trk+=1
        if pi: why.append('angle-pi x%d'%pi)
        if trk: why.append('TRACKS x%d'%trk)
        if bnd: why.append('BOUNDS x%d'%bnd)
    key='UNEXPLAINED' if any(w.split(' ')[0].upper()==w.split(' ')[0] and w.split(' ')[0] in ('IMAGES','MESH-DATA','TRACKS','BOUNDS','sections','counts') for w in why) else 'explained'
    cats[key]+=1
    res[pn]=(key,why)
for k,v in res.items():
    if v[0]=='UNEXPLAINED': print(k,v[1])
print(dict(cats))
json.dump(res,open('classify_pkx.json','w'))
