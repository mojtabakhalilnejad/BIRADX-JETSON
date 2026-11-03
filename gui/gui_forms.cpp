// gui/gui_forms.cpp — the intake form (IntakeForm.qml), the number pad and
// keyboard (NumberPadSheet / TextPadSheet / InputPad), the settings lock
// (PasswordPage + DigitWheel), the passcode change (PasscodeSheet) and the
// clock sheet (new: the Jetson took its time from the operating system).
#include "gui.h"
#include "../birxcore/patient.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace fonts;
using gfx::Rect;
using gfx::Color;
namespace th = ui::th;

namespace gui {

namespace {
enum Id {
    ID_I_CANCEL = 900, ID_I_NEXT, ID_I_START, ID_I_SCROLL, ID_I_FIELD = 1000,
    ID_P_KEY = 3000, ID_P_CANCEL = 3100, ID_P_DONE, ID_P_SHIFT, ID_P_SYM, ID_P_SPACE, ID_P_BS,
    ID_L_CLOSE = 3200, ID_L_DIAL = 3210,
    ID_PS_CANCEL = 3300, ID_PS_NEXT, ID_PS_DIAL = 3310,
    ID_CK_CANCEL = 3400, ID_CK_SAVE, ID_CK_STEP = 3410,
};
std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%g", v); return b; }
}

// ===========================================================================
// the intake form

int Gui::fieldEditor(int id, const Rect& r, const std::string& fid, const std::string& value,
                     const std::function<void(const std::string&)>& set)
{
    const birx::IntakeField* f = birx::intakeField(fid);
    if (!f) return 0;
    int y = r.y;
    int x = r.x;
    x += c->text(x, y + 20, f->label, SB19, th::text) + th::gap;
    if (f->required) x += c->text(x, y + 19, "required", S12, th::bad) + th::gap;
    if (f->stratifier) c->text(x, y + 19, "splits the analysis", S12, th::textFaint);
    y += 24 + th::gap;
    switch (f->type) {
    case birx::FieldType::Number: {
        const Rect box(r.x, y, r.w, th::touch);
        if (u->clicked(id, box)) {
            const std::string fidc = fid;
            openNumber(f->label, value, f->unit, f->min, f->max, true, false, true,
                       [set](const std::string& v) { set(v); });
        }
        u->card(box, u->pressed(id) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->text(box.x + th::pad, box.y + 45, value.empty() ? "\xE2\x80\x94" : value, MB46,
                value.empty() ? th::textFaint : th::text);
        c->label(Rect(box.x, box.y, box.w - th::pad, box.h), f->unit, S14, th::textDim, gfx::Right);
        y += th::touch;
        break;
    }
    case birx::FieldType::Choice: {
        int cx = r.x;
        int k = 0;
        for (const std::string& o : f->options) {
            const int w = std::max(th::touch, gfx::Canvas::measure(o, SB19) + th::pad * 2);
            if (cx + w > r.r()) { cx = r.x; y += th::touch + th::gap; }
            const bool on = value == o;
            if (u->chip(id * 32 + k, Rect(cx, y, w, th::touch), o, on)) set(on ? std::string() : o);
            cx += w + th::gap;
            ++k;
        }
        y += th::touch;
        break;
    }
    case birx::FieldType::Toggle: {
        const char* opts[2] = { "No", "Yes" };
        for (int k = 0; k < 2; ++k) {
            const bool on = value == opts[k];
            if (u->chip(id * 32 + k, Rect(r.x + k * (130 + th::gap), y, 130, th::touch), opts[k], on))
                set(on ? std::string() : std::string(opts[k]));
        }
        y += th::touch;
        break;
    }
    case birx::FieldType::Text: {
        const Rect box(r.x, y, r.w, th::touch);
        if (u->clicked(id, box))
            openText(f->label, value, fid == "notes" ? 200 : 40, [set](const std::string& v) { set(v); });
        u->card(box, u->pressed(id) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->label(box.inset(th::pad, 0), value.empty() ? "tap to type" : value, S16,
                 value.empty() ? th::textFaint : th::text);
        y += th::touch;
        break;
    }
    }
    if (fid == "needle_lot") {
        // the lots of the lot file, to choose from, and what the device will do with the one typed (H7-2.4.0)
        const std::vector<std::string> ch = a.lotChoices();
        if (!ch.empty()) {
            c->text(r.x, y + 22, "IN THE LOT FILE", SB11, th::textFaint);
            y += 28;
            int cx = r.x;
            for (size_t k = 0; k < ch.size() && k < 12; ++k) {
                const std::string t = ch[k] + (ch[k] == a.lotFile().refLot ? "  (reference)" : "");
                const int w = std::min(gfx::Canvas::measure(t, SB16) + 24, r.w);
                if (cx + w > r.r()) { cx = r.x; y += 48 + 10; }
                const bool on = value == ch[k];
                if (u->chip(id * 32 + 16 + int(k), Rect(cx, y, w, 48), t, on)) set(ch[k]);
                cx += w + th::gap;
            }
            y += 48;
        }
        y += 8;
        const std::string plan = a.lotPlanText(value);
        const bool warn = plan.find("NOT") != std::string::npos || plan.find("no reading") != std::string::npos;
        const int h = gfx::Canvas::wrapHeight(r.w - 24, plan, S14, 6, 1.25f);
        u->card(Rect(r.x, y, r.w, h + 20), warn ? th::warnSoft : th::accentSoft, warn ? th::warn : th::accent);
        c->textBox(Rect(r.x + 12, y + 10, r.w - 24, h), plan, S14, warn ? th::warn : th::text, gfx::Left, 6, 1.25f);
        y += h + 20;
    }
    if (fid == "needle_serial") {
        y += 6;
        const std::string st = a.needleStatus();
        std::string t;
        Color col = th::textFaint;
        if (!a.settings().needleCheck) t = "recorded as typed \xE2\x80\x94 codes are not being verified";
        else if (st == "OK") { t = "\xE2\x9C\x93 accepted"; col = th::good; }
        else if (a.intakeValue("needle_serial").empty()) t = "9 characters, printed on the packet";
        else { t = st; col = th::bad; }
        c->text(r.x, y + 14, t, MB14, col);
        y += 20;
    }
    if (!f->help.empty()) {
        y += 6;
        y += c->textBox(Rect(r.x, y, r.w, 60), f->help, S14, th::textDim, gfx::Left, 3, 1.2f);
    }
    return y - r.y;
}

void Gui::intake()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    // header
    c->fill(Rect(0, 0, scrW, 80), th::surface);
    c->fill(Rect(0, 78, scrW, 2), th::stroke);
    c->text(th::pad, 32, "New test", SB24, th::text);
    if (a.caseOpen()) c->text(th::pad, 62, a.caseId(), MB19, th::accent);
    else c->text(th::pad, 62, "no case id \xE2\x80\x94 this device has no assigned id", SB16, th::bad);
    for (int k = 0; k < 3; ++k)
        c->roundRect(Rect(scrW - th::pad - 3 * 30 - 2 * 8 + k * 38, 37, 30, 6), 3,
                     k + 1 <= m_intakePage ? th::accent : th::stroke);

    // the fields of this page, scrolled
    const Rect body(0, 80, scrW, scrH - 80 - 96);
    std::vector<const birx::IntakeField*> fs;
    for (const birx::IntakeField& f : birx::intakeSchema()) if (f.page == m_intakePage) fs.push_back(&f);
    static int contentH = 800;
    const int off = u->scroll(ID_I_SCROLL + m_intakePage * 7, body, contentH);
    c->setClip(body);
    int y = body.y + th::pad - off;
    int k = 0;
    for (const birx::IntakeField* f : fs) {
        const std::string id = f->id;
        y += fieldEditor(ID_I_FIELD + k * 40 + m_intakePage * 400, Rect(th::pad, y, scrW - 2 * th::pad - 8, 0), id,
                         a.intakeValue(id), [this, id](const std::string& v) { a.setIntakeValue(id, v); });
        y += th::gap * 2;
        ++k;
    }
    contentH = y + off - body.y + th::pad;
    c->resetClip();

    // footer
    const Rect foot(0, scrH - 96, scrW, 96);
    c->fill(foot, th::surface);
    c->fill(Rect(0, foot.y, scrW, 2), th::stroke);
    std::string missing;
    for (const std::string& m : a.intakeMissing()) missing += (missing.empty() ? "" : ", ") + m;
    if (!m_intakeMsg.empty())
        c->label(Rect(th::pad, foot.y + 6, scrW - 2 * th::pad, 20), m_intakeMsg, SB14, th::bad, gfx::Center);
    else if (m_intakePage == 1 && !a.intakeComplete() && a.caseOpen())
        c->label(Rect(th::pad, foot.y + 6, scrW - 2 * th::pad, 20), "Still needed: " + missing, S14, th::bad, gfx::Center);
    const int by = foot.y + 32;
    const Rect b1(scrW / 2 - 260, by, 140, th::touch), b2(b1.r() + th::gap, by, 160, th::touch),
               b3(b2.r() + th::gap, by, 200, th::touch);
    // IntakeForm.qml: Cancel only goes back. The case whose id is at the top of
    // this form stays OPEN, with no patient row — readings can be taken under
    // it at once, and its details entered later from Logs. (Main.qml:
    // onCancelled: currentScreen = "measure".)
    if (u->button(ID_I_CANCEL, b1, m_intakePage == 1 ? "Cancel" : "Back", false, true)) {
        if (m_intakePage > 1) --m_intakePage;
        else m_scr = Measure;
    }
    u->why(ID_I_NEXT, "Still needed first: " + missing + ".");
    u->why(ID_I_START, !a.intakeComplete() ? "Still needed first: " + missing + "."
                       : "The needle serial is not accepted (" + a.needleStatus() + "): check it as printed on the packet.");
    if (m_intakePage < 3 &&
        u->button(ID_I_NEXT, b2, m_intakePage == 1 ? "More detail" : "Next", false,
                  m_intakePage > 1 || a.intakeComplete()))
        ++m_intakePage;
    {
        ui::Ui::Btn st;
        st.enabled = a.intakeComplete() && a.needleAccepted();
        st.font = &SB19;
        if (u->button(ID_I_START, b3, "Start test", st)) {
            std::string why;
            if (a.commitPatient(&why)) { m_scr = Measure; m_intakeMsg.clear(); }
            else m_intakeMsg = why;
        }
    }
}

// ===========================================================================
// number pad and keyboard

void Gui::openNumber(const std::string& title, const std::string& value, const std::string& unit,
                     double lo, double hi, bool bounded, bool allowNeg, bool allowDot,
                     std::function<void(const std::string&)> done)
{
    m_pad = Pad();
    m_pad.open = true;
    m_pad.numeric = true;
    m_pad.title = title;
    m_pad.value = value;
    m_pad.unit = unit;
    m_pad.lo = lo; m_pad.hi = hi; m_pad.bounded = bounded;
    m_pad.allowNeg = allowNeg; m_pad.allowDot = allowDot;
    m_pad.maxLen = 12;
    m_pad.done = std::move(done);
}

void Gui::openText(const std::string& title, const std::string& value, int maxLen,
                   std::function<void(const std::string&)> done, const std::string& help)
{
    m_pad = Pad();
    m_pad.open = true;
    m_pad.numeric = false;
    m_pad.title = title;
    m_pad.value = value;
    m_pad.help = help;
    m_pad.maxLen = maxLen;
    m_pad.done = std::move(done);
}

void Gui::padSheet()
{
    Pad& p = m_pad;
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 248);
    const int W = scrW - th::pad * 2;
    int y = 16;
    c->text(th::pad, y + 24, p.title, SB24, th::text);
    y += 34;
    bool ok = true;
    if (p.numeric) {
        double v = 0;
        const bool parsed = !p.value.empty() && p.value != "-" && birx::birxStrtod(p.value, &v);
        ok = parsed && (!p.bounded || (v >= p.lo && v <= p.hi));
        if (p.bounded) {
            const std::string rng = "between " + num(p.lo) + " and " + num(p.hi) + " " + p.unit;
            c->text(th::pad, y + 16, rng, S14, !p.value.empty() && !ok ? th::bad : th::textFaint);
        }
        y += 24;
    } else if (!p.help.empty()) {
        c->text(th::pad, y + 16, p.help, S14, th::textFaint);
        y += 24;
    }
    const Rect box(th::pad, y, W, th::touch);
    u->card(box, th::surfaceIn, !p.value.empty() && !ok ? th::bad : th::stroke);
    const std::string shown = p.value.empty() ? "\xE2\x80\x94" : p.value;
    if (p.numeric) c->text(box.x + th::pad, box.y + 45, shown, MB46, p.value.empty() ? th::textFaint : th::text);
    else {
        const std::string vis = gfx::Canvas::elide(p.value, S19, box.w - 40);
        const int tw = c->text(box.x + th::pad, box.y + 36, p.value.empty() ? "" : vis, S19, th::text);
        if ((u->now() / 500) % 2 == 0) c->fill(Rect(box.x + th::pad + tw + 2, box.y + 14, 2, 28), th::accent);
        m_timerAt = u->now() + 500;
    }
    c->label(Rect(box.x, box.y, box.w - th::pad, box.h), p.unit, S14, th::textDim, gfx::Right);
    y = box.b() + th::gap;

    auto put = [&p](const std::string& s) { if (int(p.value.size()) < p.maxLen) p.value += s; };
    if (p.numeric) {
        // 1 2 3 / 4 5 6 / 7 8 9 / . 0 ⌫  (+ ± when negative values are allowed)
        const char* keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "\xE2\x8C\xAB" };
        const int cols = p.allowNeg ? 4 : 3;
        // ITDS-2.16.0: upright, taller keys, as wide as four still fit
        const int kw = std::min(150, (scrW - 2 * th::pad - (cols - 1) * th::gap) / cols), kh = portrait() ? 84 : 62;
        const int gx = (scrW - (cols * kw + (cols - 1) * th::gap)) / 2;
        for (int i = 0; i < 12; ++i) {
            const Rect k(gx + (i % 3) * (kw + th::gap), y + (i / 3) * (kh + th::gap), kw, kh);
            const std::string key = keys[i];
            const bool en = key != "." || (p.allowDot && p.value.find('.') == std::string::npos);
            u->why(ID_P_KEY + i, p.allowDot ? "One decimal point at most." : "Whole numbers only here.");
            if (u->clicked(ID_P_KEY + i, k, en)) {
                if (i == 11) { if (!p.value.empty()) p.value.pop_back(); }
                else if (key == ".") put(p.value.empty() || p.value == "-" ? "0." : ".");
                else put(key);
            }
            u->card(k, u->pressed(ID_P_KEY + i) ? th::accentSoft : th::surfaceIn, th::stroke);
            c->label(k, key == "\xE2\x8C\xAB" ? "\xE2\x86\x90" : key, MB24, en ? th::text : th::textFaint, gfx::Center);
        }
        if (p.allowNeg) {
            const Rect k(gx + 3 * (kw + th::gap), y, kw, kh);
            if (u->clicked(ID_P_KEY + 20, k)) {
                if (!p.value.empty() && p.value[0] == '-') p.value.erase(0, 1); else p.value = "-" + p.value;
            }
            u->card(k, u->pressed(ID_P_KEY + 20) ? th::accentSoft : th::surfaceIn, th::stroke);
            c->label(k, "\xC2\xB1", MB24, th::text, gfx::Center);
            const Rect k2(gx + 3 * (kw + th::gap), y + kh + th::gap, kw, kh);
            if (u->clicked(ID_P_KEY + 21, k2)) p.value.clear();
            u->card(k2, u->pressed(ID_P_KEY + 21) ? th::accentSoft : th::surfaceIn, th::stroke);
            c->label(k2, "C", MB24, th::text, gfx::Center);
        }
        y += 4 * (kh + th::gap);
    } else {
        static const char* rowsA[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
        static const char* rowsS[4] = { "1234567890", "-_.,:;/()", "+=#%&*!?@", "'\"<>[]{}" };
        const char** rows = p.sym ? rowsS : rowsA;
        // ITDS-2.16.0: on a narrow screen ten keys across it, and taller
        const int kw = std::min(90, (scrW - 2 * th::pad - 9 * 8) / 10), kh = portrait() ? 64 : 54;
        for (int rI = 0; rI < 4; ++rI) {
            const std::string row = rows[rI];
            const int n = int(row.size());
            int x = (scrW - (n * kw + (n - 1) * 8)) / 2;
            if (rI == 3 && !p.sym) x += kw < 90 ? kw / 2 : 40;
            for (int i = 0; i < n; ++i) {
                std::string ch(1, row[size_t(i)]);
                if (p.shift && !p.sym) ch[0] = char(std::toupper((unsigned char)ch[0]));
                const Rect k(x + i * (kw + 8), y + rI * (kh + 8), kw, kh);
                const int id = ID_P_KEY + rI * 16 + i;
                if (u->clicked(id, k)) { put(ch); if (p.shift) p.shift = false; }
                u->card(k, u->pressed(id) ? th::accentSoft : th::surfaceIn, th::stroke);
                c->label(k, ch, S19, th::text, gfx::Center);
            }
            if (rI == 3 && !p.sym) {
                const Rect sh = kw < 90 ? Rect(x - kw - kw / 2 - 8, y + rI * (kh + 8), kw + kw / 2, kh)
                                        : Rect(x - kw - 48, y + rI * (kh + 8), kw + 40, kh);
                if (u->clicked(ID_P_SHIFT, sh)) p.shift = !p.shift;
                u->card(sh, p.shift ? th::accent : th::surfaceIn, th::stroke);
                c->label(sh, "\xE2\x86\x91", SB19, p.shift ? th::bg : th::text, gfx::Center);
            }
        }
        const int by = y + 4 * (kh + 8);
        const int side = portrait() ? th::pad + 20 : th::pad + 60, sw = portrait() ? 110 : 130;
        const Rect sym(side, by, sw, kh), space(sym.r() + 8, by, scrW - 2 * side - 2 * (sw + 8), kh),
                   bs(space.r() + 8, by, sw, kh);
        if (u->clicked(ID_P_SYM, sym)) p.sym = !p.sym;
        u->card(sym, u->pressed(ID_P_SYM) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->label(sym, p.sym ? "abc" : "?123", S16, th::text, gfx::Center);
        if (u->clicked(ID_P_SPACE, space)) put(" ");
        u->card(space, u->pressed(ID_P_SPACE) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->label(space, "space", S16, th::textDim, gfx::Center);
        if (u->clicked(ID_P_BS, bs)) { if (!p.value.empty()) { size_t i = p.value.size() - 1; while (i > 0 && (p.value[i] & 0xC0) == 0x80) --i; p.value.erase(i); } }
        u->card(bs, u->pressed(ID_P_BS) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->label(bs, "\xE2\x86\x90", SB19, th::text, gfx::Center);
        y = by + kh + th::gap;
    }
    const int by = std::min(y + 4, scrH - th::touch - 8);
    if (u->button(ID_P_CANCEL, Rect(scrW / 2 - 230, by, 220, th::touch), "Cancel", false, true)) p.open = false;
    u->why(ID_P_DONE, p.bounded ? "Enter a number between " + num(p.lo) + " and " + num(p.hi) + " " + p.unit + "." : std::string("Enter a number."));
    if (u->button(ID_P_DONE, Rect(scrW / 2 + 10, by, 240, th::touch), "Done", true, ok)) {
        std::string v = p.value;
        if (!p.numeric) {
            while (!v.empty() && v.back() == ' ') v.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        } else if (!v.empty() && v.back() == '.') v.pop_back();
        p.open = false;
        if (p.done) p.done(v);
    }
}

// ===========================================================================
// the lock: four rolling dials, and nothing else — no words (spec §13.4)

void Gui::dials(const Rect& area, int* d, bool okGlow, int idBase)
{
    const int gapW = 12;
    const int cellW = std::min(132, (area.w - gapW * 3) / 4);
    const int cellH = std::max(52, std::min(80, area.h / 5));
    const int W = cellW * 4 + gapW * 3, H = cellH * 5;
    const int x0 = area.x + (area.w - W) / 2, y0 = area.y + (area.h - H) / 2;
    // the window across the middle
    const Rect win(x0 - 10, y0 + 2 * cellH - 3, W + 20, cellH + 6);
    u->card(win, okGlow ? th::goodSoft : th::surfaceIn, okGlow ? th::good : th::strokeHi);
    for (int k = 0; k < 4; ++k) {
        const Rect col(x0 + k * (cellW + gapW), y0, cellW, H);
        const int id = idBase + k;
        // dragging rolls the wheel; a tap on a digit turns it there
        float frac = 0;
        if (u->pressedIn(col)) { m_dialDrag = idBase * 10 + k; m_dialDragY0 = u->y(); m_dialDragV0 = d[k]; }
        const bool dragging = m_dialDrag == idBase * 10 + k && u->down();
        if (dragging) {
            const int dy = u->y() - m_dialDragY0;
            const float steps = float(dy) / cellH;
            const int whole = int(std::floor(steps + 0.5f));
            d[k] = ((m_dialDragV0 - whole) % 10 + 10) % 10;
            frac = steps - whole;
            if (std::abs(dy) > 8) u->redraw();
        }
        if (m_dialDrag == idBase * 10 + k && !u->down()) {
            const int dy = u->y() - m_dialDragY0;
            if (std::abs(dy) <= 8) {
                // a tap: the cell under the finger
                const int cell = (u->y() - y0) / cellH;      // 0..4, 2 is the middle
                if (cell >= 0 && cell < 5) d[k] = ((m_dialDragV0 + cell - 2) % 10 + 10) % 10;
            }
            m_dialDrag = -1;
            m_lockSettle = u->now() + 350;
            m_timerAt = m_lockSettle;
        }
        (void)id;
        c->setClip(col);
        for (int j = -3; j <= 3; ++j) {
            const float cy = y0 + (2 + j + frac) * cellH + cellH / 2.0f;
            const float dist = std::fabs(cy - (y0 + 2.5f * cellH)) / (2.5f * cellH);
            const int alpha = std::max(0, int(255 * (1.0f - dist)));
            const int digit = ((d[k] + j) % 10 + 10) % 10;
            const std::string s(1, char('0' + digit));
            const Font& f = (std::fabs(float(j) + frac) < 0.5f) ? MB64 : MB46;
            const int tw = gfx::Canvas::measure(s, f);
            c->text(col.x + (cellW - tw) / 2, int(cy + f.size * 0.36f), s, f, th::text, alpha);
        }
        c->resetClip();
    }
}

void Gui::lock()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    const Rect close(scrW - th::pad - 46, th::pad, 46, 44);
    if (u->clicked(ID_L_CLOSE, close)) { m_scr = Measure; return; }
    u->card(close, u->pressed(ID_L_CLOSE) ? th::accentSoft : th::bg, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, th::textDim, gfx::Center);
    dials(Rect(th::pad * 2, 60, scrW - th::pad * 4, portrait() ? scrH - 180 : 480), m_dial, m_lockOpened, ID_L_DIAL);
    if (m_lockSettle) m_timerAt = m_lockSettle;
    if (m_lockOpened) m_timerAt = m_lockOpenAt;
    // opens by itself the moment the dials show the code
    if (!m_lockOpened && m_lockSettle && int32_t(u->now() - m_lockSettle) >= 0 && m_dialDrag < 0) {
        m_lockSettle = 0;
        char code[5] = { char('0' + m_dial[0]), char('0' + m_dial[1]), char('0' + m_dial[2]), char('0' + m_dial[3]), 0 };
        if (a.checkPassword(code)) { m_lockOpened = true; m_lockOpenAt = u->now() + 260; m_timerAt = m_lockOpenAt; u->redraw(); }
    }
    if (m_lockOpened && int32_t(u->now() - m_lockOpenAt) >= 0) {
        m_lockOpened = false;
        m_dial[0] = m_dial[1] = m_dial[2] = m_dial[3] = 0;
        m_scr = Settings;
        m_editLoaded = false;
        m_rulesLoaded = false;
        m_msg.clear();
        u->redraw();
    }
}

// ===========================================================================
// PasscodeSheet.qml — current, new, new again

void Gui::passcodeSheet()
{
    PassSheet& p = m_pass;
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    static const char* titles[3] = { "Current passcode", "New passcode", "Turn it again" };
    static const char* notes[3] = { "Turn the dials to the passcode in force now.",
                                    "Four digits, on the dials you will turn to unlock Settings.",
                                    "The same four digits once more, so a slip cannot lock this instrument." };
    char step[32];
    std::snprintf(step, sizeof step, "Step %d of 3", p.step + 1);
    c->text(th::pad * 2, 44, titles[p.step], SB24, th::text);
    c->text(scrW - th::pad * 2 - gfx::Canvas::measure(step, M12), 40, step, M12, th::textFaint);
    if (portrait()) {   // ITDS-2.16.0: the sentences wrap, the dials lower
        int ty = 56;
        ty += c->textBox(Rect(th::pad * 2, ty, scrW - th::pad * 4, 48), notes[p.step], S16, th::textDim, gfx::Left, 2, 1.25f) + 6;
        if (!p.err.empty()) c->textBox(Rect(th::pad * 2, ty, scrW - th::pad * 4, 44), p.err, SB14, th::bad, gfx::Left, 2, 1.25f);
        dials(Rect(th::pad * 2, 160, scrW - th::pad * 4, scrH - 160 - th::touch - 60), p.d[p.step], false, ID_PS_DIAL + p.step * 5);
    } else {
        c->text(th::pad * 2, 70, notes[p.step], S16, th::textDim);
        if (!p.err.empty()) c->text(th::pad * 2, 94, p.err, SB14, th::bad);
        dials(Rect(th::pad * 2, 100, scrW - th::pad * 4, 400), p.d[p.step], false, ID_PS_DIAL + p.step * 5);
    }
    m_lockSettle = 0;
    const int by = scrH - th::touch - th::pad;
    if (u->button(ID_PS_CANCEL, Rect(scrW / 2 - 230, by, 220, th::touch), "Cancel", false, true)) { p.open = false; return; }
    if (u->button(ID_PS_NEXT, Rect(scrW / 2 + 10, by, 240, th::touch), p.step == 2 ? "Save" : "Next", true, true)) {
        auto code = [](const int* d) { std::string s; for (int i = 0; i < 4; ++i) s.push_back(char('0' + d[i])); return s; };
        if (p.step == 0) {
            if (!a.checkPassword(code(p.d[0]))) { p.err = "That is not the passcode in force."; return; }
            p.err.clear(); p.step = 1;
        } else if (p.step == 1) {
            p.err.clear(); p.step = 2;
        } else {
            const std::string why = a.changePassword(code(p.d[0]), code(p.d[1]), code(p.d[2]));
            if (!why.empty()) { p.err = why; if (why.find("not the same") != std::string::npos) p.step = 1; return; }
            p.open = false;
            say("Saved. The new passcode is in force from the next time Settings is opened.");
        }
    }
}

// ===========================================================================
// the clock. The Jetson took the date from its operating system; this board
// has a battery clock (DS1307) that nobody else sets, so Settings sets it.

void Gui::clockSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    c->text(th::pad * 2, 44, "Date and time", SB24, th::text);
    c->textBox(Rect(th::pad * 2, 58, scrW - th::pad * 4, 44),
               "Local time at this site. Case ids carry the day, and the log's Today / Yesterday "
               "windows count from it.", S14, th::textDim, gfx::Left, 2, 1.2f);
    plat::Clock& k = m_clockVal;
    struct F { const char* name; int* v; int lo, hi; };
    F fs[5] = { { "Year", &k.year, 2024, 2099 }, { "Month", &k.month, 1, 12 }, { "Day", &k.day, 1, 31 },
                { "Hour", &k.hour, 0, 23 }, { "Minute", &k.min, 0, 59 } };
    // ITDS-2.16.0: upright, the date on one row and the time under it
    const bool two = portrait();
    const int cw = 170;
    for (int i = 0; i < 5; ++i) {
        const int n = two ? (i < 3 ? 3 : 2) : 5, j = two && i >= 3 ? i - 3 : i;
        const int x = (scrW - (n * cw + (n - 1) * th::gap)) / 2 + j * (cw + th::gap);
        const int dy = two && i >= 3 ? 280 : 0;
        c->label(Rect(x, 130 + dy, cw, 24), fs[i].name, S14, th::textFaint, gfx::Center);
        const Rect up(x, 160 + dy, cw, 60), val(x, 228 + dy, cw, 90), dn(x, 326 + dy, cw, 60);
        if (u->button(ID_CK_STEP + i * 2, up, "+", false, true)) *fs[i].v = *fs[i].v >= fs[i].hi ? fs[i].lo : *fs[i].v + 1;
        char b[8]; std::snprintf(b, sizeof b, i == 0 ? "%04d" : "%02d", *fs[i].v);
        u->card(val, th::surfaceIn, th::stroke);
        c->label(val, b, MB46, th::text, gfx::Center);
        if (u->button(ID_CK_STEP + i * 2 + 1, dn, "\xE2\x88\x92", false, true)) *fs[i].v = *fs[i].v <= fs[i].lo ? fs[i].hi : *fs[i].v - 1;
    }
    char off[64];
    std::snprintf(off, sizeof off, "UTC offset used for the records: %+d min (Settings \xE2\x86\x92 Device)",
                  a.settings().utcOffsetMin);
    c->label(Rect(0, two ? 680 : 400, scrW, 24), off, S14, th::textFaint, gfx::Center);
    const int by = scrH - th::touch - th::pad;
    if (u->button(ID_CK_CANCEL, Rect(scrW / 2 - 230, by, 220, th::touch), "Cancel", false, true)) m_clockEdit = false;
    if (u->button(ID_CK_SAVE, Rect(scrW / 2 + 10, by, 240, th::touch), "Set the clock", true, true)) {
        plat::Clock w = k;
        w.sec = 0;
        std::string why;
        if (a.setClock(w, &why)) { m_clockEdit = false; say("The clock was set and read back."); }
        else say(why, true);
    }
}

} // namespace gui
