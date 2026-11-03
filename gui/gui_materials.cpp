// gui/gui_materials.cpp — Settings -> Training (H7-2.5.0): the material library. Sweeps of
// known materials are labelled and kept (records/materials.txt); a sweep of something not
// labelled is then said to be like each of them (birx::MatModel, matlib.h). H7-2.5.1: the
// main screen's box under the Z-IPS map; H7-2.5.3: a case's readings tagged with what they
// were, and the library to and from a USB stick; H7-2.10.0: the needle's QC (its Air check
// and its saline check) before its sweeps; H7-2.11.0: the sweeps of a faulty tip.
#include "gui.h"

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
    ID_M_SCROLL = 15000, ID_M_ID, ID_M_STOP, ID_M_NEW, ID_M_SWEEP, ID_M_AGAIN, ID_M_DELLAB, ID_M_DELLAST,
    ID_M_PH, ID_M_Z, ID_M_MAIN, ID_M_DISCARD, ID_M_KEEPNEW,
    ID_M_CHIP = 15100, ID_MB_PHASE = 15300, ID_MB_MAT, ID_M_KEEP = 15400, ID_M_USE = 16000,
    ID_M_EXP = 18100, ID_M_IMP, ID_M_REF, ID_M_AIR, ID_M_NEEDLE, ID_T_CLOSE, ID_T_NEW, ID_T_REMOVE, ID_T_EXP, ID_T_IMP, ID_T_CLOSE2, ID_T_CHIP = 18200,
    ID_TR_CLOSE = 18160,
    ID_Q_LOT = 18400, ID_Q_SWEEPS = 18500, ID_Q_MIN, ID_Q_AIR, ID_Q_DELTIP, ID_Q_KEEPUNC, ID_M_OFF, ID_M_SKIP, ID_Q_KEEPSUS,
};
// H7-2.12.2: said when a control is tapped while a sweep or check keeps it off
const char* const kWhyBusy = "Wait: a sweep or saline check is running. Stop it (below) or let it finish.";
const Color kMat[8] = { gfx::hex(0x4FB3E8), gfx::hex(0xF0B24A), gfx::hex(0x4BD07C), gfx::hex(0xFF6B6B),
                        gfx::hex(0xB18CFF), gfx::hex(0x3FD0C9), gfx::hex(0xE58BC1), gfx::hex(0xC9C27A) };
// a sweep the classifier does not learn: set aside by its own check, or by hand
bool setAside(const birx::MatModel& md, size_t i)
{
    return i < md.state.size() && (md.state[i] == birx::MatModel::kAsideAuto || md.state[i] == birx::MatModel::kAsideByHand ||
                                   md.state[i] == birx::MatModel::kQcSuspect || md.state[i] == birx::MatModel::kTipFault ||
                                   md.state[i] == birx::MatModel::kQcPending || md.state[i] == birx::MatModel::kOff);
}
Color matColor(const birx::MatLibrary& lib, const std::string& l)
{
    for (size_t i = 0; i < lib.labels.size(); ++i) if (lib.labels[i] == l) return kMat[i % 8];
    return th::textFaint;
}
// the curve drawn for a sweep: its phase, or the shape of log10|Z| (minus its mean)
bool curveOf(const std::vector<birx::Point>& pts, bool zShape, std::vector<double>* f, std::vector<double>* v)
{
    f->clear(); v->clear();
    for (const birx::Point& p : pts) {
        if (!(p.freqHz > 0) || !std::isfinite(p.phaseDeg) || !(p.magOhm > 0)) continue;
        f->push_back(p.freqHz);
        v->push_back(zShape ? std::log10(p.magOhm) : p.phaseDeg);
    }
    if (f->size() < 3) return false;
    // low to high frequency
    std::vector<size_t> ix(f->size());
    for (size_t i = 0; i < ix.size(); ++i) ix[i] = i;
    std::sort(ix.begin(), ix.end(), [&](size_t a2, size_t b2) { return (*f)[a2] < (*f)[b2]; });
    std::vector<double> f2, v2;
    for (size_t i : ix) { f2.push_back((*f)[i]); v2.push_back((*v)[i]); }
    *f = f2; *v = v2;
    if (zShape) {
        double m = 0;
        for (double x : *v) m += x;
        m /= double(v->size());
        for (double& x : *v) x = (x - m) * 100;   // per cent of a decade
    }
    return true;
}
} // namespace

