// gui/gui_measure.cpp — the measure screen (MeasurePage.qml, landscape) and
// the sheets that open over it: details and needle calibration.
#include "gui.h"
#include "../birxcore/calib.h"

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
std::string f2(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
const char* kDash = "\xE2\x80\x94";

enum Id {
    ID_LOGS = 100, ID_GEAR, ID_CASESTRIP, ID_MUTE, ID_CAL, ID_DETAILS, ID_SWEEP, ID_MID,
    ID_PRIMARY, ID_DET_CLOSE, ID_CAL_CANCEL, ID_CAL_START, ID_BANNER, ID_DET_SCROLL,
    ID_REF, ID_REF_CANCEL, ID_REF_START, ID_REF_REMOVE, ID_DIP_TEMP, ID_DIP_START, ID_DIP_AIR, ID_DIP_COND,
    ID_DET_REF = 180,
    ID_SREF_START = 300, ID_SREF_AIR, ID_SREF_CLOSE, ID_CAL_TOREF,
};

// VerdictBlock.qml
Color tierColor(birx::Tier t)
{
    switch (t) {
    case birx::Tier::High: return th::bad;
    case birx::Tier::Suspicious: return th::warn;
    case birx::Tier::Low: return th::good;
    default: return th::textDim;
    }
}
Color inkOn(Color c)
{
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float l = 0.2126f * r * r + 0.7152f * g * g + 0.0722f * b * b;
    return l > 0.30f ? gfx::hex(0x101418) : gfx::hex(0xFFFFFF);
}
} // namespace

Gui::Gui(app::App& app) : a(app) {}

// H7-2.15.0: the saline's temperature not yet entered since power-on (R at 25 degrees C is reckoned from it)
std::string Gui::tempHint(double tC)
{
    char b[200];
    std::snprintf(b, sizeof b, "Is the saline at %.1f \xC2\xB0""C? Tap Temperature and enter its own (a thermometer in the cup): "
                  "1 \xC2\xB0""C off moves its R by 2 %%.", tC);
    return b;
}

bool Gui::wants(const ui::Touch& t, uint32_t now) const
{
    if (m_again) return true;
    if (t.down != m_lastTouch.down) return true;
    if (t.down && (t.x != m_lastTouch.x || t.y != m_lastTouch.y)) return true;
    if (a.revision() != m_lastRev) return true;
    if (m_timerAt && int32_t(now - m_timerAt) >= 0) return true;
    if (uint32_t(now - m_lastDraw) >= 1000) return true;     // the clock, countdowns
    return false;
}

void Gui::frame(gfx::Canvas& canvas, const ui::Touch& t, uint32_t now)
{
    m_ui.c = canvas;
    u = &m_ui;
    c = &m_ui.c;          // one canvas (and one clip) for the toolkit and the screens
    scrW = canvas.width();
    scrH = canvas.height();

    m_lastRev = a.revision();
    m_lastDraw = now;
    m_lastTouch = t;
    m_timerAt = 0;
    u->begin(t, now);
    // what the finger was on, as it was when this frame began (diagnostic mode)
    const std::string whereBefore = where();
    const bool privateInput = m_scr == Lock || m_pass.open;
    const bool keypad = m_pad.open;
    {   // H7-2.12.2: a control tapped while it is off says why, and what to do first
        const ui::Ui::Tap& tp = u->tap();
        if (tp.happened && tp.found && !tp.live)
            a.setToast(tp.why.empty() ? std::string("Not available at this moment.") : tp.why);
    }
    // Diagnostic mode: each screen, each message shown, each tap and whether
    // what was under it was live. The passcode dials and the keypads are never
    // described: what is typed there is a passcode or patient text.
    if (a.diagnosticMode()) {
        const ui::Ui::Tap& tp = u->tap();
        if (tp.happened) {
            if (privateInput) a.uiAction("tap on the passcode dials");
            else if (keypad) a.uiAction("key on the on-screen keypad");
            else if (tp.found && !tp.words.empty())
                a.uiAction("[" + whereBefore + "] tap \"" + tp.words + "\"",
                           tp.live ? std::string() : std::string("disabled \xE2\x80\x94 nothing happens"));
            else if (tp.found)
                a.uiAction("[" + whereBefore + "] tap on control #" + std::to_string(tp.id),
                           tp.live ? std::string() : std::string("disabled \xE2\x80\x94 nothing happens"));
            else
                a.uiAction("[" + whereBefore + "] tap at " + std::to_string(tp.x) + "," +
                           std::to_string(tp.y) + " (nothing there)");
        }
    }

    const bool sheet = m_details || m_calib || m_tissue || m_pass.open || m_outcome || m_caseEdit || m_clockEdit || m_imSheet || m_monitor || m_delAsk || m_lotSheet >= 0 ||
                       m_tagSheet || m_trainSheet || m_allSheet;
    u->setInput(!sheet && !m_pad.open);
    switch (m_scr) {
    case Measure:  measure(); break;
    case Logs:     logs(); break;
    case Lock:     lock(); break;
    case Settings: settings(); break;
    case Intake:   intake(); break;
    }
    u->setInput(!m_pad.open);
    if (m_details)   detailsBox(Rect(0, 0, scrW, scrH));
    if (m_calib)     calibrateSheet();
    if (m_tissue)    tissueSheet();
    if (m_pass.open) passcodeSheet();
    if (m_clockEdit) clockSheet();
    if (m_imSheet)   intMemSheet();
    if (m_monitor)   monitorSheet();
    if (m_outcome)   outcomeSheet();
    if (m_caseEdit)  caseEditSheet();
    if (m_delAsk)    deleteLogsSheet();
    if (m_lotSheet >= 0) lotSheet();
    if (m_tagSheet)  tagSheet();
    if (m_trainSheet) trainSheet();
    if (m_allSheet)  allFilesSheet();
    if (m_calib || m_tissue || m_lotSheet >= 0) sheetRing();   // H7-2.12.3
    u->setInput(true);
    if (m_pad.open)  padSheet();
    toast();
    diagPill();
    u->end();
    m_again = u->wantsRedraw();

    // Diagnostic mode: each screen and each message shown.
    if (a.diagnosticMode()) {
        const std::string w = where();
        if (w != m_lastWhere) a.uiAction("screen", w);
        if (!m_logMsg.empty() && m_logMsg != m_lastLogMsg) a.uiAction("message shown on Logs", m_logMsg);
    }
    m_lastWhere = where();
    m_lastLogMsg = m_logMsg;
}

std::string Gui::where() const
{
    static const char* names[] = { "measure", "logs", "settings lock", "settings", "intake" };
    std::string w = names[m_scr];
    if (m_scr == Settings) {
        static const char* tabs[] = { "Device", "Protocol", "Needle", "Rules", "Sound", "Security", "About", "Lots", "Training" };
        if (m_tab >= 0 && m_tab < 9) w += " / " + std::string(tabs[m_tab]);
    }
    if (m_details)   w += " + details";
    if (m_calib)     w += " + needle calibration";
    if (m_tissue)    w += " + healthy tissue";
    if (m_pass.open) w += " + passcode";
    if (m_clockEdit) w += " + date and time";
    if (m_imSheet)   w += " + internal memory";
    if (m_monitor)   w += " + link monitor";
    if (m_outcome)   w += m_outSingle ? " + pathology for " + m_outCase : " + pathology";
    if (m_caseEdit)  w += " + case details";
    if (m_delAsk)    w += " + delete logs";
    if (m_lotSheet >= 0) w += " + lot test";
    if (m_tagSheet)  w += " + tag case " + m_tagCase;
    if (m_trainSheet) w += " + training data";
    if (m_allSheet)  w += " + all files";
    if (m_pad.open)  w += " + keypad";
    return w;
}

// DIAGNOSTIC MODE IS VISIBLE ON EVERY SCREEN (Main.qml, 2.19.0). An
// instrument that is quietly recording everything done on it must say so. It
// takes no touches: nothing underneath stops working.
void Gui::diagPill()
{
    if (!a.diagnosticMode()) return;
    const std::string t = "\xE2\x97\x8F DIAGNOSTIC MODE \xE2\x80\x94 recording";
    const int w = gfx::Canvas::measure(t, SB12) + 24;
    const Rect r((scrW - w) / 2, 4, w, 24);
    c->roundRect(r, 12, th::bad);
    c->label(r, t, SB12, gfx::hex(0xFFFFFF), gfx::Center);
}

std::string Gui::ohmShort(double ohm) const { return app::App::ohmText(ohm); }

// A one-line notice at the bottom of the screen for four seconds.
void Gui::toast()
{
    if (a.toast().empty() || a.toastAge() > 5000) return;
    const std::string m = a.toast();
    // H7-2.12.2: up to three lines (the reason a control is off, and what to do first)
    const int w = std::min(std::min(900, scrW - 2 * th::pad), gfx::Canvas::measure(m, S14) + 40);
    const int th1 = gfx::Canvas::wrapHeight(w - 40, m, S14, 3, 1.25f);
    const int h = std::max(44, th1 + 20);
    const Rect r((scrW - w) / 2, scrH - 26 - h, w, h);
    u->card(r, th::surfaceHi, th::accent);
    c->textBox(r.inset(20, 10), m, S14, th::text, w - 40 >= gfx::Canvas::measure(m, S14) ? gfx::Center : gfx::Left, 3, 1.25f);
    m_timerAt = 1;   // keep redrawing until it expires
}

// Problems that must be said on screen, never only logged (spec §12, §16.14).
void Gui::statusBanners(Rect* body)
{
    std::string msg;
    bool dismissable = false;
    if (!a.storageProblem().empty()) { msg = a.storageProblem(); dismissable = true; }
    else if (!a.clockProblem().empty()) msg = a.clockProblem();
    else if (!a.settingsProblem().empty()) msg = a.settingsProblem();
    if (msg.empty()) return;
    const int h = std::max(dismissable ? 40 : 0,
                           gfx::Canvas::wrapHeight(body->w - 40 - (dismissable ? 90 : 0), msg, S12, 2, 1.2f) + 12);
    const Rect r(body->x, body->y, body->w, h);
    u->card(r, th::badSoft, th::bad);
    c->textBox(Rect(r.x + 12, r.y + 6, r.w - 24 - (dismissable ? 90 : 0), h - 12), msg, S12, th::bad,
               gfx::Left, 2, 1.2f, true);
    if (dismissable && u->outline(ID_BANNER, Rect(r.r() - 90, r.y + (h - 30) / 2, 80, 30), "OK", th::bad))
        a.clearStorageProblem();
    body->y += h + th::gap;
    body->h -= h + th::gap;
}

// ===========================================================================

void Gui::measure()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    header(Rect(th::pad, th::pad, scrW - 2 * th::pad, 46));
    Rect body(th::pad, th::pad + 46 + th::gap, scrW - 2 * th::pad, scrH - 2 * th::pad - 46 - th::gap);
    statusBanners(&body);
    if (portrait()) { measurePortrait(body); return; }
    // H7-1.5.0: the IPS/Z map is the main picture of the screen (asked for from
    // the bench: "the map was far too small"). The left column keeps the case,
    // the verdict and the controls in 40% of the width; the right 60% is the
    // map, with the phase chart as a strip under it.
    const int leftW = int(std::lround((body.w - th::gap) * 0.40));
    const Rect left(body.x, body.y, leftW, body.h);
    const Rect right(body.x + leftW + th::gap, body.y, body.w - leftW - th::gap, body.h);

    // left column: case, needle, verdict, tiles, progress, actions (bottom up)
    const Rect cs(left.x, left.y, left.w, th::touchSm);
    const Rect ns(left.x, cs.b() + th::gap, left.w, 52);
    caseStrip(cs);
    needleStrip(ns);
    const int actH = actionRow(Rect(left.x, left.b(), left.w, 0), true);   // measure only
    const Rect act(left.x, left.b() - actH, left.w, actH);
    actionRow(act, false);
    const Rect prog(left.x, act.y - th::gap - 44, left.w, 44);
    progressStrip(prog);
    // The tiles give way before the verdict does: with a banner above and a
    // three-line sentence below, the verdict must still be a card, not a sliver.
    const int avail = prog.y - th::gap - (ns.b() + th::gap);
    const int tilesH = std::max(92, std::min(132, avail - th::gap - 72));
    const Rect tiles(left.x, prog.y - th::gap - tilesH, left.w, tilesH);
    valueTiles(tiles);
    const Rect verdict(left.x, ns.b() + th::gap, left.w, tiles.y - th::gap - ns.b() - th::gap);
    verdictCard(verdict);

    // right: the map large, the phase chart under it
    const int mapH = int(std::lround((right.h - th::gap) * 0.70));
    zipsMap(Rect(right.x, right.y, right.w, mapH));
    // H7-2.5.1: under it the material the curve is most like (Settings -> Training), else the phase chart
    const Rect ph(right.x, right.y + mapH + th::gap, right.w, right.h - mapH - th::gap);
    if (!materialBox(ph)) phaseChart(ph);
    // H7-2.12.3: the reading's sweep, as a line filling round the box its curve is drawn in
    if (a.readingSweep()) progressRing(ph, a.sweepProgress(), th::accent, th::radiusSm);
}

