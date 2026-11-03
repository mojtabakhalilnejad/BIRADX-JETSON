// gui/ui.h — the theme (qml/Theme.qml of 2.18.0) and a small immediate-mode
// toolkit: each frame draws the whole screen and asks, per control, whether
// it was pressed or clicked. There is no widget tree to fall out of step with
// the data — the screen is always drawn from the App's current state, which
// is what "views must follow the data" (spec §16.11) needs.
#ifndef BIRADX_UI_H
#define BIRADX_UI_H

#include "gfx.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

using gfx::Color;
using gfx::Rect;

namespace th {   // Theme.qml
constexpr Color bg        = gfx::hex(0x0E1418);
constexpr Color surface   = gfx::hex(0x141C21);
constexpr Color surfaceHi = gfx::hex(0x1B252B);
constexpr Color surfaceIn = gfx::hex(0x222E35);
constexpr Color stroke    = gfx::hex(0x3A4A53);
constexpr Color strokeHi  = gfx::hex(0x5C7079);
constexpr Color text      = gfx::hex(0xF2F6F8);
constexpr Color textDim   = gfx::hex(0xB4C2C9);
constexpr Color textFaint = gfx::hex(0x8296A0);
constexpr Color accent    = gfx::hex(0x4FB3E8);
constexpr Color accentSoft= gfx::hex(0x16323F);
// The patient's own healthy tissue, wherever it is shown: a colour no rule, no
// tier and no other part of the interface uses, so it cannot be taken for one.
constexpr Color healthy   = gfx::hex(0xF06BFF);
constexpr Color healthySoft = gfx::hex(0x33173A);
constexpr Color good      = gfx::hex(0x4BD07C);
constexpr Color goodSoft  = gfx::hex(0x12301F);
constexpr Color warn      = gfx::hex(0xF0B24A);
constexpr Color warnSoft  = gfx::hex(0x33260E);
constexpr Color bad       = gfx::hex(0xFF6B6B);
constexpr Color badSoft   = gfx::hex(0x3A1A1C);
constexpr Color inkOnAccent = gfx::hex(0x08131A);
constexpr int radius = 10, radiusSm = 6, gap = 10, pad = 12, touch = 56, touchSm = 46;
constexpr int strokeW = 2;
}

struct Touch {
    bool down = false;
    int  x = 0, y = 0;
};

class Ui {
public:
    explicit Ui(const gfx::Canvas& cv) : c(cv) {}
    gfx::Canvas c;       // a view of the frame buffer being drawn

    // Called once per frame with the current touch state.
    void begin(const Touch& t, uint32_t nowMs);
    void end();
    bool active() const { return m_down || m_pressEdge || m_releaseEdge; }
    bool wantsRedraw() const { return m_redraw; }
    void redraw() { m_redraw = true; }
    uint32_t now() const { return m_now; }

    // Input is ignored while false (the screen under a modal sheet).
    void setInput(bool on) { m_input = on; }

    // Hit testing. `clicked` is true on the frame the finger lifts inside the
    // rect it went down in. `pressed` is true while it is held there.
    bool clicked(int id, const Rect& r, bool enabled = true);
    bool pressed(int id) const { return m_pressed == id && m_down && !m_scrolling; }
    bool pressedIn(const Rect& r) const;       // any press inside r this frame

    // Vertical scrolling. Returns the offset to subtract from content y.
    int  scroll(int id, const Rect& viewport, int contentH);
    void resetScroll(int id);

    // ---- widgets (the look of BigButton.qml, Chip.qml, Switch2.qml …)
    struct Btn { bool primary = true; bool danger = false; bool enabled = true;
                 const fonts::Font* font = nullptr; Color tone = th::accent; bool toned = false; };
    bool button(int id, const Rect& r, const std::string& text, const Btn& st);
    bool button(int id, const Rect& r, const std::string& text, bool primary = true, bool enabled = true);
    bool chip(int id, const Rect& r, const std::string& text, bool on, bool enabled = true);
    bool toggle(int id, const Rect& r, bool on, bool enabled = true);   // a switch
    bool outline(int id, const Rect& r, const std::string& text, Color tone, bool enabled = true);
    void card(const Rect& r, Color fill = th::surface, Color border = th::stroke, int radius = th::radiusSm);

    // ---- what a tap landed on (diagnostic mode, 2.19.0 DiagTapFilter)
    // Every control registers its rect, words and whether it was live; on the
    // frame the finger lifts, the topmost one under it is the answer.
    void name(const std::string& words) { m_nextName = words; }   // for the next clicked()
    // H7-2.12.2: why control `id` is off and what to do first, said when it is tapped while off (this frame)
    void why(int id, const std::string& w) { m_whys.push_back({ id, w }); }
    struct Tap { bool happened = false; bool found = false; std::string words; bool live = true; int id = -1, x = 0, y = 0;
                 std::string why; };
    const Tap& tap() const { return m_tap; }

    // Is the finger currently on the screen (for scroll/drag feedback).
    bool down() const { return m_down; }
    int  x() const { return m_x; }
    int  y() const { return m_y; }

private:
    bool m_input = true;
    bool m_down = false, m_wasDown = false, m_pressEdge = false, m_releaseEdge = false;
    int  m_x = 0, m_y = 0, m_px = 0, m_py = 0, m_lastY = 0;
    int  m_pressed = -1, m_pressedCandidate = -1;
    bool m_scrolling = false;
    int  m_scrollId = -1;
    bool m_redraw = false;
    uint32_t m_now = 0;
    struct Hit { Rect r; std::string words; bool live; int id; std::string why; };
    std::vector<Hit> m_hits;
    std::string m_nextName;
    std::vector<std::pair<int, std::string>> m_whys;
    Tap m_tap;
    struct Scroll { int id; int off; int max; };
    std::vector<Scroll> m_scrolls;
    Scroll* findScroll(int id);
};

} // namespace ui

#endif
