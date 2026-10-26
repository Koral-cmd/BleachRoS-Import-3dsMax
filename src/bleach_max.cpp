// ============================================================
//  bleach_max.cpp - всё, что работает с 3ds Max SDK:
//  кости, меши, Skin, материалы, слои, ключи анимации, note track.
//
//  Правила (проверены на плагине SWBF2 Import, там же объяснены):
//   * Пространство: игра Y вверх -> Max Z вверх, (x, y, z) -> (x, -z, y),
//     переносы умножаются на масштаб S (единицы Max на единицу игры).
//   * Кости: сначала AttachChild, потом SetNodeTM от родителя к ребёнку.
//     Auto Align у костей выключен, иначе Max крутит их сам.
//   * Skin: ISkinImportData -> AddBoneEx -> EvalWorldState -> AddWeights.
//   * Ключи: только при AnimateOn + SuspendAnimate вокруг; контроллеры
//     линейные (Linear Position / Rotation / Scale), знак кватерниона
//     держим непрерывным.
//   * Числа в MAXScript и в user props - только с точкой (std::to_chars):
//     Max ставит локаль пользователя, printf выдал бы запятую.
//   * Исключения не выпускаем наружу: граница SDK ловит всё.
// ============================================================
#include "bleach.h"

#include "bleach_core.h"
#include "bleach_ui.h"
#include "tam_anim.h"
#include "tam_assemble.h"
#include "tam_formats.h"

#include <iskin.h>
#include <notetrck.h>
#include <maxscript/maxscript.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bleach {
namespace {

// ------------------------------------------------------------
//  Мелочи
// ------------------------------------------------------------
MSTR M(const std::string& utf8) { return MSTR::FromUTF8(utf8.c_str()); }

std::string FromMax(const MCHAR* s) { return s ? Utf8(std::wstring(s)) : std::string(); }

std::string Num(double v) {
    char b[64];
    const auto r = std::to_chars(b, b + sizeof b, v);
    return std::string(b, r.ptr);
}

bool ParseNum(const std::string& s, double& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc();
}

#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
bool RunScript(const std::wstring& s) {
    return ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::NonEmbedded, TRUE) != FALSE;
}
#else
bool RunScript(const std::wstring& s) { return ExecuteMAXScriptScript(s.c_str(), TRUE) != FALSE; }
#endif

// строка для MAXScript в кавычках
std::wstring Q(const std::string& s) {
    std::wstring w = L"\"";
    for (wchar_t c : Wide(s)) {
        if (c == L'\\' || c == L'"') w += L'\\';
        w += c;
    }
    return w + L"\"";
}

std::wstring WNum(double v) { return Wide(Num(v)); }

Matrix3 ToMax(const tam::M4& a) {
    Matrix3 m;
    m.IdentityMatrix();
    for (int r = 0; r < 4; ++r)
        m.SetRow(r, Point3(static_cast<float>(a.m[r * 4]), static_cast<float>(a.m[r * 4 + 1]), static_cast<float>(a.m[r * 4 + 2])));
    return m;
}

// мировая матрица игры -> пространство персонажа в Max
tam::M4 GameToMax(const tam::M4& w, double S) { return tam::ScaleTranslation(tam::Mul(w, tam::YupToZup()), S); }

void SetProp(INode* n, const char* key, const std::string& val) { n->SetUserPropString(M(key), M(val)); }

std::string GetProp(INode* n, const char* key) {
    MSTR v;
    if (n == nullptr || !n->GetUserPropString(M(key), v) || v.data() == nullptr) return std::string();
    return FromMax(v.data());
}

double SystemUnitsPerMeter() {
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const double m = GetSystemUnitScale(UNITS_METERS);
#else
    const double m = GetMasterScale(UNITS_METERS);
#endif
    return m > 0.0 ? 1.0 / m : 39.37007874015748;
}

struct NoAnimate {
    BOOL was;
    NoAnimate() : was(Animating()) {
        SuspendAnimate();
        AnimateOff();
    }
    ~NoAnimate() {
        ResumeAnimate();
        if (was) AnimateOn();
    }
};

struct RedrawOff {
    Interface* ip;
    explicit RedrawOff(Interface* i) : ip(i) {
        theHold.Suspend();
        ip->DisableSceneRedraw();
    }
    ~RedrawOff() {
        ip->EnableSceneRedraw();
        theHold.Resume();
        ip->RedrawViews(ip->GetTime());
    }
};

DWORD KindColor(const std::string& kind) {
    if (kind == "face") return RGB(255, 190, 150);
    if (kind == "hair") return RGB(255, 150, 40);
    if (kind == "wep") return RGB(230, 50, 50);
    if (kind == "other") return RGB(120, 200, 255);
    return RGB(225, 225, 225);
}

INode* MakeHelper(Interface* ip) {
    Object* o = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
    return o ? ip->CreateObjectNode(o) : nullptr;
}

