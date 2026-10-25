// ============================================================
//  tam_assemble.cpp - см. tam_assemble.h
// ============================================================
#include "tam_assemble.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>

namespace tam {

namespace {

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool Contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

// Правая сторона по имени: "_r", "r_", "...R" и т.п.
bool RightSide(const std::string& n) {
    return Contains(n, "_r") || n.compare(0, 2, "r_") == 0 || (!n.empty() && n.back() == 'r');
}

const char* const kWeaponHints[] = { "wep", "weapon", "buki", "sword", "katana", "zanpakuto", "blade" };
const char* const kHandHints[] = { "hand_r", "r_hand", "handr", "rhand", "hand_c_r", "te_r", "r_te", "wrist_r", "r_wrist" };

} // namespace

#if defined(_MSC_VER)
#define TAM_FMT_ATTR
#else
#define TAM_FMT_ATTR __attribute__((format(printf, 1, 2)))
#endif

static std::string Format(const char* f, ...) TAM_FMT_ATTR;
static std::string Format(const char* f, ...) {
    char b[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

// ------------------------------------------------------------
int Skeleton::Add(uint32_t hash, const std::string& name, int parent, const M4& world, const std::string& src,
                  const std::string& kind, const M4& attach) {
    SkelBone b;
    b.hash = hash;
    b.name = name;
    b.parent = parent;
    b.bindWorld = world;
    b.bindLocal = world;
    b.source = src;
    b.kind = kind;
    b.attach = attach;
    bones.push_back(b);
    const int i = static_cast<int>(bones.size()) - 1;
    byHash[hash] = i;
    return i;
}

void Skeleton::Finalize() {
    for (SkelBone& b : bones) {
        if (b.parent >= 0) {
            M4 inv;
            Inverse(bones[static_cast<size_t>(b.parent)].bindWorld, inv);
            b.bindLocal = Mul(b.bindWorld, inv);
        } else {
            b.bindLocal = b.bindWorld;
        }
    }
}

std::vector<int> Skeleton::TopoOrder() const {
    const int n = static_cast<int>(bones.size());
    std::vector<int> depth(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        int k = i, d = 0;
        while (bones[static_cast<size_t>(k)].parent >= 0 && d <= n) {
            k = bones[static_cast<size_t>(k)].parent;
            ++d;
        }
        depth[static_cast<size_t>(i)] = d;
    }
    std::vector<int> o(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) o[static_cast<size_t>(i)] = i;
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return depth[static_cast<size_t>(a)] < depth[static_cast<size_t>(b)]; });
    return o;
}

int Skeleton::FindByName(const std::string& name) const {
    const std::string l = Lower(name);
    for (size_t i = 0; i < bones.size(); ++i)
        if (Lower(bones[i].name) == l) return static_cast<int>(i);
    return -1;
}

int Skeleton::GuessAttachBone() const {
    std::vector<std::string> names;
    for (const SkelBone& b : bones) names.push_back(Lower(b.name));
    for (size_t i = 0; i < names.size(); ++i)
        for (const char* h : kWeaponHints)
            if (Contains(names[i], h) && RightSide(names[i])) return static_cast<int>(i);
    for (size_t i = 0; i < names.size(); ++i)
        for (const char* h : kWeaponHints)
            if (Contains(names[i], h)) return static_cast<int>(i);
    for (size_t i = 0; i < names.size(); ++i)
        for (const char* h : kHandHints)
            if (names[i].compare(0, std::char_traits<char>::length(h), h) == 0) return static_cast<int>(i);
    for (size_t i = 0; i < names.size(); ++i)
        if (Contains(names[i], "hand") && RightSide(names[i])) return static_cast<int>(i);
    return -1;
}

std::string ClassifyPart(const std::string& stem) {
    const std::string n = Lower(stem);
    if (Contains(n, "_cos")) return "cos";
    if (Contains(n, "_face")) return "face";
    if (Contains(n, "_hair")) return "hair";
    if (Contains(n, "_wep") || Contains(n, "_wp")) return "wep";
    return "other";
}

// ------------------------------------------------------------
void BuildSkeleton(std::vector<Part>& parts, Skeleton& skel, const BuildOptions& opt, std::vector<std::string>& log) {
    // порядок: тело, лицо, волосы, прочее, оружие
    auto rank = [](const std::string& k) {
        if (k == "cos") return 0;
        if (k == "face") return 1;
        if (k == "hair") return 2;
        if (k == "wep") return 4;
        return 3;
    };
    for (Part& p : parts)
        if (p.kind.empty()) p.kind = ClassifyPart(p.tmd->name);
    std::stable_sort(parts.begin(), parts.end(), [&](const Part& a, const Part& b) { return rank(a.kind) < rank(b.kind); });

    for (Part& p : parts) {
        const Tmd2& t = *p.tmd;
        p.boneMap.assign(t.bones.size(), -1);
        if (t.bones.empty()) continue;

        std::vector<M4> worlds(t.bones.size());
        for (size_t i = 0; i < t.bones.size(); ++i) {
            if (!Inverse(t.bones[i].invBind, worlds[i]))
                log.push_back(Format("  [!] %s: вырожденная bind-матрица у кости %s", t.name.c_str(), t.bones[i].name.c_str()));
        }

        size_t shared = 0;
        double worst = 0.0;
        bool applyAttach = false;
        for (size_t i = 0; i < t.bones.size(); ++i) {
            const auto it = skel.byHash.find(t.bones[i].hash);
            if (it == skel.byHash.end()) continue;
            const SkelBone& sb = skel.bones[static_cast<size_t>(it->second)];
            // общие кости добавлены прицепленной частью (оружие) - LOD этой части едет так же
            if (shared == 0 && !IsIdent(sb.attach)) {
                p.attachMatrix = sb.attach;
                applyAttach = true;
            }
            ++shared;
            worst = std::max(worst, MaxAbsDiff(applyAttach ? Mul(worlds[i], p.attachMatrix) : worlds[i], sb.bindWorld));
        }
        if (shared && worst > 1e-3)
            log.push_back(Format("  [!] %s: bind общих костей расходится (макс. %.4f)", t.name.c_str(), worst));

        if (!skel.bones.empty() && shared == 0 && opt.attachBone != "none") {
            int ai = -1;
            if (!opt.attachBone.empty() && opt.attachBone != "auto") {
                ai = skel.FindByName(opt.attachBone);
                if (ai < 0) log.push_back(Format("  [!] кость '%s' не найдена - %s не прицеплен", opt.attachBone.c_str(), t.name.c_str()));
            } else {
                ai = skel.GuessAttachBone();
            }
            if (ai >= 0) {
                p.attachedTo = ai;
                p.attachMatrix = skel.bones[static_cast<size_t>(ai)].bindWorld;
                applyAttach = true;
                log.push_back(Format("  %s: общих костей нет -> прицеплен к '%s'", t.name.c_str(),
                                     skel.bones[static_cast<size_t>(ai)].name.c_str()));
            } else {
                log.push_back(Format("  %s: общих костей нет, кость для прицепления не найдена - остаётся в начале координат",
                                     t.name.c_str()));
            }
        }

        // добавляем в порядке "родитель раньше ребёнка"
        std::vector<char> done(t.bones.size(), 0);
        std::function<void(size_t, int)> addBone = [&](size_t i, int guard) {
            if (done[i] || guard > 4096) return;
            const Tmd2Bone& b = t.bones[i];
            if (b.parent >= 0 && !done[static_cast<size_t>(b.parent)]) addBone(static_cast<size_t>(b.parent), guard + 1);
            done[i] = 1;
            const auto it = skel.byHash.find(b.hash);
            if (it != skel.byHash.end()) {
                p.boneMap[i] = it->second;
                return;
            }
            const int parent = (b.parent >= 0) ? p.boneMap[static_cast<size_t>(b.parent)] : p.attachedTo;
            const M4 w = applyAttach ? Mul(worlds[i], p.attachMatrix) : worlds[i];
            const std::string name = b.name.empty() ? std::to_string(b.hash) : b.name;
            p.boneMap[i] = skel.Add(b.hash, name, parent, w, t.name, p.kind, applyAttach ? p.attachMatrix : Ident());
        };
        for (size_t i = 0; i < t.bones.size(); ++i) addBone(i, 0);
    }

    // статичные части (без скелета)
    for (Part& p : parts) {
        if (!p.tmd->bones.empty() || skel.bones.empty() || opt.attachBone == "none") continue;
        int ai = -1;
        if (!opt.attachBone.empty() && opt.attachBone != "auto") ai = skel.FindByName(opt.attachBone);
        else if (p.kind == "wep") ai = skel.GuessAttachBone();
        if (ai >= 0) {
            p.staticParent = ai;
            p.attachMatrix = skel.bones[static_cast<size_t>(ai)].bindWorld;
            log.push_back(Format("  %s: статичный меш прицеплен к '%s'", p.tmd->name.c_str(),
                                 skel.bones[static_cast<size_t>(ai)].name.c_str()));
        }
    }
    skel.Finalize();
}

// ------------------------------------------------------------
namespace {
struct WeldKey {
    long long k[6];
    bool operator<(const WeldKey& o) const {
        for (int i = 0; i < 6; ++i)
            if (k[i] != o.k[i]) return k[i] < o.k[i];
        return false;
    }
};
} // namespace

void BuildMeshes(const std::vector<Part>& parts, int partIndex, const Skeleton& skel, const BuildOptions& opt,
                 std::vector<PartMesh>& out) {
    (void)skel;
    const Part& p = parts[static_cast<size_t>(partIndex)];
    const Tmd2& t = *p.tmd;
    const bool applyA = !IsIdent(p.attachMatrix);
    const std::vector<float>& nsrc = !t.nrm2.empty() ? t.nrm2 : t.nrm;
    const bool skinned = !t.bones.empty() && t.HasWeights();

    std::vector<std::pair<int, float>> ws;
    for (const Tmd2Model& model : t.models) {
        if (model.parts.empty()) continue;
        PartMesh pm;
        pm.name = model.name;
        pm.part = partIndex;
        pm.skinned = skinned;
        pm.staticParent = p.staticParent;
        // скелет есть, а весов нет (жёсткое оружие/аксессуар) - вешаем на первую кость части
        if (!skinned && !t.bones.empty() && !p.boneMap.empty() && p.boneMap[0] >= 0) pm.staticParent = p.boneMap[0];
        if (skinned) pm.wStart.push_back(0);

        std::map<WeldKey, int> weldIdx;
        std::map<std::pair<uint32_t, int>, int> plainIdx;
        std::map<uint32_t, int> mapIdx;
        std::vector<uint32_t> mapVi;
        std::vector<int> mapFaces;
        std::map<int, int> matLookup;

        for (const Tmd2Part& sp : model.parts) {
            auto mit = matLookup.find(sp.material);
            if (mit == matLookup.end()) {
                mit = matLookup.emplace(sp.material, static_cast<int>(pm.materials.size())).first;
                pm.materials.push_back(sp.material);
            }
            const int mid = mit->second;
            for (uint32_t tri = sp.start; tri < sp.start + sp.triCount; ++tri) {
                int f[3], mf[3];
                for (int c = 0; c < 3; ++c) {
                    const uint32_t vi = t.idx[3ull * tri + static_cast<size_t>(c)];
                    int gi = -1;
                    bool isNew = false;
                    if (opt.weld) {
                        WeldKey key{};
                        for (int a = 0; a < 3; ++a) key.k[a] = std::llround(t.pos[3ull * vi + static_cast<size_t>(a)] * 20000.0);
                        if (!nsrc.empty())
                            for (int a = 0; a < 3; ++a) key.k[3 + a] = std::llround(nsrc[3ull * vi + static_cast<size_t>(a)] * 50.0);
                        const auto it = weldIdx.find(key);
                        if (it == weldIdx.end()) {
                            gi = static_cast<int>(pm.pos.size() / 3);
                            weldIdx.emplace(key, gi);
                            isNew = true;
                        } else {
                            gi = it->second;
                        }
                    } else {
                        const auto key = std::make_pair(vi, sp.table);
                        const auto it = plainIdx.find(key);
                        if (it == plainIdx.end()) {
                            gi = static_cast<int>(pm.pos.size() / 3);
                            plainIdx.emplace(key, gi);
                            isNew = true;
                        } else {
                            gi = it->second;
                        }
                    }
                    if (isNew) {
                        const double x = t.pos[3ull * vi], y = t.pos[3ull * vi + 1], z = t.pos[3ull * vi + 2];
                        if (applyA) {
                            double r[3];
                            TransformPoint(p.attachMatrix, x, y, z, r);
                            pm.pos.push_back(static_cast<float>(r[0]));
                            pm.pos.push_back(static_cast<float>(r[1]));
                            pm.pos.push_back(static_cast<float>(r[2]));
                        } else {
                            pm.pos.push_back(static_cast<float>(x));
                            pm.pos.push_back(static_cast<float>(y));
                            pm.pos.push_back(static_cast<float>(z));
                        }
                        if (skinned) {
                            t.Weights(vi, sp.table, ws);
                            for (const auto& pr : ws) {
                                if (pr.first < 0 || pr.first >= static_cast<int>(p.boneMap.size())) continue;
                                const int sb = p.boneMap[static_cast<size_t>(pr.first)];
                                if (sb < 0) continue;
                                pm.wBone.push_back(sb);
                                pm.wValue.push_back(pr.second);
                            }
                            pm.wStart.push_back(static_cast<uint32_t>(pm.wBone.size()));
                        }
                    }
                    f[c] = gi;
                    auto mi = mapIdx.find(vi);
                    if (mi == mapIdx.end()) {
                        mi = mapIdx.emplace(vi, static_cast<int>(mapVi.size())).first;
                        mapVi.push_back(vi);
                    }
                    mf[c] = mi->second;
                }
                if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2]) {
                    ++pm.degenerate;
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    pm.faces.push_back(f[c]);
                    mapFaces.push_back(mf[c]);
                }
                pm.matIds.push_back(mid);
            }
        }
        if (pm.faces.empty()) continue;

        for (int k = 0; k < 3; ++k) {
            if (t.uv[k].empty()) continue;
            MapChannel& ch = pm.maps[k + 1];
            ch.faces = mapFaces;
            ch.verts.reserve(mapVi.size() * 3);
            for (uint32_t vi : mapVi) {
                ch.verts.push_back(t.uv[k][2ull * vi]);
                ch.verts.push_back(1.0f - t.uv[k][2ull * vi + 1]);
                ch.verts.push_back(0.0f);
            }
        }
        if (opt.vertexColors && !t.color.empty()) {
            MapChannel& ch = pm.maps[0];
            ch.faces = mapFaces;
            ch.verts.reserve(mapVi.size() * 3);
            for (uint32_t vi : mapVi)
                for (int a = 0; a < 3; ++a) ch.verts.push_back(t.color[4ull * vi + static_cast<size_t>(a)] / 255.0f);
        }
        out.push_back(std::move(pm));
    }
}

} // namespace tam
