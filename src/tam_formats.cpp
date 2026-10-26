// ============================================================
//  tam_formats.cpp - см. tam_formats.h
// ============================================================
#include "lang.h"
#include "tam_formats.h"

#include "miniz.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace tam {

namespace {

// ------------------------------------------------------------
//  Чтение с проверкой границ
// ------------------------------------------------------------
class Rd {
public:
    Rd(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    size_t Size() const { return n_; }
    void Need(uint64_t ofs, uint64_t len, const char* what) const {
        if (ofs > n_ || len > n_ - ofs) {
            char b[200];
            std::snprintf(b, sizeof b, TR("%s: смещение 0x%llx + %llu за пределами файла (%zu байт)"), what,
                          static_cast<unsigned long long>(ofs), static_cast<unsigned long long>(len), n_);
            throw TamError(b);
        }
    }
    template <class T> T At(uint64_t ofs, const char* what = TR("чтение")) const {
        Need(ofs, sizeof(T), what);
        T v;
        std::memcpy(&v, p_ + ofs, sizeof v);
        return v;
    }
    std::string Fixed(uint64_t ofs, size_t len) const {
        Need(ofs, len, TR("текст"));
        size_t l = 0;
        while (l < len && p_[ofs + l] != 0) ++l;
        return std::string(reinterpret_cast<const char*>(p_ + ofs), l);
    }
    std::string CStr(uint64_t ofs) const {
        if (ofs >= n_) return std::string();
        size_t l = 0;
        while (ofs + l < n_ && p_[ofs + l] != 0 && l < 1024) ++l;
        return std::string(reinterpret_cast<const char*>(p_ + ofs), l);
    }
    const uint8_t* Ptr(uint64_t ofs, uint64_t len, const char* what) const {
        Need(ofs, len, what);
        return p_ + ofs;
    }

private:
    const uint8_t* p_;
    size_t n_;
};

uint64_t AlignUp(uint64_t v, uint64_t a) { return (v % a) ? v + (a - v % a) : v; }

uint32_t g_crc[256];
bool g_crcReady = false;

void BuildCrcTable() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        g_crc[i] = c;
    }
    g_crcReady = true;
}

void UnpackNormal(const uint8_t* q, float* out) {
    double x = q[0] / 127.5 - 1.0, y = q[1] / 127.5 - 1.0, z = q[2] / 127.5 - 1.0;
    const double l = std::sqrt(x * x + y * y + z * z);
    if (l < 1e-8) { out[0] = 0; out[1] = 1; out[2] = 0; return; }
    out[0] = static_cast<float>(x / l);
    out[1] = static_cast<float>(y / l);
    out[2] = static_cast<float>(z / l);
}

} // namespace

// ------------------------------------------------------------
uint32_t Crc(const std::string& name) {
    if (!g_crcReady) BuildCrcTable();
    uint32_t h = static_cast<uint32_t>(name.size());
    for (unsigned char b : name) h = (h >> 8) ^ g_crc[(b ^ h) & 0xFF];
    return h;
}

std::string FileStem(const std::string& path) {
    const size_t s = path.find_last_of("\\/");
    std::string n = (s == std::string::npos) ? path : path.substr(s + 1);
    const size_t p = n.find_last_of('.');
    if (p != std::string::npos && p > 0) n = n.substr(0, p);
    return n;
}

bool Unpack(std::vector<uint8_t>& data, std::string& err) {
    if (data.size() < 24 || std::memcmp(data.data(), "PZZE", 4) != 0) return true;
    uint64_t want = 0, ofs = 0;
    std::memcpy(&want, data.data() + 8, 8);
    std::memcpy(&ofs, data.data() + 16, 8);
    if (ofs >= data.size() || want == 0 || want > (1ull << 31)) {
        err = TR("PZZE: неверный заголовок");
        return false;
    }
    std::vector<uint8_t> out(static_cast<size_t>(want));
    const size_t n = tinfl_decompress_mem_to_mem(out.data(), out.size(), data.data() + ofs,
                                                 data.size() - static_cast<size_t>(ofs), TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (n == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        err = TR("PZZE: данные zlib повреждены");
        return false;
    }
    out.resize(n);
    data.swap(out);
    return true;
}

bool ReadAll(const std::string& pathUtf8, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    try {
        std::ifstream f(std::filesystem::u8path(pathUtf8), std::ios::binary);
        if (!f) { err = TR("файл не открывается: ") + pathUtf8; return false; }
        f.seekg(0, std::ios::end);
        const std::streamoff n = f.tellg();
        if (n < 0 || n > (std::streamoff(1) << 31)) { err = TR("неверный размер файла"); return false; }
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(n));
        if (n > 0 && !f.read(reinterpret_cast<char*>(out.data()), n)) { err = TR("ошибка чтения"); return false; }
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return Unpack(out, err);
}

// ============================================================
//  LDS
// ============================================================
void Lds::Read(const std::vector<uint8_t>& d) {
    Rd r(d.data(), d.size());
    const uint32_t count = r.At<uint32_t>(4, TR("LDS заголовок"));
    if (count > 4096) throw TamError(TR("LDS: неверное число текстур"));
    const uint64_t base = 12 + 4ull * count;
    r.Need(0, base, TR("LDS таблица"));
    std::vector<uint32_t> ofs(count);
    for (uint32_t i = 0; i < count; ++i) ofs[i] = r.At<uint32_t>(12 + 4ull * i);
    textures.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t a = base + ofs[i];
        const uint64_t b = (i + 1 < count) ? base + ofs[i + 1] : d.size();
        if (b < a) throw TamError(TR("LDS: границы текстур перепутаны"));
        const uint8_t* p = r.Ptr(a, b - a, TR("LDS текстура"));
        textures.emplace_back(p, p + (b - a));
    }
}

