// ============================================================
//  bleach_dll.cpp - вход DLL: Файл -> Импорт (.tmd2 / .tactpkg)
//  и MAXScript-интерфейс BleachCpp:
//      BleachCpp.showDialog()        окно моделей
//      BleachCpp.showAnimDialog()    окно анимаций
//      BleachCpp.version()           версия .dlu
//      BleachCpp.lodInfo "путь.tmd2" есть ли LOD у файла
//
//  Экспорт функций: .def обязателен (Max ищет недекорированные имена).
// ============================================================
#include "bleach.h"

#include "bleach_core.h"

#include <iFnPub.h>
#include <notify.h>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// ------------------------------------------------------------
class BleachSceneImport : public SceneImport {
public:
    int ExtCount() override { return 2; }
    const MCHAR* Ext(int i) override { return i == 0 ? _T("tmd2") : (i == 1 ? _T("tactpkg") : _T("")); }
    const MCHAR* ShortDesc() override { return _T("BLEACH Rebirth of Souls"); }
    const MCHAR* LongDesc() override { return _T("BLEACH Rebirth of Souls: model (.tmd2) or animation pack (.tactpkg)"); }
    const MCHAR* AuthorName() override { return _T("Koral-cmd"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override { return _T(""); }
    const MCHAR* OtherMessage2() override { return _T(""); }
    unsigned int Version() override { return BLEACHIMPORT_VERSION; }
    void ShowAbout(HWND hWnd) override {
        MessageBox(hWnd, _T("BLEACH RoS Import ") BLEACHIMPORT_VERSION_STR _T("\n\nМеню Bleach Tool или Файл -> Импорт (.tmd2 / .tactpkg)."),
                   _T("BLEACH RoS Import"), MB_ICONINFORMATION);
    }
    int DoImport(const MCHAR* name, ImpInterface*, Interface*, BOOL suppressPrompts) override {
        try {
            return bleach::ImportEntry(name, suppressPrompts);
        } catch (...) {
            return 0;
        }
    }
};

class BleachSceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    // new, не статический объект: Max сам удаляет экземпляр импортёра.
    void* Create(BOOL) override { return new BleachSceneImport(); }
    const MCHAR* ClassName() override { return _T("BLEACH RoS Import"); }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("BLEACH RoS Import"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID ClassID() override { return BLEACHIMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("BleachRoSSceneImport"); }
    HINSTANCE HInstance() override { return hInstance; }
};

static BleachSceneImportClassDesc theSceneImportClassDesc;

// ------------------------------------------------------------
class BleachFP : public FPStaticInterface {
public:
    enum { fn_showDialog = 0, fn_version = 1, fn_showAnimDialog = 2, fn_lodInfo = 3 };

    BOOL showDialog() { return bleach::OpenModelDialog(std::string()) > 0 ? TRUE : FALSE; }
    const MCHAR* version() { return BLEACHIMPORT_VERSION_STR; }
    BOOL showAnimDialog() {
        bleach::OpenAnimDialog(std::string());
        return TRUE;
    }
    const MCHAR* lodInfo(const MCHAR* path) {
        static std::wstring last;
        last = bleach::Wide(bleach::LodInfo(path ? bleach::Utf8(std::wstring(path)) : std::string()));
        return last.c_str();
    }

    DECLARE_DESCRIPTOR(BleachFP)
    BEGIN_FUNCTION_MAP
        FN_0(fn_showDialog, TYPE_BOOL, showDialog)
        FN_0(fn_version, TYPE_STRING, version)
        FN_0(fn_showAnimDialog, TYPE_BOOL, showAnimDialog)
        FN_1(fn_lodInfo, TYPE_STRING, lodInfo, TYPE_STRING)
    END_FUNCTION_MAP
};

static BleachFP theBleachFP(
    BLEACHIMPORT_FP_ID, _T("BleachCpp"), 0, &theSceneImportClassDesc, FP_CORE,
    BleachFP::fn_showDialog, _T("showDialog"), 0, TYPE_BOOL, 0, 0,
    BleachFP::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    BleachFP::fn_showAnimDialog, _T("showAnimDialog"), 0, TYPE_BOOL, 0, 0,
    BleachFP::fn_lodInfo, _T("lodInfo"), 0, TYPE_STRING, 0, 1,
        _T("path"), 0, TYPE_STRING,
    p_end);

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("BLEACH RoS Import ") BLEACHIMPORT_VERSION_STR _T(" - BLEACH Rebirth of Souls importer");
}
__declspec(dllexport) int LibNumberClasses() { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) { return i == 0 ? &theSceneImportClassDesc : nullptr; }
__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

// Проверка, что интерфейс BleachCpp зарегистрирован; иначе регистрируем сами.
__declspec(dllexport) int LibInitialize() {
    static bool done = false;
    if (done) return TRUE;
    done = true;
    FPInterface* const ours = &theBleachFP;
    FPInterface* const before = GetCOREInterface(BLEACHIMPORT_FP_ID);
    std::wstring text = std::wstring(L"BleachRoSImport ") + BLEACHIMPORT_VERSION_STR + L", MAX_RELEASE " + std::to_wstring(MAX_RELEASE) + L"\r\n";
    text += L"BleachCpp: ";
    text += (before == ours) ? L"зарегистрирован" : (before != nullptr ? L"занят другим объектом" : L"не зарегистрирован");
    text += L"\r\n";
    if (before == nullptr) {
        RegisterCOREInterface(ours);
        text += (GetCOREInterface(BLEACHIMPORT_FP_ID) == ours) ? L"  зарегистрирован вручную\r\n" : L"  регистрация не удалась\r\n";
    }
    bleach::WriteStartLog(text);
    return TRUE;
}
__declspec(dllexport) int CanAutoDefer() { return FALSE; }
