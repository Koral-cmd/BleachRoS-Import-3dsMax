// ============================================================
//  tam_assemble.h - сборка персонажа из нескольких .tmd2
//  (тело cos, лицо face, волосы hair, оружие wep, ...) в один скелет.
//  Без Max SDK; всё в игровом пространстве (Y вверх, единицы игры).
//
//  Кости сливаются по хешу: face/hair содержат копии костей тела
//  (корень, позвоночник, шея, голова) - они переиспользуются, новые кости
//  цепляются к найденному родителю. Если у части нет ни одной общей кости
//  (обычно оружие), её можно прицепить к кости руки/оружия тела.
// ============================================================
#pragma once
#include "tam_formats.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace tam {

struct SkelBone {
    uint32_t hash = 0;
    std::string name;
    int parent = -1;
    M4 bindWorld{};      // игровое пространство
    M4 bindLocal{};      // относительно родителя
    std::string source;  // из какого файла
    std::string kind;    // cos/face/hair/wep/other
    M4 attach = Ident(); // сдвиг прицепления, с которым кость добавлена (для LOD той же части)
};

struct Skeleton {
    std::vector<SkelBone> bones;
    std::unordered_map<uint32_t, int> byHash;

    int Add(uint32_t hash, const std::string& name, int parent, const M4& world, const std::string& src,
            const std::string& kind, const M4& attach = Ident());
    void Finalize();                         // посчитать bindLocal
    std::vector<int> TopoOrder() const;      // родители раньше детей
    int FindByName(const std::string& name) const;
    int GuessAttachBone() const;             // кость для оружия
};

struct Part {
    const Tmd2* tmd = nullptr;
    std::string kind;                        // cos/face/hair/wep/other
    std::vector<int> boneMap;                // кость файла -> индекс в Skeleton
    M4 attachMatrix = Ident();               // сдвиг части при прицеплении
    int attachedTo = -1;                     // кость, к которой прицеплены корни
    int staticParent = -1;                   // для меша без скелета
    // Переход "пространство файла части -> скелет" для каждой кости файла.
    // Лицо/волосы несут свои копии общих костей (шея, голова) с ДРУГОЙ
    // bind-позой; вершины и новые кости части переносятся по этим матрицам.
    std::vector<M4> boneCorr;
    double corrMax = 0.0;                    // насколько переход отличается от единичного
};

struct MapChannel {
    std::vector<float> verts;                // 3 на map-вершину
    std::vector<int> faces;                  // 3 на грань, с нуля
};

struct PartMesh {
    std::string name;
    int part = -1;
    std::vector<float> pos;                  // 3 на вершину (игровое пр-во, уже с attachMatrix)
    std::vector<int> faces;                  // 3 на грань, с нуля
    std::vector<int> matIds;                 // на грань, с нуля
    std::vector<int> materials;              // индекс материала tmd2 на matId (-1 = нет)
    std::map<int, MapChannel> maps;          // 0 = цвет вершин, 1.. = UV
    // Веса: для вершины v - пары [wStart[v], wStart[v+1])
    std::vector<uint32_t> wStart;
    std::vector<int> wBone;                  // индекс в Skeleton
    std::vector<float> wValue;
    bool skinned = false;
    int staticParent = -1;
    size_t degenerate = 0;
    std::string note;                        // диагностика (в лог)
};

struct BuildOptions {
    std::string attachBone = "auto";         // "auto" | "none" | имя кости
    bool weld = true;
    bool vertexColors = true;
};

std::string ClassifyPart(const std::string& stem);   // cos/face/hair/wep/other

// parts: по одному на .tmd2 (tmd указывает на живой объект)
void BuildSkeleton(std::vector<Part>& parts, Skeleton& skel, const BuildOptions& opt,
                   std::vector<std::string>& log);
void BuildMeshes(const std::vector<Part>& parts, int partIndex, const Skeleton& skel, const BuildOptions& opt,
                 std::vector<PartMesh>& out);

} // namespace tam
