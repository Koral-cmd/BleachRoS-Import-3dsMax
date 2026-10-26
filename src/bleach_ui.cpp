// ============================================================
//  bleach_ui.cpp - окна плагина (Win32, без Max SDK).
//
//  Окно моделей - модальное: папка игры, список персонажей, части
//  (тело/лицо/волосы/оружие) с колонкой LOD, опции, импорт.
//  Окно анимаций - немодальное: скелет в сцене, пакет .tactpkg,
//  клипы, загрузка одного клипа или нескольких подряд на таймлайн.
//
//  Всё, что трогает сцену, идёт через UiBridge (bleach_max.cpp).
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>

#include "bleach_core.h"
#include "bleach_res.h"
#include "bleach_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bleach {
namespace {

// ------------------------------------------------------------
//  Общее
// ------------------------------------------------------------
GameIndex g_index;                 // общий для обоих окон
bool g_indexValid = false;

void SetText(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, Wide(s).c_str()); }
void SetText(HWND dlg, int id, const char* s) { SetDlgItemTextW(dlg, id, Wide(s).c_str()); }

std::string GetText(HWND dlg, int id) {
    wchar_t buf[2048];
    GetDlgItemTextW(dlg, id, buf, 2048);
    return Utf8(buf);
}

bool Checked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
void Check(HWND dlg, int id, bool on) { CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED); }

void ComboFill(HWND dlg, int id, const std::vector<std::string>& items, int sel) {
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const std::string& s : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(s).c_str()));
    if (sel >= 0 && sel < static_cast<int>(items.size())) SendMessageW(c, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

int ComboSel(HWND dlg, int id) { return static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0)); }

std::string FormatSize(unsigned long long b) {
    char t[64];
    if (b >= 1024ull * 1024ull) std::snprintf(t, sizeof t, "%.1f MB", b / (1024.0 * 1024.0));
    else std::snprintf(t, sizeof t, "%.0f KB", b / 1024.0);
    return t;
}

bool PickFolder(HWND owner, std::string& out) {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&d))) || d == nullptr)
        return false;
    DWORD o = 0;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Папка BLEACH Rebirth of Souls");
    bool ok = false;
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it)) && it != nullptr) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p != nullptr) {
                out = Utf8(p);
                CoTaskMemFree(p);
                ok = true;
            }
            it->Release();
        }
    }
    d->Release();
    return ok;
}

bool PickFile(HWND owner, const wchar_t* filter, const wchar_t* title, std::string& out) {
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = owner;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    out = Utf8(buf);
    return true;
}

struct WaitCursor {
    HCURSOR old;
    WaitCursor() : old(SetCursor(LoadCursor(nullptr, IDC_WAIT))) {}
    ~WaitCursor() { SetCursor(old); }
};

bool EnsureIndex(Settings& st, bool force, std::string& err) {
    const std::string root = st.Get("game_folder");
    if (root.empty()) { err = "Папка игры не задана"; return false; }
    if (g_indexValid && !force && g_index.root == root) return true;
    WaitCursor wc;
    g_indexValid = ScanGame(root, g_index, err);
    if (g_indexValid)
        Log("Скан игры: %s - файлов %zu, персонажей %zu, пакетов анимаций %zu", root.c_str(), g_index.scanned,
            g_index.chars.size(), g_index.motions.size());
    return g_indexValid;
}

// ============================================================
//  Окно моделей
// ============================================================
const char* const kQualities[] = { "00HIGH", "01MIDDLE", "02LOW" };

struct ModelState {
    const UiBridge* bridge = nullptr;
    Settings st;
    std::vector<std::string> charIds;     // в списке слева
    std::string currentChar;
    std::vector<std::string> extraFiles;  // добавленные вручную
    std::string preselect;
};

std::string MainQuality(HWND dlg) {
    const int s = ComboSel(dlg, IDC_M_QUALITY);
    return (s >= 0 && s < 3) ? kQualities[s] : "00HIGH";
}

void FillChars(HWND dlg, ModelState& s) {
    HWND lb = GetDlgItem(dlg, IDC_M_CHARS);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    s.charIds.clear();
    const std::string q = Lower(GetText(dlg, IDC_M_SEARCH));
    for (const auto& kv : g_index.chars) {
        const std::string name = CharacterName(kv.first);
        std::string line = kv.first;
        if (!name.empty()) line += "  " + name;
        const int lods = kv.second.MaxLods();
        if (lods > 1) line += "  [LOD x" + std::to_string(lods) + "]";
        if (!q.empty() && Lower(line).find(q) == std::string::npos) continue;
        s.charIds.push_back(kv.first);
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(line).c_str()));
    }
    char t[160];
    std::snprintf(t, sizeof t, "Персонажей: %zu, пакетов анимаций: %zu", s.charIds.size(), g_index.motions.size());
    SetText(dlg, IDC_M_STATUS, t);
}

const Character* CurrentChar(const ModelState& s) {
    const auto it = g_index.chars.find(s.currentChar);
    return it == g_index.chars.end() ? nullptr : &it->second;
}