INode* MakeBoneNode(Interface* ip, bool asPoint) {
    Object* o = nullptr;
    if (!asPoint) o = static_cast<Object*>(ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
    if (o == nullptr) o = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
    if (o == nullptr) return nullptr;
    INode* n = ip->CreateObjectNode(o);
    if (n == nullptr) return nullptr;
    n->ShowBone(1);
    n->SetBoneNodeOnOff(TRUE, 0);
    n->SetBoneAutoAlign(FALSE);
    n->SetBoneFreezeLen(TRUE);
    n->SetRenderable(FALSE);
    return n;
}

void SetWorld(INode* n, const tam::M4& m) {
    Matrix3 t = ToMax(m);
    n->SetNodeTM(0, t);
}

// ------------------------------------------------------------
//  Текстуры
// ------------------------------------------------------------
struct TexInfo {
    std::string path;
    bool alpha = false;
    std::string format;
};

bool WriteFileW(const std::wstring& p, const std::vector<uint8_t>& d) {
#if defined(_MSC_VER)
    std::FILE* f = _wfopen(p.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(Utf8(p).c_str(), "wb");
#endif
    if (f == nullptr) return false;
    const size_t n = d.empty() ? 0 : std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    return n == d.size();
}

// .lds рядом с .tmd2 -> DDS в кэше; ключ - индекс в .lds
std::map<int, TexInfo> ExportTextures(const tam::Tmd2& t, const std::string& charId, const std::string& suffix) {
    std::map<int, TexInfo> out;
    std::string lds = t.path;
    const size_t dot = lds.find_last_of('.');
    if (dot != std::string::npos) lds = lds.substr(0, dot);
    lds += ".lds";
    tam::Lds l;
    std::string err;
    if (!tam::LoadLds(lds, l, err)) {
        Log("  текстуры %s: %s", t.name.c_str(), err.c_str());
        return out;
    }
    const std::wstring dir = DataDir() + L"\\textures\\" + Wide(charId);
#if defined(_MSC_VER)
    CreateDirectoryW((DataDir() + L"\\textures").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
#endif
    for (size_t i = 0; i < l.textures.size(); ++i) {
        const std::wstring p = dir + L"\\" + Wide(t.name + suffix) + L"_" + std::to_wstring(i) + L".dds";
        if (!WriteFileW(p, l.textures[i])) {
            Log("  [!] не записать %s", Utf8(p).c_str());
            continue;
        }
        const tam::DdsInfo di = tam::InspectDds(l.textures[i]);
        TexInfo ti;
        ti.path = Utf8(p);
        ti.alpha = di.alpha;
        ti.format = di.format;
        out[static_cast<int>(i)] = ti;
    }
    Log("  %s.lds: текстур %zu -> %s", t.name.c_str(), l.textures.size(), Utf8(dir).c_str());
    return out;
}

// ------------------------------------------------------------
//  Скелет из сцены (по user props, записанным при импорте)
// ------------------------------------------------------------
struct SceneRig {
    INode* root = nullptr;
    tam::Skeleton skel;
    std::vector<INode*> nodes;   // по индексам skel
    double scale = 1.0;
    std::string charId;
};

void Collect(INode* n, std::vector<INode*>& out, int depth) {
    if (n == nullptr || depth > 512) return;
    for (int i = 0; i < n->NumberOfChildren(); ++i) {
        INode* c = n->GetChildNode(i);
        out.push_back(c);
        Collect(c, out, depth + 1);
    }
}

std::vector<double> ParseList(const std::string& s) {
    std::vector<double> v;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(';', a);
        if (b == std::string::npos) b = s.size();
        double x = 0.0;
        if (b > a && ParseNum(s.substr(a, b - a), x)) v.push_back(x);
        a = b + 1;
    }
    return v;
}

bool ReadRig(ULONG handle, SceneRig& rig, std::string& err) {
    Interface* ip = GetCOREInterface();
    rig.root = ip->GetINodeByHandle(handle);
    if (rig.root == nullptr || GetProp(rig.root, "bleach_root") != "1") {
        err = "Скелет не найден в сцене";
        return false;
    }
    double sc = 1.0;
    if (ParseNum(GetProp(rig.root, "bleach_scale"), sc) && sc > 0.0) rig.scale = sc;
    rig.charId = GetProp(rig.root, "bleach_char");
    std::vector<INode*> all;
    Collect(rig.root, all, 0);
    struct B { int idx; uint32_t hash; int parent; tam::M4 bind; INode* node; };
    std::vector<B> bones;
    for (INode* n : all) {
        const std::string si = GetProp(n, "bleach_index");
        if (si.empty()) continue;
        double idx = 0, h = 0, par = -1;
        if (!ParseNum(si, idx) || !ParseNum(GetProp(n, "bleach_hash"), h)) continue;
        ParseNum(GetProp(n, "bleach_parent"), par);
        const std::vector<double> m = ParseList(GetProp(n, "bleach_bind"));
        if (m.size() != 16) continue;
        B b;
        b.idx = static_cast<int>(idx);
        b.hash = static_cast<uint32_t>(h);
        b.parent = static_cast<int>(par);
        for (int k = 0; k < 16; ++k) b.bind.m[k] = m[static_cast<size_t>(k)];
        b.node = n;
        bones.push_back(b);
    }
    std::sort(bones.begin(), bones.end(), [](const B& a, const B& b) { return a.idx < b.idx; });
    std::map<int, int> remap;
    for (size_t i = 0; i < bones.size(); ++i) remap[bones[i].idx] = static_cast<int>(i);
    rig.skel = tam::Skeleton();
    rig.nodes.clear();
    for (const B& b : bones) {
        const auto it = remap.find(b.parent);
        rig.skel.Add(b.hash, FromMax(b.node->GetName()), it == remap.end() ? -1 : it->second, b.bind, "", "");
        rig.nodes.push_back(b.node);
    }
    rig.skel.Finalize();
    if (rig.nodes.empty()) {
        err = "У скелета нет костей с данными импорта";
        return false;
    }
    return true;
}

// ============================================================
//  ИМПОРТ МОДЕЛЕЙ
// ============================================================
struct MatGroup {
    std::string name;
    std::string diffuse;
    bool alpha = false;
    double color[3] = { 1, 1, 1 };
};

struct NodeMats {
    ULONG handle = 0;
    std::vector<int> groups;   // по matId
};

bool ImportModels(const std::vector<ImportItem>& items, const ImportOptions& o, std::string& report) {
    LogOpen(false);
    Log("BLEACH RoS Import %s, MAX_RELEASE %d", Utf8(BLEACHIMPORT_VERSION_STR).c_str(), MAX_RELEASE);
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) { report = "Нет интерфейса Max"; return false; }

    // ---- чтение ----
    std::vector<std::unique_ptr<tam::Tmd2>> tmds;
    std::map<const tam::Tmd2*, ImportItem> itemOf;
    std::string errors;
    for (const ImportItem& it : items) {
        std::unique_ptr<tam::Tmd2> t(new tam::Tmd2());
        std::string err;
        if (!tam::LoadTmd2(it.path, *t, err)) {
            Log("ОШИБКА %s", err.c_str());
            errors += err + "\n";
            continue;
        }
        Log("%s", t->Report().c_str());
        itemOf[t.get()] = it;
        tmds.push_back(std::move(t));
    }
    if (tmds.empty()) {
        report = "Ни один файл не прочитан.\n" + errors;
        return false;
    }

    std::vector<tam::Part> parts(tmds.size());
    for (size_t i = 0; i < tmds.size(); ++i) parts[i].tmd = tmds[i].get();
    tam::BuildOptions bo;
    bo.attachBone = o.attachBone;
    bo.weld = o.weld;
    bo.vertexColors = o.vertexColors;
    tam::Skeleton skel;
    std::vector<std::string> blog;
    tam::BuildSkeleton(parts, skel, bo, blog);
    for (const std::string& l : blog) LogRaw(l);
    Log("Скелет: костей %zu", skel.bones.size());

    // ---- масштаб ----
    double height = 0.0;
    for (const auto& t : tmds) height = std::max({ height, static_cast<double>(std::fabs(t->bbox[4] - t->bbox[1])), static_cast<double>(std::fabs(t->bbox[3] - t->bbox[0])) });
    const double gameK = (o.gameUnits == 1) ? 1.0 : (o.gameUnits == 2) ? 0.01 : (height > 20.0 ? 0.01 : 1.0);
    const double upm = o.unitsPerMeter > 0.0 ? o.unitsPerMeter : SystemUnitsPerMeter();
    const double S = upm * gameK;
    Log("Масштаб: высота bbox %.3f, ед. игры = %s, %.4f ед. Max на метр -> S = %.5f", height, gameK == 1.0 ? "м" : "см", upm, S);

    std::string charId;
    for (const tam::Part& p : parts)
        if (itemOf[p.tmd].lod == 0) { charId = CharIdOf(p.tmd->name); break; }
    if (charId.empty()) charId = CharIdOf(parts[0].tmd->name);

    std::vector<INode*> boneNodes(skel.bones.size(), nullptr);
    std::vector<ULONG> mainLayer;
    std::map<std::string, std::vector<ULONG>> lodLayers;
    std::vector<NodeMats> nodeMats;
    std::vector<MatGroup> groups;
    size_t meshCount = 0, vertTotal = 0, faceTotal = 0, skinned = 0, lodMeshes = 0, inFileLods = 0;
    std::wstring boneScript;
    INode* root = nullptr;

    {
        RedrawOff ro(ip);
        NoAnimate na;

        // ---- корень ----
        root = MakeHelper(ip);
        if (root == nullptr) { report = "Не удалось создать корневой helper"; return false; }
        root->SetName(M(charId + "_root").data());
        root->SetWireColor(RGB(255, 200, 40));
        SetProp(root, "bleach_root", "1");
        SetProp(root, "bleach_char", charId);
        SetProp(root, "bleach_scale", Num(S));
        {
            std::string pl;
            for (const auto& t : tmds) pl += (pl.empty() ? "" : ";") + t->name;
            SetProp(root, "bleach_parts", pl);
        }
        mainLayer.push_back(root->GetHandle());
        boneScript += L" (local n = maxOps.getNodeByHandle " + std::to_wstring(root->GetHandle()) +
                      L"; if n != undefined do try (n.box = true; n.cross = false; n.axistripod = false; n.size = " +
                      WNum(0.25 * S) + L") catch ())\n";

        // ---- кости ----
        std::vector<tam::M4> maxW(skel.bones.size());
        for (size_t i = 0; i < skel.bones.size(); ++i) maxW[i] = GameToMax(skel.bones[i].bindWorld, S);
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            INode* n = MakeBoneNode(ip, o.boneStyle == 1);
            if (n == nullptr) continue;
            const tam::SkelBone& b = skel.bones[i];
            n->SetName(M(b.name).data());
            n->SetWireColor(KindColor(b.kind));
            SetProp(n, "bleach_index", std::to_string(i));
            SetProp(n, "bleach_hash", std::to_string(b.hash));
            SetProp(n, "bleach_parent", std::to_string(b.parent));
            std::string bind;
            for (int k = 0; k < 16; ++k) bind += (k ? ";" : "") + Num(b.bindWorld.m[k]);
            SetProp(n, "bleach_bind", bind);
            SetProp(n, "bleach_source", b.source);
            boneNodes[i] = n;
            mainLayer.push_back(n->GetHandle());
        }
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            if (boneNodes[i] == nullptr) continue;
            const int p = skel.bones[i].parent;
            INode* par = (p >= 0 && boneNodes[static_cast<size_t>(p)] != nullptr) ? boneNodes[static_cast<size_t>(p)] : root;
            par->AttachChild(boneNodes[i], 0);
        }
        for (int i : skel.TopoOrder())
            if (boneNodes[static_cast<size_t>(i)] != nullptr) SetWorld(boneNodes[static_cast<size_t>(i)], maxW[static_cast<size_t>(i)]);

        // длина костей = до ближайшего ребёнка (только вид)
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            if (boneNodes[i] == nullptr) continue;
            double len = 0.0;
            for (size_t c = 0; c < skel.bones.size(); ++c) {
                if (skel.bones[c].parent != static_cast<int>(i)) continue;
                const double dx = maxW[c].m[12] - maxW[i].m[12], dy = maxW[c].m[13] - maxW[i].m[13], dz = maxW[c].m[14] - maxW[i].m[14];
                const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > 1e-6 && (len == 0.0 || d < len)) len = d;
            }
            if (len == 0.0) len = 0.03 * S;
            const double w = std::max(0.003 * S, std::min(0.015 * S, len * 0.12));
            const std::wstring h = std::to_wstring(boneNodes[i]->GetHandle());
            if (o.boneStyle == 1)
                boneScript += L" (local n = maxOps.getNodeByHandle " + h + L"; if n != undefined do try (n.size = " + WNum(w * 4) +
                              L"; n.cross = true; n.box = false; n.axistripod = false; n.centermarker = false) catch ())\n";
            else
                boneScript += L" (local n = maxOps.getNodeByHandle " + h + L"; if n != undefined do try (n.width = " + WNum(w) +
                              L"; n.height = " + WNum(w) + L"; n.length = " + WNum(len) + L"; n.taper = 80) catch ())\n";
        }

        // ---- части ----
        std::map<std::pair<const tam::Tmd2*, int>, int> groupOf;   // (файл, материал) -> группа
        for (size_t pi = 0; pi < parts.size(); ++pi) {
            const tam::Tmd2& t = *parts[pi].tmd;
            const ImportItem& item = itemOf[&t];
            const std::map<int, TexInfo> tex =
                o.textures ? ExportTextures(t, charId, item.lod > 0 ? "_" + item.lodName : std::string()) : std::map<int, TexInfo>();
            std::vector<tam::PartMesh> meshes;
            tam::BuildMeshes(parts, static_cast<int>(pi), skel, bo, meshes);
            for (tam::PartMesh& pm : meshes) {
                const size_t nv = pm.pos.size() / 3, nf = pm.faces.size() / 3;
                TriObject* tri = CreateNewTriObject();
                if (tri == nullptr) continue;
                Mesh& mesh = tri->GetMesh();
                mesh.setNumVerts(static_cast<int>(nv));
                for (size_t v = 0; v < nv; ++v) {
                    const double x = pm.pos[v * 3], y = pm.pos[v * 3 + 1], z = pm.pos[v * 3 + 2];
                    mesh.setVert(static_cast<int>(v), static_cast<float>(x * S), static_cast<float>(-z * S), static_cast<float>(y * S));
                }
                mesh.setNumFaces(static_cast<int>(nf));
                for (size_t f = 0; f < nf; ++f) {
                    mesh.faces[f].setVerts(pm.faces[f * 3], pm.faces[f * 3 + 1], pm.faces[f * 3 + 2]);
                    mesh.faces[f].setEdgeVisFlags(1, 1, 1);
                    mesh.faces[f].setSmGroup(1);
                    mesh.faces[f].setMatID(static_cast<unsigned short>(pm.matIds[f]));
                }
                for (const auto& kv : pm.maps) {
                    const int ch = kv.first;
                    const tam::MapChannel& mc = kv.second;
                    const int nmv = static_cast<int>(mc.verts.size() / 3);
                    mesh.setMapSupport(ch, TRUE);
                    mesh.setNumMapVerts(ch, nmv);
                    for (int v = 0; v < nmv; ++v)
                        mesh.setMapVert(ch, v, Point3(mc.verts[static_cast<size_t>(v) * 3], mc.verts[static_cast<size_t>(v) * 3 + 1],
                                                      mc.verts[static_cast<size_t>(v) * 3 + 2]));
                    mesh.setNumMapFaces(ch, static_cast<int>(nf));
                    TVFace* tf = mesh.mapFaces(ch);
                    for (size_t f = 0; f < nf && tf != nullptr; ++f)
                        tf[f].setTVerts(mc.faces[f * 3], mc.faces[f * 3 + 1], mc.faces[f * 3 + 2]);
                }
                mesh.InvalidateTopologyCache();
                mesh.InvalidateGeomCache();
                mesh.buildNormals();
                INode* node = ip->CreateObjectNode(tri);
                if (node == nullptr) continue;

                // LOD: младший уровень из другой папки качества или модель "lodN" внутри файла
                std::string lodLayer;
                if (item.lod > 0) {
                    lodLayer = charId + " LOD" + std::to_string(item.lod) + " (" + item.lodName + ")";
                    ++lodMeshes;
                } else if (Lower(pm.name).find("lod") != std::string::npos) {
                    lodLayer = charId + " LOD (внутри файла)";
                    ++inFileLods;
                }
                std::string nodeName = t.name + "_" + pm.name;
                if (item.lod > 0) nodeName += "_LOD" + std::to_string(item.lod);
                node->SetName(M(nodeName).data());
                Matrix3 one;
                one.IdentityMatrix();
                node->SetNodeTM(0, one);
                SetProp(node, "bleach_part", t.name);
                if (!lodLayer.empty()) SetProp(node, "bleach_lod", lodLayer);
                if (lodLayer.empty()) mainLayer.push_back(node->GetHandle());
                else lodLayers[lodLayer].push_back(node->GetHandle());

                // материалы
                NodeMats nm;
                nm.handle = node->GetHandle();
                for (int matIndex : pm.materials) {
                    const auto key = std::make_pair(&t, matIndex);
                    auto g = groupOf.find(key);
                    if (g == groupOf.end()) {
                        MatGroup mg;
                        mg.name = t.name + "_" + (matIndex >= 0 ? t.materials[static_cast<size_t>(matIndex)].shader + "_" + std::to_string(matIndex) : "nomat");
                        if (matIndex >= 0) {
                            const tam::Tmd2Material& tm = t.materials[static_cast<size_t>(matIndex)];
                            const bool hair = tm.shader.compare(0, 2, "BH") == 0;
                            if (hair && tm.params.size() >= 15)
                                for (int k = 0; k < 3; ++k) mg.color[k] = std::min(1.0, std::max(0.0, static_cast<double>(tm.params[static_cast<size_t>(12 + k)])));
                            if (!hair && !tm.textures.empty()) {
                                const int ti = tm.textures[0].texture;
                                if (ti >= 0 && ti < static_cast<int>(t.textures.size())) {
                                    const auto tx = tex.find(t.textures[static_cast<size_t>(ti)].ldsIndex);
                                    if (tx != tex.end()) {
                                        mg.diffuse = tx->second.path;
                                        mg.alpha = o.alpha && tx->second.alpha;
                                    }
                                }
                            }
                            Log("  материал %s: шейдер %s, текстур %zu, параметров %zu, диффуз %s", mg.name.c_str(), tm.shader.c_str(),
                                tm.textures.size(), tm.params.size(), mg.diffuse.empty() ? "-" : mg.diffuse.c_str());
                        }
                        g = groupOf.emplace(key, static_cast<int>(groups.size())).first;
                        groups.push_back(mg);
                    }
                    nm.groups.push_back(g->second);
                }
                nodeMats.push_back(nm);

                // привязка / Skin
                if (pm.staticParent >= 0 && boneNodes[static_cast<size_t>(pm.staticParent)] != nullptr) {
                    boneNodes[static_cast<size_t>(pm.staticParent)]->AttachChild(node, 1);
                    Log("  %s: статичный, привязан к %s", nodeName.c_str(), skel.bones[static_cast<size_t>(pm.staticParent)].name.c_str());
                } else if (pm.skinned && o.skin) {
                    Modifier* mod = static_cast<Modifier*>(ip->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
                    ISkinImportData* imp = mod ? static_cast<ISkinImportData*>(mod->GetInterface(I_SKINIMPORTDATA)) : nullptr;
                    Interface7* ip7 = GetCOREInterface7();
                    if (imp != nullptr && ip7 != nullptr && ip7->AddModifier(*node, *mod) == Interface7::kRES_SUCCESS) {
                        std::set<int> used(pm.wBone.begin(), pm.wBone.end());
                        int fallback = pm.wBone.empty() ? 0 : pm.wBone.front();
                        used.insert(fallback);
                        for (int b : used)
                            if (b >= 0 && b < static_cast<int>(boneNodes.size()) && boneNodes[static_cast<size_t>(b)] != nullptr)
                                imp->AddBoneEx(boneNodes[static_cast<size_t>(b)], FALSE);
                        node->EvalWorldState(ip->GetTime());
                        size_t set = 0;
                        for (size_t v = 0; v < nv; ++v) {
                            Tab<INode*> bn;
                            Tab<float> wt;
                            for (uint32_t k = pm.wStart[v]; k < pm.wStart[v + 1]; ++k) {
                                INode* b = boneNodes[static_cast<size_t>(pm.wBone[k])];
                                float w = pm.wValue[k];
                                if (b == nullptr || w <= 0.0f) continue;
                                bn.Append(1, &b);
                                wt.Append(1, &w);
                            }
                            if (bn.Count() == 0 && boneNodes[static_cast<size_t>(fallback)] != nullptr) {
                                INode* b = boneNodes[static_cast<size_t>(fallback)];
                                float w = 1.0f;
                                bn.Append(1, &b);
                                wt.Append(1, &w);
                            }
                            if (imp->AddWeights(node, static_cast<int>(v), bn, wt)) ++set;
                        }
                        node->EvalWorldState(ip->GetTime());
                        ++skinned;
                        Log("  %s: вершин %zu, граней %zu, костей в Skin %zu, весов записано %zu", nodeName.c_str(), nv, nf, used.size(), set);
                    } else {
                        Log("  [!] %s: Skin не создан", nodeName.c_str());
                    }
                } else {
                    root->AttachChild(node, 1);
                    Log("  %s: вершин %zu, граней %zu (без Skin)", nodeName.c_str(), nv, nf);
                }
                if (pm.degenerate) Log("  %s: пропущено вырожденных граней %zu", nodeName.c_str(), pm.degenerate);
                if (!pm.note.empty()) Log("  %s: %s", nodeName.c_str(), pm.note.c_str());
                ++meshCount;
                vertTotal += nv;
                faceTotal += nf;
            }
        }
    }

    // ---- вид костей, материалы, слои (MAXScript одним куском) ----
    std::wstring sk = L"(\n" + boneScript + L")\n";
    if (!RunScript(sk)) Log("[!] скрипт вида костей не выполнился");

    std::wstring ms = L"(\n local cls = (if PhysicalMaterial != undefined then PhysicalMaterial else StandardMaterial)\n local mats = #()\n";
    for (const MatGroup& g : groups) {
        ms += L" (local m = cls name:" + Q(g.name) + L"\n";
        ms += L"  try (m.base_color = color " + WNum(g.color[0] * 255) + L" " + WNum(g.color[1] * 255) + L" " + WNum(g.color[2] * 255) +
              L") catch (try (m.diffuse = color " + WNum(g.color[0] * 255) + L" " + WNum(g.color[1] * 255) + L" " + WNum(g.color[2] * 255) +
              L") catch ())\n  try (m.roughness = 0.6; m.metalness = 0.0) catch ()\n";
        if (!g.diffuse.empty()) {
            ms += L"  (local bt = Bitmaptexture filename:" + Q(g.diffuse) + L"\n";
            ms += L"   try (m.base_color_map = bt) catch (try (m.diffuseMap = bt) catch ())\n";
            ms += L"   try (showTextureMap m bt true) catch ())\n";
            if (g.alpha)
                ms += L"  (local ba = Bitmaptexture filename:" + Q(g.diffuse) + L"; ba.monoOutput = 1; try (m.cutout_map = ba) catch (try (m.opacityMap = ba) catch ()))\n";
        }
        ms += L"  append mats m)\n";
    }
    for (const NodeMats& nm : nodeMats) {
        ms += L" (local n = maxOps.getNodeByHandle " + std::to_wstring(nm.handle) + L"\n  if n != undefined do (\n";
        if (nm.groups.size() == 1) {
            ms += L"   n.material = mats[" + std::to_wstring(nm.groups[0] + 1) + L"]\n";
        } else {
            ms += L"   local mm = Multimaterial numsubs:" + std::to_wstring(nm.groups.size()) + L"\n   mm.name = n.name + \"_multi\"\n";
            for (size_t k = 0; k < nm.groups.size(); ++k)
                ms += L"   mm.materialList[" + std::to_wstring(k + 1) + L"] = mats[" + std::to_wstring(nm.groups[k] + 1) + L"]\n";
            ms += L"   n.material = mm\n";
        }
        ms += L"  ))\n";
    }
    ms += L" OK\n)\n";
    if (!groups.empty() && !RunScript(ms)) Log("[!] скрипт материалов не выполнился");

    auto layerScript = [](const std::string& name, const std::vector<ULONG>& handles, bool hidden) {
        std::wstring s = L"(\n local l = LayerManager.getLayerFromName " + Q(name) + L"\n if l == undefined do l = LayerManager.newLayerFromName " + Q(name) + L"\n";
        s += L" for h in #(";
        for (size_t i = 0; i < handles.size(); ++i) s += (i ? L"," : L"") + std::to_wstring(handles[i]);
        s += L") do (local n = maxOps.getNodeByHandle h; if n != undefined do l.addNode n)\n";
        s += std::wstring(L" l.isHidden = ") + (hidden ? L"true" : L"false") + L"\n OK\n)\n";
        return s;
    };
    RunScript(layerScript("BleachRoS " + charId, mainLayer, false));
    for (const auto& kv : lodLayers) RunScript(layerScript(kv.first, kv.second, true));
    ip->RedrawViews(ip->GetTime());

    char b[700];
    std::snprintf(b, sizeof b,
                  "Персонаж %s%s%s\nФайлов: %zu, костей: %zu\nОбъектов: %zu (вершин %zu, граней %zu), со Skin: %zu\nМатериалов: %zu\n"
                  "LOD: %s\nМасштаб: 1 ед. игры = %s ед. Max\n\nЛог: %s%s%s",
                  charId.c_str(), CharacterName(charId).empty() ? "" : " - ", CharacterName(charId).c_str(), tmds.size(), skel.bones.size(),
                  meshCount, vertTotal, faceTotal, skinned, groups.size(),
                  lodMeshes + inFileLods == 0 ? "нет (импортированы только основные модели)"
                                              : (std::to_string(lodMeshes) + " объектов младших уровней, " + std::to_string(inFileLods) +
                                                 " внутри файлов (скрытые слои)").c_str(),
                  Num(S).c_str(), Utf8(LogPath()).c_str(), errors.empty() ? "" : "\n\nОшибки:\n", errors.c_str());
    report = b;
    Log("%s", report.c_str());
    return meshCount > 0 || !skel.bones.empty();
}