void Gui::tabMaterials(const Rect& body)
{
    static int contentH = 1200;
    const int off = u->scroll(ID_M_SCROLL, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    const uint32_t now = u->now();
    const birx::MatLibrary& lib = a.matLibrary();
    const birx::MatModel& md = a.matModel();
    if (!m_matSel.empty() && !lib.hasLabel(m_matSel)) m_matSel.clear();
    if (m_matSel.empty() && !a.matLastLabel().empty() && lib.hasLabel(a.matLastLabel())) m_matSel = a.matLastLabel();
    const bool delLabArmed = m_matDelLabArm && now - m_matDelLabArm < 5000;
    const bool delLastArmed = m_matDelLastArm && now - m_matDelLastArm < 5000;
    const bool delTipArmed = m_matDelTipArm && now - m_matDelTipArm < 5000;
    if (!delTipArmed) m_matDelTipArm = 0;
    else m_timerAt = m_matDelTipArm + 5000;
    const bool keepSusArmed = m_matKeepSusArm && now - m_matKeepSusArm < 5000;     // H7-2.12.5
    if (!keepSusArmed) m_matKeepSusArm = 0;
    else m_timerAt = m_matKeepSusArm + 5000;
    const bool skipArmed = m_matSkipArm && now - m_matSkipArm < 5000;              // H7-2.12.4
    if (!skipArmed) m_matSkipArm = 0;
    else m_timerAt = m_matSkipArm + 5000;
    const bool keepUncArmed = m_matKeepUncArm && now - m_matKeepUncArm < 5000;     // H7-2.12.0
    if (!keepUncArmed) m_matKeepUncArm = 0;
    else m_timerAt = m_matKeepUncArm + 5000;
    if (!delLabArmed) m_matDelLabArm = 0;
    if (!delLastArmed) m_matDelLastArm = 0;
    if (delLabArmed) m_timerAt = m_matDelLabArm + 5000;
    if (delLastArmed) m_timerAt = m_matDelLastArm + 5000;

    if (!m_msg.empty()) {
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 6, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 6, 1.25f);
        y += h + 8;
    }
    y += c->textBox(Rect(body.x + 4, y, W - 8, 120),
        "Sweep something you know (a saline, an apple, a phantom) and label it with what it was. A sweep of something "
        "not labelled is then said to be, in per cent, like each labelled material: by its whole curve (the phase and |Z| "
        "at all 15 frequencies), as a blend of them, by its Z and IPS alone, and FINALLY by the three together, each "
        "weighed by how well it told left-out placements apart. Every labelled sweep teaches them at once. Not for "
        "patients: the per cent compare materials on the bench, not tissue.", S13, th::textDim, gfx::Left, 0, 1.3f) + 10;

    // ---- H7-2.9.0: the needle these sweeps are taken with
    {
        const std::string& nd = a.trainNeedle();
        const bool calMine = a.matCpKnown() && a.trainCalMatches();
        std::string sub;
        if (nd.empty()) sub = "Enter the number of the needle before its first sweep. It is kept with every sweep, its Air check and "
                              "its saline check, so that what each needle changes by itself can be told from the materials.";
        else if (!a.matCpKnown()) sub = "No Air check yet for this needle (QC, below).";
        else if (!a.trainCalMatches()) sub = "The Air check in force was taken with needle " + a.calNeedle() +
                                             ": do this needle's Air check and saline check (QC, below) before its sweeps.";
        else sub = std::string("Its Air check is done") + (a.calNeedle().empty() ? " (taken before needle numbers were asked for)" : "") +
                   (a.qcCheckOk() && !a.qcSettled() ? "; its saline check too, but its tip is still settling (QC, below)."
                    : a.qcCheckOk() ? "; its saline check too." : "; its saline check is not (QC, below).");
        // ITDS-2.16.0: on a narrow screen the sentence under the number and the button, across the card
        const bool narrow = W < 800;
        const int tw = narrow ? W - 28 : W - 28 - 230 - 160;
        const int subH = gfx::Canvas::wrapHeight(tw, sub, S13, narrow ? 6 : 4, 1.25f);
        const int h = narrow ? 70 + subH + 12 : std::max(70, subH + 40);
        const bool warn = nd.empty() || (a.matCpKnown() && !a.trainCalMatches());
        u->card(Rect(body.x, y, W, h), warn ? th::warnSoft : th::surface, warn ? th::warn : th::accent);
        c->text(body.x + 14, y + 22, "NEEDLE", SB12, th::textFaint);
        c->text(body.x + 14, y + 52, nd.empty() ? std::string("\xE2\x80\x94") : gfx::Canvas::elide(nd, SB24, 150), SB24,
                nd.empty() ? th::warn : th::text);
        c->textBox(narrow ? Rect(body.x + 14, y + 70, tw, subH) : Rect(body.x + 180, y + 12, tw, h - 20), sub, S13,
                   warn ? th::warn : (calMine ? th::textDim : th::warn), gfx::Left, narrow ? 6 : 4, 1.25f);
        u->why(ID_M_NEEDLE, kWhyBusy);
        if (u->button(ID_M_NEEDLE, Rect(body.x + W - 14 - 220, narrow ? y + 11 : y + (h - 48) / 2, 220, 48),
                      nd.empty() ? "Enter its number" : "Another needle", nd.empty(), !a.matTesting())) {
            openText("The needle's number", nd, 20, [this](const std::string& v) {
                const std::string w2 = a.setTrainNeedle(v);
                if (!w2.empty()) say(w2, true); else m_msg.clear();
            }, "As printed on it, or one you write on it (N1, N2 ...): the same number every time that needle is used.");
        }
        y += h + 8;
    }

    // ---- H7-2.10.0: the needle's QC — its Air check and its saline check, within its lot's limits (Settings ->
    // Lots, carried in the lot file) or the generic ones; due again by sweeps and by minutes; each saline check
    // closes the sweeps since the one before
    {
        const app::Settings& st = a.settings();
        const std::string lot = st.trainLot;
        const birx::QcLimits q = a.qcLimitsFor(lot, birx::kCheck9);
        const bool airMine = a.matCpKnown() && a.trainCalMatches();
        const bool airOk = a.qcAirOk();
        const bool due = a.qcCheckOk() && (a.qcSweepsSince() >= st.qcSweeps || a.qcMinutesSince() >= st.qcMinutes);
        const bool settling = a.qcCheckOk() && !a.qcSettled();      // H7-2.12.0: a new tip settles first
        const bool chkOk = a.qcCheckOk() && !due && !settling;
        char b[400];
        std::string airT, chkT;
        if (!airMine) airT = a.matCpKnown() ? "Air check: taken with needle " + a.calNeedle() + ", not this one. Do it."
                                            : std::string("Air check: not done. The needle in clean air, six seconds.");
        else {
            std::snprintf(b, sizeof b, "Air check \xE2\x9C\x93 %.1f pF, %d min ago (again after %d min)%s",
                          a.trainingNeedle().cpF * 1e12, std::max(0, a.qcAirMinutes()), st.qcAirMinutes, airOk ? "" : " \xE2\x80\x94 DUE");
            airT = b;
        }
        if (!a.qcCheckOk()) chkT = "Saline check: not done since the Air check. The tip in 0.9 % saline, two sweeps.";
        else if (settling) chkT = "Saline check \xE2\x9C\x93 " + a.matRefText() + " \xE2\x80\x94 SETTLING. " + a.qcSettleText();
        else {
            std::snprintf(b, sizeof b, "Saline check %s %s \xC2\xB7 %d of %d sweeps, %d of %d min since", due ? "DUE \xE2\x80\x94 the last:" : "\xE2\x9C\x93",
                          a.matRefText().c_str(), a.qcSweepsSince(), st.qcSweeps, std::max(0, a.qcMinutesSince()), st.qcMinutes);
            chkT = b;
        }
        if (a.qcPending()) {
            std::snprintf(b, sizeof b, " \xC2\xB7 %d sweep%s wait%s for the next check", a.qcPending(), a.qcPending() == 1 ? "" : "s",
                          a.qcPending() == 1 ? "s" : "");
            chkT += b;
        }
        const std::string lim = (q.fromLot ? "Lot " + lot + "'s limits (its good needles in Settings \xE2\x86\x92 Lots): "
                                           : std::string("Generic limits (no lot chosen): ")) + q.describe() + ".";
        const int tw = W - 28 - 230;
        if (settling) m_timerAt = u->now() + 1000;                  // its countdown
        const int h1 = gfx::Canvas::wrapHeight(tw, airT, SB14, 3, 1.25f), h2 = gfx::Canvas::wrapHeight(tw, chkT, SB14, 5, 1.25f);
        const int h3 = gfx::Canvas::wrapHeight(W - 28, lim, S13, 4, 1.25f);
        // the lot chips
        std::vector<std::string> lots = a.lotIds();
        lots.insert(lots.begin(), std::string());
        struct Ch { std::string id; Rect r; };
        std::vector<Ch> chips;
        int cx = body.x + 14 + 70, cyy = 0;
        for (const std::string& l : lots) {
            const std::string tl = l.empty() ? std::string("No lot") : gfx::Canvas::elide(l, SB14, 160);
            const int w = gfx::Canvas::measure(tl, SB14) + 28;
            if (cx + w > body.x + W - 14) { cx = body.x + 14 + 70; cyy += 46; }
            chips.push_back({ l, Rect(cx, cyy, w, 40) });
            cx += w + 8;
        }
        // H7-2.11.2: why the two buttons cannot be pressed, said under them
        const bool canAct = !a.matTesting() && !a.matRefRunning() && !a.caseOpen() && !a.trainNeedle().empty();
        const std::string refWhy = canAct ? a.matRefBlocker() : std::string();
        std::string why;
        if (a.trainNeedle().empty()) why = "Air check and saline check: enter the needle's number first (NEEDLE, above).";
        else if (a.caseOpen()) why = "Air check and saline check are off while a case is open (" + a.caseId() +
                                     "). End the case on the main screen first: Training's checks are not patient readings.";
        else if (a.matRefRunning()) why = "The saline check is under way (below): wait, or Stop it.";
        else if (a.matTesting()) why = "A sweep is under way (below): wait, or Stop it.";
        else if (!refWhy.empty()) why = "Saline check: " + refWhy;
        const int h4 = why.empty() ? 0 : gfx::Canvas::wrapHeight(W - 28, why, SB14, 4, 1.25f) + 10;
        const int hTop = 34, hRows = std::max(settling ? 166 : 110, h1 + h2 + 34), hChips = cyy + 40 + 12;
        const int h = hTop + hRows + h4 + hChips + h3 + 16;
        const bool bad = !(airOk && chkOk);
        u->card(Rect(body.x, y, W, h), bad ? th::warnSoft : th::surface, bad ? th::warn : th::good);
        c->text(body.x + 14, y + 24, "QC OF THIS NEEDLE \xE2\x80\x94 NO SWEEP UNTIL BOTH PASS", SB12, th::textFaint);
        int yy = y + hTop;
        c->textBox(Rect(body.x + 14, yy, tw, h1), airT, SB14, airOk ? th::good : th::warn, gfx::Left, 3, 1.25f);
        c->textBox(Rect(body.x + 14, yy + h1 + 14, tw, h2), chkT, SB14, chkOk ? th::good : th::warn, gfx::Left, 5, 1.25f);
        u->why(ID_M_AIR, why);
        if (u->button(ID_M_AIR, Rect(body.x + W - 14 - 210, yy, 210, 46), airMine ? "Air check again" : "Air check", !airOk, canAct)) {
            a.calOpen(true); m_calib = true; m_calDip = false; m_calRef = false; m_calRefStarted = false;
        }
        u->why(ID_M_REF, why.empty() ? std::string("The needle's Air check first (the button above).") : why);
        if (u->button(ID_M_REF, Rect(body.x + W - 14 - 210, yy + 56, 210, 46), a.qcCheckOk() && (!due || settling) ? "Saline check again" : "Saline check",
                      airOk && !chkOk, canAct && refWhy.empty())) {
            m_calib = true; m_calDip = false; m_calRef = true; m_calRefStarted = false;
        }
        // H7-2.12.4: the way out of the settling (two taps): sweep now; the next check still closes the sweeps
        if (settling) {
            ui::Ui::Btn st;
            st.primary = false; st.danger = skipArmed; st.enabled = canAct;
            u->why(ID_M_SKIP, why);
            if (u->button(ID_M_SKIP, Rect(body.x + W - 14 - 210, yy + 112, 210, 46),
                          skipArmed ? "Tap again: sweep now" : "Sweep without waiting", st)) {
                if (!skipArmed) m_matSkipArm = now | 1;
                else { m_matSkipArm = 0; const std::string w = a.qcSkipSettling(); if (!w.empty()) say(w, true); }
            }
        }
        yy += hRows;
        if (h4) {
            c->textBox(Rect(body.x + 14, yy, W - 28, h4 - 10), why, SB14, th::warn, gfx::Left, 4, 1.25f);
            yy += h4;
        }
        c->text(body.x + 14, yy + 26, "Lot", SB14, th::textDim);
        for (size_t i = 0; i < chips.size(); ++i) {
            const Rect r(chips[i].r.x, yy + chips[i].r.y, chips[i].r.w, chips[i].r.h);
            if (u->chip(ID_Q_LOT + int(i), r, chips[i].id.empty() ? std::string("No lot") : chips[i].id, chips[i].id == lot) &&
                chips[i].id != lot) {
                const std::string w = a.setTrainLot(chips[i].id);
                if (!w.empty()) say(w, true); else m_msg.clear();
            }
        }
        yy += hChips;
        c->textBox(Rect(body.x + 14, yy, W - 28, h3), lim, S13, th::textDim, gfx::Left, 4, 1.25f);
        y += h + 8;
    }

    // ---- what is going on now
    {
        const std::string blk = a.matBlocker();
        std::string t;
        Color tc = th::textDim, bc = th::stroke, fc = th::surface;
        if (a.matRefRunning()) {        // H7-2.10.0: the needle's saline check
            const int s = a.dipSecondsLeft();
            char b[200];
            if (a.dipStage() == app::DipStage::Settling)
                std::snprintf(b, sizeof b, "Saline check: settling, %d s. Keep the tip still in the 0.9 %% saline.", s);
            else std::snprintf(b, sizeof b, "Saline check: sweeping (two sweeps). Keep the tip still in the saline.");
            t = b; tc = th::accent; bc = th::accent; fc = th::accentSoft;
        } else if (a.matTesting()) {
            const int s = a.matSecondsLeft();
            const std::string what = a.matRefRunning() ? std::string("The needle's saline reference")
                                   : !a.matRunLabel().empty() ? "Sweep of " + a.matRunLabel() : std::string("Identify");
            char b[200];
            switch (a.matStage()) {
            case app::MatStage::Settling:
                std::snprintf(b, sizeof b, "%s: settling, %d s. Keep the tip still in the material.", what.c_str(), s); break;
            case app::MatStage::Waiting:
                std::snprintf(b, sizeof b, "%s: waiting for the sweep to start (%d s).", what.c_str(), s); break;
            default:
                std::snprintf(b, sizeof b, "%s: sweeping. Keep the tip still.", what.c_str()); break;
            }
            t = b; tc = th::accent; bc = th::accent; fc = th::accentSoft;
        } else if (a.caseOpen()) {      // H7-2.11.2: an open case comes before an older message
            t = blk; tc = th::warn; bc = th::warn; fc = th::warnSoft;
        } else if (!a.matMessage().empty()) {
            t = a.matMessage();
            tc = a.matMessageOk() ? th::good : th::warn;
            bc = tc; fc = a.matMessageOk() ? th::goodSoft : th::warnSoft;
        } else if (!blk.empty()) {
            t = blk; tc = th::warn; bc = th::warn; fc = th::warnSoft;
        } else {
            t = "Ready. Put the tip in the material, then press a Sweep button (or Identify).";
        }
        const bool busy = a.matTesting() || a.matRefRunning();
        const int tw = W - 28 - (busy ? 140 : 0);
        const int h = std::max(54, gfx::Canvas::wrapHeight(tw, t, SB14, 6, 1.25f) + 24);
        u->card(Rect(body.x, y, W, h), fc, bc);
        {   // H7-2.12.3: the settling (amber), then the sweep (blue), as a line filling round this card
            double f = 0;
            Color col = th::accent;
            if (a.matRefRunning()) { if (!checkRing(&f, &col)) f = 0; }
            else if (!matRing(&f, &col)) f = 0;
            if (f > 0) progressRing(Rect(body.x, y, W, h), f, col, th::radiusSm);
        }
        c->textBox(Rect(body.x + 14, y + 12, tw, h - 24), t, SB14, tc, gfx::Left, 6, 1.25f);
        if (busy && u->button(ID_M_STOP, Rect(body.x + W - 134, y + (h - 44) / 2, 120, 44), "Stop", false, true)) {
            if (a.matRefRunning()) a.dipStop();
            else a.matStop("Stopped: nothing was kept.");
        }
        if (busy) m_timerAt = u->now() + 125;
        y += h + 10;
    }

    // ---- H7-2.5.1: an identified sweep, told what it really was, is learnt too
    if (a.matCanKeepLast()) {
        const birx::MatMatch& mm = a.matLastMatch();
        std::vector<std::string> order;
        if (mm.ok && mm.best >= 0 && !mm.unlike) order.push_back(mm.labels[size_t(mm.best)]);   // the guess first
        for (const std::string& l : lib.labels) if (std::find(order.begin(), order.end(), l) == order.end()) order.push_back(l);
        // lay the chips out first, to know the card's height
        struct Ch { std::string l; Rect r; };
        std::vector<Ch> chips;
        int x = body.x + 14, yy = 40;
        for (const std::string& l : order) {
            const std::string t = "    " + gfx::Canvas::elide(l, SB16, 220);
            const int w = gfx::Canvas::measure(t, SB16) + 30;
            if (x + w > body.x + W - 14) { x = body.x + 14; yy += 50; }
            chips.push_back({ l, Rect(x, yy, w, 42) });
            x += w + 8;
        }
        const int wNew = gfx::Canvas::measure("+ New label", SB16) + 30, wNo = gfx::Canvas::measure("Don't keep it", SB16) + 30;
        if (x + wNew + 8 + wNo > body.x + W - 14) { x = body.x + 14; yy += 50; }
        const Rect rNew(x, yy, wNew, 42), rNo(x + wNew + 8, yy, wNo, 42);
        const int h = yy + 42 + 14;
        const Rect card(body.x, y, W, h);
        u->card(card, th::surfaceHi, th::accent);
        c->text(card.x + 14, card.y + 26, "WHAT WAS IT REALLY?", SB12, th::accent);
        c->text(card.x + 170, card.y + 26, "tap it, and this sweep is learnt with that label (a new placement)", S13, th::textDim);
        for (size_t i = 0; i < chips.size(); ++i) {
            const Rect rr(chips[i].r.x, card.y + chips[i].r.y, chips[i].r.w, chips[i].r.h);
            const std::string t = "    " + gfx::Canvas::elide(chips[i].l, SB16, 220);
            if (u->chip(ID_M_KEEP + int(i), rr, t, false)) {
                const std::string w = a.matKeepLast(chips[i].l);
                if (w.empty()) { m_msg.clear(); m_matSel = chips[i].l; }
                else say(w, true);
            }
            c->circle(rr.x + 18, rr.y + 21, 6, matColor(lib, chips[i].l));
        }
        if (u->chip(ID_M_KEEPNEW, Rect(rNew.x, card.y + rNew.y, rNew.w, rNew.h), "+ New label", false)) {
            openText("What it was: a new material", "", 40, [this](const std::string& v) {
                const std::string w = a.matAddLabel(v);
                if (!w.empty()) { say(w, true); return; }
                std::string l = v;
                while (!l.empty() && l.back() == ' ') l.pop_back();
                while (!l.empty() && l[0] == ' ') l.erase(0, 1);
                const std::string w2 = a.matKeepLast(l);
                if (w2.empty()) { m_msg.clear(); m_matSel = l; }
                else say(w2, true);
            }, "The identified sweep is kept with it. Sweep it again (new placements) so that it can join the classifier.");
        }
        if (u->chip(ID_M_DISCARD, Rect(rNo.x, card.y + rNo.y, rNo.w, rNo.h), "Don't keep it", false)) a.matDiscardLast();
        y += h + 10;
    }

    // ---- the curves (left) and the identification (right)
    auto curves = [&]() {
        // ITDS-2.16.0: on a narrow screen the identification under the curves
        const bool narrow = W < 800;
        const Rect ch(body.x, y, narrow ? W : 586, 340);
        const Rect idr = narrow ? Rect(body.x, y + 350, W, 340) : Rect(body.x + 596, y, W - 596, 340);
        u->card(ch, th::surface, th::stroke);
        if (u->chip(ID_M_PH, Rect(ch.x + 10, ch.y + 8, 80, 32), "Phase", !m_matChartZ)) m_matChartZ = false;
        if (u->chip(ID_M_Z, Rect(ch.x + 96, ch.y + 8, 100, 32), "|Z| shape", m_matChartZ)) m_matChartZ = true;
        c->text(ch.x + 206, ch.y + 29, m_matChartZ ? "log|Z| minus its mean, % of a decade" : "degrees", M10, th::textFaint);
        // every label's mean curve (its sweeps in use), and the last sweep
        struct Curve { std::vector<double> f, v; Color col; bool last; };
        std::vector<Curve> cs;
        for (const std::string& l : lib.labels) {
            std::vector<double> mf, mv;
            int n = 0;
            for (size_t si = 0; si < lib.sweeps.size(); ++si) {
                const birx::MatSweep& s = lib.sweeps[si];
                if (s.label != l || setAside(md, si)) continue;
                std::vector<double> f, v;
                if (!curveOf(s.pts, m_matChartZ, &f, &v)) continue;
                if (n == 0) { mf = f; mv = v; n = 1; continue; }
                if (f.size() != mf.size()) continue;
                bool same = true;
                for (size_t k = 0; k < f.size(); ++k) if (!(std::fabs(f[k] / mf[k] - 1) < 0.02)) { same = false; break; }
                if (!same) continue;
                for (size_t k = 0; k < v.size(); ++k) mv[k] += v[k];
                ++n;
            }
            if (!n) continue;
            for (double& x : mv) x /= n;
            cs.push_back({ mf, mv, matColor(lib, l), false });
        }
        {
            Curve lc;
            if (curveOf(a.matLastSweep(), m_matChartZ, &lc.f, &lc.v)) { lc.col = th::text; lc.last = true; cs.push_back(lc); }
        }
        const int px = ch.x + 48, py = ch.y + 52, pw = ch.w - 60, ph = ch.h - 82;
        if (cs.empty()) {
            c->textBox(Rect(px, ch.y + 112, pw, 120), "No sweep yet. Each label's mean curve is drawn here in its colour, and the last "
                       "sweep in white.", S14, th::textFaint, gfx::Center, 0, 1.3f);
        } else {
            double lo = 1e9, hi = -1e9;
            for (const Curve& k : cs) for (double x : k.v) { if (x < lo) lo = x; if (x > hi) hi = x; }
            if (hi - lo < 4) { const double m = (hi + lo) * 0.5; lo = m - 2; hi = m + 2; }
            const double pad = (hi - lo) * 0.08;
            lo -= pad; hi += pad;
            const double lx0 = std::log10(400.0), lx1 = std::log10(6e5);
            auto X = [&](double f) { return px + (std::log10(f) - lx0) / (lx1 - lx0) * pw; };
            auto Y = [&](double v) { return py + (1 - (v - lo) / (hi - lo)) * ph; };
            c->setClip(ch.intersect(body));
            const double fs[3] = { 1e3, 1e4, 1e5 };
            for (double f : fs) {
                c->fill(Rect(int(X(f)), py, 1, ph), gfx::hex(0xFFFFFF), 18);
                c->text(int(X(f)) - 10, py + ph + 18, f >= 1e5 ? "100k" : f >= 1e4 ? "10k" : "1k", M9, th::textFaint);
            }
            const double span = hi - lo;
            const double st = span >= 60 ? 20 : span >= 30 ? 10 : span >= 12 ? 5 : span >= 6 ? 2 : 1;
            for (double g = std::ceil(lo / st) * st; g <= hi; g += st) {
                const int gy = int(Y(g));
                c->fill(Rect(px, gy, pw, 1), gfx::hex(0xFFFFFF), 14);
                char b[16];
                std::snprintf(b, sizeof b, "%.0f", g);
                c->text(ch.x + 8, gy + 4, b, M9, th::textFaint);
            }
            for (const Curve& k : cs) {
                for (size_t i = 1; i < k.f.size(); ++i)
                    c->line(float(X(k.f[i - 1])), float(Y(k.v[i - 1])), float(X(k.f[i])), float(Y(k.v[i])), k.last ? 2.6f : 2.0f, k.col);
                for (size_t i = 0; i < k.f.size(); ++i) c->circle(float(X(k.f[i])), float(Y(k.v[i])), k.last ? 3.0f : 2.2f, k.col);
            }
            c->setClip(body);
        }

        u->card(idr, th::surface, th::stroke);
        {   // H7-2.12.3: an Identify sweep under way, round its own card
            double f = 0; Color col = th::accent;
            if (a.matRunLabel().empty() && matRing(&f, &col) && f > 0) progressRing(idr, f, col, th::radiusSm);
        }
        c->text(idr.x + 14, idr.y + 24, "IDENTIFY", SB12, th::textFaint);
        const bool canId = md.ok && a.matBlocker().empty() && !a.matTesting();
        const app::App::NeedleStep step = !md.ok || a.matTesting() || a.matRefRunning() ? app::App::NeedleStep::None : a.matNextStep();
        if (step != app::App::NeedleStep::None) {      // H7-2.13.0: the needle's check, here
            const char* lab = step == app::App::NeedleStep::Air ? "Air check first" : step == app::App::NeedleStep::Saline
                                  ? (a.qcCheckOk() ? "Saline check (due)" : "Saline check first") : "Enter the needle's number";
            if (u->button(ID_M_ID, Rect(idr.x + 14, idr.y + 34, idr.w - 28, 48), lab, true, true)) needleStep(int(step));
        } else {
            u->why(ID_M_ID, !md.ok ? md.why : a.matTesting() ? std::string(kWhyBusy) : a.matBlocker());
            if (u->button(ID_M_ID, Rect(idr.x + 14, idr.y + 34, idr.w - 28, 48), "Sweep it and compare", true, canId)) {
                const std::string w = a.matStart("", true);
                if (!w.empty()) say(w, true); else m_msg.clear();
            }
        }
        int yy = idr.y + 94;
        const birx::MatMatch& mm = a.matLastMatch();
        if (!md.ok) {
            c->textBox(Rect(idr.x + 14, yy, idr.w - 28, 120), md.why, S14, th::textDim, gfx::Left, 0, 1.3f);
        } else if (!mm.ok) {
            c->textBox(Rect(idr.x + 14, yy, idr.w - 28, 160), "Put the tip in the unknown material and press the button. The result is "
                       "how much the curve is like each labelled material, the per cent adding up to 100.", S14, th::textDim, gfx::Left, 0, 1.3f);
        } else {
            if (mm.normalised) c->text(idr.x + 100, idr.y + 24, "needle-normalised", SB12, th::good);
            // H7-2.8.0: the final answer first
            if (mm.fOk && mm.fBest >= 0) {
                const std::string fl = mm.labels[size_t(mm.fBest)];
                char b[160];
                if (mm.unlike) std::snprintf(b, sizeof b, "FINAL: like none of them (nearest %s)", fl.c_str());
                else std::snprintf(b, sizeof b, "FINAL: %.0f %%  %s", mm.fProb[size_t(mm.fBest)] * 100, fl.c_str());
                c->text(idr.x + 14, yy + 14, gfx::Canvas::elide(b, SB16, idr.w - 28), SB16, mm.unlike ? th::warn : matColor(lib, fl));
                yy += 24;
            }
            if (mm.zOk && mm.zBest >= 0) {
                const std::string zl = mm.labels[size_t(mm.zBest)];
                char b[200];
                if (mm.zUnlike)
                    std::snprintf(b, sizeof b, "By Z and IPS (%s, %.2f): like none (nearest %s)", ohmShort(mm.z1k).c_str(), mm.ips, zl.c_str());
                else
                    std::snprintf(b, sizeof b, "By Z and IPS (%s, %.2f): %.0f %% %s", ohmShort(mm.z1k).c_str(), mm.ips,
                                  mm.zProb[size_t(mm.zBest)] * 100, zl.c_str());
                c->text(idr.x + 14, yy + 12, gfx::Canvas::elide(b, S13, idr.w - 28), S13, th::textDim);
                yy += 22;
            }
            c->text(idr.x + 14, yy + 12, "By the whole curve:", SB11, th::textFaint);
            yy += 18;
            std::vector<size_t> ord(mm.labels.size());
            for (size_t i = 0; i < ord.size(); ++i) ord[i] = i;
            std::sort(ord.begin(), ord.end(), [&](size_t a2, size_t b2) { return mm.prob[a2] > mm.prob[b2]; });
            const int nShow = 3 - int(mm.unlike);
            double rest = 0;
            for (size_t k = 0; k < ord.size(); ++k) {
                const size_t i = ord[k];
                if (int(k) >= nShow) { rest += mm.prob[i]; continue; }
                const Color col = matColor(lib, mm.labels[i]);
                c->roundRect(Rect(idr.x + 14, yy + 4, 10, 10), 2, col);
                c->text(idr.x + 30, yy + 14, gfx::Canvas::elide(mm.labels[i], SB14, 150), SB14, th::text);
                const Rect bar(idr.x + 186, yy + 2, idr.w - 256, 16);
                c->roundRect(bar, 3, th::surfaceIn);
                const long bw = std::lround(bar.w * std::clamp(mm.prob[i], 0.0, 1.0));
                if (bw > 0) c->roundRect(Rect(bar.x, bar.y, int(std::max(4L, bw)), bar.h), 3, col, mm.unlike ? 110 : 255);
                const double p = mm.prob[i] * 100;
                char pb[16];
                if (p > 0 && p < 1) std::snprintf(pb, sizeof pb, "<1 %%");
                else std::snprintf(pb, sizeof pb, "%.0f %%", p);
                c->label(Rect(idr.x + idr.w - 70, yy, 56, 20), pb, SB14, th::text, gfx::Right);
                yy += 28;
            }
            if (rest > 0.005) {
                char b[64];
                std::snprintf(b, sizeof b, "the other %d: %.0f %%", int(ord.size()) - nShow, rest * 100);
                c->text(idr.x + 30, yy + 14, b, S13, th::textFaint);
                yy += 26;
            }
            // H7-2.8.0: the blend
            if (mm.w.size() == mm.labels.size() && std::isfinite(mm.blendD2)) {
                std::string t = "As a blend of their curves: ";
                std::vector<size_t> ow(mm.w.size());
                for (size_t i = 0; i < ow.size(); ++i) ow[i] = i;
                std::sort(ow.begin(), ow.end(), [&](size_t a2, size_t b2) { return mm.w[a2] > mm.w[b2]; });
                bool first = true;
                for (size_t i : ow) {
                    if (mm.w[i] < 0.02) continue;
                    char b[96];
                    std::snprintf(b, sizeof b, "%s%.0f %% %s", first ? "" : " + ", mm.w[i] * 100, mm.labels[i].c_str());
                    t += b;
                    first = false;
                }
                t += mm.blendD2 > mm.d2Limit ? ". No blend of them reproduces it either." : ".";
                const int h = gfx::Canvas::wrapHeight(idr.w - 28, t, S13, 4, 1.25f);
                c->textBox(Rect(idr.x + 14, yy + 2, idr.w - 28, h), t, S13, th::textDim, gfx::Left, 4, 1.25f);
                yy += h + 8;
            }
            if (mm.rangeDiff) {
                char b[200];
                std::snprintf(b, sizeof b, "On another gain range than %s's sweeps at %d frequenc%s: the phase there may step.",
                              mm.labels[size_t(mm.best)].c_str(), mm.rangeDiff, mm.rangeDiff == 1 ? "y" : "ies");
                const int h = gfx::Canvas::wrapHeight(idr.w - 28, b, S12, 3, 1.2f);
                c->textBox(Rect(idr.x + 14, yy, idr.w - 28, h), b, S12, th::warn, gfx::Left, 3, 1.2f);
                yy += h + 6;
            }
            if (mm.unlike) {
                char b[300];
                std::snprintf(b, sizeof b, "LIKE NONE OF THEM: farther from every material than its own sweeps ever are (distance %.0f, "
                              "limit %.0f). The per cent only say which is the least unlike. Label it if it is a material of its own.",
                              mm.d2[size_t(mm.best)], mm.d2Limit);
                c->textBox(Rect(idr.x + 14, yy + 4, idr.w - 28, idr.y + idr.h - yy - 10), b, SB12, th::warn, gfx::Left, 0, 1.25f);
            }
        }
        y += (narrow ? 350 + 340 : 340) + 12;
    };

    // ---- the labels, and the one chosen
    auto labels = [&]() {
        c->text(body.x + 4, y + 20, "MATERIALS", SB12, th::textFaint);
        c->text(body.x + 100, y + 20, gfx::Canvas::elide("tap one to sweep it; a label with fewer than 2 sweeps is not in the classifier yet",
                                                         S12, W - 100), S12, th::textFaint);
        y += 30;
        int x = body.x;
        for (size_t i = 0; i < lib.labels.size(); ++i) {
            const std::string& l = lib.labels[i];
            char b[96];
            std::snprintf(b, sizeof b, "    %s  %d%s", gfx::Canvas::elide(l, SB16, 220).c_str(), lib.count(l),
                          lib.isOff(l) ? "  off" : "");      // H7-2.12.0: left out of the comparison
            const int w = gfx::Canvas::measure(b, SB16) + 30;
            if (x + w > body.x + W) { x = body.x; y += 52; }
            if (u->chip(ID_M_CHIP + int(i), Rect(x, y, w, 44), b, m_matSel == l)) { m_matSel = l; m_matDelLabArm = 0; }
            if (m_matSel == l) c->circle(x + 18, y + 22, 8, th::inkOnAccent);
            c->circle(x + 18, y + 22, 6, kMat[i % 8]);
            x += w + 8;
        }
        {
            const int w = gfx::Canvas::measure("+ New label", SB16) + 30;
            if (x + w > body.x + W) { x = body.x; y += 52; }
            if (u->chip(ID_M_NEW, Rect(x, y, w, 44), "+ New label", false)) {
                openText("New material: its name", "", 40, [this](const std::string& v) {
                    const std::string w2 = a.matAddLabel(v);
                    if (!w2.empty()) { say(w2, true); return; }
                    m_matSel = v;
                    while (!m_matSel.empty() && m_matSel.back() == ' ') m_matSel.pop_back();
                    while (!m_matSel.empty() && m_matSel[0] == ' ') m_matSel.erase(0, 1);
                    say("\"" + m_matSel + "\" added. Put the tip in it and press Sweep.");
                }, "For example: Saline 0.9 %, Saline 0.09 %, Apple, Phantom A. Once made, a label can be chosen again at any time.");
            }
        }
        y += 56;
        if (m_matSel.empty()) return;
        // ---- the chosen label
        const int n = lib.count(m_matSel), np = lib.placements(m_matSel);
        std::vector<std::string> needles;
        for (const birx::MatSweep& s : lib.sweeps)
            if (s.label == m_matSel && !s.needle.empty() && std::find(needles.begin(), needles.end(), s.needle) == needles.end())
                needles.push_back(s.needle);
        char nb[96] = "";
        if (!needles.empty()) std::snprintf(nb, sizeof nb, ", %d needle%s", int(needles.size()), needles.size() == 1 ? "" : "s");
        char b[400];
        std::snprintf(b, sizeof b, "%d sweep%s from %d placement%s%s.%s", n, n == 1 ? "" : "s", np, np == 1 ? "" : "s", nb,
                      n < 2 ? " Two sweeps at least before it joins the classifier."
                            : np < 3 ? " Better: three placements or more, with other needles and on other days." : "");
        const bool off = lib.isOff(m_matSel);
        // ITDS-2.16.0: on a narrow screen the two sweep buttons share a line, Delete on the next
        const bool narrow = W < 800;
        const int sw2 = narrow ? (W - 28 - 10) / 2 : 300;
        const Rect r(body.x, y, W, 128 + 60 + (narrow ? 58 : 0));
        u->card(r, th::surface, matColor(lib, m_matSel));
        {   // H7-2.12.3: a sweep of this material under way, round its card
            double f = 0; Color col = th::accent;
            if (a.matRunLabel() == m_matSel && matRing(&f, &col) && f > 0) progressRing(r, f, col, th::radiusSm);
        }
        c->text(r.x + 14, r.y + 28, gfx::Canvas::elide(m_matSel, SB19, std::min(600, r.w - 28)), SB19, th::text);
        c->text(r.x + 14, r.y + 52, narrow ? gfx::Canvas::elide(b, S13, r.w - 28) : std::string(b), S13, th::textDim);
        const bool can = a.matBlocker().empty() && !a.matTesting();
        const bool sameOk = !lib.sweeps.empty() && lib.sweeps.back().label == m_matSel;
        const int by = r.y + 68;
        // H7-2.13.0: when the needle's check comes first, the button does it here (and comes back here once it passes)
        const app::App::NeedleStep step = a.matTesting() || a.matRefRunning() ? app::App::NeedleStep::None : a.matNextStep();
        if (step != app::App::NeedleStep::None) {
            const char* lab = step == app::App::NeedleStep::Air ? "Air check first" : step == app::App::NeedleStep::Saline
                                  ? (a.qcCheckOk() ? "Saline check (due)" : "Saline check first") : "Enter the needle's number";
            if (u->button(ID_M_SWEEP, Rect(r.x + 14, by, sw2, 48), lab, true, true)) needleStep(int(step));
        } else {
            u->why(ID_M_SWEEP, a.matTesting() ? std::string(kWhyBusy) : a.matBlocker());
            if (u->button(ID_M_SWEEP, Rect(r.x + 14, by, sw2, 48), narrow ? "Sweep: new placement" : "Sweep it: a new placement", true, can)) {
                const std::string w = a.matStart(m_matSel, true);
                if (!w.empty()) say(w, true); else m_msg.clear();
            }
        }
        u->why(ID_M_AGAIN, !can ? (a.matTesting() ? std::string(kWhyBusy) : a.matBlocker()) : std::string("The same placement goes on from the last sweep, which was of another material (or there is none): use Sweep it: a new placement."));
        if (u->button(ID_M_AGAIN, Rect(r.x + 14 + sw2 + (narrow ? 10 : 10), by, sw2, 48), "Again, same placement", false, can && sameOk)) {
            const std::string w = a.matStart(m_matSel, false);
            if (!w.empty()) say(w, true); else m_msg.clear();
        }
        ui::Ui::Btn st;
        st.primary = false; st.danger = delLabArmed; st.enabled = !a.matTesting();
        u->why(ID_M_DELLAB, kWhyBusy);
        if (u->button(ID_M_DELLAB, Rect(r.r() - 14 - 250, narrow ? by + 58 : by, 250, 48), delLabArmed ? "Tap again: delete all" : "Delete this label", st)) {
            if (!delLabArmed) m_matDelLabArm = now | 1;
            else {
                m_matDelLabArm = 0;
                const std::string l = m_matSel;
                const std::string w = a.matDeleteLabel(l);
                if (!w.empty()) say(w, true);
                else { say("\"" + l + "\" and its " + std::to_string(n) + " sweeps are deleted."); m_matSel.clear(); }
            }
        }
        // H7-2.12.0: in the comparison, or left out of it (kept: its sweeps are neither learnt nor answered)
        {
            const int ty = by + 58 + (narrow ? 58 : 0);
            c->fill(Rect(r.x + 14, ty - 6, r.w - 28, 1), th::stroke);
            const std::string tt = off ? "LEFT OUT of the comparison: its sweeps are kept, but not learnt, and it is never an answer."
                                       : "In the comparison: learnt, and one of the answers. Leave it out to compare the others "
                                         "among themselves (each material makes the others' spread too).";
            c->textBox(Rect(r.x + 14, ty, r.w - 28 - 220, 46), tt, S13, off ? th::warn : th::textDim, gfx::Left, 2, 1.25f);
            if (u->toggle(ID_M_OFF, Rect(r.r() - 14 - 200, ty, 200, 44), !off)) {
                const std::string w = a.matSetOff(m_matSel, !off);
                if (!w.empty()) say(w, true);
            }
        }
        y += r.h + 8;
        y += c->textBox(Rect(body.x + 4, y, W - 8, 80),
            "A new placement: the tip put in afresh (another piece, another spot, another needle). Same placement: one more sweep "
            "without moving it. The quality test leaves a whole placement out at a time, so it is the number of placements that "
            "counts. Take each material with several needles, cables and days: what they change by themselves is then learnt as "
            "part of each material.", S13, th::textDim, gfx::Left, 0, 1.3f) + 12;
    };
    // before the classifier is learnt, the labels come first: they are what it needs
    if (!md.ok) { labels(); curves(); }
    else { curves(); labels(); }

    // ---- how good the classifier is
    {
        int h = 0;
        std::string t;
        char b[400];
        if (md.ok) {
            int nsw = 0;
            for (const birx::MatSweep& s : lib.sweeps)
                if (std::find(md.labels.begin(), md.labels.end(), s.label) != md.labels.end()) ++nsw;
            // H7-2.14.1: the sweeps it learnt from, not every sweep of its materials (the others: suspect, waiting, aside)
            std::snprintf(b, sizeof b, "Learnt from %d of the %d sweeps of %d materials%s. Each placement left out in turn, its "
                          "sweeps said by a classifier made without it: %d of %d sweeps right (%.0f %%).", md.nSweeps, nsw,
                          int(md.labels.size()), md.nSweeps < nsw ? " (the others are not learnt: QC, below)" : "",
                          md.nCorrect, md.nTested, md.nTested ? 100.0 * md.nCorrect / md.nTested : 0.0);
            t = b;
            std::snprintf(b, sizeof b, " From %d placements, no answer is given more than %.0f %%: label more placements, with "
                          "other needles and on other days, and it can be surer.", md.nPlacements,
                          100.0 * ((1 - md.eps) + md.eps / double(std::max<size_t>(1, md.labels.size()))));
            t += b;
            // H7-2.8.0: each answer, the same placements left out
            if (md.nTested) {
                std::snprintf(b, sizeof b, "\nLEFT OUT, EACH WAY: whole curve %d of %d \xC2\xB7 blend %d of %d", md.nCorrect, md.nTested,
                              md.nBlendCorrect, md.nTested);
                t += b;
                if (md.zips.ok) {
                    std::snprintf(b, sizeof b, " \xC2\xB7 Z and IPS %d of %d", md.zips.nCorrect, md.zips.nTested);
                    t += b;
                }
                if (md.stack.ok) {
                    std::snprintf(b, sizeof b, " \xC2\xB7 FINAL %d of %d (it weighs them %.2f : %.2f : %.2f).", md.stack.nCorrect,
                                  md.stack.nTested, md.stack.w[0], md.stack.w[1], md.stack.w[2]);
                    t += b;
                } else t += ".";
            }
            int right = 0;
            const int told = lib.told(&right);
            if (told) {
                std::snprintf(b, sizeof b, "\nIdentified first and labelled after (sweeps it had not learnt): %d of %d said right by the whole curve", right, told);
                t += b;
                int rf = 0;
                const int tf = lib.toldFinal(&rf);
                if (tf) { std::snprintf(b, sizeof b, ", %d of %d by the final answer.", rf, tf); t += b; }
                else t += ".";
            }
        } else {
            t = md.why;
        }
        if (md.nTipFault) {
            std::snprintf(b, sizeof b, "\nTIP: %d sweep%s taken with a shorted or bridged tip, not learnt (below).", md.nTipFault,
                          md.nTipFault == 1 ? "" : "s");
            t += b;
        }
        // H7-2.10.0: the needle checks that closed the sweeps
        {   // H7-2.12.0: a sweep is learnt once a saline check of its needle has confirmed it
            const int open = a.qcPending(), waiting = a.qcWaiting(), never = a.qcUnconfirmed();
            int rawOnly = 0;
            for (const birx::MatSweep& sw : lib.sweeps) if (sw.qc == "ok-raw") ++rawOnly;
            if (md.nQcSuspect || waiting || never || rawOnly) {
                std::snprintf(b, sizeof b, "\nQC: %d sweep%s SUSPECT (the needle's next saline check found it moved), not learnt.",
                              md.nQcSuspect, md.nQcSuspect == 1 ? "" : "s");
                t += b;
                if (rawOnly) {   // H7-2.12.6
                    std::snprintf(b, sizeof b, " %d learnt as measured only (R held, the tip surface moved: not needle-normalised).",
                                  rawOnly);
                    t += b;
                }
                if (open) { std::snprintf(b, sizeof b, " %d of this needle wait%s for its next check, and %s learnt once it confirms them.",
                                          open, open == 1 ? "s" : "", open == 1 ? "is" : "are"); t += b; }
                if (waiting > open) {
                    std::snprintf(b, sizeof b, " %d of other needles wait for their own next check.", waiting - open);
                    t += b;
                }
                if (never) {
                    std::snprintf(b, sizeof b, " %d no check on record can close (taken before H7-2.12.0, or its record lost): "
                                  "not learnt (below).", never);
                    t += b;
                }
            }
        }
        if (md.nOff) {
            std::snprintf(b, sizeof b, "\nLEFT OUT: %d sweep%s of materials left out of the comparison.", md.nOff, md.nOff == 1 ? "" : "s");
            t += b;
        }
        // H7-2.6.0: the same, from the sweeps taken with a needle reference, compared needle-normalised
        {
            const birx::MatModel& mn = a.matModelN();
            if (mn.ok)
                std::snprintf(b, sizeof b, "\nNEEDLE-NORMALISED (the %d sweeps taken with a saline reference): %d of %d right (%.0f %%). "
                              "A sweep of a needle with a reference is compared this way.", mn.nSweeps, mn.nCorrect, mn.nTested,
                              mn.nTested ? 100.0 * mn.nCorrect / mn.nTested : 0.0);
            else
                std::snprintf(b, sizeof b, "\nNEEDLE-NORMALISED: not yet. It needs at least two materials with two sweeps each taken after "
                              "a saline reference of their needle.");
            t += b;
        }
        const int tw = W - 28;
        const int th1 = gfx::Canvas::wrapHeight(tw, t, S14, 12, 1.3f);
        const size_t L = md.ok ? md.labels.size() : 0;
        const bool table = L >= 2 && L <= 8 && md.confusion.size() == L;
        const int tabH = table ? 26 + int(L) * 24 + 8 : 0;
        h = 40 + th1 + tabH + 14;
        const Rect r(body.x, y, W, h);
        u->card(r, th::surface, th::stroke);
        c->text(r.x + 14, r.y + 24, "HOW WELL IT TELLS THEM APART", SB12, th::textFaint);
        c->textBox(Rect(r.x + 14, r.y + 34, tw, th1), t, S14, md.ok ? th::text : th::textDim, gfx::Left, 12, 1.3f);
        if (table) {
            const int tx = r.x + 14, ty = r.y + 40 + th1 + 6, cw = std::min(100, (W - 28 - 200) / int(L));
            c->text(tx, ty + 16, "learnt, left out: was \xE2\x86\x93 said \xE2\x86\x92", S12, th::textFaint);
            for (size_t j = 0; j < L; ++j)
                c->label(Rect(tx + 200 + int(j) * cw, ty, cw, 22), gfx::Canvas::elide(md.labels[j], SB11, cw - 6), SB11,
                         matColor(lib, md.labels[j]), gfx::Center);
            for (size_t i = 0; i < L; ++i) {
                const int ry = ty + 26 + int(i) * 24;
                c->text(tx, ry + 16, gfx::Canvas::elide(md.labels[i], S13, 190), S13, matColor(lib, md.labels[i]));
                for (size_t j = 0; j < L; ++j) {
                    const int v = (i < md.confusion.size() && j < md.confusion[i].size()) ? md.confusion[i][j] : 0;
                    const Rect cell(tx + 200 + int(j) * cw + 2, ry, cw - 4, 22);
                    if (v) c->roundRect(cell, 3, i == j ? th::goodSoft : th::badSoft);
                    c->label(cell, v ? std::to_string(v) : std::string("\xC2\xB7"), SB12, v ? (i == j ? th::good : th::bad) : th::textFaint,
                             gfx::Center);
                }
            }
        }
        y += h + 8;
    }

    // ---- H7-2.14.1: the Identify tests, as they were said at the time: each sweep identified first and labelled after
    // (WHAT WAS IT REALLY?), whether it is learnt yet or not
    {
        std::vector<std::string> rows, cols;
        bool none = false;
        int n = 0, right = 0;
        auto saidOf = [](const birx::MatSweep& s) { return s.saidFRight >= 0 && !s.saidF.empty() ? s.saidF : s.said; };
        for (const birx::MatSweep& s : lib.sweeps) {
            if (s.saidRight < 0 && s.saidFRight < 0) continue;
            ++n;
            if ((s.saidFRight >= 0 ? s.saidFRight : s.saidRight) == 1) ++right;
            if (std::find(rows.begin(), rows.end(), s.label) == rows.end()) rows.push_back(s.label);
            const std::string sd = saidOf(s);
            if (sd == "(none)" || sd.empty()) none = true;
            else if (std::find(cols.begin(), cols.end(), sd) == cols.end()) cols.push_back(sd);
        }
        auto byLib = [&](std::vector<std::string>* v) {
            std::vector<std::string> o;
            for (const std::string& l : lib.labels) if (std::find(v->begin(), v->end(), l) != v->end()) o.push_back(l);
            for (const std::string& l : *v) if (std::find(o.begin(), o.end(), l) == o.end()) o.push_back(l);
            *v = o;
        };
        byLib(&rows); byLib(&cols);
        if (none) cols.push_back("(none)");
        if (n > 0) {
            char b[400];
            std::snprintf(b, sizeof b, "Swept with Sweep it and compare, then told what it really was: %d of %d said right (by the "
                          "final answer when it was given). Counted as they were said at the time, whether learnt yet or not; "
                          "they join the table above once a saline check confirms them.", right, n);
            const int tw = W - 28;
            const int th1 = gfx::Canvas::wrapHeight(tw, b, S14, 5, 1.3f);
            const size_t R = rows.size(), C = cols.size();
            const bool table = R >= 1 && R <= 8 && C >= 1 && C <= 8;
            const int tabH = table ? 26 + int(R) * 24 + 8 : 0;
            const int h = 40 + th1 + tabH + 14;
            const Rect r(body.x, y, W, h);
            u->card(r, th::surface, th::stroke);
            c->text(r.x + 14, r.y + 24, "IDENTIFY TESTS", SB12, th::textFaint);
            c->textBox(Rect(r.x + 14, r.y + 34, tw, th1), b, S14, th::text, gfx::Left, 5, 1.3f);
            if (table) {
                const int tx = r.x + 14, ty = r.y + 40 + th1 + 6, cw = std::min(100, (W - 28 - 200) / int(C));
                c->text(tx, ty + 16, "was \xE2\x86\x93  said \xE2\x86\x92", S12, th::textFaint);
                for (size_t j = 0; j < C; ++j)
                    c->label(Rect(tx + 200 + int(j) * cw, ty, cw, 22), gfx::Canvas::elide(cols[j], SB11, cw - 6), SB11,
                             cols[j] == "(none)" ? th::textDim : matColor(lib, cols[j]), gfx::Center);
                for (size_t i = 0; i < R; ++i) {
                    const int ry = ty + 26 + int(i) * 24;
                    c->text(tx, ry + 16, gfx::Canvas::elide(rows[i], S13, 190), S13, matColor(lib, rows[i]));
                    for (size_t j = 0; j < C; ++j) {
                        int v = 0;
                        for (const birx::MatSweep& s : lib.sweeps) {
                            if ((s.saidRight < 0 && s.saidFRight < 0) || s.label != rows[i]) continue;
                            const std::string sd = saidOf(s);
                            if (sd == cols[j] || (cols[j] == "(none)" && (sd.empty() || sd == "(none)"))) ++v;
                        }
                        const bool diag = rows[i] == cols[j];
                        const Rect cell(tx + 200 + int(j) * cw + 2, ry, cw - 4, 22);
                        if (v) c->roundRect(cell, 3, diag ? th::goodSoft : th::badSoft);
                        c->label(cell, v ? std::to_string(v) : std::string("\xC2\xB7"), SB12, v ? (diag ? th::good : th::bad) : th::textFaint,
                                 gfx::Center);
                    }
                }
            }
            y += h + 8;
        }
    }

    // ---- H7-2.10.0: how often the needle is checked
    {
        char b[800];
        std::snprintf(b, sizeof b, "A saline check every %d sweeps or %d minutes, whichever comes first, and an Air check every %d "
                      "minutes and whenever the needle or its cable changes. Each check passes or not by itself (the limits), and "
                      "also closes the sweeps since the one before: when the needle's R held (within 8 %%) they are confirmed and "
                      "learnt \xE2\x80\x94 needle-normalised too when its tip surface held as well (within 20 %%); when R moved, "
                      "they are marked suspect and not learnt. A needle sweeps from its first passing check on (H7-2.13.0).",
                      a.settings().qcSweeps, a.settings().qcMinutes, a.settings().qcAirMinutes);
        const int h = settingRow(Rect(body.x, y, W, 0), "How often", b, 330);
        const int yy = m_rowR.y + (m_rowR.h - 44) / 2, x0 = m_rowR.x;
        char l1[24], l2[24], l3[24];
        std::snprintf(l1, sizeof l1, "%d sweeps", a.settings().qcSweeps);
        std::snprintf(l2, sizeof l2, "%d min", a.settings().qcMinutes);
        std::snprintf(l3, sizeof l3, "Air %d min", a.settings().qcAirMinutes);
        const bool can = !a.matTesting() && !a.matRefRunning();
        u->why(ID_Q_SWEEPS, kWhyBusy); u->why(ID_Q_MIN, kWhyBusy); u->why(ID_Q_AIR, kWhyBusy);
        if (u->button(ID_Q_SWEEPS, Rect(x0, yy, 100, 44), l1, false, can))
            openNumber("A saline check every ... sweeps", std::to_string(a.settings().qcSweeps), "sweeps", 1, 200, true, false, false,
                       [this](const std::string& v) { const app::Settings& s2 = a.settings();
                                                      const std::string w = a.setQcIntervals(std::atoi(v.c_str()), s2.qcMinutes, s2.qcAirMinutes);
                                                      if (!w.empty()) say(w, true); });
        if (u->button(ID_Q_MIN, Rect(x0 + 110, yy, 100, 44), l2, false, can))
            openNumber("... or every ... minutes", std::to_string(a.settings().qcMinutes), "min", 1, 600, true, false, false,
                       [this](const std::string& v) { const app::Settings& s2 = a.settings();
                                                      const std::string w = a.setQcIntervals(s2.qcSweeps, std::atoi(v.c_str()), s2.qcAirMinutes);
                                                      if (!w.empty()) say(w, true); });
        if (u->button(ID_Q_AIR, Rect(x0 + 220, yy, 110, 44), l3, false, can))
            openNumber("An Air check every ... minutes", std::to_string(a.settings().qcAirMinutes), "min", 1, 1440, true, false, false,
                       [this](const std::string& v) { const app::Settings& s2 = a.settings();
                                                      const std::string w = a.setQcIntervals(s2.qcSweeps, s2.qcMinutes, std::atoi(v.c_str()));
                                                      if (!w.empty()) say(w, true); });
        y += h + 8;
    }

    // ---- H7-2.11.0: the sweeps taken with a shorted or bridged tip
    if (md.nTipFault > 0) {
        char b[520];
        std::snprintf(b, sizeof b, "%d sweep%s of the library %s taken with the needle's tip shorted (|Z| flat at the instrument's "
                      "floor) or bridged by residue (lower at 500 kHz than the same needle in 0.9 %% saline): the needle measured "
                      "itself, not the material. %s not learnt. Delete them to take them out of the library for good.",
                      md.nTipFault, md.nTipFault == 1 ? "" : "s", md.nTipFault == 1 ? "was" : "were", md.nTipFault == 1 ? "It is" : "They are");
        const int h = settingRow(Rect(body.x, y, W, 0), "Sweeps of a faulty tip", b, 260);
        ui::Ui::Btn st;
        st.primary = false; st.danger = delTipArmed; st.enabled = !a.matTesting() && !a.matRefRunning();
        u->why(ID_Q_DELTIP, kWhyBusy);
        if (u->button(ID_Q_DELTIP, Rect(m_rowR.r() - 260, m_rowR.y + (m_rowR.h - 44) / 2, 260, 44),
                      delTipArmed ? "Tap again to delete" : "Delete them (" + std::to_string(md.nTipFault) + ")", st)) {
            if (!delTipArmed) m_matDelTipArm = now | 1;
            else { m_matDelTipArm = 0; const std::string w = a.matDeleteTipFaults(); say(w, w.find("are deleted") == std::string::npos); }
        }
        y += h + 8;
    }

    // ---- H7-2.12.0: pending sweeps no check can close
    if (a.qcUnconfirmed() > 0) {
        const int nu = a.qcUnconfirmed();
        char b[520];
        std::snprintf(b, sizeof b, "%d sweep%s no saline check on record can confirm: taken before H7-2.12.0 without the check "
                      "after them, or with a needle whose record was lost. Not learnt. Learn them anyway only when you know the "
                      "needle stayed sound (the classifier's own check still sets aside any far from its material).", nu,
                      nu == 1 ? "" : "s");
        const int h = settingRow(Rect(body.x, y, W, 0), "Unconfirmed sweeps", b, 260);
        ui::Ui::Btn st;
        st.primary = false; st.danger = keepUncArmed; st.enabled = !a.matTesting() && !a.matRefRunning();
        u->why(ID_Q_KEEPUNC, kWhyBusy);
        if (u->button(ID_Q_KEEPUNC, Rect(m_rowR.r() - 260, m_rowR.y + (m_rowR.h - 44) / 2, 260, 44),
                      keepUncArmed ? "Tap again to learn them" : "Learn them anyway (" + std::to_string(nu) + ")", st)) {
            if (!keepUncArmed) m_matKeepUncArm = now | 1;
            else { m_matKeepUncArm = 0; const std::string w = a.matKeepUnconfirmed(); say(w, w.find("learnt") == std::string::npos); }
        }
        y += h + 8;
    }

    // ---- H7-2.12.5: the sweeps marked suspect
    if (md.nQcSuspect > 0) {
        char b[700];
        const int rj = a.qcRejudged();
        char r2[160] = "";
        if (rj > 0) std::snprintf(r2, sizeof r2, " At this start %d more were judged again by the H7-2.15.0 rules and learnt.", rj);
        std::snprintf(b, sizeof b, "%d sweep%s marked SUSPECT, so %s not learnt: the needle's saline check after %s found its R "
                      "moved more than 8 %%, or its tip fouled, or a check refused it, or its leakage was over its limits (a worn "
                      "needle). Kept so also: sweeps from before H7-2.12.6 with an Air check before the check that closed them "
                      "(the tip may have been cleaned for a fault).%s Learn them anyway only if you know the needle was sound.",
                      md.nQcSuspect, md.nQcSuspect == 1 ? " is" : "s are", md.nQcSuspect == 1 ? "it is" : "they are",
                      md.nQcSuspect == 1 ? "it" : "them", r2);
        const int h = settingRow(Rect(body.x, y, W, 0), "Suspect sweeps", b, 260);
        ui::Ui::Btn st;
        st.primary = false; st.danger = keepSusArmed; st.enabled = !a.matTesting() && !a.matRefRunning();
        u->why(ID_Q_KEEPSUS, kWhyBusy);
        if (u->button(ID_Q_KEEPSUS, Rect(m_rowR.r() - 260, m_rowR.y + (m_rowR.h - 44) / 2, 260, 44),
                      keepSusArmed ? "Tap again to learn them" : "Learn them anyway (" + std::to_string(md.nQcSuspect) + ")", st)) {
            if (!keepSusArmed) m_matKeepSusArm = now | 1;
            else { m_matKeepSusArm = 0; const std::string w = a.matKeepSuspect(); say(w, w.find("learnt by hand") == std::string::npos); }
        }
        y += h + 8;
    }

    // ---- the main screen
    {
        const int h = settingRow(Rect(body.x, y, W, 0), "Show it on the main screen",
            "After every sweep the phase box of the main screen shows which labelled material the curve is most like, by "
            "the whole curve and by the blend (tap it for the phase curve). It does not change the verdict.", 200);
        const Rect r = m_rowR;
        if (u->toggle(ID_M_MAIN, r, a.matOnMain())) { const std::string w = a.setMatOnMain(!a.matOnMain()); if (!w.empty()) say(w, true); }
        y += h + 8;
    }

    // ---- H7-2.5.3: the whole library to and from a USB stick
    {
        char b[300];
        std::snprintf(b, sizeof b, "Every sweep and label here, the tagged readings of the logs among them (%d), to a USB stick; and "
                      "every library file on a stick ADDED to this one \xE2\x80\x94 nothing here is deleted, a sweep already here is "
                      "not added twice.%s", a.matTaggedReadings(),
                      a.matFromOtherDevices() ? (" " + std::to_string(a.matFromOtherDevices()) + " sweeps here came from other devices.").c_str() : "");
        const int h = settingRow(Rect(body.x, y, W, 0), "Training data", b, 380);
        const int yy = m_rowR.y + (m_rowR.h - 44) / 2, x0 = m_rowR.x;
        u->why(ID_M_EXP, kWhyBusy); u->why(ID_M_IMP, kWhyBusy);
        if (u->button(ID_M_EXP, Rect(x0, yy, 184, 44), "Export to USB", false, !a.matTesting())) {
            const std::string m = a.matExport(); say(m, m.find("Written") != 0);
        }
        if (u->button(ID_M_IMP, Rect(x0 + 196, yy, 184, 44), "Import from USB", false, !a.matTesting())) {
            const std::string m = a.matImport(); say(m, m.find("Added") != 0);
        }
        y += h + 8;
    }

    // ---- the library
    {
        char b[300];
        if (lib.sweeps.empty()) std::snprintf(b, sizeof b, "No sweep kept yet. The library is records/materials.txt; Delete logs keeps it.");
        else {
            const birx::MatSweep& s = lib.sweeps.back();
            std::string when = s.when.substr(0, 16);
            if (when.size() > 10 && when[10] == 'T') when[10] = ' ';
            std::snprintf(b, sizeof b, "%d sweeps of %d labels in records/materials.txt (Delete logs keeps it). The last: %s, %s%s%s.",
                          int(lib.sweeps.size()), int(lib.labels.size()), s.label.c_str(), when.c_str(),
                          s.needle.empty() ? "" : ", needle ", s.needle.c_str());
        }
        const int h = settingRow(Rect(body.x, y, W, 0), "Library", b, 250);
        const Rect rr = m_rowR;
        ui::Ui::Btn st;
        st.primary = false; st.danger = delLastArmed; st.enabled = !lib.sweeps.empty() && !a.matTesting();
        u->why(ID_M_DELLAST, lib.sweeps.empty() ? std::string("The library has no sweep to delete.") : std::string(kWhyBusy));
        if (u->button(ID_M_DELLAST, Rect(rr.r() - 250, rr.y + (rr.h - 44) / 2, 250, 44), delLastArmed ? "Tap again to delete it" : "Delete the last sweep", st)) {
            if (!delLastArmed) m_matDelLastArm = now | 1;
            else { m_matDelLastArm = 0; const std::string w = a.matDeleteLast(); say(w, w.find("is deleted") == std::string::npos); }
        }
        y += h + 8;
    }
    contentH = y + off - body.y;
    c->resetClip();
}

