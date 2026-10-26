// ============================================================
//  tam_anim.cpp - см. tam_anim.h
// ============================================================
#include "lang.h"
#include "tam_anim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace tam {

double SampleCurve(const std::vector<Key>& k, double f, double def) {
    if (k.empty()) return def;
    if (f <= k.front().frame) return k.front().value;
    if (f >= k.back().frame) return k.back().value;
    size_t lo = 0, hi = k.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (k[mid].frame <= f) lo = mid;
        else hi = mid;
    }
    const double f0 = k[lo].frame, f1 = k[hi].frame;
    if (f1 <= f0) return k[hi].value;
    const double a = (f - f0) / (f1 - f0);
    return k[lo].value + (k[hi].value - k[lo].value) * a;
}

ClipMatch MatchClip(const Tmo& clip, const Skeleton& skel, const AnimSettings& s) {
    ClipMatch m;
    for (size_t ci = 0; ci < clip.hashes.size(); ++ci) {
        const auto it = skel.byHash.find(clip.hashes[ci]);
        if (it != skel.byHash.end()) m.matched.push_back({ static_cast<int>(ci), it->second });
        else m.missing.push_back(clip.hashes[ci]);
    }
    for (const auto& pr : m.matched) {
        const size_t base = static_cast<size_t>(pr.first) * 9;
        if (base + 9 > clip.curves.size()) continue;
        for (int k = 3; k < 6; ++k)
            for (const Key& key : clip.curves[base + static_cast<size_t>(k)])
                if (std::fabs(key.value) > m.maxRot) m.maxRot = std::fabs(key.value);
        const SkelBone& b = skel.bones[static_cast<size_t>(pr.second)];
        if (b.parent < 0) continue;
        const double bt[3] = { b.bindLocal.m[12], b.bindLocal.m[13], b.bindLocal.m[14] };
        const double bl = std::sqrt(bt[0] * bt[0] + bt[1] * bt[1] + bt[2] * bt[2]);
        if (bl < 1e-4) continue;
        double dAbs = 0, dDel = 0;
        bool any = false;
        for (int k = 0; k < 3; ++k) {
            const std::vector<Key>& c = clip.curves[base + static_cast<size_t>(k)];
            if (c.empty()) continue;
            any = true;
            const double v = c.front().value;
            dAbs += (v - bt[k]) * (v - bt[k]);
            dDel += v * v;
        }
        if (!any) continue;
        if (dAbs < dDel) ++m.votesAbs;
        else ++m.votesDelta;
    }
    m.degrees = m.maxRot > 20.0;
    if (s.mode == AnimMode::Auto) m.resolved = (m.votesAbs > m.votesDelta) ? AnimMode::Absolute : AnimMode::Delta;
    else m.resolved = s.mode;
    return m;
}

void EvaluateClip(const Tmo& clip, const Skeleton& skel, const AnimSettings& s, const ClipMatch& m,
                  std::vector<std::vector<M4>>& poses, std::vector<char>& animated) {
    const size_t nb = skel.bones.size();
    std::vector<int> curveOf(nb, -1);
    for (const auto& pr : m.matched)
        if (static_cast<size_t>(pr.first) * 9 + 9 <= clip.curves.size()) curveOf[static_cast<size_t>(pr.second)] = pr.first;
    animated.assign(nb, 0);
    for (size_t i = 0; i < nb; ++i) animated[i] = curveOf[i] >= 0 ? 1 : 0;

    const double rk = m.degrees ? 3.14159265358979323846 / 180.0 : 1.0;
    const std::vector<int> topo = skel.TopoOrder();
    const int frames = std::max(1, clip.length);
    poses.assign(static_cast<size_t>(frames), std::vector<M4>(nb, Ident()));

    for (int f = 0; f < frames; ++f) {
        std::vector<M4>& world = poses[static_cast<size_t>(f)];
        for (int i : topo) {
            const SkelBone& b = skel.bones[static_cast<size_t>(i)];
            const int cb = curveOf[static_cast<size_t>(i)];
            M4 local = b.bindLocal;
            if (cb >= 0) {
                const std::vector<Key>* c = &clip.curves[static_cast<size_t>(cb) * 9];
                double t[3], r[3], sc[3];
                for (int k = 0; k < 3; ++k) sc[k] = SampleCurve(c[6 + k], f, 1.0);
                if (m.resolved == AnimMode::Absolute) {
                    for (int k = 0; k < 3; ++k) t[k] = SampleCurve(c[k], f, b.bindLocal.m[12 + k]) * s.locScale;
                    if (!c[3].empty() || !c[4].empty() || !c[5].empty()) {
                        for (int k = 0; k < 3; ++k) r[k] = SampleCurve(c[3 + k], f, 0.0) * rk;
                        local = Trs(t, r, sc, s.order);
                    } else {
                        local = b.bindLocal;
                        for (int row = 0; row < 3; ++row)
                            for (int col = 0; col < 3; ++col) local.m[row * 4 + col] *= sc[row];
                        local.m[12] = t[0];
                        local.m[13] = t[1];
                        local.m[14] = t[2];
                    }
                } else {
                    for (int k = 0; k < 3; ++k) {
                        t[k] = SampleCurve(c[k], f, 0.0) * s.locScale;
                        r[k] = SampleCurve(c[3 + k], f, 0.0) * rk;
                    }
                    local = Mul(Trs(t, r, sc, s.order), b.bindLocal);
                }
            }
            world[static_cast<size_t>(i)] = (b.parent >= 0) ? Mul(local, world[static_cast<size_t>(b.parent)]) : local;
        }
    }
}