// ITDS-2.16.0: the same screen on an upright screen (600 x 1024): one column, full width.
// From the top: the case, the needle, the answer and its values, how many readings, the map
// and the phase chart (or the material), and the buttons at the bottom, under the thumb.
void Gui::measurePortrait(const Rect& body)
{
    const Rect cs(body.x, body.y, body.w, th::touchSm);
    const Rect ns(body.x, cs.b() + th::gap, body.w, 52);
    caseStrip(cs);
    needleStrip(ns);
    const int actH = actionRow(Rect(body.x, body.b(), body.w, 0), true);   // measure only
    const Rect act(body.x, body.b() - actH, body.w, actH);
    actionRow(act, false);
    // the answer, its two values and DETAILS, then the progress: never smaller than this
    const int verdictH = 112, tilesH = 104, progH = 44;
    const Rect verdict(body.x, ns.b() + th::gap, body.w, verdictH);
    const Rect tiles(body.x, verdict.b() + th::gap, body.w, tilesH);
    const Rect prog(body.x, tiles.b() + th::gap, body.w, progH);
    verdictCard(verdict);
    valueTiles(tiles);
    progressStrip(prog);
    // the map and, under it, the phase chart or the material: what is left
    const int top = prog.b() + th::gap, bottom = act.y - th::gap;
    const int room = std::max(0, bottom - top);
    const int mapH = int(std::lround((room - th::gap) * 0.68));
    if (mapH > 80) zipsMap(Rect(body.x, top, body.w, mapH));
    const Rect ph(body.x, top + mapH + th::gap, body.w, bottom - (top + mapH + th::gap));
    if (ph.h > 60) {
        if (!materialBox(ph)) phaseChart(ph);
        if (a.readingSweep()) progressRing(ph, a.sweepProgress(), th::accent, th::radiusSm);
    }
}

// H7-2.12.3: a line that fills round a box, clockwise from its top-left corner, as the sweep there goes on
void Gui::progressRing(const Rect& r, double f, Color col, int radius)
{
    m_timerAt = u->now() + 200;           // it moves between the points too
    if (!(f > 0)) return;
    f = std::min(1.0, f);
    const float kPi = 3.14159265f;
    const float x0 = float(r.x) + 1.5f, y0 = float(r.y) + 1.5f, x1 = float(r.r()) - 1.5f, y1 = float(r.b()) - 1.5f;
    const float rad = std::min(float(radius), std::min(x1 - x0, y1 - y0) / 2);
    std::vector<std::pair<float, float>> p;
    auto arc = [&](float cx, float cy, float a0) {
        for (int k = 1; k <= 6; ++k) { const float an = a0 + float(k) * (kPi / 2) / 6; p.push_back({ cx + rad * std::cos(an), cy + rad * std::sin(an) }); }
    };
    p.push_back({ x0 + rad, y0 });
    p.push_back({ x1 - rad, y0 }); arc(x1 - rad, y0 + rad, -kPi / 2);
    p.push_back({ x1, y1 - rad }); arc(x1 - rad, y1 - rad, 0);
    p.push_back({ x0 + rad, y1 }); arc(x0 + rad, y1 - rad, kPi / 2);
    p.push_back({ x0, y0 + rad }); arc(x0 + rad, y0 + rad, kPi);
    double len = 0;
    for (size_t i = 1; i < p.size(); ++i) len += std::hypot(p[i].first - p[i - 1].first, p[i].second - p[i - 1].second);
    // the whole way round, dim, over the box's own border: what is still to come
    for (size_t i = 1; i < p.size(); ++i) c->line(p[i - 1].first, p[i - 1].second, p[i].first, p[i].second, 3.5f, th::stroke);
    double left = f * len;
    float hx = p[0].first, hy = p[0].second;
    for (size_t i = 1; i < p.size() && left > 0; ++i) {
        const double d = std::hypot(p[i].first - p[i - 1].first, p[i].second - p[i - 1].second);
        const float t = d > left ? float(left / d) : 1.0f;
        hx = p[i - 1].first + t * (p[i].first - p[i - 1].first);
        hy = p[i - 1].second + t * (p[i].second - p[i - 1].second);
        c->line(p[i - 1].first, p[i - 1].second, hx, hy, 3.5f, col);
        left -= d;
    }
    c->circle(hx, hy, 4.5f, col);             // where it has come to
}

// H7-2.12.3: the saline check's settling (amber), then its two sweeps (blue, half each)
bool Gui::checkRing(double* f, Color* col) const
{
    const app::DipStage st = a.dipStage();
    const int settle = std::max(1, a.settings().settleS);
    if (st == app::DipStage::Settling) { *f = 1.0 - double(a.dipSecondsLeft()) / settle; *col = th::warn; return true; }
    if (st == app::DipStage::Waiting || st == app::DipStage::Sweeping || st == app::DipStage::Between) {
        *f = (a.dipSweepsDone() + (a.sweeping() ? a.sweepProgress() : 0.0)) / 2.0;
        *col = th::accent;
        return true;
    }
    return false;
}

void Gui::sheetRing()
{
    double f = 0;
    Color col = th::accent;
    const int settle = std::max(1, a.settings().settleS);
    if (m_calib && (m_calDip || m_calRef)) {
        if (!checkRing(&f, &col)) f = 0;
    } else if (m_calib) {
        const app::CalStage st = a.calStage();
        if ((st == app::CalStage::Waiting || st == app::CalStage::Sweeping) && a.sweeping()) f = a.sweepProgress();
    } else if (m_tissue) {
        const app::RefStage st = a.refStage();
        if ((st == app::RefStage::Waiting || st == app::RefStage::Sweeping) && a.sweeping()) f = a.sweepProgress();
    } else if (m_lotSheet >= 0) {
        const app::LotStage st = a.lotStage();
        if (st == app::LotStage::Settling) { f = 1.0 - double(a.lotSecondsLeft()) / settle; col = th::warn; }
        else if ((st == app::LotStage::Waiting || st == app::LotStage::Sweeping) && a.sweeping()) f = a.sweepProgress();
    }
    if (f > 0) progressRing(Rect(3, 3, scrW - 6, scrH - 6), f, col, th::radius);
}

// PanelHeader.qml
void Gui::header(const Rect& r)
{
    // the Coosa mark: two concentric arcs (the SVG is not carried)
    const float mx = r.x + 14.0f, my = r.y + r.h / 2.0f;
    c->ring(mx, my, 11, 3, th::accent);
    c->circle(mx, my, 4, th::accent);
    c->fill(Rect(r.x + 34, r.y + 8, 2, 30), th::stroke);
    c->text(r.x + 46, r.y + 21, "BIRADx", SB19, th::text);
    c->text(r.x + 46, r.y + 38, "Impedimetric Tumor Detection", S12, th::textFaint);

    // the clock, so a wrong date is visible at a glance
    const plat::Clock n = a.nowLocal();
    char clk[32];
    std::snprintf(clk, sizeof clk, "%04d-%02d-%02d  %02d:%02d", n.year, n.month, n.day, n.hour, n.min);
    const bool clockBad = !a.clockProblem().empty();
    // ITDS-2.16.0: upright, the clock just after the name, the battery under it
    const int clkX = portrait() ? r.x + 222 : r.x + 330;
    c->label(Rect(clkX, portrait() ? r.y + 2 : r.y, 200, portrait() ? 20 : r.h), clk, M12, clockBad ? th::bad : th::textFaint, gfx::Left);
    const int bat = plat::batteryPercent();
    if (bat >= 0) {
        char b[16]; std::snprintf(b, sizeof b, "%d%%", bat);
        const Color bc = bat < 15 ? th::bad : th::textFaint;
        const Rect br(portrait() ? clkX : r.x + 470, portrait() ? r.y + 26 : r.y + 16, 26, 14);
        c->roundFrame(br, 2, 1.5f, bc);
        c->fill(Rect(br.r(), br.y + 4, 2, 6), bc);
        c->fill(Rect(br.x + 3, br.y + 3, std::max(1, (br.w - 6) * std::min(bat, 100) / 100), br.h - 6), bc);
        c->label(portrait() ? Rect(br.r() + 6, br.y, 60, br.h) : Rect(br.r() + 6, r.y, 60, r.h), std::string(b) + (plat::onMains() ? " \xE2\x86\x91" : ""),
                 M11, bc, gfx::Left);
    }

    // LOGS n, gear
    const std::string n1 = std::to_string(a.logCount());
    const int lw = gfx::Canvas::measure("LOGS", SB14) + 8 + gfx::Canvas::measure(n1, M14) + 30;
    const Rect gear(r.r() - 46, r.y + 1, 46, 44);
    const Rect logs(gear.x - th::gap - lw, r.y + 1, lw, 44);
    const bool lp = u->pressed(ID_LOGS);
    if (u->clicked(ID_LOGS, logs)) { m_scr = Logs; m_logsShown = false; }
    u->card(logs, u->pressed(ID_LOGS) || lp ? th::accentSoft : th::bg, th::accent);
    int x = logs.x + 15;
    x += c->text(x, logs.y + 27, "LOGS", SB14, th::accent) + 8;
    c->text(x, logs.y + 27, n1, M14, th::textFaint);
    if (u->clicked(ID_GEAR, gear)) {
        m_scr = Lock;
        m_dial[0] = m_dial[1] = m_dial[2] = m_dial[3] = 0;
        m_lockOpened = false;
        m_lockSettle = 0;
    }
    u->card(gear, u->pressed(ID_GEAR) ? th::accentSoft : th::bg, th::stroke);
    // a gear drawn: ring + 8 teeth
    const float gx = gear.x + 23.0f, gy = gear.y + 22.0f;
    for (int k = 0; k < 8; ++k) {
        const float an = k * float(M_PI) / 4;
        c->line(gx + 6 * std::cos(an), gy + 6 * std::sin(an), gx + 10 * std::cos(an),
                gy + 10 * std::sin(an), 3.2f, th::textDim);
    }
    c->ring(gx, gy, 7.5f, 3, th::textDim);
}

// CaseStrip.qml
void Gui::caseStrip(const Rect& r)
{
    const Rect box(r.x, r.y, r.w - 46 - th::gap, r.h);
    const Rect snd(box.r() + th::gap, r.y, 46, r.h);
    if (u->clicked(ID_CASESTRIP, box)) openIntakeForNewCase();
    u->card(box, u->pressed(ID_CASESTRIP) ? th::accentSoft : th::surfaceHi, th::stroke);
    int x = box.x + 12;
    x += c->text(x, box.y + 28, "CASE", M11, th::textFaint) + 10;
    if (a.caseOpen()) {
        x += c->text(x, box.y + 30, a.caseId(), MB19, th::text);
        const std::string sum = a.caseSummary();
        const int sw = std::min(box.r() - 12 - (x + 12), gfx::Canvas::measure(sum, S12));
        if (sw > 40) c->label(Rect(box.r() - 12 - sw, box.y, sw, box.h), sum, S12, th::textFaint, gfx::Right);
    } else {
        c->text(x, box.y + 29, "no case \xE2\x80\x94 tap to start", SB14, th::accent);
    }
    // the speaker
    const bool on = !a.muted();
    if (u->clicked(ID_MUTE, snd)) a.setMuted(on);
    u->card(snd, u->pressed(ID_MUTE) ? th::accentSoft : th::bg, on ? th::stroke : th::bad);
    const Color ic = on ? th::textDim : th::textFaint;
    const int cx = snd.x + 11, cy = snd.y + snd.h / 2;
    c->fill(Rect(cx, cy - 4, 6, 8), ic);
    c->triangle(cx + 5.0f, cy - 4.0f, cx + 13.0f, cy - 9.0f, cx + 13.0f, cy + 9.0f, ic);
    c->triangle(cx + 5.0f, cy - 4.0f, cx + 13.0f, cy + 9.0f, cx + 5.0f, cy + 4.0f, ic);
    if (on) {
        c->line(cx + 17.0f, cy - 4.0f, cx + 19.0f, cy + 0.0f, 1.6f, ic);
        c->line(cx + 19.0f, cy + 0.0f, cx + 17.0f, cy + 4.0f, 1.6f, ic);
        c->line(cx + 21.0f, cy - 7.0f, cx + 24.0f, cy + 0.0f, 1.6f, ic);
        c->line(cx + 24.0f, cy + 0.0f, cx + 21.0f, cy + 7.0f, 1.6f, ic);
    } else {
        c->line(snd.x + 10.0f, snd.b() - 12.0f, snd.r() - 10.0f, snd.y + 12.0f, 2.5f, th::bad);
    }
}

