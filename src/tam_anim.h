// ============================================================
//  tam_anim.h - наложение клипа TMO на скелет (без Max SDK).
//
//  У каждой кости в клипе 9 кривых: позиция XYZ, поворот XYZ (эйлер),
//  масштаб XYZ. Две возможные трактовки значений:
//    Delta     - смещение от bind-позы (так делает Blender-аддон):
//                local = D(S,R,T) * bindLocal
//    Absolute  - полная локальная трансформация относительно родителя:
//                local = S * R * T  (недостающие каналы берутся из bind)
//  Режим Auto выбирает по первым ключам позиции: что ближе - к нулю или
//  к bind-смещению кости.
// ============================================================
#pragma once
#include "tam_assemble.h"

#include <string>
#include <vector>

namespace tam {

enum class AnimMode { Auto = 0, Delta = 1, Absolute = 2 };

struct AnimSettings {
    AnimMode mode = AnimMode::Auto;
    char order[4] = "XYZ";
    double locScale = 1.0;      // доп. множитель позиций
    bool restPose = true;       // кости вне клипа - в позе покоя (extra offset), а не в bind
};

struct ClipMatch {
    std::vector<std::pair<int, int>> matched;   // (кость клипа, кость скелета)
    std::vector<uint32_t> missing;              // хеши без кости в сцене
    int votesAbs = 0, votesDelta = 0;
    AnimMode resolved = AnimMode::Delta;
    bool degrees = false;                       // значения поворота похожи на градусы
    double maxRot = 0.0;
};

ClipMatch MatchClip(const Tmo& clip, const Skeleton& skel, const AnimSettings& s);

// Позы всех костей скелета в игровом пространстве: poses[кадр][кость]
// = мировая матрица. animated[i] = true, если кость есть в клипе.
void EvaluateClip(const Tmo& clip, const Skeleton& skel, const AnimSettings& s, const ClipMatch& m,
                  std::vector<std::vector<M4>>& poses, std::vector<char>& animated);

// Подходит ли клип к скелету: совпавшие кости покрывают хотя бы треть клипа
// или треть скелета. Одно правило для загрузки и для поиска.
inline bool ClipFits(size_t matched, size_t clipBones, size_t skelBones) {
    return matched > 0 && (matched * 3 >= clipBones || matched * 3 >= skelBones);
}

// Несколько клипов в один (части одного объекта, которые играют вместе):
// объединение костей, у общей кости берутся кривые первого клипа с ключами.
Tmo MergeClips(const std::vector<Tmo>& clips, const std::string& name);
// Общее имя: "garganta_line" + "garganta_outer" -> "garganta".
std::string CommonClipName(const std::vector<std::string>& names);

// Физика ткани по .cphy: кости-суставы (плащ, ленты, рукава, юбка, волосы)
// запекаются в позы poses; поза клипа - цель упругости: инерция, затухание, гравитация,
// упругость к позе покоя, предел угла, длины связей. Корни цепочек идут за
// анимацией. animated[] у затронутых костей становится 1.
struct PhysicsSettings {
    double fps = 30.0;          // кадров клипа в секунду
    int substeps = 2;           // шагов симуляции на кадр (игра - 60 Гц)
    int warmup = 30;            // шагов "успокоения" на первой позе
    double gravity = 9.8;       // в единицах игры (метры) / с^2
};
size_t SimulatePhysics(const Skeleton& skel, const std::vector<PhysJoint>& joints, const std::vector<PhysLink>& links,
                       const PhysicsSettings& ps, std::vector<std::vector<M4>>& poses, std::vector<char>& animated);

double SampleCurve(const std::vector<Key>& k, double f, double def);

std::string DescribeMatch(const Tmo& clip, const ClipMatch& m);

} // namespace tam