void FillParts(HWND dlg, ModelState& s) {
    HWND lv = GetDlgItem(dlg, IDC_M_PARTS);
    ListView_DeleteAllItems(lv);
    const Character* ch = CurrentChar(s);
    int row = 0;
    if (ch != nullptr) {
        for (size_t i = 0; i < ch->parts.size(); ++i) {
            const PartEntry& p = ch->parts[i];
            std::wstring stem = Wide(p.stem), kind = Wide(p.kind), lod = Wide(p.LodText());
            unsigned long long size = 0;
            const auto best = p.byQuality.find(MainQuality(dlg));
            size = (best != p.byQuality.end()) ? best->second.size : p.byQuality.begin()->second.size;
            std::wstring sz = Wide(FormatSize(size));
            LVITEMW it{};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = row;
            it.pszText = &stem[0];
            it.lParam = static_cast<LPARAM>(i);
            const int at = static_cast<int>(SendMessageW(lv, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it)));
            ListView_SetItemText(lv, at, 1, &kind[0]);
            ListView_SetItemText(lv, at, 2, &lod[0]);
            ListView_SetItemText(lv, at, 3, &sz[0]);
            ListView_SetCheckState(lv, at, DefaultChecked(p.kind, p.stem) ? TRUE : FALSE);
            ++row;
        }
    }
    for (size_t i = 0; i < s.extraFiles.size(); ++i) {
        std::wstring stem = Wide(tam::FileStem(s.extraFiles[i])), kind = L"файл", lod = L"-", sz = L"";
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = row;
        it.pszText = &stem[0];
        it.lParam = static_cast<LPARAM>(100000 + i);
        const int at = static_cast<int>(SendMessageW(lv, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it)));
        ListView_SetItemText(lv, at, 1, &kind[0]);
        ListView_SetItemText(lv, at, 2, &lod[0]);
        ListView_SetCheckState(lv, at, TRUE);
        ++row;
    }
    if (ch != nullptr) {
        int withLod = 0;
        for (const PartEntry& p : ch->parts)
            if (p.LodCount() > 1) ++withLod;
        char t[200];
        if (withLod > 0)
            std::snprintf(t, sizeof t, "%s: частей %zu, с LOD %d (уровней до %d)", ch->id.c_str(), ch->parts.size(), withLod, ch->MaxLods());
        else
            std::snprintf(t, sizeof t, "%s: частей %zu, LOD нет", ch->id.c_str(), ch->parts.size());
        SetText(dlg, IDC_M_STATUS, t);
    }
}

void SetAllChecks(HWND dlg, bool on) {
    HWND lv = GetDlgItem(dlg, IDC_M_PARTS);
    const int n = ListView_GetItemCount(lv);
    for (int i = 0; i < n; ++i) ListView_SetCheckState(lv, i, on ? TRUE : FALSE);
}

void LoadOptions(HWND dlg, const Settings& st) {
    Check(dlg, IDC_M_TEX, st.GetInt("tex", 1) != 0);
    Check(dlg, IDC_M_VCOL, st.GetInt("vcol", 1) != 0);
    Check(dlg, IDC_M_WELD, st.GetInt("weld", 1) != 0);
    Check(dlg, IDC_M_SKIN, st.GetInt("skin", 1) != 0);
    Check(dlg, IDC_M_ALPHA, st.GetInt("alpha", 0) != 0);
    Check(dlg, IDC_M_LODS, st.GetInt("lods", 0) != 0);
    ComboFill(dlg, IDC_M_BONESTYLE, { "Bone", "Point" }, st.GetInt("bonestyle", 0));
    ComboFill(dlg, IDC_M_UNITS, { "Авто", "Метры", "Сантиметры" }, st.GetInt("gameunits", 0));
    ComboFill(dlg, IDC_M_SCALE, { "Системные единицы", "1 м = 39.37", "1 м = 100", "1 м = 1" }, st.GetInt("scale", 0));
    ComboFill(dlg, IDC_M_QUALITY, { "00HIGH (основной)", "01MIDDLE", "02LOW" }, st.GetInt("quality", 0));
    HWND a = GetDlgItem(dlg, IDC_M_ATTACH);
    SendMessageW(a, CB_RESETCONTENT, 0, 0);
    SendMessageW(a, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"auto"));
    SendMessageW(a, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"none"));
    SetDlgItemTextW(dlg, IDC_M_ATTACH, Wide(st.Get("attach", "auto")).c_str());
}

ImportOptions ReadOptions(HWND dlg, Settings& st) {
    ImportOptions o;
    o.textures = Checked(dlg, IDC_M_TEX);
    o.vertexColors = Checked(dlg, IDC_M_VCOL);
    o.weld = Checked(dlg, IDC_M_WELD);
    o.skin = Checked(dlg, IDC_M_SKIN);
    o.alpha = Checked(dlg, IDC_M_ALPHA);
    o.lods = Checked(dlg, IDC_M_LODS);
    o.boneStyle = std::max(0, ComboSel(dlg, IDC_M_BONESTYLE));
    o.gameUnits = std::max(0, ComboSel(dlg, IDC_M_UNITS));
    const int sc = std::max(0, ComboSel(dlg, IDC_M_SCALE));
    o.unitsPerMeter = (sc == 1) ? 39.37007874015748 : (sc == 2) ? 100.0 : (sc == 3) ? 1.0 : 0.0;
    o.attachBone = GetText(dlg, IDC_M_ATTACH);
    if (o.attachBone.empty()) o.attachBone = "auto";
    st.SetInt("tex", o.textures);
    st.SetInt("vcol", o.vertexColors);
    st.SetInt("weld", o.weld);
    st.SetInt("skin", o.skin);
    st.SetInt("alpha", o.alpha);
    st.SetInt("lods", o.lods);
    st.SetInt("bonestyle", o.boneStyle);
    st.SetInt("gameunits", o.gameUnits);
    st.SetInt("scale", sc);
    st.SetInt("quality", std::max(0, ComboSel(dlg, IDC_M_QUALITY)));
    st.Set("attach", o.attachBone);
    return o;
}

void Rescan(HWND dlg, ModelState& s, bool force) {
    std::string err;
    SetText(dlg, IDC_M_FOLDER, s.st.Get("game_folder"));
    if (!EnsureIndex(s.st, force, err)) {
        SetText(dlg, IDC_M_STATUS, err);
        return;
    }
    FillChars(dlg, s);
}

