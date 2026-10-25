// ============================================================
//  bleach_ui.h - окна плагина (чистый Win32, без Max SDK).
//  Всё, что трогает сцену, приходит через UiBridge из bleach_max.cpp.
// ============================================================
#pragma once
#include "tam_formats.h"

#include <functional>
#include <string>
#include <vector>

namespace bleach {

struct ImportOptions {
    bool textures = true;
    bool vertexColors = true;
    bool weld = true;
    bool skin = true;
    bool alpha = false;          // альфа диффузной текстуры -> прозрачность (cutout)
    int boneStyle = 0;           // 0 = Bone, 1 = Point
    int gameUnits = 0;           // 0 = авто, 1 = метры, 2 = сантиметры
    double unitsPerMeter = 0.0;  // 0 = по системным единицам сцены
    std::string attachBone = "auto";
    bool lods = false;           // импортировать младшие уровни LOD
};

// Файл для импорта: основной (lod 0) или младший уровень (1, 2...)
struct ImportItem {
    std::string path;
    int lod = 0;
    std::string lodName;         // "01MIDDLE"
};

struct AnimOptions {
    int mode = 0;                // 0 авто, 1 delta, 2 absolute
    std::string order = "XYZ";
    int fps = 0;                 // 0 = не менять
    int gap = 10;                // кадров между клипами на таймлайне
    bool notes = true;           // note track + NeoDexSequenceData
    bool append = false;         // дописать после уже загруженных
};

struct RigInfo {
    unsigned long handle = 0;
    std::string name;            // имя корня
    std::string charId;          // pl000
};

struct SequenceInfo {
    std::string name;
    int start = 0, end = 0;      // в кадрах
};

struct UiBridge {
    void* hInstance = nullptr;
    void* parent = nullptr;      // HWND главного окна Max
    std::function<bool(const std::vector<ImportItem>&, const ImportOptions&, std::string&)> importModels;
    std::function<std::vector<RigInfo>()> listRigs;
    std::function<unsigned long()> selectedRig;
    std::function<bool(unsigned long, const tam::TactPkg&, const std::vector<size_t>&, const AnimOptions&, std::string&)> applyClips;
    std::function<bool(unsigned long, std::string&)> resetBind;
    std::function<std::vector<SequenceInfo>()> sequences;
    std::function<void(int, int)> showRange;
    std::function<void(void*, bool)> registerDialog;   // модельное окно Max: RegisterDlgWnd
};

// Окно моделей (модальное). preselectFile: .tmd2 - выбрать его персонажа.
int ShowModelDialog(const UiBridge& b, const std::string& preselectFile);
// Окно анимаций (немодальное, можно крутить таймлайн).
void ShowAnimDialog(const UiBridge& b, const std::string& preselectPack);

} // namespace bleach