DdsInfo InspectDds(const std::vector<uint8_t>& d) {
    DdsInfo i;
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) { i.format = TR("не DDS"); return i; }
    uint32_t h = 0, w = 0, pf = 0;
    std::memcpy(&h, d.data() + 12, 4);
    std::memcpy(&w, d.data() + 16, 4);
    std::memcpy(&pf, d.data() + 80, 4);
    i.width = static_cast<int>(w);
    i.height = static_cast<int>(h);
    char fc[5] = { 0, 0, 0, 0, 0 };
    std::memcpy(fc, d.data() + 84, 4);
    if (std::strcmp(fc, "DX10") == 0 && d.size() >= 148) {
        uint32_t dx = 0;
        std::memcpy(&dx, d.data() + 128, 4);
        static const std::map<uint32_t, const char*> names = {
            { 71, "BC1" }, { 72, "BC1_SRGB" }, { 74, "BC2" }, { 75, "BC2_SRGB" }, { 77, "BC3" }, { 78, "BC3_SRGB" },
            { 80, "BC4" }, { 83, "BC5" }, { 95, "BC6H" }, { 98, "BC7" }, { 99, "BC7_SRGB" }, { 28, "RGBA8" },
            { 29, "RGBA8_SRGB" }, { 87, "BGRA8" }, { 91, "BGRA8_SRGB" } };
        const auto it = names.find(dx);
        i.format = (it != names.end()) ? it->second : ("DXGI_" + std::to_string(dx));
        i.alpha = dx == 74 || dx == 75 || dx == 77 || dx == 78 || dx == 98 || dx == 99 || dx == 28 || dx == 29 ||
                  dx == 87 || dx == 91;
        return i;
    }
    if (fc[0] != 0) {
        i.format = fc;
        i.alpha = (i.format == "DXT2" || i.format == "DXT3" || i.format == "DXT4" || i.format == "DXT5");
    } else {
        i.format = "RGB";
        i.alpha = (pf & 1) != 0;
    }
    return i;
}

