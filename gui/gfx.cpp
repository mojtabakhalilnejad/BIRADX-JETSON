#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace gfx {

FillHook fillHook = nullptr;

bool parseColor(const std::string& s, Color* out)
{
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i) if (!std::isxdigit((unsigned char)s[i])) return false;
    const unsigned long v = std::strtoul(s.c_str() + 1, nullptr, 16);
    *out = hex(uint32_t(v));
    return true;
}

Color mix(Color a, Color b, float t)
{
    auto m = [t](int x, int y) { return uint8_t(std::lround(x + (y - x) * t)); };
    return Color(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b));
}

// Qt.lighter / Qt.darker work in HSV value; this is close enough for borders.
Color lighter(Color c, float f)
{
    auto l = [f](int x) { return uint8_t(std::min(255L, std::lround(x * f))); };
    Color o(l(c.r), l(c.g), l(c.b));
    const int mx = std::max(c.r, std::max(c.g, c.b));
    if (mx * f > 255) {   // saturate toward white as Qt does
        const float over = std::min(1.0f, (mx * f - 255) / 255.0f);
        o = mix(o, Color(255, 255, 255), over);
    }
    return o;
}

Color darker(Color c, float f)
{
    auto d = [f](int x) { return uint8_t(std::lround(x / f)); };
    return Color(d(c.r), d(c.g), d(c.b));
}

Rect Rect::intersect(const Rect& o) const
{
    const int X = std::max(x, o.x), Y = std::max(y, o.y);
    const int R = std::min(r(), o.r()), B = std::min(b(), o.b());
    return Rect(X, Y, std::max(0, R - X), std::max(0, B - Y));
}

static inline uint16_t blend565(uint16_t d, Color c, int a)
{
    // unpack, blend at 8 bits, repack
    int dr = (d >> 8) & 0xF8; dr |= dr >> 5;
    int dg = (d >> 3) & 0xFC; dg |= dg >> 6;
    int db = (d << 3) & 0xF8; db |= db >> 5;
    dr += ((c.r - dr) * a + 127) / 255;
    dg += ((c.g - dg) * a + 127) / 255;
    db += ((c.b - db) * a + 127) / 255;
    return uint16_t(((dr & 0xF8) << 8) | ((dg & 0xFC) << 3) | (db >> 3));
}

void Canvas::fill(const Rect& r0, Color c, int alpha)
{
    const Rect r = r0.intersect(m_clip);
    if (r.empty() || alpha <= 0) return;
    if (alpha >= 248) {                       // indistinguishable from opaque: let DMA2D do it
        const uint16_t v = c.rgb565();
        if (fillHook && r.w * r.h >= 256) { fillHook(m_px + r.y * m_w + r.x, r.w, r.h, m_w, v); return; }
        for (int y = r.y; y < r.b(); ++y) {
            uint16_t* p = m_px + y * m_w + r.x;
            for (int x = 0; x < r.w; ++x) p[x] = v;
        }
        return;
    }
    for (int y = r.y; y < r.b(); ++y) {
        uint16_t* p = m_px + y * m_w + r.x;
        for (int x = 0; x < r.w; ++x) p[x] = blend565(p[x], c, alpha);
    }
}

void Canvas::pixel(int x, int y, Color c, int alpha)
{
    if (!m_clip.contains(x, y) || alpha <= 0) return;
    uint16_t& p = m_px[y * m_w + x];
    p = alpha >= 255 ? c.rgb565() : blend565(p, c, alpha);
}

void Canvas::frame(const Rect& r, Color c, int w, int alpha)
{
    fill(Rect(r.x, r.y, r.w, w), c, alpha);
    fill(Rect(r.x, r.b() - w, r.w, w), c, alpha);
    fill(Rect(r.x, r.y + w, w, r.h - 2 * w), c, alpha);
    fill(Rect(r.r() - w, r.y + w, w, r.h - 2 * w), c, alpha);
}

