// ============================================================
//  bleachdump - консольная проверка тех же читателей, что в плагине.
//
//    bleachdump model.tmd2 [ещё.tmd2 ...]   модель(и), скелет, меши
//    bleachdump pack.tactpkg|.tmo|.cat       список клипов (и эффектов/материалов)
//    bleachdump pack.tactpkg clip model.tmd2 [ещё.tmd2]
//                                            клип на скелет: совпадения, режим
//    bleachdump tex.lds                      текстуры
//    bleachdump --find <папка игры> model.tmd2 [ещё.tmd2]
//                                            где во всей игре клипы под этот скелет
//
//  Вывод - обычный текст; его удобно прислать при проблемах.
// ============================================================
#include "bleach_core.h"
#include "tam_anim.h"
#include "tam_assemble.h"
#include "tam_formats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <string>
#include <vector>

using namespace tam;

static bool EndsWith(const std::string& s, const char* e) {
    const size_t n = std::strlen(e);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != e[i]) return false;
    }
    return true;
}

static int DumpModels(const std::vector<std::string>& files, Skeleton* skelOut) {
    std::vector<Tmd2> tmds(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        std::string err;
        if (!LoadTmd2(files[i], tmds[i], err)) {
            std::printf("ERROR %s\n", err.c_str());
            return 1;
        }
        std::printf("%s", tmds[i].Report().c_str());
        for (size_t b = 0; b < tmds[i].bones.size(); ++b) {
            const Tmd2Bone& k = tmds[i].bones[b];
            std::printf("  bone %zu %s hash=%u parent=%d extra=%d\n", b, k.name.c_str(), k.hash, k.parent, k.extra);
        }
    }
    std::vector<Part> parts(tmds.size());
    for (size_t i = 0; i < tmds.size(); ++i) parts[i].tmd = &tmds[i];
    Skeleton skel;
    BuildOptions opt;
    std::vector<std::string> log;
    BuildSkeleton(parts, skel, opt, log);
    for (const std::string& l : log) std::printf("%s\n", l.c_str());
    std::printf("SKELETON %zu\n", skel.bones.size());
    for (size_t i = 0; i < skel.bones.size(); ++i) {
        const SkelBone& b = skel.bones[i];
        std::printf("  S %zu %s parent=%d world_t=(%.5f %.5f %.5f) local_t=(%.5f %.5f %.5f) src=%s\n", i, b.name.c_str(),
                    b.parent, b.bindWorld.m[12], b.bindWorld.m[13], b.bindWorld.m[14], b.bindLocal.m[12],
                    b.bindLocal.m[13], b.bindLocal.m[14], b.source.c_str());
    }
    for (size_t p = 0; p < parts.size(); ++p) {
        std::vector<PartMesh> meshes;
        BuildMeshes(parts, static_cast<int>(p), skel, opt, meshes);
        for (const PartMesh& m : meshes) {
            double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
            for (size_t v = 0; v * 3 < m.pos.size(); ++v)
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], static_cast<double>(m.pos[v * 3 + a]));
                    hi[a] = std::max(hi[a], static_cast<double>(m.pos[v * 3 + a]));
                }
            size_t wsum = m.wBone.size();
            std::printf("  MESH %s/%s verts=%zu faces=%zu mats=%zu maps=%zu weights=%zu degenerate=%zu bbox=(%.4f %.4f %.4f)-(%.4f %.4f %.4f)\n",
                        parts[p].tmd->name.c_str(), m.name.c_str(), m.pos.size() / 3, m.faces.size() / 3,
                        m.materials.size(), m.maps.size(), wsum, m.degenerate, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            if (!m.note.empty()) std::printf("    note: %s\n", m.note.c_str());
        }
    }
    if (skelOut != nullptr) *skelOut = skel;
    return 0;
}

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // русский текст в консоли
#endif
    if (const char* l = std::getenv("BLEACH_LANG"))
        if (std::string(l) == "en") i18n::SetLang(i18n::Lang::English);
    if (argc < 2) {
        std::printf("bleachdump <file.tmd2|file.tactpkg|file.lds> ...\n");
        return 1;
    }
    const std::string a1 = argv[1];
    if (a1 == "--find" && argc >= 4) {
        std::vector<std::string> files;
        for (int i = 3; i < argc; ++i) files.push_back(argv[i]);
        Skeleton skel;
        {
            std::vector<Tmd2> tmds(files.size());
            std::vector<Part> parts(files.size());
            for (size_t i = 0; i < files.size(); ++i) {
                std::string err;
                if (!LoadTmd2(files[i], tmds[i], err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
                parts[i].tmd = &tmds[i];
            }
            BuildOptions opt;
            std::vector<std::string> log;
            BuildSkeleton(parts, skel, opt, log);
        }
        std::vector<uint32_t> hashes;
        for (const SkelBone& b : skel.bones) hashes.push_back(b.hash);
        bleach::GameIndex g;
        std::string err;
        if (!bleach::ScanGame(argv[2], g, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        bleach::AnimSearch res;
        bleach::FindAnimations(g, hashes, bleach::CharIdOf(FileStem(files[0])), res, nullptr);
        std::printf("FIND bones=%zu files=%zu checked=%zu opened=%zu\n", hashes.size(), g.files.size(), res.checked, res.opened);
        for (const bleach::AnimSource& a : res.found) {
            std::printf("  found %s [%s] clips=%zu fit=%zu best=%zu/%zu\n", FileStem(a.path).c_str(), a.container.c_str(), a.clips,
                        a.fit, a.bestMatched, a.bestBones);
            for (const std::string& e : a.extras) std::printf("    fx %s\n", e.c_str());
        }
        for (const auto& r : res.related) std::printf("  related %s [%s]\n", FileStem(r.first).c_str(), r.second.c_str());
        for (const auto& kv : res.magicCount) std::printf("  magic %s %zu\n", kv.first.c_str(), kv.second);
        return 0;
    }
    if (EndsWith(a1, ".lds")) {
        Lds l;
        std::string err;
        if (!LoadLds(a1, l, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        for (size_t i = 0; i < l.textures.size(); ++i) {
            const DdsInfo d = InspectDds(l.textures[i]);
            std::printf("  tex %zu %s %dx%d alpha=%d bytes=%zu\n", i, d.format.c_str(), d.width, d.height, d.alpha ? 1 : 0,
                        l.textures[i].size());
        }
        return 0;
    }
    if (EndsWith(a1, ".tactpkg") || EndsWith(a1, ".tmo") || EndsWith(a1, ".cat") || EndsWith(a1, ".bin")) {
        TactPkg p;
        std::string err;
        if (!LoadTact(a1, p, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        std::printf("TACTPKG %s container=%s magic=%s clips=%zu\n", p.name.c_str(), p.container.c_str(), SniffMagic(a1).c_str(),
                    p.clips.size());
        for (const std::string& o : p.other) std::printf("  other %s\n", o.c_str());
        for (const SubEntry& e : p.extras) std::printf("  extra %s %s/%s size=%zu\n", e.type.c_str(), e.where.c_str(), e.name.c_str(), e.size);
        for (size_t i = 0; i < p.clips.size(); ++i) {
            std::vector<uint32_t> h;
            p.ClipBones(i, h);
            std::printf("  clip %zu %s frames=%d bones=%d hashes=%zu where=%s\n", i, p.clips[i].name.c_str(), p.clips[i].frames,
                        p.clips[i].bones, h.size(), p.clips[i].where.c_str());
        }
        if (argc >= 4) {
            const std::string clipName = argv[2];
            std::vector<std::string> files;
            for (int i = 3; i < argc; ++i) files.push_back(argv[i]);
            Skeleton skel;
            if (DumpModels(files, &skel) != 0) return 1;
            // "*" = все клипы пакета одновременно (как "Одновременно (1 клип)" в плагине)
            Tmo all;
            if (clipName == "*") {
                std::vector<Tmo> v(p.clips.size());
                std::vector<std::string> names;
                for (size_t i = 0; i < p.clips.size(); ++i) {
                    if (!p.Clip(i, v[i], err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
                    names.push_back(v[i].name);
                }
                all = MergeClips(v, CommonClipName(names));
                std::printf("MERGED %s bones=%zu length=%d\n", all.name.c_str(), all.hashes.size(), all.length);
            }
            for (size_t i = 0; i < p.clips.size(); ++i) {
                if (clipName != "*" && p.clips[i].name != clipName) continue;
                if (clipName == "*" && i > 0) break;
                Tmo t;
                if (clipName == "*") t = all;
                else if (!p.Clip(i, t, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
                AnimSettings s;
                const ClipMatch m = MatchClip(t, skel, s);
                std::printf("%s\n", DescribeMatch(t, m).c_str());
                for (const std::string& w : t.warnings) std::printf("  [!] %s\n", w.c_str());
                std::vector<std::vector<M4>> poses;
                std::vector<char> anim;
                EvaluateClip(t, skel, s, m, poses, anim);
                for (size_t b = 0; b < skel.bones.size(); ++b) {
                    const M4& w0 = poses.front()[b];
                    std::printf("  F0 %s anim=%d t=(%.5f %.5f %.5f) dAngleToBind=%.2f\n", skel.bones[b].name.c_str(), anim[b],
                                w0.m[12], w0.m[13], w0.m[14], AngleDeg(w0, skel.bones[b].bindWorld));
                }
            }
        }
        return 0;
    }
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.push_back(argv[i]);
    return DumpModels(files, nullptr);
}