// ============================================================
//  TMD2
// ============================================================
void Tmd2::Read(const std::vector<uint8_t>& d) {
    Rd r(d.data(), d.size());
    if (r.Fixed(0, 4) != "tmd0") throw TamError(TR("не tmd0 (чужой формат)"));

    flag1 = r.At<uint8_t>(4);
    flag2 = r.At<uint8_t>(5);
    flags = r.At<uint16_t>(6);
    animFlag = r.At<uint16_t>(8);
    version = r.At<uint16_t>(10);
    for (int k = 0; k < 6; ++k) bbox[k] = r.At<float>(16 + 4ull * k);
    const uint64_t modelsOfs = r.At<uint64_t>(40);
    const uint32_t entriesOfs = r.At<uint32_t>(52);
    const uint32_t materialsOfs = r.At<uint32_t>(56);
    const uint32_t paramsOfs = r.At<uint32_t>(60);
    const uint32_t namesOfs = r.At<uint32_t>(64);
    const uint32_t partsOfs = r.At<uint32_t>(72);
    const uint32_t trisOfs = r.At<uint32_t>(76);
    const uint64_t texOfs = r.At<uint64_t>(80);
    const uint32_t matTexOfs = r.At<uint32_t>(88);
    const uint32_t vertsOfs = r.At<uint32_t>(92);
    const uint32_t modelCount = r.At<uint32_t>(104);
    const uint32_t entryCount = r.At<uint32_t>(116);
    const uint32_t materialCount = r.At<uint32_t>(120);
    const uint32_t paramCount = r.At<uint32_t>(124);
    const uint32_t partCount = r.At<uint32_t>(136);
    triCountTotal = r.At<uint32_t>(140);
    const uint64_t texCount = r.At<uint64_t>(144);
    const uint32_t matTexCount = r.At<uint32_t>(152);
    vertexCount = r.At<uint32_t>(156);

    uint32_t tblOfs = 0, tblIdxOfs = 0, matrixOfs = 0, hierOfs = 0, tblCount = 0, tblIdxCount = 0, boneCount = 0,
             extraOfs = 0;
    if (HasSkeleton()) {
        tblOfs = r.At<uint32_t>(160);
        tblIdxOfs = r.At<uint32_t>(164);
        matrixOfs = r.At<uint32_t>(168);
        hierOfs = r.At<uint32_t>(172);
        tblCount = r.At<uint32_t>(176);
        tblIdxCount = r.At<uint32_t>(180);
        boneCount = r.At<uint32_t>(184);
        extraOfs = r.At<uint32_t>(196);
    }
    if (modelCount > 100000 || materialCount > 100000 || partCount > 1000000 || texCount > 100000 ||
        boneCount > 100000 || vertexCount > 50000000 || triCountTotal > 50000000)
        throw TamError(TR("неверные количества в заголовке"));

    // ---- Texturen ----
    textures.assign(static_cast<size_t>(texCount), Tmd2Texture());
    for (uint64_t i = 0; i < texCount; ++i) {
        const uint64_t o = texOfs + 12 * i;
        Tmd2Texture& t = textures[static_cast<size_t>(i)];
        t.hash = r.At<uint32_t>(o, TR("текстура"));
        t.ldsIndex = r.At<uint16_t>(o + 4);
        t.width = r.At<uint16_t>(o + 6);
        t.height = r.At<uint16_t>(o + 8);
        t.format = r.At<uint16_t>(o + 10);
    }

    // ---- Material-Texturen ----
    std::vector<Tmd2MatTexture> matTex(matTexCount);
    for (uint32_t i = 0; i < matTexCount; ++i) {
        const uint64_t o = matTexOfs + 12ull * i;
        Tmd2MatTexture& t = matTex[i];
        t.hash = r.At<uint32_t>(o, TR("текстура материала"));
        t.texture = r.At<int16_t>(o + 4);
        t.unk1 = r.At<int16_t>(o + 6);
        t.unk2 = r.At<int16_t>(o + 8);
        t.slot = static_cast<int16_t>(r.At<int16_t>(o + 10)) >> 8;
        if (t.texture >= static_cast<int>(textures.size())) t.texture = -1;
    }

    // ---- Shader-Parameter: zweiter float jedes Paares ----
    std::vector<float> params(paramCount);
    for (uint32_t i = 0; i < paramCount; ++i) params[i] = r.At<float>(paramsOfs + 8ull * i + 4, TR("параметры"));

    // ---- Materialien ----
    materials.assign(materialCount, Tmd2Material());
    for (uint32_t i = 0; i < materialCount; ++i) {
        const uint64_t o = materialsOfs + 20ull * i;
        Tmd2Material& m = materials[i];
        m.index = static_cast<int>(i);
        m.hash = r.At<uint32_t>(o, TR("материал"));
        m.shader = r.Fixed(o + 4, 4);
        const uint16_t ts = r.At<uint16_t>(o + 8), tc = r.At<uint16_t>(o + 10);
        const uint16_t ps = r.At<uint16_t>(o + 12), pc = r.At<uint16_t>(o + 14);
        for (uint32_t k = ts; k < static_cast<uint32_t>(ts) + tc && k < matTex.size(); ++k) m.textures.push_back(matTex[k]);
        for (uint32_t k = ps; k < static_cast<uint32_t>(ps) + pc && k < params.size(); ++k) m.params.push_back(params[k]);
    }

    // ---- Vertices ----
    struct Field { const char* name; int bytes; };
    std::vector<Field> fields;
    if (flags & 0x2) fields.push_back({ "pos", 12 });
    if (flags & 0x400) { fields.push_back({ "w0", 4 }); fields.push_back({ "i0", 4 }); }
    if (flags & 0x8000) { fields.push_back({ "w1", 4 }); fields.push_back({ "i1", 4 }); }
    if (flags & 0x4) fields.push_back({ "nrm", 4 });
    if (flags & 0x8) { fields.push_back({ "tan", 4 }); fields.push_back({ "bin", 4 }); }
    if (flags & 0x80) fields.push_back({ "col", 4 });
    if (flags & 0x100) fields.push_back({ "nrm2", 4 });
    if (flags & 0x200) fields.push_back({ "col2", 4 });
    const bool uvFloat = (flags & 0x1000) != 0;
    if (flags & 0x10) fields.push_back({ "uv0", uvFloat ? 8 : 4 });
    if (flags & 0x20) fields.push_back({ "uv1", uvFloat ? 8 : 4 });
    if (flags & 0x40) fields.push_back({ "uv2", uvFloat ? 8 : 4 });
    stride = 0;
    layout.clear();
    for (const Field& f : fields) { stride += f.bytes; layout.push_back(f.name); }
    if (!(flags & 0x2)) throw TamError(TR("модель без позиций вершин (нет флага 0x2)"));

    const uint8_t* vb = r.Ptr(vertsOfs, static_cast<uint64_t>(stride) * vertexCount, TR("буфер вершин"));
    pos.assign(3ull * vertexCount, 0.0f);
    if (flags & 0x4) nrm.assign(3ull * vertexCount, 0.0f);
    if (flags & 0x100) nrm2.assign(3ull * vertexCount, 0.0f);
    if (flags & 0x80) color.assign(4ull * vertexCount, 255);
    for (int k = 0; k < 3; ++k) uv[k].clear();
    if (flags & 0x10) uv[0].assign(2ull * vertexCount, 0.0f);
    if (flags & 0x20) uv[1].assign(2ull * vertexCount, 0.0f);
    if (flags & 0x40) uv[2].assign(2ull * vertexCount, 0.0f);
    w0.clear(); i0.clear(); w1.clear(); i1.clear();
    if (flags & 0x400) { w0.assign(4ull * vertexCount, 0); i0.assign(4ull * vertexCount, 0); }
    if (flags & 0x8000) { w1.assign(4ull * vertexCount, 0); i1.assign(4ull * vertexCount, 0); }

    for (uint32_t v = 0; v < vertexCount; ++v) {
        const uint8_t* p = vb + static_cast<size_t>(stride) * v;
        for (const Field& f : fields) {
            const std::string n = f.name;
            if (n == "pos") std::memcpy(&pos[3ull * v], p, 12);
            else if (n == "w0") std::memcpy(&w0[4ull * v], p, 4);
            else if (n == "i0") std::memcpy(&i0[4ull * v], p, 4);
            else if (n == "w1") std::memcpy(&w1[4ull * v], p, 4);
            else if (n == "i1") std::memcpy(&i1[4ull * v], p, 4);
            else if (n == "nrm") UnpackNormal(p, &nrm[3ull * v]);
            else if (n == "nrm2") UnpackNormal(p, &nrm2[3ull * v]);
            else if (n == "col") std::memcpy(&color[4ull * v], p, 4);
            else if (n[0] == 'u') {
                const int k = n[2] - '0';
                float* dst = &uv[k][2ull * v];
                if (uvFloat) std::memcpy(dst, p, 8);
                else {
                    int16_t a, b;
                    std::memcpy(&a, p, 2);
                    std::memcpy(&b, p + 2, 2);
                    dst[0] = a / 1024.0f;
                    dst[1] = b / 1024.0f;
                }
            }
            p += f.bytes;
        }
    }

    // ---- Dreiecke ----
    idx.assign(3ull * triCountTotal, 0);
    if (flags & 0x800) {
        const uint8_t* p = r.Ptr(trisOfs, 12ull * triCountTotal, TR("треугольники"));
        std::memcpy(idx.data(), p, 12ull * triCountTotal);
    } else {
        const uint8_t* p = r.Ptr(trisOfs, 6ull * triCountTotal, TR("треугольники"));
        for (size_t k = 0; k < idx.size(); ++k) {
            uint16_t s;
            std::memcpy(&s, p + 2 * k, 2);
            idx[k] = s;
        }
    }
    for (uint32_t& i : idx)
        if (i >= vertexCount) i = 0;   // битый индекс -> 0 (треугольник потом отбросится как вырожденный)

    // ---- Skelett ----
    bones.clear();
    tables.clear();
    if (HasSkeleton()) {
        std::vector<uint32_t> all(tblIdxCount);
        for (uint32_t i = 0; i < tblIdxCount; ++i) all[i] = r.At<uint32_t>(tblIdxOfs + 4ull * i, TR("таблица индексов"));
        for (uint32_t i = 0; i < tblCount; ++i) {
            const uint32_t n = r.At<uint32_t>(tblOfs + 8ull * i), s = r.At<uint32_t>(tblOfs + 8ull * i + 4);
            std::vector<uint32_t> t;
            for (uint64_t k = s; k < static_cast<uint64_t>(s) + n && k < all.size(); ++k) t.push_back(all[static_cast<size_t>(k)]);
            tables.push_back(t);
        }
        bones.assign(boneCount, Tmd2Bone());
        for (uint32_t i = 0; i < boneCount; ++i) {
            const uint64_t o = hierOfs + 24ull * i;
            Tmd2Bone& k = bones[i];
            k.hash = r.At<uint32_t>(o, TR("кость"));
            for (int a = 0; a < 3; ++a) k.restPos[a] = r.At<float>(o + 4 + 4ull * a);
            const int32_t e = r.At<int32_t>(o + 16);
            k.parent = (e >= 0 && static_cast<uint32_t>(e) < boneCount && static_cast<uint32_t>(e) != i) ? e : -1;
            const uint16_t nameOfs = r.At<uint16_t>(o + 22);
            if (namesOfs != 0 && nameOfs != 0xFFFF) k.name = r.CStr(static_cast<uint64_t>(namesOfs) + nameOfs);
            for (int z = 0; z < 16; ++z) k.invBind.m[z] = r.At<float>(matrixOfs + 64ull * i + 4ull * z, TR("матрица кости"));
        }
        if (extraOfs != 0) {
            int count = 0;
            for (uint32_t i = 0; i < boneCount; ++i) {
                bones[i].extra = r.At<int16_t>(extraOfs + 2ull * i, TR("кость extra"));
                if (bones[i].extra != -1) ++count;
            }
            const uint64_t p = AlignUp(extraOfs + 2ull * boneCount, 16);
            for (uint32_t i = 0; i < boneCount; ++i) {
                const int e = bones[i].extra;
                if (e < 0 || e >= count) continue;
                for (int a = 0; a < 3; ++a) bones[i].offset[a] = r.At<float>(p + 12ull * e + 4ull * a, TR("смещение кости"));
            }
        }
    }

    // ---- Teilmeshes und Eintraege ----
    std::vector<Tmd2Part> parts(partCount);
    for (uint32_t i = 0; i < partCount; ++i) {
        parts[i].triCount = r.At<uint32_t>(partsOfs + 8ull * i, TR("подмеш"));
        parts[i].start = r.At<uint32_t>(partsOfs + 8ull * i + 4);
        if (static_cast<uint64_t>(parts[i].start) + parts[i].triCount > triCountTotal) {
            parts[i].triCount = (parts[i].start < triCountTotal) ? triCountTotal - parts[i].start : 0;
        }
    }
    std::vector<std::pair<uint8_t, uint8_t>> entries(entryCount);   // (индекс, тип)
    for (uint32_t i = 0; i < entryCount; ++i)
        entries[i] = { r.At<uint8_t>(entriesOfs + 2ull * i, TR("запись")), r.At<uint8_t>(entriesOfs + 2ull * i + 1) };

    models.assign(modelCount, Tmd2Model());
    for (uint32_t i = 0; i < modelCount; ++i) {
        const uint64_t o = modelsOfs + 44ull * i;
        Tmd2Model& m = models[i];
        const uint16_t count = r.At<uint16_t>(o + 24, TR("модель"));
        const uint32_t start = r.At<uint32_t>(o + 28);
        const int32_t nameOfs = r.At<int32_t>(o + 32);
        m.hash = r.At<uint32_t>(o + 36);
        if (namesOfs != 0 && nameOfs >= 0) m.name = r.CStr(static_cast<uint64_t>(namesOfs) + static_cast<uint32_t>(nameOfs));
        if (m.name.empty()) m.name = std::to_string(m.hash);
        int material = -1;
        int table = tables.empty() ? -1 : 0;
        for (uint64_t e = start; e < static_cast<uint64_t>(start) + count && e < entries.size(); ++e) {
            const int ix = entries[static_cast<size_t>(e)].first;
            const int type = entries[static_cast<size_t>(e)].second;
            if (type == 96) material = (ix < static_cast<int>(materials.size())) ? ix : -1;
            else if (type == 64) { if (ix < static_cast<int>(tables.size())) table = ix; }
            else if (type == 48 && ix < static_cast<int>(parts.size())) {
                Tmd2Part t = parts[static_cast<size_t>(ix)];
                t.material = material;
                t.table = table;
                m.parts.push_back(t);
            }
        }
    }
}

