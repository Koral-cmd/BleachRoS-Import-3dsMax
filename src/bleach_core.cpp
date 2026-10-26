// ============================================================
//  bleach_core.cpp - см. bleach_core.h
// ============================================================
#include "bleach_core.h"

#include "tam_anim.h"
#include "tam_assemble.h"
#include "tam_formats.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace bleach {

// ------------------------------------------------------------
//  UTF-8 <-> UTF-16 без WinAPI (чтобы тот же код собирался везде)
// ------------------------------------------------------------
std::wstring Wide(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu); n = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 30 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            n = 4;
        } else { cp = 0xFFFD; n = 1; }
        i += static_cast<size_t>(n);
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            w += static_cast<wchar_t>(0xD800 + (cp >> 10));
            w += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            w += static_cast<wchar_t>(cp);
        }
    }
    return w;
}

std::string Utf8(const std::wstring& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = static_cast<uint32_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            const uint32_t c2 = static_cast<uint32_t>(w[i + 1]);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); ++i; }
        }
        if (c < 0x80) s += static_cast<char>(c);
        else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            s += static_cast<char>(0xE0 | (c >> 12));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (c >> 18));
            s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// ------------------------------------------------------------
namespace {
std::wstring Env(const wchar_t* name) {
#if defined(_WIN32)
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(name, buf, 2048);
    if (n > 0 && n < 2048) return std::wstring(buf, n);
    return std::wstring();
#else
    const char* v = std::getenv(Utf8(name).c_str());
    return v ? Wide(v) : std::wstring();
#endif
}
} // namespace

std::wstring DataDir() {
    std::wstring base = Env(L"LOCALAPPDATA");
    if (base.empty()) base = Env(L"HOME");
    if (base.empty()) base = L".";
    const std::wstring d = base + L"\\BleachRoSImport";
    std::error_code ec;
#if defined(_WIN32)
    fs::create_directories(fs::path(d), ec);
#else
    fs::create_directories(fs::path(Utf8(d)), ec);
#endif
    return d;
}

std::wstring LogPath() {
    const std::wstring o = Env(L"BLEACHIMPORT_LOG");
    if (!o.empty()) return o;
    std::wstring u = Env(L"USERPROFILE");
    if (!u.empty()) {
        std::error_code ec;
        const std::wstring dl = u + L"\\Downloads";
#if defined(_WIN32)
        if (fs::is_directory(fs::path(dl), ec)) return dl + L"\\Import bleach.log";
#endif
    }
    return DataDir() + L"\\import.log";
}

// ------------------------------------------------------------
//  Лог
// ------------------------------------------------------------
namespace {
std::FILE* OpenW(const std::wstring& p, const char* mode) {
#if defined(_WIN32)
    const std::wstring m = Wide(mode);
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(Utf8(p).c_str(), mode);
#endif
}
} // namespace

void LogOpen(bool append) {
    std::FILE* f = OpenW(LogPath(), append ? "ab" : "wb");
    if (f) {
        if (!append) std::fputs("\xEF\xBB\xBF", f);   // BOM, чтобы Блокнот открыл UTF-8
        std::fclose(f);
    }
}

void LogRaw(const std::string& line) {
    std::FILE* f = OpenW(LogPath(), "ab");
    if (!f) return;
    std::fputs(line.c_str(), f);
    std::fputs("\r\n", f);
    std::fclose(f);
}

void Log(const char* fmt, ...) {
    char b[2048];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    LogRaw(b);
}

// ------------------------------------------------------------
//  Настройки
// ------------------------------------------------------------
static std::wstring SettingsPath() { return DataDir() + L"\\settings.ini"; }

