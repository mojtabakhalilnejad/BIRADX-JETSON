// gui/gui_lotcal.cpp — Settings -> Lots (H7-2.0.0): the reference needle, the
// lots measured against it, the test sheet of one lot, its result, and the lot
// calibration file. The arithmetic is birx::evaluateLot (lotfit.h); this file
// only shows it and asks for the sweeps.
#include "gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace fonts;
using gfx::Rect;
using gfx::Color;
namespace th = ui::th;

namespace gui {

namespace {
enum Id {
    ID_L_SCROLL = 13000, ID_L_TRIAL, ID_L_TRIAL2, ID_L_EXPORT, ID_L_IMPORT,
    ID_L_NM, ID_L_NP, ID_L_RM, ID_L_RP, ID_L_PH = 13020, ID_L_ADDREF = 13030, ID_L_ADD, ID_L_ADDCTRL, ID_L_CLRFLOOR,
    ID_L_ROW = 13100,
    ID_LS_CLOSE = 13400, ID_LS_GRID = 13410, ID_LS_START = 13700, ID_LS_STOP, ID_LS_TEMP, ID_LS_COND,
    ID_LS_RESULT, ID_LS_TESTS, ID_LS_DEL, ID_LS_APPROVE, ID_LS_RSCROLL, ID_LS_GSCROLL, ID_LS_CMP, ID_LS_RAW,
    ID_LR_PREV = 13780, ID_LR_NEXT, ID_LR_BACK, ID_LR_REMOVE = 13790, ID_LR_ROW = 13800,
    ID_LS_FLOOR = 13740, ID_LS_RM = 13900, ID_LC_STEP = 13750, ID_LC_SCROLL = 13760, ID_LC_CURVES = 13770, ID_LC_DIFF, ID_L_SETM = 13745, ID_L_SETP,
};

Color verdictColor(birx::LotVerdict2 v)
{
    switch (v) {
    case birx::LotVerdict2::SameAsReference:       return th::good;
    case birx::LotVerdict2::UsableWithCalibration: return th::accent;
    case birx::LotVerdict2::RangesMustChange:      return th::warn;
    case birx::LotVerdict2::Rejected:              return th::bad;
    default: break;
    }
    return th::textFaint;
}

std::string num1(double v, const char* f = "%.1f")
{
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
} // namespace

// ---------------------------------------------------------------------------
void Gui::tabLots(const Rect& body)
{
    static int contentH = 1200;
    const int off = u->scroll(ID_L_SCROLL, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    auto row = [&](const std::string& l, const std::string& n, int rightW) {
        const int h = settingRow(Rect(body.x, y, W, 0), l, n, rightW);
        const Rect right = m_rowR;
        y += h + 8;
        return right;
    };
    if (!m_msg.empty()) {
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 6, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 6, 1.25f);
        y += h + 8;
    }
    y += c->textBox(Rect(body.x + 4, y, W - 8, 120),
        "A new lot of needles (or cables, or connectors) is measured here against the REFERENCE: the needle the "
        "trial cut-offs were found with. Each needle of the lot is swept in air and in saline of up to three "
        "strengths; the result says whether the lot reads like the reference, can be corrected to read like it, "
        "or would need different rule ranges. Approved lots go into the lot calibration file, which every device "
        "loads; a reading taken with a needle of a lot in that file is corrected by itself (the lot is the one "
        "typed at intake).", S13, th::textDim, gfx::Left, 0, 1.3f) + 12;

    const birx::LotFile& lf = a.lotFile();
    {
        Rect r = row("The rule ranges were found with",
                     "Set in Settings \xE2\x86\x92 Rules. It decides how a lot's readings are made into the reference "
                     "needle's, and it goes into the lot file.", 380);
        c->label(r, a.airCalUsed() ? "air-calibrated needles" : "needles not calibrated", SB16, th::text, gfx::Right);
    }
    {
        char b[200];
        if (a.lotFileLoaded())
            std::snprintf(b, sizeof b, "Rev %d, %d lots, from device %s, %s. Reference: %s.", lf.rev, int(lf.lots.size()),
                          lf.device.empty() ? "?" : lf.device.c_str(), lf.issued.substr(0, 10).c_str(),
                          lf.refLot.empty() ? "none" : lf.refLot.c_str());
        else std::snprintf(b, sizeof b, "None on this device. Approve a lot below, or load the file made on another device.");
        Rect r = row("Lot calibration file", b, 380);
        const int bw = 184;
        u->why(ID_L_EXPORT, "No lot file is loaded yet: put a lot in it first (Put in the lot file), or Load from USB.");
        if (u->button(ID_L_EXPORT, Rect(r.x, r.y + (r.h - 44) / 2, bw, 44), "Export to USB", false, a.lotFileLoaded())) {
            const std::string m = a.lotExport(); say(m, m.find("Written") != 0);
        }
        if (u->button(ID_L_IMPORT, Rect(r.x + bw + 8, r.y + (r.h - 44) / 2, bw, 44), "Load from USB", false, true)) {
            const std::string m = a.lotImport(); say(m, m.find("Loaded") != 0);
        }
    }
    {
        Rect r = row("Plan for the next lot",
                     "Needles sampled from the lot, sweeps per needle per step, and saline strengths (0.9 %, then "
                     "0.225 %, then 0.09 %). 5 x 3 with all three is the full test: 60 sweeps, about 15 minutes. The "
                     "reference uses the same sweeps and strengths, on its one needle.", 496);
        auto stepper = [&](int idm, int idp, int x, int* v, int lo, int hi, const char* unit) {
            const int yy = r.y + (r.h - 44) / 2;
            u->why(idm, "This is the smallest: " + std::to_string(lo) + " " + unit + "."); u->why(idp, "This is the largest: " + std::to_string(hi) + " " + unit + ".");
            if (u->button(idm, Rect(x, yy, 40, 44), "\xE2\x88\x92", false, *v > lo)) --*v;
            char b[32]; std::snprintf(b, sizeof b, "%d %s", *v, unit);
            c->label(Rect(x + 40, yy, 80, 44), b, SB14, th::text, gfx::Center);
            if (u->button(idp, Rect(x + 120, yy, 40, 44), "+", false, *v < hi)) ++*v;
        };
        stepper(ID_L_NM, ID_L_NP, r.x, &m_lotPlan.needles, 1, 20, "needles");
        stepper(ID_L_RM, ID_L_RP, r.x + 168, &m_lotPlan.repeats, 1, 10, "sweeps");
        stepper(ID_L_PH, ID_L_PH + 1, r.x + 336, &m_lotPlan.phantoms, 1, 3, "saline");
    }
    // H7-2.6.0: the seconds the tip settles in a solution before its sweep
    {
        const int s = a.settings().settleS;
        Rect r = row("Settling time in saline",
                     "After Start the tip stays in the solution this long before its sweep begins, in the lot tests and "
                     "in the Saline check before a case alike, so that every needle is measured at the same moment after "
                     "it went in. Dip the tip, then press Start.", 200);
        const int yy = r.y + (r.h - 44) / 2;
        u->why(ID_L_SETM, "Already 0 s: no settling."); u->why(ID_L_SETP, "60 s at most.");
        if (u->button(ID_L_SETM, Rect(r.x, yy, 40, 44), "\xE2\x88\x92", false, s > 0)) a.setSettleSeconds(std::max(0, s - 5));
        char b[24]; std::snprintf(b, sizeof b, "%d s", s);
        c->label(Rect(r.x + 40, yy, 120, 44), b, SB16, th::text, gfx::Center);
        if (u->button(ID_L_SETP, Rect(r.x + 160, yy, 40, 44), "+", false, s < 60)) a.setSettleSeconds(std::min(60, s + 5));
    }

    const auto& runs = a.lotRuns();
    auto lotRow = [&](int i) {
        const birx::LotRun& lr = runs[size_t(i)];
        const birx::LotResult res = a.lotResult(i);
        const Rect rr(body.x, y, W, 64);
        if (u->clicked(ID_L_ROW + i, rr)) {
            m_lotSheet = i; m_lotShowResult = false; m_lotCompare = false; m_lotRaw = false; m_lotSel = -1; m_lotDelArm = 0; a.clearLotMessage();
            u->resetScroll(ID_LS_RSCROLL); u->resetScroll(ID_LS_GSCROLL);
        }
        u->card(rr, u->pressed(ID_L_ROW + i) ? th::accentSoft : th::surface, lr.reference ? th::healthy : th::stroke);
        int x = rr.x + 14;
        if (lr.reference || lr.control) {
            const Rect bdg(x, rr.y + 10, 52, 20);
            c->roundRect(bdg, 4, lr.reference ? th::healthy : th::warn);
            c->label(bdg, lr.reference ? "REF" : "CTRL", SB11, gfx::hex(0x101418), gfx::Center);
            x += 60;
        }
        c->text(x, rr.y + 27, gfx::Canvas::elide(lr.lotId, SB16, 300), SB16, th::text);
        char b[160];
        std::snprintf(b, sizeof b, "%d of %d sweeps \xC2\xB7 %d needle%s, %d sweeps each, air + %d saline%s", lr.doneTests(),
                      lr.plannedTests(), lr.plan.needles, lr.plan.needles == 1 ? "" : "s", lr.plan.repeats, lr.plan.phantoms,
                      a.lotFile().find(lr.lotId) ? " \xC2\xB7 in the lot file" : "");
        c->text(rr.x + 14, rr.y + 50, b, S12, th::textDim);
        // H7-2.5.0: a lot not every needle of which is done says what the done ones give
        const std::string v = lr.reference ? (lr.complete() ? std::string("Measured") : std::string("Incomplete"))
                            : res.provisional ? "Incomplete \xC2\xB7 " + std::to_string(res.needlesDone) + " of " +
                                                std::to_string(lr.plan.needles) + " needles: " + birx::lotVerdictName(res.soFar)
                                              : std::string(birx::lotVerdictName(res.verdict));
        const Color vc = lr.reference ? (lr.complete() ? th::healthy : th::textFaint) : verdictColor(res.verdict);
        const int vw = gfx::Canvas::measure(v, SB14) + 24;
        const Rect vr(rr.r() - 14 - vw, rr.y + 16, vw, 32);
        c->roundFrame(vr, 6, 1.5f, vc);
        c->label(vr, v, SB14, vc, gfx::Center);
        y += 72;
    };

    c->text(body.x + 4, y + 20, "REFERENCE", SB12, th::textFaint);
    y += 30;
    const int ri = a.lotReferenceIdx();
    if (ri >= 0) lotRow(ri);
    else if (u->button(ID_L_ADDREF, Rect(body.x, y, 320, 50), "Add the reference needle", true, true)) {
        openText("Reference needle: its lot or name", "", 24, [this](const std::string& v) {
            birx::LotPlan p = m_lotPlan; p.needles = 1;
            const std::string w = a.lotAdd(v, true, p);
            if (!w.empty()) say(w, true); else say("Reference " + v + " added. Open it and measure it.");
        }, "The needle (with its cable and connector) the trial cut-offs were found with.");
    }
    if (ri < 0) y += 58;
    c->text(body.x + 4, y + 20, "LOTS", SB12, th::textFaint);
    y += 30;
    int nl = 0;
    for (int i = 0; i < int(runs.size()); ++i) if (!runs[size_t(i)].reference) { lotRow(i); ++nl; }
    if (!nl) {
        c->text(body.x + 4, y + 18, "No lot measured yet.", S14, th::textFaint);
        y += 30;
    }
    if (u->button(ID_L_ADD, Rect(body.x, y, 320, 50), "Add a lot", true, true)) {
        openText("New lot number", "", 24, [this](const std::string& v) {
            const std::string w = a.lotAdd(v, false, m_lotPlan);
            if (!w.empty()) say(w, true); else say("Lot " + v + " added. Open it and measure its needles.");
        }, "As printed on the box, and as it will be typed at intake.");
    }
    // H7-2.4.0: a control run, the reference needle measured again
    u->why(ID_L_ADDCTRL, "Measure the reference needle's lot completely first (Add the reference needle, every repeat).");
    // ITDS-2.16.0: on a narrow screen the control's button under the lot's
    const bool narrowAdd = W < 720;
    if (narrowAdd) y += 58;
    if (u->button(ID_L_ADDCTRL, narrowAdd ? Rect(body.x, y, 360, 50) : Rect(body.x + 332, y, 360, 50), "Add a control (the reference again)", false,
                  ri >= 0 && runs[size_t(ri)].complete())) {
        openText("Control run: a name", "CONTROL-" + std::to_string(int(runs.size())), 24, [this](const std::string& v) {
            const std::string w = a.lotAdd(v, false, m_lotPlan, true);
            if (!w.empty()) say(w, true); else say("Control " + v + " added: measure the reference needle again, with fresh solutions.");
        }, "The reference needle measured again, in another session, with the same plan. Its differences from the "
           "reference are how far the instrument and the needle repeat by themselves.");
    }
    y += 64;
    // H7-2.8.0: the repeatability floor under every lot's tolerances
    {
        const app::Settings& st = a.settings();
        char b[300];
        if (!std::isfinite(st.floorIps))
            std::snprintf(b, sizeof b, "None yet: the default tolerances apply. Measure a control run (the reference needle "
                          "again) and use it as the floor, so that the tolerances come from this instrument.");
        else if (st.floorIps <= birx::LotLimits::kFloorIpsMax && st.floorZ <= birx::LotLimits::kFloorZMax &&
                 st.floorDrift <= birx::LotLimits::kFloorDriftMax && st.floorLeak <= birx::LotLimits::kFloorLeakMax)
            std::snprintf(b, sizeof b, "From the control runs: IPS %.2f, Z %.1f %%, drift %.1f %%, leak ratio %.2f. No lot is "
                          "judged tighter than this.", st.floorIps, st.floorZ * 100, st.floorDrift * 100, st.floorLeak);
        else
            std::snprintf(b, sizeof b, "SET TOO WIDE (IPS %.2f, Z %.1f %%): more than the same needle differs from itself. "
                          "Only IPS %.2f and Z %.0f %% are used. Clear it and measure the control again.", st.floorIps,
                          st.floorZ * 100, birx::LotLimits::kFloorIpsMax, birx::LotLimits::kFloorZMax * 100);
        Rect r = row("Repeatability floor", b, 200);
        if (std::isfinite(st.floorIps) && u->button(ID_L_CLRFLOOR, Rect(r.x, r.y + (r.h - 44) / 2, 200, 44), "Clear", false, true))
            say(a.lotClearFloor());
    }
    contentH = y + off - body.y;
    c->resetClip();
}

// ---------------------------------------------------------------------------
void Gui::lotSheet()
{
    const auto& runs = a.lotRuns();
    if (m_lotSheet < 0 || m_lotSheet >= int(runs.size())) { m_lotSheet = -1; return; }
    const int idx = m_lotSheet;
    const birx::LotRun& lr = runs[size_t(idx)];
    const uint32_t now = u->now();
    const bool testing = a.lotTesting();

    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    c->text(th::pad, 34, gfx::Canvas::elide((lr.reference ? "Reference " : "Lot ") + lr.lotId, SB24, 440), SB24,
            lr.reference ? th::healthy : th::text);
    char sub[160];
    std::snprintf(sub, sizeof sub, "%d needle%s x (air + %d saline) x %d sweeps \xC2\xB7 %d of %d done", lr.plan.needles,
                  lr.plan.needles == 1 ? "" : "s",
                  lr.plan.phantoms, lr.plan.repeats, lr.doneTests(), lr.plannedTests());
    c->text(th::pad, 56, sub, S13, th::textFaint);
    const Rect close(scrW - th::pad - 46, th::pad, 46, 44);
    u->why(ID_LS_CLOSE, "Wait: a lot test sweep is running. Stop it, or let it finish.");
    if (u->clicked(ID_LS_CLOSE, close, !testing)) { m_lotSheet = -1; return; }
    u->card(close, u->pressed(ID_LS_CLOSE) ? th::accentSoft : th::bg, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, testing ? th::textFaint : th::textDim, gfx::Center);

    // view switch: the tests, the result, the comparison with the reference (H7-2.2.0), the raw data (H7-2.2.1)
    {
        // ITDS-2.16.0: upright, the four views on a line of their own under the title
        const int cw0 = 104;
        const int ty = portrait() ? 70 : th::pad;
        const Rect t1(portrait() ? th::pad : scrW - th::pad - 46 - 8 - 4 * cw0 - 24, ty, cw0, 44), t2(t1.r() + 8, ty, cw0, 44),
                   t3(t2.r() + 8, ty, cw0, 44), t4(t3.r() + 8, ty, cw0, 44);
        if (u->chip(ID_LS_TESTS, t1, "Tests", !m_lotShowResult && !m_lotCompare && !m_lotRaw)) {
            m_lotShowResult = false; m_lotCompare = false; m_lotRaw = false;
        }
        if (u->chip(ID_LS_RESULT, t2, "Result", m_lotShowResult && !m_lotCompare && !m_lotRaw)) {
            m_lotShowResult = true; m_lotCompare = false; m_lotRaw = false; u->resetScroll(ID_LS_RSCROLL);
        }
        u->why(ID_LS_CMP, "The reference needle is what the others are compared with: open another lot to compare it.");
        if (u->chip(ID_LS_CMP, t3, "Compare", m_lotCompare && !m_lotRaw, !lr.reference)) {
            m_lotCompare = true; m_lotShowResult = false; m_lotRaw = false;
        }
        if (u->chip(ID_LS_RAW, t4, "Raw", m_lotRaw)) { m_lotRaw = true; m_lotRawPage = 0; m_lotRawRow = -1; }
    }

    const int areaY = portrait() ? 126 : 76;
    const Rect area(th::pad, areaY, scrW - 2 * th::pad, scrH - areaY - th::pad);
    const bool tall = portrait();   // the bottom buttons with their sentence above them, the panel under the grid
    if (m_lotRaw) { lotRawView(idx, area); return; }
    if (m_lotCompare && !lr.reference) { lotCompareView(idx, area); return; }
    if (m_lotShowResult) {
        // ---------------- the result
        const birx::LotResult res = a.lotResult(idx);
        static int contentH = 600;
        const Rect vp(area.x, area.y, area.w, area.h - 64 - (tall ? 62 : 0));
        const int off = u->scroll(ID_LS_RSCROLL, vp, contentH);
        c->setClip(vp);
        int y = vp.y - off;
        const int W = vp.w - 10;
        // H7-2.5.0: a lot not every needle of which is done says what the done ones give
        const std::string vname = lr.reference ? (lr.complete() ? std::string("Reference measured") : std::string("Incomplete"))
                                : res.provisional ? std::string("Provisional: ") + birx::lotVerdictName(res.soFar)
                                                  : std::string(birx::lotVerdictName(res.verdict));
        const Color vc = lr.reference ? th::healthy : verdictColor(res.provisional ? res.soFar : res.verdict);
        const int hh = gfx::Canvas::wrapHeight(W - 28, res.headline, S16, 8, 1.3f) + 64;
        u->card(Rect(vp.x, y, W, hh), th::surface, vc, th::radius);
        c->text(vp.x + 14, y + 34, vname, SB24, vc);
        c->textBox(Rect(vp.x + 14, y + 46, W - 28, hh - 52), res.headline, S16, th::text, gfx::Left, 8, 1.3f);
        y += hh + 10;
        // H7-2.3.0: each needle with its own model
        if (res.model && !res.needleQc.empty()) {
            c->text(vp.x + 4, y + 16, "EACH NEEDLE: STRAY CAPACITANCE, TIP IN THE WEAKEST SALINE, AND WHAT ITS OWN MODEL LEAVES",
                    SB12, th::textFaint);
            // ITDS-2.16.0: upright, two lines a needle: Leak, Drift, Own model and the verdict under the rest
            const int qxW[9] = { 6, 70, 160, 260, 380, 470, 560, 650, 790 };
            const int qxT[9] = { 6, 70, 160, 260, 400, 70, 160, 260, 400 };
            const int* qx = tall ? qxT : qxW;
            auto qdy = [&](int k) { return tall && k >= 5 ? 18 : 0; };
            const int lineH = tall ? 40 : 22;
            const char* qh[9] = { "Needle", "Cp pF", "R \xCE\xA9", "Surface \xCE\xA9", "Ratio", "Leak", "Drift", "Own model", "" };
            for (int k = 0; k < 9; ++k) c->text(vp.x + qx[k], y + 38 + qdy(k), qh[k], SB12, th::textDim);
            if (tall) y += 18;
            {
                const std::string ref[9] = {
                    "Ref.", num1(res.eRef.ok ? a.lotResult(a.lotReferenceIdx()).cpMeanF * 1e12 : birx::kNaN),
                    num1(res.eRef.rOhm, "%.0f"),
                    num1(res.eRef.ze1k(), "%.0f") + "  (" + num1(res.eRef.alpha, "%.2f") + ")", "1.000", "", "", "", "" };
                for (int k = 0; k < 9; ++k) c->text(vp.x + qx[k], y + 61 + qdy(k), ref[k], M12, th::textDim);
            }
            y += 68 + (tall ? 8 : 0);
            for (const birx::LotNeedleQc& q : res.needleQc) {
                const std::string cells[9] = {
                    std::to_string(q.needle), num1(q.cpF * 1e12), num1(q.rWeak, "%.0f"),
                    num1(q.e.ze1k(), "%.0f") + "  (" + num1(q.e.alpha, "%.2f") + ")", num1(q.cellRatio, "%.3f"),
                    num1(q.leakRatio, "%.2f"),
                    std::isfinite(q.drift) ? num1(q.drift * 100) + " %" : std::string("\xE2\x80\x94"),
                    std::isfinite(q.ownIps) ? num1(q.ownIps, "%.2f") + " / " + num1(q.ownZ * 100) + " %" : std::string("\xE2\x80\x94"),
                    q.good() ? "\xE2\x9C\x93 good" : "\xE2\x9C\x95 excluded" };
                const Color col = q.good() ? th::text : th::bad;
                for (int k = 0; k < 9; ++k) c->text(vp.x + qx[k], y + 15 + qdy(k), cells[k], M12, col);
                y += lineH;
            }
            c->textBox(Rect(vp.x + 4, y, W - 8, 40),
                       "R: the solution's resistance at 25 \xC2\xB0""C (the tip's size). Surface: the tip's interface at 1 kHz "
                       "(its exponent). Ratio: reference R / this R. Leak: 1.00 is the reference's shape across the two weak "
                       "salines. Own model: IPS / Z left after the needle's own transfer.", S12, th::textFaint, gfx::Left, 3, 1.2f);
            y += 44;
        }
        for (const std::string& f : res.findings) {
            const int h = gfx::Canvas::wrapHeight(W - 30, f, S14, 6, 1.25f);
            c->text(vp.x + 6, y + 15, "\xC2\xB7", SB16, th::textDim);
            c->textBox(Rect(vp.x + 22, y, W - 30, h), f, S14, th::textDim, gfx::Left, 6, 1.25f);
            y += h + 8;
        }
        if (res.verdict == birx::LotVerdict2::RangesMustChange) {
            y += 8;
            c->text(vp.x + 4, y + 16, "THE RULE RANGES THIS LOT WOULD NEED (reference \xE2\x86\x92 this lot)", SB12, th::warn);
            y += 26;
            const auto eq = a.lotEquivalentRanges(idx);
            for (const auto& b : eq) {
                auto z = [](double v) { return v >= 1e5 ? std::string("max") : num1(v, "%.0f"); };
                auto ip = [](double v) { return v <= -100 ? std::string("min") : v >= 100 ? std::string("max") : num1(v, "%+.1f"); };
                const std::string t = b.label + ":  Z " + z(b.zMin) + "\xE2\x80\x93" + z(b.zMax) + " \xE2\x86\x92 " +
                                      z(b.lzMin) + "\xE2\x80\x93" + z(b.lzMax) + " \xCE\xA9,   IPS " + ip(b.ipsMin) + "\xE2\x80\xA6" +
                                      ip(b.ipsMax) + " \xE2\x86\x92 " + ip(b.lipsMin) + "\xE2\x80\xA6" + ip(b.lipsMax);
                c->text(vp.x + 10, y + 16, t, M12, th::text);
                y += 22;
            }
            y += 6;
            y += c->textBox(Rect(vp.x + 4, y, W - 8, 80),
                "These are the reference's boxes seen through this lot's measured differences, for the record and for "
                "a decision. They are not applied: a lot like this needs its own rule table, confirmed with readings.",
                S13, th::textDim, gfx::Left, 0, 1.25f) + 8;
        }
        contentH = y + off - vp.y;
        c->resetClip();
        if (lr.control) {
            // H7-2.4.0: a control run never goes into the lot file: it sets the repeatability floor
            const bool can = lr.complete() && std::isfinite(res.ctrlIps);
            u->why(ID_LS_FLOOR, "Finish this lot's tests first (every needle, every repeat), with the control phantom's IPS.");
            if (u->button(ID_LS_FLOOR, tall ? Rect(area.x, area.b() - 54, area.w, 54) : Rect(area.r() - 400, area.b() - 54, 400, 54),
                          "Use as the repeatability floor", true, can)) {
                const std::string m = a.lotUseAsFloor(idx);
                say(m, m.find("Every lot") == std::string::npos);
                m_lotSheet = -1;
                return;
            }
            c->textBox(tall ? Rect(area.x, area.b() - 54 - 62, area.w, 56) : Rect(area.x, area.b() - 54, area.w - 420, 54),
                       "A control run is the reference needle itself: it never goes into the lot file. Its differences from "
                       "the reference become the floor of every tolerance.", S13, th::textFaint, gfx::Left, 3, 1.2f);
            return;
        }
        // approve
        const bool canApprove = lr.reference ? lr.complete()
            : (res.verdict == birx::LotVerdict2::SameAsReference || res.verdict == birx::LotVerdict2::UsableWithCalibration ||
               res.verdict == birx::LotVerdict2::UsableWithPerNeedle);
        const bool inFile = a.lotFile().find(lr.lotId) != nullptr;
        const Rect ab = tall ? Rect(area.x, area.b() - 54, area.w, 54) : Rect(area.r() - 360, area.b() - 54, 360, 54);
        u->why(ID_LS_APPROVE, lr.reference ? std::string("Finish the reference needle's tests first (every repeat).") : std::string("This lot's result is not usable (see its verdict above): it cannot go in the lot file."));
        if (u->button(ID_LS_APPROVE, ab, inFile ? "Put in the lot file again" : "Put in the lot file", true, canApprove)) {
            const std::string m = a.lotApprove(idx);
            say(m, m.find("is in the lot") == std::string::npos);
            m_lotSheet = -1;
            return;
        }
        if (!canApprove)
            c->textBox(tall ? Rect(area.x, area.b() - 54 - 62, area.w, 56) : Rect(area.x, area.b() - 54, area.w - 380, 54),
                       lr.complete() || lr.reference ? "Only a lot that is the same as the reference or usable with "
                       "calibration can go into the file." : "The result is final once every planned sweep is done.",
                       S13, th::textFaint, gfx::Left, 3, 1.2f);
        return;
    }

    // ---------------- the tests: the grid on the left, the next sweep on the right
    const int gw = tall ? area.w : 520;
    const int gridHt = tall ? std::min(28 + lr.plan.needles * 42 + 4, (area.h - 60) * 2 / 5) : area.h - 60;
    const Rect grid(area.x, area.y, gw, gridHt);
    const int cols = lr.plan.phantoms + 1;
    const int cw = (gw - 70) / cols;
    // header
    c->text(grid.x + 4, grid.y + 18, "NEEDLE", SB11, th::textFaint);
    for (int s = 0; s < cols; ++s)
        c->label(Rect(grid.x + 70 + s * cw, grid.y, cw, 26), s == 0 ? std::string("Air")
                 : std::string(birx::lotStepName(s)).substr(7), SB12, th::textDim, gfx::Center);
    const Rect gvp(grid.x, grid.y + 28, gw, grid.h - 28);
    static int gridH = 400;
    const int goff = u->scroll(ID_LS_GSCROLL, gvp, gridH);
    c->setClip(gvp);
    // the selection: what was tapped, else the next the plan needs
    int selN = 1, selS = 0;
    const bool haveNext = lr.next(&selN, &selS);
    if (m_lotSel >= 0) { selN = m_lotSel / 10; selS = m_lotSel % 10; }
    int y = gvp.y - goff;
    for (int n = 1; n <= lr.plan.needles; ++n) {
        c->label(Rect(grid.x, y, 64, 40), std::to_string(n), SB16, th::text, gfx::Center);
        for (int s = 0; s < cols; ++s) {
            const Rect cell(grid.x + 70 + s * cw + 3, y + 2, cw - 6, 36);
            const int ok = lr.okCount(n, s), bad = lr.failCount(n, s);
            const bool done = ok >= lr.plan.repeats;
            // H7-2.5.0: a needle refused too often is set aside; the plan goes on without it
            if (lr.needleGivenUp(n) && !done) {
                if ((u->why(ID_LS_GRID + n * 10 + s, "Wait: a lot test sweep is running. Stop it, or let it finish."), u->clicked(ID_LS_GRID + n * 10 + s, cell, !testing))) m_lotSel = n * 10 + s;
                u->card(cell, th::badSoft, m_lotSel == n * 10 + s ? th::accent : th::bad);
                c->label(cell, bad >= birx::LotRun::kGiveUp ? "set aside" : "\xE2\x80\x94", SB12, th::bad, gfx::Center);
                continue;
            }
            const bool sel = n == selN && s == selS && (haveNext || m_lotSel >= 0);
            if ((u->why(ID_LS_GRID + n * 10 + s, "Wait: a lot test sweep is running. Stop it, or let it finish."), u->clicked(ID_LS_GRID + n * 10 + s, cell, !testing))) m_lotSel = n * 10 + s;
            u->card(cell, done ? th::goodSoft : sel ? th::accentSoft : th::surface, sel ? th::accent : done ? th::good : th::stroke);
            char b[32];
            if (bad) std::snprintf(b, sizeof b, "%d/%d  %d\xE2\x9C\x95", std::min(ok, lr.plan.repeats), lr.plan.repeats, bad);
            else std::snprintf(b, sizeof b, "%d/%d", std::min(ok, lr.plan.repeats), lr.plan.repeats);
            c->label(cell, b, SB14, done ? th::good : bad ? th::warn : th::textDim, gfx::Center);
        }
        y += 42;
    }
    gridH = y + goff - gvp.y;
    c->resetClip();

    // the right-hand panel
    const Rect pr = tall ? Rect(area.x, grid.b() + 10, area.w, area.b() - 60 - (grid.b() + 10))
                         : Rect(area.x + gw + 14, area.y, area.w - gw - 14, area.h - 60);
    u->card(pr, th::surface, th::stroke, th::radius);
    int py = pr.y + 14;
    const int px = pr.x + 16, pw = pr.w - 32;
    {
        char b[120];
        const int rep = lr.okCount(selN, selS) + 1;
        if (!haveNext && m_lotSel < 0) std::snprintf(b, sizeof b, "Every planned sweep is done");
        else if (rep > lr.plan.repeats)
            std::snprintf(b, sizeof b, "Needle %d \xC2\xB7 %s \xC2\xB7 complete", selN, birx::lotStepName(selS));
        else std::snprintf(b, sizeof b, "Needle %d \xC2\xB7 %s \xC2\xB7 sweep %d of %d", selN, birx::lotStepName(selS), rep,
                           lr.plan.repeats);
        c->text(px, py + 22, b, SB19, th::text);
        py += 34;
    }
    const Rect sb(px, pr.b() - 16 - 56, pw, 56);
    const bool solution = (haveNext || m_lotSel >= 0) && selS > 0;
    // H7-2.4.1: the sweeps this cell already has (one can be taken out of the evaluation)
    std::vector<int> taken;
    for (int i = 0; i < int(lr.tests.size()); ++i) {
        const birx::LotTest& t = lr.tests[size_t(i)];
        if (t.needle == selN && t.step == selS && !t.removed) taken.push_back(i);
    }
    // what the last sweep came to, or why it cannot be started: measured first, for the room it takes
    std::string msg;
    bool good = true;
    const std::string block = a.lotTestBlocker();
    if (!testing || a.lotTestIdx() != idx) {
        msg = a.lotMessage();
        good = a.lotMessageOk();
        if (!m_lotLocalMsg.empty() && now - m_lotLocalAt < 8000) { msg = m_lotLocalMsg; good = false; }
        if (msg.empty() && !block.empty()) { msg = block; good = false; }
    }
    const int msgH = msg.empty() ? 0 : std::min(gfx::Canvas::wrapHeight(pw, msg, S13, 6, 1.22f), 102) + 8;
    const int condH = solution ? 70 : 0;
    const int room = sb.y - 10 - msgH - condH - py;
    if (taken.empty()) {
        if (haveNext || m_lotSel >= 0)
            py += c->textBox(Rect(px, py, pw, room), birx::lotStepHowTo(selS), S13, th::textDim, gfx::Left,
                             room >= 136 ? 8 : std::max(2, room / 17), 1.28f) + 10;
    } else {
        // the newest that fit, one row each
        const int n = int(taken.size());
        int fit = room / 28;
        if (n * 28 > room) --fit;
        int first = fit < 1 ? n - 1 : n - fit;
        if (first < 1) first = 0;
        else {
            c->text(px, py + 13, std::to_string(first) + " earlier sweep(s) above: see Raw", S12, th::textFaint);
            py += 18;
        }
        for (int k = first; k < n; ++k) {
            const int i = taken[size_t(k)];
            const birx::LotTest& t = lr.tests[size_t(i)];
            int num = 1;
            for (int j = 0; j < i; ++j)
                if (lr.tests[size_t(j)].needle == t.needle && lr.tests[size_t(j)].step == t.step) ++num;
            std::string line = "#" + std::to_string(num) + "  ";
            if (t.step == 0) line += "Cp " + num1(t.cpF * 1e12) + " pF";
            else line += "Z " + num1(t.zNorm, "%.0f") + " \xCE\xA9  IPS " + num1(t.ips, "%+.2f");
            if (t.soakS >= 0 && t.step > 0) line += "  \xC2\xB7 " + std::to_string(t.soakS) + " s";
            c->text(px, py + 18, t.ok ? "\xE2\x9C\x93" : "\xE2\x9C\x95", SB14, t.ok ? th::good : th::bad);
            c->text(px + 22, py + 18, line, M12, t.ok ? th::good : th::bad);
            // Remove, twice within 5 s
            const bool armed = m_lotRmArm == i + 1 && now - m_lotRmAt < 5000;
            ui::Ui::Btn st;
            st.primary = armed;
            st.danger = true;
            st.enabled = !testing;
            st.font = &SB12;
            const char* rl = armed ? "Sure?" : "Remove";
            u->why(ID_LS_RM + i % 90, "Wait: a lot test sweep is running. Stop it, or let it finish.");
            if (u->button(ID_LS_RM + i % 90, Rect(px + pw - 110, py, 110, 24), rl, st)) {
                if (!armed) { m_lotRmArm = i + 1; m_lotRmAt = now; m_timerAt = now + 5000; }
                else {
                    m_lotRmArm = 0;
                    const std::string w = a.lotSetRemoved(idx, i, true);
                    if (w.empty()) a.clearLotMessage();
                    else { m_lotLocalMsg = w; m_lotLocalAt = now; }
                }
            }
            py += 28;
        }
        py += 6;
    }
    if (solution) {
        const int bw = (pw - 10) / 2;
        const std::string tv = std::isfinite(m_lotTempC) ? num1(m_lotTempC) + " \xC2\xB0""C" : std::string("tap to enter");
        const std::string cv = std::isfinite(m_lotCond) ? num1(m_lotCond, "%.2f") + " mS/cm" : std::string("optional");
        c->text(px, py + 12, "Temperature (in the solution)", S12, th::textFaint);
        c->text(px + bw + 10, py + 12, "Conductivity (25 \xC2\xB0""C)", S12, th::textFaint);
        u->why(ID_LS_TEMP, "Wait: a lot test sweep is running. Stop it, or let it finish."); u->why(ID_LS_COND, "Wait: a lot test sweep is running. Stop it, or let it finish.");
        if (u->button(ID_LS_TEMP, Rect(px, py + 18, bw, 44), tv, false, !testing))
            openNumber("Solution temperature", std::isfinite(m_lotTempC) ? num1(m_lotTempC) : "", "\xC2\xB0""C", 5, 45, true,
                       false, true, [this](const std::string& v) { m_lotTempC = std::atof(v.c_str()); });
        if (u->button(ID_LS_COND, Rect(px + bw + 10, py + 18, bw, 44), cv, false, !testing))
            openNumber("Conductivity at 25 \xC2\xB0""C (blank: none)", std::isfinite(m_lotCond) ? num1(m_lotCond, "%.2f") : "",
                       "mS/cm", 0, 200, true, false, true, [this](const std::string& v) {
                           m_lotCond = v.empty() ? birx::kNaN : std::atof(v.c_str());
                           if (!(m_lotCond > 0)) m_lotCond = birx::kNaN;
                       });
        py += condH;
    }
    if (!msg.empty()) {
        const int h = std::max(17, std::min(msgH - 8, sb.y - 8 - py));
        c->textBox(Rect(px, py, pw, h), msg, S13, good ? th::good : th::bad, gfx::Left, h < 90 ? h / 15 : 6, 1.22f);
    }
    // start / stop
    if (testing && a.lotTestIdx() == idx) {
        char b[96];
        if (a.lotStage() == app::LotStage::Settling)
            std::snprintf(b, sizeof b, "Settling\xE2\x80\xA6 %d s \xE2\x80\x94 keep the tip still", a.lotSecondsLeft());
        else std::snprintf(b, sizeof b, "Measuring\xE2\x80\xA6 %d s", a.lotSecondsLeft());
        c->text(px, sb.y - 12, b, SB16, th::accent);
        if (u->button(ID_LS_STOP, sb, "Stop", false, true)) a.lotStopTest("Stopped. Nothing was kept from that sweep.");
        m_timerAt = now + 500;
    } else {
        const bool can = block.empty() && (haveNext || m_lotSel >= 0);
        u->why(ID_LS_START, !block.empty() ? block : std::string("Choose a needle and repeat in the grid first (or all are done)."));
        if (u->button(ID_LS_START, sb, "Start sweep", true, can)) {
            const std::string w = a.lotStartTest(idx, selN, selS, m_lotTempC, m_lotCond);
            if (!w.empty()) { m_lotLocalMsg = w; m_lotLocalAt = now; }
            else { m_lotLocalMsg.clear(); m_lotSel = -1; }
        }
    }

    // bottom: delete (twice)
    const bool armed = m_lotDelArm && now - m_lotDelArm < 5000;
    if (m_lotDelArm && !armed) m_lotDelArm = 0;
    if (armed) m_timerAt = m_lotDelArm + 5000;
    ui::Ui::Btn st;
    st.primary = armed;
    st.danger = true;
    st.enabled = !testing;
    u->why(ID_LS_DEL, "Wait: a lot test sweep is running. Stop it, or let it finish.");
    if (u->button(ID_LS_DEL, Rect(area.x, area.b() - 50, 260, 50), armed ? "Tap again to delete" : "Delete this lot", st)) {
        if (!armed) m_lotDelArm = now | 1;
        else {
            m_lotDelArm = 0;
            const std::string id = lr.lotId;
            const std::string w = a.lotRemove(idx);
            say(w.empty() ? "Deleted: " + id + " and its sweeps. The lot calibration file is unchanged." : w, !w.empty());
            m_lotSheet = -1;
            return;
        }
    }
    c->textBox(Rect(area.x + 280, area.b() - 50, area.w - 280, 50),
               "Tap a cell to choose a sweep; otherwise the next one the plan needs is chosen. Air comes first for each "
               "needle, with the needle dry.", S12, th::textFaint, gfx::Left, 3, 1.2f);
}

// ---------------------------------------------------------------------------
// COMPARE (H7-2.2.0): this lot against the reference, strength by strength —
// the phase curves (reference, the lot as measured, the lot corrected), their
// Z and IPS, and in words what the device does to a reading of this lot.
namespace {
// H7-2.4.5: a curve's differences from the reference's, at the frequencies both have
void diffsOf(const birx::LotCurve& a, const birx::LotCurve& r, std::vector<double>* f, std::vector<double>* dPh,
             std::vector<double>* dZ)
{
    for (size_t i = 0; i < a.freqHz.size(); ++i)
        for (size_t j = 0; j < r.freqHz.size(); ++j)
            if (std::fabs(a.freqHz[i] / r.freqHz[j] - 1) < 0.02) {
                f->push_back(a.freqHz[i]);
                dPh->push_back(a.phaseDeg[i] - r.phaseDeg[j]);
                dZ->push_back((a.magOhm[i] / r.magOhm[j] - 1) * 100);
                break;
            }
}
} // namespace

void Gui::lotCompareView(int idx, const Rect& area)
{
    const birx::LotRun& lr = a.lotRuns()[size_t(idx)];
    const birx::LotCompare cmp = a.lotCompare(idx);
    const birx::LotResult res = a.lotResult(idx);
    if (!cmp.ok) {
        c->textBox(Rect(area.x + 20, area.y + 40, area.w - 40, 200),
                   a.lotReferenceIdx() < 0 ? "There is no reference yet. Measure the reference needle first: everything here "
                                             "is a comparison with it."
                                           : "Nothing to compare yet: this lot and the reference both need saline sweeps. The "
                                             "comparison appears as soon as each has some, and is final when both are complete.",
                   S16, th::textDim, gfx::Left, 0, 1.3f);
        return;
    }
    const int ns = int(cmp.steps.size());
    if (m_lotCmpStep < 1 || m_lotCmpStep > ns) m_lotCmpStep = 1;
    const birx::LotCompareStep& st = cmp.steps[size_t(m_lotCmpStep - 1)];
    const Color cRef = th::healthy, cLot = th::warn, cFix = th::accent;

    // strength chips
    int x = area.x;
    c->text(x, area.y + 22, "STRENGTH", SB11, th::textFaint);
    x += 72;
    for (int k = 1; k <= ns; ++k) {
        const std::string t = std::string(birx::lotStepName(k)).substr(7);
        const int w = gfx::Canvas::measure(t, SB14) + 28;
        if (u->chip(ID_LC_STEP + k, Rect(x, area.y, w, 34), t, m_lotCmpStep == k)) m_lotCmpStep = k;
        x += w + 6;
    }
    // H7-2.4.5: the curves, or their differences from the reference
    if (u->chip(ID_LC_CURVES, Rect(x + 10, area.y, 86, 34), "Curves", !m_lotCmpDiff)) m_lotCmpDiff = false;
    if (u->chip(ID_LC_DIFF, Rect(x + 102, area.y, 106, 34), "Difference", m_lotCmpDiff)) m_lotCmpDiff = true;
    if (!lr.complete()) c->text(x + 222, area.y + 22, "provisional", S12, th::warn);

    const bool tall = portrait();      // ITDS-2.16.0: the panel under the chart and the table, not beside them
    const Rect ch(area.x, area.y + 44, tall ? area.w : 560, tall ? 260 : 300);
    u->card(ch, th::surface, th::stroke);
    if (!m_lotCmpDiff) {
        // ---- the phase chart
        const int pl = 44, prr = 10, pt = 26, pb = 24;
        const Rect plot(ch.x + pl, ch.y + pt, ch.w - pl - prr, ch.h - pt - pb);
        double lo = 1e9, hi = -1e9, f0 = 1e12, f1 = 0;
        for (const birx::LotCurve* cv : { &st.ref, &st.lot, &st.lotT })
            for (size_t i = 0; i < cv->freqHz.size(); ++i) {
                lo = std::min(lo, cv->phaseDeg[i]); hi = std::max(hi, cv->phaseDeg[i]);
                f0 = std::min(f0, cv->freqHz[i]); f1 = std::max(f1, cv->freqHz[i]);
            }
        if (!(hi > lo)) { lo -= 1; hi += 1; }
        if (hi - lo < 4) { const double m = (hi + lo) / 2; lo = m - 2; hi = m + 2; }
        const double pad = (hi - lo) * 0.08; lo -= pad; hi += pad;
        if (!(f1 > f0)) { f0 = 500; f1 = 500000; }
        const double lx0 = std::log10(f0), lx1 = std::log10(f1);
        auto X = [&](double f) { return float(plot.x + (std::log10(f) - lx0) / (lx1 - lx0) * plot.w); };
        auto Y = [&](double p) { return float(plot.y + (1 - (p - lo) / (hi - lo)) * plot.h); };
        c->setClip(ch);
        for (double d : { 1e3, 1e4, 1e5 }) {
            if (d < f0 || d > f1) continue;
            c->fill(Rect(int(X(d)), plot.y, 1, plot.h), gfx::Color(255, 255, 255), 18);
            c->text(int(X(d)) - 10, plot.b() + 16, d >= 1e5 ? "100k" : d >= 1e4 ? "10k" : "1k", M9, th::textFaint);
        }
        {
            const double step = (hi - lo) > 20 ? 10 : (hi - lo) > 8 ? 5 : (hi - lo) > 4 ? 2 : 1;
            for (double v = std::ceil(lo / step) * step; v <= hi; v += step) {
                c->fill(Rect(plot.x, int(Y(v)), plot.w, 1), gfx::Color(255, 255, 255), 18);
                char b[16]; std::snprintf(b, sizeof b, "%g", v);
                c->text(plot.x - 6 - gfx::Canvas::measure(b, M9), int(Y(v)) + 3, b, M9, th::textFaint);
            }
        }
        // the band the IPS is read in (100 kHz .. 500 kHz)
        if (f1 >= 4e5) {
            const float bx0 = X(1e5), bx1 = X(std::min(f1, 5e5));
            c->fill(Rect(int(bx0), plot.y, int(bx1 - bx0), plot.h), th::accent, 18);
            c->text(int(bx0) + 4, plot.y + 12, "IPS band", S10, th::textFaint);
        }
        auto curve = [&](const birx::LotCurve& cv, Color col, float wdt, bool dash) {
            for (size_t i = 1; i < cv.freqHz.size(); ++i) {
                const float xa = X(cv.freqHz[i - 1]), ya = Y(cv.phaseDeg[i - 1]), xb = X(cv.freqHz[i]), yb = Y(cv.phaseDeg[i]);
                if (dash) c->dashed(xa, ya, xb, yb, 5, 4, wdt, col);
                else c->line(xa, ya, xb, yb, wdt, col);
            }
            for (size_t i = 0; i < cv.freqHz.size(); ++i) c->circle(X(cv.freqHz[i]), Y(cv.phaseDeg[i]), wdt + 0.8f, col);
        };
        curve(st.lot, cLot, 1.8f, true);
        curve(st.ref, cRef, 2.6f, false);
        curve(st.lotT, cFix, 2.0f, false);
        c->text(ch.x + 8, ch.y + 16, "phase \xC2\xB0", M9, th::textFaint);
        c->text(ch.r() - 10 - gfx::Canvas::measure("f Hz \xE2\x86\x92", M9), ch.y + 16, "f Hz \xE2\x86\x92", M9, th::textFaint);
        {
            int kx = ch.x + 80;
            const int ky = ch.y + 12;
            auto key = [&](Color col, const char* t, bool dash) {
                if (dash) c->dashed(float(kx), float(ky), float(kx + 18), float(ky), 4, 3, 2, col);
                else c->line(float(kx), float(ky), float(kx + 18), float(ky), 2.4f, col);
                kx += 22 + c->text(kx + 22, ky + 4, t, S10, col) + 14;
            };
            key(cRef, "reference", false);
            key(cLot, "this lot, as measured", true);
            key(cFix, cmp.excluded ? "corrected (own model)" : "this lot, corrected", false);
        }
        c->resetClip();
    } else {
        // ---- H7-2.4.5: the differences from the reference, phase above and |Z| below
        std::vector<double> fL, dPhL, dZL, fT, dPhT, dZT;
        diffsOf(st.lot, st.ref, &fL, &dPhL, &dZL);
        diffsOf(st.lotT, st.ref, &fT, &dPhT, &dZT);
        double mPh = 1, mZ = 3;
        for (double v : dPhL) mPh = std::max(mPh, std::fabs(v));
        for (double v : dPhT) mPh = std::max(mPh, std::fabs(v));
        for (double v : dZL) mZ = std::max(mZ, std::fabs(v));
        for (double v : dZT) mZ = std::max(mZ, std::fabs(v));
        mPh *= 1.15;
        mZ *= 1.15;
        const Rect top(ch.x + 52, ch.y + 26, ch.w - 62, 118), bot(ch.x + 52, ch.y + 168, ch.w - 62, 104);
        const double lx0 = std::log10(400.0), lx1 = std::log10(6e5);
        auto X = [&](const Rect& p, double f) { return float(p.x + (std::log10(f) - lx0) / (lx1 - lx0) * p.w); };
        auto half = [&](const Rect& p, double m, double band, const char* unit, const std::vector<double>& fa,
                        const std::vector<double>& va, const std::vector<double>& fb, const std::vector<double>& vb) {
            auto Y = [&](double v) { return float(p.y + (1 - (v + m) / (2 * m)) * p.h); };
            c->fill(Rect(p.x, int(Y(band)), p.w, int(Y(-band) - Y(band))), th::good, 30);
            c->fill(Rect(p.x, int(Y(0)), p.w, 1), gfx::Color(255, 255, 255), 60);
            for (double d : { 1e3, 1e4, 1e5 }) c->fill(Rect(int(X(p, d)), p.y, 1, p.h), gfx::Color(255, 255, 255), 18);
            char b[24];
            std::snprintf(b, sizeof b, "%+.1f%s", m, unit);
            c->text(ch.x + 6, p.y + 9, b, M9, th::textFaint);
            std::snprintf(b, sizeof b, "%+.1f%s", -m, unit);
            c->text(ch.x + 6, p.b(), b, M9, th::textFaint);
            auto plotOf = [&](const std::vector<double>& f, const std::vector<double>& v, Color col, bool dash) {
                for (size_t i = 1; i < f.size(); ++i) {
                    const float xa = X(p, f[i - 1]), ya = Y(v[i - 1]), xb = X(p, f[i]), yb = Y(v[i]);
                    if (dash) c->dashed(xa, ya, xb, yb, 5, 4, 1.8f, col);
                    else c->line(xa, ya, xb, yb, 2, col);
                }
                for (size_t i = 0; i < f.size(); ++i) c->circle(X(p, f[i]), Y(v[i]), 2.4f, col);
            };
            plotOf(fa, va, cLot, true);
            plotOf(fb, vb, cFix, false);
        };
        c->setClip(ch);
        c->text(ch.x + 8, ch.y + 16, "phase, lot \xE2\x88\x92 reference (\xC2\xB0)  \xC2\xB7  green: \xC2\xB1" "0.5\xC2\xB0", M9, th::textFaint);
        half(top, mPh, 0.5, "\xC2\xB0", fL, dPhL, fT, dPhT);
        c->text(ch.x + 8, bot.y - 6, "|Z|, lot / reference \xE2\x88\x92 1 (%)  \xC2\xB7  green: \xC2\xB1" "3 %", M9, th::textFaint);
        half(bot, mZ, 3, "%", fL, dZL, fT, dZT);
        for (double d : { 1e3, 1e4, 1e5 })
            c->text(int(X(bot, d)) - 10, bot.b() + 14, d >= 1e5 ? "100k" : d >= 1e4 ? "10k" : "1k", M9, th::textFaint);
        // the worst of each where the verdict looks: phase from 50 to 500 kHz, |Z| below 5 kHz
        auto worst = [](const std::vector<double>& f, const std::vector<double>& v, double f0, double f1) {
            double w = 0;
            for (size_t i = 0; i < f.size(); ++i)
                if (f[i] >= f0 && f[i] <= f1) w = std::max(w, std::fabs(v[i]));
            return w;
        };
        char b[160];
        std::snprintf(b, sizeof b, "50-500 kHz: phase %.2f\xC2\xB0 (corrected %.2f\xC2\xB0)   below 5 kHz: |Z| %.1f %% (corrected %.1f %%)",
                      worst(fL, dPhL, 49000, 600000), worst(fT, dPhT, 49000, 600000), worst(fL, dZL, 0, 5100),
                      worst(fT, dZT, 0, 5100));
        c->text(ch.x + 8, ch.b() - 6, b, M9, th::text);
        c->resetClip();
    }

    // ---- the numbers, strength by strength
    int ty = ch.b() + 10;
    const int cx0 = area.x, colW[5] = { 112, 112, 112, 112, 112 };
    const char* heads[5] = { "", "Reference", "Lot, measured", "Lot, corrected", "Left over" };
    { int xx = cx0; for (int k = 0; k < 5; ++k) { c->text(xx + 4, ty + 12, heads[k], SB11, k == 1 ? cRef : k == 2 ? cLot : k == 3 ? cFix : th::textFaint); xx += colW[k]; } }
    ty += 18;
    for (int k = 0; k < ns; ++k) {
        const birx::LotCompareStep& q = cmp.steps[size_t(k)];
        const bool cur = k + 1 == m_lotCmpStep;
        if (cur) c->fill(Rect(cx0, ty, std::min(560, area.w), 40), th::accentSoft, 120);
        int xx = cx0;
        c->text(xx + 4, ty + 16, std::string(birx::lotStepName(k + 1)).substr(7), SB12, th::text);
        xx += colW[0];
        auto cell = [&](double z, double ips, Color col) {
            char b[40];
            std::snprintf(b, sizeof b, "%s \xCE\xA9", num1(z, "%.0f").c_str());
            c->text(xx + 4, ty + 16, b, M12, col);
            std::snprintf(b, sizeof b, "IPS %s", num1(ips, "%+.2f").c_str());
            c->text(xx + 4, ty + 33, b, M12, col);
            xx += 112;
        };
        cell(q.zRef, q.ipsRef, cRef);
        cell(q.zLot, q.ipsLot, cLot);
        cell(q.zLotT, q.ipsLotT, cFix);
        char b[48];
        std::snprintf(b, sizeof b, "Z %s %%", num1(std::isfinite(q.zLotT / q.zRef) ? (std::fabs(q.zLotT / q.zRef - 1) < 0.0005 ? 0.0 : (q.zLotT / q.zRef - 1) * 100) : birx::kNaN, "%+.1f").c_str());
        c->text(xx + 4, ty + 16, b, M12, th::textDim);
        std::snprintf(b, sizeof b, "IPS %s", num1(q.ipsLotT - q.ipsRef, "%+.2f").c_str());
        c->text(xx + 4, ty + 33, b, M12, th::textDim);
        ty += 42;
    }

    // ---- what the device does with this lot
    const Rect pr = tall ? Rect(area.x, ty + 6, area.w, area.b() - (ty + 6))
                         : Rect(area.x + 574, area.y + 44, area.w - 574, area.h - 44);
    u->card(pr, th::surface, th::stroke, th::radius);
    static int contentH = 600;
    const Rect vp = pr.inset(14, 10);
    const int off = u->scroll(ID_LC_SCROLL, vp, contentH);
    c->setClip(vp);
    int y = vp.y - off;
    const int W = vp.w;
    c->text(vp.x, y + 16, "WHAT A READING OF LOT " + gfx::Canvas::elide(lr.lotId, SB12, 200) + " GETS", SB12, th::textFaint);
    y += 26;
    const birx::LotTransfer& t = cmp.transfer;
    const bool inFile = a.lotFile().find(lr.lotId) != nullptr;
    char b[640];
    auto para = [&](const char* head, const std::string& body, Color hc) {
        c->text(vp.x, y + 14, head, SB14, hc);
        y += 20;
        y += c->textBox(Rect(vp.x, y, W, 200), body, S13, th::textDim, gfx::Left, 0, 1.28f) + 10;
    };
    if (cmp.model && res.goodNeedles == 0) {
        para("No needle has passed its checks yet",
             "So there is no correction of the lot to show. The corrected curve is each needle's OWN model (its stray "
             "capacitance, tip surface and size put to the reference's), so that what is left can be judged: see "
             "Result for why each needle was excluded, and Difference above for where its curve departs from the "
             "reference's.", th::warn);
    } else if (cmp.model) {
        // H7-2.3.0: the three-part model
        const bool per = res.mode == 1;
        std::snprintf(b, sizeof b, "%s. After the correction every good needle is within %.2f IPS and %.1f %% Z of the reference "
                      "in the two weakest salines.%s", birx::lotVerdictName(res.verdict),
                      per ? res.needleResidIps : res.lotResidIps, (per ? res.needleResidZ : res.lotResidZ) * 100,
                      inFile ? " It is in the lot file: devices with that file apply all of the above when this lot is typed at intake."
                             : " It is NOT in the lot file yet, so no device applies it (Result \xE2\x86\x92 Put in the lot file).");
        para("Verdict", b, inFile ? th::good : th::warn);
        std::snprintf(b, sizeof b, "This needle's stray capacitance is taken off every point: its own, from its Air check%s. "
                      "The lot's good needles: %.1f pF, spread %.1f pF.", per ? ", which every needle needs" : ", else the lot's mean",
                      res.lotCpF * 1e12, cmp.cpSdF * 1e12);
        para("1  Stray capacitance removed", b, th::text);
        if (per)
            std::snprintf(b, sizeof b, "Each needle's own, from its Saline check before the case (the tip 10 s in %s): its tip "
                          "surface is taken off, and Z is scaled by reference R / its R. The check is refused outside R "
                          "%.0f\xE2\x80\x93%.0f \xCE\xA9 or a surface of %.0f\xE2\x80\x93%.0f \xCE\xA9, or when its two sweeps disagree.",
                          birx::lotStepName(lr.plan.phantoms), res.dipRMin, res.dipRMax, res.dipZeMin, res.dipZeMax);
        else
            std::snprintf(b, sizeof b, "The lot's mean tip surface, %.0f \xCE\xA9 at 1 kHz (exponent %.2f), is taken off and Z "
                          "is scaled by %.3f, the ratio of the reference tip's resistance in saline to this lot's.",
                          res.eLot.ze1k(), res.eLot.alpha, res.kZLot);
        para("2  Tip surface and size", b, th::text);
        std::snprintf(b, sizeof b, "The reference needle's tip surface (%.0f \xCE\xA9 at 1 kHz, exponent %.2f) and stray "
                      "capacitance, %.1f pF, are put back, and the gain range the instrument would then have used is taken.",
                      res.eRef.ze1k(), res.eRef.alpha, t.cpRefF * 1e12);
        para("3  The reference needle's", b, th::text);
    } else {
        std::snprintf(b, sizeof b, "%s. After the correction the lot is within %.2f IPS and %.1f %% Z of the reference at every "
                      "strength.%s", birx::lotVerdictName(res.verdict), res.worstResidIps, res.worstResidZ * 100,
                      inFile ? " It is in the lot file: devices with that file apply all of the above when this lot is typed at intake."
                             : " It is NOT in the lot file yet, so no device applies it (Result \xE2\x86\x92 Put in the lot file).");
        para("Verdict", b, inFile ? th::good : th::warn);
        std::snprintf(b, sizeof b, "This needle's stray capacitance is taken off every point of the sweep: its own, when an Air "
                      "check was done for it, otherwise the lot's mean, %.1f pF (spread %.1f pF between the needles tested).",
                      t.cpNeedleF * 1e12, cmp.cpSdF * 1e12);
        para("1  Stray capacitance removed", b, th::text);
        std::snprintf(b, sizeof b, "Z x %.3f at every frequency: the ratio of the reference tip's cell constant to this lot's, from "
                      "saline (this lot's tip reads %.1f %% %s). This is what moves Z.", t.kZ, std::fabs(1 / t.kZ - 1) * 100,
                      t.kZ < 1 ? "higher" : "lower");
        para("2  Tip geometry", b, th::text);
        std::snprintf(b, sizeof b, "The reference needle's stray capacitance, %.1f pF, is put back, and the gain range the "
                      "instrument would then have used is taken. This is most of what moves IPS.", t.cpRefF * 1e12);
        para("3  The reference's capacitance", b, th::text);
    }
    para("4  Processed as the trial was", t.trialCalibrated
         ? "With the reference needle's own air correction, as the trial's needles were air-calibrated (Settings \xE2\x86\x92 Rules)."
         : "As it came, with no air correction, as the trial's needles were not calibrated in air (Settings \xE2\x86\x92 Rules).", th::text);
    // H7-2.10.0: the QC limits its good needles set, carried in the lot file to every device
    {
        std::string qt;
        if (inFile) {
            const birx::QcLimits q9 = a.qcLimitsFor(lr.lotId, birx::kCheck9);
            qt = "Before it measures, every needle of this lot is checked against the limits its good needles set here, "
                 "carried to every device in the lot file. " + q9.describe() + " (in 0.9 % saline). Outside them the needle is "
                 "refused and gives no reading, from the screen or the pedal.";
        } else {
            birx::LotCorrection tmp; tmp.lotId = lr.lotId;
            birx::lotQcFill(lr, &res, a.lotLimits(), &tmp);
            if (tmp.hasQc()) {
                std::snprintf(b, sizeof b, "When it is put in the lot file, every needle of this lot is checked against the limits "
                              "its good needles set here: an Air check of %.1f to %.1f pF and leakage below %.0f uS, then the saline "
                              "check. Until then a needle of it is checked against the generic limits.",
                              tmp.qcCpMinF * 1e12, tmp.qcCpMaxF * 1e12, tmp.qcGpMaxS * 1e6);
                qt = b;
            } else qt = "No needle of this lot has passed its checks yet, so it sets no QC limits: a needle of it is checked "
                        "against the generic limits.";
        }
        para("5  QC of every needle", qt, inFile ? th::text : th::warn);
    }
    para("Then", "Z and IPS are read from that sweep and the rule table decides as usual: the rule ranges are not "
         "changed. The healthy-tissue samples of a case get the same.", th::text);

    contentH = y + off - vp.y;
    c->resetClip();
}

// ---------------------------------------------------------------------------
// RAW (H7-2.2.1): the numbers every sweep of the lot and of the reference gave,
// as they arrived from the measurement unit (no correction of any kind), to be
// read off the screen on an instrument without a USB stick. Page 1 sets every
// needle of both side by side; then one page per needle, one row per sweep; a
// tap on a row gives all fifteen frequencies with their gain ranges.
namespace {
const birx::Point* rawAt(const std::vector<birx::Point>& pts, double f)
{
    const birx::Point* best = nullptr;
    for (const birx::Point& p : pts)
        if (p.freqHz > 0 && std::fabs(p.freqHz / f - 1) < 0.06 && (!best || std::fabs(p.freqHz - f) < std::fabs(best->freqHz - f))) best = &p;
    return best;
}
std::string zs(const birx::Point* p) { return p ? num1(p->magOhm, "%.0f") : std::string("\xE2\x80\x94"); }
std::string ps(const birx::Point* p) { return p ? num1(p->phaseDeg, "%+.2f") : std::string("\xE2\x80\x94"); }
std::string rs(const birx::Point* p) { return p ? std::to_string(p->range) : std::string("\xE2\x80\x94"); }
} // namespace

void Gui::lotRawView(int idx, const Rect& area)
{
    const auto& runs = a.lotRuns();
    const birx::LotRun& lr = runs[size_t(idx)];
    const int ri = a.lotReferenceIdx();
    const birx::LotRun* ref = (ri >= 0 && ri != idx) ? &runs[size_t(ri)] : nullptr;
    // the pages: 0 = all needles; then the reference's needles; then this lot's
    struct Pg { const birx::LotRun* run; int needle; };
    std::vector<Pg> pages;
    pages.push_back({ nullptr, 0 });
    if (ref) for (int n = 1; n <= ref->plan.needles; ++n) pages.push_back({ ref, n });
    for (int n = 1; n <= lr.plan.needles; ++n) pages.push_back({ &lr, n });
    if (m_lotRawPage < 0 || m_lotRawPage >= int(pages.size())) m_lotRawPage = 0;
    const Pg pg = pages[size_t(m_lotRawPage)];

    // navigation
    {
        const Rect pv(area.x, area.y, 120, 40), nx(area.x + 128, area.y, 120, 40);
        u->why(ID_LR_PREV, "This is the first page."); u->why(ID_LR_NEXT, "This is the last page.");
        if (u->button(ID_LR_PREV, pv, "\xE2\x86\x90 prev", false, m_lotRawPage > 0)) { --m_lotRawPage; m_lotRawRow = -1; }
        if (u->button(ID_LR_NEXT, nx, "next \xE2\x86\x92", false, m_lotRawPage + 1 < int(pages.size()))) { ++m_lotRawPage; m_lotRawRow = -1; }
        char b[160];
        if (!pg.run) std::snprintf(b, sizeof b, "page %d of %d \xC2\xB7 every needle, mean of its sweeps (raw, nothing corrected)",
                                   m_lotRawPage + 1, int(pages.size()));
        else std::snprintf(b, sizeof b, "page %d of %d \xC2\xB7 %s %s, needle %d \xC2\xB7 raw, nothing corrected",
                           m_lotRawPage + 1, int(pages.size()), pg.run->reference ? "REFERENCE" : "LOT",
                           gfx::Canvas::elide(pg.run->lotId, SB14, 260).c_str(), pg.needle);
        // ITDS-2.16.0: upright, the page's name on a line under the two buttons
        if (portrait()) c->text(area.x, area.y + 64, gfx::Canvas::elide(b, SB14, area.w), SB14, pg.run && pg.run->reference ? th::healthy : th::text);
        else c->text(area.x + 264, area.y + 26, b, SB14, pg.run && pg.run->reference ? th::healthy : th::text);
    }
    const int y0 = area.y + 50 + (portrait() ? 30 : 0);

    if (!pg.run) {
        // ---- every needle side by side
        const int nst = std::max(lr.plan.phantoms, ref ? ref->plan.phantoms : 0);
        const int c0 = portrait() ? 70 : 150, cCp = 80;
        const int cw = std::min(130, (area.w - c0 - cCp) / std::max(1, nst * 2));
        int x = area.x + c0;
        c->text(area.x + 4, y0 + 14, "NEEDLE", SB11, th::textFaint);
        c->text(x + 4, y0 + 14, "Cp pF", SB11, th::textFaint);
        x += cCp;
        for (int s = 1; s <= nst; ++s) {
            const std::string nm = std::string(birx::lotStepName(s)).substr(7);
            c->text(x + 4, y0 + 14, ("Z1k " + nm).c_str(), SB11, th::textFaint);
            c->text(x + cw + 4, y0 + 14, ("Z100k " + nm).c_str(), SB11, th::textFaint);
            x += 2 * cw;
        }
        int y = y0 + 22;
        auto rowFor = [&](const birx::LotRun& run, int n) {
            const bool isRef = run.reference;
            c->fill(Rect(area.x, y, area.w, 30), isRef ? th::healthySoft : th::surface, isRef ? 120 : 255);
            char b[64];
            std::snprintf(b, sizeof b, "%s %d", isRef ? "REF" : "lot", n);
            c->text(area.x + 6, y + 20, b, SB14, isRef ? th::healthy : th::text);
            int xx = area.x + c0;
            double cp = birx::kNaN;
            run.needleAir(n, &cp, nullptr);
            c->text(xx + 4, y + 20, num1(cp * 1e12), M12, th::text);
            xx += cCp;
            for (int s = 1; s <= nst; ++s) {
                double z1 = 0, z100 = 0; int k1 = 0, k100 = 0;
                for (const birx::LotTest& t : run.tests) {
                    if (!t.ok || t.needle != n || t.step != s) continue;
                    if (const birx::Point* p = rawAt(t.raw, 1000)) { z1 += p->magOhm; ++k1; }
                    if (const birx::Point* p = rawAt(t.raw, 100000)) { z100 += p->magOhm; ++k100; }
                }
                c->text(xx + 4, y + 20, k1 ? num1(z1 / k1, "%.0f") : std::string("\xE2\x80\x94"), M12, th::text);
                c->text(xx + cw + 4, y + 20, k100 ? num1(z100 / k100, "%.0f") : std::string("\xE2\x80\x94"), M12, th::text);
                xx += 2 * cw;
            }
            y += 32;
        };
        if (ref) for (int n = 1; n <= ref->plan.needles; ++n) rowFor(*ref, n);
        for (int n = 1; n <= lr.plan.needles; ++n) rowFor(lr, n);
        c->textBox(Rect(area.x, y + 10, area.w, 80),
            "|Z| in ohm as the measurement unit sent it, the mean of each needle's accepted sweeps in each solution; Cp is "
            "the fitted stray capacitance of its air sweeps. If two needles differ by the same number of ohms at 1 kHz and at "
            "100 kHz, the difference is a resistance in series (wire, joint, connector); if they differ mostly at 1 kHz, it "
            "is the electrode surface.", S13, th::textDim, gfx::Left, 0, 1.25f);
        return;
    }

    // ---- one needle: every sweep, or one sweep's fifteen points
    std::vector<const birx::LotTest*> rows;
    for (const birx::LotTest& t : pg.run->tests) if (t.needle == pg.needle) rows.push_back(&t);
    if (m_lotRawRow >= 0 && m_lotRawRow < int(rows.size())) {
        const birx::LotTest& t = *rows[size_t(m_lotRawRow)];
        // its number among the needle's sweeps in that solution, as the test sheet counts them
        int k = 1;
        for (const birx::LotTest& q : pg.run->tests) {
            if (&q == &t) break;
            if (q.needle == t.needle && q.step == t.step) ++k;
        }
        char b[200];
        std::snprintf(b, sizeof b, "%s, sweep %d \xC2\xB7 %s \xC2\xB7 %s \xC2\xB7 T %s \xC2\xB0""C%s", birx::lotStepName(t.step),
                      k, t.removed ? "REMOVED by hand" : t.ok ? "accepted" : "REFUSED", t.when.c_str(), num1(t.tempC).c_str(),
                      std::isfinite(t.condMsCm) ? (" \xC2\xB7 " + num1(t.condMsCm, "%.2f") + " mS/cm").c_str() : "");
        // ITDS-2.16.0: upright, the two buttons on a line under the sweep's own
        const bool tall = portrait();
        c->text(area.x, y0 + 16, tall ? gfx::Canvas::elide(b, SB14, area.w) : std::string(b), SB14, t.ok ? th::text : th::bad);
        const int by0 = tall ? y0 + 28 : y0 - 4;
        if (u->button(ID_LR_BACK, Rect(area.r() - 160, by0, 160, 36), "back to the sweeps", false, true)) { m_lotRawRow = -1; return; }
        // H7-2.4.1: a sweep taken out of the evaluation by hand (the record keeps it)
        {
            int ri2 = -1;
            for (int i = 0; i < int(runs.size()); ++i) if (&runs[size_t(i)] == pg.run) ri2 = i;
            const int ti = int(&t - pg.run->tests.data());
            u->why(ID_LR_REMOVE, "Wait: a lot test sweep is running. Stop it, or let it finish.");
            if (ri2 >= 0 && ti >= 0 && !t.removed &&
                u->button(ID_LR_REMOVE, Rect(area.r() - 340, by0, 170, 36), "Remove this sweep", false, !a.lotTesting())) {
                a.lotSetRemoved(ri2, ti, true);
                return;
            }
        }
        int y = y0 + 30 + (tall ? 40 : 0);
        if (t.raw.empty()) {
            c->textBox(Rect(area.x, y + 10, area.w, 80), t.step == 0
                ? "Air sweeps are kept as their fit only: Cp " + num1(t.cpF * 1e12) + " pF, Gp " + num1(t.gpS * 1e9, "%.2f") + " nS." +
                  (t.why.empty() ? std::string() : " " + t.why)
                : "This sweep's points were not kept." + (t.why.empty() ? std::string() : " " + t.why),
                S14, th::textDim, gfx::Left, 0, 1.25f);
            return;
        }
        const int cols[4] = { 130, 150, 130, 70 };
        const char* hd[4] = { "f Hz", "|Z| ohm", "phase \xC2\xB0", "range" };
        for (int half = 0; half < (tall ? 1 : 2); ++half) {
            int x = area.x + half * 500;
            for (int k = 0; k < 4; ++k) { c->text(x + 4, y + 14, hd[k], SB11, th::textFaint); x += cols[k] / 1; }
        }
        y += 22;
        std::vector<birx::Point> pts = t.raw;
        std::sort(pts.begin(), pts.end(), [](const birx::Point& p, const birx::Point& q) { return p.freqHz < q.freqHz; });
        const int per = tall ? std::max(1, int(pts.size())) : int((pts.size() + 1) / 2);
        for (int i = 0; i < int(pts.size()); ++i) {
            const int half = i / per, row = i % per;
            const birx::Point& p = pts[size_t(i)];
            int x = area.x + half * 500;
            const int yy = y + row * 30;
            c->text(x + 4, yy + 20, num1(p.freqHz, "%.0f"), M14, th::text); x += cols[0];
            c->text(x + 4, yy + 20, num1(p.magOhm, "%.1f"), M14, th::text); x += cols[1];
            c->text(x + 4, yy + 20, num1(p.phaseDeg, "%+.2f"), M14, th::text); x += cols[2];
            c->text(x + 4, yy + 20, std::to_string(p.range), M14, th::accent);
        }
        if (!t.why.empty()) c->textBox(Rect(area.x, y + per * 30 + 8, area.w, 40), t.why, S12, th::bad, gfx::Left, 2, 1.2f);
        return;
    }
    const char* hd[12] = { "step", "#", "ok", "T \xC2\xB0""C", "Cp pF", "Z 1k", "Z 10k", "Z 100k", "Z 500k", "ph 100k", "ph 500k", "ranges 1k/100k/500k" };
    const int cw[12] = { 74, 30, 40, 56, 66, 70, 70, 72, 72, 76, 76, 150 };
    // ITDS-2.16.0: upright, two lines a sweep: from Z 500k on under the rest
    const bool tall = portrait();
    auto colX = [&](int k) { int x = area.x; for (int j = tall && k >= 8 ? 8 : 0; j < k; ++j) x += cw[j]; return tall && k >= 8 ? x + cw[0] : x; };
    auto colDy = [&](int k) { return tall && k >= 8 ? 18 : 0; };
    for (int k = 0; k < 12; ++k) c->text(colX(k) + 3, y0 + 14 + colDy(k), hd[k], SB11, th::textFaint);
    static int contentH = 400;
    const int vy = y0 + 20 + (tall ? 18 : 0), rowH = tall ? 48 : 30;
    const Rect vp(area.x, vy, area.w, area.b() - vy);
    const int off = u->scroll(ID_LS_RSCROLL + 50, vp, contentH);
    c->setClip(vp);
    int y = vp.y - off;
    for (int i = 0; i < int(rows.size()); ++i) {
        const birx::LotTest& t = *rows[size_t(i)];
        const Rect rr(area.x, y, area.w - 10, rowH);
        if (u->clicked(ID_LR_ROW + i, rr)) m_lotRawRow = i;
        c->fill(rr, u->pressed(ID_LR_ROW + i) ? th::accentSoft : (i % 2 ? th::surface : th::bg));
        const birx::Point *p1 = rawAt(t.raw, 1000), *p10 = rawAt(t.raw, 10000), *p100 = rawAt(t.raw, 100000), *p500 = rawAt(t.raw, 500000);
        const std::string cells[12] = {
            t.step == 0 ? std::string("air") : std::string(birx::lotStepName(t.step)).substr(7), std::to_string(t.repeat),
            t.removed ? "\xE2\x80\x94" : t.ok ? "\xE2\x9C\x93" : "\xE2\x9C\x95", t.step ? num1(t.tempC) : std::string(), t.step == 0 ? num1(t.cpF * 1e12) : std::string(),
            zs(p1), zs(p10), zs(p100), zs(p500), ps(p100), ps(p500),
            t.raw.empty() ? std::string() : rs(p1) + " / " + rs(p100) + " / " + rs(p500) };
        for (int k = 0; k < 12; ++k) {
            const Color col = t.removed ? th::textFaint : k == 2 ? (t.ok ? th::good : th::bad) : k == 11 ? th::accent : th::text;
            c->text(colX(k) + 3, y + 20 + colDy(k), cells[k], M12, col);
        }
        y += rowH + 2;
    }
    const char* tapNote = "Tap a row for its fifteen frequencies. |Z| in ohm, phase in degrees, as sent by the measurement unit.";
    if (tall) c->textBox(Rect(area.x, y + 4, area.w - 10, 40), tapNote, S12, th::textFaint, gfx::Left, 2, 1.2f);
    else c->text(area.x, y + 18, tapNote, S12, th::textFaint);
    y += 30;
    contentH = y + off - vp.y;
    c->resetClip();
}

} // namespace gui