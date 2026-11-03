// gui/gui_logs.cpp — LogsPage.qml, OutcomeSheet.qml, CaseEditSheet.qml.
#include "gui.h"
#include "../birxcore/patient.h"

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
    ID_G_CLOSE = 8000, ID_G_WHEN = 8010, ID_G_TAG = 8030, ID_G_SCROLL = 8050, ID_G_SHOWALL,
    ID_G_PATH, ID_G_EXPORT, ID_G_ALL, ID_G_DIAG, ID_G_DEL, ID_D_CANCEL, ID_D_COPY, ID_D_DEL, ID_G_ROW = 8100, ID_G_EDIT = 9900,
    ID_O_CASE = 10000, ID_O_RES = 10500, ID_O_DATE, ID_O_SRC, ID_O_NOTE, ID_O_SAVE, ID_O_CANCEL, ID_O_SCROLL,
    ID_E_CANCEL = 11000, ID_E_SAVE, ID_E_SCROLL, ID_E_FIELD = 11100, ID_E_PAGE = 11050,
    ID_G_READING = 12000, ID_G_SAMPLE = 12600, ID_G_TAGCASE = 10700,
    ID_G_TRAIN = 14900, ID_A_COPY = 14950, ID_A_PREP, ID_A_NEXT, ID_A_CLOSE,
};
struct Win { const char* k; const char* t; };
const Win kWhen[] = { { "today", "Today" }, { "yest", "Yesterday" }, { "d3", "Last 3 days" }, { "d7", "Last 7 days" },
                      { "m1", "Last month" }, { "y1", "Last year" }, { "all", "All" } };
const Win kTags[] = { { "all", "All" }, { "High Risk", "High Risk" }, { "Suspicious", "Suspicious" },
                      { "Low Risk", "Low Risk" }, { "Invalid", "Invalid" }, { "malignant", "Malignant" },
                      { "benign", "Benign" }, { "borderline", "Borderline" }, { "awaiting", "Awaiting path." } };

Color tierColorOf(const std::string& tier)
{
    if (tier == "High Risk") return th::bad;
    if (tier == "Suspicious") return th::warn;
    if (tier == "Low Risk") return th::good;
    return th::textDim;
}

int badge(gfx::Canvas* c, int rightX, int y, const std::string& text, Color col, bool filled)
{
    const int w = gfx::Canvas::measure(text, SB12) + 18;
    const Rect r(rightX - w, y, w, 24);
    if (filled) { c->roundRect(r, 5, col); c->label(r, text, SB12, gfx::hex(0x101418), gfx::Center); }
    else { c->roundFrame(r, 5, 1.5f, col); c->label(r, text, SB12, col, gfx::Center); }
    return w;
}
} // namespace

