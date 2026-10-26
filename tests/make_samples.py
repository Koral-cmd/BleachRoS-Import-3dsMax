# Синтетические файлы TamEngine для проверки читателей (не данные игры).
import struct, zlib, math, os, sys
out = sys.argv[1] if len(sys.argv) > 1 else "samples"
os.makedirs(out, exist_ok=True)

def crc_table():
    t=[]
    for i in range(256):
        c=i
        for _ in range(8): c=(c>>1)^0xEDB88320 if c&1 else c>>1
        t.append(c)
    return t
T=crc_table()
def tcrc(s):
    b=s.encode(); h=len(b)
    for x in b: h=(h>>8)^T[(x^h)&0xFF]
    return h&0xFFFFFFFF

def pzze(fmt, data):
    comp=zlib.compress(data)
    return b"PZZE"+fmt.encode()[:4].ljust(4,b"\0")+struct.pack("<QQ",len(data),24)+comp

def align(b, a=16):
    while len(b)%a: b+=b"\0"
    return b

def rows_from(m):  # 4x4 row-major list
    return struct.pack("<16f", *m)

def mat_mul(a,b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]
def trans(x,y,z): return [1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1]
def rotz(a):
    c,s=math.cos(a),math.sin(a); return [c,s,0,0,-s,c,0,0,0,0,1,0,0,0,0,1]
def inv_affine(m):
    # rotation orthonormal
    R=[[m[r*4+c] for c in range(3)] for r in range(3)]
    Rt=[[R[c][r] for c in range(3)] for r in range(3)]
    t=m[12:15]
    nt=[-sum(t[k]*Rt[k][c] for k in range(3)) for c in range(3)]
    return [Rt[0][0],Rt[0][1],Rt[0][2],0,Rt[1][0],Rt[1][1],Rt[1][2],0,Rt[2][0],Rt[2][1],Rt[2][2],0,nt[0],nt[1],nt[2],1]