void Settings::Load() {
    v.clear();
    std::FILE* f = OpenW(SettingsPath(), "rb");
    if (!f) return;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    std::fclose(f);
    std::istringstream in(all);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == ';' || line[0] == '[') continue;
        v[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void Settings::Save() const {
    std::FILE* f = OpenW(SettingsPath(), "wb");
    if (!f) return;
    for (const auto& kv : v) {
        std::fputs(kv.first.c_str(), f);
        std::fputc('=', f);
        std::fputs(kv.second.c_str(), f);
        std::fputs("\r\n", f);
    }
    std::fclose(f);
}

std::string Settings::Get(const std::string& k, const std::string& def) const {
    const auto it = v.find(k);
    return it == v.end() ? def : it->second;
}

int Settings::GetInt(const std::string& k, int def) const {
    const auto it = v.find(k);
    if (it == v.end() || it->second.empty()) return def;
    char* e = nullptr;
    const long r = std::strtol(it->second.c_str(), &e, 10);
    return (e && *e == 0) ? static_cast<int>(r) : def;
}

double Settings::GetDouble(const std::string& k, double def) const {
    const auto it = v.find(k);
    if (it == v.end() || it->second.empty()) return def;
    // десятичная точка независимо от локали (Max ставит локаль пользователя)
    std::string s = it->second;
    std::replace(s.begin(), s.end(), ',', '.');
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    double r = def;
    in >> r;
    return in.fail() ? def : r;
}

// ------------------------------------------------------------
//  Поиск игры
// ------------------------------------------------------------
namespace {
const char* const kGameDir = "BLEACH Rebirth of Souls";

#if defined(_WIN32)
std::wstring RegString(HKEY root, const wchar_t* key, const wchar_t* val) {
    wchar_t buf[1024];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, key, val, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return buf;
    return std::wstring();
}
#endif

bool IsDir(const std::string& p) {
    std::error_code ec;
    return fs::is_directory(fs::u8path(p), ec);
}
} // namespace

std::string FindGameFolder() {
    std::vector<std::string> steams;
#if defined(_WIN32)
    const std::wstring a = RegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    const std::wstring b = RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (!a.empty()) steams.push_back(Utf8(a));
    if (!b.empty()) steams.push_back(Utf8(b));
    steams.push_back("C:\\Program Files (x86)\\Steam");
#endif
    std::vector<std::string> libs;
    for (const std::string& s : steams) {
        libs.push_back(s);
        std::ifstream f(fs::u8path(s + "/steamapps/libraryfolders.vdf"));
        std::string line;
        while (std::getline(f, line)) {
            const size_t k = line.find("\"path\"");
            if (k == std::string::npos) continue;
            const size_t q1 = line.find('"', k + 6);
            const size_t q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string p = line.substr(q1 + 1, q2 - q1 - 1);
            std::string clean;
            for (size_t i = 0; i < p.size(); ++i) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') { clean += '\\'; ++i; }
                else clean += p[i];
            }
            libs.push_back(clean);
        }
    }
    for (const std::string& l : libs) {
        const std::string g = l + "\\steamapps\\common\\" + kGameDir;
        if (IsDir(g)) return g;
    }
    return std::string();
}

std::string CharIdOf(const std::string& stem) {
    const size_t u = stem.find('_');
    return Lower(u == std::string::npos ? stem : stem.substr(0, u));
}

bool DefaultChecked(const std::string& kind, const std::string& stem) {
    if (kind == "other") return false;
    const std::string s = Lower(stem);
    return s.size() >= 5 && s.compare(s.size() - 5, 5, "00_00") == 0;
}

std::string QualityShort(const std::string& q) {
    const std::string l = Lower(q);
    if (l.find("high") != std::string::npos) return "H";
    if (l.find("middle") != std::string::npos || l.find("mid") != std::string::npos) return "M";
    if (l.find("low") != std::string::npos) return "L";
    return q.empty() ? "-" : q;
}

std::vector<std::pair<std::string, std::string>> LodSiblings(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> r;
    static const char* const kQ[] = { "00HIGH", "01MIDDLE", "02LOW" };
    std::string found;
    size_t at = std::string::npos;
    for (const char* q : kQ) {
        const size_t p = path.find(std::string("\\") + q + "\\");
        const size_t p2 = path.find(std::string("/") + q + "/");
        const size_t x = (p != std::string::npos) ? p : p2;
        if (x != std::string::npos) { found = q; at = x + 1; break; }
    }
    if (found.empty()) {
        r.push_back({ "", path });
        return r;
    }
    for (const char* q : kQ) {
        const std::string cand = path.substr(0, at) + q + path.substr(at + found.size());
        std::error_code ec;
        if (fs::exists(fs::u8path(cand), ec)) r.push_back({ q, cand });
    }
    return r;
}

std::string PartEntry::LodText() const {
    std::string r;
    for (const auto& kv : byQuality) {
        if (!r.empty()) r += ' ';
        r += QualityShort(kv.first);
    }
    return r;
}

int Character::MaxLods() const {
    int m = 0;
    for (const PartEntry& p : parts) m = std::max(m, p.LodCount());
    return m;
}

