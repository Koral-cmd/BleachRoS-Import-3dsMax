// ============================================================
//  tam_formats.h - чтение форматов TamEngine (Tamsoft) из
//  BLEACH Rebirth of Souls. Без Max SDK и без Windows: тот же код
//  использует и плагин, и консольная утилита tools/bleachdump.
//
//    PZZE      'PZZE' fmt[4] u64 decSize u64 dataOffset, затем zlib
//    .tmd2     модель ('tmd0'): вершины, треугольники, подмеши,
//              материалы, скелет (обратные bind-матрицы)
//    .lds      пачка DDS-текстур
//    .tactpkg  'actmng_pkg' -> 'acttmo_pkg' -> клипы 'tmo1'
//
//  Структуры сверены с открытым Blender-аддоном BlenderTMD2 (Al-Hydra);
//  код написан заново.
//
//  Каждое смещение проверяется по размеру буфера. Ошибки летят как
//  tam::TamError (std::runtime_error) и ловятся на границе с SDK.
// ============================================================
#pragma once
#include "tam_math.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tam {

struct TamError : std::runtime_error {
    explicit TamError(const std::string& s) : std::runtime_error(s) {}
};

uint32_t Crc(const std::string& name);   // хеш имени (как в движке)

// Прочитать файл (путь UTF-8), PZZE распаковывается.
bool ReadAll(const std::string& pathUtf8, std::vector<uint8_t>& out, std::string& err);
bool Unpack(std::vector<uint8_t>& data, std::string& err);   // PZZE -> сырые данные (иначе без изменений)

// ------------------------------------------------------------
struct Lds {
    std::string name;
    std::vector<std::vector<uint8_t>> textures;
    void Read(const std::vector<uint8_t>& d);
};

// Что внутри DDS (для лога и альфы).
struct DdsInfo {
    std::string format;
    bool alpha = false;
    int width = 0, height = 0;
};
DdsInfo InspectDds(const std::vector<uint8_t>& d);

// ------------------------------------------------------------
struct Tmd2Texture {             // ссылка на элемент .lds
    uint32_t hash = 0;
    int ldsIndex = 0;
    int width = 0, height = 0, format = 0;
};

struct Tmd2MatTexture {
    uint32_t hash = 0;
    int texture = -1;            // индекс в Tmd2::textures, -1 = внешняя
    int unk1 = 0, unk2 = 0, slot = 0;
};

struct Tmd2Material {
    int index = 0;
    uint32_t hash = 0;
    std::string shader;         // четыре символа, напр. BSS1, BHA1
    std::vector<Tmd2MatTexture> textures;
    std::vector<float> params;
};

struct Tmd2Bone {
    uint32_t hash = 0;
    std::string name;
    int parent = -1;
    float restPos[3] = { 0, 0, 0 };
    M4 invBind{};               // как в файле: обратная bind-матрица (строки-векторы)
    int extra = -1;
    float offset[3] = { 0, 0, 0 };
};

struct Tmd2Part {               // подмеш: диапазон треугольников + материал + таблица индексов
    uint32_t triCount = 0, start = 0;
    int material = -1;
    int table = -1;           // индекс в Tmd2::tables, -1 = без таблицы
};

struct Tmd2Model {
    std::string name;
    uint32_t hash = 0;
    std::vector<Tmd2Part> parts;
};

struct Tmd2 {
    std::string name, path;
    unsigned flag1 = 0, flag2 = 0, flags = 0, animFlag = 0, version = 0;
    float bbox[6] = { 0, 0, 0, 0, 0, 0 };
    uint32_t vertexCount = 0, triCountTotal = 0;
    int stride = 0;
    std::vector<std::string> layout;

    std::vector<Tmd2Texture> textures;
    std::vector<Tmd2Material> materials;
    std::vector<Tmd2Bone> bones;
    std::vector<std::vector<uint32_t>> tables;
    std::vector<Tmd2Model> models;

    std::vector<float> pos;           // 3 на вершину
    std::vector<float> nrm;           // 3 на вершину (0x4)
    std::vector<float> nrm2;          // 3 на вершину (0x100)
    std::vector<uint8_t> color;       // 4 на вершину (0x80)
    std::vector<float> uv[3];         // 2 на вершину
    std::vector<uint8_t> w0, i0, w1, i1;   // 4 на вершину
    std::vector<uint32_t> idx;        // 3 на треугольник

    bool HasSkeleton() const { return (flags & 0x2000) != 0; }
    bool HasWeights() const { return !w0.empty(); }

    // Веса вершины: (глобальный индекс кости, вес), нормированы.
    void Weights(uint32_t v, int table, std::vector<std::pair<int, float>>& out) const;

    void Read(const std::vector<uint8_t>& d);
    std::string Report() const;
};

// ------------------------------------------------------------
struct Key {
    float frame;
    float value;
};

struct Tmo {
    std::string name;
    std::string magic;
    unsigned flag1 = 0, flag2 = 0, flag3 = 0;
    float scale = 1.0f;
    int frames = 0;                   // по заголовку
    int length = 1;                   // max(frames, последний ключ + 1)
    std::vector<uint32_t> hashes;     // на кость
    // curves[кость*9 + канал]: 0..2 позиция, 3..5 поворот (эйлер), 6..8 масштаб
    std::vector<std::vector<Key>> curves;
    std::vector<std::string> warnings;
    void Read(const uint8_t* d, size_t n);
};

struct ClipEntry {
    std::string name;
    size_t ofs = 0, size = 0;      // внутри буфера пакета
    int frames = 0, bones = 0;      // из заголовка (быстро)
};

struct TactPkg {
    std::string name;
    std::vector<uint8_t> data;       // распакованное содержимое
    std::vector<ClipEntry> clips;
    std::vector<std::string> other;
    void Read();                      // из `data`
    bool Clip(size_t i, Tmo& out, std::string& err) const;
};

// Загрузчики (путь UTF-8). false + err при ошибке.
bool LoadTmd2(const std::string& path, Tmd2& out, std::string& err);
bool LoadLds(const std::string& path, Lds& out, std::string& err);
bool LoadTact(const std::string& path, TactPkg& out, std::string& err);
bool LoadTmo(const std::string& path, Tmo& out, std::string& err);

std::string FileStem(const std::string& path);   // без папки и расширения

} // namespace tam