// H7-2.13.0: the needle's next step, from where the sweep is asked for; the sheet comes back here once it passes
void Gui::needleStep(int step)
{
    using S = app::App::NeedleStep;
    if (step == int(S::Number)) {
        openText("The needle's number", a.trainNeedle(), 20, [this](const std::string& v) {
            const std::string w2 = a.setTrainNeedle(v);
            if (!w2.empty()) say(w2, true); else m_msg.clear();
        }, "As printed on it, or one you write on it (N1, N2 ...): the same number every time that needle is used.");
        return;
    }
    m_calib = true; m_calDip = false; m_calRefStarted = false; m_calAutoClose = true; m_calDoneAt = 0;
    if (step == int(S::Air)) { a.calOpen(true); m_calRef = false; }
    else m_calRef = true;
}

// H7-2.12.3: a material sweep's settling (amber), then its sweep (blue)
bool Gui::matRing(double* f, Color* col) const
{
    if (!a.matTesting()) return false;
    if (a.matStage() == app::MatStage::Settling) {
        *f = 1.0 - double(a.matSecondsLeft()) / std::max(1, a.settings().settleS);
        *col = th::warn;
        return true;
    }
    *f = a.sweeping() ? a.sweepProgress() : 0.0;
    *col = th::accent;
    return true;
}