// Evaluates coverage `cov(px, py)` (0..1) over a bounding box and blends.
template <class Cov>
void Canvas::shape(float x0, float y0, float x1, float y1, Color c, int alpha, Cov cov)
{
    const int X0 = std::max(m_clip.x, int(std::floor(x0)));
    const int Y0 = std::max(m_clip.y, int(std::floor(y0)));
    const int X1 = std::min(m_clip.r(), int(std::ceil(x1)) + 1);
    const int Y1 = std::min(m_clip.b(), int(std::ceil(y1)) + 1);
    for (int y = Y0; y < Y1; ++y) {
        uint16_t* row = m_px + y * m_w;
        for (int x = X0; x < X1; ++x) {
            const float k = cov(x + 0.5f, y + 0.5f);
            if (k <= 0.0f) continue;
            const int a = int(alpha * (k > 1.0f ? 1.0f : k) + 0.5f);
            if (a >= 255) row[x] = c.rgb565();
            else if (a > 0) row[x] = blend565(row[x], c, a);
        }
    }
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float hyp(float x, float y) { return std::sqrt(x * x + y * y); }   // std::hypot is slow on the M7

void Canvas::roundRect(const Rect& r, float rad, Color c, int alpha)
{
    if (r.empty()) return;
    rad = std::min(rad, std::min(r.w, r.h) / 2.0f);
    const int ir = int(std::ceil(rad));
    // the body without the corner squares
    fill(Rect(r.x + ir, r.y, r.w - 2 * ir, r.h), c, alpha);
    fill(Rect(r.x, r.y + ir, ir, r.h - 2 * ir), c, alpha);
    fill(Rect(r.r() - ir, r.y + ir, ir, r.h - 2 * ir), c, alpha);
    const float cx[2] = { r.x + rad, r.r() - rad }, cy[2] = { r.y + rad, r.b() - rad };
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) {
        const float ccx = cx[i], ccy = cy[j];
        const float bx0 = i == 0 ? float(r.x) : float(r.r() - ir);
        const float by0 = j == 0 ? float(r.y) : float(r.b() - ir);
        const Rect save = m_clip;
        m_clip = m_clip.intersect(Rect(int(bx0), int(by0), ir, ir));
        shape(bx0, by0, bx0 + ir, by0 + ir, c, alpha, [&](float px, float py) {
            const float d = hyp(px - ccx, py - ccy) - rad;
            return clamp01(0.5f - d);
        });
        m_clip = save;
    }
}

void Canvas::roundFrame(const Rect& r, float rad, float w, Color c, int alpha)
{
    if (r.empty()) return;
    rad = std::min(rad, std::min(r.w, r.h) / 2.0f);
    const float x0 = float(r.x), y0 = float(r.y), x1 = float(r.r()), y1 = float(r.b());
    const int iw = int(std::ceil(w)) + 1;
    auto cov = [&](float px, float py) {
        // signed distance to the rounded rectangle
        const float qx = std::fabs(px - (x0 + x1) / 2) - ((x1 - x0) / 2 - rad);
        const float qy = std::fabs(py - (y0 + y1) / 2) - ((y1 - y0) / 2 - rad);
        const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        const float d = hyp(ox, oy) + std::min(std::max(qx, qy), 0.0f) - rad;
        // inside band [-w, 0]
        return clamp01(0.5f - d) * clamp01(0.5f + d + w);
    };
    // Only the band near the outline can be covered: top and bottom strips
    // across the full width, left and right strips between them.
    const int R = int(std::ceil(std::max(rad, w))) + 1;
    const Rect save = m_clip;
    const Rect strips[4] = { Rect(r.x, r.y, r.w, R), Rect(r.x, r.b() - R, r.w, R),
                             Rect(r.x, r.y + R, iw + 1, r.h - 2 * R),
                             Rect(r.r() - iw - 1, r.y + R, iw + 1, r.h - 2 * R) };
    for (const Rect& st : strips) {
        if (st.empty()) continue;
        m_clip = save.intersect(st);
        shape(float(st.x), float(st.y), float(st.r()), float(st.b()), c, alpha, cov);
    }
    m_clip = save;
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, Color c, int alpha)
{
    const float hw = width / 2;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    shape(std::min(x0, x1) - hw - 1, std::min(y0, y1) - hw - 1, std::max(x0, x1) + hw + 1,
          std::max(y0, y1) + hw + 1, c, alpha, [&](float px, float py) {
        float t = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float d = hyp(px - (x0 + t * dx), py - (y0 + t * dy));
        return clamp01(hw + 0.5f - d);
    });
}