def make_tmd2(name, bones, verts, tris, models, materials, textures, tables, flags):
    """bones: [(name, parent, worldMatrix)]; verts: list of dicts; tris: list of (a,b,c);
    models: [(name, [(mat, table, triStart, triCount)])]; materials: [(shader, [texIdx], [params])]"""
    names=b""
    def addname(s):
        nonlocal names
        o=len(names); names+=s.encode()+b"\0"; return o
    # vertex buffer
    vb=b""
    for v in verts:
        vb+=struct.pack("<3f",*v["pos"])
        if flags&0x400: vb+=bytes(v["w0"])+bytes(v["i0"])
        if flags&0x8000: vb+=bytes(v["w1"])+bytes(v["i1"])
        if flags&0x4: vb+=bytes([int(round((c+1)*127.5)) for c in v["n"]]+[255])
        if flags&0x80: vb+=bytes(v["col"])
        if flags&0x10:
            if flags&0x1000: vb+=struct.pack("<2f",*v["uv"])
            else: vb+=struct.pack("<2h",*(int(round(c*1024)) for c in v["uv"]))
    tb=b"".join(struct.pack("<3H",*t) for t in tris)
    # textures
    texb=b"".join(struct.pack("<IHHHH",tcrc("tex%d"%i),i,64,64,0) for i in range(textures))
    mattex=b""; params=b""; matb=b""; mt_i=0; p_i=0
    for (sh, tex, prm) in materials:
        matb+=struct.pack("<I",tcrc(sh+name))+sh.encode()+struct.pack("<HHHHi",mt_i,len(tex),p_i,len(prm),-1)
        for ti in tex: mattex+=struct.pack("<Ihhhh",tcrc("tex%d"%ti),ti,0,0,0); mt_i+=1
        for p in prm: params+=struct.pack("<ff",0.0,p); p_i+=1
    # submeshes + entries
    subb=b""; entries=b""; modelb=b""; si=0
    for (mname, parts) in models:
        estart=len(entries)//2
        for (mat, tbl, ts, tc) in parts:
            entries+=struct.pack("<BB",mat,96)
            if tbl is not None: entries+=struct.pack("<BB",tbl,64)
            subb+=struct.pack("<II",tc,ts)
            entries+=struct.pack("<BB",si,48); si+=1
        entries+=struct.pack("<BB",0,16)
        ecount=len(entries)//2-estart
        modelb+=struct.pack("<6f",*([0]*6))+struct.pack("<HBB",ecount,1,4)+struct.pack("<IiII",estart,addname(mname),tcrc(mname),0)
    # bones
    hier=b""; mats=b""; extra=b""; extraf=b""; ne=0
    for bone in bones:
        (bn, par, w) = bone[:3]
        hier+=struct.pack("<I3fiHH",tcrc(bn),w[12],w[13],w[14],par,0,addname(bn))
        mats+=rows_from(inv_affine(w))
        if len(bone) > 3:   # extra: поворот позы покоя (эйлер XYZ)
            extra+=struct.pack("<h",ne); extraf+=struct.pack("<3f",*bone[3]); ne+=1
        else:
            extra+=struct.pack("<h",-1)
    extra=align(extra)+extraf
    tblidx=b""; tblb=b""; allidx=[]
    for t in tables:
        tblb+=struct.pack("<II",len(t),len(allidx)); allidx+=t
    tblidx=struct.pack("<%dI"%len(allidx),*allidx)

    hdr_size = 200 if flags&0x2000 else 160
    body=b""; offs={}
    def put(key, data):
        nonlocal body
        offs[key]=hdr_size+len(body); body+=data; body=align(body)
    put("models",modelb); put("entries",entries); put("mats",matb); put("params",params); put("sub",subb)
    put("tris",tb); put("tex",texb); put("mattex",mattex); put("verts",vb)
    if flags&0x2000:
        put("tbl",tblb); put("tblidx",tblidx); put("bmat",mats); put("extra",extra); put("hier",hier)
    put("bbox", b"\0"*(96*(len(models)+1)))
    put("names",names)
    h=b"tmd0"+struct.pack("<BBHHHHh",0,0,flags,0,0x209,0x28,-1)+struct.pack("<6f",-1,0,-1,1,2,1)
    h+=struct.pack("<Q",offs["models"])+struct.pack("<I",0)
    h+=struct.pack("<IIII",offs["entries"],offs["mats"],offs["params"],offs["names"])
    h+=struct.pack("<III",0,offs["sub"],offs["tris"])
    h+=struct.pack("<Q",offs["tex"])+struct.pack("<II",offs["mattex"],offs["verts"])
    h+=struct.pack("<Q",offs["bbox"])+struct.pack("<I",len(models))+struct.pack("<Q",0)
    h+=struct.pack("<IIII",len(entries)//2,len(materials),len(params)//8,0)
    h+=struct.pack("<III",0,len(subb)//8,len(tris))
    h+=struct.pack("<Q",textures)+struct.pack("<II",len(mattex)//12,len(verts))
    assert len(h)==160, len(h)
    if flags&0x2000:
        h+=struct.pack("<10I",offs["tbl"],offs["tblidx"],offs["bmat"],offs["hier"],len(tables),len(allidx),len(bones),len(bones),offs["extra"],0)
    assert len(h)==hdr_size
    return h+body

# ---------- body (cos): 3 bones ----------
bones=[("root",-1,trans(0,0,0)), ("spine",0,trans(0,1,0)), ("head",1,mat_mul(rotz(0.3),trans(0,1.6,0)))]
def V(p,n,uv,ws,ids,col=(255,0,0,255)):
    w0=list(ws[:4])+[0]*(4-len(ws[:4])); i0=list(ids[:4])+[0]*(4-len(ids[:4]))
    w1=list(ws[4:8])+[0]*(4-len(ws[4:8])); i1=list(ids[4:8])+[0]*(4-len(ids[4:8]))
    return {"pos":p,"n":n,"uv":uv,"w0":w0,"i0":i0,"w1":w1,"i1":i1,"col":col}
verts=[
 V((0,0,0),(0,0,1),(0,0),[255],[0]), V((1,0,0),(0,0,1),(1,0),[255],[0]), V((0,1,0),(0,0,1),(0,1),[128,127],[0,1]),
 V((1,1,0),(0,0,1),(1,1),[255],[1]), V((0,1.6,0),(0,0,1),(0,0),[255],[0]), V((1,1.6,0),(0,0,1),(1,0),[200,0,0,0,55],[0,0,0,0,1]),
]
tris=[(0,1,2),(1,3,2),(2,3,4),(3,5,4)]
# table0 maps local 0->0 (root), 1->1 (spine); table1 maps local 0->1 (spine), 1->2 (head)
tables=[[0,1],[1,2]]
models=[("body",[(0,0,0,2),(1,1,2,2)])]
mats=[("BSS1",[0],[0.5]*32),("BHA1",[1],[0.1]*15)]
flags=0x2|0x4|0x10|0x80|0x400|0x1000|0x2000|0x8000
open(os.path.join(out,"pl999_cos00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl999_cos00_00",bones,verts,tris,models,mats,2,tables,flags)))

# ---------- hair: shares spine/head, adds hair bone ----------
hb=[("spine",-1,trans(0,1,0)),("head",0,mat_mul(rotz(0.3),trans(0,1.6,0))),("hair_A00",1,trans(0,1.9,0))]
hv=[V((0,1.8,0),(0,1,0),(0,0),[255],[1]),V((1,1.8,0),(0,1,0),(1,0),[255],[2]),V((0,2,0),(0,1,0),(0,1),[255],[2])]
open(os.path.join(out,"pl999_hair00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl999_hair00_00",hb,hv,[(0,1,2)],[("hair",[(0,None,0,1)])],[("BHA1",[0],[0.2]*15)],1,[],flags)))

# ---------- weapon: own skeleton, no shared bones; uint16 uv ----------
wb=[("wep_root",-1,trans(0,0,0)),("wep_tip",0,trans(0,0,1))]
wv=[V((0,0,0),(1,0,0),(0,0),[255],[0]),V((0,0.1,0),(1,0,0),(1,0),[255],[0]),V((0,0,1),(1,0,0),(0,1),[255],[1])]
wflags=0x2|0x4|0x10|0x400|0x2000
open(os.path.join(out,"pl999_wep00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl999_wep00_00",wb,wv,[(0,1,2)],[("blade",[(0,None,0,1)])],[("BSS1",[0],[1.0])],1,[],wflags)))