// NeedleStrip.qml
void Gui::needleStrip(const Rect& r)
{
    // H7-2.1.0: with rules found without air calibration the needle is ready
    // as it is. H7-2.3.0: a lot used needle by needle needs its Saline check too.
    const bool air = a.airCalUsed();
    const bool dip = a.dipNeeded();
    const bool ok = dip ? a.needleReady() && a.dipDone() : !air || a.needleReady();
    u->card(r, ok ? th::goodSoft : th::badSoft, ok ? gfx::hex(0x1D4A30) : th::bad);
    const std::string btn = dip ? (!a.needleReady() ? "Air + saline check" : a.dipDone() ? "Checks \xE2\x9C\x93" : "Saline check")
                          : !a.needleReady() ? (air ? "Calibrate \xE2\x80\x94 6 s" : "Air check")
                          : !a.matHasRef() ? "Saline reference" : "Air + saline \xE2\x9C\x93";
    const int bw = gfx::Canvas::measure(btn, SB14) + 30;
    const Rect b(r.r() - 8 - bw, r.y + (r.h - 38) / 2, bw, 38);
    const Color tone = ok ? th::good : th::bad;
    c->text(r.x + 12, r.y + 31, ok ? "\xE2\x9C\x93" : "\xE2\x9C\x95", SB16, tone);
    // The healthy-tissue sample of this patient (H7-1.3.0): beside the needle,
    // because it is taken with the needle on its way in, before any reading.
    // Shown once the needle is ready; a tick when this case has its sample.
    int leftOfButtons = b.x;
    if (ok) {
        const bool have = a.haveTissueRef();
        std::string hl = have ? "Healthy tissue \xE2\x9C\x93" : "Healthy tissue";
        if (a.tissueRefCount() > 1) hl += std::to_string(a.tissueRefCount());
        const int hw = gfx::Canvas::measure(hl, SB14) + 26;
        const Rect hb(b.x - 6 - hw, b.y, hw, b.h);
        u->name("Healthy tissue");
        if (u->clicked(ID_REF, hb)) { a.refOpen(); m_tissue = true; }
        const bool need = a.caseOpen() && !have;
        u->card(hb, u->pressed(ID_REF) ? th::accentSoft : have ? th::healthySoft : th::bg, have ? th::healthy : need ? th::accent : th::stroke);
        c->label(hb, hl, SB14, have ? th::healthy : need ? th::accent : th::textDim, gfx::Center);
        leftOfButtons = hb.x;
    }
    const Rect t(r.x + 36, r.y + 6, leftOfButtons - th::gap - (r.x + 36), r.h - 12);
    c->label(Rect(t.x, t.y, t.w, 20), ok ? "Ready" : dip && a.needleReady() ? "This needle's saline check is not done"
                                                  : "This needle is not calibrated", SB14, tone);
    // H7-2.0.0: the lot, when a lot calibration file is installed
    const std::string ls = a.lotStatus();
    const bool lotBad = ls.find("not in") != std::string::npos;
    std::string line2 = !air ? std::string("as in the trial: no air calibration")
                      : ok ? a.needleCalibration() : std::string("hold the needle in air, clear of everything");
    if (dip) line2 = a.dipDone() ? a.dipText()
                   : a.needleReady() ? "the tip 10 s in the " + a.dipSolutionName() + " saline"
                   : "Air check, then the tip 10 s in the " + a.dipSolutionName() + " saline";
    if (!ls.empty()) line2 = ls + " \xC2\xB7 " + line2;
    c->label(Rect(t.x, t.y + 20, t.w, 18), line2, M12, lotBad ? th::warn : tone, gfx::Left, 220);
    if (u->clicked(ID_CAL, b)) {
        m_calib = true;
        m_calDip = dip && a.needleReady();       // the air part is done: straight to the saline check
        m_calRef = false;                        // (Training's saline check is opened from Training)
        m_calRefStarted = false;
        if (m_calDip) { a.calOpen(); a.dipOpen(); }
        else a.calOpen();
    }
    if (ok) u->card(b, u->pressed(ID_CAL) ? th::goodSoft : th::goodSoft, gfx::hex(0x2A6B45));
    else c->roundRect(b, th::radiusSm, u->pressed(ID_CAL) ? gfx::darker(th::bad, 1.2f) : th::bad);
    c->label(b, btn, SB14, ok ? th::good : gfx::hex(0x120608), gfx::Center);
}

// VerdictBlock.qml — the CASE answer, the card filled with the rule's colour.
void Gui::verdictCard(const Rect& r)
{
    const birx::CaseResult& cr = a.caseResult();
    const bool idle = cr.nReadings == 0;
    const bool provisional = !cr.complete && cr.nValid > 0;
    Color shown = tierColor(cr.tier);
    gfx::Color parsed;
    if (gfx::parseColor(cr.color, &parsed)) shown = parsed;
    // The LAST reading, when it was not counted ("Noisy"), is what the card
    // says, in its row's colour — whether or not the lesion already has
    // counted readings (H7-1.8.0, from the bench: a 10 MOhm reading showed the
    // lesion's "Low Risk" on the card and looked like a Low Risk reading). The
    // lesion's answer so far is the line under it, and is the card again as
    // soon as a reading counts.
    const bool lastWord = !idle && a.haveLast() && !a.last().answered() && !a.last().label.empty();
    if (lastWord) shown = gfx::parseColor(a.last().color, &parsed) ? parsed : th::textFaint;
    const bool showProv = provisional && !lastWord;
    const Color fill = idle ? th::surfaceHi : shown;
    const Color border = idle ? th::stroke : showProv ? gfx::lighter(shown, 1.5f) : gfx::darker(shown, 1.4f);
    u->card(r, fill, border, th::radius);
    const Color ink = idle ? th::text : inkOn(shown);
    const std::string label = idle ? std::string("Ready") : lastWord ? a.last().label : a.caseLabel();
    const Font& f = (label.size() > 18 || r.h < 60) ? SB24 : (label.size() > 12 || r.h < 76) ? SB29 : SB36;
    const int labelH = gfx::Canvas::wrapHeight(r.w - 32, label, f, 2, 1.15f);
    const int provH = (showProv || lastWord) ? 16 : 0;
    const int subH = lastWord ? 20 : 0;
    int y = r.y + (r.h - labelH - provH - subH) / 2;
    if (showProv) {
        c->text(r.x + 16, y + 11, "PROVISIONAL", M11, ink, 178);
        y += provH;
    } else if (lastWord) {
        c->text(r.x + 16, y + 11, "THIS READING \xE2\x80\x94 NOT COUNTED", M11, ink, 178);
        y += provH;
    }
    c->textBox(Rect(r.x + 16, y, r.w - 32, labelH), label, f, ink, gfx::Left, 2, 1.15f);
    if (lastWord) {
        const std::string sub = cr.nValid > 0
            ? "Lesion so far: " + std::string(cr.complete ? "" : "provisional ") + a.caseLabel() + " (" +
              std::to_string(cr.nValid) + " counted)"
            : std::string("Lesion: no counted reading yet");
        c->label(Rect(r.x + 16, y + labelH + 4, r.w - 32, 16), sub, SB14, ink, gfx::Left, 220);
    }
}

// ValueTiles.qml
void Gui::valueTiles(const Rect& r)
{
    const int dw = 92;
    const int tw = (r.w - dw - th::gap * 2) / 2;
    const Rect t1(r.x, r.y, tw, r.h), t2(t1.r() + th::gap, r.y, tw, r.h), t3(t2.r() + th::gap, r.y, dw, r.h);
    u->card(t1, th::surface, th::stroke);
    const int ly = r.h * 42 / 132, vy = r.h >= 120 ? r.h * 92 / 132 : r.h - 16;   // label and value baselines
    c->text(t1.x + 14, t1.y + ly, "IPS", M12, th::textFaint);
    // dimmer when the reading was not answered (no rule matched it, or the sweep was refused)
    // H7-1.5.0: narrower tiles; a value that does not fit the big digits is
    // set one size down rather than run into the next tile.
    auto fit = [&](const std::string& t, int room) -> const Font& {
        if (gfx::Canvas::measure(t, MB44) <= room) return MB44;
        if (gfx::Canvas::measure(t, SB36) <= room) return SB36;
        return SB29;
    };
    c->text(t1.x + 14, t1.y + vy, a.ipsText(), fit(a.ipsText(), tw - 22),
            a.haveLast() && !a.last().answered() ? th::textDim : th::text);

    u->card(t2, th::surface, th::stroke);
    const double z = a.z1kOhm();
    const bool known = std::isfinite(z);
    const bool inFat = false;         // no placement gate: no impedance is marked by itself
    c->text(t2.x + 14, t2.y + ly, "Z @ 1 kHz", M12, th::textFaint);
    // the number big, its unit ("k", "M") with the ohm sign small
    std::string zn = a.z1kText(), zu;
    const size_t sp = zn.find(' ');
    if (sp != std::string::npos) { zu = zn.substr(sp + 1); zn = zn.substr(0, sp); }
    if (known) zu += "\xCE\xA9";
    const int uw = zu.empty() ? 0 : gfx::Canvas::measure(zu, MB24) + 4;
    const int w = c->text(t2.x + 14, t2.y + vy, zn, fit(zn, tw - 22 - uw), inFat ? th::warn : th::text);
    if (!zu.empty()) c->text(t2.x + 14 + w + 4, t2.y + vy, zu, MB24, inFat ? th::warn : th::textDim);

    if (u->clicked(ID_DETAILS, t3)) { m_details = true; m_detLog = false; m_detRef = -1; u->resetScroll(ID_DET_SCROLL); }
    u->card(t3, u->pressed(ID_DETAILS) ? th::accentSoft : th::bg, th::accent);
    const int d0 = (r.h - 132) / 2;
    c->label(Rect(t3.x, t3.y + 30 + d0, t3.w, 24), "\xE2\x8B\xAF", SB24, th::accent, gfx::Center);
    c->label(Rect(t3.x, t3.y + 60 + d0, t3.w, 16), "DETAILS", M11, th::accent, gfx::Center);
    c->label(Rect(t3.x, t3.y + 80 + d0, t3.w, 14), "22 values", S10, th::textFaint, gfx::Center);
}

// ProgressStrip.qml
void Gui::progressStrip(const Rect& r)
{
    const birx::CaseResult& cr = a.caseResult();
    const int target = std::max(1, std::min(a.settings().readings, 20));
    u->card(r, th::surfaceHi, th::stroke);
    const bool early = cr.stoppedEarly;
    const int segW = target <= 5 ? (r.w < 450 ? 16 : 28) : std::max(8, std::min(28, (r.w / 2 - 24) / target - 6));
    int x = r.x + 12;
    for (int i = 0; i < target; ++i) {
        const bool done = i < cr.nValid;
        const Rect s(x, r.y + (r.h - 10) / 2, segW, 10);
        if (done) c->roundRect(s, 5, early ? th::bad : th::accent);
        else {
            c->roundRect(s, 5, th::stroke, early ? 102 : 255);
            c->roundRect(s.inset(1), 4, th::surfaceIn, early ? 102 : 255);
        }
        x += segW + 6;
    }
    char b[160];
    if (cr.stoppedEarly)
        std::snprintf(b, sizeof b, "%d of %d \xE2\x80\x94 positive, done", cr.nValid, target);
    else if (cr.complete)
        std::snprintf(b, sizeof b, "%d of %d \xE2\x80\x94 complete", cr.nValid, target);
    else
        std::snprintf(b, sizeof b, "%d of %d \xE2\x80\x94 %d more needed", cr.nValid, target,
                      std::max(cr.stillNeeded, target - cr.nValid));
    c->label(Rect(x + 6, r.y, r.r() - 12 - (x + 6), r.h), b, S13, th::textDim);
}

// ActionRow.qml. With measureOnly the height is returned and nothing drawn.
int Gui::actionRow(const Rect& r, bool measureOnly)
{
    const std::string reason = a.actionReason();
    const int textH = reason.empty() ? 0 : gfx::Canvas::wrapHeight(r.w, reason, S12, 5, 1.15f);
    const int h = th::touch + (textH ? textH + 6 : 0);
    if (measureOnly) return h;
    if (textH) c->textBox(Rect(r.x, r.y, r.w, textH), reason, S12, th::warn, gfx::Left, 5, 1.15f);
    const int by = r.b() - th::touch;
    const int w = r.w - th::gap * 2;
    const Rect b1(r.x, by, int(w * 0.24), th::touch);
    const Rect b2(b1.r() + th::gap, by, int(w * 0.30), th::touch);
    const Rect b3(b2.r() + th::gap, by, r.r() - (b2.r() + th::gap), th::touch);
    const bool running = a.running() || a.sweeping() || a.asking();
    const bool busy = a.busy();
    const bool caseOpen = a.caseOpen();

    // Sweep: deliberately not gated (bench control); only a link is needed — and, H7-2.10.0, a needle
    // that has passed its QC when one is asked for (else its sweep is no reading at all)
    u->why(ID_SWEEP, !a.connected() ? std::string("Nothing is arriving from the measurement unit: check its cable and its power.") : busy ? std::string("Wait: a sweep or command is in progress.") : "Check the needle first (the needle line above): " + a.qcBlocker() + ".");
    if (u->button(ID_SWEEP, b1, "Sweep", false, a.connected() && !busy && a.qcBlocker().empty())) a.startRun();

    const std::string mid = running ? "Stop" : caseOpen ? "End case" : "Measure, no case";
    u->why(ID_MID, busy ? std::string("Wait: a sweep or command is in progress.") : a.measureBlocker());
    if (u->button(ID_MID, b2, mid, false, running || (!busy && (caseOpen || a.canMeasure())))) {
        if (running) a.stopRun();
        else if (caseOpen) a.endCase();
        else a.startRun();
    }

    char primary[64];
    const bool closed = caseOpen && a.caseResult().closed;      // H7-2.14.0: final, no more readings
    if (!caseOpen) std::snprintf(primary, sizeof primary, "Start case");
    else if (closed) std::snprintf(primary, sizeof primary, "End case");
    else if (a.caseResult().complete) std::snprintf(primary, sizeof primary, "New reading anyway");
    else std::snprintf(primary, sizeof primary, "Reading %d of %d", a.caseResult().nValid + 1,
                       std::max(1, std::min(a.settings().readings, 20)));
    const bool en = closed || (caseOpen ? a.canMeasure() : a.canStartCase());
    u->why(ID_PRIMARY, caseOpen ? a.measureBlocker() : a.caseBlocker());
    if (u->button(ID_PRIMARY, b3, primary, true, en)) {
        if (closed) a.endCase();
        else if (caseOpen) a.startRun();
        else openIntakeForNewCase();
    }
    return h;
}