// ---------------------------------------------------------------------------
// H7-2.5.1: the main screen. After a sweep, the box under the Z-IPS map shows which labelled
// material the curve is most like. H7-2.8.0: on the right the three answers — by the whole
// curve, by the blend, by Z and IPS — and on the left the final one, the three together.
// false: nothing to show (the phase chart is drawn instead).
bool Gui::materialBox(const Rect& r0)
{
    if (!a.matOnMain() || a.sweeping()) return false;
    const birx::MatMatch& mm = a.matForCurrent();
    if (!mm.ok || mm.best < 0 || mm.labels.empty()) {
        // H7-2.12.1: the phase chart, and why no material is shown beside it (it was said nowhere)
        const std::string why = a.matMainWhy();
        if (why.empty() || !a.qcBlocker().empty()) return false;
        const int nh = 34;
        u->card(Rect(r0.x, r0.y, r0.w, nh), th::surface, th::stroke);
        c->text(r0.x + 10, r0.y + 14, "MATERIAL: NOT YET", SB11, th::textFaint);
        c->textBox(Rect(r0.x + 130, r0.y + 3, r0.w - 140, nh - 4), why, S11, th::textDim, gfx::Left, 2, 1.2f);
        phaseChart(Rect(r0.x, r0.y + nh + 2, r0.w, r0.h - nh - 2));
        return true;
    }
    if (m_phaseForSeq == a.sweepSeq()) {      // the phase chart, asked for by a tap
        phaseChart(r0);
        if (u->chip(ID_MB_MAT, Rect(r0.r() - 98, r0.y + 5, 92, 24), "Material", false)) m_phaseForSeq = 0;
        return true;
    }
    const birx::MatLibrary& lib = a.matLibrary();
    u->card(r0, th::surface, th::stroke);
    c->text(r0.x + 10, r0.y + 20, "MATERIAL", SB11, th::textFaint);
    c->text(r0.x + 78, r0.y + 20, std::string(mm.normalised ? "needle-normalised \xC2\xB7 " : "") +
            "like the labelled materials \xC2\xB7 not part of the verdict", S11, mm.normalised ? th::good : th::textFaint);
    if (u->chip(ID_MB_PHASE, Rect(r0.r() - 98, r0.y + 5, 92, 24), "Phase", false)) m_phaseForSeq = a.sweepSeq();
    const int top = r0.y + 28, bodyH = r0.b() - 6 - top;

    // ---- the final answer, on the left
    const Rect fin(r0.x + 8, top + 2, 262, bodyH - 4);
    u->card(fin, th::surfaceIn, th::stroke);
    c->text(fin.x + 10, fin.y + 16, "FINAL \xC2\xB7 THE THREE TOGETHER", SB11, th::textFaint);
    if (!mm.fOk || mm.fBest < 0) {
        c->textBox(Rect(fin.x + 10, fin.y + 26, fin.w - 20, fin.h - 30), "Not yet: every material needs two sweeps with their Z and IPS.",
                   S12, th::textDim, gfx::Left, 3, 1.25f);
    } else if (mm.unlike) {
        const std::string l = mm.labels[size_t(mm.fBest)];
        c->text(fin.x + 10, fin.y + 52, "Like none of them", SB19, th::warn);
        c->text(fin.x + 10, fin.y + 76, gfx::Canvas::elide("nearest: " + l, S12, fin.w - 20), S12, th::textDim);
    } else {
        const std::string l = mm.labels[size_t(mm.fBest)];
        const Color col = matColor(lib, l);
        char b[16];
        std::snprintf(b, sizeof b, "%.0f %%", mm.fProb[size_t(mm.fBest)] * 100);
        c->text(fin.x + 10, fin.y + 56, b, SB36, th::text);
        const int pw = gfx::Canvas::measure(b, SB36) + 12;
        c->roundRect(Rect(fin.x + 10 + pw, fin.y + 38, 12, 12), 3, col);
        c->text(fin.x + pw + 28, fin.y + 50, gfx::Canvas::elide(l, SB16, fin.w - pw - 38), SB16, col);
    }
    {
        const birx::MatModel& md = mm.normalised ? a.matModelN() : a.matModel();
        if (md.stack.ok && md.stack.nTested) {
            char b[96];
            std::snprintf(b, sizeof b, "left out: %d of %d right", md.stack.nCorrect, md.stack.nTested);
            c->text(fin.x + 10, fin.y + fin.h - 8, b, S11, th::textFaint);
        }
    }

    // ---- the three answers, on the right
    const int ax = fin.x + fin.w + 14, aw = r0.x + r0.w - 6 - ax;
    const int rowH = bodyH / 3;
    auto answer = [&](int yy, const std::string& head, double p, const std::string& l, bool unlike, bool dim) {
        c->text(ax, yy + 12, gfx::Canvas::elide(head, SB11, aw), SB11, th::textFaint);
        if (!unlike) {
            char b[16];
            std::snprintf(b, sizeof b, "%.0f %%", p * 100);
            c->text(ax, yy + 33, b, SB19, dim ? th::textDim : th::text);
            const int pw = gfx::Canvas::measure(b, SB19) + 8;
            const Color col = matColor(lib, l);
            c->roundRect(Rect(ax + pw, yy + 21, 9, 9), 2, col);
            c->text(ax + pw + 14, yy + 31, gfx::Canvas::elide(l, SB14, aw - pw - 14), SB14, col);
        } else {
            c->text(ax, yy + 32, "Like none of them", SB16, th::warn);
            const int lw = gfx::Canvas::measure("Like none of them", SB16);
            c->text(ax + lw + 8, yy + 32, gfx::Canvas::elide("nearest: " + l, S12, aw - (lw + 8)), S12, th::textDim);
        }
    };
    int yy = top;
    answer(yy, "BY THE WHOLE CURVE", mm.prob[size_t(mm.best)], mm.labels[size_t(mm.best)], mm.unlike, false);
    yy += rowH;
    c->fill(Rect(ax, yy, aw, 1), th::stroke);
    if (mm.w.size() == mm.labels.size()) {
        size_t bi = 0;
        for (size_t i = 0; i < mm.w.size(); ++i) if (mm.w[i] > mm.w[bi]) bi = i;
        const bool off = !std::isfinite(mm.blendD2) || mm.blendD2 > mm.d2Limit;
        answer(yy, off ? "BY THE BLEND (it does not fit either)" : "BY THE BLEND", mm.w[bi], mm.labels[bi], false, off);
    }
    yy += rowH;
    c->fill(Rect(ax, yy, aw, 1), th::stroke);
    if (mm.zOk && mm.zBest >= 0) {
        char b[96];
        std::snprintf(b, sizeof b, "BY Z AND IPS \xC2\xB7 %s, %.2f", ohmShort(mm.z1k).c_str(), mm.ips);
        answer(yy, b, mm.zProb[size_t(mm.zBest)], mm.labels[size_t(mm.zBest)], mm.zUnlike, false);
    } else {
        c->text(ax, yy + 12, "BY Z AND IPS", SB11, th::textFaint);
        c->text(ax, yy + 31, "not yet", S12, th::textDim);
    }
    return true;
}