// ============================================================
//  АНИМАЦИИ
// ============================================================
Control* FreshController(Interface* ip, SClass_ID sc, ULONG id) {
    return static_cast<Control*>(ip->CreateInstance(sc, Class_ID(id, 0)));
}

struct BoneCtrl {
    Control* pos = nullptr;
    Control* rot = nullptr;
    Control* scl = nullptr;
    Quat last;
    bool hasLast = false;
};

void Decompose(const tam::M4& local, Point3& p, Quat& q, Point3& s) {
    p = Point3(static_cast<float>(local.m[12]), static_cast<float>(local.m[13]), static_cast<float>(local.m[14]));
    double sc[3];
    tam::M4 r = local;
    for (int row = 0; row < 3; ++row) {
        sc[row] = tam::RowLen(local, row);
        if (sc[row] < 1e-12) sc[row] = 1.0;
        for (int c = 0; c < 3; ++c) r.m[row * 4 + c] /= sc[row];
    }
    r.m[12] = r.m[13] = r.m[14] = 0.0;
    q = Quat(ToMax(r));
    s = Point3(static_cast<float>(sc[0]), static_cast<float>(sc[1]), static_cast<float>(sc[2]));
}

void SetKey(BoneCtrl& c, TimeValue t, const tam::M4& local, bool withScale) {
    Point3 p, s;
    Quat q;
    Decompose(local, p, q, s);
    if (c.hasLast && (q.x * c.last.x + q.y * c.last.y + q.z * c.last.z + q.w * c.last.w) < 0.0f) q = Quat(-q.x, -q.y, -q.z, -q.w);
    c.last = q;
    c.hasLast = true;
    if (c.pos) c.pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
    if (c.rot) c.rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
    if (withScale && c.scl) {
        ScaleValue sv(s, Quat());
        c.scl->SetValue(t, &sv, 1, CTRL_ABSOLUTE);
    }
}