void Gui::logs()
{
    if (!m_logsShown) { a.refreshLog(); m_logsShown = true; }   // the list follows the records
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    // header
    char h[160];
    std::snprintf(h, sizeof h, "%d readings \xC2\xB7 %d cases \xC2\xB7 %d awaiting pathology", a.readingCount(),
                  a.logCount(), a.pendingOutcomes());
    c->text(th::pad, 34, "Logs", SB24, th::text);
    c->text(th::pad + 76, 32, h, S14, th::textFaint);
    const Rect close(scrW - th::pad - 46, th::pad, 46, 44);
    if (u->clicked(ID_G_CLOSE, close)) { m_scr = Measure; m_expanded.clear(); return; }
    u->card(close, u->pressed(ID_G_CLOSE) ? th::accentSoft : th::bg, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, th::textDim, gfx::Center);

    // filters
    // ITDS-2.16.0: on a narrow screen the chips go on as many lines as they need
    int x = th::pad, fy = 64;
    auto wrap = [&](int w) { if (x > th::pad + 50 && x + w > scrW - th::pad) { x = th::pad + 50; fy += 42; } };
    c->text(x, fy + 18, "WHEN", SB11, th::textFaint);
    x += 50;
    for (int i = 0; i < 7; ++i) {
        const int w = gfx::Canvas::measure(kWhen[i].t, S16) + 24;
        wrap(w);
        if (u->chip(ID_G_WHEN + i, Rect(x, fy, w, 36), kWhen[i].t, m_when == kWhen[i].k)) { m_when = kWhen[i].k; m_expanded.clear(); }
        x += w + 6;
    }
    x = th::pad;
    fy += 42;
    c->text(x, fy + 18, "RESULT", SB11, th::textFaint);
    x += 50;
    for (int i = 0; i < 9; ++i) {
        const int w = gfx::Canvas::measure(kTags[i].t, S16) + 20;
        wrap(w);
        if (u->chip(ID_G_TAG + i, Rect(x, fy, w, 36), kTags[i].t, m_tag == kTags[i].k)) { m_tag = kTags[i].k; m_expanded.clear(); }
        x += w + 6;
    }

    int hidden = 0;
    const std::vector<app::LogCase> cases = a.caseLog(m_when, m_tag, &hidden);
    // the footer's six buttons: one row, or (narrow screen) two of three
    const int footRows = portrait() ? 2 : 1;
    const Rect foot(0, scrH - 10 - 60 * footRows, scrW, 10 + 60 * footRows);
    const int msgH = m_logMsg.empty() ? 0 : gfx::Canvas::wrapHeight(scrW - 2 * th::pad - 60, m_logMsg, S13, 3, 1.2f) + 14;
    int top = fy + 44;
    if (hidden > 0 && m_when != "all") {
        char b[200];
        std::snprintf(b, sizeof b, "%d more case(s) are recorded outside this time window. If today's cases are among "
                      "them, check the date on this unit.", hidden);
        c->textBox(Rect(th::pad, top, scrW - 2 * th::pad - 140, 36), b, S13, th::warn, gfx::Left, 2, 1.2f, true);
        if (u->button(ID_G_SHOWALL, Rect(scrW - th::pad - 130, top, 130, 36), "Show all", false, true)) m_when = "all";
        top += 42;
    }
    const Rect list(th::pad, top, scrW - 2 * th::pad, foot.y - msgH - top - 6);
    static int contentH = 600;
    const int off = u->scroll(ID_G_SCROLL, list, contentH);
    c->setClip(list);
    int y = list.y - off;
    const int W = list.w - 10;
    if (cases.empty()) c->label(Rect(list.x, list.y, W, 60), "No case matches this filter.", S16, th::textFaint, gfx::Center);
    for (size_t i = 0; i < cases.size(); ++i) {
        const app::LogCase& lc = cases[i];
        const bool open = m_expanded == lc.caseId;
        const int rowH = 60;
        const Rect row(list.x, y, W, rowH);
        const int id = ID_G_ROW + int(i) * 3;
        if (y + rowH >= list.y && y < list.b()) {
            u->name(lc.caseId + " (case row)");
            if (u->clicked(id, row)) m_expanded = open ? std::string() : lc.caseId;
            u->card(row, u->pressed(id) ? th::accentSoft : (open ? th::surfaceHi : th::surface), open ? th::accent : th::stroke);
            c->text(row.x + 12, row.y + 26, lc.caseId, MB14, lc.test ? th::textDim : th::text);
            char sub[200];
            std::snprintf(sub, sizeof sub, "%s \xC2\xB7 %d of %d readings", lc.whenText.c_str(), lc.readings, lc.total);
            std::string s2 = sub;
            if (!lc.detail.empty()) s2 += " \xC2\xB7 " + lc.detail;
            c->text(row.x + 12, row.y + 47, gfx::Canvas::elide(s2, S13, 440), S13, th::textFaint);
            // badges from the right: pathology, device answer, NO CASE / EDITED
            int bx = row.r() - 12;
            const bool path = !lc.pathology.empty();
            const std::string pl = path ? std::string(1, char(std::toupper((unsigned char)lc.pathology[0]))) + lc.pathology.substr(1) : "awaiting";
            const Color pc = lc.pathology == "malignant" ? th::bad : lc.pathology == "benign" ? th::good :
                             lc.pathology == "borderline" ? th::warn : th::textFaint;
            bx -= badge(c, bx, row.y + 18, pl, pc, false) + 8;
            Color dc = tierColorOf(lc.tier);
            gfx::parseColor(lc.color, &dc);
            bx -= badge(c, bx, row.y + 18, lc.label + (lc.complete ? "" : " \xC2\xB7 provisional"), dc, true) + 8;
            if (lc.test) bx -= badge(c, bx, row.y + 18, "NO CASE", th::textDim, false) + 8;
            // H7-2.9.0: the case's tag in the material library
            const app::CaseTag ct = a.caseTag(lc.caseId);
            if (ct.tagged) {
                if (ct.mixed) bx -= badge(c, bx, row.y + 18, "TAG: mixed", th::accent, false) + 8;
                else bx -= badge(c, bx, row.y + 18, "TAG: " + gfx::Canvas::elide(ct.label, SB12, 150), th::accent, false) + 8;
            }
            if (lc.edited) bx -= badge(c, bx, row.y + 18, "EDITED", th::accent, false) + 8;
        }
        y += rowH + 6;
        if (open) {
            const std::vector<app::LogReading> rs = a.caseReadingsLog(lc.caseId);
            const int x0 = list.x + 24;
            const int cols[5] = { x0, x0 + 50, x0 + 150, x0 + 260, x0 + 380 };
            c->text(cols[0], y + 14, "#", SB11, th::textFaint);
            c->text(cols[1], y + 14, "TIME", SB11, th::textFaint);
            c->text(cols[2], y + 14, "IPS", SB11, th::textFaint);
            c->text(cols[3], y + 14, "Z@1kHz", SB11, th::textFaint);
            c->text(cols[4], y + 14, "RESULT", SB11, th::textFaint);
            y += 22;
            // Every row can be opened: its 22 quantities, as DETAILS shows them
            // for the last reading on the main screen (H7-1.3.1). Rows are 30 px
            // so that a finger can take one.
            const int rh = 30;
            auto rowTap = [&](int id, const Rect& row) {
                if (row.y < list.y || row.b() > list.b()) return false;
                const bool click = u->clicked(id, row);
                if (u->pressed(id)) c->fill(row, th::accentSoft);
                c->label(Rect(row.r() - 70, row.y, 62, row.h), "details", M11, th::accent, gfx::Right);
                return click;
            };
            // Readings and the patient's healthy-tissue samples (H1, H2, …) in the
            // order they were taken.
            const std::vector<app::TissueSample> hs = a.tissueSamplesOf(lc.caseId);
            size_t ri = 0, hi = 0;
            while (ri < rs.size() || hi < hs.size()) {
                char b[32];
                const Rect row(list.x + 12, y, W - 24, rh - 2);
                const bool takeSample = hi < hs.size() && (ri >= rs.size() || hs[hi].epochMs <= rs[ri].epochMs);
                if (takeSample) {
                    const app::TissueSample& h = hs[hi];
                    u->name("healthy sample H" + std::to_string(h.n));
                    if (rowTap(ID_G_SAMPLE + int(hi), row)) openLogDetails(lc.caseId, nullptr, &h);
                    c->text(cols[0], y + 20, "H" + std::to_string(h.n), SB12, th::healthy);
                    c->text(cols[1], y + 20, h.when, M12, th::textDim);
                    const double ips = a.ipsIsLegacy() ? h.features.ipsLegacy : h.features.ipsFit;
                    if (std::isfinite(ips)) { std::snprintf(b, sizeof b, "%+.2f", ips); c->text(cols[2], y + 20, b, M12, th::healthy); }
                    else c->text(cols[2], y + 20, "\xE2\x80\x94", M12, th::textFaint);
                    c->text(cols[3], y + 20, app::App::ohmText(h.features.z1k) + (std::isfinite(h.features.z1k) ? " \xCE\xA9" : ""), M12, th::healthy);
                    c->text(cols[4], y + 20, gfx::Canvas::elide("healthy tissue \xC2\xB7 " + h.flags, S13, list.r() - cols[4] - 100), S13, th::healthy);
                    ++hi;
                } else {
                    const app::LogReading& r = rs[ri];
                    u->name("reading " + std::to_string(r.site));
                    if (rowTap(ID_G_READING + int(ri), row)) openLogDetails(lc.caseId, &r, nullptr);
                    c->text(cols[0], y + 20, std::to_string(r.site), M12, th::textDim);
                    c->text(cols[1], y + 20, r.when, M12, th::textDim);
                    if (std::isfinite(r.ipsLegacy)) { std::snprintf(b, sizeof b, "%+.2f", r.ipsLegacy); c->text(cols[2], y + 20, b, M12, th::text); }
                    else c->text(cols[2], y + 20, "\xE2\x80\x94", M12, th::textFaint);
                    c->text(cols[3], y + 20, app::App::ohmText(r.z1k) + (std::isfinite(r.z1k) ? " \xCE\xA9" : ""), M12, th::text);
                    const bool refused = r.tier == "Invalid";
                    Color rc = tierColorOf(r.tier);
                    gfx::parseColor(r.color, &rc);
                    std::string res = refused ? "refused \xC2\xB7 " + r.label + " \xC2\xB7 " + r.flags : r.label;
                    c->text(cols[4], y + 20, gfx::Canvas::elide(res, S13, list.r() - cols[4] - 100), S13, refused ? th::textFaint : rc);
                    ++ri;
                }
                y += rh;
            }
            if (hs.size() > 1) {
                c->text(cols[1], y + 16, "median of " + std::to_string(hs.size()) + " healthy samples: " +
                        app::App::sampleText(app::App::medianOf(hs), a.ipsIsLegacy()), S12, th::healthy);
                y += 22;
            }
            y += 4;
            // ITDS-2.16.0: on a narrow screen the sentence across the row, the three buttons under it
            const bool narrow = W < 900;
            const int sumW = narrow ? W - 24 - 12 : W - 680;
            const int th2 = c->textBox(Rect(x0, y, sumW, 120), lc.summary + ". The case answer is the median of the "
                                       "readings that passed their checks \xE2\x80\x94 never a count of how many landed in the "
                                       "box, which would let the number of readings decide the verdict.",
                                       S12, th::textFaint, gfx::Left, 7, 1.2f);
            if (narrow) y += th2 + 8;
            const int nbw = (W - 24 - 2 * 8) / 3;
            /* The pathology of THIS case, from its own row (2.19.0). The
               footer's Enter pathology offers a list of every case still
               waiting, which is the wrong tool for "the result for this
               patient has come back". */
            // Both buttons on every row, the readings taken with no case open
            // (NO CASE) included — LogsPage.qml makes no exception for them.
            // H7-2.9.0: the whole case tagged in the material library (was a "+ tag" on each reading)
            {
                const app::CaseTag ct = a.caseTag(lc.caseId);
                const Rect tb = narrow ? Rect(list.x + 12, y, nbw, 44) : Rect(list.x + W - 650, y, 200, 44);
                const std::string t = !ct.tagged ? std::string("+ Tag the case")
                                    : ct.mixed ? std::string("Tag: mixed")
                                               : "Tag: " + gfx::Canvas::elide(ct.label, SB16, 120);
                if (u->button(ID_G_TAGCASE + int(i), tb, t, false, true)) {
                    m_tagSheet = true;
                    m_tagCase = lc.caseId;
                    m_tagSite = 0;
                    m_tagEpoch = 0;
                    m_tagWhen = lc.whenText;
                    m_tagResult = lc.label;
                    m_tagMsg.clear();
                }
            }
            {
                const Rect pb = narrow ? Rect(list.x + 12 + nbw + 8, y, nbw, 44) : Rect(list.x + W - 440, y, 200, 44);
                if (u->button(ID_G_EDIT + 400 + int(i), pb, lc.pathology.empty() ? "Enter pathology" : "Change pathology",
                              false, true))
                    openOutcome(lc.caseId);
            }
            const Rect eb = narrow ? Rect(list.x + 12 + 2 * (nbw + 8), y, nbw, 44) : Rect(list.x + W - 230, y, 220, 44);
            if (u->button(ID_G_EDIT + int(i), eb, "Edit case details", false, true)) {
                m_caseEdit = true;
                m_editCase = lc.caseId;
                m_editVals = a.caseDetails(lc.caseId);
                m_editMsg.clear();
                m_editPage = 1;
                u->resetScroll(ID_E_SCROLL);
            }
            y += narrow ? 44 + 12 : std::max(th2, 44) + 12;
        }
    }
    contentH = y + off - list.y;
    c->resetClip();

    // footer
    c->fill(foot, th::surface);
    c->fill(Rect(0, foot.y, scrW, 2), th::stroke);
    const int perRow = 6 / footRows;
    const int bw = footRows == 1 ? 156 : (scrW - 2 * th::pad - (perRow - 1) * 8) / perRow;
    const int bx0 = scrW - th::pad - perRow * bw - (perRow - 1) * 8;
    int bx = bx0, by = foot.y + 10, nb = 0;
    // the next button's place: along the row, then (narrow screen) the second row
    auto next = [&]() { bx += bw + 8; if (++nb % perRow == 0) { bx = bx0; by += 60; } };
    if (!m_logMsg.empty()) {
        const Rect mr(th::pad, foot.y - msgH - 2, scrW - 2 * th::pad, msgH);
        u->card(mr, th::surfaceHi, th::accent);
        c->textBox(Rect(mr.x + 12, mr.y + 7, mr.w - 70, mr.h - 14), m_logMsg, S13, th::text, gfx::Left, 3, 1.2f, true);
        if (u->clicked(ID_G_SHOWALL + 5, mr)) m_logMsg.clear();
        c->label(Rect(mr.r() - 50, mr.y, 40, mr.h), "\xE2\x9C\x95", S16, th::textDim, gfx::Center);
    }
    u->why(ID_G_PATH, "No case is shown and none waits for its pathology: change the filter above, or measure a case first.");
    if (u->button(ID_G_PATH, Rect(bx, by, bw, 50), "Enter pathology", false, a.pendingOutcomes() > 0 || !cases.empty()))
        openOutcome(std::string());
    next();
    u->why(ID_G_EXPORT, "Nothing to export: no case is shown. Change the filter above.");
    if (u->button(ID_G_EXPORT, Rect(bx, by, bw, 50), "Export shown", false, !cases.empty()))
        m_logMsg = a.exportLogs(m_when, m_tag);
    next();
    // H7-2.7.0: to a USB stick, or by the programming cable
    if (u->button(ID_G_ALL, Rect(bx, by, bw, 50), "All files", false, true)) {
        m_allSheet = true;
        m_allMsg.clear();
        m_allPending = 0;
    }
    next();
    // H7-2.5.3: the material library to and from a USB stick, and the cable's records
    if (u->button(ID_G_TRAIN, Rect(bx, by, bw, 50), "Training data", false, true)) {
        m_trainSheet = true;
        m_trainMsg.clear();
    }
    next();
    /* DIAGNOSTIC MODE, on and off (2.19.0). On: a snapshot now, then every
       action and its result into the same file until it is turned off. The
       button says which press is which. */
    {
        ui::Ui::Btn b;
        b.primary = false;
        b.danger = a.diagnosticMode();
        if (u->button(ID_G_DIAG, Rect(bx, by, bw, 50),
                      a.diagnosticMode() ? "Stop diagnostics" : "Diagnostics", b))
            m_logMsg = !a.diagnosticMode() ? a.startDiagnosticMode() : a.stopDiagnosticMode();
    }
    next();
    // H7-1.9.0: the logs deleted, whenever wanted (asked from a sheet that
    // says what goes and what stays, with a copy to USB offered first)
    {
        ui::Ui::Btn b;
        b.primary = false;
        b.danger = true;
        if (u->button(ID_G_DEL, Rect(bx, by, bw, 50), "Delete logs", b)) {
            m_delAsk = true;
            m_delArmedAt = 0;
            m_delMsg.clear();
            a.logsFootprint(&m_delFiles, &m_delBytes);
        }
    }
}