# body with a weapon bone so attach finds it
# (add 'weapon_R' bone under spine in a second body variant)
bones2=bones+[("weapon_R",1,trans(0.5,1.2,0))]
open(os.path.join(out,"pl998_cos00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl998_cos00_00",bones2,verts,tris,models,mats,2,tables,flags)))

# ---------- тело с цепочкой оружия, на которую тело вершин не вешает (кость стоит "как попало") ----------
bones3=bones+[("weapon_R",1,trans(0.5,1.2,0)),("chain_A",3,trans(0.5,1.2,1))]
open(os.path.join(out,"pl996_cos00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl996_cos00_00",bones3,verts,tris,models,mats,2,tables,flags)))
# оружие: та же цепочка, но chain_A в файле стоит по X - это и есть настоящая форма оружия
cb=[("weapon_R",-1,trans(0.5,1.2,0)),("chain_A",0,trans(1.5,1.2,0))]
cv=[V((0.5,1.2,0),(1,0,0),(0,0),[255],[0]),V((1.5,1.2,0),(1,0,0),(1,0),[255],[1]),V((1.5,1.3,0),(1,0,0),(0,1),[255],[1])]
open(os.path.join(out,"pl996_wep00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl996_wep00_00",cb,cv,[(0,1,2)],[("blade",[(0,None,0,1)])],[("BSS1",[0],[1.0])],1,[],wflags)))

# ---------- плащ: кость с extra-поворотом позы покоя ----------
bones4=bones+[("cape_A",1,trans(0,1,-0.1)),("cape_B",3,trans(0,0.5,-0.1),(0.0,0.0,0.5))]
open(os.path.join(out,"pl995_cos00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl995_cos00_00",bones4,verts,tris,models,mats,2,tables,flags)))