// Локальные матрицы (относительно родителя в Max) для позы poses[f]
tam::M4 MaxLocal(const SceneRig& rig, const std::vector<tam::M4>& pose, size_t b) {
    const tam::M4 w = GameToMax(pose[b], rig.scale);
    const int p = rig.skel.bones[b].parent;
    if (p < 0) return w;
    tam::M4 inv;
    tam::Inverse(GameToMax(pose[static_cast<size_t>(p)], rig.scale), inv);
    return tam::Mul(w, inv);
}

std::vector<tam::M4> BindPose(const SceneRig& rig) {
    std::vector<tam::M4> pose(rig.skel.bones.size());
    for (size_t i = 0; i < pose.size(); ++i) pose[i] = rig.skel.bones[i].bindWorld;
    return pose;
}

// ------------------------------------------------------------
//  Загруженные клипы = последовательности
//
//  Запись: note track на корневом helper'е скелета, два ключа на клип
//  (начало и конец) со значением в формате Warcraft 3 / WhiteoutDex:
//      "Имя"            или   "Имя"\r\nNonLooping
//  Публикация в сцену (то, что видят Sequence Manager WhiteoutDex и его
//  MDX-экспортёр):
//    * есть WhiteoutDex (WdxSequenceStorage) или на rootNode уже есть блок
//      последовательностей (seqNames) -> массивы блока; у оставшихся клипов
//      (тот же кадр начала) сохраняются Rarity / MoveSpeed / Extents / Group;
//    * иначе -> note track на rootNode (старый формат; WhiteoutDex сам
//      переносит его в свой блок при открытии Sequence Manager).
//  Перед каждым действием блок WhiteoutDex читается обратно: если клип
//  сдвинули, переименовали или удалили там - так и остаётся.
// ------------------------------------------------------------
struct Loaded {
    std::string name;
    int start = 0, end = 0;     // кадры
    bool nonLooping = false;
};

