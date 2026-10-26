// ============================================================
//  bleach_core.h - всё, что не трогает Max SDK и окна:
//  папки, настройки, лог, поиск файлов игры, имена персонажей.
// ============================================================
#pragma once
#include "lang.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bleach {

// ---- строки ----
std::wstring Wide(const std::string& utf8);
std::string Utf8(const std::wstring& w);
std::string Lower(std::string s);

// ---- папки ----
std::wstring DataDir();          // %LOCALAPPDATA%\BleachRoSImport (создаётся)
std::wstring LogPath();          // %USERPROFILE%\Downloads\Import bleach.log

// ---- лог ----
void LogOpen(bool append);       // начать новый лог (или дописать)
void Log(const char* fmt, ...);
void LogRaw(const std::string& line);

// ---- настройки (простой key=value в UTF-8) ----
struct Settings {
    std::map<std::string, std::string> v;
    void Load();
    void Save() const;
    std::string Get(const std::string& k, const std::string& def = std::string()) const;
    int GetInt(const std::string& k, int def) const;
    double GetDouble(const std::string& k, double def) const;
    void Set(const std::string& k, const std::string& val) { v[k] = val; }
    void SetInt(const std::string& k, int val) { v[k] = std::to_string(val); }
};

// Язык интерфейса из настроек (lang: 0 = как в Windows, 1 = русский, 2 = английский).
void ApplyLanguage();

// ---- игра ----
struct GameFile {
    std::string path;       // UTF-8
    std::string stem;       // pl000_cos00_00
    std::string kind;       // cos/face/hair/wep/other
    std::string quality;    // 00HIGH/01MIDDLE/02LOW/"" (папка)
    unsigned long long size = 0;
};

// Одна часть персонажа (pl000_cos00_00) во всех уровнях детализации.
// LOD в игре = та же модель в папках 00HIGH / 01MIDDLE / 02LOW.
struct PartEntry {
    std::string stem;
    std::string kind;
    std::map<std::string, GameFile> byQuality;   // "00HIGH" -> файл ("" = вне папок качества)
    std::string LodText() const;                 // "H M L"
    int LodCount() const { return static_cast<int>(byQuality.size()); }
};

struct Character {
    std::string id;         // pl000
    std::vector<GameFile> files;
    std::vector<PartEntry> parts;                // собирается после сканирования
    int MaxLods() const;                         // сколько уровней у самой "богатой" части
};

struct AnyFile {
    std::string path;       // UTF-8
    std::string ext;        // ".tactpkg" (строчные)
    unsigned long long size = 0;
};

struct GameIndex {
    std::string root;                       // папка игры
    std::map<std::string, Character> chars; // id -> файлы моделей
    std::vector<std::string> motions;       // все .tactpkg / .tmo / .tmo2
    std::vector<AnyFile> files;             // все файлы (для поиска анимаций и эффектов)
    size_t scanned = 0;
};

// ---- поиск анимаций по костям во всей папке игры ----
struct AnimSource {
    std::string path;
    std::string container;                  // actmng_pkg / CATS / ...
    size_t clips = 0, fit = 0;              // клипов всего / подходят к скелету
    size_t bestMatched = 0, bestBones = 0;  // лучший клип: совпало костей / костей в клипе
    std::vector<std::string> extras;        // acttmv/actmtl с именем объекта
    bool related = false;                   // имя файла содержит id объекта
};

struct AnimSearch {
    std::vector<AnimSource> found;                      // по убыванию fit
    std::map<std::string, size_t> extCount;             // расширение -> файлов
    std::map<std::string, size_t> magicCount;           // сигнатура внутри -> файлов
    std::vector<std::pair<std::string, std::string>> related;   // (путь, сигнатура): имя содержит id
    size_t checked = 0, opened = 0;
    bool cancelled = false;
};

// progress(сделано, всего, файл) -> false = остановить.
void FindAnimations(const GameIndex& g, const std::vector<uint32_t>& rigHashes, const std::string& rigId, AnimSearch& out,
                    const std::function<bool(size_t, size_t, const std::string&)>& progress);

std::string FindGameFolder();               // Steam: реестр + libraryfolders.vdf
bool ScanGame(const std::string& root, GameIndex& out, std::string& err);
std::vector<std::string> MotionsFor(const GameIndex& g, const std::string& charId);
std::string CharacterName(const std::string& id);   // "Ichigo Kurosaki" или ""
std::string CharIdOf(const std::string& stem);      // pl000_cos00_00 -> pl000
bool DefaultChecked(const std::string& kind, const std::string& stem);   // отмечать по умолчанию
std::string QualityShort(const std::string& q);     // 00HIGH -> H
// Тот же файл в других папках качества (LOD): [(папка, путь)], включая сам файл.
std::vector<std::pair<std::string, std::string>> LodSiblings(const std::string& path);

} // namespace bleach