# ---------- физика ткани (.cphy): cape_A (корень цепочки) -> cape_B ----------
def cphy(joints, links=()):
    out=b"CPHY"+struct.pack("<IIIII",10004,len(joints),len(links),0,2)
    out=out.ljust(872,b"\x00")   # схема полей (здесь пустая) - читатель ищет таблицу сам
    for (par, j, length) in joints:
        prm=struct.pack("<18f4BI",1.0,0.2,0.06,-0.52,-0.52,-0.52,0.52,0.52,0.52,0.98,0.98,0.2,1.0,0.0,3.1416,length,length,0.0,0,0,0,0,0)
        out+=struct.pack("<II",tcrc(par) if par else 0,tcrc(j))+prm+struct.pack("<IIf",1,0,0.02)
    for (a,b,mx,mn) in links:
        out+=struct.pack("<IIff",tcrc(a),tcrc(b),mx,mn)+b"\x00"*32
    return out
open(os.path.join(out,"pl995_cos00.cphy"),"wb").write(cphy([(None,"cape_A",1.0),("cape_A","cape_B",0.5)]))

# ---------- LDS with 2 DDS ----------
def dds(fourcc, w=4, h=4):
    hd=bytearray(128); hd[0:4]=b"DDS "; struct.pack_into("<I",hd,4,124); struct.pack_into("<II",hd,12,h,w)
    struct.pack_into("<I",hd,80,4); hd[84:88]=fourcc
    return bytes(hd)+b"\0"*16
ddss=[dds(b"DXT1"),dds(b"DXT5")]
offs=[];cur=0
for d in ddss: offs.append(cur); cur+=len(d)
lds=struct.pack("<III",12+4*len(ddss)-4,len(ddss),0)+struct.pack("<%dI"%len(ddss),*offs)+b"".join(ddss)
open(os.path.join(out,"pl999_cos00_00.lds"),"wb").write(pzze("lds\0",lds))

# ---------- tactpkg with tmo1 clips ----------
def tmo1(hashes, curves, frames):
    # curves: list per bone of 9 lists of (frame,value)
    fc=b""; kf=b""; k=0
    for b in curves:
        for ch in b:
            fc+=struct.pack("<III",k,len(ch),ch[-1][0] if ch else 0)
            for (f,v) in ch: kf+=struct.pack("<HHf",f,0,v); k+=1
    head=b"tmo1"+struct.pack("<IHHf",0,1,0,1.0)  # 16
    head+=struct.pack("<II",0,0)  # flag3, framesOffset (patched)
    head=align(head)+struct.pack("<Q",0)
    # hashesOff, fcCount, fcOff, kfCount, kfOff
    pos_tbl=len(head); head+=b"\0"*20; head=align(head)
    framesOff=len(head); head+=struct.pack("<HH",frames,len(hashes)); head=align(head)
    hashesOff=len(head); head+=struct.pack("<%dI"%len(hashes),*hashes); head=align(head)
    fcOff=len(head); head+=fc; head=align(head)
    kfOff=len(head); head+=kf
    head=bytearray(head)
    struct.pack_into("<I",head,20,framesOff)
    struct.pack_into("<5I",head,pos_tbl,hashesOff,len(curves)*9,fcOff,k,kfOff)
    return bytes(head)
def pkg(typ, entries, base_pad=0):
    hdr=typ.encode().ljust(28,b"\0")+struct.pack("<II",0,len(entries))
    tbl_end=len(hdr)+72*len(entries)
    data=b""; tbl=b""; cur=align(b"\0"*tbl_end)
    cur_off=len(cur)
    for (n,d) in entries:
        tbl+=n.encode().ljust(64,b"\0")+struct.pack("<II",cur_off+len(data),len(d))
        data+=d; data=align(data)
    return hdr+tbl+b"\0"*(cur_off-tbl_end)+data
H=lambda n:tcrc(n)
# delta clip: spine rotates about Y over 10 frames, head bobs
c_delta=tmo1([H("spine"),H("head"),H("missing_bone")],[
  [[],[],[],[],[(0,0.0),(10,1.0)],[],[],[],[]],
  [[(0,0.0),(5,0.1),(10,0.0)],[],[],[],[],[],[],[],[]],
  [[]]*9], 11)