bool StartsWithI(const std::string& s, const char* p) {
    const std::string l = Lower(s);
    return l.compare(0, std::char_traits<char>::length(p), p) == 0;
}

// Анимации WC3, которые по смыслу не зациклены
bool DefaultNonLooping(const std::string& name) {
    if (StartsWithI(name, "spell channel")) return false;
    for (const char* p : { "attack", "spell", "death", "decay", "birth", "dissipate", "morph", "stand hit" })
        if (StartsWithI(name, p)) return true;
    return false;
}

std::string NoteValue(const Loaded& l) {
    std::string v = "\"" + l.name + "\"";
    if (l.nonLooping) v += "\r\nNonLooping";
    return v;
}

bool ParseNoteValue(const std::string& v, std::string& name, bool& nonLoop) {
    nonLoop = Lower(v).find("nonlooping") != std::string::npos;
    const size_t a = v.find('"');
    if (a != std::string::npos) {
        const size_t b = v.find('"', a + 1);
        name = v.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    } else {
        const size_t e = v.find_first_of("\r\n");
        name = v.substr(0, e);
    }
    return !name.empty();
}

std::vector<Loaded> NotesToLoaded(INode* holder) {
    std::vector<Loaded> out;
    if (holder == nullptr || holder->NumNoteTracks() < 1) return out;
    DefNoteTrack* nt = static_cast<DefNoteTrack*>(holder->GetNoteTrack(0));
    if (nt == nullptr) return out;
    const int tpf = std::max(1, GetTicksPerFrame());
    std::vector<std::pair<TimeValue, std::string>> keys;
    for (int i = 0; i < nt->keys.Count(); ++i)
        if (nt->keys[i] != nullptr) keys.emplace_back(nt->keys[i]->time, FromMax(nt->keys[i]->note.data()));
    std::sort(keys.begin(), keys.end());
    std::vector<bool> used(keys.size(), false);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (used[i] || keys[i].second.empty()) continue;
        for (size_t j = i + 1; j < keys.size(); ++j) {
            if (used[j] || keys[j].second != keys[i].second) continue;
            used[i] = used[j] = true;
            Loaded l;
            if (ParseNoteValue(keys[i].second, l.name, l.nonLooping)) {
                l.start = keys[i].first / tpf;
                l.end = keys[j].first / tpf;
                out.push_back(l);
            }
            break;
        }
    }
    return out;
}

void LoadedToNotes(INode* holder, const std::vector<Loaded>& clips) {
    if (holder == nullptr) return;
    for (int i = holder->NumNoteTracks() - 1; i >= 0; --i) holder->DeleteNoteTrack(holder->GetNoteTrack(i), TRUE);
    if (clips.empty()) return;
    DefNoteTrack* nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
    if (nt == nullptr) return;
    const int tpf = std::max(1, GetTicksPerFrame());
    for (const Loaded& c : clips) {
        const MSTR v = M(NoteValue(c));
        NoteKey* k0 = new NoteKey(c.start * tpf, v, 0);
        NoteKey* k1 = new NoteKey(c.end * tpf, v, 0);
        nt->keys.Append(1, &k0);
        nt->keys.Append(1, &k1);
    }
    holder->AddNoteTrack(nt);
}

void SortLoaded(std::vector<Loaded>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Loaded& a, const Loaded& b) { return a.start < b.start; });
}