void Tmd2::Weights(uint32_t v, int table, std::vector<std::pair<int, float>>& out) const {
    out.clear();
    if (w0.empty() || v >= vertexCount) return;
    const std::vector<uint32_t>* tab = (table >= 0 && table < static_cast<int>(tables.size())) ? &tables[static_cast<size_t>(table)] : nullptr;
    auto take = [&](const std::vector<uint8_t>& w, const std::vector<uint8_t>& ids) {
        if (w.empty()) return;
        for (int k = 0; k < 4; ++k) {
            const uint8_t g = w[4ull * v + k];
            if (g == 0) continue;
            const uint8_t loc = ids[4ull * v + k];
            int glob;
            if (tab != nullptr) {
                if (loc >= tab->size()) continue;
                glob = static_cast<int>((*tab)[loc]);
            } else {
                glob = loc;
            }
            bool found = false;
            for (auto& p : out)
                if (p.first == glob) { p.second += g / 255.0f; found = true; break; }
            if (!found) out.push_back({ glob, g / 255.0f });
        }
    };
    take(w0, i0);
    take(w1, i1);
    float sum = 0.0f;
    for (const auto& p : out) sum += p.second;
    if (sum <= 0.0f) { out.clear(); return; }
    for (auto& p : out) p.second /= sum;
}