// Main.qml onOpenIntake, from "Start case" and from a tap on the case strip
// alike: a NEW case id is minted every time, also over a case that is open,
// and the intake form opens for it. (H7-1.0.0 to 1.2.4 opened the open case's
// details from the strip instead; the Jetson never did, and the difference was
// reported from the bench as "it does not make a new case".)
void Gui::openIntakeForNewCase()
{
    std::string why;
    if (a.beginPatient(&why).empty()) { a.setToast(why.empty() ? a.caseBlocker() : why); return; }
    m_intakePage = 1;
    m_intakeMsg.clear();
    u->resetScroll(900);
    m_scr = Intake;
}

// ---------------------------------------------------------------------------
// PhaseChart.qml

void Gui::phaseChart(const Rect& r0)
{
    u->card(r0, th::surface, th::stroke);
    const Rect r = r0.inset(6);
    const int pl = 40, pt = 14, pb = 20, pr = 8;
    const birx::Spectrum& s = a.current();
    std::vector<std::pair<double, double>> pts;       // (log10 f, phase)
    for (const birx::Point& p : s.points) if (p.freqHz > 0) pts.push_back({ std::log10(p.freqHz), p.phaseDeg });
    if (pts.size() < 2) {
        c->label(r, "no sweep yet", M11, th::textFaint, gfx::Center);
        return;
    }
    // H7-2.10.0: a needle that has not passed its QC gives no output, not even a curve
    if (!a.qcBlocker().empty()) {
        c->label(r, "no curve: the needle's checks first", M11, th::textFaint, gfx::Center);
        return;
    }
    std::sort(pts.begin(), pts.end());
    // The patient's healthy-tissue samples, behind the curve of the reading, in
    // their own colour: the two shapes are compared at a glance.
    const bool mainIsSample = a.chartShowsTissueRef() && !a.sweeping();
    std::vector<std::vector<std::pair<double, double>>> hcurves;
    for (const app::TissueSample& hs : a.tissueRefs()) {
        std::vector<std::pair<double, double>> hp;
        for (const birx::Point& p : hs.points) if (p.freqHz > 0) hp.push_back({ std::log10(p.freqHz), p.phaseDeg });
        std::sort(hp.begin(), hp.end());
        if (hp.size() >= 2) hcurves.push_back(hp);
    }
    if (mainIsSample && !hcurves.empty()) hcurves.pop_back();     // the last sample IS the main curve
    const double x0 = 2.66, x1 = 5.74;
    // y: auto with at least a 10 degree span, nice step
    double lo = pts[0].second, hi = pts[0].second;
    for (auto& p : pts) { lo = std::min(lo, p.second); hi = std::max(hi, p.second); }
    for (auto& hc : hcurves) for (auto& p : hc) { lo = std::min(lo, p.second); hi = std::max(hi, p.second); }
    if (!(hi > lo)) { lo -= 0.5; hi += 0.5; }
    double span = hi - lo;
    if (10 > span) { const double cc = (lo + hi) / 2; lo = cc - 5; hi = cc + 5; span = 10; }
    auto niceStep = [](double raw) {
        if (!(raw > 0)) return 1.0;
        const double e = std::pow(10, std::floor(std::log10(raw)));
        const double m = raw / e;
        return (m < 1.5 ? 1 : m < 3 ? 2 : m < 7 ? 5 : 10) * e;
    };
    const double step = niceStep(span / 3.2);
    lo = std::floor(lo / step) * step;
    hi = std::ceil(hi / step) * step;
    const double y0 = lo, y1 = hi;
    const int w = r.w, h = r.h;
    auto X = [&](double v) { return float(r.x + pl + (v - x0) / (x1 - x0) * (w - pl - pr)); };
    auto Y = [&](double v) { return float(r.y + pt + (1 - (v - y0) / (y1 - y0)) * (h - pt - pb)); };

    c->setClip(r0);
    for (double v = lo; v <= hi + step * 1e-6; v += step) {
        const float yy = Y(v);
        c->fill(Rect(r.x + pl, int(yy), w - pl - pr, 1), th::surfaceIn);
        char b[16]; std::snprintf(b, sizeof b, "%g", std::fabs(v) < step * 1e-6 ? 0.0 : v);
        const int tw = gfx::Canvas::measure(b, M9);
        c->text(r.x + pl - 4 - tw, int(yy) + 3, b, M9, th::textFaint);
    }
    const double xt[5] = { 2.69897, 3, 4, 5, 5.69897 };
    const char* xl[5] = { "500", "", "10k", "100k", "500k" };
    for (int t = 0; t < 5; ++t) {
        const float xx = X(xt[t]);
        c->fill(Rect(int(xx), r.y + pt, 1, h - pt - pb), th::surfaceIn);
        const int tw = gfx::Canvas::measure(xl[t], M9);
        const int tx = t == 0 ? int(xx) : (t == 4 ? int(xx) - tw : int(xx) - tw / 2);
        c->text(tx, r.y + h - pb + 11, xl[t], M9, th::textFaint);
    }
    c->line(float(r.x + pl), float(r.y + pt), float(r.x + pl), float(r.y + h - pb), 1.5f, th::stroke);
    c->line(float(r.x + pl), float(r.y + h - pb), float(r.x + w - pr), float(r.y + h - pb), 1.5f, th::stroke);

    // a curve: centripetal Catmull-Rom through the points, as the QML spline
    auto curve = [&](const std::vector<std::pair<double, double>>& cp, Color col, float width, int alpha, float dot,
                     std::vector<float>* outX, std::vector<float>* outY) {
        const size_t n = cp.size();
        std::vector<float> px(n), py(n);
        for (size_t i = 0; i < n; ++i) { px[i] = X(cp[i].first); py[i] = Y(cp[i].second); }
        auto knot = [&](float t, size_t i, size_t j) {
            const float dx = px[j] - px[i], dy = py[j] - py[i];
            const float d = std::pow(dx * dx + dy * dy, 0.25f);
            return t + (d > 1e-6f ? d : 1e-6f);
        };
        float lx = px[0], ly = py[0];
        for (size_t i = 0; i + 1 < n; ++i) {
            const size_t i0 = i > 0 ? i - 1 : 0, i1 = i, i2 = i + 1, i3 = i + 2 < n ? i + 2 : n - 1;
            const float t0 = 0, t1 = knot(t0, i0, i1), t2 = knot(t1, i1, i2), t3 = knot(t2, i2, i3);
            const float d12 = t2 - t1;
            const float m1x = (px[i2] - px[i0]) / (t2 - t0) * d12, m1y = (py[i2] - py[i0]) / (t2 - t0) * d12;
            const float m2x = (px[i3] - px[i1]) / (t3 - t1) * d12, m2y = (py[i3] - py[i1]) / (t3 - t1) * d12;
            const float c1x = px[i1] + m1x / 3, c1y = py[i1] + m1y / 3;
            const float c2x = px[i2] - m2x / 3, c2y = py[i2] - m2y / 3;
            for (int k = 1; k <= 8; ++k) {
                const float t = k / 8.0f, mt = 1 - t;
                const float bx = mt * mt * mt * px[i1] + 3 * mt * mt * t * c1x + 3 * mt * t * t * c2x + t * t * t * px[i2];
                const float by = mt * mt * mt * py[i1] + 3 * mt * mt * t * c1y + 3 * mt * t * t * c2y + t * t * t * py[i2];
                c->line(lx, ly, bx, by, width, col, alpha);
                lx = bx; ly = by;
            }
        }
        if (dot > 0) for (size_t i = 0; i < n; ++i) c->circle(px[i], py[i], dot, col, alpha);
        if (outX) *outX = px;
        if (outY) *outY = py;
    };
    for (size_t k = 0; k < hcurves.size(); ++k)
        curve(hcurves[k], th::healthy, 2.0f, k + 1 == hcurves.size() ? 235 : 150, 0, nullptr, nullptr);
    const size_t n = pts.size();
    std::vector<float> px, py;
    curve(pts, mainIsSample ? th::healthy : th::accent, 2.4f, 255, 2.0f, &px, &py);
    if (!hcurves.empty()) {
        // which line is which: on the title line above the plot (H7-1.5.0)
        const std::string hl = hcurves.size() > 1 ? "healthy tissue (" + std::to_string(hcurves.size()) + ")" : std::string("healthy tissue");
        const int kw = 22 + gfx::Canvas::measure(hl, S10) + 16 + 22 + gfx::Canvas::measure("this reading", S10);
        const int kx = r.x + pl + (w - pl - pr - kw) / 2, ky = r.y + pt - 7;
        c->line(float(kx), float(ky), float(kx + 18), float(ky), 2.0f, th::healthy);
        const int k2 = kx + 22 + c->text(kx + 22, ky + 4, hl, S10, th::healthy) + 16;
        c->line(float(k2), float(ky), float(k2 + 18), float(ky), 2.4f, th::accent);
        c->text(k2 + 22, ky + 4, "this reading", S10, th::accent);
    }

    // the IPS chord, from the 100 kHz point to the 500 kHz point
    auto nearest = [&](double target) {
        int best = -1; double bd = 1e9;
        for (size_t q = 0; q < n; ++q) { const double d = std::fabs(pts[q].first - target); if (d < bd) { bd = d; best = int(q); } }
        return bd < 0.05 ? best : -1;
    };
    const int ia = nearest(5.0), ib = nearest(5.69897);
    if (ia >= 0 && ib >= 0 && ia != ib) {
        const float ax = px[size_t(ia)], ay = py[size_t(ia)], bx = px[size_t(ib)], by = py[size_t(ib)];
        // One number, one definition (§16.8): after the sweep the label is the
        // reading's own ipsLegacy, which is the tile; during it, the same
        // formula on the points as they arrive.
        double ips = birx::InstrumentConstants::kIpsPerDecade * (pts[size_t(ib)].second - pts[size_t(ia)].second);
        if (!a.sweeping() && a.haveLast() && std::isfinite(a.last().features.ipsLegacy))
            ips = a.last().features.ipsLegacy;
        c->triangle(ax, ay, bx, ay, bx, by, th::warn, 31);
        c->dashed(ax, ay, bx, ay, 3, 2, 1, th::warn, 140);
        c->dashed(bx, ay, bx, by, 3, 2, 1, th::warn, 140);
        c->line(ax, ay, bx, by, 3, th::warn);
        c->circle(ax, ay, 3.2f, th::warn);
        c->circle(bx, by, 3.2f, th::warn);
        char lab[48];
        std::snprintf(lab, sizeof lab, "IPS %s%.2f\xC2\xB0/dec", ips > 0 ? "+" : "", ips);
        const int tw = gfx::Canvas::measure(lab, SB11);
        const float ly2 = (by < ay) ? std::max(float(r.y + pt + 10), by - 6) : std::min(float(r.y + h - pb - 4), by + 14);
        const float lx2 = std::min(float(r.x + w - pr - 2), bx);
        c->text(int(lx2) - tw, int(ly2), lab, SB11, th::warn);
    }
    c->text(r.x + pl + 3, r.y + pt - 3, "phase \xC2\xB0 \xE2\x86\x91", M9, th::textFaint);
    // The curve on the chart is the patient's healthy-tissue sample, not a reading of the lesion.
    if (a.chartShowsTissueRef() && !a.sweeping())
        c->label(Rect(r.x + pl, r.y + pt + 4, r.w - pl - 8, 16), "HEALTHY TISSUE", SB11, th::healthy, gfx::Center);
    const char* cap = "f Hz \xE2\x86\x92";
    c->text(r.x + w - pr - gfx::Canvas::measure(cap, M9), r.y + pt - 3, cap, M9, th::textFaint);
    c->resetClip();
}