// ---------------------------------------------------------------------------
// H7-2.5.3: a case of the logs tagged with what its lesion was (Logs -> + Tag the case): every
// reading of it becomes a sweep of the library with that label.
void Gui::tagSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 230);
    const Rect box = sheetBox(Rect(62, 60, 900, 480), 700);
    u->card(box, th::surface, th::accent, th::radius);
    const int x0 = box.x + 28;
    c->text(x0, box.y + 44, gfx::Canvas::elide(m_tagCase + ": what was its lesion?", SB24, box.w - 116), SB24, th::text);
    const Rect close(box.r() - 62, box.y + 14, 46, 44);
    if (u->clicked(ID_T_CLOSE, close, true)) { m_tagSheet = false; return; }
    u->card(close, u->pressed(ID_T_CLOSE) ? th::accentSoft : th::surface, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, th::textDim, gfx::Center);
    const app::CaseTag ct = a.caseTag(m_tagCase);
    const std::string cur = ct.mixed ? std::string() : ct.label;
    const std::string sub = m_tagWhen + " \xC2\xB7 the device said " + m_tagResult +
        (!ct.tagged ? std::string(" \xC2\xB7 not tagged")
                    : ct.mixed ? std::string(" \xC2\xB7 its readings have different tags: choose one for all")
                               : " \xC2\xB7 tagged: " + ct.label);
    c->text(x0, box.y + 68, gfx::Canvas::elide(sub, S14, box.w - 56), S14, ct.mixed ? th::warn : th::textDim);
    int y = box.y + 82;
    y += c->textBox(Rect(x0, y, box.w - 56, 80),
        "Every reading of the case that passed its checks joins the material library with the label chosen, its sweep as "
        "measured, and the classifier learns them at once. They are placements of one lesion, so they count as one sample: "
        "the quality test leaves the whole case out together. It does not change the case, its verdict or its pathology. "
        "Choose another label to change the tag.", S14, th::text, gfx::Left, 0, 1.3f) + 14;
    const birx::MatLibrary& lib = a.matLibrary();
    int x = x0;
    for (size_t i = 0; i < lib.labels.size(); ++i) {
        const std::string& l = lib.labels[i];
        const std::string t = "    " + gfx::Canvas::elide(l, SB16, 220);
        const int w = gfx::Canvas::measure(t, SB16) + 30;
        if (x + w > x0 + box.w - 56) { x = x0; y += 52; }
        if (y >= box.b() - 149) break;
        if (u->chip(ID_T_CHIP + int(i), Rect(x, y, w, 44), t, cur == l)) {
            const std::string w2 = a.tagCase(m_tagCase, l);
            if (w2.empty()) { m_logMsg = a.matMessage(); m_tagSheet = false; return; }
            m_tagMsg = w2;
        }
        c->circle(x + 18, y + 22, 6, matColor(lib, l));
        x += w + 8;
    }
    {
        const int w = gfx::Canvas::measure("+ New label", SB16) + 30;
        if (x + w > x0 + box.w - 56) { x = x0; y += 52; }
        if (u->chip(ID_T_NEW, Rect(x, y, w, 44), "+ New label", false)) {
            openText("What it was: a new material", "", 40, [this](const std::string& v) {
                const std::string w2 = a.matAddLabel(v);
                if (!w2.empty()) { m_tagMsg = w2; return; }
                std::string l = v;
                while (!l.empty() && l.back() == ' ') l.pop_back();
                while (!l.empty() && l[0] == ' ') l.erase(0, 1);
                const std::string w3 = a.tagCase(m_tagCase, l);
                if (w3.empty()) { m_logMsg = a.matMessage(); m_tagSheet = false; }
                else m_tagMsg = w3;
            }, "For example: Fat, Fibroglandular, Phantom A. Once made, the label is in Settings \xE2\x86\x92 Training too.");
        }
    }
    if (!m_tagMsg.empty())
        c->textBox(Rect(x0, y + 56, box.w - 56, 44), m_tagMsg, SB14, th::bad, gfx::Left, 2, 1.25f);
    if (ct.tagged) {
        ui::Ui::Btn st;
        st.primary = false; st.danger = true;
        if (u->button(ID_T_REMOVE, Rect(x0, box.b() - 80, 260, 52), "Remove the tag", st)) {
            const std::string w = a.tagCase(m_tagCase, "");
            if (w.empty()) { m_logMsg = a.matMessage(); m_tagSheet = false; return; }
            m_tagMsg = w;
        }
    }
    if (u->button(ID_T_CLOSE2, Rect(box.r() - 228, box.b() - 80, 200, 52), "Close", false, true)) m_tagSheet = false;
}