std::string ReadTextFile(const std::wstring& p) {
#if defined(_MSC_VER)
    std::FILE* f = _wfopen(p.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(Utf8(p).c_str(), "rb");
#endif
    if (f == nullptr) return std::string();
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    std::fclose(f);
    if (all.size() >= 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB &&
        static_cast<unsigned char>(all[2]) == 0xBF)
        all.erase(0, 3);
    return all;
}

void DeleteFileW2(const std::wstring& p) {
#if defined(_MSC_VER)
    _wremove(p.c_str());
#else
    std::remove(Utf8(p).c_str());
#endif
}

std::wstring ScriptPath(const std::wstring& p) {
    std::wstring r;
    for (wchar_t c : p) r += (c == L'\\') ? std::wstring(L"/") : std::wstring(1, c);
    return L"\"" + r + L"\"";
}

// Блок последовательностей на rootNode (WhiteoutDex или наследие NeoDex) -> список.
// false: блока нет или он пустой (тогда правда - наша запись).
bool PullSceneSequences(std::vector<Loaded>& out) {
    const std::wstring file = DataDir() + L"\\seq_pull.txt";
    DeleteFileW2(file);
    std::wstring s = L"(\n local n = (try (rootNode.seqNames.count) catch (-1))\n"
                     L" if n > 0 do (\n  local f = undefined\n"
                     L"  try (f = createFile " + ScriptPath(file) + L" encoding:#utf8 writeBOM:false) catch (f = createFile " + ScriptPath(file) + L")\n"
                     L"  for i = 1 to n do (\n"
                     L"   local nl = (try (rootNode.nonLooping[i]) catch (false))\n"
                     L"   format \"%\\t%\\t%\\t%\\n\" rootNode.seqNames[i] rootNode.startFrames[i] rootNode.endFrames[i] (if nl == true then 1 else 0) to:f\n"
                     L"  )\n  close f\n )\n OK\n)\n";
    if (!RunScript(s)) return false;
    const std::string txt = ReadTextFile(file);
    DeleteFileW2(file);
    if (txt.empty()) return false;
    out.clear();
    size_t a = 0;
    while (a < txt.size()) {
        size_t e = txt.find('\n', a);
        if (e == std::string::npos) e = txt.size();
        std::string line = txt.substr(a, e - a);
        a = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t p = 0;
        while (true) {
            const size_t t = line.find('\t', p);
            f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
            if (t == std::string::npos) break;
            p = t + 1;
        }
        if (f.size() < 4 || f[0].empty()) continue;
        double s0 = 0, s1 = 0;
        if (!ParseNum(f[1], s0) || !ParseNum(f[2], s1)) continue;
        Loaded l;
        l.name = f[0];
        l.start = static_cast<int>(s0);
        l.end = static_cast<int>(s1);
        l.nonLooping = (f[3] == "1");
        out.push_back(l);
    }
    SortLoaded(out);
    return true;
}

// Запись -> сцена (блок WhiteoutDex или note track на rootNode).
void PublishSequences(const std::vector<Loaded>& clips) {
    Interface* ip = GetCOREInterface();
    const std::wstring file = DataDir() + L"\\seq_mode.txt";
    DeleteFileW2(file);
    std::wstring recs;
    for (size_t i = 0; i < clips.size(); ++i)
        recs += std::wstring(i ? L"," : L"") + L"#(" + Q(clips[i].name) + L"," + std::to_wstring(clips[i].start) + L"," +
                std::to_wstring(clips[i].end) + L"," + (clips[i].nonLooping ? L"true" : L"false") + L")";
    std::wstring s =
        L"(\n local recs = #(" + recs + L")\n"
        L" local wdx = (try (::WdxSequenceStorage != undefined) catch (false))\n"
        L" if wdx do try (::WdxSequenceStorage.ensureCA()) catch (wdx = false)\n"
        L" local hasCA = (try (rootNode.seqNames.count >= 0) catch (false))\n"
        L" local mode = \"notes\"\n"
        L" if hasCA do (\n"
        L"  fn grab p = (try (for v in (getProperty rootNode p) collect v) catch (#()))\n"
        L"  local oS = grab #startFrames, oR = grab #rarity, oM = grab #moveSpeed, oX = grab #seqExtents, oG = grab #sharedGroup\n"
        L"  local nN = #(), nS = #(), nE = #(), nL = #(), nR = #(), nM = #(), nX = #(), nG = #()\n"
        L"  for r in recs do (\n"
        L"   local k = findItem oS r[2]\n"
        L"   append nN r[1]; append nS r[2]; append nE r[3]; append nL r[4]\n"
        L"   append nR (if k > 0 and k <= oR.count then oR[k] else 0.0)\n"
        L"   append nM (if k > 0 and k <= oM.count then oM[k] else 0.0)\n"
        L"   append nX (if k > 0 and k <= oX.count then oX[k] else \"\")\n"
        L"   append nG (if k > 0 and k <= oG.count then oG[k] else \"\")\n"
        L"  )\n"
        L"  rootNode.seqNames = nN; rootNode.startFrames = nS; rootNode.endFrames = nE; rootNode.nonLooping = nL\n"
        L"  rootNode.rarity = nR; rootNode.moveSpeed = nM\n"
        L"  try (rootNode.seqExtents = nX) catch ()\n"
        L"  try (rootNode.sharedGroup = nG) catch ()\n"
        L"  while (numNoteTracks rootNode) != 0 do deleteNoteTrack rootNode (getNoteTrack rootNode 1)\n"
        L"  mode = (if wdx then \"wdx\" else \"ca\")\n"
        L" )\n"
        L" local f = createFile " + ScriptPath(file) + L"\n format \"%\" mode to:f\n close f\n OK\n)\n";
    const bool ran = RunScript(s);
    std::string mode = ReadTextFile(file);
    DeleteFileW2(file);
    if (!ran || mode.empty()) mode = "notes";
    if (mode.compare(0, 5, "notes") == 0 && ip != nullptr) LoadedToNotes(ip->GetRootNode(), clips);
    Log("  последовательностей %zu -> %s", clips.size(),
        mode.compare(0, 3, "wdx") == 0 ? "WhiteoutDex (Sequence Manager)" : mode.compare(0, 2, "ca") == 0 ? "блок seqNames на rootNode" : "note track на rootNode");
}

// Текущие клипы скелета: блок WhiteoutDex (если не пуст), иначе запись на helper'е.
std::vector<Loaded> CurrentLoaded(INode* holder) {
    std::vector<Loaded> fromScene;
    if (PullSceneSequences(fromScene)) {
        LoadedToNotes(holder, fromScene);
        return fromScene;
    }
    std::vector<Loaded> r = NotesToLoaded(holder);
    SortLoaded(r);
    return r;
}

void StoreLoaded(INode* holder, std::vector<Loaded> clips) {
    SortLoaded(clips);
    LoadedToNotes(holder, clips);
    PublishSequences(clips);
}

bool IsLinear(Control* c, ULONG id) { return c != nullptr && c->ClassID() == Class_ID(id, 0); }

TimeValue LastKeyTime(const SceneRig& rig) {
    TimeValue last = 0;
    for (INode* n : rig.nodes) {
        Control* tm = n->GetTMController();
        if (tm == nullptr) continue;
        for (Control* c : { tm->GetPositionController(), tm->GetRotationController(), tm->GetScaleController() }) {
            const int k = c ? c->NumKeys() : 0;
            if (k > 0) last = std::max(last, c->GetKeyTime(k - 1));
        }
    }
    return last;
}

void DeleteKeysIn(const SceneRig& rig, int a, int b) {
    const int tpf = std::max(1, GetTicksPerFrame());
    const Interval iv(a * tpf, b * tpf);
    for (INode* n : rig.nodes) {
        Control* tm = n->GetTMController();
        if (tm == nullptr) continue;
        for (Control* c : { tm->GetPositionController(), tm->GetRotationController(), tm->GetScaleController() })
            if (c != nullptr) c->DeleteTime(iv, TIME_INCLEFT | TIME_INCRIGHT | TIME_NOSLIDE);
    }
}

void ResetControllers(Interface* ip, const SceneRig& rig) {
    NoAnimate na;
    const std::vector<tam::M4> bind = BindPose(rig);
    for (size_t b = 0; b < rig.nodes.size(); ++b) {
        Control* tmc = rig.nodes[b]->GetTMController();
        if (tmc == nullptr) continue;
        Control* p = FreshController(ip, CTRL_POSITION_CLASS_ID, LININTERP_POSITION_CLASS_ID);
        Control* r = FreshController(ip, CTRL_ROTATION_CLASS_ID, LININTERP_ROTATION_CLASS_ID);
        Control* s = FreshController(ip, CTRL_SCALE_CLASS_ID, LININTERP_SCALE_CLASS_ID);
        if (p) tmc->SetPositionController(p);
        if (r) tmc->SetRotationController(r);
        if (s) tmc->SetScaleController(s);
        BoneCtrl c;
        c.pos = tmc->GetPositionController();
        c.rot = tmc->GetRotationController();
        c.scl = tmc->GetScaleController();
        SetKey(c, 0, MaxLocal(rig, bind, b), true);
    }
}

std::vector<SequenceInfo> ToInfo(const std::vector<Loaded>& v) {
    std::vector<SequenceInfo> r;
    for (const Loaded& l : v) {
        SequenceInfo s;
        s.name = l.name;
        s.start = l.start;
        s.end = l.end;
        s.nonLooping = l.nonLooping;
        r.push_back(s);
    }
    return r;
}

bool ApplyClips(ULONG handle, const tam::TactPkg& pkg, const std::vector<size_t>& clipIdx, const AnimOptions& o, std::string& report) {
    LogOpen(true);
    Interface* ip = GetCOREInterface();
    SceneRig rig;
    std::string err;
    if (!ReadRig(handle, rig, err)) { report = err; return false; }

    tam::AnimSettings as;
    as.mode = static_cast<tam::AnimMode>(o.mode);
    for (int k = 0; k < 3 && k < static_cast<int>(o.order.size()); ++k) as.order[k] = o.order[static_cast<size_t>(k)];

    // ---- клипы: неподходящие отбрасываются, сцена не трогается ----
    struct Ready { std::string name; std::vector<std::vector<tam::M4>> poses; std::vector<char> anim; bool scale = false; };
    std::vector<Ready> ready;
    std::vector<std::string> rejected;
    for (size_t ci : clipIdx) {
        tam::Tmo clip;
        if (!pkg.Clip(ci, clip, err)) { Log("  [!] %s", err.c_str()); rejected.push_back(pkg.clips[ci].name + " (не читается)"); continue; }
        const tam::ClipMatch m = tam::MatchClip(clip, rig.skel, as);
        Log("АНИМ %s", tam::DescribeMatch(clip, m).c_str());
        for (const std::string& w : clip.warnings) Log("    [!] %s", w.c_str());
        // подходит, если покрывает большую часть клипа или большую часть скелета
        const size_t mt = m.matched.size(), cb = clip.hashes.size(), sb = rig.skel.bones.size();
        if (mt == 0 || (mt * 3 < cb && mt * 3 < sb)) {
            Log("    не подходит к скелету (%zu из %zu костей клипа) - пропуск", mt, cb);
            rejected.push_back(clip.name + " (" + std::to_string(mt) + "/" + std::to_string(cb) + " костей)");
            continue;
        }
        Ready r;
        r.name = clip.name;
        tam::EvaluateClip(clip, rig.skel, as, m, r.poses, r.anim);
        for (const auto& pr : m.matched)
            for (int k = 6; k < 9; ++k)
                for (const tam::Key& key : clip.curves[static_cast<size_t>(pr.first) * 9 + static_cast<size_t>(k)])
                    if (std::fabs(key.value - 1.0f) > 1e-4f) r.scale = true;
        ready.push_back(std::move(r));
    }
    if (ready.empty()) {
        report = "Ни один клип не подходит к скелету \"" + FromMax(rig.root->GetName()) + "\" - ничего не изменено.";
        if (!rejected.empty()) report += "\nОтброшены: " + rejected.front() + (rejected.size() > 1 ? " и ещё " + std::to_string(rejected.size() - 1) : "");
        Log("%s", report.c_str());
        return false;
    }

    if (o.fps > 0 && GetFrameRate() != o.fps && TIME_TICKSPERSEC % o.fps == 0) SetFrameRate(o.fps);
    const int tpf = std::max(1, GetTicksPerFrame());
    const int gap = std::max(1, o.gap);

    std::vector<Loaded> loaded = CurrentLoaded(rig.root);
    if (o.replace && !loaded.empty()) {
        RedrawOff ro(ip);
        ResetControllers(ip, rig);
        Log("  загруженные клипы (%zu) удалены перед загрузкой", loaded.size());
        loaded.clear();
    }
    int lastEnd = 0;
    for (const Loaded& l : loaded) lastEnd = std::max(lastEnd, l.end);
    lastEnd = std::max(lastEnd, static_cast<int>(LastKeyTime(rig) / tpf));
    const bool first = loaded.empty() && lastEnd == 0;
    const bool anyScale = std::any_of(ready.begin(), ready.end(), [](const Ready& r) { return r.scale; });

    size_t keys = 0;
    int firstStart = 0;
    std::vector<Loaded> added;
    bool withScale = anyScale;
    {
        RedrawOff ro(ip);
        const std::vector<tam::M4> bind = BindPose(rig);
        std::vector<BoneCtrl> ctl(rig.nodes.size());
        {
            NoAnimate na;
            for (size_t b = 0; b < rig.nodes.size(); ++b) {
                Control* tmc = rig.nodes[b]->GetTMController();
                if (tmc == nullptr) continue;
                Control* p0 = tmc->GetPositionController();
                Control* r0 = tmc->GetRotationController();
                Control* s0 = tmc->GetScaleController();
                const bool keep = !first && IsLinear(p0, LININTERP_POSITION_CLASS_ID) && IsLinear(r0, LININTERP_ROTATION_CLASS_ID);
                if (keep) {
                    ctl[b].pos = p0;
                    ctl[b].rot = r0;
                } else {
                    Control* p = FreshController(ip, CTRL_POSITION_CLASS_ID, LININTERP_POSITION_CLASS_ID);
                    Control* r = FreshController(ip, CTRL_ROTATION_CLASS_ID, LININTERP_ROTATION_CLASS_ID);
                    if (p) tmc->SetPositionController(p);
                    if (r) tmc->SetRotationController(r);
                    ctl[b].pos = tmc->GetPositionController();
                    ctl[b].rot = tmc->GetRotationController();
                }
                if (IsLinear(s0, LININTERP_SCALE_CLASS_ID) && !first) {
                    ctl[b].scl = s0;
                    if (s0->NumKeys() > 0) withScale = true;
                } else if (anyScale || s0 == nullptr || !IsLinear(s0, LININTERP_SCALE_CLASS_ID)) {
                    Control* s = FreshController(ip, CTRL_SCALE_CLASS_ID, LININTERP_SCALE_CLASS_ID);
                    if (s) tmc->SetScaleController(s);
                    ctl[b].scl = tmc->GetScaleController();
                }
                if (!keep) SetKey(ctl[b], 0, MaxLocal(rig, bind, b), true);   // без ключей = bind
            }
        }
        AnimateOn();
        int at = first ? gap : lastEnd + gap;
        if (first) {
            // кадр 0 = bind-поза
            for (size_t b = 0; b < rig.nodes.size(); ++b) { SetKey(ctl[b], 0, MaxLocal(rig, bind, b), withScale); ++keys; }
        }
        firstStart = at;
        for (const Ready& r : ready) {
            const int frames = static_cast<int>(r.poses.size());
            const int start = at, end = at + std::max(1, frames - 1);
            for (size_t b = 0; b < rig.nodes.size(); ++b) {
                if (r.anim[b]) {
                    for (int f = 0; f < frames; ++f) {
                        SetKey(ctl[b], (start + f) * tpf, MaxLocal(rig, r.poses[static_cast<size_t>(f)], b), withScale);
                        ++keys;
                    }
                } else {
                    // кость вне клипа: держим её позу на обоих краях, чтобы клипы не перетекали друг в друга
                    SetKey(ctl[b], start * tpf, MaxLocal(rig, r.poses.front(), b), withScale);
                    SetKey(ctl[b], end * tpf, MaxLocal(rig, r.poses.back(), b), withScale);
                    keys += 2;
                }
            }
            Loaded l;
            l.name = r.name;
            l.start = start;
            l.end = end;
            l.nonLooping = DefaultNonLooping(r.name);
            added.push_back(l);
            at = end + gap;
        }
        AnimateOff();
    }
    loaded.insert(loaded.end(), added.begin(), added.end());
    StoreLoaded(rig.root, loaded);

    // диапазон пользователя не обрезаем, только расширяем
    int end = 1;
    for (const Loaded& l : loaded) end = std::max(end, l.end);
    const Interval cur = ip->GetAnimRange();
    if (end * tpf > cur.End()) ip->SetAnimRange(Interval(std::min<TimeValue>(cur.Start(), 0), end * tpf));
    ip->SetTime(firstStart * tpf, TRUE);
    ip->RedrawViews(ip->GetTime());

    char b[500];
    std::snprintf(b, sizeof b, "Добавлено клипов: %zu (кадры %d-%d), всего на таймлайне: %zu, ключей %zu, %d fps, линейные контроллеры%s",
                  added.size(), added.front().start, added.back().end, loaded.size(), keys, GetFrameRate(), withScale ? " (+ масштаб)" : "");
    report = b;
    if (!rejected.empty())
        report += "\nОтброшено (не подходят к скелету): " + std::to_string(rejected.size()) + " - " + rejected.front() +
                  (rejected.size() > 1 ? ", ..." : "");
    Log("%s", report.c_str());
    return true;
}

bool ResetBind(ULONG handle, std::string& report) {
    Interface* ip = GetCOREInterface();
    SceneRig rig;
    std::string err;
    if (!ReadRig(handle, rig, err)) { report = err; return false; }
    {
        RedrawOff ro(ip);
        ResetControllers(ip, rig);
    }
    StoreLoaded(rig.root, std::vector<Loaded>());
    report = "Скелет в bind-позе, все клипы и последовательности удалены";
    return true;
}

std::vector<SequenceInfo> Sequences(ULONG handle) {
    SceneRig rig;
    std::string err;
    if (!ReadRig(handle, rig, err)) return std::vector<SequenceInfo>();
    return ToInfo(CurrentLoaded(rig.root));
}

bool DeleteSequence(ULONG handle, size_t index, std::string& report) {
    LogOpen(true);
    Interface* ip = GetCOREInterface();
    SceneRig rig;
    std::string err;
    if (!ReadRig(handle, rig, err)) { report = err; return false; }
    std::vector<Loaded> loaded = CurrentLoaded(rig.root);
    if (index >= loaded.size()) { report = "Выбери загруженный клип"; return false; }
    const Loaded gone = loaded[index];
    loaded.erase(loaded.begin() + static_cast<long>(index));
    {
        RedrawOff ro(ip);
        NoAnimate na;
        // ключи клипа удаляются, время не сдвигается - остальные клипы на своих местах
        DeleteKeysIn(rig, gone.start, gone.end);
    }
    StoreLoaded(rig.root, loaded);
    char b[300];
    std::snprintf(b, sizeof b, "\"%s\" удалён (кадры %d-%d), осталось клипов: %zu", gone.name.c_str(), gone.start, gone.end, loaded.size());
    report = b;
    Log("%s", report.c_str());
    return true;
}

bool RenameSequence(ULONG handle, size_t index, const std::string& name, bool nonLooping, std::string& report) {
    SceneRig rig;
    std::string err;
    if (!ReadRig(handle, rig, err)) { report = err; return false; }
    std::vector<Loaded> loaded = CurrentLoaded(rig.root);
    if (index >= loaded.size()) { report = "Выбери загруженный клип"; return false; }
    std::string n = name;
    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.pop_back();
    while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.erase(0, 1);
    n.erase(std::remove(n.begin(), n.end(), '"'), n.end());
    if (n.empty()) { report = "Пустое имя"; return false; }
    const std::string old = loaded[index].name;
    loaded[index].name = n;
    loaded[index].nonLooping = nonLooping;
    StoreLoaded(rig.root, loaded);
    report = "\"" + old + "\" -> \"" + n + "\"" + (nonLooping ? " (NonLooping)" : "");
    return true;
}

std::vector<RigInfo> ListRigs() {
    std::vector<RigInfo> out;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr || ip->GetRootNode() == nullptr) return out;
    INode* w = ip->GetRootNode();
    for (int i = 0; i < w->NumberOfChildren(); ++i) {
        INode* n = w->GetChildNode(i);
        if (GetProp(n, "bleach_root") != "1") continue;
        RigInfo r;
        r.handle = n->GetHandle();
        r.name = FromMax(n->GetName());
        r.charId = GetProp(n, "bleach_char");
        out.push_back(r);
    }
    return out;
}