void SelectChar(HWND dlg, ModelState& s, const std::string& id) {
    for (size_t i = 0; i < s.charIds.size(); ++i) {
        if (s.charIds[i] != id) continue;
        SendDlgItemMessageW(dlg, IDC_M_CHARS, LB_SETCURSEL, i, 0);
        s.currentChar = id;
        FillParts(dlg, s);
        return;
    }
}

void DoImport(HWND dlg, ModelState& s) {
    ImportOptions o = ReadOptions(dlg, s.st);
    s.st.Save();
    std::vector<ImportItem> items;
    HWND lv = GetDlgItem(dlg, IDC_M_PARTS);
    const Character* ch = CurrentChar(s);
    const std::string mainQ = MainQuality(dlg);
    const int n = ListView_GetItemCount(lv);
    for (int i = 0; i < n; ++i) {
        if (!ListView_GetCheckState(lv, i)) continue;
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = i;
        SendMessageW(lv, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&it));
        const size_t idx = static_cast<size_t>(it.lParam);
        if (idx >= 100000) {
            if (idx - 100000 < s.extraFiles.size()) items.push_back({ s.extraFiles[idx - 100000], 0, "" });
            continue;
        }
        if (ch == nullptr || idx >= ch->parts.size()) continue;
        const PartEntry& p = ch->parts[idx];
        // основной уровень: выбранное качество, иначе лучший имеющийся
        auto mainIt = p.byQuality.find(mainQ);
        if (mainIt == p.byQuality.end()) mainIt = p.byQuality.begin();
        items.push_back({ mainIt->second.path, 0, mainIt->first });
        if (o.lods) {
            int lod = 1;
            for (const auto& kv : p.byQuality) {
                if (kv.first == mainIt->first) continue;
                items.push_back({ kv.second.path, lod++, kv.first });
            }
        }
    }
    if (items.empty()) {
        SetText(dlg, IDC_M_STATUS, "Ничего не отмечено");
        return;
    }
    std::string report;
    bool ok = false;
    {
        WaitCursor wc;
        SetText(dlg, IDC_M_STATUS, "Импорт...");
        UpdateWindow(dlg);
        ok = s.bridge->importModels && s.bridge->importModels(items, o, report);
    }
    SetText(dlg, IDC_M_STATUS, ok ? "Готово. Подробности: Downloads\\Import bleach.log" : "Ошибка - см. лог");
    MessageBoxW(dlg, Wide(report).c_str(), L"BLEACH RoS Import", ok ? MB_ICONINFORMATION : MB_ICONERROR);
}

INT_PTR CALLBACK ModelProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    ModelState* s = reinterpret_cast<ModelState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        s = reinterpret_cast<ModelState*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(s));
        SetWindowTextW(dlg, L"BLEACH Rebirth of Souls - импорт модели");
        SetText(dlg, IDC_M_FOLDER_LBL, "Папка игры");
        SetText(dlg, IDC_M_BROWSE, "Обзор...");
        SetText(dlg, IDC_M_FIND, "Найти в Steam");
        SetText(dlg, IDC_M_QUAL_LBL, "Качество");
        SetText(dlg, IDC_M_SEARCH_LBL, "Поиск");
        SetText(dlg, IDC_M_RESCAN, "Обновить");
        SetText(dlg, IDC_M_ADDFILE, "Добавить .tmd2...");
        SetText(dlg, IDC_M_ALL, "Все");
        SetText(dlg, IDC_M_NONE, "Снять все");
        SetText(dlg, IDC_M_OPTGROUP, "Параметры");
        SetText(dlg, IDC_M_TEX, "Текстуры");
        SetText(dlg, IDC_M_VCOL, "Цвет вершин");
        SetText(dlg, IDC_M_WELD, "Сварить швы");
        SetText(dlg, IDC_M_SKIN, "Skin (веса)");
        SetText(dlg, IDC_M_ALPHA, "Альфа -> прозрачность");
        SetText(dlg, IDC_M_LODS, "Импортировать LOD (слои)");
        SetText(dlg, IDC_M_BONE_LBL, "Кости");
        SetText(dlg, IDC_M_UNITS_LBL, "Ед. игры");
        SetText(dlg, IDC_M_SCALE_LBL, "Масштаб");
        SetText(dlg, IDC_M_ATTACH_LBL, "Кость для оружия");
        SetText(dlg, IDC_M_ANIMS, "Анимации...");
        SetText(dlg, IDOK, "Импорт");
        SetText(dlg, IDCANCEL, "Закрыть");

        HWND lv = GetDlgItem(dlg, IDC_M_PARTS);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        const wchar_t* cols[] = { L"Часть", L"Тип", L"LOD", L"Размер" };
        const int widths[] = { 210, 60, 70, 70 };
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = const_cast<wchar_t*>(cols[i]);
            c.cx = widths[i];
            SendMessageW(lv, LVM_INSERTCOLUMNW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&c));
        }
        s->st.Load();
        LoadOptions(dlg, s->st);
        if (s->st.Get("game_folder").empty()) {
            const std::string g = FindGameFolder();
            if (!g.empty()) s->st.Set("game_folder", g);
        }
        // предвыбор по файлу (Файл -> Импорт): персонаж этого файла
        if (!s->preselect.empty()) {
            const std::string id = CharIdOf(tam::FileStem(s->preselect));
            bool known = false;
            Rescan(dlg, *s, false);
            for (const std::string& c : s->charIds)
                if (c == id) known = true;
            if (known) SelectChar(dlg, *s, id);
            else {
                s->extraFiles.push_back(s->preselect);
                FillParts(dlg, *s);
            }
        } else if (!s->st.Get("game_folder").empty()) {
            Rescan(dlg, *s, false);
            SelectChar(dlg, *s, s->st.Get("last_char"));
        } else {
            SetText(dlg, IDC_M_STATUS, "Укажи папку игры (Обзор...)");
        }
        return TRUE;
    }
    case WM_COMMAND: {
        if (s == nullptr) break;
        const int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDC_M_BROWSE: {
            std::string f;
            if (PickFolder(dlg, f)) {
                s->st.Set("game_folder", f);
                s->st.Save();
                Rescan(dlg, *s, true);
            }
            return TRUE;
        }
        case IDC_M_FIND: {
            const std::string g = FindGameFolder();
            if (g.empty()) SetText(dlg, IDC_M_STATUS, "Игра в библиотеках Steam не найдена - укажи папку вручную");
            else {
                s->st.Set("game_folder", g);
                s->st.Save();
                Rescan(dlg, *s, true);
            }
            return TRUE;
        }
        case IDC_M_RESCAN: Rescan(dlg, *s, true); return TRUE;
        case IDC_M_SEARCH:
            if (code == EN_CHANGE) FillChars(dlg, *s);
            return TRUE;
        case IDC_M_QUALITY:
            if (code == CBN_SELCHANGE) FillParts(dlg, *s);
            return TRUE;
        case IDC_M_CHARS:
            if (code == LBN_SELCHANGE) {
                const int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_M_CHARS, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(s->charIds.size())) {
                    s->currentChar = s->charIds[static_cast<size_t>(sel)];
                    s->st.Set("last_char", s->currentChar);
                    FillParts(dlg, *s);
                }
            } else if (code == LBN_DBLCLK) {
                DoImport(dlg, *s);
            }
            return TRUE;
        case IDC_M_ADDFILE: {
            std::string f;
            if (PickFile(dlg, L"Модели TamEngine (*.tmd2)\0*.tmd2\0Все файлы\0*.*\0", L"Добавить .tmd2", f)) {
                s->extraFiles.push_back(f);
                FillParts(dlg, *s);
            }
            return TRUE;
        }
        case IDC_M_ALL: SetAllChecks(dlg, true); return TRUE;
        case IDC_M_NONE: SetAllChecks(dlg, false); return TRUE;
        case IDC_M_ANIMS: {
            ReadOptions(dlg, s->st);
            s->st.Save();
            ShowAnimDialog(*s->bridge, std::string());
            return TRUE;
        }
        case IDOK: DoImport(dlg, *s); return TRUE;
        case IDCANCEL:
            ReadOptions(dlg, s->st);
            s->st.Save();
            EndDialog(dlg, 0);
            return TRUE;
        default: break;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(dlg, 0);
        return TRUE;
    default: break;
    }
    return FALSE;
}