# absolute clip: positions equal to bind local translations
c_abs=tmo1([H("spine"),H("head")],[
  [[(0,0.0)],[(0,1.0)],[(0,0.0)],[],[],[],[],[],[]],
  [[(0,0.0)],[(0,0.6)],[(0,0.0)],[],[],[(0,0.3)],[],[],[]]], 5)
tmo_pkg=pkg("acttmo_pkg",[("idle_delta",c_delta),("pose_abs",c_abs)])
mng=pkg("actmng_pkg",[("script.lua",b"print('x')"),("motion",tmo_pkg)])
open(os.path.join(out,"pl999.tactpkg"),"wb").write(pzze("tact",mng))
# ---------- контейнеры: CATS, одиночный tmo, незнакомый контейнер, actmtl ----------
def cats(entries):
    # magic, unk, count, headersOffset(относительно 16) ; записи: u64 name, u64 ofs, u64 size (+выравнивание 16)
    n=len(entries); hdr_end=16+32*n
    names=b""; name_ofs=[]
    for (nm,_) in entries: name_ofs.append(hdr_end+len(names)); names+=nm.encode()+b"\0"
    blob=align(b"\0"*(hdr_end+len(names))); data=b""; offs=[]
    for (_,d) in entries: offs.append(len(blob)+len(data)); data+=d; data=align(data)
    out=b"CATS"+struct.pack("<III",0,n,0)
    for i,(nm,d) in enumerate(entries): out+=struct.pack("<QQQ",name_ofs[i],offs[i],len(d))+b"\0"*8
    out+=names; out=align(out)
    return out+data
mtl=pkg("actmtl_pkg",[("garganta_uv_scroll",b"\x01"*40)])
obj_tmo=pkg("acttmo_pkg",[("garganta_open",c_abs)])
open(os.path.join(out,"garganta.cat"),"wb").write(cats([("mdl.cat",b"tmd0"+b"\0"*60),("mot",pzze("tact",obj_tmo)),("raw.tmo",c_delta),("fx",mtl)]))
open(os.path.join(out,"single.tmo"),"wb").write(pzze("tmo\0",c_delta))
open(os.path.join(out,"unknown.bin"),"wb").write(b"XFX1"+b"\0"*12+c_abs+b"\0"*8+c_delta)
open(os.path.join(out,"pl997.tactpkg"),"wb").write(pzze("tact",pkg("actmng_pkg",[("motion",tmo_pkg),("mtl",mtl),("blend",b"motblend_file\0\0\0"+b"\0"*16)])))
print("ok", out)


# ---------- лицо с ДРУГОЙ bind-позой общей кости (голова в начале координат) ----------
fb=[("head",-1,trans(0,0,0)),("eye_L",0,trans(0.1,0.1,0.05))]
fv=[V((0,0,0.1),(0,0,1),(0,0),[255],[0]),V((0.1,0,0.1),(0,0,1),(1,0),[255],[0]),V((0.1,0.1,0.06),(0,0,1),(0,1),[255],[1])]
open(os.path.join(out,"pl999_face00_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl999_face00_00",fb,fv,[(0,1,2)],[("face",[(0,None,0,1)])],[("BSS1",[0],[1.0])],1,[],flags)))

# ---------- волосы: bind как у тела, но вершины в локальных координатах головы ----------
lb=[("spine",-1,trans(0,1,0)),("head",0,mat_mul(rotz(0.3),trans(0,1.6,0)))]
lv=[V((0,0.2,0),(0,1,0),(0,0),[255],[1]),V((0.1,0.2,0),(0,1,0),(1,0),[255],[1]),V((0,0.3,0.02),(0,1,0),(0,1),[255],[1])]
open(os.path.join(out,"pl999_hair01_00.tmd2"),"wb").write(pzze("tmd2",make_tmd2("pl999_hair01_00",lb,lv,[(0,1,2)],[("hairL",[(0,None,0,1)])],[("BHA1",[0],[0.2]*15)],1,[],flags)))