void Canvas::dashed(float x0, float y0, float x1, float y1, float dash, float gap, float width,
                    Color c, int alpha)
{
    const float len = hyp(x1 - x0, y1 - y0);
    if (len <= 0) return;
    const float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
    for (float s = 0; s < len; s += dash + gap) {
        const float e = std::min(len, s + dash);
        line(x0 + ux * s, y0 + uy * s, x0 + ux * e, y0 + uy * e, width, c, alpha);
    }
}

void Canvas::circle(float cx, float cy, float r, Color c, int alpha)
{
    shape(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, c, alpha, [&](float px, float py) {
        return clamp01(r + 0.5f - hyp(px - cx, py - cy));
    });
}

void Canvas::ring(float cx, float cy, float r, float w, Color c, int alpha)
{
    shape(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, c, alpha, [&](float px, float py) {
        const float d = hyp(px - cx, py - cy);
        return clamp01(r + 0.5f - d) * clamp01(d - (r - w) + 0.5f);
    });
}

void Canvas::triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c, int alpha)
{
    // edge functions scaled to distances; coverage from the nearest edge
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-3f) return;
    const float s = area > 0 ? 1.0f : -1.0f;
    struct E { float a, b, c; };
    auto edge = [s](float ax, float ay, float bx, float by) {
        float a = (ay - by), b = (bx - ax);
        const float n = hyp(a, b);
        a /= n; b /= n;
        return E{ a * s, b * s, -(a * ax + b * ay) * s };
    };
    const E e0 = edge(x0, y0, x1, y1), e1 = edge(x1, y1, x2, y2), e2 = edge(x2, y2, x0, y0);
    shape(std::min(x0, std::min(x1, x2)) - 1, std::min(y0, std::min(y1, y2)) - 1,
          std::max(x0, std::max(x1, x2)) + 1, std::max(y0, std::max(y1, y2)) + 1, c, alpha,
          [&](float px, float py) {
        const float d0 = e0.a * px + e0.b * py + e0.c;
        const float d1 = e1.a * px + e1.b * py + e1.c;
        const float d2 = e2.a * px + e2.b * py + e2.c;
        return clamp01(0.5f + std::min(d0, std::min(d1, d2)));
    });
}

// ---------------------------------------------------------------------------
// text

uint32_t nextCp(const std::string& s, size_t* i)
{
    const unsigned char c = (unsigned char)s[*i];
    uint32_t cp = c;
    int len = 1;
    if (c >= 0xF0) { cp = c & 0x07; len = 4; }
    else if (c >= 0xE0) { cp = c & 0x0F; len = 3; }
    else if (c >= 0xC0) { cp = c & 0x1F; len = 2; }
    for (int k = 1; k < len && *i + k < s.size(); ++k)
        cp = (cp << 6) | ((unsigned char)s[*i + k] & 0x3F);
    *i += len;
    return cp;
}

const fonts::Glyph* glyph(const fonts::Font& f, uint32_t cp)
{
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (f.glyphs[m].cp == cp) return &f.glyphs[m];
        if (f.glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return nullptr;
}

static const fonts::Glyph* glyphOrQ(const fonts::Font& f, uint32_t cp)
{
    const fonts::Glyph* g = glyph(f, cp);
    if (!g && cp == 0x2212) g = glyph(f, '-');
    if (!g) g = glyph(f, '?');
    return g;
}

int Canvas::measure(const std::string& s, const fonts::Font& f)
{
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        const fonts::Glyph* g = glyphOrQ(f, nextCp(s, &i));
        if (g) w += g->adv;
    }
    return w;
}

int Canvas::text(int x, int y, const std::string& s, const fonts::Font& f, Color c, int alpha)
{
    const int x0 = x;
    const uint16_t solid = c.rgb565();
    for (size_t i = 0; i < s.size();) {
        const fonts::Glyph* g = glyphOrQ(f, nextCp(s, &i));
        if (!g) continue;
        const int gx = x + g->left, gy = y - g->top;
        if (gx < m_clip.r() && gx + g->w > m_clip.x && gy < m_clip.b() && gy + g->h > m_clip.y) {
            const int rb = (g->w + 1) / 2;
            const uint8_t* bits = f.bits + g->off;
            for (int yy = 0; yy < g->h; ++yy) {
                const int py = gy + yy;
                if (py < m_clip.y || py >= m_clip.b()) continue;
                uint16_t* row = m_px + py * m_w;
                for (int xx = 0; xx < g->w; ++xx) {
                    const int px = gx + xx;
                    if (px < m_clip.x || px >= m_clip.r()) continue;
                    const uint8_t b = bits[yy * rb + (xx >> 1)];
                    const int v = (xx & 1) ? (b & 0x0F) : (b >> 4);
                    if (!v) continue;
                    const int a = (v * 17 * alpha + 127) / 255;
                    if (a >= 255) row[px] = solid;
                    else row[px] = blend565(row[px], c, a);
                }
            }
        }
        x += g->adv;
    }
    return x - x0;
}