// ---------------------------------------------------------------------------
// OutcomeSheet.qml (2.19.0)

void Gui::openOutcome(const std::string& preselect)
{
    m_outcome = true;
    m_outMsg.clear();
    m_outMsgOk = true;
    m_outCloseAt = 0;
    m_outPending = a.casesAwaitingPathology();
    m_outSingle = !preselect.empty();
    m_outCase = preselect;
    m_outPath = m_outSingle ? a.outcomeOf(preselect) : std::string();
    if (m_outSingle) {
        // The case it was opened for goes first, where it can be seen chosen.
        std::vector<std::string> rest{ preselect };
        for (const std::string& id : m_outPending) if (id != preselect) rest.push_back(id);
        m_outPending = rest;
    }
    u->resetScroll(ID_O_SCROLL);
    a.uiAction("pathology sheet", m_outSingle ? "opened for " + preselect
                                              : "opened, " + std::to_string(m_outPending.size()) + " waiting");
}

void Gui::deleteLogsSheet()
{
    const uint32_t now = u->now();
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 230);
    const Rect box = sheetBox(Rect(112, 100, 800, 390), 600);
    u->card(box, th::surface, th::bad, th::radius);
    const int x0 = box.x + 28, W = box.w - 56;
    int y = box.y + 44;
    c->text(x0, y, "Delete all logs?", SB24, th::text);
    y += 22;

    // while it runs: a count, nothing to press
    if (a.deletingLogs()) {
        char b[96];
        std::snprintf(b, sizeof b, "Deleting\xE2\x80\xA6 %d of %d files", a.deleteDone(), a.deleteTotal());
        c->text(x0, y + 40, b, SB19, th::text);
        const Rect bar(x0, y + 64, W, 12);
        c->roundRect(bar, 6, th::surfaceIn);
        const int fw = a.deleteTotal() ? bar.w * a.deleteDone() / a.deleteTotal() : 0;
        if (fw > 0) c->roundRect(Rect(bar.x, bar.y, std::max(12, fw), bar.h), 6, th::bad);
        c->text(x0, y + 104, "Do not switch the instrument off.", S14, th::textDim);
        m_timerAt = now + 50;
        return;
    }
    const std::string done = a.takeDeleteResult();
    if (!done.empty()) { m_logMsg = done; m_delAsk = false; m_logsShown = false; return; }

    const std::string size = m_delBytes >= 1048576 ? std::to_string(m_delBytes / 1048576) + "." +
                             std::to_string((m_delBytes % 1048576) * 10 / 1048576) + " MB"
                           : std::to_string((m_delBytes + 1023) / 1024) + " kB";