// ZIpsMap.qml
void Gui::zipsMap(const Rect& r0)
{
    u->card(r0, th::surface, th::stroke);
    const Rect r = r0.inset(6);
    const int pl = 30, pt = 14, pb = 20, pr = 8;
    // H7-1.7.0, asked for from the bench: a fixed window, Z 0-5 kOhm (above
    // 5 kOhm is Noisy and needs no room) and IPS -20..+20. A point outside it
    // is a small arrow on the edge it lies beyond.
    const double x0 = -20, x1 = 20, y0 = 0, y1 = 5;
    const int zStep = 1;
    const int w = r.w, h = r.h;
    auto X = [&](double v) { return float(r.x + pl + (v - x0) / (x1 - x0) * (w - pl - pr)); };
    auto Y = [&](double v) { return float(r.y + pt + (1 - (v - y0) / (y1 - y0)) * (h - pt - pb)); };
    auto cx_ = [&](double v) { return std::max(x0, std::min(x1, v)); };
    auto cy_ = [&](double v) { return std::max(y0, std::min(y1, v)); };
    c->setClip(r0);
    const app::RulesConfig& rc = a.settings().rules;
    // The plane itself is empty: where no rule is drawn, no rule decides.
    const Rect plot(int(X(x0)), int(Y(y1)), int(X(x1) - X(x0)), int(Y(y0) - Y(y1)));
    c->fill(plot, th::surfaceIn, 120);
    for (int i = int(rc.rows.size()) - 1; i >= 0; --i) {       // reverse: row 1 ends on top
        const app::RuleRowCfg& row = rc.rows[size_t(i)];
        if (!row.enabled) continue;
        const double ia = cx_(row.ipsMin), ib = cx_(row.ipsMax);
        const double zl = cy_(row.zMin / 1000), zh = cy_(row.zMax / 1000);
        if (!(ib > ia) || !(zh > zl)) continue;
        const Rect rr(int(std::lround(X(ia))), int(std::lround(Y(zh))),
                      int(std::lround(X(ib) - X(ia))), int(std::lround(Y(zl) - Y(zh))));
        Color col(128, 128, 128);
        gfx::parseColor(row.color, &col);
        c->fill(rr, th::surface);
        c->fill(rr, col, 87);
        c->frame(rr, col, 1, 217);
    }
    const int xt[9] = { -20, -15, -10, -5, 0, 5, 10, 15, 20 };
    for (int t = 0; t < 9; ++t) {
        const float xx = X(xt[t]);
        c->fill(Rect(int(xx), r.y + pt, 1, h - pt - pb), gfx::Color(255, 255, 255), 15);
        char b[8]; std::snprintf(b, sizeof b, xt[t] > 0 ? "+%d" : "%d", xt[t]);
        const int tw = gfx::Canvas::measure(b, M9);
        c->text(t == 0 ? int(xx) : t == 8 ? int(xx) - tw : int(xx) - tw / 2, r.y + h - pb + 11, b, M9, th::textFaint);
    }
    for (int v = 0; v <= int(y1); v += zStep) {
        const float yy = Y(v);
        c->fill(Rect(r.x + pl, int(yy), w - pl - pr, 1), gfx::Color(255, 255, 255), 15);
        const std::string b = std::to_string(v);
        c->text(r.x + pl - 4 - gfx::Canvas::measure(b, M9), int(yy) + 3, b, M9, th::textFaint);
    }
    c->line(float(r.x + pl), float(r.y + pt), float(r.x + pl), float(r.y + h - pb), 1.5f, th::stroke);
    c->line(float(r.x + pl), float(r.y + h - pb), float(r.x + w - pr), float(r.y + h - pb), 1.5f, th::stroke);
    // the readings of the open case
    const auto& rs = a.caseReadings();
    int last = -1;
    for (size_t i = 0; i < rs.size(); ++i)
        if (std::isfinite(rs[i].features.z1k) && std::isfinite(rs[i].features.ipsLegacy)) last = int(i);
    std::vector<Rect> placed;
    for (size_t i = 0; i < rs.size(); ++i) {
        const birx::Verdict& v = rs[i];
        const double iv = v.features.ipsLegacy, zk = v.features.z1k / 1000;
        if (!std::isfinite(iv) || !std::isfinite(zk)) continue;
        const bool off = iv < x0 || iv > x1 || zk < y0 || zk > y1;
        const float px = X(cx_(iv)), py = Y(cy_(zk));
        const bool isLast = int(i) == last;
        Color ink = th::textFaint;
        if (v.answered()) { ink = th::accent; gfx::parseColor(v.color, &ink); ink = gfx::lighter(ink, 1.45f); }
        const float rad = isLast ? 7.5f : 5.5f;    // H7-1.5.0: the map is large now
        if (isLast && !off) {
            c->dashed(px, py, px, float(r.y + h - pb), 2, 3, 1, th::text, 140);
            c->dashed(px, py, float(r.x + pl), py, 2, 3, 1, th::text, 140);
            char b[16]; std::snprintf(b, sizeof b, "%s%.1f", iv > 0 ? "+" : "", iv);
            c->text(int(px) - gfx::Canvas::measure(b, MB9) / 2, r.y + h - pb - 3, b, MB9, th::text);
            std::snprintf(b, sizeof b, "%.2fk", zk);
            c->text(r.x + pl + 3, int(py) - 3, b, MB9, th::text);
        }
        if (off) {
            const float dx = iv < x0 ? -1.0f : iv > x1 ? 1.0f : 0.0f;
            const float dy = zk > y1 ? -1.0f : zk < y0 ? 1.0f : 0.0f;
            // inside the plot, pointing out of it, clear of the key above
            const float qx = px - dx * 8, qy = py - dy * 8;
            c->triangle(qx + dx * 7, qy + dy * 7, qx - dy * 5 - dx * 2, qy + dx * 5 - dy * 2,
                        qx + dy * 5 - dx * 2, qy - dx * 5 - dy * 2, ink);
        } else {
            if (v.answered()) c->circle(px, py, rad, ink);
            c->ring(px, py, rad + (isLast ? 1.0f : 0.7f), isLast ? 2.0f : 1.4f, v.answered() ? th::text : th::textFaint);
        }
        const std::string txt = "#" + std::to_string(i + 1);
        const Font& lf = isLast ? SB11 : S10;
        const int tw = 8 + 6 * int(std::to_string(i + 1).size()), tht = 11;
        const int cands[6][2] = { { int(rad) + 2, -tht - 1 }, { int(rad) + 2, 2 }, { -int(rad) - 2 - tw, -tht - 1 },
                                  { -int(rad) - 2 - tw, 2 }, { -tw / 2, -int(rad) - tht - 2 }, { -tw / 2, int(rad) + 2 } };
        int cx = cands[0][0], cy = cands[0][1];
        for (auto& cd : cands) {
            const int bx0 = int(px) + cd[0], by0 = int(py) + cd[1];
            if (bx0 < r.x + pl || bx0 + tw > r.x + w - pr || by0 < r.y + pt || by0 + tht > r.y + h - pb) continue;
            bool hit = false;
            for (const Rect& o : placed) hit = hit || (bx0 < o.r() && bx0 + tw > o.x && by0 < o.b() && by0 + tht > o.y);
            if (!hit) { cx = cd[0]; cy = cd[1]; break; }
        }
        const Rect lr(int(px) + cx, int(py) + cy, tw, tht);
        placed.push_back(lr);
        c->fill(Rect(lr.x - 1, lr.y, tw + 2, tht), gfx::hex(0x0E1418), 184);
        c->text(lr.x + 1, lr.y + tht - 2, txt, lf, th::text);
    }
    // The patient's own healthy tissue: DIAMONDS in a colour nothing else on
    // this chart uses, one per sample (H1, H2, …), so that where the lesion's
    // readings lie RELATIVE to this patient's normal tissue is seen at once.
    // With several, their median is the larger hollow diamond, and dashed lines
    // from it (or from the only sample) mark its IPS and its Z on the axes.
    const std::vector<app::TissueSample>& hs = a.tissueRefs();
    if (!hs.empty()) {
        const bool legacy = a.ipsIsLegacy();
        auto diamond = [&](float px, float py, float rad, bool filled, int alpha) {
            if (filled) {
                c->triangle(px, py - rad, px - rad, py, px + rad, py, th::healthy, alpha);
                c->triangle(px, py + rad, px - rad, py, px + rad, py, th::healthy, alpha);
            }
            const Color edge = filled ? gfx::hex(0xFFFFFF) : th::healthy;
            const float wv = filled ? 1.2f : 2.0f;
            c->line(px, py - rad, px + rad, py, wv, edge, alpha); c->line(px + rad, py, px, py + rad, wv, edge, alpha);
            c->line(px, py + rad, px - rad, py, wv, edge, alpha); c->line(px - rad, py, px, py - rad, wv, edge, alpha);
        };
        const birx::Features mf = app::App::medianOf(hs);
        const double miv = legacy ? mf.ipsLegacy : mf.ipsFit, mzk = mf.z1k / 1000;
        if (std::isfinite(miv) && std::isfinite(mzk)) {
            const float px = X(cx_(miv)), py = Y(cy_(mzk));
            c->dashed(px, py, px, float(r.y + h - pb), 3, 3, 1.2f, th::healthy, 200);
            c->dashed(px, py, float(r.x + pl), py, 3, 3, 1.2f, th::healthy, 200);
            char b[16]; std::snprintf(b, sizeof b, "%s%.1f", miv > 0 ? "+" : "", miv);
            const int tw = gfx::Canvas::measure(b, MB9);
            c->fill(Rect(int(px) - tw / 2 - 2, r.y + h - pb - 23, tw + 4, 11), gfx::hex(0x0E1418), 200);
            c->text(int(px) - tw / 2, r.y + h - pb - 14, b, MB9, th::healthy);
            std::snprintf(b, sizeof b, "%.2fk", mzk);
            const int zw = gfx::Canvas::measure(b, MB9);
            c->fill(Rect(r.x + pl + 2, int(py) + 2, zw + 4, 11), gfx::hex(0x0E1418), 200);
            c->text(r.x + pl + 4, int(py) + 11, b, MB9, th::healthy);
            if (hs.size() > 1) diamond(px, py, 10.0f, false, 255);
        }
        for (const app::TissueSample& sm : hs) {
            const double iv = legacy ? sm.features.ipsLegacy : sm.features.ipsFit, zk = sm.features.z1k / 1000;
            if (!std::isfinite(iv) || !std::isfinite(zk)) continue;
            const float px = X(cx_(iv)), py = Y(cy_(zk));
            diamond(px, py, 6.5f, true, 255);
            const std::string txt = "H" + std::to_string(sm.n);
            const int tw = gfx::Canvas::measure(txt, SB11) + 2, tht = 11;
            const int cands[4][2] = { { 9, -tht - 2 }, { -9 - tw, -tht - 2 }, { 9, 3 }, { -9 - tw, 3 } };
            int cx = cands[0][0], cy = cands[0][1];
            for (auto& cd : cands) {
                const int bx0 = int(px) + cd[0], by0 = int(py) + cd[1];
                if (bx0 < r.x + pl || bx0 + tw > r.x + w - pr || by0 < r.y + pt || by0 + tht > r.y + h - pb) continue;
                bool hit = false;
                for (const Rect& o : placed) hit = hit || (bx0 < o.r() && bx0 + tw > o.x && by0 < o.b() && by0 + tht > o.y);
                if (!hit) { cx = cd[0]; cy = cd[1]; break; }
            }
            const Rect lr(int(px) + cx, int(py) + cy, tw, tht);
            placed.push_back(lr);
            c->fill(Rect(lr.x - 1, lr.y, tw + 2, tht), gfx::hex(0x0E1418), 200);
            c->text(lr.x + 1, lr.y + tht - 2, txt, SB11, th::healthy);
        }
        // the key, in the empty top of the plot
    }
    // H7-1.7.0: one key on the title line above the plot — the colour of each
    // answer of the table (the boxes carry no words), then the two marks.
    {
        std::vector<std::pair<std::string, Color>> keys;
        for (const app::RuleRowCfg& row : rc.rows) {
            if (!row.enabled || row.label.empty()) continue;
            bool seen = false;
            for (auto& k : keys) seen = seen || k.first.compare(0, row.label.size(), row.label) == 0;
            if (seen) continue;
            Color col(128, 128, 128);
            gfx::parseColor(row.color, &col);
            // a row that lies wholly above the window is said to be there
            keys.push_back({ row.zMin >= y1 * 1000 ? row.label + " (above 5 k)" : row.label, col });
        }
        const bool marks = !hs.empty();
        int kw = 0;
        for (auto& k : keys) kw += 14 + gfx::Canvas::measure(k.first, S10) + 14;
        if (marks) kw += 15 + gfx::Canvas::measure("healthy tissue", S10) + 14 + 15 + gfx::Canvas::measure("reading", S10);
        int kx = r.x + pl + std::max(0, (w - pl - pr - kw) / 2);
        const int ky = r.y + pt - 7;
        for (auto& k : keys) {
            const Rect sw(kx, ky - 4, 10, 9);
            c->fill(sw, k.second, 200);
            c->frame(sw, gfx::lighter(k.second, 1.4f), 1, 255);
            kx += 14 + c->text(kx + 14, ky + 4, k.first, S10, gfx::lighter(k.second, 1.5f)) + 14;
        }
        if (marks) {
            c->triangle(float(kx + 5), float(ky - 5), float(kx), float(ky), float(kx + 10), float(ky), th::healthy);
            c->triangle(float(kx + 5), float(ky + 5), float(kx), float(ky), float(kx + 10), float(ky), th::healthy);
            kx += 15 + c->text(kx + 15, ky + 4, "healthy tissue", S10, th::healthy) + 14;
            c->ring(float(kx + 5), float(ky), 4.5f, 1.4f, th::text);
            c->text(kx + 15, ky + 4, "reading", S10, th::textDim);
        }
    }
    if (rs.empty() && hs.empty()) {
        c->label(Rect(r.x, r.y + r.h / 2 - 22, r.w, 16), "no reading", M11, th::textFaint, gfx::Center);
        c->label(Rect(r.x, r.y + r.h / 2 - 6, r.w, 16), "in this case yet", M11, th::textFaint, gfx::Center);
    }
    c->text(r.x + pl + 3, r.y + pt - 3, "Z 1kHz k\xCE\xA9 \xE2\x86\x91", M9, th::textFaint);
    const char* cap = "IPS \xE2\x86\x92";
    c->text(r.x + w - pr - gfx::Canvas::measure(cap, M9), r.y + pt - 3, cap, M9, th::textFaint);
    c->resetClip();
}