std::string Tmd2::Report() const {
    char b[400];
    std::snprintf(b, sizeof b,
                  "TMD2 %s: ver=0x%X flags=0x%04X stride=%d verts=%u tris=%u models=%zu mats=%zu tex=%zu bones=%zu tables=%zu\n  layout:",
                  name.c_str(), version, flags, stride, vertexCount, triCountTotal, models.size(), materials.size(),
                  textures.size(), bones.size(), tables.size());
    std::string s = b;
    for (const std::string& l : layout) s += " " + l;
    s += "\n";
    for (const Tmd2Model& m : models) {
        std::snprintf(b, sizeof b, "  model '%s' parts=%zu\n", m.name.c_str(), m.parts.size());
        s += b;
    }
    for (const Tmd2Material& m : materials) {
        std::snprintf(b, sizeof b, "  mat #%d hash=%u shader=%s tex=%zu params=%zu\n", m.index, m.hash, m.shader.c_str(),
                      m.textures.size(), m.params.size());
        s += b;
    }
    return s;
}

// ============================================================
//  TMO
// ============================================================
void Tmo::Read(const uint8_t* d, size_t n) {
    Rd r(d, n);
    magic = r.Fixed(0, 4);
    flag1 = r.At<uint16_t>(8, TR("TMO заголовок"));
    flag2 = r.At<uint16_t>(10);
    scale = r.At<float>(12);
    uint64_t p = 16;
    if (flag2 & 16) p += 20 * 4;   // данные камеры
    flag3 = r.At<uint32_t>(p);
    const uint32_t framesOfs = r.At<uint32_t>(p + 4);
    p = AlignUp(p + 8, 16) + 8;
    const uint32_t hashOfs = r.At<uint32_t>(p);
    const uint32_t curveCount = r.At<uint32_t>(p + 4);
    const uint32_t curveOfs = r.At<uint32_t>(p + 8);
    const uint32_t keyCount = r.At<uint32_t>(p + 12);
    const uint32_t keyOfs = r.At<uint32_t>(p + 16);
    frames = r.At<uint16_t>(framesOfs, TR("TMO кадры"));
    const uint16_t boneCount = r.At<uint16_t>(framesOfs + 2);
    hashes.resize(boneCount);
    for (uint32_t i = 0; i < boneCount; ++i) hashes[i] = r.At<uint32_t>(hashOfs + 4ull * i, TR("TMO хеши"));
    if (curveCount > 1000000 || keyCount > 50000000) throw TamError(TR("TMO: неверные количества"));

    const bool full = (flag1 & 1) != 0;
    std::vector<Key> keys(keyCount);
    for (uint32_t i = 0; i < keyCount; ++i) {
        if (full) {
            keys[i].frame = static_cast<float>(r.At<uint16_t>(keyOfs + 8ull * i, TR("TMO ключи")));
            keys[i].value = r.At<float>(keyOfs + 8ull * i + 4);
        } else {
            keys[i].frame = static_cast<float>(r.At<uint16_t>(keyOfs + 4ull * i, TR("TMO ключи")));
            keys[i].value = r.At<int16_t>(keyOfs + 4ull * i + 2) / 1000.0f;
        }
    }
    if (curveCount != static_cast<uint32_t>(boneCount) * 9) {
        char b[120];
        std::snprintf(b, sizeof b, TR("кривых=%u, ожидалось костей*9=%u"), curveCount, static_cast<unsigned>(boneCount) * 9);
        warnings.push_back(b);
    }
    curves.assign(static_cast<size_t>(boneCount) * 9, std::vector<Key>());
    float last = 0.0f;
    for (uint32_t c = 0; c < curveCount && c < curves.size(); ++c) {
        uint64_t start, count;
        if (full) {
            start = r.At<uint32_t>(curveOfs + 12ull * c, TR("TMO кривые"));
            count = r.At<uint32_t>(curveOfs + 12ull * c + 4);
        } else {
            start = r.At<uint64_t>(curveOfs + 16ull * c, TR("TMO кривые"));
            count = r.At<uint32_t>(curveOfs + 16ull * c + 8);
        }
        if (start > keys.size() || count > keys.size() - start) {
            warnings.push_back(TR("кривая ") + std::to_string(c) + TR(" указывает за пределы ключей"));
            continue;
        }
        std::vector<Key>& k = curves[c];
        k.assign(keys.begin() + static_cast<long>(start), keys.begin() + static_cast<long>(start + count));
        std::sort(k.begin(), k.end(), [](const Key& a, const Key& b) { return a.frame < b.frame; });
        if (!k.empty() && k.back().frame > last) last = k.back().frame;
    }
    length = std::max(std::max(frames, static_cast<int>(last) + 1), 1);
}

