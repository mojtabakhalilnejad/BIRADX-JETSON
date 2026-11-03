#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ui {

using namespace fonts;

void Ui::begin(const Touch& t, uint32_t nowMs)
{
    m_now = nowMs;
    m_redraw = false;
    m_tap = Tap();
    m_wasDown = m_down;
    m_down = t.down;
    m_pressEdge = m_down && !m_wasDown;
    m_releaseEdge = !m_down && m_wasDown;
    if (m_down) { m_lastY = m_y; m_x = t.x; m_y = t.y; }
    // What the finger lifted from, among the controls of the frame it saw.
    if (m_releaseEdge && !m_scrolling) {
        m_tap.happened = true;
        m_tap.x = m_x; m_tap.y = m_y;
        for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
            if (it->r.contains(m_x, m_y)) {
                m_tap.found = true; m_tap.words = it->words; m_tap.live = it->live; m_tap.id = it->id; m_tap.why = it->why;
                break;
            }
    }
    m_hits.clear();
    m_whys.clear();
    if (m_pressEdge) {
        m_px = t.x; m_py = t.y; m_lastY = t.y;
        m_pressed = -1;
        m_scrolling = false;
        m_scrollId = -1;
    }
    // A drag of more than 12 px turns a press into a scroll.
    if (m_down && !m_scrolling && m_scrollId >= 0 && std::abs(m_y - m_py) > 12) {
        m_scrolling = true;
        m_pressed = -1;
    }
}

void Ui::end()
{
    if (m_releaseEdge) {
        m_pressed = -1;
        m_scrolling = false;
        m_scrollId = -1;
        m_redraw = true;
    }
    if (m_pressEdge) m_redraw = true;
}

bool Ui::pressedIn(const Rect& r) const
{
    return m_input && m_pressEdge && r.contains(m_px, m_py);
}

bool Ui::clicked(int id, const Rect& r, bool enabled)
{
    if (m_input) {
        std::string w;
        if (!enabled)
            for (auto it = m_whys.rbegin(); it != m_whys.rend(); ++it) if (it->first == id) { w = it->second; break; }
        m_hits.push_back({ r, m_nextName, enabled, id, w });
    }
    m_nextName.clear();
    if (!m_input || !enabled) return false;
    if (m_pressEdge && r.contains(m_px, m_py)) m_pressed = id;
    if (m_releaseEdge && m_pressed == id && !m_scrolling && r.contains(m_x, m_y)) {
        m_pressed = -1;
        m_redraw = true;
        return true;
    }
    return false;
}

Ui::Scroll* Ui::findScroll(int id)
{
    for (Scroll& s : m_scrolls) if (s.id == id) return &s;
    m_scrolls.push_back({ id, 0, 0 });
    return &m_scrolls.back();
}

void Ui::resetScroll(int id) { findScroll(id)->off = 0; }

int Ui::scroll(int id, const Rect& vp, int contentH)
{
    Scroll* s = findScroll(id);
    s->max = std::max(0, contentH - vp.h);
    if (m_input && m_pressEdge && vp.contains(m_px, m_py)) m_scrollId = id;
    if (m_input && m_scrolling && m_scrollId == id && m_down) {
        s->off -= (m_y - m_lastY);
        m_redraw = true;
    }
    s->off = std::max(0, std::min(s->off, s->max));
    // a thin indicator when the content does not fit
    if (s->max > 0) {
        const int barH = std::max(24, vp.h * vp.h / std::max(1, contentH));
        const int barY = vp.y + (vp.h - barH) * s->off / std::max(1, s->max);
        c.roundRect(Rect(vp.r() - 4, barY, 3, barH), 1.5f, th::strokeHi);
    }
    return s->off;
}

void Ui::card(const Rect& r, Color fill, Color border, int radius)
{
    c.roundRect(r, float(radius), border);
    c.roundRect(r.inset(th::strokeW), float(std::max(0, radius - th::strokeW)), fill);
}

// BigButton.qml
bool Ui::button(int id, const Rect& r, const std::string& text, const Btn& st)
{
    m_nextName = text;
    const bool click = clicked(id, r, st.enabled);
    const bool prs = pressed(id);
    const Color base = st.toned ? st.tone : (st.danger ? th::bad : th::accent);
    // BigButton text is 19 px bold; a label that does not fit steps down
    // rather than overflowing the button as the QML one did.
    const Font* fp = st.font ? st.font : &SB19;
    if (gfx::Canvas::measure(text, *fp) > r.w - 12 && fp == &SB19) fp = &SB16;
    if (gfx::Canvas::measure(text, *fp) > r.w - 12 && fp == &SB16) fp = &SB14;
    const Font& f = *fp;
    if (!st.enabled) {
        card(r, th::bg, th::stroke);
        c.label(r.inset(6, 0), text, f, th::textFaint, gfx::Center);
    } else if (st.primary) {
        c.roundRect(r, th::radiusSm, prs ? gfx::darker(base, 1.2f) : base);
        c.label(r.inset(6, 0), text, f, th::inkOnAccent, gfx::Center);
    } else {
        card(r, prs ? th::accentSoft : th::bg, base);
        c.label(r.inset(6, 0), text, f, base, gfx::Center);
    }
    return click;
}

bool Ui::button(int id, const Rect& r, const std::string& text, bool primary, bool enabled)
{
    Btn b;
    b.primary = primary;
    b.enabled = enabled;
    return button(id, r, text, b);
}

bool Ui::outline(int id, const Rect& r, const std::string& text, Color tone, bool enabled)
{
    m_nextName = text;
    const bool click = clicked(id, r, enabled);
    const bool prs = pressed(id);
    card(r, prs ? th::accentSoft : th::bg, enabled ? tone : th::stroke);
    c.label(r.inset(6, 0), text, SB14, enabled ? tone : th::textFaint, gfx::Center);
    return click;
}

bool Ui::chip(int id, const Rect& r, const std::string& text, bool on, bool enabled)
{
    m_nextName = text;
    const bool click = clicked(id, r, enabled);
    const bool prs = pressed(id);
    if (on) c.roundRect(r, th::radiusSm, th::accent);
    else card(r, prs ? th::accentSoft : th::surfaceIn, th::stroke);
    c.label(r.inset(6, 0), text, on ? SB16 : S16, on ? th::bg : (enabled ? th::text : th::textFaint),
            gfx::Center);
    return click;
}

bool Ui::toggle(int id, const Rect& r, bool on, bool enabled)
{
    if (m_nextName.empty()) m_nextName = on ? "switch (on)" : "switch (off)";
    const bool click = clicked(id, r, enabled);
    // track 52x28 at the right of r
    const Rect t(r.r() - 56, r.y + (r.h - 30) / 2, 54, 30);
    c.roundRect(t, 15, on ? (enabled ? th::accent : th::strokeHi) : th::surfaceIn);
    if (!on) c.roundFrame(t, 15, 2, th::stroke);
    const float kx = on ? t.r() - 15.0f : t.x + 15.0f;
    c.circle(kx, t.y + 15.0f, 11, on ? th::inkOnAccent : th::textDim);
    return click;
}

} // namespace ui