#if defined(BIRADX_ITDS)
    const std::string where = "this computer's records folder";
#else
    const std::string where = plat::recordsOnInternal() ? "the internal memory" : "the SD card";
#endif
    const std::string text =
        "This deletes from " + where + ": every case and reading (" + std::to_string(a.logCount()) +
        " cases, " + std::to_string(a.readingCount()) + " readings), their sweeps, the healthy-tissue samples, "
        "the pathology results, and the health and diagnostic files \xE2\x80\x94 " + std::to_string(m_delFiles) +
        " files, " + size + ". It cannot be undone.";
    y += c->textBox(Rect(x0, y, W, 120), text, S16, th::text, gfx::Left, 0, 1.3f) + 12;
    y += c->textBox(Rect(x0, y, W, 60),
                    "Kept: the case numbering (no case number is ever given twice), the needle calibrations, "
                    "the list of needles used, the material library (the tagged readings in it), and all settings and rules.",
                    S14, th::textDim, gfx::Left, 0, 1.3f) + 12;
    y += c->textBox(Rect(x0, y, W, 40), "Copy them to a USB stick first if they may be needed.",
                    SB14, th::warn, gfx::Left, 0, 1.3f) + 10;

    const std::string block = a.deleteLogsBlocker();
    const std::string note = !block.empty() ? block : m_delMsg;
    if (!note.empty())
        c->textBox(Rect(x0, y, W, 50), note, S14, block.empty() ? th::text : th::bad, gfx::Left, 2, 1.25f);

    // Cancel | Copy to USB first | Delete (twice)
    const int by = box.b() - 28 - 56, bw = (W - 2 * 12) / 3;
    if (u->button(ID_D_CANCEL, Rect(x0, by, bw, 56), "Cancel", false, true)) { m_delAsk = false; return; }
    u->why(ID_D_COPY, "No USB stick is mounted: plug one in (FAT32 or exFAT).");
    if (u->button(ID_D_COPY, Rect(x0 + bw + 12, by, bw, 56), "Copy to USB first", false, plat::usbReady())) {
        m_delMsg = a.exportAll();
        m_delArmedAt = 0;
    }
    const bool armed = m_delArmedAt && int32_t(now - m_delArmedAt) < 5000;
    if (m_delArmedAt && !armed) m_delArmedAt = 0;
    if (armed) m_timerAt = m_delArmedAt + 5000;
    ui::Ui::Btn st;
    st.primary = armed;
    st.danger = true;
    st.enabled = block.empty() && m_delFiles > 0;
    u->why(ID_D_DEL, block.empty() ? std::string("There is no log to delete.") : block);
    if (u->button(ID_D_DEL, Rect(x0 + 2 * (bw + 12), by, bw, 56),
                  armed ? "Tap again to delete" : "Delete", st)) {
        if (!armed) m_delArmedAt = now | 1;
        else {
            m_delArmedAt = 0;
            const std::string why = a.deleteLogsStart();
            if (!why.empty()) m_delMsg = why;
        }
    }
}