// ---------------------------------------------------------------------------
// DetailsBox.qml + FeaturePanel: the 22 values of the last reading

void Gui::detailsBox(const Rect&)
{
    const Rect box(th::pad, th::pad + 46 + th::gap, scrW - 2 * th::pad, scrH - 2 * th::pad - 46 - th::gap);
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 200);
    u->card(box, th::surface, th::accent, th::radius);
    // What is shown: the last reading (main screen), or one recorded reading
    // or healthy-tissue sample (opened from a row of the log).
    const std::vector<app::TissueSample>& refs = m_detLog ? m_detRefs : a.tissueRefs();
    const birx::Features mainF = m_detLog ? m_detMain : (a.haveLast() ? a.last().features : birx::Features());
    const bool haveMain = m_detLog || a.haveLast();
    const std::string title = m_detLog ? m_detTitle : std::string("All measured quantities");
    const std::string flags = m_detLog ? m_detFlags
                            : (a.haveLast() ? a.last().quality.summary() : std::string("no reading yet"));
    c->text(box.x + 14, box.y + 30, gfx::Canvas::elide(title, SB19, portrait() ? box.w - 160 : 300), SB19, th::text);
    c->text(box.x + 14, box.y + 48, gfx::Canvas::elide(a.ruleTable().version + " \xC2\xB7 flags: " + flags, M12, 440),
            M12, th::textFaint);
    // The patient's own healthy tissue, beside each value. With several
    // samples the column is their median, or any one of them: the chips choose.
    // A sample opened by itself is compared with the median of them all.
    const bool ref = m_detMainIsSample && m_detLog ? refs.size() > 1 : !refs.empty();
    if (m_detRef >= int(refs.size())) m_detRef = -1;
    const birx::Features refF = !ref ? birx::Features()
                              : (m_detRef < 0 ? app::App::medianOf(refs) : refs[size_t(m_detRef)].features);
    const int refW = 118, valW = 138;
    const Rect close(box.r() - 116, box.y + 12, 104, 42);
    if (u->outline(ID_DET_CLOSE, close, "Close", th::accent)) { m_details = false; m_detLog = false; }
    // ITDS-2.16.0: upright, the healthy tissue on lines of its own under the title, and one
    // column of 22 values instead of two of 11
    const bool tall = portrait();
    const int hx = tall ? box.x + 14 : box.x + 330;

    struct D { const char* key; const char* group; const char* label; const char* unit; };
    static const D kDesc[] = {
        { "z1k", "Placement", "Impedance at 1 kHz", "\xCE\xA9" },
        { "logZ1k", "Placement", "log10 impedance", "" },
        { "zRatio", "Impedance", "Z(1k) / Z(100k)", "" },
        { "zRatioHigh", "Impedance", "Z(1k) / Z(500k)", "" },
        { "ipsFit", "Phase slope", "IPS, fitted over the band", "\xC2\xB0/decade" },
        { "ipsLegacy", "Phase slope", "IPS, instrument definition", "\xC2\xB0/decade" },
        { "ipsTheilSen", "Phase slope", "IPS, robust estimate", "\xC2\xB0/decade" },
        { "phaseAtRefDeg", "Phase shape", "Phase level at 100 kHz", "\xC2\xB0" },
        { "curvature", "Phase shape", "Curvature", "" },
        { "phaseMinDeg", "Phase shape", "Lowest phase in the band", "\xC2\xB0" },
        { "logFAtPhaseMin", "Phase shape", "log10 f at that point", "" },
        { "coleR0", "Cole model", "R0, extracellular", "\xCE\xA9" },
        { "coleRinf", "Cole model", "Rinf, total", "\xCE\xA9" },
        { "coleRatio", "Cole model", "R0 / Rinf", "" },
        { "coleLogFc", "Cole model", "log10 characteristic freq.", "" },
        { "coleAlpha", "Cole model", "Dispersion alpha", "" },
        { "fitRmsDeg", "Quality", "Phase fit residual", "\xC2\xB0" },
        { "coleRmsRel", "Quality", "Cole fit residual", "" },
        { "nFit", "Instrument", "Points used in the fit", "" },
        { "rangeAt1k", "Instrument", "Gain range at 1 kHz", "" },
        { "rangeAt500k", "Instrument", "Gain range at 500 kHz", "" },
        { "rangeSwitches", "Instrument", "Gain changes in the band", "" },
    };
    const int cols = tall ? 1 : 2, rows = tall ? 22 : 11;
    const int areaY = tall ? box.y + 116 : box.y + 66;
    const Rect area(box.x + 14, areaY, box.w - 28, box.b() - 14 - areaY);
    const int colW = (area.w - th::gap) / cols, rowH = area.h / rows;
    const char* lastGroup[2] = { "", "" };
    if (ref) {
        std::string what;
        if (refs.size() == 1) what = "healthy tissue: " + app::App::sampleText(refs[0].features, a.ipsIsLegacy()) + " \xC2\xB7 " + refs[0].when;
        else if (m_detRef < 0) what = "median of " + std::to_string(refs.size()) + ": " + app::App::sampleText(refF, a.ipsIsLegacy());
        else what = "H" + std::to_string(refs[size_t(m_detRef)].n) + " \xC2\xB7 " + refs[size_t(m_detRef)].when + ": " +
                    app::App::sampleText(refF, a.ipsIsLegacy());
        if (refs.size() > 1) {
            // chips on the title line: median | H1 | H2 …
            int cx = hx;
            const int cy = tall ? box.y + 60 : box.y + 10, limit = tall ? box.r() - 14 : close.x - 10;
            auto chip = [&](int idx, const std::string& t) {
                const int cw = gfx::Canvas::measure(t, S13) + 20;
                if (cx + cw > limit) return;
                const Rect r(cx, cy, cw, 28);
                const bool on = m_detRef == idx;
                u->name("healthy " + t);
                if (u->clicked(ID_DET_REF + idx + 1, r)) m_detRef = idx;
                if (on) c->roundRect(r, th::radiusSm, th::healthy); else u->card(r, th::surfaceIn, th::stroke);
                c->label(r, t, S13, on ? th::bg : th::text, gfx::Center);
                cx += cw + 5;
            };
            chip(-1, "median of " + std::to_string(refs.size()));
            for (size_t k = 0; k < refs.size() && k < 12; ++k) chip(int(k), "H" + std::to_string(refs[k].n));
            if (tall) c->label(Rect(hx, box.y + 92, box.w - 28, 14), what, S12, th::healthy, gfx::Left);
            else c->label(Rect(box.x + 460, box.y + 40, close.x - 10 - (box.x + 460), 14), what, S12, th::healthy, gfx::Right);
        } else {
            c->text(hx, tall ? box.y + 76 : box.y + 30, what, S13, th::healthy);
        }
        for (int col = 0; col < cols; ++col) {
            const int cr = area.x + col * (colW + th::gap) + colW - 10;
            c->label(Rect(cr - valW, area.y - 13, valW, 12), m_detMainIsSample && m_detLog ? "THIS SAMPLE" : "THIS READING",
                     M9, th::textFaint, gfx::Right);
            c->label(Rect(cr - valW - refW, area.y - 13, refW, 12),
                     refs.size() > 1 && m_detRef < 0 ? "HEALTHY, MEDIAN" : "HEALTHY", M9, th::healthy, gfx::Right);
        }
    } else if (!(m_detMainIsSample && m_detLog)) {
        c->text(hx, tall ? box.y + 76 : box.y + 30, "no healthy-tissue sample for this case", S13, th::textFaint);
    }
    auto shown = [](const char* key, const char* unit, double v) {
        if (!std::isfinite(v)) return std::string(kDash);
        const std::string k = key;
        std::string val;
        if (k == "nFit" || k.compare(0, 5, "range") == 0) val = f2("%.0f", v);
        else if (std::fabs(v) >= 1000) val = f2("%.1f", v);
        else val = f2("%.3f", v);
        if (*unit) val += std::string(" ") + unit;
        return val;
    };
    for (int i = 0; i < 22; ++i) {
        const int col = i / rows, row = i % rows;
        const Rect cell(area.x + col * (colW + th::gap), area.y + row * rowH, colW, rowH - 2);
        c->fill(cell, row % 2 ? th::surface : th::surfaceHi);
        const D& d = kDesc[i];
        if (std::string(d.group) != lastGroup[col]) {
            lastGroup[col] = d.group;
            c->text(cell.x + 8, cell.y + 13, d.group, M9, th::accent);
        }
        c->text(cell.x + 8, cell.y + rowH - 10, d.label, S13, th::textDim);
        const double v = haveMain ? mainF.byName(d.key) : birx::kNaN;
        c->label(Rect(cell.x, cell.y, cell.w - 10, cell.h), shown(d.key, d.unit, v), M14,
                 std::isfinite(v) ? (m_detMainIsSample && m_detLog ? th::healthy : th::text) : th::textFaint, gfx::Right);
        if (ref) {
            const double rv = refF.byName(d.key);
            c->label(Rect(cell.r() - 10 - valW - refW, cell.y, refW, cell.h), shown(d.key, "", rv), M12,
                     std::isfinite(rv) ? th::healthy : th::textFaint, gfx::Right);
        }
    }
}

// ---------------------------------------------------------------------------
// CalibrateSheet.qml

void Gui::calibrateSheet()
{
    // H7-2.3.0: after the Air check, a lot used needle by needle goes on to the Saline check;
    // H7-2.10.0: every needle, and Training's needle to its own saline check (its QC)
    if (!m_calDip && !a.calTraining() && a.calStage() == app::CalStage::Done && a.dipNeeded() && !a.dipDone()) { m_calDip = true; a.dipOpen(); }
    if (!m_calRef && a.calTraining() && a.calStage() == app::CalStage::Done && !a.qcCheckOk()) { m_calRef = true; m_calRefStarted = false; }
    if (m_calDip) { dipSheet(); return; }
    if (m_calRef) { refSheet(); return; }
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    const app::CalStage st = a.calStage();
    const bool running = st == app::CalStage::Waiting || st == app::CalStage::Sweeping;
    const bool done = st == app::CalStage::Done;
    const int W = scrW - th::pad * 8;
    const int x = th::pad * 4;
    int y = 140;
    c->label(Rect(x, y, W, 34), done ? "Ready" : a.calTraining() ? "Air check (QC) \xE2\x80\x94 needle " + a.trainNeedle()
                                                                  : std::string("Calibrate this needle"), SB24, done ? th::good : th::text, gfx::Center);
    y += 50;
    const std::string body = running ? std::string("Measuring\xE2\x80\xA6 keep the needle still.")
                           : done ? a.needleCalibration()
                           : std::string("Hold the needle in air, clear of your hands and of any metal by "
                                         "about 100 mm. Then press Start. It takes six seconds.");
    y += c->textBox(Rect(x, y, W, 80), body, S16, th::textDim, gfx::Center, 3, 1.25f) + 10;
    if (!a.calError().empty() && !running) {
        y += c->textBox(Rect(x, y, W, 100), a.calError(), SB14, th::bad, gfx::Center, 5, 1.25f) + 10;
    }
    const std::string blocker = a.calBlocker();
    if (st == app::CalStage::Idle && !blocker.empty()) {
        const int bh = gfx::Canvas::wrapHeight(W - 24, blocker, S14, 4, 1.2f) + 20;
        const Rect br(x, y, W, bh);
        u->card(br, th::warnSoft, th::warn);
        c->textBox(br.inset(12, 10), blocker, S14, th::warn, gfx::Center, 4, 1.2f, true);
        y += bh + 10;
    }
    if (done && !a.calWarning().empty())
        y += c->textBox(Rect(x, y, W, 90), a.calWarning(), SB14, th::warn, gfx::Center, 4, 1.25f) + 8;
    // H7-2.6.1: after the Air check, Training's saline check (H7-2.10.0: its QC, and its reference)
    const bool offerRef = done && a.calTraining();
    if (offerRef) {
        y += c->textBox(Rect(x, y, W, 60), a.qcCheckOk() ? "Saline check \xE2\x9C\x93  " + a.matRefText()
                        : std::string("Next, its saline check: the tip in 0.9 % saline, two sweeps. No sweep of this needle "
                                      "before it passes."), S14, a.qcCheckOk() ? th::good : th::textDim, gfx::Center, 3, 1.25f) + 6;
    }
    y = std::max(y + 10, 380);
    const Rect b1(scrW / 2 - (done && !offerRef ? 100 : 200 + th::gap / 2), y, 200, th::touch);
    const Rect b2(b1.r() + th::gap, y, 280, th::touch);
    if (u->button(ID_CAL_CANCEL, b1, done ? "Close" : "Cancel", false, true)) {
        a.calStop();
        m_calib = false; m_calAutoClose = false;
    }
    if (offerRef && u->button(ID_CAL_TOREF, b2, a.qcCheckOk() ? "Saline check again" : "Saline check \xE2\x86\x92",
                              !a.qcCheckOk(), true)) {
        m_calRef = true; m_calRefStarted = false;
    }
    if (!done) {
        char lab[40];
        if (running) std::snprintf(lab, sizeof lab, "Stop \xE2\x80\x94 %d s", a.calSecondsLeft());
        else std::snprintf(lab, sizeof lab, "Start \xE2\x80\x94 6 s");
        u->why(ID_CAL_START, blocker);
        if (u->button(ID_CAL_START, b2, lab, true, running || blocker.empty())) {
            if (running) a.calStop();
            else a.calRun();
        }
    }
}