std::string DescribeMatch(const Tmo& clip, const ClipMatch& m) {
    char b[400];
    std::snprintf(b, sizeof b, TR("%s: кадров %d, костей в клипе %zu, найдено %zu, нет в скелете %zu, режим %s (авто: abs %d / delta %d)%s"),
                  clip.name.c_str(), clip.length, clip.hashes.size(), m.matched.size(), m.missing.size(),
                  m.resolved == AnimMode::Absolute ? "absolute" : "delta", m.votesAbs, m.votesDelta,
                  m.degrees ? TR(", поворот в градусах") : "");
    return b;
}

namespace {
bool HasKeys(const Tmo& c, size_t b) {
    for (int k = 0; k < 9; ++k) {
        const size_t ci = b * 9 + static_cast<size_t>(k);
        if (ci < c.curves.size() && !c.curves[ci].empty()) return true;
    }
    return false;
}
} // namespace

Tmo MergeClips(const std::vector<Tmo>& clips, const std::string& name) {
    Tmo r;
    r.name = name;
    r.magic = clips.empty() ? std::string("tmo1") : clips.front().magic;
    r.frames = 0;
    r.length = 1;
    std::unordered_map<uint32_t, size_t> at;
    for (const Tmo& c : clips) {
        r.frames = std::max(r.frames, c.frames);
        r.length = std::max(r.length, c.length);
        for (size_t b = 0; b < c.hashes.size(); ++b) {
            const uint32_t h = c.hashes[b];
            const auto it = at.find(h);
            if (it != at.end()) {
                // кость уже есть: заменить, только если там не было ключей
                if (!HasKeys(r, it->second) && HasKeys(c, b))
                    for (int k = 0; k < 9; ++k) {
                        const size_t ci = b * 9 + static_cast<size_t>(k);
                        r.curves[it->second * 9 + static_cast<size_t>(k)] = ci < c.curves.size() ? c.curves[ci] : std::vector<Key>();
                    }
                continue;
            }
            at[h] = r.hashes.size();
            r.hashes.push_back(h);
            for (int k = 0; k < 9; ++k) {
                const size_t ci = b * 9 + static_cast<size_t>(k);
                r.curves.push_back(ci < c.curves.size() ? c.curves[ci] : std::vector<Key>());
            }
        }
        for (const std::string& w : c.warnings) r.warnings.push_back(c.name + ": " + w);
    }
    return r;
}

std::string CommonClipName(const std::vector<std::string>& names) {
    if (names.empty()) return "clip";
    std::string p = names.front();
    for (const std::string& n : names) {
        size_t k = 0;
        while (k < p.size() && k < n.size() && p[k] == n[k]) ++k;
        p.resize(k);
    }
    while (!p.empty() && (p.back() == '_' || p.back() == '-' || p.back() == ' ' || p.back() == '.')) p.pop_back();
    if (p.size() < 3) p = names.front() + " +" + std::to_string(names.size() - 1);
    return p;
}

} // namespace tam