// ---------------------------------------------------------------------------
// Logs -> All files (H7-2.7.0): every file of the records, to a USB stick, or
// by the programming cable (ST-LINK) when there is no stick: the files are put
// in memory as a ZIP archive, part by part, and the screen shows the command
// that reads it (app_dump.cpp).

void Gui::allFilesSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 235);
    const Rect box = sheetBox(Rect(40, 30, 944, 540), 760);
    u->card(box, th::surface, th::accent, th::radius);
    const int x0 = box.x + 28, W = box.w - 56;
    c->text(x0, box.y + 44, "All files", SB24, th::text);
    // a part asked for on the frame before, after "Preparing…" was drawn
    if (m_allPending > 0) {
        const int part = m_allPending;
        m_allPending = 0;
        const std::string e = a.dumpPrepare(part);
        m_allMsg = e;
        m_allMsgBad = !e.empty();
    }
    int y = box.y + 60;
    y += c->textBox(Rect(x0, y, W, 40), "Every file of the records (cases, readings, sweeps, the material library, the lots, "
                    "the logs of the device), as they are.", S14, th::textDim, gfx::Left, 0, 1.3f) + 12;

    // ---- to a USB stick
    {
        const Rect r(x0, y, W, 64);
        u->card(r, th::surfaceHi, th::stroke);
        c->text(r.x + 14, r.y + 26, "To a USB stick", SB16, th::text);
        c->text(r.x + 14, r.y + 48, plat::usbReady() ? "the stick is mounted" : "no stick is mounted", S13, th::textDim);
        u->why(ID_A_COPY, "No USB stick is mounted: plug one in (FAT32 or exFAT).");
        if (u->button(ID_A_COPY, Rect(r.r() - 214, r.y + 10, 200, 44), "Copy", false, plat::usbReady())) {
            m_allMsg = a.exportAll();
            m_allMsgBad = m_allMsg.find("Copied") != 0;
        }
        y += 72;
    }

    // ---- by the programming cable