// ---------------------------------------------------------------------------
// H7-2.6.1: the needle's saline reference for the material library, after its Air check.
// H7-2.10.0: Training's saline check — its QC (two sweeps in 0.9 % saline at its temperature,
// within its lot's limits or the generic ones), its reference, and the check that closes the
// sweeps since the one before. Same shape as the Saline check before a case.
void Gui::refSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    const bool running = a.matRefRunning();
    const app::DipStage st = a.dipStage();
    // H7-2.13.0: this check passed (not an earlier one: a check due, or "not a check", leaves that one standing)
    const bool done = m_calRefStarted && !running && a.qcPasses() > m_calRefPass0;
    if (done && m_calAutoClose) {          // opened from a material's card: back to it
        if (!m_calDoneAt) m_calDoneAt = u->now() | 1;
        if (uint32_t(u->now() - m_calDoneAt) >= 1500) {
            m_calib = false; m_calRef = false; m_calAutoClose = false; m_calDoneAt = 0;
            return;
        }
        m_timerAt = m_calDoneAt + 1500;
    }
    const int W = scrW - th::pad * 8;
    const int x = th::pad * 4;
    int y = 84;
    const std::string lot = a.settings().trainLot;
    c->label(Rect(x, y, W, 34), done ? "Passed" : "Saline check \xE2\x80\x94 0.9 % (QC)", SB24, done ? th::good : th::text, gfx::Center);
    y += 42;
    c->label(Rect(x, y, W, 22), a.needleReady() ? "Air check \xE2\x9C\x93  " + a.needleCalibration() : std::string("No Air check yet"),
             S14, a.needleReady() ? th::good : th::warn, gfx::Center);
    y += 30;
    std::string body;
    if (running) {
        body = st == app::DipStage::Settling ? "Settling\xE2\x80\xA6 " + std::to_string(a.dipSecondsLeft()) + " s \xE2\x80\x94 keep the tip still in the saline."
                                             : std::string("Measuring\xE2\x80\xA6 keep the tip still in the saline (two sweeps).");
    } else if (done) {
        body = a.matMessage();
    } else {
        body = "Put the tip about 10 mm into 0.9 % saline, clear of the cup's sides, and keep it there: " +
               std::to_string(a.settings().settleS) + " s settling, then two sweeps. Enter the saline's own temperature (or its "
               "conductivity), then Start. The needle is checked against " +
               (lot.empty() ? std::string("the generic limits (no lot chosen)") : "lot " + lot + "'s limits") +
               "; it sweeps only after the check passes" +
               (a.qcPending() ? ", and this check closes the " + std::to_string(a.qcPending()) + " sweeps since the last one." : std::string("."));
    }
    y += c->textBox(Rect(x, y, W, 130), body, S16, done ? (a.matMessageOk() ? th::text : th::warn) : th::textDim, gfx::Center, 5, 1.25f) + 10;
    if (!done) {
        const int tw = fitW(300, 620);
        const Rect tb(scrW / 2 - 10 - tw, y, tw, 50);
        const Rect cb(scrW / 2 + 10, y, tw, 50);
        char tv[64], tn[16], cv[64], cn[16];
        const double tC = a.settings().salineTempC;      // H7-2.15.0: kept; asked for once after power-on
        std::snprintf(tv, sizeof tv, a.salineTempEntered() ? "Temperature  %.1f \xC2\xB0""C" : "Temperature  %.1f \xC2\xB0""C ?", tC);
        std::snprintf(tn, sizeof tn, "%.1f", tC);
        if (std::isfinite(m_dipCond)) std::snprintf(cv, sizeof cv, "Conductivity  %.2f mS/cm", m_dipCond);
        else std::snprintf(cv, sizeof cv, "Conductivity (optional)");
        std::snprintf(cn, sizeof cn, "%.2f", std::isfinite(m_dipCond) ? m_dipCond : 0.0);
        u->why(ID_DIP_TEMP, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
        if (u->button(ID_DIP_TEMP, tb, tv, false, !running))
            openNumber("Saline temperature, in the cup", tn, "\xC2\xB0""C", 5, 45, true, false, true,
                       [this](const std::string& v) { a.setSalineTemp(std::atof(v.c_str())); });
        u->why(ID_DIP_COND, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
        if (u->button(ID_DIP_COND, cb, cv, false, !running))
            openNumber("Conductivity at 25 \xC2\xB0""C (blank: none)", std::isfinite(m_dipCond) ? std::string(cn) : std::string(),
                       "mS/cm", 0, 40, false, false, true,
                       [this](const std::string& v) { const double d = std::atof(v.c_str()); m_dipCond = d > 0 ? d : birx::kNaN; });
        y += 60;
        if (!a.salineTempEntered() && !running && !std::isfinite(m_dipCond))
            y += c->textBox(Rect(x, y - 4, W, 40), tempHint(tC), S13, th::warn, gfx::Center, 2, 1.2f) + 4;
    }
    if (!done && !a.calWarning().empty())
        y += c->textBox(Rect(x, y, W, 60), a.calWarning(), S13, th::warn, gfx::Center, 3, 1.2f) + 6;
    const bool failed = m_calRefStarted && !running && !done && !a.matMessage().empty();
    if (failed) {
        y += c->textBox(Rect(x, y, W, 100), a.matMessage(), SB14, th::bad, gfx::Center, 5, 1.25f) + 8;
        // H7-2.6.2: the sweep itself, as it came (|Z| and phase), to see what the tip did
        const std::vector<birx::Point>& pts = a.matRefAttempt();
        if (pts.size() >= 3 && y < 330) {
            const Rect pz(x + 40, y, W / 2 - 60, 80), pp(x + W / 2 + 20, y, W / 2 - 60, 80);
            std::vector<std::pair<double, double>> zv, pv;
            for (const birx::Point& p : pts) if (p.freqHz > 0 && p.magOhm > 0) {
                zv.push_back({ std::log10(p.freqHz), std::log10(p.magOhm) });
                pv.push_back({ std::log10(p.freqHz), p.phaseDeg });
            }
            std::sort(zv.begin(), zv.end()); std::sort(pv.begin(), pv.end());
            auto panel = [&](const Rect& pr, const std::vector<std::pair<double, double>>& v, const char* name, bool logY) {
                c->fill(pr, th::surfaceIn, 160);
                double lo = 1e9, hi = -1e9;
                for (auto& q : v) { lo = std::min(lo, q.second); hi = std::max(hi, q.second); }
                if (!(hi > lo)) { lo -= 1; hi += 1; }
                const double pd = (hi - lo) * 0.1; lo -= pd; hi += pd;
                auto X = [&](double l) { return float(pr.x + (l - 2.6) / (5.8 - 2.6) * pr.w); };
                auto Y = [&](double val) { return float(pr.y + (1 - (val - lo) / (hi - lo)) * pr.h); };
                for (size_t i = 1; i < v.size(); ++i) c->line(X(v[i - 1].first), Y(v[i - 1].second), X(v[i].first), Y(v[i].second), 2.0f, th::accent);
                for (auto& q : v) c->circle(X(q.first), Y(q.second), 2.4f, th::accent);
                char b[48];
                if (logY) std::snprintf(b, sizeof b, "%s  %.0f\xE2\x80\xA6%.0f ohm", name, std::pow(10.0, lo + pd), std::pow(10.0, hi - pd));
                else std::snprintf(b, sizeof b, "%s  %.1f\xE2\x80\xA6%.1f\xC2\xB0", name, lo + pd, hi - pd);
                c->text(pr.x + 4, pr.y + 12, b, M9, th::textDim);
                c->text(pr.x, pr.b() + 12, "500 Hz", M9, th::textFaint);
                c->text(pr.r() - 40, pr.b() + 12, "500 kHz", M9, th::textFaint);
            };
            panel(pz, zv, "|Z|", true);
            panel(pp, pv, "phase", false);
            y += 80 + 18;
        }
    }
    const std::string blocker = a.matRefBlocker();
    if (!running && !done && !blocker.empty()) {
        const int bh = gfx::Canvas::wrapHeight(W - 24, blocker, S14, 4, 1.2f) + 20;
        const Rect br(x, y, W, bh);
        u->card(br, th::warnSoft, th::warn);
        c->textBox(br.inset(12, 10), blocker, S14, th::warn, gfx::Center, 4, 1.2f, true);
        y += bh + 10;
    }
    y = std::min(std::max(y + 6, 470), scrH - th::touch - 12);
    const int w01 = fitW(200, 680), w2 = fitW(260, 680);
    const Rect b0(done ? scrW / 2 - w01 - th::gap / 2 : scrW / 2 - (2 * w01 + w2 + 2 * th::gap) / 2, y, w01, th::touch);
    const Rect b1(b0.r() + th::gap, y, w01, th::touch);
    const Rect b2(b1.r() + th::gap, y, w2, th::touch);
    u->why(ID_SREF_AIR, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
    if (u->button(ID_SREF_AIR, b0, "Air check again", false, !running)) {
        m_calRef = false; a.calOpen(true);
        return;
    }
    if (u->button(ID_SREF_CLOSE, b1, done ? "Close" : "Cancel", false, true)) {
        if (running) a.dipStop();
        m_calib = false; m_calRef = false; m_calAutoClose = false;
        return;
    }
    if (!done) {
        char lab[48];
        if (running) std::snprintf(lab, sizeof lab, "Stop \xE2\x80\x94 %d s", a.dipSecondsLeft());
        else std::snprintf(lab, sizeof lab, "Start \xE2\x80\x94 %d s", a.settings().settleS + 8);
        u->why(ID_SREF_START, blocker);
        if (u->button(ID_SREF_START, b2, lab, true, running || blocker.empty())) {
            if (running) a.dipStop();
            else {
                m_calRefPass0 = a.qcPasses(); m_calDoneAt = 0;
                const std::string w = a.matStartRef(a.settings().salineTempC, m_dipCond);
                m_calRefStarted = w.empty();
                if (!w.empty()) a.uiAction("training saline check refused", w);
            }
        }
    }
    if (running) m_timerAt = u->now() + 500;
}

// ---------------------------------------------------------------------------
// The Saline check (H7-2.3.0, app_dip.cpp): the second half of a needle's
// checks for a lot used needle by needle. Same shape as the air sheet.
void Gui::dipSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    const app::DipStage st = a.dipStage();
    const bool running = st == app::DipStage::Waiting || st == app::DipStage::Sweeping || st == app::DipStage::Between ||
                         st == app::DipStage::Settling;
    const bool done = st == app::DipStage::Done && a.dipDone();
    const int W = scrW - th::pad * 8;
    const int x = th::pad * 4;
    int y = 96;
    c->label(Rect(x, y, W, 34), done ? "Ready" : "Saline check \xE2\x80\x94 " + a.dipSolutionName(), SB24, done ? th::good : th::text, gfx::Center);
    y += 44;
    c->label(Rect(x, y, W, 22), "Air check \xE2\x9C\x93  " + a.needleCalibration(), S14, th::good, gfx::Center);
    y += 34;
    const std::string body = st == app::DipStage::Settling
                               ? "Settling\xE2\x80\xA6 " + std::to_string(a.dipSecondsLeft()) + " s \xE2\x80\x94 keep the tip still in the saline."
                           : running ? std::string("Measuring\xE2\x80\xA6 keep the tip still in the saline (two sweeps).")
                           : done ? a.dipText() + ". Every reading of this needle is carried to the reference needle's."
                           : "Dip the tip about 10 mm into the " + a.dipSolutionName() + " saline" +
                             (a.dipStep() == birx::kCheck9 ? std::string(" (sterile, poured into a sterile cup)") : std::string()) +
                             ", clear of the cup's sides, and keep it there until the check is over: about " +
                             std::to_string(a.settings().settleS + 8) + " seconds (" + std::to_string(a.settings().settleS) +
                             " s settling, then two sweeps). Enter "
                             "the saline's own temperature (a thermometer in the cup), or its conductivity, then press Start." +
                             (a.dipValidated() ? std::string() : std::string(" This needle's lot was not validated in Settings "
                              "\xE2\x86\x92 Lots: it is checked against generic limits and its readings are marked so."));
    y += c->textBox(Rect(x, y, W, 130), body, S16, th::textDim, gfx::Center, 5, 1.25f) + 12;
    if (!done) {
        const int tw = fitW(300, 620);
        const Rect tb(scrW / 2 - 10 - tw, y, tw, 50);
        const Rect cb(scrW / 2 + 10, y, tw, 50);
        char tv[64], tn[16], cv[64], cn[16];
        const double tC = a.settings().salineTempC;      // H7-2.15.0: kept; asked for once after power-on
        std::snprintf(tv, sizeof tv, a.salineTempEntered() ? "Temperature  %.1f \xC2\xB0""C" : "Temperature  %.1f \xC2\xB0""C ?", tC);
        std::snprintf(tn, sizeof tn, "%.1f", tC);
        if (std::isfinite(m_dipCond)) std::snprintf(cv, sizeof cv, "Conductivity  %.2f mS/cm", m_dipCond);
        else std::snprintf(cv, sizeof cv, "Conductivity (optional)");
        std::snprintf(cn, sizeof cn, "%.2f", std::isfinite(m_dipCond) ? m_dipCond : 0.0);
        u->why(ID_DIP_TEMP, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
        if (u->button(ID_DIP_TEMP, tb, tv, false, !running))
            openNumber("Saline temperature, in the cup", tn, "\xC2\xB0""C", 5, 45, true, false, true,
                       [this](const std::string& v) { a.setSalineTemp(std::atof(v.c_str())); });
        u->why(ID_DIP_COND, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
        if (u->button(ID_DIP_COND, cb, cv, false, !running))
            openNumber("Conductivity at 25 \xC2\xB0""C (blank: none)", std::isfinite(m_dipCond) ? std::string(cn) : std::string(),
                       "mS/cm", 0, 40, false, false, true,
                       [this](const std::string& v) { const double d = std::atof(v.c_str()); m_dipCond = d > 0 ? d : birx::kNaN; });
        y += 62;
        if (!a.salineTempEntered() && !running && !std::isfinite(m_dipCond))
            y += c->textBox(Rect(x, y - 6, W, 40), tempHint(tC), S13, th::warn, gfx::Center, 2, 1.2f) + 4;
    }
    if (!a.dipError().empty() && !running)
        y += c->textBox(Rect(x, y, W, 100), a.dipError(), SB14, th::bad, gfx::Center, 5, 1.25f) + 10;
    const std::string blocker = a.dipBlocker();
    if (!running && !done && !blocker.empty()) {
        const int bh = gfx::Canvas::wrapHeight(W - 24, blocker, S14, 4, 1.2f) + 20;
        const Rect br(x, y, W, bh);
        u->card(br, th::warnSoft, th::warn);
        c->textBox(br.inset(12, 10), blocker, S14, th::warn, gfx::Center, 4, 1.2f, true);
        y += bh + 10;
    }
    y = std::max(y + 6, 470);
    const int w01 = fitW(200, 680), w2 = fitW(260, 680);
    const Rect b0(done ? scrW / 2 - w01 - th::gap / 2 : scrW / 2 - (2 * w01 + w2 + 2 * th::gap) / 2, y, w01, th::touch);
    const Rect b1(b0.r() + th::gap, y, w01, th::touch);
    const Rect b2(b1.r() + th::gap, y, w2, th::touch);
    u->why(ID_DIP_AIR, "Wait: a sweep is running. Press Stop, or wait for it to finish.");
    if (u->button(ID_DIP_AIR, b0, "Air check again", false, !running)) {
        a.dipStop(); m_calDip = false; a.calOpen();
        return;
    }
    if (u->button(ID_CAL_CANCEL, b1, done ? "Close" : "Cancel", false, true)) {
        a.dipStop(); a.calStop();
        m_calib = false; m_calDip = false;
        return;
    }
    if (!done) {
        char lab[40];
        if (running) std::snprintf(lab, sizeof lab, "Stop \xE2\x80\x94 %d s", a.dipSecondsLeft());
        else std::snprintf(lab, sizeof lab, "Start \xE2\x80\x94 %d s", a.settings().settleS + 8);
        u->why(ID_DIP_START, blocker);
        if (u->button(ID_DIP_START, b2, lab, true, running || blocker.empty())) {
            if (running) a.dipStop();
            else a.dipRun(a.settings().salineTempC, m_dipCond);
        }
    }
}

// ---------------------------------------------------------------------------
// The healthy-tissue sample (app_tissue.cpp). Same shape as the needle
// calibration sheet: say what to do, one button, the result with its numbers.

void Gui::tissueSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 250);
    const app::RefStage st = a.refStage();
    const bool running = st == app::RefStage::Waiting || st == app::RefStage::Sweeping;
    const bool done = st == app::RefStage::Done;
    const std::vector<app::TissueSample>& refs = a.tissueRefs();
    const int W = scrW - th::pad * 8;
    const int x = th::pad * 4;
    int y = 62;
    const std::string title = done ? "Healthy tissue \xE2\x80\x94 H" + std::to_string(refs.empty() ? 1 : refs.back().n) + " kept"
                                   : std::string("Healthy tissue of this patient");
    c->label(Rect(x, y, W, 34), title, SB24, done ? th::healthy : th::text, gfx::Center);
    y += 44;
    const std::string body = running ? std::string("Measuring\xE2\x80\xA6 keep the needle still.")
                           : std::string("With the needle tip in NORMAL tissue, on the way in and before the lesion is "
                                         "entered, hold still and press Start. Each sweep is one sample of this patient's "
                                         "own baseline; as many as wanted can be taken, in different places. They are not "
                                         "readings of the lesion: not judged, not counted.");
    y += c->textBox(Rect(x, y, W, 100), body, S14, th::textDim, gfx::Center, 4, 1.25f) + 12;

    // the samples of this case so far (the last six; all of them are in DETAILS and in the log)
    if (!refs.empty()) {
        const size_t from = refs.size() > 6 ? refs.size() - 6 : 0;
        if (from > 0) { c->label(Rect(x, y, W, 16), "\xE2\x80\xA6 and " + std::to_string(from) + " earlier", S12, th::textFaint, gfx::Center); y += 18; }
        for (size_t k = from; k < refs.size(); ++k) {
            const app::TissueSample& s = refs[k];
            const bool last = k + 1 == refs.size();
            const Rect row = portrait() ? Rect(th::pad, y, scrW - 2 * th::pad, 26) : Rect(x + 120, y, W - 240, 26);
            c->fill(row, (k % 2) ? th::surface : th::surfaceHi);
            c->text(row.x + 10, row.y + 18, "H" + std::to_string(s.n), MB14, th::healthy);
            c->text(row.x + 60, row.y + 18, s.when, M12, th::textDim);
            c->text(row.x + 150, row.y + 18, app::App::sampleText(s.features, a.ipsIsLegacy()), M14, last && done ? th::healthy : th::text);
            c->label(Rect(row.x + 400, row.y, row.w - 410, row.h), s.flags, S12, th::textFaint, gfx::Right);
            y += 27;
        }
        if (refs.size() > 1) {
            c->label(Rect(x, y + 2, W, 18), "median of " + std::to_string(refs.size()) + ": " +
                     app::App::sampleText(app::App::medianOf(refs), a.ipsIsLegacy()), SB14, th::healthy, gfx::Center);
            y += 24;
        }
        y += 6;
    }
    if (!a.refError().empty() && !running)
        y += c->textBox(Rect(x, y, W, 80), a.refError(), SB14, th::bad, gfx::Center, 4, 1.25f) + 8;
    const std::string blocker = a.refBlocker();
    if (!running && !blocker.empty()) {
        const int bh = gfx::Canvas::wrapHeight(W - 24, blocker, S14, 4, 1.2f) + 20;
        const Rect br(x, y, W, bh);
        u->card(br, th::warnSoft, th::warn);
        c->textBox(br.inset(12, 10), blocker, S14, th::warn, gfx::Center, 4, 1.2f, true);
        y += bh + 8;
    }
    y = std::min(std::max(y + 8, 420), scrH - th::touch - 16);
    // Close | Remove last | Start / Take another
    const bool canRemove = !running && !refs.empty();
    const int w1 = fitW(170, 650), w2 = fitW(210, 650), w3 = fitW(250, 650);
    const int total = w1 + (canRemove ? w2 + th::gap : 0) + w3 + th::gap;
    int bx = scrW / 2 - total / 2;
    if (u->button(ID_REF_CANCEL, Rect(bx, y, w1, th::touch), running ? "Cancel" : "Close", false, true)) {
        a.refStop();
        m_tissue = false;
    }
    bx += w1 + th::gap;
    if (canRemove) {
        ui::Ui::Btn rb;
        rb.danger = true;
        if (u->button(ID_REF_REMOVE, Rect(bx, y, w2, th::touch), "Remove H" + std::to_string(refs.back().n), rb))
            a.refRemoveLast();
        bx += w2 + th::gap;
    }
    char lab[40];
    if (running) std::snprintf(lab, sizeof lab, "Stop \xE2\x80\x94 %d s", a.refSecondsLeft());
    else std::snprintf(lab, sizeof lab, "%s", refs.empty() ? "Start" : "Take another");
    u->why(ID_REF_START, blocker);
    if (u->button(ID_REF_START, Rect(bx, y, w3, th::touch), lab, true, running || blocker.empty())) {
        if (running) a.refStop();
        else { a.refOpen(); a.refRun(); }
    }
}

// DETAILS for one row of the log: a recorded reading (its 22 quantities are in
// its row of dataset.csv) or one healthy-tissue sample (tissue_reference.csv),
// with the healthy samples of that case beside it.
void Gui::openLogDetails(const std::string& caseId, const app::LogReading* reading, const app::TissueSample* sample)
{
    m_detRefs = a.tissueSamplesOf(caseId);
    m_detRef = -1;
    m_detMain = birx::Features();
    m_detFlags.clear();
    if (sample) {
        m_detMainIsSample = true;
        m_detMain = sample->features;
        m_detFlags = sample->flags;
        m_detTitle = "Healthy tissue H" + std::to_string(sample->n) + " \xC2\xB7 " + sample->when;
    } else if (reading) {
        m_detMainIsSample = false;
        if (!a.readingFeatures(caseId, reading->epochMs, &m_detMain, &m_detFlags)) {
            m_detMain.z1k = reading->z1k; m_detMain.ipsLegacy = reading->ipsLegacy; m_detMain.ipsFit = reading->ipsFit;
            m_detFlags = reading->flags + " (the row was not found in the record)";
        }
        m_detTitle = "Reading " + std::to_string(reading->site) + " \xC2\xB7 " + reading->when;
    } else return;
    m_detLog = true;
    m_details = true;
    a.uiAction("log details", caseId + " \xC2\xB7 " + m_detTitle);
}

} // namespace gui