// ============================================================
//  Окно анимаций (немодальное)
// ============================================================
struct AnimState {
    UiBridge bridge;
    Settings st;
    std::vector<RigInfo> rigs;
    std::vector<std::string> packs;       // пути в комбобоксе
    std::unique_ptr<tam::TactPkg> pkg;
    std::string pkgPath;
    std::vector<size_t> shown;            // индексы клипов в списке
    std::vector<SequenceInfo> seqs;
};

HWND g_animDlg = nullptr;
std::unique_ptr<AnimState> g_anim;

const char* const kOrders[] = { "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX" };

// Стандартные имена анимаций Warcraft 3 (подсказки в поле переименования)
const char* const kWc3Names[] = {
    "Stand", "Stand - 2", "Stand - 3", "Stand Ready", "Stand Victory", "Stand Hit", "Stand Channel",
    "Walk", "Walk Fast", "Attack", "Attack - 2", "Attack Slam", "Spell", "Spell - 2", "Spell Channel",
    "Spell Throw", "Spell Slam", "Death", "Decay", "Dissipate", "Birth", "Morph", "Portrait", "Portrait Talk",
};

bool Wc3NonLooping(const std::string& n) {
    const std::string l = Lower(n);
    if (l.compare(0, 13, "spell channel") == 0) return false;
    for (const char* p : { "attack", "spell", "death", "decay", "birth", "dissipate", "morph", "stand hit" })
        if (l.compare(0, std::char_traits<char>::length(p), p) == 0) return true;
    return false;
}

unsigned long CurrentRig(HWND dlg, const AnimState& s) {
    const int r = ComboSel(dlg, IDC_A_RIG);
    return (r >= 0 && r < static_cast<int>(s.rigs.size())) ? s.rigs[static_cast<size_t>(r)].handle : 0;
}

int SelectedSeq(HWND dlg, const AnimState& s) {
    const int i = static_cast<int>(SendDlgItemMessageW(dlg, IDC_A_SEQ, LB_GETCURSEL, 0, 0));
    return (i >= 0 && i < static_cast<int>(s.seqs.size())) ? i : -1;
}

void FillSequences(HWND dlg, AnimState& s, int select = -1) {
    const unsigned long rig = CurrentRig(dlg, s);
    s.seqs = (s.bridge.sequences && rig != 0) ? s.bridge.sequences(rig) : std::vector<SequenceInfo>();
    HWND lb = GetDlgItem(dlg, IDC_A_SEQ);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (const SequenceInfo& q : s.seqs) {
        char t[300];
        std::snprintf(t, sizeof t, "%s   [%d - %d]%s", q.name.c_str(), q.start, q.end, q.nonLooping ? "   NonLooping" : "");
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(t).c_str()));
    }
    if (select >= 0 && select < static_cast<int>(s.seqs.size())) SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    char t[200];
    std::snprintf(t, sizeof t, "Клипов: %zu. Это же - Sequences в WhiteoutDex (двойной щелчок - проиграть)", s.seqs.size());
    SetText(dlg, IDC_A_SEQ_LBL, t);
}