#if defined(BIRADX_ITDS)
    // the ITDS computer has no programming cable: its records are files on its own disk
    const int h = 96;
    {
        const Rect r(x0, y, W, h);
        u->card(r, th::surfaceHi, th::stroke);
        c->text(r.x + 14, r.y + 26, "On this computer", SB16, th::text);
        c->textBox(Rect(r.x + 14, r.y + 36, W - 28, 56), "The records are the files of " + plat::recordsRoot() +
                   " \xE2\x80\x94 to be copied from a terminal or a file manager as well.", S13, th::textDim, gfx::Left, 0, 1.3f);
    }
#else
    const bool ready = a.dumpReady();
    const bool narrow = portrait();
    const int h = ready ? 290 : narrow ? 200 : 120;
    {
        const Rect r(x0, y, W, h);
        u->card(r, th::surfaceHi, ready ? th::accent : th::stroke);
        c->text(r.x + 14, r.y + 26, "By the programming cable (ST-LINK)", SB16, th::text);
        int yy = r.y + 36;
        if (!ready) {
            c->textBox(Rect(r.x + 14, yy, narrow ? W - 28 : W - 258, narrow ? 100 : 80),
                       "The instrument puts the files into its memory as a ZIP archive, part by part, and shows the "
                       "command that reads it with STM32_Programmer_CLI over the same cable the firmware is flashed with "
                       "\xE2\x80\x94 while the program runs. The file it makes, renamed .zip, opens in Windows.",
                       S13, th::textDim, gfx::Left, 0, 1.3f);
            u->why(ID_A_PREP, "Wait: a part is being prepared.");
            if (u->button(ID_A_PREP, Rect(r.r() - 234, narrow ? r.b() - 60 : r.y + 36, 220, 48), "Prepare part 1", true, m_allPending == 0)) {
                m_allPending = 1;
                m_allMsg = "Preparing\xE2\x80\xA6 do not switch the instrument off.";
                m_timerAt = u->now() + 30;          // drawn first, then prepared
                m_allMsgBad = false;
            }
        } else {
            const size_t len = a.dumpLength();
            char sz[32];
            if (len >= 1048576) std::snprintf(sz, sizeof sz, "%.2f MB", len / 1048576.0);
            else std::snprintf(sz, sizeof sz, "%.0f kB", long(len) / 1024.0);
            char b[300];
            std::snprintf(b, sizeof b, "Part %d of %d: %d files, %s, in memory at 0x%08lX, length 0x%lX. Keep this sheet "
                          "open while it is read. In a Command Prompt:", a.dumpPart(), a.dumpParts(), a.dumpFiles(), sz,
                          (unsigned long)(uintptr_t)a.dumpData(), (unsigned long)a.dumpLength());
            yy += c->textBox(Rect(r.x + 14, yy, W - 28, 50), b, S14, th::text, gfx::Left, 0, 1.3f) + 8;
            const Rect k(r.x + 14, yy, W - 28, 108);
            c->fill(k, th::bg);
            c->text(k.x + 10, k.y + 22, "cd \"C:\\Program Files\\STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin\"",
                    M12, th::textDim);
            const std::string cmd = a.dumpCommand();
            const size_t cut = cmd.find(" -u ");      // the command on two lines, the second the address and length
            c->text(k.x + 10, k.y + 48, cut == std::string::npos ? cmd : cmd.substr(0, cut), M14, th::accent);
            c->text(k.x + 30, k.y + 72, cut == std::string::npos ? std::string() : cmd.substr(cut + 1), MB14, th::accent);
            char rn[128];
            std::snprintf(rn, sizeof rn, "ren \"%%USERPROFILE%%\\Downloads\\records_%d.bin\" records_%d.zip", a.dumpPart(),
                          a.dumpPart());
            c->text(k.x + 10, k.y + 98, rn, M12, th::textDim);
            std::string note = "Type the address and the length exactly as shown here: they change every time. The file "
                               "goes to your Downloads folder; there, records_" + std::to_string(a.dumpPart()) +
                               ".zip opens in Windows Explorer as a folder.";
            if (!a.dumpSkipped().empty())
                note += " Too large for this way (copy them to a USB stick): " + std::to_string(a.dumpSkipped().size()) +
                        " file(s), first " + a.dumpSkipped()[0] + ".";
            c->textBox(Rect(r.x + 14, yy + 116, W - 28, 40), note, S12, th::textDim, gfx::Left, 0, 1.25f);
            u->why(ID_A_NEXT, "Wait: a part is being prepared.");
            if (a.dumpPart() < a.dumpParts() &&
                u->button(ID_A_NEXT, Rect(r.r() - 234, r.y + 10, 220, 44), "Next part \xE2\x86\x92", true, m_allPending == 0)) {
                m_allPending = a.dumpPart() + 1;
                m_allMsg = "Preparing\xE2\x80\xA6";
                m_timerAt = u->now() + 30;
                m_allMsgBad = false;
            }
        }
    }