// ---------------------------------------------------------------------------
// H7-2.5.3: Logs -> Training data: the whole library to and from a USB stick.
void Gui::trainSheet()
{
    const birx::MatLibrary& lib = a.matLibrary();
    const birx::MatModel& md = a.matModel();
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 230);
    const Rect box = sheetBox(Rect(112, 80, 800, 440), 640);
    u->card(box, th::surface, th::accent, th::radius);
    const int x0 = box.x + 28, w = box.w - 56;
    c->text(x0, box.y + 44, "Training data", SB24, th::text);
    int right = 0;
    const int told = lib.told(&right);
    char b[400];
    std::snprintf(b, sizeof b, "The material library: %d sweeps of %d labels \xE2\x80\x94 %d of them tagged readings of the logs, %d from "
                  "other devices. %s", int(lib.sweeps.size()), int(lib.labels.size()), a.matTaggedReadings(), a.matFromOtherDevices(),
                  md.ok ? "" : md.why.c_str());
    int y = box.y + 68;
    y += c->textBox(Rect(x0, y, w, 80), b, S16, th::text, gfx::Left, 0, 1.3f) + 8;
    if (md.ok) {
        std::snprintf(b, sizeof b, "Left-out test: %d of %d sweeps right.%s", md.nCorrect, md.nTested,
                      told ? (" Tagged after being identified: " + std::to_string(right) + " of " + std::to_string(told) + " said right.").c_str() : "");
        y += c->textBox(Rect(x0, y, w, 40), b, S14, th::textDim, gfx::Left, 0, 1.3f) + 8;
    }
    const int h = c->textBox(Rect(x0, y, w, 120),
        "Export to USB writes all of it (every sweep with its label, needle, Cp and where it came from) as "
        "BIRADx_materials_<device>_<date>.txt. Import from USB ADDS every such file on the stick to this device's library: "
        "nothing here is deleted, a sweep already here is not added twice (one with another label here keeps this "
        "device's), and new labels are made. The classifier is learnt again from the whole of it. To tag a case with what "
        "its lesion was: open it in the log and press \"+ Tag the case\".", S14, th::textDim, gfx::Left, 0, 1.3f);
    if (!m_trainMsg.empty())
        c->textBox(Rect(x0, y + h + 10, w, 70), m_trainMsg, SB14, m_trainMsgBad ? th::bad : th::good, gfx::Left, 3, 1.25f);
    const int bw = (box.w - 80) / 3, by = box.b() - 80;
    u->why(ID_T_EXP, !plat::usbReady() ? std::string("No USB stick is mounted: plug one in (FAT32 or exFAT).") : std::string(kWhyBusy)); u->why(ID_T_IMP, !plat::usbReady() ? std::string("No USB stick is mounted: plug one in (FAT32 or exFAT).") : std::string(kWhyBusy));
    if (u->button(ID_T_EXP, Rect(x0, by, bw, 52), "Export to USB", true, plat::usbReady() && !a.matTesting())) {
        m_trainMsg = a.matExport();
        m_trainMsgBad = m_trainMsg.find("Written") != 0;
    }
    if (u->button(ID_T_IMP, Rect(x0 + bw + 12, by, bw, 52), "Import from USB", false, plat::usbReady() && !a.matTesting())) {
        m_trainMsg = a.matImport();
        m_trainMsgBad = m_trainMsg.find("Added") != 0;
    }
    if (u->button(ID_TR_CLOSE, Rect(x0 + 2 * (bw + 12), by, bw, 52), "Close", false, true)) m_trainSheet = false;
}

} // namespace gui