ULONG SelectedRig() {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr || ip->GetSelNodeCount() == 0) return 0;
    INode* n = ip->GetSelNode(0);
    const std::string part = GetProp(n, "bleach_part");
    for (int d = 0; n != nullptr && d < 512; ++d) {
        if (GetProp(n, "bleach_root") == "1") return n->GetHandle();
        if (n->IsRootNode()) break;
        n = n->GetParentNode();
    }
    if (!part.empty())
        for (const RigInfo& r : ListRigs()) {
            INode* rn = ip->GetINodeByHandle(r.handle);
            const std::string parts = GetProp(rn, "bleach_parts");
            if (parts.find(part) != std::string::npos) return r.handle;
        }
    return 0;
}

void ShowRange(int a, int b) {
    Interface* ip = GetCOREInterface();
    const int tpf = std::max(1, GetTicksPerFrame());
    if (b <= a) b = a + 1;
    ip->SetAnimRange(Interval(a * tpf, b * tpf));
    ip->SetTime(a * tpf, TRUE);
}

void RegisterDialog(void* hwnd, bool reg) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return;
    if (reg) ip->RegisterDlgWnd(static_cast<HWND>(hwnd));
    else ip->UnRegisterDlgWnd(static_cast<HWND>(hwnd));
}

// Мост для окон: каждое обращение к сцене завёрнуто в try/catch.
UiBridge MakeBridge() {
    UiBridge b;
    b.hInstance = hInstance;
    Interface* ip = GetCOREInterface();
    b.parent = ip ? ip->GetMAXHWnd() : nullptr;
    b.importModels = [](const std::vector<ImportItem>& items, const ImportOptions& o, std::string& rep) {
        try { return ImportModels(items, o, rep); }
        catch (const std::exception& e) { rep = std::string("Ошибка: ") + e.what(); Log("ИСКЛЮЧЕНИЕ %s", e.what()); }
        catch (...) { rep = "Неизвестная ошибка"; }
        return false;
    };
    b.listRigs = []() {
        try { return ListRigs(); } catch (...) { return std::vector<RigInfo>(); }
    };
    b.selectedRig = []() -> unsigned long {
        try { return SelectedRig(); } catch (...) { return 0; }
    };
    b.applyClips = [](unsigned long h, const tam::TactPkg& p, const std::vector<size_t>& c, const AnimOptions& o, std::string& rep) {
        try { return ApplyClips(h, p, c, o, rep); }
        catch (const std::exception& e) { rep = std::string("Ошибка: ") + e.what(); Log("ИСКЛЮЧЕНИЕ %s", e.what()); }
        catch (...) { rep = "Неизвестная ошибка"; }
        return false;
    };
    b.resetBind = [](unsigned long h, std::string& rep) {
        try { return ResetBind(h, rep); } catch (...) { rep = "Ошибка"; return false; }
    };
    b.sequences = [](unsigned long h) {
        try { return Sequences(h); } catch (...) { return std::vector<SequenceInfo>(); }
    };
    b.deleteSequence = [](unsigned long h, size_t i, std::string& rep) {
        try { return DeleteSequence(h, i, rep); } catch (...) { rep = "Ошибка"; return false; }
    };
    b.renameSequence = [](unsigned long h, size_t i, const std::string& n, bool nl, std::string& rep) {
        try { return RenameSequence(h, i, n, nl, rep); } catch (...) { rep = "Ошибка"; return false; }
    };
    b.showRange = [](int a, int e) {
        try { ShowRange(a, e); } catch (...) {}
    };
    b.registerDialog = [](void* h, bool r) {
        try { RegisterDialog(h, r); } catch (...) {}
    };
    return b;
}