#endif
    if (!m_allMsg.empty()) {
        const int my = y + h + 8;
        c->textBox(Rect(x0, my, W, box.b() - 90 - my), m_allMsg, SB14, m_allMsgBad ? th::bad : th::good, gfx::Left, 3, 1.25f);
    }
    if (u->button(ID_A_CLOSE, Rect(box.r() - 248, box.b() - 80, 220, 52), a.dumpReady() ? "Close (frees it)" : "Close",
                  false, true)) {
        a.dumpFree();
        m_allSheet = false;
        m_allMsg.clear();
    }
}

void Gui::outcomeSheet()
{
    const uint32_t now = u->now();
    if (m_outCloseAt && int32_t(now - m_outCloseAt) >= 0) { m_outcome = false; m_outCloseAt = 0; return; }
    const bool closing = m_outCloseAt != 0;
    if (closing) m_timerAt = m_outCloseAt;

    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    const int x0 = th::pad, W = scrW - 2 * th::pad;
    const Rect close(scrW - th::pad - 46, th::pad, 46, 44);
    c->text(x0, 36, gfx::Canvas::elide(m_outSingle ? "Pathology for " + m_outCase : std::string("Enter pathology"),
                                       SB24, W - 60), SB24, th::text);
    char b[96];
    std::snprintf(b, sizeof b, "%d cases are waiting", int(m_outPending.size()));
    c->text(x0, 58, b, S13, th::textFaint);
    if (u->clicked(ID_O_CANCEL, close)) { m_outcome = false; m_outCloseAt = 0; return; }
    u->card(close, u->pressed(ID_O_CANCEL) ? th::accentSoft : th::bg, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, th::textDim, gfx::Center);

    c->text(x0, 86, "CASE", M11, th::textFaint);
    const Rect lr(x0, 94, W, scrH - 360);
    static int contentH = 200;
    const int off = u->scroll(ID_O_SCROLL, lr, contentH);
    c->setClip(lr);
    int y = lr.y - off;
    if (m_outPending.empty())
        c->label(Rect(lr.x, lr.y, lr.w, lr.h), "Every measured case has an outcome.", S14, th::textFaint, gfx::Center);
    for (size_t i = 0; i < m_outPending.size(); ++i) {
        const std::string& id = m_outPending[i];
        const Rect r(lr.x, y, lr.w - 10, th::touchSm);
        const bool on = m_outCase == id;
        if (y + r.h >= lr.y && y < lr.b()) {
            u->name(id);
            if (u->clicked(ID_O_CASE + int(i), r)) { m_outCase = id; m_outMsg.clear(); }
            u->card(r, on ? th::accentSoft : (u->pressed(ID_O_CASE + int(i)) ? th::accentSoft : th::bg),
                    on ? th::accent : th::stroke);
            c->label(Rect(r.x + 12, r.y, r.w - 24, r.h), id, MB14, on ? th::accent : th::text);
            const std::string was = a.outcomeOf(id);
            if (!was.empty())
                c->label(Rect(r.x + 12, r.y, r.w - 24, r.h), "recorded: " + was + " \xE2\x80\x94 saving replaces it",
                         S13, th::warn, gfx::Right);
        }
        y += th::touchSm + 6;
    }
    contentH = y + off - lr.y;
    c->resetClip();

    int yy = lr.b() + th::gap;
    c->text(x0, yy + 10, "RESULT", M11, th::textFaint);
    yy += 18;
    const char* keys[3] = { "benign", "borderline", "malignant" };
    const char* names[3] = { "Benign", "Borderline", "Malignant" };
    int cx = x0;
    for (int k = 0; k < 3; ++k) {
        const int w = gfx::Canvas::measure(names[k], SB16) + 40;
        if (u->chip(ID_O_RES + k, Rect(cx, yy, w, th::touchSm), names[k], m_outPath == keys[k])) {
            m_outPath = keys[k];
            m_outMsg.clear();
        }
        cx += w + th::gap;
    }
    yy += th::touchSm + 2 * th::gap;
    const std::string label = m_outCase.empty() ? "Choose a case above"
                            : m_outPath.empty() ? "Choose Benign, Borderline or Malignant" : "Save outcome";
    u->why(ID_O_SAVE, closing ? std::string("Wait: it is being saved.") : label + " first.");
    if (u->button(ID_O_SAVE, Rect(x0, yy, W, th::touch), label, true,
                  !m_outCase.empty() && !m_outPath.empty() && !closing)) {
        const std::string id = m_outCase, what = m_outPath;
        const std::string why = a.recordOutcome(id, what, a.localText(a.nowMs(), "%Y-%m-%d"), "core biopsy", "");
        if (!why.empty()) {
            m_outMsgOk = false;
            m_outMsg = "NOT saved: " + why;
        } else {
            m_outMsgOk = true;
            m_outMsg = "Saved \xE2\x80\x94 " + id + " is " + what + ".";
            m_logMsg = m_outMsg;     // it also stays on the Logs page after this sheet has closed
            if (m_outSingle) { m_outCloseAt = now + 1400; if (!m_outCloseAt) m_outCloseAt = 1; m_timerAt = m_outCloseAt; }
            else { m_outPending = a.casesAwaitingPathology(); m_outCase.clear(); m_outPath.clear(); }
        }
        a.uiAction("pathology sheet", m_outMsg);
    }
    yy += th::touch + th::gap;
    if (!m_outMsg.empty())
        yy += c->textBox(Rect(x0, yy, W, 48), m_outMsg, SB16, m_outMsgOk ? th::good : th::bad, gfx::Left, 2, 1.2f) + 6;
    c->textBox(Rect(x0, yy, W, 40), "This does not change the installed rules. It is appended to the record "
               "(outcomes.csv), and the analysis is done off the device from all of them together.",
               S12, th::textFaint, gfx::Left, 2, 1.2f);
}