void SeqSelected(HWND dlg, AnimState& s) {
    const int i = SelectedSeq(dlg, s);
    if (i < 0) return;
    SetDlgItemTextW(dlg, IDC_A_NAME, Wide(s.seqs[static_cast<size_t>(i)].name).c_str());
    Check(dlg, IDC_A_NONLOOP, s.seqs[static_cast<size_t>(i)].nonLooping);
}

void PlaySeq(HWND dlg, AnimState& s) {
    const int i = SelectedSeq(dlg, s);
    if (i < 0) { SetText(dlg, IDC_A_STATUS, "Выбери загруженный клип"); return; }
    if (s.bridge.showRange) s.bridge.showRange(s.seqs[static_cast<size_t>(i)].start, s.seqs[static_cast<size_t>(i)].end);
}

void FullTimeline(HWND dlg, AnimState& s) {
    int end = 1;
    for (const SequenceInfo& q : s.seqs) end = std::max(end, q.end);
    if (s.bridge.showRange) s.bridge.showRange(0, end);
    SetText(dlg, IDC_A_STATUS, "Таймлайн 0-" + std::to_string(end));
}

void DeleteSeq(HWND dlg, AnimState& s) {
    const int i = SelectedSeq(dlg, s);
    if (i < 0 || !s.bridge.deleteSequence) { SetText(dlg, IDC_A_STATUS, "Выбери загруженный клип"); return; }
    std::string rep;
    s.bridge.deleteSequence(CurrentRig(dlg, s), static_cast<size_t>(i), rep);
    SetText(dlg, IDC_A_STATUS, rep);
    FillSequences(dlg, s, std::min(i, static_cast<int>(s.seqs.size()) - 2));
}

void RenameSeq(HWND dlg, AnimState& s) {
    const int i = SelectedSeq(dlg, s);
    if (i < 0 || !s.bridge.renameSequence) { SetText(dlg, IDC_A_STATUS, "Выбери загруженный клип"); return; }
    std::string rep;
    s.bridge.renameSequence(CurrentRig(dlg, s), static_cast<size_t>(i), GetText(dlg, IDC_A_NAME), Checked(dlg, IDC_A_NONLOOP), rep);
    SetText(dlg, IDC_A_STATUS, rep);
    FillSequences(dlg, s, i);
}

void FillClips(HWND dlg, AnimState& s) {
    HWND lb = GetDlgItem(dlg, IDC_A_CLIPS);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    s.shown.clear();
    if (!s.pkg) {
        SetText(dlg, IDC_A_COUNT, "");
        return;
    }
    const std::string q = Lower(GetText(dlg, IDC_A_SEARCH));
    for (size_t i = 0; i < s.pkg->clips.size(); ++i) {
        const tam::ClipEntry& c = s.pkg->clips[i];
        if (!q.empty() && Lower(c.name).find(q) == std::string::npos) continue;
        char t[200];
        std::snprintf(t, sizeof t, "%s   (%d кадр., %d кост.)", c.name.c_str(), c.frames, c.bones);
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(t).c_str()));
        s.shown.push_back(i);
    }
    char t[80];
    std::snprintf(t, sizeof t, "%zu из %zu", s.shown.size(), s.pkg->clips.size());
    SetText(dlg, IDC_A_COUNT, t);
}

void OpenPack(HWND dlg, AnimState& s, const std::string& path) {
    WaitCursor wc;
    std::unique_ptr<tam::TactPkg> p(new tam::TactPkg());
    std::string err;
    if (!tam::LoadTact(path, *p, err)) {
        s.pkg.reset();
        s.pkgPath.clear();
        SetText(dlg, IDC_A_STATUS, "Не открылся: " + err);
        Log("Пакет %s не открылся: %s", path.c_str(), err.c_str());
        FillClips(dlg, s);
        return;
    }
    Log("Пакет %s: клипов %zu", path.c_str(), p->clips.size());
    for (const std::string& o : p->other) Log("  прочее: %s", o.c_str());
    s.pkg = std::move(p);
    s.pkgPath = path;
    s.st.Set("last_pack", path);
    s.st.Save();
    FillClips(dlg, s);
    SetText(dlg, IDC_A_STATUS, "Клипов: " + std::to_string(s.pkg->clips.size()));
}

std::string CurrentRigChar(HWND dlg, const AnimState& s) {
    const int r = ComboSel(dlg, IDC_A_RIG);
    return (r >= 0 && r < static_cast<int>(s.rigs.size())) ? s.rigs[static_cast<size_t>(r)].charId : std::string();
}

void FillPacks(HWND dlg, AnimState& s, const std::string& prefer) {
    s.packs.clear();
    std::string err;
    if (EnsureIndex(s.st, false, err)) s.packs = MotionsFor(g_index, CurrentRigChar(dlg, s));
    const std::string last = s.st.Get("last_pack");
    if (!prefer.empty() && std::find(s.packs.begin(), s.packs.end(), prefer) == s.packs.end()) s.packs.insert(s.packs.begin(), prefer);
    if (!last.empty() && std::find(s.packs.begin(), s.packs.end(), last) == s.packs.end()) s.packs.push_back(last);
    std::vector<std::string> names;
    int sel = s.packs.empty() ? -1 : 0;
    for (size_t i = 0; i < s.packs.size(); ++i) {
        names.push_back(tam::FileStem(s.packs[i]) + "   " + s.packs[i]);
        if (!prefer.empty() && s.packs[i] == prefer) sel = static_cast<int>(i);
    }
    ComboFill(dlg, IDC_A_PACK, names, sel);
    if (sel >= 0) OpenPack(dlg, s, s.packs[static_cast<size_t>(sel)]);
    else FillClips(dlg, s);
}

