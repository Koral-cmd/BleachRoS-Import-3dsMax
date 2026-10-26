// ============================================================
//  bleach.h - общий заголовок плагина (версия, ID классов, входы).
// ============================================================
#pragma once

#include <max.h>
#include <iparamb2.h>
#include <impexp.h>
#include <istdplug.h>

#include <string>

#define BLEACHIMPORT_VERSION      141
#define BLEACHIMPORT_VERSION_STR  _T("1.4.1")

// Собственные ID (сгенерированы один раз, больше не менять).
#define BLEACHIMPORT_SCENE_CLASS_ID  Class_ID(0x4c1e7a93, 0x2b8d61f5)
#define BLEACHIMPORT_FP_ID           Interface_ID(0x7a3d29c4, 0x51e86b0d)

extern HINSTANCE hInstance;

namespace bleach {

// Окно моделей (preselect: путь .tmd2 из Файл -> Импорт). 1 = ок, -1 = окно не открылось.
int OpenModelDialog(const std::string& preselect);
// Окно анимаций (preselect: путь .tactpkg).
void OpenAnimDialog(const std::string& preselect);
// Файл -> Импорт: .tmd2 открывает окно моделей, .tactpkg - окно анимаций.
int ImportEntry(const MCHAR* path, BOOL suppressPrompts);
// Есть ли у файла LOD (те же файлы в 00HIGH / 01MIDDLE / 02LOW). Текст для MAXScript.
std::string LodInfo(const std::string& path);
// Запись в start.log при загрузке .dlu
void WriteStartLog(const std::wstring& text);

} // namespace bleach