// ---------------------------------------------------------------------------
// CaseEditSheet.qml

void Gui::caseEditSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg, 252);
    c->text(th::pad * 2, 40, "Case details", SB24, th::text);
    c->text(th::pad * 2 + (portrait() ? 170 : 200), 38, m_editCase, MB19, th::accent);
    auto src = m_editVals.find("_source");
    c->text(th::pad * 2, 66, src != m_editVals.end() ? "Showing what is on record, from " + src->second + "."
                                                     : "Nothing was recorded for this case. Anything entered here is added to it.",
            S13, th::textFaint);
    for (int p = 1; p <= 3; ++p)
        // ITDS-2.16.0: upright, the three pages on a line of their own under the title
        if (u->chip(ID_E_PAGE + p, portrait() ? Rect(th::pad * 2 + (p - 1) * 128, 80, 120, 40)
                                              : Rect(scrW - th::pad * 2 - (4 - p) * 118, 20, 110, 40),
                    p == 1 ? "Required" : p == 2 ? "Recommended" : "Notes", m_editPage == p)) { m_editPage = p; u->resetScroll(ID_E_SCROLL); }
    const int bodyY = portrait() ? 130 : 80;
    const Rect body(th::pad * 2, bodyY, scrW - th::pad * 4, scrH - bodyY - 90);
    static int contentH = 800;
    const int off = u->scroll(ID_E_SCROLL, body, contentH);
    c->setClip(body);
    int y = body.y + 8 - off;
    int k = 0;
    for (const birx::IntakeField& f : birx::intakeSchema()) {
        if (f.page != m_editPage) continue;
        const std::string id = f.id;
        y += fieldEditor(ID_E_FIELD + k * 40, Rect(body.x, y, body.w - 10, 0), id, m_editVals[id],
                         [this, id](const std::string& v) { m_editVals[id] = v; });
        y += th::gap * 2;
        ++k;
    }
    contentH = y + off - body.y;
    c->resetClip();
    const int by = scrH - th::touch - th::pad;
    if (!m_editMsg.empty()) c->textBox(Rect(th::pad * 2, by - 30, scrW - th::pad * 4, 28), m_editMsg, S13, th::warn, gfx::Left, 1);
    if (u->button(ID_E_CANCEL, Rect(scrW / 2 - 230, by, 220, th::touch), "Cancel", false, true)) m_caseEdit = false;
    if (u->button(ID_E_SAVE, Rect(scrW / 2 + 10, by, 240, th::touch), "Save details", true, true)) {
        std::map<std::string, std::string> vals;
        for (const auto& kv : m_editVals) if (!kv.first.empty() && kv.first[0] != '_') vals[kv.first] = kv.second;
        const std::string r = a.updateCaseDetails(m_editCase, vals);
        if (r.compare(0, 5, "Saved") == 0) { m_caseEdit = false; m_logMsg = r; }
        else m_editMsg = r;
    }
}

} // namespace gui