void FillRigs(HWND dlg, AnimState& s) {
    s.rigs = s.bridge.listRigs ? s.bridge.listRigs() : std::vector<RigInfo>();
    const unsigned long selRig = s.bridge.selectedRig ? s.bridge.selectedRig() : 0;
    std::vector<std::string> names;
    int sel = s.rigs.empty() ? -1 : 0;
    for (size_t i = 0; i < s.rigs.size(); ++i) {
        std::string n = s.rigs[i].name;
        const std::string cn = CharacterName(s.rigs[i].charId);
        if (!cn.empty()) n += "  (" + cn + ")";
        names.push_back(n);
        if (s.rigs[i].handle == selRig) sel = static_cast<int>(i);
    }
    if (s.rigs.empty()) names.push_back("(в сцене нет скелета - сначала импортируй модель)");
    ComboFill(dlg, IDC_A_RIG, names, s.rigs.empty() ? 0 : sel);
}

AnimOptions ReadAnimOptions(HWND dlg, Settings& st) {
    AnimOptions o;
    o.mode = std::max(0, ComboSel(dlg, IDC_A_MODE));
    const int oi = std::max(0, ComboSel(dlg, IDC_A_ORDER));
    o.order = kOrders[oi < 6 ? oi : 0];
    const int fi = std::max(0, ComboSel(dlg, IDC_A_FPS));
    o.fps = (fi == 1) ? 30 : (fi == 2) ? 60 : 0;
    o.gap = std::max(1, std::atoi(GetText(dlg, IDC_A_GAP).c_str()));
    o.replace = Checked(dlg, IDC_A_APPEND);
    st.SetInt("anim_mode", o.mode);
    st.SetInt("anim_order", oi);
    st.SetInt("anim_fps", fi);
    st.SetInt("anim_gap", o.gap);
    st.SetInt("anim_replace", o.replace);
    return o;
}

