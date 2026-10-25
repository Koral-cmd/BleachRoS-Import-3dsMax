// ============================================================
//  tam_math.h - матрицы 4x4 для чтения и анимации.
//
//  Конвенция как у Matrix3 в 3ds Max (строки-векторы):
//      p' = p * M,    world = local * parentWorld
//  m[0..15] по строкам, строка 3 = перенос.
//  Считаем в double, во float переводим только при передаче в Max.
// ============================================================
#pragma once
#include <cmath>

namespace tam {

struct M4 {
    double m[16];
};

inline M4 Ident() {
    M4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
    return r;
}

inline bool IsIdent(const M4& a, double eps = 1e-12) {
    const M4 i = Ident();
    for (int k = 0; k < 16; ++k)
        if (std::fabs(a.m[k] - i.m[k]) > eps) return false;
    return true;
}

inline M4 Mul(const M4& a, const M4& b) {
    M4 o{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double v = 0.0;
            for (int k = 0; k < 4; ++k) v += a.m[r * 4 + k] * b.m[k * 4 + c];
            o.m[r * 4 + c] = v;
        }
    return o;
}

// Гаусс-Жордан; false для вырожденной матрицы (тогда `out` = единичная).
inline bool Inverse(const M4& in, M4& out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) a[r][c] = (c < 4) ? in.m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-14) {
            out = Ident();
            return false;
        }
        if (piv != col)
            for (int c = 0; c < 8; ++c) { const double t = a[col][c]; a[col][c] = a[piv][c]; a[piv][c] = t; }
        const double pv = a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] /= pv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            if (f == 0.0) continue;
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.m[r * 4 + c] = a[r][4 + c];
    return true;
}

inline void TransformPoint(const M4& m, double x, double y, double z, double out[3]) {
    out[0] = x * m.m[0] + y * m.m[4] + z * m.m[8] + m.m[12];
    out[1] = x * m.m[1] + y * m.m[5] + z * m.m[9] + m.m[13];
    out[2] = x * m.m[2] + y * m.m[6] + z * m.m[10] + m.m[14];
}

// Игра (Y вверх) -> Max (Z вверх):  (x, y, z) -> (x, -z, y)
inline M4 YupToZup() {
    M4 r{};
    r.m[0] = 1.0;
    r.m[6] = 1.0;    // строка 1 (y) -> (0, 0, 1)
    r.m[9] = -1.0;   // строка 2 (z) -> (0, -1, 0)
    r.m[15] = 1.0;
    return r;
}

inline M4 ScaleTranslation(M4 a, double s) {
    a.m[12] *= s;
    a.m[13] *= s;
    a.m[14] *= s;
    return a;
}

// Поворот вокруг оси, форма для строк-векторов.
inline M4 RotAxis(char ax, double a) {
    M4 r = Ident();
    const double c = std::cos(a), s = std::sin(a);
    if (ax == 'X') { r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; }
    else if (ax == 'Y') { r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; }
    else { r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; }
    return r;
}

// Эйлер как в Blender: "XYZ" = сначала X, потом Y, потом Z.
inline M4 Euler(double rx, double ry, double rz, const char order[3]) {
    M4 m = Ident();
    for (int i = 0; i < 3; ++i) {
        const char ax = order[i];
        const double a = (ax == 'X') ? rx : (ax == 'Y') ? ry : rz;
        m = Mul(m, RotAxis(ax, a));
    }
    return m;
}

// local = S * R * T  (в столбцовой записи T @ R @ S)
inline M4 Trs(const double t[3], const double r[3], const double s[3], const char order[3]) {
    M4 m = Euler(r[0], r[1], r[2], order);
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c) m.m[row * 4 + c] *= s[row];
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
}

inline double MaxAbsDiff(const M4& a, const M4& b) {
    double d = 0.0;
    for (int k = 0; k < 16; ++k) {
        const double x = std::fabs(a.m[k] - b.m[k]);
        if (x > d) d = x;
    }
    return d;
}

// Длина строки (масштаб оси).
inline double RowLen(const M4& a, int r) {
    return std::sqrt(a.m[r * 4] * a.m[r * 4] + a.m[r * 4 + 1] * a.m[r * 4 + 1] + a.m[r * 4 + 2] * a.m[r * 4 + 2]);
}

// Угол (в градусах) между поворотами двух матриц.
inline double AngleDeg(const M4& a, const M4& b) {
    double spur = 0.0;
    for (int r = 0; r < 3; ++r) {
        const double la = RowLen(a, r), lb = RowLen(b, r);
        if (la < 1e-12 || lb < 1e-12) continue;
        for (int c = 0; c < 3; ++c) spur += (a.m[r * 4 + c] / la) * (b.m[r * 4 + c] / lb);
    }
    double c = (spur - 1.0) * 0.5;
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return std::acos(c) * 180.0 / 3.14159265358979323846;
}

} // namespace tam