// ============================================================
//  TACTPKG
// ============================================================
namespace {

// Заголовок клипа tmo: кадры, кости, таблица хешей. false - не похоже на клип.
bool TmoHeader(const uint8_t* p, size_t n, int& frames, int& bones, uint32_t& hashOfs) {
    if (n < 48 || p[0] != 't' || p[1] != 'm' || p[2] != 'o') return false;
    try {
        Rd t(p, n);
        const uint16_t f2 = t.At<uint16_t>(10);
        uint64_t q = 16 + ((f2 & 16) ? 80 : 0);
        const uint32_t bo = t.At<uint32_t>(q + 4);
        q = AlignUp(q + 8, 16) + 8;
        hashOfs = t.At<uint32_t>(q);
        frames = t.At<uint16_t>(bo);
        bones = t.At<uint16_t>(bo + 2);
        if (bones <= 0 || bones > 4096) return false;
        t.Need(hashOfs, 4ull * static_cast<uint64_t>(bones), TR("TMO хеши"));
        return true;
    } catch (const TamError&) {
        return false;
    }
}

bool Is(const uint8_t* p, size_t n, const char* magic) {
    const size_t l = std::strlen(magic);
    return n >= l && std::memcmp(p, magic, l) == 0;
}

// Обход вложенных контейнеров. Всё, что не клип tmo, попадает в extras.
struct Walker {
    TactPkg& pkg;
    size_t found = 0;

    const std::vector<uint8_t>& Buf(int blob) const { return blob < 0 ? pkg.data : pkg.blobs[static_cast<size_t>(blob)]; }

    void AddClip(int blob, size_t base, size_t size, const std::string& where, const std::string& name) {
        const std::vector<uint8_t>& d = Buf(blob);
        ClipEntry c;
        c.name = name;
        c.ofs = base;
        c.size = size;
        c.blob = blob;
        c.where = where;
        uint32_t ho = 0;
        TmoHeader(d.data() + base, size, c.frames, c.bones, ho);
        pkg.clips.push_back(c);
        ++found;
    }

    // Таблица "имя[64] + смещение + размер" (acttmo/acttmv/actmtl/actmng): count @32, записи с 36.
    template <class F> void Table(int blob, size_t base, size_t size, const char* what, F&& each) {
        Rd r(Buf(blob).data() + base, size);
        const uint32_t count = r.At<uint32_t>(32, what);
        if (count > 100000) throw TamError(std::string(what) + TR(": неверное число записей"));
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t o = 36 + 72ull * i;
            const std::string nm = r.Fixed(o, 64);
            const uint32_t ofs = r.At<uint32_t>(o + 64), n = r.At<uint32_t>(o + 68);
            r.Need(ofs, n, what);
            each(nm, static_cast<size_t>(ofs), static_cast<size_t>(n));
        }
    }

    void Extra(const std::string& where, const std::string& type, const std::string& name, size_t size) {
        SubEntry e;
        e.where = where;
        e.type = type;
        e.name = name;
        e.size = size;
        pkg.extras.push_back(e);
    }

