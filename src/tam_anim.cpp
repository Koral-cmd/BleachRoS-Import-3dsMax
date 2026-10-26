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
            M4 local = s.restPose ? b.restLocal : b.bindLocal;   // кость вне клипа - поза покоя
            if (cb >= 0) {
                local = b.bindLocal;
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

// ------------------------------------------------------------
//  Физика ткани
// ------------------------------------------------------------
namespace {
struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Len(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Pos(const M4& m) { return { m.m[12], m.m[13], m.m[14] }; }

// Поворот (строки-векторы), переводящий направление a в b.
M4 RotBetween(V3 a, V3 b) {
    M4 r = Ident();
    const double la = Len(a), lb = Len(b);
    if (la < 1e-9 || lb < 1e-9) return r;
    a = a * (1.0 / la);
    b = b * (1.0 / lb);
    V3 ax = Cross(a, b);
    const double s = Len(ax), c = Dot(a, b);
    if (s < 1e-9) return r;   // параллельны (противоположные - редкость, пропускаем)
    ax = ax * (1.0 / s);
    const double t = 1.0 - c;
    // столбцовая матрица Родрига; для строк-векторов - транспонированная
    const double m[3][3] = {
        { t * ax.x * ax.x + c, t * ax.x * ax.y - s * ax.z, t * ax.x * ax.z + s * ax.y },
        { t * ax.x * ax.y + s * ax.z, t * ax.y * ax.y + c, t * ax.y * ax.z - s * ax.x },
        { t * ax.x * ax.z - s * ax.y, t * ax.y * ax.z + s * ax.x, t * ax.z * ax.z + c },
    };
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = m[j][i];
    return r;
}

// Повернуть ориентацию мировой матрицы w на r (вокруг её начала).
M4 RotateWorld(const M4& w, const M4& r) {
    M4 o = w;
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c) {
            double v = 0;
            for (int k = 0; k < 3; ++k) v += w.m[row * 4 + k] * r.m[k * 4 + c];
            o.m[row * 4 + c] = v;
        }
    return o;
}
} // namespace

size_t SimulatePhysics(const Skeleton& skel, const std::vector<PhysJoint>& joints, const std::vector<PhysLink>& links,
                       const PhysicsSettings& ps, std::vector<std::vector<M4>>& poses, std::vector<char>& animated) {
    const size_t nb = skel.bones.size();
    if (poses.empty() || nb == 0 || joints.empty()) return 0;
    std::vector<int> jointOf(nb, -1);
    for (size_t j = 0; j < joints.size(); ++j) {
        const auto it = skel.byHash.find(joints[j].hash);
        if (it != skel.byHash.end() && jointOf[static_cast<size_t>(it->second)] < 0) jointOf[static_cast<size_t>(it->second)] = static_cast<int>(j);
    }
    // динамические: сустав с физическим родителем и массой. Ключи клипа у такой
    // кости (если есть) - только цель, к которой тянет упругость: в игре
    // animBlendRate = 0, т.е. кость ведёт физика.
    std::vector<char> dyn(nb, 0);
    size_t nDyn = 0;
    for (size_t b = 0; b < nb; ++b) {
        const int j = jointOf[b];
        if (j < 0) continue;
        const PhysJoint& pj = joints[static_cast<size_t>(j)];
        if (pj.parent == 0 || pj.invMass <= 0.0f || pj.isDummy) continue;
        const int p = skel.bones[b].parent;
        if (p < 0 || jointOf[static_cast<size_t>(p)] < 0 || joints[static_cast<size_t>(jointOf[static_cast<size_t>(p)])].hash != pj.parent) continue;
        dyn[b] = 1;
        ++nDyn;
    }
    if (nDyn == 0) return 0;
    // дети-динамики для "прицеливания" родителя
    std::vector<std::vector<int>> dynKids(nb);
    for (size_t b = 0; b < nb; ++b)
        if (dyn[b]) dynKids[static_cast<size_t>(skel.bones[b].parent)].push_back(static_cast<int>(b));
    std::vector<char> aims(nb, 0);
    for (size_t b = 0; b < nb; ++b) aims[b] = !dynKids[b].empty();
    // кости под влиянием физики: динамические и всё под ними (и под "прицельными")
    const std::vector<int> topo = skel.TopoOrder();
    std::vector<char> affected(nb, 0);
    for (int b : topo) {
        const int p = skel.bones[static_cast<size_t>(b)].parent;
        affected[static_cast<size_t>(b)] =
            dyn[static_cast<size_t>(b)] || (p >= 0 && (affected[static_cast<size_t>(p)] || aims[static_cast<size_t>(p)]));
    }

    std::vector<std::pair<int, int>> lk;
    std::vector<std::pair<double, double>> lkLen;
    for (const PhysLink& l : links) {
        const auto ia = skel.byHash.find(l.a), ib = skel.byHash.find(l.b);
        if (ia == skel.byHash.end() || ib == skel.byHash.end()) continue;
        if (!dyn[static_cast<size_t>(ia->second)] && !dyn[static_cast<size_t>(ib->second)]) continue;
        lk.push_back({ ia->second, ib->second });
        lkLen.push_back({ l.minLength, l.maxLength });
    }

    const size_t frames = poses.size();
    // локальные матрицы из анимации (для костей под физикой)
    auto localOf = [&](const std::vector<M4>& w, size_t b) {
        const int p = skel.bones[b].parent;
        if (p < 0) return w[b];
        M4 inv;
        Inverse(w[static_cast<size_t>(p)], inv);
        return Mul(w[b], inv);
    };
    std::vector<double> restLen(nb, 0.0);
    for (size_t b = 0; b < nb; ++b)
        if (dyn[b]) restLen[b] = Len(Pos(localOf(poses.front(), b)));

    std::vector<V3> x(nb), prev(nb);
    for (size_t b = 0; b < nb; ++b) x[b] = prev[b] = Pos(poses.front()[b]);
    const double dt = 1.0 / (ps.fps * std::max(1, ps.substeps));
    const V3 g{ 0.0, -ps.gravity * dt * dt, 0.0 };

    // один шаг: цель - жёсткое следование за (уже посчитанным) родителем
    auto step = [&](const std::vector<M4>& anim) {
        for (int bi : topo) {
            const size_t b = static_cast<size_t>(bi);
            if (!dyn[b]) continue;
            const PhysJoint& pj = joints[static_cast<size_t>(jointOf[b])];
            const size_t p = static_cast<size_t>(skel.bones[b].parent);
            const V3 pp = dyn[p] ? x[p] : Pos(anim[p]);
            const V3 target = pp + (Pos(anim[b]) - Pos(anim[p]));
            V3 v = (x[b] - prev[b]) * (1.0 - std::min(1.0, std::max(0.0, static_cast<double>(pj.damping))));
            prev[b] = x[b];
            V3 nx = x[b] + v + g * std::max(0.0, static_cast<double>(pj.gravityScale));
            nx = nx + (target - nx) * std::min(1.0, std::max(0.0, static_cast<double>(pj.elasticity)));
            // предел угла от позы покоя
            const V3 d0 = target - pp, d1 = nx - pp;
            const double lim = std::max({ std::fabs(pj.maxLimit[0]), std::fabs(pj.maxLimit[1]), std::fabs(pj.maxLimit[2]),
                                          std::fabs(pj.minLimit[0]), std::fabs(pj.minLimit[1]), std::fabs(pj.minLimit[2]) });
            const double l0 = Len(d0), l1 = Len(d1);
            if (lim > 1e-4 && lim < 3.1 && l0 > 1e-9 && l1 > 1e-9) {
                const double ang = std::acos(std::max(-1.0, std::min(1.0, Dot(d0, d1) / (l0 * l1))));
                if (ang > lim) {
                    const double k = lim / ang;
                    nx = pp + (d0 * (1.0 / l0) * (1.0 - k) + d1 * (1.0 / l1) * k) * l1;
                }
            }
            // длина до родителя
            const V3 d = nx - pp;
            const double l = Len(d);
            if (l > 1e-9 && restLen[b] > 1e-9) nx = pp + d * (restLen[b] / l);
            x[b] = nx;
        }
        // связи между цепочками
        for (size_t k = 0; k < lk.size(); ++k) {
            const size_t a = static_cast<size_t>(lk[k].first), b = static_cast<size_t>(lk[k].second);
            V3 d = x[b] - x[a];
            const double l = Len(d);
            if (l < 1e-9) continue;
            const double want = std::min(std::max(l, lkLen[k].first), lkLen[k].second);
            if (std::fabs(want - l) < 1e-9) continue;
            const double wa = dyn[a] ? 1.0 : 0.0, wb = dyn[b] ? 1.0 : 0.0;
            if (wa + wb == 0.0) continue;
            const V3 corr = d * ((l - want) / l / (wa + wb));
            x[a] = x[a] + corr * wa;
            x[b] = x[b] - corr * wb;
        }
    };

    // мировые матрицы из положений частиц (сверху вниз)
    auto solve = [&](const std::vector<M4>& anim, std::vector<M4>& out) {
        out = anim;
        for (int bi : topo) {
            const size_t b = static_cast<size_t>(bi);
            const int p = skel.bones[b].parent;
            if (!affected[b] && !aims[b]) continue;
            M4 w = (affected[b] && p >= 0) ? Mul(localOf(anim, b), out[static_cast<size_t>(p)]) : anim[b];
            if (dyn[b]) {
                w.m[12] = x[b].x;
                w.m[13] = x[b].y;
                w.m[14] = x[b].z;
            }
            if (aims[b]) {
                V3 from, to;
                for (int c : dynKids[b]) {
                    const M4 cw = Mul(localOf(anim, static_cast<size_t>(c)), w);
                    from = from + (Pos(cw) - Pos(w));
                    to = to + (x[static_cast<size_t>(c)] - Pos(w));
                }
                w = RotateWorld(w, RotBetween(from, to));
            }
            out[b] = w;
        }
    };

    for (int k = 0; k < ps.warmup; ++k) step(poses.front());
    std::vector<M4> res;
    for (size_t f = 0; f < frames; ++f) {
        for (int k = 0; k < std::max(1, ps.substeps); ++k) step(poses[f]);
        solve(poses[f], res);
        poses[f] = res;
    }
    for (size_t b = 0; b < nb; ++b)
        if (affected[b] || aims[b]) animated[b] = 1;
    return nDyn;
}

} // namespace tam