namespace gui {

// The simulator and the ITDS tests: straight to a screen, a Settings tab or a sheet, so that
// every one can be drawn (and checked) at both frame sizes without a script of taps for each.
void Gui::debugShow(const std::string& what)
{
    const size_t sp = what.find(' ');
    const std::string k = what.substr(0, sp);
    const int n = sp == std::string::npos ? 0 : std::atoi(what.c_str() + sp + 1);
    if (k == "measure")  { m_scr = Measure; }
    else if (k == "logs")     { m_scr = Logs; m_logsShown = false; }
    else if (k == "lock")     { m_scr = Lock; }
    else if (k == "intake")   { m_scr = Intake; }
    else if (k == "settings") { m_scr = Settings; m_tab = n; m_editLoaded = false; m_rulesLoaded = false; }
    else if (k == "details")  { m_details = true; m_detLog = false; m_detRef = -1; }
    else if (k == "calib")    { a.calOpen(); m_calib = true; m_calDip = false; m_calRef = false; }
    else if (k == "dip")      { m_calib = true; m_calDip = true; m_calRef = false; a.dipOpen(); }
    else if (k == "tissue")   { a.refOpen(); m_tissue = true; }
    else if (k == "keyboard") { openText("Needle revision", "NDL-D", 16, [](const std::string&) {}); m_pad.shift = n != 0; }
    else if (k == "numpad")   { openNumber("Age", "54", "years", 18, 99, true, false, false, [](const std::string&) {}); }
    else if (k == "clock")    { m_clockVal = a.nowLocal(); m_clockEdit = true; }
    else if (k == "passcode") { m_pass = PassSheet(); m_pass.open = true; }
    else if (k == "outcome")  { openOutcome(std::string()); }
    else if (k == "caseedit") { m_caseEdit = true; m_editCase = a.caseId(); m_editVals.clear(); m_editPage = n > 0 ? n : 1; m_editMsg.clear(); }
    else if (k == "delete")   { m_delAsk = true; }
    else if (k == "monitor")  { m_monitor = true; }
    else if (k == "allfiles") { m_allSheet = true; }
    else if (k == "train")    { m_trainSheet = true; }
    else if (k == "tag")      { m_tagSheet = true; m_tagCase = a.caseId(); }
    else if (k == "lotsheet") { m_lotSheet = n; }
    else if (k == "rule")     { m_ruleOpen = n; }
    else if (k == "caserule") { if (!m_rulesLoaded) { m_rules = a.settings().rules; m_rulesLoaded = true; }
                                app::CaseRuleCfg cr; cr.minCount = 2; cr.ofLabel = "High Risk"; cr.label = "High Risk";
                                cr.color = "#C62828"; cr.level = "high"; m_rules.caseRows.push_back(cr); }
    else if (k == "logopen")  { m_expanded = a.caseId(); }
    else if (k == "matsel")   { if (!a.matLibrary().labels.empty()) m_matSel = a.matLibrary().labels[0]; }
    else if (k == "lotview")  { m_lotShowResult = n == 1; m_lotCompare = n == 2; m_lotRaw = n == 3; m_lotRawPage = 0; m_lotRawRow = -1; }
    else if (k == "lotpage")  { m_lotRawPage = n; }
    else if (k == "numneg")   { openNumber("Offset", "-3", "", -50, 50, true, true, true, [](const std::string&) {}); }
    else if (k == "close")    { m_details = m_calib = m_tissue = m_clockEdit = m_caseEdit = m_delAsk = m_monitor = m_allSheet =
                                m_trainSheet = m_tagSheet = m_outcome = false; m_pass.open = false; m_pad.open = false; m_lotSheet = -1; }
    m_again = true;
}

} // namespace gui