    void Walk(int blob, size_t base, size_t size, const std::string& where, const std::string& name, int depth) {
        if (depth > 8 || size < 4) return;
        const uint8_t* p = Buf(blob).data() + base;
        const std::string here = where.empty() ? name : where + "/" + name;
        int fr = 0, bo = 0;
        uint32_t ho = 0;
        if (Is(p, size, "PZZE")) {
            std::vector<uint8_t> u(p, p + size);
            std::string err;
            if (!Unpack(u, err)) { Extra(where, "PZZE (" + err + ")", name, size); return; }
            pkg.blobs.push_back(std::move(u));
            const int nb = static_cast<int>(pkg.blobs.size()) - 1;
            Walk(nb, 0, pkg.blobs.back().size(), where, name, depth + 1);
            return;
        }
        if (TmoHeader(p, size, fr, bo, ho)) { AddClip(blob, base, size, where, name); return; }
        if (Is(p, size, "acttmo_pkg")) {
            Table(blob, base, size, "acttmo_pkg", [&](const std::string& n, size_t o, size_t sz) {
                const uint8_t* q = Buf(blob).data() + base + o;
                if (TmoHeader(q, sz, fr, bo, ho)) AddClip(blob, base + o, sz, here, n);
                else Walk(blob, base + o, sz, here, n, depth + 1);
            });
            return;
        }
        if (Is(p, size, "acttmv_pkg") || Is(p, size, "actmtl_pkg")) {
            const std::string type = Is(p, size, "acttmv_pkg") ? "acttmv_pkg" : "actmtl_pkg";
            Table(blob, base, size, type.c_str(), [&](const std::string& n, size_t, size_t sz) { Extra(here, type, n, sz); });
            return;
        }
        if (Is(p, size, "actmng_pkg")) {
            Table(blob, base, size, "actmng_pkg", [&](const std::string& n, size_t o, size_t sz) {
                const uint8_t* q = Buf(blob).data() + base + o;
                if (sz >= 10 && (Is(q, sz, "act") || Is(q, sz, "PZZE") || Is(q, sz, "CATS") || Is(q, sz, "tmo"))) {
                    Walk(blob, base + o, sz, here, n, depth + 1);
                } else {
                    std::string type = TR("данные");
                    if (sz >= 16) {
                        Rd r(q, sz);
                        const std::string t = r.Fixed(0, 16);
                        bool printable = !t.empty();
                        for (char c : t) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) printable = false;
                        if (printable) type = t;
                    }
                    pkg.other.push_back(n + " (" + type + ", " + std::to_string(sz) + TR(" байт)"));
                }
            });
            return;
        }
        if (Is(p, size, "CATS") && size >= 16) {
            Rd r(p, size);
            const uint32_t count = r.At<uint32_t>(8, "CATS");
            const uint32_t hdr = r.At<uint32_t>(12, "CATS");
            if (count > 1000000) throw TamError(TR("CATS: неверное число записей"));
            uint64_t q = 16ull + hdr;
            for (uint32_t i = 0; i < count; ++i) {
                const uint64_t no = r.At<uint64_t>(q, TR("CATS запись"));
                const uint64_t o = r.At<uint64_t>(q + 8, TR("CATS запись"));
                const uint64_t n = r.At<uint64_t>(q + 16, TR("CATS запись"));
                q = AlignUp(q + 24, 16);
                r.Need(o, n, TR("CATS данные"));
                std::string nm = r.CStr(no);
                if (nm.empty()) nm = "#" + std::to_string(i);
                Walk(blob, base + static_cast<size_t>(o), static_cast<size_t>(n), here, nm, depth + 1);
            }
            return;
        }
        if (Is(p, size, "tmd0") || Is(p, size, "DDS ")) return;   // модели и текстуры - мимо
        // незнакомый контейнер: клипы tmo по сигнатуре (каждые 4 байта)
        if (size > (512u << 20)) return;
        std::vector<size_t> at;
        for (size_t i = 0; i + 48 <= size; i += 4)
            if (p[i] == 't' && p[i + 1] == 'm' && p[i + 2] == 'o' && TmoHeader(p + i, size - i, fr, bo, ho)) at.push_back(i);
        for (size_t k = 0; k < at.size(); ++k) {
            const size_t end = (k + 1 < at.size()) ? at[k + 1] : size;
            AddClip(blob, base + at[k], end - at[k], here, name + "#" + std::to_string(k));
        }
        if (at.empty() && depth > 0) {
            std::string t;
            for (size_t i = 0; i < size && i < 16 && p[i] >= 32 && p[i] <= 126; ++i) t += static_cast<char>(p[i]);
            Extra(where, t.size() >= 3 ? t : std::string("?"), name, size);
        }
    }
};

} // namespace

void TactPkg::Read() {
    clips.clear();
    other.clear();
    extras.clear();
    blobs.clear();
    if (data.size() < 4) throw TamError(TR("файл пуст"));
    const uint8_t* p = data.data();
    const size_t n = data.size();
    if (Is(p, n, "actmng_pkg")) container = "actmng_pkg";
    else if (Is(p, n, "acttmo_pkg")) container = "acttmo_pkg";
    else if (Is(p, n, "CATS")) container = "CATS";
    else if (Is(p, n, "tmo")) container = "tmo";
    else container = TR("поиск по сигнатуре");
    Walker w{ *this };
    w.Walk(-1, 0, n, std::string(), name, 0);
    if (clips.empty() && extras.empty() && other.empty())
        throw TamError(container == TR("поиск по сигнатуре") ? TR("не пакет анимаций (клипов tmo не найдено)") : TR("клипов нет"));
}

void TactPkg::AddFile(const std::string& fileName, std::vector<uint8_t>&& bytes) {
    blobs.push_back(std::move(bytes));
    const int b = static_cast<int>(blobs.size()) - 1;
    Walker w{ *this };
    const size_t before = clips.size();
    w.Walk(b, 0, blobs.back().size(), std::string(), fileName, 0);
    // одиночный клип в файле получает имя файла
    if (clips.size() == before + 1) clips.back().name = fileName;
}

bool TactPkg::Clip(size_t i, Tmo& out, std::string& err) const {
    if (i >= clips.size()) { err = TR("номер клипа вне списка"); return false; }
    try {
        const ClipEntry& c = clips[i];
        const std::vector<uint8_t>& d = c.blob < 0 ? data : blobs[static_cast<size_t>(c.blob)];
        if (c.ofs > d.size() || c.size > d.size() - c.ofs) throw TamError(TR("клип за пределами пакета"));
        out = Tmo();
        out.name = c.name;
        out.Read(d.data() + c.ofs, c.size);
        return true;
    } catch (const std::exception& e) {
        err = clips[i].name + ": " + e.what();
        return false;
    }
}

bool TactPkg::ClipBones(size_t i, std::vector<uint32_t>& hashes) const {
    hashes.clear();
    if (i >= clips.size()) return false;
    const ClipEntry& c = clips[i];
    const std::vector<uint8_t>& d = c.blob < 0 ? data : blobs[static_cast<size_t>(c.blob)];
    if (c.ofs > d.size() || c.size > d.size() - c.ofs) return false;
    int fr = 0, bo = 0;
    uint32_t ho = 0;
    if (!TmoHeader(d.data() + c.ofs, c.size, fr, bo, ho)) return false;
    hashes.resize(static_cast<size_t>(bo));
    std::memcpy(hashes.data(), d.data() + c.ofs + ho, 4u * static_cast<size_t>(bo));
    return true;
}