void ApplySelected(HWND dlg, AnimState& s, bool all) {
    const int r = ComboSel(dlg, IDC_A_RIG);
    if (r < 0 || r >= static_cast<int>(s.rigs.size())) {
        SetText(dlg, IDC_A_STATUS, "Нет скелета в сцене");
        return;
    }
    if (!s.pkg) {
        SetText(dlg, IDC_A_STATUS, "Пакет анимаций не открыт");
        return;
    }
    if (Checked(dlg, IDC_A_APPEND) && !s.seqs.empty() &&
        MessageBoxW(dlg, Wide("Удалить уже загруженные клипы (" + std::to_string(s.seqs.size()) + ") перед загрузкой?").c_str(),
                    L"BLEACH RoS", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    std::vector<size_t> clips;
    if (all) {
        clips = s.shown;
    } else {
        HWND lb = GetDlgItem(dlg, IDC_A_CLIPS);
        const int n = static_cast<int>(SendMessageW(lb, LB_GETSELCOUNT, 0, 0));
        if (n > 0) {
            std::vector<int> sel(static_cast<size_t>(n));
            SendMessageW(lb, LB_GETSELITEMS, static_cast<WPARAM>(n), reinterpret_cast<LPARAM>(sel.data()));
            for (int i : sel)
                if (i >= 0 && i < static_cast<int>(s.shown.size())) clips.push_back(s.shown[static_cast<size_t>(i)]);
        }
    }
    if (clips.empty()) {
        SetText(dlg, IDC_A_STATUS, "Выбери клип(ы)");
        return;
    }
    const AnimOptions o = ReadAnimOptions(dlg, s.st);
    s.st.Save();
    std::string report;
    bool ok = false;
    {
        WaitCursor wc;
        SetText(dlg, IDC_A_STATUS, "Загрузка...");
        UpdateWindow(dlg);
        ok = s.bridge.applyClips && s.bridge.applyClips(s.rigs[static_cast<size_t>(r)].handle, *s.pkg, clips, o, report);
    }
    SetText(dlg, IDC_A_STATUS, report);
    FillSequences(dlg, s);
    if (!ok) MessageBoxW(dlg, Wide(report).c_str(), L"BLEACH RoS - анимации", MB_ICONWARNING);
}

// Все клипы всех пакетов персонажа подряд. Неподходящие к скелету клипы
// отбрасывает сам плагин; повторы по имени (тот же клип в двух пакетах или уже
// загруженный) пропускаются.
void LoadCharacterAll(HWND dlg, AnimState& s) {
    const unsigned long rig = CurrentRig(dlg, s);
    if (rig == 0) { SetText(dlg, IDC_A_STATUS, "Нет скелета в сцене"); return; }
    if (!s.bridge.applyClips) return;
    const std::string charId = CurrentRigChar(dlg, s);
    std::vector<std::string> packs;
    std::string err;
    if (EnsureIndex(s.st, false, err)) packs = MotionsFor(g_index, charId);
    if (packs.empty() && !s.pkgPath.empty()) packs.push_back(s.pkgPath);
    if (packs.empty()) {
        SetText(dlg, IDC_A_STATUS, "Пакеты анимаций персонажа " + charId + " не найдены (Motion\\" + charId + "*.tactpkg)");
        return;
    }
    // список клипов заранее - чтобы показать, сколько будет
    std::vector<std::unique_ptr<tam::TactPkg>> loaded;
    size_t total = 0;
    {
        WaitCursor wc;
        for (const std::string& p : packs) {
            std::unique_ptr<tam::TactPkg> pk(new tam::TactPkg());
            if (!tam::LoadTact(p, *pk, err)) { Log("Пакет %s не открылся: %s", p.c_str(), err.c_str()); continue; }
            total += pk->clips.size();
            loaded.push_back(std::move(pk));
        }
    }
    if (total == 0) { SetText(dlg, IDC_A_STATUS, "В пакетах персонажа нет клипов"); return; }
    const bool replace = Checked(dlg, IDC_A_APPEND);
    std::string q = "Загрузить все анимации " + charId + ": пакетов " + std::to_string(loaded.size()) + ", клипов " +
                    std::to_string(total) + "?\n\nНеподходящие к скелету клипы и повторы пропускаются.";
    if (replace && !s.seqs.empty()) q += "\nУже загруженные клипы (" + std::to_string(s.seqs.size()) + ") будут удалены.";
    else if (!s.seqs.empty()) q += "\nНовые клипы встанут после уже загруженных (" + std::to_string(s.seqs.size()) + ").";
    if (MessageBoxW(dlg, Wide(q).c_str(), L"BLEACH RoS", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;

    AnimOptions o = ReadAnimOptions(dlg, s.st);
    s.st.Save();
    std::set<std::string> names;
    if (!replace)
        for (const SequenceInfo& si : s.seqs) names.insert(Lower(si.name));
    size_t added = 0, skippedDup = 0, failedPacks = 0;
    std::string lastReport;
    {
        WaitCursor wc;
        bool first = true;
        for (size_t pi = 0; pi < loaded.size(); ++pi) {
            const tam::TactPkg& pk = *loaded[pi];
            std::vector<size_t> idx;
            for (size_t i = 0; i < pk.clips.size(); ++i) {
                if (!names.insert(Lower(pk.clips[i].name)).second) { ++skippedDup; continue; }
                idx.push_back(i);
            }
            if (idx.empty()) continue;
            char t[200];
            std::snprintf(t, sizeof t, "Пакет %zu/%zu: %s (%zu клипов)...", pi + 1, loaded.size(), pk.name.c_str(), idx.size());
            SetText(dlg, IDC_A_STATUS, t);
            UpdateWindow(dlg);
            o.replace = replace && first;
            std::string rep;
            if (s.bridge.applyClips(rig, pk, idx, o, rep)) {
                first = false;
                ++added;
                lastReport = rep;
            } else {
                ++failedPacks;
                Log("Пакет %s: %s", pk.name.c_str(), rep.c_str());
            }
        }
    }
    FillSequences(dlg, s);
    char t[300];
    std::snprintf(t, sizeof t, "Готово: на таймлайне клипов %zu. Пакетов с клипами: %zu, не подошло пакетов: %zu, повторов пропущено: %zu",
                  s.seqs.size(), added, failedPacks, skippedDup);
    SetText(dlg, IDC_A_STATUS, t);
    Log("%s", t);
}

INT_PTR CALLBACK AnimProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    AnimState* s = g_anim.get();
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(dlg, L"BLEACH Rebirth of Souls - анимации");
        SetText(dlg, IDC_A_RIG_LBL, "Скелет");
        SetText(dlg, IDC_A_REFRESH, "Обновить");
        SetText(dlg, IDC_A_PACK_LBL, "Пакет");
        SetText(dlg, IDC_A_OPEN, "Открыть файл...");
        SetText(dlg, IDC_A_SEARCH_LBL, "Поиск");
        SetText(dlg, IDC_A_MODE_LBL, "Режим");
        SetText(dlg, IDC_A_ORDER_LBL, "Эйлер");
        SetText(dlg, IDC_A_FPS_LBL, "FPS");
        SetText(dlg, IDC_A_GAP_LBL, "Пауза");
        SetText(dlg, IDC_A_APPEND, "Сначала удалить загруженные (иначе новые - после них)");
        SetText(dlg, IDC_A_LOAD, "Добавить выбранные");
        SetText(dlg, IDC_A_LOADALL, "Все из пакета (фильтр)");
        SetText(dlg, IDC_A_LOADCHAR, "ВСЕ анимации персонажа");
        SetText(dlg, IDC_A_BIND, "Bind-поза (удалить все)");
        SetText(dlg, IDC_A_SEQGROUP, "Загруженные клипы = Sequences WhiteoutDex (для MDX)");
        SetText(dlg, IDC_A_PLAY, "Проиграть");
        SetText(dlg, IDC_A_FULL, "Весь таймлайн");
        SetText(dlg, IDC_A_DELSEQ, "Удалить клип");
        SetText(dlg, IDC_A_NAME_LBL, "Имя");
        SetText(dlg, IDC_A_NONLOOP, "NonLooping");
        SetText(dlg, IDC_A_RENAME, "Переименовать");
        SetText(dlg, IDCANCEL, "Закрыть");
        if (s == nullptr) return TRUE;
        s->st.Load();
        ComboFill(dlg, IDC_A_MODE, { "Авто", "Дельта от bind", "Абсолютные" }, s->st.GetInt("anim_mode", 0));
        ComboFill(dlg, IDC_A_ORDER, { "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX" }, s->st.GetInt("anim_order", 0));
        ComboFill(dlg, IDC_A_FPS, { "не менять", "30", "60" }, s->st.GetInt("anim_fps", 0));
        SetText(dlg, IDC_A_GAP, std::to_string(s->st.GetInt("anim_gap", 10)));
        Check(dlg, IDC_A_APPEND, s->st.GetInt("anim_replace", 0) != 0);
        {
            HWND nc = GetDlgItem(dlg, IDC_A_NAME);
            for (const char* n : kWc3Names) SendMessageW(nc, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(n).c_str()));
        }
        FillRigs(dlg, *s);
        FillPacks(dlg, *s, s->pkgPath);
        FillSequences(dlg, *s);
        return TRUE;
    }
    case WM_COMMAND: {
        if (s == nullptr) break;
        const int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDC_A_REFRESH:
            FillRigs(dlg, *s);
            FillPacks(dlg, *s, s->pkgPath);
            FillSequences(dlg, *s);
            return TRUE;
        case IDC_A_RIG:
            if (code == CBN_SELCHANGE) {
                FillPacks(dlg, *s, std::string());
                FillSequences(dlg, *s);
            }
            return TRUE;
        case IDC_A_PACK:
            if (code == CBN_SELCHANGE) {
                const int p = ComboSel(dlg, IDC_A_PACK);
                if (p >= 0 && p < static_cast<int>(s->packs.size())) OpenPack(dlg, *s, s->packs[static_cast<size_t>(p)]);
            }
            return TRUE;
        case IDC_A_OPEN: {
            std::string f;
            if (PickFile(dlg, L"Пакеты анимаций (*.tactpkg)\0*.tactpkg\0Все файлы\0*.*\0", L"Открыть .tactpkg", f))
                FillPacks(dlg, *s, f);
            return TRUE;
        }
        case IDC_A_SEARCH:
            if (code == EN_CHANGE) FillClips(dlg, *s);
            return TRUE;
        case IDC_A_CLIPS:
            if (code == LBN_DBLCLK) ApplySelected(dlg, *s, false);
            return TRUE;
        case IDC_A_LOAD: ApplySelected(dlg, *s, false); return TRUE;
        case IDC_A_LOADCHAR: LoadCharacterAll(dlg, *s); return TRUE;
        case IDC_A_LOADALL: {
            if (s->shown.size() > 40 &&
                MessageBoxW(dlg, Wide("Загрузить " + std::to_string(s->shown.size()) + " клипов подряд? Это может занять время.").c_str(),
                            L"BLEACH RoS", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return TRUE;
            ApplySelected(dlg, *s, true);
            return TRUE;
        }
        case IDC_A_BIND: {
            const int r = ComboSel(dlg, IDC_A_RIG);
            if (r >= 0 && r < static_cast<int>(s->rigs.size()) && s->bridge.resetBind) {
                std::string rep;
                s->bridge.resetBind(s->rigs[static_cast<size_t>(r)].handle, rep);
                SetText(dlg, IDC_A_STATUS, rep);
                FillSequences(dlg, *s);
            }
            return TRUE;
        }
        case IDC_A_SEQ:
            if (code == LBN_SELCHANGE) SeqSelected(dlg, *s);
            else if (code == LBN_DBLCLK) PlaySeq(dlg, *s);
            return TRUE;
        case IDC_A_PLAY: PlaySeq(dlg, *s); return TRUE;
        case IDC_A_FULL: FullTimeline(dlg, *s); return TRUE;
        case IDC_A_DELSEQ: DeleteSeq(dlg, *s); return TRUE;
        case IDC_A_RENAME: RenameSeq(dlg, *s); return TRUE;
        case IDC_A_NAME:
            if (code == CBN_SELCHANGE) {
                const int k = ComboSel(dlg, IDC_A_NAME);
                if (k >= 0 && k < static_cast<int>(sizeof(kWc3Names) / sizeof(kWc3Names[0]))) Check(dlg, IDC_A_NONLOOP, Wc3NonLooping(kWc3Names[k]));
            }
            return TRUE;
        case IDCANCEL:
            DestroyWindow(dlg);
            return TRUE;
        default: break;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(dlg);
        return TRUE;
    case WM_DESTROY:
        if (s != nullptr && s->bridge.registerDialog) s->bridge.registerDialog(dlg, false);
        g_animDlg = nullptr;
        return TRUE;
    default: break;
    }
    return FALSE;
}

} // namespace

// ============================================================
int ShowModelDialog(const UiBridge& b, const std::string& preselectFile) {
    INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    ModelState s;
    s.bridge = &b;
    s.preselect = preselectFile;
    const INT_PTR r = DialogBoxParamW(static_cast<HINSTANCE>(b.hInstance), MAKEINTRESOURCEW(IDD_MODEL),
                                      static_cast<HWND>(b.parent), ModelProc, reinterpret_cast<LPARAM>(&s));
    return r < 0 ? -1 : 1;
}

void ShowAnimDialog(const UiBridge& b, const std::string& preselectPack) {
    if (g_animDlg != nullptr) {
        // уже открыто: обновить и показать
        if (g_anim) {
            g_anim->bridge = b;
            if (!preselectPack.empty()) FillPacks(g_animDlg, *g_anim, preselectPack);
            FillRigs(g_animDlg, *g_anim);
            FillSequences(g_animDlg, *g_anim);
        }
        ShowWindow(g_animDlg, SW_SHOW);
        SetForegroundWindow(g_animDlg);
        return;
    }
    g_anim.reset(new AnimState());
    g_anim->bridge = b;
    g_anim->pkgPath = preselectPack;
    g_animDlg = CreateDialogParamW(static_cast<HINSTANCE>(b.hInstance), MAKEINTRESOURCEW(IDD_ANIM),
                                   static_cast<HWND>(b.parent), AnimProc, 0);
    if (g_animDlg == nullptr) return;
    if (b.registerDialog) b.registerDialog(g_animDlg, true);
    ShowWindow(g_animDlg, SW_SHOW);
}

} // namespace bleach
