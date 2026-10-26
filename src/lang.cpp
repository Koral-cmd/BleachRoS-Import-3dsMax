// ============================================================
//  lang.cpp - см. lang.h
// ============================================================
#include "lang.h"

#include <string>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace i18n {
namespace {

Lang g_lang = Lang::Russian;

struct Pair {
    const char* ru;
    const char* en;
};

const Pair kTable[] = {
#include "lang_en.inc"
};

const std::unordered_map<std::string, const char*>& Map() {
    static const std::unordered_map<std::string, const char*> m = [] {
        std::unordered_map<std::string, const char*> r;
        for (const Pair& p : kTable) r.emplace(p.ru, p.en);
        return r;
    }();
    return m;
}

} // namespace

void SetLang(Lang l) { g_lang = l; }
Lang GetLang() { return g_lang; }

const char* TR(const char* ru) {
    if (g_lang != Lang::English || ru == nullptr) return ru;
    const auto& m = Map();
    const auto it = m.find(ru);
    return it == m.end() ? ru : it->second;
}

Lang FromSetting(int v) {
    if (v == 1) return Lang::Russian;
    if (v == 2) return Lang::English;
#if defined(_WIN32)
    const LANGID id = GetUserDefaultUILanguage();
    const WORD p = PRIMARYLANGID(id);
    return (p == LANG_RUSSIAN || p == LANG_UKRAINIAN || p == LANG_BELARUSIAN || p == LANG_KAZAK) ? Lang::Russian : Lang::English;
#else
    return Lang::Russian;
#endif
}

} // namespace i18n
