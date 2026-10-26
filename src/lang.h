// ============================================================
//  lang.h - язык интерфейса (русский / английский).
//
//  Все тексты в коде написаны по-русски и обёрнуты в TR("...").
//  В английском режиме TR ищет перевод в таблице lang_en.inc;
//  нет перевода - остаётся русский текст.
// ============================================================
#pragma once
#include <string>

namespace i18n {

enum class Lang { Russian, English };

void SetLang(Lang l);
Lang GetLang();
inline bool English() { return GetLang() == Lang::English; }

// Перевод (UTF-8). Указатель живёт до конца программы.
const char* TR(const char* ru);

// Выбор из настройки: 0 = как в Windows, 1 = русский, 2 = английский.
Lang FromSetting(int v);

} // namespace i18n

using i18n::TR;