bool ScanGame(const std::string& root, GameIndex& out, std::string& err) {
    out = GameIndex();
    out.root = root;
    std::error_code ec;
    const fs::path r = fs::u8path(root);
    if (!fs::is_directory(r, ec)) { err = "Папка не найдена: " + root; return false; }
    fs::recursive_directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) { err = ec.message(); return false; }
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        ++out.scanned;
        const fs::path& p = it->path();
        const std::string ext = Lower(p.extension().u8string());
        {
            AnyFile a;
            a.path = p.u8string();
            a.ext = ext;
            a.size = it->file_size(ec);
            out.files.push_back(a);
        }
        if (ext == ".tactpkg" || ext == ".tmo" || ext == ".tmo2") {   // .tmo2 - анимации объектов (adv_object)
            out.motions.push_back(p.u8string());
            continue;
        }
        if (ext != ".tmd2") continue;
        GameFile g;
        g.path = p.u8string();
        g.stem = p.stem().u8string();
        g.kind = tam::ClassifyPart(g.stem);
        g.size = it->file_size(ec);
        for (const auto& part : p) {
            const std::string c = part.u8string();
            if (c.size() > 2 && c[0] >= '0' && c[0] <= '9' && c[1] >= '0' && c[1] <= '9' && c[2] >= 'A' && c[2] <= 'Z') g.quality = c;
        }
        Character& ch = out.chars[CharIdOf(g.stem)];
        ch.id = CharIdOf(g.stem);
        ch.files.push_back(g);
    }
    for (auto& kv : out.chars) {
        Character& ch = kv.second;
        std::sort(ch.files.begin(), ch.files.end(),
                  [](const GameFile& a, const GameFile& b) { return a.stem != b.stem ? a.stem < b.stem : a.quality < b.quality; });
        for (const GameFile& f : ch.files) {
            if (ch.parts.empty() || Lower(ch.parts.back().stem) != Lower(f.stem)) {
                PartEntry p;
                p.stem = f.stem;
                p.kind = f.kind;
                ch.parts.push_back(p);
            }
            ch.parts.back().byQuality[f.quality] = f;
        }
    }
    std::sort(out.motions.begin(), out.motions.end());
    std::sort(out.files.begin(), out.files.end(), [](const AnyFile& a, const AnyFile& b) { return a.path < b.path; });
    return true;
}

std::vector<std::string> MotionsFor(const GameIndex& g, const std::string& charId) {
    std::vector<std::string> r;
    const std::string id = Lower(charId);
    for (const std::string& m : g.motions) {
        const std::string stem = Lower(fs::u8path(m).stem().u8string());
        if (id.empty()) continue;
        // pl000*.tactpkg; для объектов (garganta...) - имя где угодно в названии
        if (stem.compare(0, id.size(), id) == 0 || (id.size() > 5 && stem.find(id) != std::string::npos)) r.push_back(m);
    }
    return r;
}

namespace {
bool SkipExt(const std::string& e) {
    static const char* const k[] = { ".tmd2", ".lds", ".dds", ".usm", ".awb", ".acb", ".wem", ".bnk", ".bank", ".exe", ".dll",
                                     ".pdb", ".ini", ".txt", ".json", ".xml", ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".ogg",
                                     ".wav", ".mp4", ".webm", ".bik", ".ttf", ".otf", ".ico", ".log", ".cfg", ".vdf", ".manifest",
                                     ".url", ".lnk", ".html", ".htm", ".pdf", ".md" };
    for (const char* x : k)
        if (e == x) return true;
    return false;
}

bool AnimMagic(const std::string& m) {
    return m.compare(0, 3, "act") == 0 || m.compare(0, 4, "CATS") == 0 || m.compare(0, 3, "tmo") == 0;
}
} // namespace

