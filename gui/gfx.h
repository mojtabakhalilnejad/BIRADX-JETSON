// gui/gfx.h — drawing into an RGB565 frame buffer.
//
// Everything is drawn by the CPU except plain rectangle fills, which go to
// the board's DMA2D through `fillHook` when one is installed. Shapes are
// anti-aliased with a signed-distance estimate at their edges, so a line or a
// rounded card looks the same on the instrument as in the simulator.
#ifndef BIRADX_GFX_H
#define BIRADX_GFX_H

#include "../fonts/fonts.h"
#include <cstdint>
#include <string>

namespace gfx {

struct Color {
    uint8_t r = 0, g = 0, b = 0;
    constexpr Color() {}
    constexpr Color(uint8_t R, uint8_t G, uint8_t B) : r(R), g(G), b(B) {}
    uint16_t rgb565() const { return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
};
constexpr Color hex(uint32_t v) { return Color(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)); }
bool parseColor(const std::string& s, Color* out);     // "#rrggbb"
Color mix(Color a, Color b, float t);                   // a..b
Color lighter(Color c, float f);                        // Qt.lighter
Color darker(Color c, float f);                         // Qt.darker

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    Rect() {}
    Rect(int X, int Y, int W, int H) : x(X), y(Y), w(W), h(H) {}
    int r() const { return x + w; }
    int b() const { return y + h; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(int d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
    Rect inset(int dx, int dy) const { return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
    Rect intersect(const Rect& o) const;
    bool empty() const { return w <= 0 || h <= 0; }
};

using FillHook = void (*)(uint16_t* dst, int w, int h, int stride, uint16_t color);
extern FillHook fillHook;

enum Align { Left = 0, Center = 1, Right = 2 };

class Canvas {
public:
    Canvas(uint16_t* px, int w, int h) : m_px(px), m_w(w), m_h(h), m_clip(0, 0, w, h) {}
    int width() const { return m_w; }
    int height() const { return m_h; }
    uint16_t* pixels() const { return m_px; }

    void setClip(const Rect& r) { m_clip = r.intersect(Rect(0, 0, m_w, m_h)); }
    void resetClip() { m_clip = Rect(0, 0, m_w, m_h); }
    Rect clip() const { return m_clip; }

    void fill(const Rect& r, Color c, int alpha = 255);
    void pixel(int x, int y, Color c, int alpha);           // 0..255, blended
    void frame(const Rect& r, Color c, int width = 1, int alpha = 255);
    void roundRect(const Rect& r, float radius, Color c, int alpha = 255);
    void roundFrame(const Rect& r, float radius, float width, Color c, int alpha = 255);
    void line(float x0, float y0, float x1, float y1, float width, Color c, int alpha = 255);
    void dashed(float x0, float y0, float x1, float y1, float dash, float gap, float width,
                Color c, int alpha = 255);
    void circle(float cx, float cy, float r, Color c, int alpha = 255);
    void ring(float cx, float cy, float r, float width, Color c, int alpha = 255);
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c,
                  int alpha = 255);

    // Text. y is the BASELINE. Returns the advance.
    int text(int x, int y, const std::string& s, const fonts::Font& f, Color c, int alpha = 255);
    static int measure(const std::string& s, const fonts::Font& f);
    // Text in a box: word-wrapped, at most maxLines, the last line elided with "…".
    // Returns the height used.
    int textBox(const Rect& r, const std::string& s, const fonts::Font& f, Color c,
                Align a = Left, int maxLines = 0, float lineHeight = 1.2f, bool vcenter = false);
    static int wrapHeight(int w, const std::string& s, const fonts::Font& f, int maxLines = 0,
                          float lineHeight = 1.2f);
    // One line, aligned in the rect, vertically centred, elided if too wide.
    void label(const Rect& r, const std::string& s, const fonts::Font& f, Color c,
               Align a = Left, int alpha = 255);
    static std::string elide(const std::string& s, const fonts::Font& f, int w);

private:
    void span(int x0, int x1, int y, uint16_t c565);
    template <class Cov> void shape(float x0, float y0, float x1, float y1, Color c, int alpha, Cov cov);
    uint16_t* m_px;
    int m_w, m_h;
    Rect m_clip;
};

// UTF-8 → code points
uint32_t nextCp(const std::string& s, size_t* i);
const fonts::Glyph* glyph(const fonts::Font& f, uint32_t cp);

} // namespace gfx

#endif