// ============================================================
namespace {
struct QuickBuf {
    std::vector<uint8_t> out;
    size_t want = 256;
};
int QuickPut(const void* p, int len, void* user) {
    QuickBuf* q = static_cast<QuickBuf*>(user);
    const uint8_t* b = static_cast<const uint8_t*>(p);
    q->out.insert(q->out.end(), b, b + len);
    return q->out.size() >= q->want ? 0 : 1;      // заголовка хватит - стоп
}
} // namespace

Tmd2Quick QuickTmd2(const std::string& path) {
    Tmd2Quick r;
    std::vector<uint8_t> head;
    try {
        std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
        if (!f) return r;
        head.resize(64 * 1024);
        f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<size_t>(f.gcount()));
    } catch (...) {
        return r;
    }
    std::vector<uint8_t> d;
    if (head.size() >= 24 && std::memcmp(head.data(), "PZZE", 4) == 0) {
        uint64_t ofs = 0;
        std::memcpy(&ofs, head.data() + 16, 8);
        if (ofs >= head.size()) return r;
        QuickBuf q;
        size_t inSize = head.size() - static_cast<size_t>(ofs);
        tinfl_decompress_mem_to_callback(head.data() + ofs, &inSize, QuickPut, &q, TINFL_FLAG_PARSE_ZLIB_HEADER);
        d.swap(q.out);
    } else {
        d.swap(head);
    }
    if (d.size() < 188 || std::memcmp(d.data(), "tmd0", 4) != 0) return r;
    uint16_t flags = 0;
    std::memcpy(&flags, d.data() + 6, 2);
    std::memcpy(&r.models, d.data() + 104, 4);
    std::memcpy(&r.tris, d.data() + 140, 4);
    std::memcpy(&r.verts, d.data() + 156, 4);
    if (flags & 0x2000) std::memcpy(&r.bones, d.data() + 184, 4);
    r.ok = true;
    return r;
}

std::string SniffMagic(const std::string& path, bool* pzze) {
    if (pzze != nullptr) *pzze = false;
    std::vector<uint8_t> head;
    try {
        std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
        if (!f) return std::string();
        head.resize(4096);
        f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<size_t>(f.gcount()));
    } catch (...) {
        return std::string();
    }
    std::vector<uint8_t> d;
    if (head.size() >= 24 && std::memcmp(head.data(), "PZZE", 4) == 0) {
        if (pzze != nullptr) *pzze = true;
        uint64_t ofs = 0;
        std::memcpy(&ofs, head.data() + 16, 8);
        if (ofs >= head.size()) return "PZZE";
        QuickBuf q;
        q.want = 16;
        size_t inSize = head.size() - static_cast<size_t>(ofs);
        tinfl_decompress_mem_to_callback(head.data() + ofs, &inSize, QuickPut, &q, TINFL_FLAG_PARSE_ZLIB_HEADER);
        d.swap(q.out);
    } else {
        d.swap(head);
    }
    std::string m;
    for (size_t i = 0; i < d.size() && i < 16; ++i) {
        const unsigned char c = d[i];
        if (c == 0) break;
        if (c < 32 || c > 126) { if (i < 3) return m.empty() ? std::string("?") : m; break; }
        m += static_cast<char>(c);
    }
    return m.empty() ? std::string("?") : m;
}

// ============================================================
bool LoadTmd2(const std::string& path, Tmd2& out, std::string& err) {
    std::vector<uint8_t> d;
    if (!ReadAll(path, d, err)) return false;
    try {
        out = Tmd2();
        out.name = FileStem(path);
        out.path = path;
        out.Read(d);
        return true;
    } catch (const std::exception& e) {
        err = FileStem(path) + ": " + e.what();
        return false;
    }
}

bool LoadLds(const std::string& path, Lds& out, std::string& err) {
    std::vector<uint8_t> d;
    if (!ReadAll(path, d, err)) return false;
    try {
        out = Lds();
        out.name = FileStem(path);
        out.Read(d);
        return true;
    } catch (const std::exception& e) {
        err = FileStem(path) + ": " + e.what();
        return false;
    }
}

bool LoadTact(const std::string& path, TactPkg& out, std::string& err) {
    out = TactPkg();
    out.name = FileStem(path);
    if (!ReadAll(path, out.data, err)) return false;
    try {
        out.Read();
        return true;
    } catch (const std::exception& e) {
        err = out.name + ": " + e.what();
        return false;
    }
}

bool LoadTactMany(const std::vector<std::string>& paths, const std::string& name, TactPkg& out, std::string& err) {
    out = TactPkg();
    out.name = name;
    out.container = TR("несколько файлов");
    for (const std::string& p : paths) {
        std::vector<uint8_t> d;
        std::string e;
        if (!ReadAll(p, d, e)) { err += (err.empty() ? "" : "; ") + e; continue; }
        try {
            out.AddFile(FileStem(p), std::move(d));
        } catch (const std::exception& x) {
            err += (err.empty() ? "" : "; ") + FileStem(p) + ": " + x.what();
        }
    }
    if (out.clips.empty()) {
        if (err.empty()) err = TR("клипов нет");
        return false;
    }
    return true;
}

bool LoadTmo(const std::string& path, Tmo& out, std::string& err) {
    std::vector<uint8_t> d;
    if (!ReadAll(path, d, err)) return false;
    try {
        out = Tmo();
        out.name = FileStem(path);
        out.Read(d.data(), d.size());
        return true;
    } catch (const std::exception& e) {
        err = out.name + ": " + e.what();
        return false;
    }
}

} // namespace tam