void FindAnimations(const GameIndex& g, const std::vector<uint32_t>& rigHashes, const std::string& rigId, AnimSearch& out,
                    const std::function<bool(size_t, size_t, const std::string&)>& progress) {
    out = AnimSearch();
    const std::unordered_set<uint32_t> rig(rigHashes.begin(), rigHashes.end());
    const std::string id = Lower(rigId);
    const size_t total = g.files.size();
    std::vector<uint32_t> h;
    for (size_t fi = 0; fi < total; ++fi) {
        const AnyFile& f = g.files[fi];
        ++out.extCount[f.ext.empty() ? std::string("(без расширения)") : f.ext];
        const std::string stem = Lower(tam::FileStem(f.path));
        const bool related = id.size() >= 3 && stem.find(id) != std::string::npos;
        if (progress && (fi % 16) == 0 && !progress(fi, total, f.path)) { out.cancelled = true; break; }
        if (SkipExt(f.ext) && !related) continue;
        ++out.checked;
        const std::string magic = tam::SniffMagic(f.path);
        ++out.magicCount[magic.empty() ? std::string("(пусто)") : magic];
        if (related) out.related.push_back({ f.path, magic });
        // контейнеры анимаций; файлы с именем объекта - ещё и по сигнатуре tmo внутри
        const bool modelOrTex = magic == "tmd0" || magic == "DDS " || f.ext == ".lds";
        if (!AnimMagic(magic) && f.ext != ".tactpkg" && !(related && !modelOrTex)) continue;
        if (f.size > (768ull << 20)) continue;
        tam::TactPkg pk;
        std::string err;
        if (!tam::LoadTact(f.path, pk, err)) continue;
        ++out.opened;
        AnimSource src;
        src.path = f.path;
        src.related = related;
        src.container = pk.container;
        src.clips = pk.clips.size();
        for (size_t i = 0; i < pk.clips.size(); ++i) {
            if (!pk.ClipBones(i, h)) continue;
            size_t m = 0;
            for (uint32_t x : h)
                if (rig.count(x)) ++m;
            // для поиска строже: клипы из 2-3 общих корневых костей (камера, root) не в счёт
            const bool strong = m >= 8 || m == rig.size() || (m == h.size() && m * 2 >= rig.size());
            if (tam::ClipFits(m, h.size(), rig.size()) && strong) ++src.fit;
            if (m > src.bestMatched) { src.bestMatched = m; src.bestBones = h.size(); }
        }
        if (!id.empty())
            for (const tam::SubEntry& e : pk.extras)
                if (Lower(e.name).find(id) != std::string::npos || Lower(e.where).find(id) != std::string::npos)
                    src.extras.push_back(e.type + " " + e.where + "/" + e.name + " (" + std::to_string(e.size) + " байт)");
        if (src.fit > 0 || !src.extras.empty()) out.found.push_back(src);
    }
    // выше: файлы с именем объекта, потом по полноте совпадения лучшего клипа
    std::stable_sort(out.found.begin(), out.found.end(), [](const AnimSource& a, const AnimSource& b) {
        if ((a.fit > 0) != (b.fit > 0)) return a.fit > 0;
        if (a.related != b.related) return a.related;
        const double ra = a.bestBones ? static_cast<double>(a.bestMatched) / static_cast<double>(a.bestBones) : 0.0;
        const double rb = b.bestBones ? static_cast<double>(b.bestMatched) / static_cast<double>(b.bestBones) : 0.0;
        if (a.bestMatched != b.bestMatched) return a.bestMatched > b.bestMatched;
        if (ra != rb) return ra > rb;
        return a.fit > b.fit;
    });
}

std::string CharacterName(const std::string& id) {
    // Список с форума ResHax (по моделям "кукол") + DLC и поддержка (по игре).
    static const std::map<std::string, const char*> n = {
        { "pl000", "Ichigo Kurosaki" }, { "pl001", "Ichigo (Hollow)" }, { "pl002", "Ichigo (Final Getsugatensho)" },
        { "pl003", "Uryu Ishida" }, { "pl004", "Yasutora Sado" }, { "pl006", "Kisuke Urahara" },
        { "pl007", "Yoruichi Shihoin" }, { "pl008", "Renji Abarai" }, { "pl010", "Rukia Kuchiki" },
        { "pl011", "Shuhei Hisagi" }, { "pl012", "Rangiku Matsumoto" }, { "pl013", "Izuru Kira" },
        { "pl014", "Ikkaku Madarame" }, { "pl015", "Yumichika Ayasegawa" }, { "pl016", "Genryusai Yamamoto" }, { "pl017", "Soi Fon" },
        { "pl018", "Gin Ichimaru" }, { "pl019", "Retsu Unohana" }, { "pl020", "Sosuke Aizen" }, { "pl021", "Sosuke Aizen (butterfly form)" }, { "pl022", "Byakuya Kuchiki" },
        { "pl023", "Sajin Komamura" }, { "pl024", "Shunsui Kyoraku" }, { "pl025", "Kaname Tosen" },
        { "pl026", "Toshiro Hitsugaya" }, { "pl027", "Kenpachi Zaraki" }, { "pl029", "Mayuri Kurotsuchi" },
        { "pl031", "Kaien Shiba" }, { "pl032", "Shinji Hirako" }, { "pl033", "Coyote Starrk" },
        { "pl035", "Tier Harribel" }, { "pl036", "Ulquiorra Cifer" }, { "pl037", "Nnoitra Gilga" },
        { "pl038", "Grimmjow Jaegerjaquez" }, { "pl039", "Szayelaporro Granz" }, { "pl042", "Nelliel Tu Odelschwanck" },
        // DLC (Season Pass)
        { "pl050", "Ichibe Hyosube (DLC)" }, { "pl051", "Ichigo Kurosaki TYBW (DLC)" }, { "pl052", "Yhwach (DLC)" },
        // персонажи поддержки
        { "sp000", "Zangetsu (dark, support)" }, { "sp001", "Nemu Kurotsuchi (support)" }, { "sp002", "Yachiru Kusajishi (support)" },
        { "sp003", "Lilynette Gingerbuck (support)" }, { "sp004", "Hachigen Ushoda (support)" }, { "sp005", "Marechiyo Omaeda (support)" },
    };
    const auto it = n.find(Lower(id));
    return it == n.end() ? std::string() : it->second;
}

} // namespace bleach