UiBridge& Bridge() {
    static UiBridge b;
    b = MakeBridge();
    return b;
}

std::string LowerExt(const std::string& p) {
    const size_t d = p.find_last_of('.');
    return d == std::string::npos ? std::string() : Lower(p.substr(d));
}

} // namespace

// ============================================================
int OpenModelDialog(const std::string& preselect) {
    try {
        return ShowModelDialog(Bridge(), preselect);
    } catch (...) {
        return -1;
    }
}

void OpenAnimDialog(const std::string& preselect) {
    try {
        ShowAnimDialog(Bridge(), preselect);
    } catch (...) {
    }
}

int ImportEntry(const MCHAR* path, BOOL suppressPrompts) {
    const std::string p = FromMax(path);
    const std::string ext = LowerExt(p);
    if (ext == ".tactpkg") {
        OpenAnimDialog(p);
        return 1;
    }
    if (suppressPrompts) {
        // без окна: импорт одного файла с настройками по умолчанию
        ImportOptions o;
        std::string rep;
        std::vector<ImportItem> items;
        items.push_back({ p, 0, "" });
        return Bridge().importModels(items, o, rep) ? 1 : 0;
    }
    return OpenModelDialog(p) > 0 ? 1 : 0;
}

std::string LodInfo(const std::string& path) {
    try {
        const auto s = LodSiblings(path);
        if (s.size() <= 1) return "LOD нет (только " + (s.empty() || s[0].first.empty() ? std::string("этот файл") : s[0].first) + ")";
        std::string r = "LOD есть, уровней " + std::to_string(s.size()) + ":";
        for (const auto& x : s) r += " " + x.first;
        return r;
    } catch (...) {
        return "ошибка";
    }
}

void WriteStartLog(const std::wstring& text) {
    const std::wstring p = DataDir() + L"\\start.log";
#if defined(_MSC_VER)
    std::FILE* f = _wfopen(p.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(Utf8(p).c_str(), "wb");
#endif
    if (f == nullptr) return;
    const std::string u = Utf8(text);
    std::fwrite(u.data(), 1, u.size(), f);
    std::fclose(f);
}

} // namespace bleach