std::string Canvas::elide(const std::string& s, const fonts::Font& f, int w)
{
    if (measure(s, f) <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const int dw = measure(dots, f);
    std::string out;
    int cur = 0;
    for (size_t i = 0; i < s.size();) {
        const size_t start = i;
        const fonts::Glyph* g = glyphOrQ(f, nextCp(s, &i));
        const int a = g ? g->adv : 0;
        if (cur + a + dw > w) break;
        cur += a;
        out.append(s, start, i - start);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

void Canvas::label(const Rect& r, const std::string& s0, const fonts::Font& f, Color c, Align a,
                   int alpha)
{
    const std::string s = elide(s0, f, r.w);
    const int w = measure(s, f);
    const int x = a == Left ? r.x : (a == Center ? r.x + (r.w - w) / 2 : r.r() - w);
    // centre the cap height (≈ ascent*0.73) on the rect
    const int capH = int(f.size * 0.72f + 0.5f);
    const int y = r.y + (r.h + capH) / 2;
    text(x, y, s, f, c, alpha);
}

static std::vector<std::string> wrapLines(int w, const std::string& s, const fonts::Font& f)
{
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= s.size()) {
        size_t nl = s.find('\n', p);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(p, nl - p);
        std::string cur;
        size_t i = 0;
        while (i < para.size()) {
            size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            const std::string word = para.substr(i, j - i);
            const std::string cand = cur.empty() ? word : cur + " " + word;
            if (Canvas::measure(cand, f) <= w || cur.empty()) {
                if (cur.empty() && Canvas::measure(word, f) > w) {
                    // a word longer than the line: break it
                    std::string piece;
                    for (size_t k = 0; k < word.size();) {
                        const size_t st = k;
                        nextCp(word, &k);
                        const std::string np = piece + word.substr(st, k - st);
                        if (Canvas::measure(np, f) > w && !piece.empty()) { lines.push_back(piece); piece = word.substr(st, k - st); }
                        else piece = np;
                    }
                    cur = piece;
                } else cur = cand;
            } else {
                lines.push_back(cur);
                cur = word;
            }
            i = j + 1;
        }
        lines.push_back(cur);
        if (nl >= s.size()) break;
        p = nl + 1;
    }
    return lines;
}

int Canvas::wrapHeight(int w, const std::string& s, const fonts::Font& f, int maxLines, float lh)
{
    if (s.empty()) return 0;
    int n = int(wrapLines(w, s, f).size());
    if (maxLines > 0 && n > maxLines) n = maxLines;
    return int(n * f.size * lh + 0.5f);
}

int Canvas::textBox(const Rect& r, const std::string& s, const fonts::Font& f, Color c, Align a,
                    int maxLines, float lh, bool vcenter)
{
    if (s.empty()) return 0;
    std::vector<std::string> lines = wrapLines(r.w, s, f);
    if (maxLines > 0 && int(lines.size()) > maxLines) {
        std::string last = lines[size_t(maxLines - 1)];
        for (size_t k = size_t(maxLines); k < lines.size(); ++k) last += " " + lines[k];
        lines.resize(size_t(maxLines));
        lines.back() = elide(last, f, r.w);
        if (Canvas::measure(lines.back(), f) > r.w) lines.back() = elide(lines.back(), f, r.w);
    }
    const int step = int(f.size * lh + 0.5f);
    const int total = int(lines.size()) * step;
    int y = r.y + (vcenter ? (r.h - total) / 2 : 0);
    const int capH = int(f.size * 0.72f + 0.5f);
    for (const std::string& l : lines) {
        const int w = measure(l, f);
        const int x = a == Left ? r.x : (a == Center ? r.x + (r.w - w) / 2 : r.r() - w);
        text(x, y + (step + capH) / 2, l, f, c);
        y += step;
    }
    return total;
}

} // namespace gfx
