// gui/gui_settings.cpp — SettingsHub.qml, with the rule-table editor of the
// older SettingsPage.qml moved into its own tab (spec §13.5).
#include "gui.h"
#include "../app/version.h"

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
    ID_S_CLOSE = 4000, ID_S_TAB = 4010, ID_S_SCROLL = 4030, ID_S_DONE,
    ID_D_ID = 4100, ID_D_CLOCK, ID_D_UTCM, ID_D_UTCP, ID_D_REV, ID_D_PROBE, ID_D_BLM, ID_D_BLP, ID_D_IM,
    ID_IM_SAVE = 4150, ID_IM_ERASE, ID_IM_CANCEL, ID_D_MON, ID_MON_R, ID_MON_T, ID_MON_CLOSE,
    ID_P_RM = 4200, ID_P_RP, ID_P_EARLY, ID_P_MM, ID_P_MP, ID_P_APPLY, ID_P_REVERT, ID_P_GATE, ID_P_SPREAD,
    ID_N_CHECK = 4300, ID_N_REQ = 4310, ID_N_CAL = 4320, ID_N_DIP = 4330, ID_N_SOL = 4340,
    ID_R_ROW = 5000, ID_R_FIELD = 5500, ID_R_ADD = 6900, ID_R_DEF, ID_R_SAVE, ID_R_REVERT, ID_R_CASEADD, ID_R_AIR1, ID_R_AIR2,
    ID_R_CHM, ID_R_CHP, ID_R_CLM, ID_R_CLP, ID_R_CCLOSE,
    ID_SND_MUTE = 7000, ID_SND_VM, ID_SND_VP, ID_SND_ASK, ID_SND_T = 7010, ID_SND_SW = 7030,
    ID_SEC_CHANGE = 7100,
};
const char* kTabs[] = { "Device", "Protocol", "Needle", "Rules", "Sound", "Security", "About", "Lots", "Training" };
const int kTabCount = 9;
const int kTabOrder[kTabCount] = { 0, 1, 2, 7, 8, 3, 4, 5, 6 };   // Lots and Training shown after Needle
const char* kPalette[8] = { "#C62828", "#E06C1F", "#C9A227", "#43BE8B", "#2E7D32", "#2F82C4", "#7E57C2", "#5E6E78" };
std::string n2s(double v) { return app::fmtNum(v); }
}

// SettingRow.qml: label and note on the left, the control on the right.
int Gui::settingRow(const Rect& r, const std::string& label, const std::string& note, int rightW)
{
    int textW = r.w - rightW - th::gap * 2 - 24;
    // ITDS-2.16.0: upright, a wide control leaves the text too narrow to read: the text across
    // the whole row, the control under it (m_rowR, as wide as the row)
    const bool stacked = portrait() && rightW > 0 && textW < 320;
    if (stacked) textW = r.w - 28;
    const int noteH = note.empty() ? 0 : gfx::Canvas::wrapHeight(textW, note, S13, stacked ? 10 : 6, 1.25f);
    int h = std::max(64, 22 + noteH + 20);
    if (stacked) {
        const int bandY = r.y + (noteH ? 34 + noteH + 4 : 34);
        m_rowR = Rect(r.x + 14, bandY, r.w - 28, 56);
        h = m_rowR.b() + 6 - r.y;
    }
    u->card(Rect(r.x, r.y, r.w, h), th::surface, th::stroke);
    c->text(r.x + 14, r.y + 26, label, SB16, th::text);
    if (noteH) c->textBox(Rect(r.x + 14, r.y + 34, textW, noteH), note, S13, th::textDim, gfx::Left, stacked ? 10 : 6, 1.25f);
    if (!stacked) m_rowR = Rect(r.x + r.w - rightW - 12, r.y, rightW, h);
    return h;
}

void Gui::settings()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    if (!m_editLoaded) { m_edit = a.settings(); m_editLoaded = true; }
    if (!m_rulesLoaded) { m_rules = a.settings().rules; m_rulesLoaded = true; m_ruleOpen = -1; m_caseRuleOpen = -1; }
    // header
    c->text(th::pad, 34, "Settings", SB24, th::text);
    const std::string sub = (a.settings().deviceId.empty() ? std::string("no device id") : a.settings().deviceId) +
                            " \xC2\xB7 " + BIRADX_VERSION;
    c->text(th::pad + 130, 32, sub, M12, th::textFaint);
    const Rect close(scrW - th::pad - 46, th::pad, 46, 44);
    if (u->clicked(ID_S_CLOSE, close)) { m_scr = Measure; return; }   // re-locks
    u->card(close, u->pressed(ID_S_CLOSE) ? th::accentSoft : th::bg, th::stroke);
    c->label(close, "\xE2\x9C\x95", SB19, th::textDim, gfx::Center);
    // tabs
    // ITDS-2.16.0: on a narrow screen the tabs go on as many rows as they need
    int x = th::pad, ty = 66;
    for (int k = 0; k < kTabCount; ++k) {
        const int i = kTabOrder[k];
        const int w = gfx::Canvas::measure(kTabs[i], SB16) + 30;
        if (x > th::pad && x + w > scrW - th::pad) { x = th::pad; ty += 44 + 8; }
        if (u->chip(ID_S_TAB + i, Rect(x, ty, w, 44), kTabs[i], m_tab == i)) {
            m_tab = i; m_msg.clear(); u->resetScroll(ID_S_SCROLL + i);
            m_edit = a.settings();
        }
        x += w + 8;
    }
    // body
    const int top = ty + 44 + 10;
    const Rect body(th::pad, top, scrW - 2 * th::pad, scrH - top - th::pad);
    switch (m_tab) {
    case 0: tabDevice(body); break;
    case 1: tabProtocol(body); break;
    case 2: tabNeedle(body); break;
    case 3: tabRules(body); break;
    case 4: tabSound(body); break;
    case 5: tabSecurity(body); break;
    case 6: tabAbout(body); break;
    case 7: tabLots(body); break;
    case 8: tabMaterials(body); break;
    }
}

// ---------------------------------------------------------------------------

void Gui::tabDevice(const Rect& body)
{
    static int contentH = 1200;
    const int off = u->scroll(ID_S_SCROLL + 0, body, contentH);
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
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 5, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 5, 1.25f);
        y += h + 8;
    }
    const app::Settings& s = a.settings();
    {   // Device ID
        Rect r = row("Device ID", "Assigned, four characters. Every case id starts with it, and a made-up one "
                     "would collide across the fleet.", 300);
        c->label(Rect(r.x, r.y, 150, r.h), s.deviceId.empty() ? "not set" : s.deviceId,
                 s.deviceId.empty() ? S16 : MB19, s.deviceId.empty() ? th::bad : th::text);
        if (u->button(ID_D_ID, Rect(r.r() - 110, r.y + (r.h - 40) / 2, 110, 40), "Set", s.deviceId.empty(), true))
            openText("Device ID", s.deviceId, 8, [this](const std::string& v) {
                const std::string why = a.setDeviceId(v);
                if (why.empty()) say("Device ID set to " + a.settings().deviceId + " and read back from the "
                                     "settings memory. Case ids will start with it.");
                else say(why, true);
            }, "exactly four characters: 0-9 and A-Z without I, L, O, U");
    }
    {   // link
        std::string st;
        char b[160];
        if (a.connected()) std::snprintf(b, sizeof b, "receiving \xC2\xB7 %d heartbeats \xC2\xB7 %u UART errors recovered",
                                         a.heartbeats(), unsigned(plat::linkErrors()));
        else std::snprintf(b, sizeof b, "nothing arriving \xC2\xB7 %d heartbeats so far", a.heartbeats());
        char t[64];
        std::snprintf(t, sizeof t, " Commands sent: %d, refused by the port: %d. ", a.framesSent(), a.framesFailed());
#if defined(BIRADX_ITDS)
        Rect r = row("Measurement unit", std::string("This computer's serial port \xE2\x80\x94 the cable to the ITDS board. ") + b + "." + t +
#else
        Rect r = row("Measurement unit", std::string("USART1, 57600 8N1 \xE2\x80\x94 the cable to the ITDS board. ") + b + "." + t +
#endif
                     "Port: " + plat::linkPortStatus() + ".", 300);
        c->label(Rect(r.x, r.y, 140, r.h), a.connected() ? "connected" : "no link", SB16,
                 a.connected() ? th::good : th::bad);
        if (u->button(ID_D_MON, Rect(r.r() - 150, r.y + (r.h - 40) / 2, 150, 40), "Monitor", false, true)) m_monitor = true;
    }
    {   // probe
        const std::string res = a.probeResult();
        Rect r = row("Test the link", "Sends one command the measurement unit answers straight back. It is the "
                     "only way to tell whether it can HEAR this board, as opposed to just talk to it." +
                     (res.empty() ? std::string() : "\n\n" + res), 300);
        // The verdict next to the button, where it can be seen without scrolling;
        // the full sentence is under the row.
        if (!res.empty() && !a.probing()) {
            const bool ok = res.compare(0, 8, "ANSWERED") == 0;
            c->label(Rect(r.x, r.y + 12, 140, 40), ok ? (res.compare(0, 14, "ANSWERED after") == 0 ? "RE-ALIGNED" : "ANSWERED") : "NO REPLY",
                     SB16, ok ? th::good : th::bad);
        }
        u->why(ID_D_PROBE, a.probing() ? std::string("Wait: the test is running.") : std::string("Nothing is arriving from the measurement unit: check its cable and its power first."));
        if (u->button(ID_D_PROBE, Rect(r.r() - 150, r.y + 12, 150, 40), a.probing() ? "Waiting\xE2\x80\xA6" : "Test",
                      false, a.connected() && !a.probing()))
            a.startProbe();
    }
    {   // where the records are kept
        Rect r = row("Records", plat::sdStatus() + ". USB stick: " + plat::usbStatus() + ".", 300);
        c->label(Rect(r.x, r.y, 280, r.h), plat::sdReady() ? "recording" : "NOT recording", SB16,
                 plat::sdReady() ? th::good : th::bad);
    }
#if !defined(BIRADX_ITDS)
    {   // the instrument's own memory (H7-2.11.1: 2 to 16 MB, as the chip says)
        const plat::IntMem st = plat::internalMemory();
        const int mb = plat::internalMemoryMb();
        Rect r = row("Internal memory", (mb > 0 ? std::to_string(mb) + " MB" : std::string("The memory")) +
                     " on the board itself, used for the records when no SD card is in. Now: " +
                     plat::internalMemoryStatus() + "." +
                     (st == plat::IntMem::Ready ? std::string() : " [" + plat::internalMemoryChip() + "]"), 300);
        const bool inUse = plat::recordsOnInternal();
        c->label(Rect(r.x, r.y, 170, r.h),
                 st == plat::IntMem::Absent ? "not found" : st == plat::IntMem::Foreign ? "not in use" : inUse ? "in use" : "ready",
                 SB16, st == plat::IntMem::Ready ? th::good : st == plat::IntMem::Foreign ? th::warn : th::bad);
        if (st == plat::IntMem::Foreign &&
            u->button(ID_D_IM, Rect(r.r() - 110, r.y + (r.h - 40) / 2, 110, 40), "Use it\xE2\x80\xA6", true, true)) {
            m_imSheet = true;
            m_imMsg.clear();
        }
    }
#endif
    {   // clock
        const std::string prob = a.clockProblem();
#if defined(BIRADX_ITDS)
        Rect r = row("Date and time", prob.empty() ? "This computer's clock. Case ids, file names and the log's time "
                     "windows are derived from it." : prob, 300);
#else
        Rect r = row("Date and time", prob.empty() ? "The battery clock. Case ids, file names and the log's time "
                     "windows are derived from it." : prob, 300);
#endif
        const plat::Clock n = a.nowLocal();
        char b[32]; std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", n.year, n.month, n.day, n.hour, n.min);
        c->label(Rect(r.x, r.y, 180, r.h), b, M14, prob.empty() ? th::text : th::bad);
        if (u->button(ID_D_CLOCK, Rect(r.r() - 110, r.y + (r.h - 40) / 2, 110, 40), "Set", !prob.empty(), true)) {
            m_clockVal = n;
            if (m_clockVal.year < 2024) { m_clockVal.year = 2026; m_clockVal.month = 1; m_clockVal.day = 1; }
            m_clockEdit = true;
        }
    }
    {   // UTC offset
        Rect r = row("Time zone of the clock", "Offset from UTC. epoch_ms and the ISO times in the records are "
                     "UTC, as on the Jetson; the screen and the file names use local time.", 300);
        char b[32]; std::snprintf(b, sizeof b, "UTC%+d:%02d", s.utcOffsetMin / 60, std::abs(s.utcOffsetMin % 60));
        c->label(Rect(r.x, r.y, 130, r.h), b, M14, th::text);
        auto set = [this](int d) {
            app::Settings e = a.settings();
            e.utcOffsetMin = std::max(-720, std::min(840, e.utcOffsetMin + d));
            std::string why;
            if (!a.saveSettings(e, &why)) say(why, true);
        };
        if (u->button(ID_D_UTCM, Rect(r.r() - 150, r.y + (r.h - 40) / 2, 70, 40), "\xE2\x88\x92 15", false, true)) set(-15);
        if (u->button(ID_D_UTCP, Rect(r.r() - 70, r.y + (r.h - 40) / 2, 70, 40), "+ 15", false, true)) set(15);
    }
    {   // needle revision
        Rect r = row("Needle revision", "A model is only valid for the needle it was fitted on. This is "
                     "configuration, never taken from the model file.", 300);
        c->label(Rect(r.x, r.y, 150, r.h), s.needleRev, S16, th::text);
        if (u->button(ID_D_REV, Rect(r.r() - 110, r.y + (r.h - 40) / 2, 110, 40), "Change", false, true))
            openText("Needle revision", s.needleRev, 16, [this](const std::string& v) {
                if (v.empty()) { say("A needle revision cannot be empty.", true); return; }
                app::Settings e = a.settings(); e.needleRev = v;
                std::string why;
                if (a.saveSettings(e, &why)) say("Needle revision set to " + v + ".");
                else say(why, true);
            });
    }
    {
        Rect r = row("Unparsed lines", "On this instrument an unparsed line usually means a framing problem or "
                     "the wrong baud rate.", 300);
        c->label(Rect(r.x, r.y, 150, r.h), std::to_string(a.unparsedCount()), M14, th::text);
    }
#if !defined(BIRADX_ITDS)   // the ITDS screen is a monitor: its brightness is its own
    {   // brightness
        Rect r = row("Screen brightness", "", 300);
        char b[16]; std::snprintf(b, sizeof b, "%d %%", (s.backlight - 20) * 100 / 180);
        c->label(Rect(r.x, r.y, 100, r.h), b, M14, th::text);
        auto set = [this](int d) {
            app::Settings e = a.settings();
            e.backlight = std::max(20, std::min(200, e.backlight + d));
            std::string why;
            if (!a.saveSettings(e, &why)) say(why, true);
        };
        if (u->button(ID_D_BLM, Rect(r.r() - 150, r.y + (r.h - 40) / 2, 70, 40), "\xE2\x88\x92", false, true)) set(-20);
        if (u->button(ID_D_BLP, Rect(r.r() - 70, r.y + (r.h - 40) / 2, 70, 40), "+", false, true)) set(20);
    }
#endif
    {
        Rect r = row("Software", std::string(BIRADX_VERSION) + " \xC2\xB7 built " + plat::buildDate() +
                     " \xC2\xB7 behaves as BIRADx " + BIRADX_PARITY + " \xC2\xB7 settings memory " +
                     std::to_string(a.configBytes()) + " of " + std::to_string(int(app::ConfigStore::capacity())) + " bytes", 0);
        (void)r;
    }
    contentH = y + off - body.y;
    c->resetClip();
}

// ---------------------------------------------------------------------------

void Gui::tabProtocol(const Rect& body)
{
    static int contentH = 900;
    const int off = u->scroll(ID_S_SCROLL + 1, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    app::Settings& e = m_edit;
    auto row = [&](const std::string& l, const std::string& n, int rightW) {
        const int h = settingRow(Rect(body.x, y, W, 0), l, n, rightW);
        const Rect right = m_rowR;
        y += h + 8;
        return right;
    };
    auto stepper = [&](const Rect& r, int idm, int idp, int* v, int lo, int hi) {
        c->label(Rect(r.x, r.y, r.w - 160, r.h), std::to_string(*v), MB24, th::text, gfx::Right);
        u->why(idm, "This is the smallest: " + std::to_string(lo) + "."); u->why(idp, "This is the largest: " + std::to_string(hi) + ".");
        if (u->button(idm, Rect(r.r() - 150, r.y + (r.h - 44) / 2, 70, 44), "\xE2\x88\x92", false, *v > lo)) --*v;
        if (u->button(idp, Rect(r.r() - 70, r.y + (r.h - 44) / 2, 70, 44), "+", false, *v < hi)) ++*v;
    };
    if (!m_msg.empty()) {
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 4, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 4, 1.25f);
        y += h + 8;
    }
    stepper(row("Readings per lesion", "The quota a case runs to when it does not turn positive. Five is what "
                "the trial data supports: the standard error of the median falls as \xE2\x88\x9An, so two to five "
                "cuts the case-level uncertainty by about 40%.", 260), ID_P_RM, ID_P_RP, &e.readings, 1, 20);
    {
        Rect r = row("Stop early on a positive", "More passes cannot change the management once the case is "
                     "positive, so they have cost and no benefit.", 260);
        if (u->toggle(ID_P_EARLY, r, e.earlyStop)) e.earlyStop = !e.earlyStop;
    }
    e.minForEarlyStop = std::min(e.minForEarlyStop, e.readings);
    stepper(row("Not before this many readings", "The case answer is the median: with two readings the median "
                "is their mean, so one stray placement half-carries the case. From three on, one outlier cannot "
                "move it at all.", 260), ID_P_MM, ID_P_MP, &e.minForEarlyStop, 1, e.readings);
    // The spread limit is a line of device.ini on the Jetson; this instrument
    // has no file to edit, so it is set here. (The placement gate that stood
    // beside it in 1.3.2 is gone: the rule table alone decides.)
    {
        char b[64]; std::snprintf(b, sizeof b, "%.1f IPS", e.maxSpreadIps);
        Rect r = row("Max IPS spread", "The readings of one lesion may differ by this much, after the one furthest from "
                     "the middle is set aside; beyond it the case is Invalid.", 260);
        c->label(Rect(r.x, r.y, r.w - 120, r.h), b, MB19, e.maxSpreadIps != a.settings().maxSpreadIps ? th::warn : th::text, gfx::Right);
        if (u->button(ID_P_SPREAD, Rect(r.r() - 110, r.y + (r.h - 44) / 2, 110, 44), "Change", false, true))
            openNumber("Max IPS spread", n2s(e.maxSpreadIps), "IPS", 0.1, 100, true, false, true,
                       [this](const std::string& v) { const double d = std::atof(v.c_str()); if (d >= 0.1) m_edit.maxSpreadIps = d; });
    }
    {
        const app::Settings& s = a.settings();
        const bool dirty = e.readings != s.readings || e.earlyStop != s.earlyStop || e.minForEarlyStop != s.minForEarlyStop ||
                           e.maxSpreadIps != s.maxSpreadIps;
        Rect r = row("Apply", dirty ? "Changed but not yet in force." : "The instrument is running these numbers.", 260);
        u->why(ID_P_REVERT, "Nothing was changed."); u->why(ID_P_APPLY, "Nothing was changed: change a value first.");
        if (u->button(ID_P_REVERT, Rect(r.r() - 250, r.y + (r.h - 40) / 2, 110, 40), "Revert", false, dirty)) m_edit = a.settings();
        if (u->button(ID_P_APPLY, Rect(r.r() - 130, r.y + (r.h - 40) / 2, 130, 40), "Apply", true, dirty)) {
            app::Settings n = a.settings();
            n.readings = e.readings; n.earlyStop = e.earlyStop; n.minForEarlyStop = e.minForEarlyStop;
            n.maxSpreadIps = e.maxSpreadIps;
            std::string why;
            if (a.saveSettings(n, &why)) say("In force, and applied to the open case.");
            else say(why, true);
            m_edit = a.settings();
        }
    }
    c->text(body.x + 4, y + 20, "WHAT PRODUCES THE RESULT", SB12, th::textFaint);
    y += 30;
    {
        Rect r = row("Decision source", "The rule table (Settings \xE2\x86\x92 Rules). This instrument decides with "
                     "the rules only; the AI-model option of the Jetson is not carried.", 260);
        c->label(r, "rule table", SB16, th::accent, gfx::Right);
    }
    contentH = y + off - body.y;
    c->resetClip();
}

// ---------------------------------------------------------------------------

void Gui::tabNeedle(const Rect& body)
{
    static int contentH = 1100;
    const int off = u->scroll(ID_S_SCROLL + 2, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    auto row = [&](const std::string& l, const std::string& n, int rightW) {
        const int h = settingRow(Rect(body.x, y, W, 0), l, n, rightW);
        const Rect right = m_rowR;
        y += h + 8;
        return right;
    };
    auto save = [this](const app::Settings& n) {
        std::string why;
        if (!a.saveSettings(n, &why)) say(why, true);
    };
    const app::Settings& s = a.settings();
    if (!m_msg.empty()) {
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 4, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 4, 1.25f);
        y += h + 8;
    }
    {
        Rect r = row("Calibration in force", a.needleReady() ? a.needleCalibration()
                     : "No needle is calibrated. End case retires the calibration, so every case starts here.", 200);
        c->label(r, a.needleReady() ? "calibrated" : "not calibrated", SB16, a.needleReady() ? th::good : th::bad, gfx::Right);
    }
    {
        Rect r = row("Gain ranges learned", a.rangeCalSummary(), 200);
        (void)r;
    }
    {
        Rect r = row("Verify needle codes", s.needleCheck
                     ? "A lot and serial that the key does not vouch for is refused, and a serial is never accepted twice."
                     : "OFF for the trials: any lot and serial is accepted and recorded exactly as typed. The codes on "
                       "the boxes are not yet issued by the key this device holds, and a refused label would stop a real test.", 200);
        if (u->toggle(ID_N_CHECK, r, s.needleCheck)) { app::Settings n = s; n.needleCheck = !n.needleCheck; save(n); }
    }
    {
        Rect r = row("Needle key", "Without it every needle code reads INVALID \xE2\x80\x94 which is the intended "
                     "failure: it refuses rather than accepting anything. Copied to records/needle_keys.txt.", 200);
        c->label(r, a.needleKeyInstalled() ? "installed" : "not installed", S16, th::textDim, gfx::Right);
    }
    // H7-2.4.0: the needle check before each case
    {
        const char* opts[3] = { "Off", "Lots that need it", "Every needle" };
        Rect r = row("Needle check before each case",
                     s.salineCheck == 2 ? "Every needle (QC): Air check, then the tip 10 s in the check saline, against the "
                                          "limits its lot's good needles set in Settings \xE2\x86\x92 Lots (carried in the lot "
                                          "file), or generic ones for a lot not in it. A needle outside them is refused: no reading "
                                          "of it, from the screen or the pedal. Where the lot asks for it, its readings are carried "
                                          "to the reference needle's with its own model."
                   : s.salineCheck == 1 ? "Only needles of a lot approved \"per needle\" are checked; the others get their lot's "
                                          "correction from the lot file."
                                        : "Bench use only: no needle QC, the lot file's corrections only (a per-needle lot is "
                                          "still checked). A needle is then never refused.", 456);
        for (int k = 0; k < 3; ++k)
            if (u->chip(ID_N_DIP + k, Rect(r.x + k * 154, r.y + (r.h - 40) / 2, 148, 40), opts[k], s.salineCheck == k)) {
                app::Settings n = s; n.salineCheck = k; save(n);
            }
    }
    {
        Rect r = row("Check saline",
                     s.checkSolution == 0 ? "0.9 %: the sterile normal saline from the shelf, poured into a sterile cup. A lot "
                                            "must have been validated with it (Settings \xE2\x86\x92 Lots \xE2\x86\x92 Result)."
                                          : "0.09 %: one part 0.9 % to nine parts water for injection, made sterile by the pharmacy.", 310);
        const char* opts[2] = { "0.9 %", "0.09 %" };
        for (int k = 0; k < 2; ++k)
            if (u->chip(ID_N_SOL + k, Rect(r.x + k * 156, r.y + (r.h - 40) / 2, 150, 40), opts[k], s.checkSolution == k)) {
                app::Settings n = s; n.checkSolution = k; save(n);
            }
    }
    c->text(body.x + 4, y + 20, "REQUIRED BEFORE A READING", SB12, th::textFaint);
    y += 30;
    struct Req { const char* l; const char* n; bool app::Settings::*f; bool allowed; };
    const Req reqs[5] = {
        { "Needle calibrated in air", "Six seconds per needle. 100 pF of uncorrected stray capacitance shifts IPS by 12 "
          "units and the whole diagnostic span is 4.6 \xE2\x80\x94 this is not a formality. Only when the rule ranges were "
          "found with air-calibrated needles (Settings \xE2\x86\x92 Rules); otherwise it is not asked for.", &app::Settings::reqNeedleCal, true },
        { "Intake form complete", "A reading with no case can never be joined to a pathology result, so it teaches "
          "nothing. Off during bench work.", &app::Settings::reqIntake, true },
        { "Gain ranges calibrated", "The phase offset between gain ranges, learned from the curve's own continuity. "
          "Turn on once the instrument has seen enough sweeps to have learned them.", &app::Settings::reqRangeCal, true },
        { "Lot covered by the lot calibration file", "On: no reading until the needle lot typed at intake is in the lot "
          "calibration file (Settings \xE2\x86\x92 Lots). Off: a lot in the file is corrected, any other is measured as it "
          "is, and the needle line says so.", &app::Settings::reqLotFile, true },
        { "Healthy-tissue sample taken", "One sweep in the patient's own normal tissue, before the lesion is entered "
          "(Healthy tissue, on the needle line of the main screen). Its values are kept with the case and shown in "
          "DETAILS beside every reading. On: no reading of an open case until it has been taken. Off: it is offered, "
          "not demanded.", &app::Settings::reqTissueRef, true },
    };
    for (int i = 0; i < 5; ++i) {
        Rect r = row(reqs[i].l, reqs[i].n, 200);
        const bool on = s.*(reqs[i].f);
        const bool usable = !(i == 0 && !a.airCalUsed());
        u->why(ID_N_REQ + i, "Not asked for: the rule ranges were found without air-calibrated needles (Settings \xE2\x86\x92 Rules).");
        if (u->toggle(ID_N_REQ + i, r, on && usable, usable && (reqs[i].allowed || on))) { app::Settings n = s; n.*(reqs[i].f) = !on; save(n); }
    }
    {
        const bool can = a.canMeasure();
        Rect r = row("Right now", can ? "Nothing is blocking a reading." : a.measureBlocker(), 200);
        c->label(r, can ? "\xE2\x9C\x93" : "\xE2\x9C\x95", SB24, can ? th::good : th::warn, gfx::Right);
    }
    contentH = y + off - body.y;
    c->resetClip();
}

// ---------------------------------------------------------------------------
// the rule table editor (SettingsPage.qml "margins")

void Gui::tabRules(const Rect& body0)
{
    // a save bar fixed at the bottom
    const Rect bar(body0.x, body0.b() - 60, body0.w, 60);
    const Rect body(body0.x, body0.y, body0.w, body0.h - 66);
    static int contentH = 2000;
    const int off = u->scroll(ID_S_SCROLL + 3, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    app::RulesConfig& R = m_rules;

    y += c->textBox(Rect(body.x, y, W, 200),
        "Each rule is a Z(1kHz) range and an IPS range; both bounds are inclusive. Rules are tested top to "
        "bottom and the first match wins. THIS TABLE ALONE DECIDES: there is no placement gate and no hidden "
        "fallback. A reading that falls between rules is given the nearest rule. The LABEL is yours to write and is shown exactly as typed; COUNTS AS is "
        "separate, because a lesion's answer is the median of its five readings and words have no order. Only a "
        "sweep that is itself broken (incomplete, unstable, a short) is refused before the table is looked at.", S13, th::textDim, gfx::Left, 0, 1.25f) + 10;

    // H7-2.1.0: what the numbers of this table were measured with
    {
        const bool air = a.airCalUsed();
        const std::string note = air
            ? "The trial's needles were calibrated in air (six seconds) before use. So is every needle here: the "
              "air calibration is asked for and its correction is applied to every reading."
            : "The trial's needles were used as they came, with no air calibration (as the previous software "
              "did). So readings here are taken the same way: the air calibration is not asked for and not "
              "applied. (Taken anyway, it is used only to bring a lot in the lot file to the reference needle.)";
        const int h = settingRow(Rect(body.x, y, W, 0), "The ranges were found with", note, 380);
        const Rect r = m_rowR;
        const int bw = 184;
        auto set = [this](bool on) { const std::string w = a.setRulesAirCal(on); if (!w.empty()) say(w, true); };
        if (u->chip(ID_R_AIR1, Rect(r.x, r.y + (r.h - 40) / 2, bw, 40), "air-calibrated", air)) set(true);
        if (u->chip(ID_R_AIR2, Rect(r.x + bw + 8, r.y + (r.h - 40) / 2, bw, 40), "not calibrated", !air)) set(false);
        y += h + 8;
        if (!m_msg.empty() && m_msgBad) {
            const int mh = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 4, 1.25f) + 20;
            u->card(Rect(body.x, y, W, mh), th::badSoft, th::bad);
            c->textBox(Rect(body.x + 14, y + 10, W - 28, mh - 20), m_msg, SB14, th::bad, gfx::Left, 4, 1.25f);
            y += mh + 8;
        }
    }

    // what the table does wrong, if anything
    const std::vector<std::string> probs = R.problems();
    {
        std::string t;
        for (const std::string& p : probs) t += "\xE2\x80\xA2 " + p + "\n";
        if (!t.empty()) {
            t.pop_back();
            const int h = gfx::Canvas::wrapHeight(W - 28, t, S13, 0, 1.25f) + 20;
            u->card(Rect(body.x, y, W, h), th::warnSoft, th::warn);
            c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), t, S13, th::warn, gfx::Left, 0, 1.25f);
            y += h + 8;
        }
    }

    auto colorPick = [&](int idBase, int x, int yy, std::string* col) {
        for (int k = 0; k < 8; ++k) {
            const Rect sw(x + k * (46 + 7), yy, 46, 34);
            if (u->clicked(idBase + k, sw)) *col = kPalette[k];
            Color cc; gfx::parseColor(kPalette[k], &cc);
            const bool sel = *col == kPalette[k];
            c->roundRect(sw, th::radiusSm, sel ? th::text : th::stroke);
            c->roundRect(sw.inset(sel ? 3 : 1), th::radiusSm - 1, cc);
        }
        // "none" = use the level's colour
        const Rect sw(x + 8 * 53, yy, 70, 34);
        if (u->clicked(idBase + 8, sw)) col->clear();
        u->card(sw, th::surfaceIn, col->empty() ? th::text : th::stroke);
        c->label(sw, "level", S13, th::textDim, gfx::Center);
    };
    auto levelPick = [&](int idBase, int x, int yy, int w, std::string* lvl, const std::string& label) {
        const char* keys[4] = { "", "low", "suspicious", "high" };
        const char* names[4] = { "from the words", "Low", "Watch", "High" };
        const std::string cur = (*lvl == "low" || *lvl == "suspicious" || *lvl == "high") ? *lvl : "";
        const int bw = (w - 18) / 4;
        for (int k = 0; k < 4; ++k)
            if (u->chip(idBase + k, Rect(x + k * (bw + 6), yy, bw, 40), names[k], cur == keys[k])) *lvl = keys[k];
        if (cur.empty()) {
            const birx::Tier g = birx::RuleTable::tierOfLabel(label);
            std::string t; Color col = th::textFaint;
            if (g == birx::Tier::Invalid) { t = "These words read as INVALID \xE2\x80\x94 the instrument will refuse every reading that matches this rule. Set the level yourself."; col = th::bad; }
            else if (g == birx::Tier::Suspicious) { t = "These words are not ones the instrument recognises, so it counts this rule as Watch. Set the level yourself if that is not what it means."; col = th::warn; }
            else t = std::string("These words read as ") + (g == birx::Tier::High ? "High." : "Low.");
            return c->textBox(Rect(x, yy + 46, w, 60), t, S12, col, gfx::Left, 3, 1.2f) + 46;
        }
        return 46;
    };
    auto soundPick = [&](int idBase, int x, int yy, int w, std::string* snd) {
        const auto& lib = app::App::soundLibrary();
        int cx = x, cy = yy;
        auto chip = [&](int k, const std::string& t, bool on) {
            const int cw = gfx::Canvas::measure(t, S13) + 24;
            if (cx + cw > x + w) { cx = x; cy += 40; }
            const Rect r(cx, cy, cw, 34);
            const bool click = u->clicked(idBase + k, r);
            if (on) c->roundRect(r, th::radiusSm, th::accent); else u->card(r, th::surfaceIn, th::stroke);
            c->label(r, t, S13, on ? th::bg : th::text, gfx::Center);
            cx += cw + 6;
            return click;
        };
        if (chip(0, "silent", snd->empty())) snd->clear();
        for (size_t k = 0; k < lib.size(); ++k) {
            char b[16]; std::snprintf(b, sizeof b, "%d", int(k + 1));
            if (chip(int(k) + 1, b, app::App::trackOf(*snd) == int(k) + 1)) *snd = lib[k];
        }
        const int tk = app::App::trackOf(*snd);
        std::string name = tk < 1 ? "says nothing aloud" : lib[size_t(tk - 1)];
        c->text(x, cy + 34 + 16, name, S12, th::textFaint);
        return cy + 34 + 22 - yy;
    };
    auto numField = [&](int id, const Rect& r, const char* lab, double* v, bool neg) {
        c->text(r.x, r.y + 12, lab, S12, th::textDim);
        const Rect box(r.x, r.y + 18, r.w, 40);
        if (u->clicked(id, box))
            openNumber(lab, n2s(*v), "", 0, 0, false, neg, true, [v](const std::string& s) {
                double d = 0; if (birx::birxStrtod(s, &d) && std::isfinite(d)) *v = d; });
        u->card(box, u->pressed(id) ? th::accentSoft : th::surfaceIn, th::stroke);
        c->label(box.inset(10, 0), n2s(*v), M14, th::text);
    };

    for (size_t i = 0; i < R.rows.size(); ++i) {
        app::RuleRowCfg& r = R.rows[i];
        const int id = ID_R_ROW + int(i) * 40;
        const bool open = m_ruleOpen == int(i);
        Color col = th::textFaint; gfx::parseColor(r.color, &col);
        if (!open) {
            // ITDS-2.16.0: on a narrow screen the ranges on a third line, the level and sound on the first
            const bool narrow = W < 800;
            const Rect card(body.x, y, W, narrow ? 80 : 56);
            if (u->clicked(id, card)) m_ruleOpen = int(i);
            u->card(card, u->pressed(id) ? th::accentSoft : th::surface, th::stroke);
            c->roundRect(Rect(card.x + 12, card.y + 12, 8, 32), 3, col);
            char b[200];
            std::snprintf(b, sizeof b, "Z %s to %s \xCE\xA9  \xC2\xB7  IPS %s to %s", n2s(r.zMin).c_str(),
                          n2s(r.zMax).c_str(), n2s(r.ipsMin).c_str(), n2s(r.ipsMax).c_str());
            c->text(card.x + 30, card.y + 23, std::string("RULE ") + std::to_string(i + 1) + (r.enabled ? "" : "  (off)"), M11, th::textFaint);
            c->text(card.x + 30, card.y + 44, gfx::Canvas::elide(r.label, SB16, narrow ? card.w - 50 : 260), SB16, r.enabled ? col : th::textFaint);
            c->text(narrow ? card.x + 30 : card.x + 310, narrow ? card.y + 66 : card.y + 34, b, M12, th::textDim);
            const std::string lv = r.level.empty() || r.level == "invalid" ? "from the words" : r.level;
            c->label(narrow ? Rect(card.r() - 330, card.y + 8, 320, 20) : Rect(card.r() - 260, card.y, 250, card.h),
                     lv + (app::App::trackOf(r.sound) < 1 ? std::string() : " \xC2\xB7 " + app::App::soundLibrary()[size_t(app::App::trackOf(r.sound) - 1)]), S12,
                     th::textFaint, gfx::Right);
            y += card.h + 8;
            continue;
        }
        // expanded editor
        const int y0 = y;
        int h = 0;
        const Rect card(body.x, y0, W, 10);  // height known after drawing; draw border after
        (void)card;
        int yy = y0 + 14;
        const int x0 = body.x + 14, iw = W - 28;
        c->text(x0, yy + 12, "RULE " + std::to_string(i + 1), SB12, th::textFaint);
        // row tools: up, down, enabled, delete, close
        const int tb = 90;
        int tx = x0 + iw - 5 * (tb + 6);
        u->why(id + 1, "It is the first rule already."); u->why(id + 2, "It is the last rule already.");
        if (u->button(id + 1, Rect(tx, yy - 6, tb, 36), "\xE2\x86\x91 up", false, i > 0)) { std::swap(R.rows[i], R.rows[i - 1]); m_ruleOpen = int(i) - 1; }
        tx += tb + 6;
        if (u->button(id + 2, Rect(tx, yy - 6, tb, 36), "\xE2\x86\x93 down", false, i + 1 < R.rows.size())) { std::swap(R.rows[i], R.rows[i + 1]); m_ruleOpen = int(i) + 1; }
        tx += tb + 6;
        if (u->button(id + 3, Rect(tx, yy - 6, tb, 36), r.enabled ? "on" : "off", r.enabled, true)) r.enabled = !r.enabled;
        tx += tb + 6;
        { ui::Ui::Btn st; st.primary = false; st.danger = true; if (u->button(id + 4, Rect(tx, yy - 6, tb, 36), "delete", st)) { R.rows.erase(R.rows.begin() + long(i)); m_ruleOpen = -1; c->resetClip(); return; } }
        tx += tb + 6;
        if (u->button(id + 5, Rect(tx, yy - 6, tb, 36), "done", true, true)) m_ruleOpen = -1;
        yy += 40;
        const int fw = (iw - 3 * th::gap) / 4;
        numField(id + 6, Rect(x0, yy, fw, 60), "Z min", &r.zMin, false);
        numField(id + 7, Rect(x0 + (fw + th::gap), yy, fw, 60), "Z max", &r.zMax, false);
        numField(id + 8, Rect(x0 + 2 * (fw + th::gap), yy, fw, 60), "IPS min", &r.ipsMin, true);
        numField(id + 9, Rect(x0 + 3 * (fw + th::gap), yy, fw, 60), "IPS max", &r.ipsMax, true);
        yy += 68;
        c->text(x0, yy + 12, "Label shown on screen", S12, th::textDim);
        {
            const Rect box(x0, yy + 18, iw, 40);
            if (u->clicked(id + 10, box)) openText("Label shown on screen", r.label, 40, [&r](const std::string& v) { r.label = v; });
            u->card(box, u->pressed(id + 10) ? th::accentSoft : th::surfaceIn, th::stroke);
            c->label(box.inset(10, 0), r.label.empty() ? "tap to type" : r.label, SB16, r.label.empty() ? th::textFaint : col);
        }
        yy += 66;
        c->text(x0, yy + 12, "Colour it is shown in", S12, th::textDim);
        colorPick(id + 11, x0, yy + 18, &r.color);
        yy += 60;
        c->text(x0, yy + 12, "Counts as", S12, th::textDim);
        yy += 18 + levelPick(id + 21, x0, yy + 18, std::min(iw, 600), &r.level, r.label);
        c->text(x0, yy + 12, "Plays", S12, th::textDim);
        yy += 18 + soundPick(ID_R_FIELD + int(i) * 20, x0, yy + 18, iw, &r.sound) + 8;
        h = yy - y0 + 6;
        c->roundFrame(Rect(body.x, y0, W, h), th::radiusSm, 2, th::accent);
        y += h + 8;
    }
    {
        const int bw = (W - 8) / 2;
        u->why(ID_R_ADD, "12 rules at most: delete one first.");
        if (u->button(ID_R_ADD, Rect(body.x, y, bw, th::touch), "Add rule", false, R.rows.size() < 12)) {
            app::RuleRowCfg n;
            n.zMin = 0; n.zMax = 1900; n.ipsMin = -12; n.ipsMax = 6;
            n.label = "High Risk"; n.color = "#C62828";
            R.rows.push_back(n);
            m_ruleOpen = int(R.rows.size()) - 1;
        }
        if (u->button(ID_R_DEF, Rect(body.x + bw + 8, y, bw, th::touch), "Restore defaults", false, true)) {
            R = app::RulesConfig::defaults();
            m_ruleOpen = -1;
        }
        y += th::touch + 12;
    }
    // case rules
    c->text(body.x + 4, y + 18, "THE CASE RESULT", SB12, th::textFaint);
    y += 26;
    y += c->textBox(Rect(body.x, y, W, 100),
        "The rules above judge one needle placement. These judge the LESION: first by count (High Risk from, Low "
        "Risk from), then the case rules, each reading \"if at least N of this lesion's valid readings said X, the "
        "lesion is Y\" (first match wins), else the median of its readings, as before. The measure screen and the log "
        "both report what these say.",
        S13, th::textDim, gfx::Left, 0, 1.25f) + 8;
    // H7-2.14.0: the case result by count, and the case closed once final
    {
        auto count = [&](const char* title, const std::string& note, int* v, int idm, int idp) {
            const int h = settingRow(Rect(body.x, y, W, 0), title, note, 260);
            const Rect r = m_rowR;
            c->label(Rect(r.x, r.y, r.w - 160, r.h), *v > 0 ? std::to_string(*v) : std::string("off"), MB24,
                     *v > 0 ? th::text : th::textFaint, gfx::Right);
            u->why(idm, "It is off already: the median and the case rules decide."); u->why(idp, "20 readings at most.");
            if (u->button(idm, Rect(r.r() - 150, r.y + (r.h - 44) / 2, 70, 44), "\xE2\x88\x92", false, *v > 0)) --*v;
            if (u->button(idp, Rect(r.r() - 70, r.y + (r.h - 44) / 2, 70, 44), "+", false, *v < 20)) ++*v;
            y += h + 8;
        };
        count("High Risk from", "The lesion is High Risk, and final, once at least this many of its valid readings count as "
              "high (their row's level, below). Tested first. Off: as before, the median and the case rules decide.",
              &R.caseHigh, ID_R_CHM, ID_R_CHP);
        count("Low Risk from", "Else the lesion is Low Risk, and final, once at least this many count as low. Until one of the "
              "two is reached the lesion is provisional (the median so far); at the quota (Protocol \xE2\x86\x92 Readings per "
              "lesion) without either, the case rules and the median decide.", &R.caseLow, ID_R_CLM, ID_R_CLP);
        const int h = settingRow(Rect(body.x, y, W, 0), "Close the case once final",
            "No more readings once the lesion's answer is final (a count above reached, the quota, or Protocol \xE2\x86\x92 "
            "Stop early on a positive): the main button becomes End case, and a sweep from the pedal is no reading.", 200);
        if (u->toggle(ID_R_CCLOSE, m_rowR, R.caseClose)) R.caseClose = !R.caseClose;
        y += h + 12;
    }
    if (R.caseRows.empty()) {
        y += c->textBox(Rect(body.x, y, W, 60), "No case rules: a lesion is the median of its valid readings. That is "
                        "the default and it is a good one \xE2\x80\x94 it does not depend on how many readings were taken.",
                        S13, th::textFaint, gfx::Left, 0, 1.25f) + 8;
    }
    for (size_t i = 0; i < R.caseRows.size(); ++i) {
        app::CaseRuleCfg& cr = R.caseRows[i];
        const int id = ID_R_ROW + 600 + int(i) * 40;
        const int y0 = y;
        int yy = y0 + 14;
        const int x0 = body.x + 14, iw = W - 28;
        c->text(x0, yy + 12, "CASE RULE " + std::to_string(i + 1), SB12, th::textFaint);
        { ui::Ui::Btn st; st.primary = false; st.danger = true;
          if (u->button(id, Rect(x0 + iw - 90, yy - 6, 90, 36), "delete", st)) { R.caseRows.erase(R.caseRows.begin() + long(i)); c->resetClip(); return; } }
        if (u->button(id + 1, Rect(x0 + iw - 186, yy - 6, 90, 36), cr.enabled ? "on" : "off", cr.enabled, true)) cr.enabled = !cr.enabled;
        yy += 36;
        c->text(x0, yy + 26, "If at least", S14, th::textDim);
        {
            const Rect box(x0 + 90, yy + 4, 70, 40);
            double v = cr.minCount;
            if (u->clicked(id + 2, box))
                openNumber("At least this many readings", std::to_string(cr.minCount), "", 1, 20, true, false, false,
                           [&cr](const std::string& s) { cr.minCount = std::atoi(s.c_str()); });
            u->card(box, th::surfaceIn, th::stroke);
            c->label(box, n2s(v), M14, th::text, gfx::Center);
        }
        c->text(x0 + 172, yy + 26, "readings said", S14, th::textDim);
        // ITDS-2.16.0: on a narrow screen "then the lesion is" on a line of its own
        const bool narrow = iw < 800;
        {
            const Rect box(x0 + 290, yy + 4, narrow ? iw - 290 : 260, 40);
            if (u->clicked(id + 3, box)) openText("said (empty = any label)", cr.ofLabel, 40, [&cr](const std::string& s) { cr.ofLabel = s; });
            u->card(box, th::surfaceIn, th::stroke);
            c->label(box.inset(10, 0), cr.ofLabel.empty() ? "any label" : cr.ofLabel, S16, cr.ofLabel.empty() ? th::textFaint : th::text);
        }
        if (narrow) yy += 48;
        const int tx = narrow ? x0 : x0 + 562;
        c->text(tx, yy + 26, "then the lesion is", S14, th::textDim);
        {
            const Rect box(tx + 128, yy + 4, x0 + iw - (tx + 128), 40);
            if (u->clicked(id + 4, box)) openText("then the lesion is", cr.label, 40, [&cr](const std::string& s) { cr.label = s; });
            u->card(box, th::surfaceIn, th::stroke);
            Color cc = th::text; gfx::parseColor(cr.color, &cc);
            c->label(box.inset(10, 0), cr.label.empty() ? "tap to type" : cr.label, SB16, cr.label.empty() ? th::textFaint : cc);
        }
        yy += 52;
        c->text(x0, yy + 12, "Colour", S12, th::textDim);
        colorPick(id + 5, x0, yy + 18, &cr.color);
        yy += 60;
        c->text(x0, yy + 12, "Counts as", S12, th::textDim);
        yy += 18 + levelPick(id + 15, x0, yy + 18, std::min(iw, 600), &cr.level, cr.label);
        c->text(x0, yy + 12, "Plays", S12, th::textDim);
        yy += 18 + soundPick(ID_R_FIELD + 200 + int(i) * 20, x0, yy + 18, iw, &cr.sound) + 8;
        const int h = yy - y0 + 6;
        c->roundFrame(Rect(body.x, y0, W, h), th::radiusSm, 2, th::stroke);
        y += h + 8;
    }
    u->why(ID_R_CASEADD, "6 case rules at most: delete one first.");
    if (u->button(ID_R_CASEADD, Rect(body.x, y, (W - 8) / 2, th::touch), "Add case rule", false, R.caseRows.size() < 6)) {
        app::CaseRuleCfg n; n.minCount = 2; n.ofLabel = "High Risk"; n.label = "High Risk"; n.color = "#C62828"; n.level = "high";
        R.caseRows.push_back(n);
    }
    y += th::touch + 12;
    contentH = y + off - body.y;
    c->resetClip();

    // the save bar
    const bool dirty = app::ConfigStore::serialise([&] { app::Settings t = a.settings(); t.rules = R; return t; }()) !=
                       app::ConfigStore::serialise(a.settings());
    c->fill(Rect(bar.x - th::pad, bar.y - 6, scrW, bar.h + 6 + th::pad), th::bg);
    const std::string note = !m_msg.empty() ? m_msg
                           : dirty ? "Changed but not saved. Saving applies these immediately, including to the lesion "
                                     "already on the measure screen."
                                   : "The instrument is running this table.";
    c->textBox(Rect(bar.x, bar.y, bar.w - 300, bar.h), note, m_msgBad ? SB14 : S13,
               m_msgBad ? th::bad : (dirty ? th::warn : th::textFaint), gfx::Left, 3, 1.2f, true);
    u->why(ID_R_REVERT, "Nothing was changed."); u->why(ID_R_SAVE, "Nothing was changed: change a rule first.");
    if (u->button(ID_R_REVERT, Rect(bar.r() - 290, bar.y + 4, 130, 52), "Revert", false, dirty)) { R = a.settings().rules; m_ruleOpen = -1; m_msg.clear(); }
    if (u->button(ID_R_SAVE, Rect(bar.r() - 150, bar.y + 4, 150, 52), "Save", true, dirty)) {
        const std::string why = a.saveRules(R);
        if (why.empty()) { say("Saved. The open case, if any, was judged again under this table \xE2\x80\x94 nothing recorded was changed."); m_ruleOpen = -1; }
        else say(why, true);
    }
}

// ---------------------------------------------------------------------------

void Gui::tabSound(const Rect& body)
{
    static int contentH = 700;
    const int off = u->scroll(ID_S_SCROLL + 4, body, contentH);
    c->setClip(body);
    int y = body.y - off;
    const int W = body.w - 10;
    auto row = [&](const std::string& l, const std::string& n, int rightW) {
        const int h = settingRow(Rect(body.x, y, W, 0), l, n, rightW);
        const Rect right = m_rowR;
        y += h + 8;
        return right;
    };
    const app::Settings& s = a.settings();
    {
        Rect r = row("Sound", "The speaker button beside the case strip does the same. A rule's sound plays on every "
                     "reading that matches it; nothing plays for the case answer, on End case or when a rule edit "
                     "re-judges the case.", 200);
        if (u->toggle(ID_SND_MUTE, r, !s.muted)) a.setMuted(!s.muted);
    }
    {
#if defined(BIRADX_ITDS)
        Rect r = row("Volume", "5 to 30. Moves the mixer control named in itds.ini ([audio] mixer), when one is named.", 260);
#else
        Rect r = row("Volume", "Of the voice module (5 to 30).", 260);
#endif
        c->label(Rect(r.x, r.y, r.w - 160, r.h), std::to_string(s.volume), MB24, th::text, gfx::Right);
        u->why(ID_SND_VM, "This is the lowest volume."); u->why(ID_SND_VP, "This is the highest volume.");
        if (u->button(ID_SND_VM, Rect(r.r() - 150, r.y + (r.h - 44) / 2, 70, 44), "\xE2\x88\x92", false, s.volume > 5)) a.setVolume(s.volume - 1);
        if (u->button(ID_SND_VP, Rect(r.r() - 70, r.y + (r.h - 44) / 2, 70, 44), "+", false, s.volume < 30)) a.setVolume(s.volume + 1);
    }
    {
        // The recording that repeats while a sweep runs (the previous firmware
        // of this instrument played its track 7 there).
        // ITDS-2.16.0: on a narrow screen the sentence wraps and the chips go on two lines
        const bool narrow = W < 800;
        const char* what = "This recording repeats from the first point of a sweep to its last; then the "
                           "rule's own sound, if it has one, plays.";
        const int whatH = narrow ? gfx::Canvas::wrapHeight(W - 28, what, S13, 3, 1.25f) : 0;
        const Rect card(body.x, y, W, narrow ? 34 + whatH + 8 + 2 * 38 + 6 + 14 : 112);
        u->card(card, th::surface, th::stroke);
        c->text(card.x + 14, card.y + 26, "While a sweep runs", SB16, th::text);
        if (narrow) c->textBox(Rect(card.x + 14, card.y + 34, W - 28, whatH), what, S13, th::textDim, gfx::Left, 3, 1.25f);
        else c->text(card.x + 14, card.y + 46, what, S13, th::textDim);
        int cx = card.x + 14;
        int cy = narrow ? card.y + 34 + whatH + 8 : card.y + 62;
        auto chip = [&](int k, const std::string& t) {
            const int cw = gfx::Canvas::measure(t, S13) + 28;
            if (narrow && cx + cw > card.r() - 14) { cx = card.x + 14; cy += 38 + 6; }
            const Rect r(cx, cy, cw, 38);
            const bool on = s.sweepTrack == k;
            if (u->clicked(ID_SND_SW + k, r) && !on) a.setSweepTrack(k);
            if (on) c->roundRect(r, th::radiusSm, th::accent); else u->card(r, th::surfaceIn, th::stroke);
            c->label(r, t, S13, on ? th::bg : th::text, gfx::Center);
            cx += cw + 6;
        };
        chip(0, "silent");
        for (int k = 1; k <= 12; ++k) chip(k, std::to_string(k));
        y += card.h + 8;
    }
    {
        // What the module itself says: it is asked at start, on "Ask", and
        // 0.4 s after every test below.
#if defined(BIRADX_ITDS)
        Rect r = row("Recordings", plat::voiceStatus() + ".", 130);
#else
        Rect r = row("Voice module", plat::voiceStatus() + ".", 130);
#endif
        if (u->button(ID_SND_ASK, Rect(r.r() - 120, r.y + (r.h - 44) / 2, 120, 44), "Ask", false, true)) {
            plat::voiceProbe();
            a.uiAction("[settings] voice module asked", plat::voiceStatus());
        }
    }
    y += c->textBox(Rect(body.x, y, W, 80),
#if defined(BIRADX_ITDS)
        "The recordings are the files voice/01.wav to voice/12.wav beside the program (the voice module's twelve, in "
        "its order), played with aplay on the device named in itds.ini; a rule names one of them in "
#else
        "The recordings are stored on the voice module itself, numbered 1 to 12; a rule names one of them in "
#endif
        "Settings \xE2\x86\x92 Rules (open a rule, scroll to \"Plays\"). The shipped table assigns none, so no answer "
        "is spoken until a site chooses. Test each one here:", S13, th::textDim, gfx::Left, 0, 1.25f) + 10;
    const auto& lib = app::App::soundLibrary();
    const int bw = (W - 3 * th::gap) / 4;
    for (size_t k = 0; k < lib.size(); ++k) {
        const Rect b(body.x + int(k % 4) * (bw + th::gap), y + int(k / 4) * (52 + th::gap), bw, 52);
        ui::Ui::Btn st; st.primary = false; st.font = &SB14;
        if (u->button(ID_SND_T + int(k), b, lib[k], st)) a.testTrack(int(k) + 1);
    }
    y += int((lib.size() + 3) / 4) * (52 + th::gap);
    contentH = y + off - body.y + 8;
    c->resetClip();
}

void Gui::tabSecurity(const Rect& body)
{
    int y = body.y;
    const int W = body.w - 10;
    if (!m_msg.empty()) {
        const int h = gfx::Canvas::wrapHeight(W - 28, m_msg, SB14, 4, 1.25f) + 20;
        u->card(Rect(body.x, y, W, h), m_msgBad ? th::badSoft : th::goodSoft, m_msgBad ? th::bad : th::good);
        c->textBox(Rect(body.x + 14, y + 10, W - 28, h - 20), m_msg, SB14, m_msgBad ? th::bad : th::good, gfx::Left, 4, 1.25f);
        y += h + 8;
    }
    const int h = settingRow(Rect(body.x, y, W, 0), "Settings passcode",
                             "Four digits, turned on the dials every time Settings is opened.", 260);
    if (u->button(ID_SEC_CHANGE, Rect(m_rowR.r() - 220, m_rowR.y + (m_rowR.h - 44) / 2, 220, 44), "Change passcode", false, true)) {
        m_pass = PassSheet();
        m_pass.open = true;
        m_msg.clear();
    }
    y += h + 16;
    c->textBox(Rect(body.x, y, W, 120),
        "What this lock is for: it keeps the decision rules, the protocol and the needle switch "
        "out of reach of a stray tap in a procedure room. It is not encryption. The passcode is kept in the "
        "settings memory in plain text, on purpose, so that a site can never lock itself out of its own "
        "instrument's settings.", S13, th::textDim, gfx::Left, 0, 1.25f);
}

void Gui::tabAbout(const Rect& body)
{
    int y = body.y + 10;
    const int W = body.w - 10;
    c->text(body.x, y + 22, "Coosa Technologies Private Limited", SB19, th::text);
    y += 32;
    c->text(body.x, y + 18, "BIRADx \xE2\x80\x94 impedimetric tumour detection system", S16, th::textDim);
    y += 26;
    c->text(body.x, y + 18, "Manufactured 2026-04 \xC2\xB7 Delhi, India", S16, th::textDim);
    y += 40;
    // ITDS-2.16.0: on a narrow screen the long lines wrap
    auto line = [&](const std::string& t, Color col) {
        if (!portrait()) { c->text(body.x, y + 18, t, M12, col); y += 26; return; }
        y += 4 + c->textBox(Rect(body.x, y + 6, W, 60), t, M12, col, gfx::Left, 3, 1.3f) + 10;
    };
    line(std::string("Software ") + BIRADX_VERSION + " \xC2\xB7 built " + plat::buildDate() +
         " \xC2\xB7 decision core birx, behaves as BIRADx " + BIRADX_PARITY, th::textFaint);
    line(BIRADX_HARDWARE, th::textFaint);
    // H7-2.7.2: the firmware's timer against the battery clock
    const double tr = a.timerRatio();
    const bool tcBad = tr > 0 && std::fabs(tr - 1.0) > 0.03;
    line("Timer check: " + a.timerCheck(), tcBad ? th::bad : th::textFaint);
    y += 14;
    c->textBox(Rect(body.x, y, W, 100), "The result is an aid to a decision, not the decision. It does not replace "
               "imaging, clinical judgement or histology, and it must never be the sole basis for withholding a biopsy.",
               SB14, th::warn, gfx::Left, 0, 1.3f);
    y += 70;
    c->textBox(Rect(body.x, y, W, 80), "Fonts: Inter and JetBrains Mono (SIL Open Font License 1.1), DejaVu Sans.",
               S12, th::textFaint, gfx::Left, 0, 1.3f);
}

// ---------------------------------------------------------------------------
// The internal memory still holds the previous firmware's records. Using it
// for BIRADx means erasing it, and that is the operator's decision: this
// sheet says what will be lost, offers a copy to a USB stick first, and only
// then erases.
void Gui::intMemSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    const int x0 = th::pad * 2, W = scrW - th::pad * 4;
    c->text(x0, 46, "Use the internal memory for the records", SB24, th::text);
    int y = 70;
    y += c->textBox(Rect(x0, y, W, 120),
                    "This instrument has " + std::to_string(plat::internalMemoryMb()) + " MB of memory of its own, on the board. It still holds what the "
                    "previous firmware stored there \xE2\x80\x94 its own records, in a format this software "
                    "cannot read. To keep BIRADx records on it, it has to be erased, and that cannot be undone.",
                    S16, th::textDim, gfx::Left, 5, 1.3f) + 14;
    y += c->textBox(Rect(x0, y, W, 90),
                    std::to_string(plat::internalMemoryMb()) + " MB holds about " + std::to_string(plat::internalMemoryMb() * 190) +
                    " readings. With an SD card in, the card is used instead and the internal memory is left alone.", S14, th::textFaint, gfx::Left, 3, 1.3f) + 22;
    const bool usb = plat::usbReady();
    u->why(ID_IM_SAVE, "No USB stick is mounted: plug one in (FAT32 or exFAT, 2 MB free at least).");
    if (u->button(ID_IM_SAVE, Rect(x0, y, 420, th::touch), "Save old contents to USB first", false, usb)) {
        std::string res;
        const bool ok = plat::internalMemorySaveOld(&res);
        m_imMsg = ok ? "Saved on the USB stick as " + res + "." : "NOT saved: " + res + ".";
        m_imMsgOk = ok;
        a.event("internal memory, old contents to USB: " + m_imMsg);
    }
    c->textBox(Rect(x0 + 440, y + 8, W - 440, 44),
               usb ? "Copies the chip exactly as it is, as one " + std::to_string(plat::internalMemoryMb()) +
                     " MB file. Takes about " + std::to_string(std::max(10, plat::internalMemoryMb() * 4)) + " seconds."
                   : "Plug in a USB stick to make a copy of what is on the chip now.",
               S13, th::textFaint, gfx::Left, 2, 1.25f);
    y += th::touch + 16;
    if (!m_imMsg.empty())
        y += c->textBox(Rect(x0, y, W, 50), m_imMsg, SB16, m_imMsgOk ? th::good : th::bad, gfx::Left, 2, 1.2f) + 10;
    const int by = scrH - th::touch - th::pad * 2;
    if (u->button(ID_IM_CANCEL, Rect(x0, by, 220, th::touch), "Cancel", false, true)) m_imSheet = false;
    ui::Ui::Btn b;
    b.primary = true;
    b.danger = true;
    if (u->button(ID_IM_ERASE, Rect(scrW - x0 - 420, by, 420, th::touch), "Erase it and use it for the records", b)) {
        const std::string r = a.prepareInternalMemory();
        m_imSheet = false;
        say(r, r.compare(0, 22, "The internal memory is") != 0);
    }
}

// ---------------------------------------------------------------------------
// The link monitor: every line to and from the measurement unit, with the
// time it passed, as it happens; and two buttons that put one frame on the
// wire and do nothing else. It answers "what does the unit send, and how
// long after being asked" by looking, instead of by reasoning.
void Gui::monitorSheet()
{
    c->fill(Rect(0, 0, scrW, scrH), th::bg);
    const int x0 = th::pad * 2, W = scrW - th::pad * 4;
    const uint32_t now = a.nowTicks();
    char hd[160];
    std::snprintf(hd, sizeof hd, "now %u.%03u s  \xC2\xB7  sent %d, refused %d  \xC2\xB7  %d heartbeats  \xC2\xB7  %u UART errors",
                  unsigned(now / 1000), unsigned(now % 1000), a.framesSent(), a.framesFailed(), a.heartbeats(),
                  unsigned(plat::linkErrors()));
    c->text(x0, 38, "Link monitor", SB24, th::text);
    // ITDS-2.16.0: on a narrow screen the counters on a line of their own
    const bool narrow = portrait();
    c->text(narrow ? x0 : x0 + 200, narrow ? 60 : 36, gfx::Canvas::elide(hd, M12, narrow ? W : W - 200), M12, th::textDim);
    c->text(x0, narrow ? 80 : 58, gfx::Canvas::elide("time since switch-on, newest at the bottom; heartbeats are counted, not listed", S12, W),
            S12, th::textFaint);

    // newest last; runs of heartbeats collapsed into one line
    std::vector<std::string> lines;
    const std::deque<std::string>& tr = a.traffic();
    int beats = 0;
    std::string lastBeat;
    auto flush = [&]() {
        if (!beats) return;
        char b[96];
        // the time of the LAST heartbeat of the run is the one that matters for a latency
        std::snprintf(b, sizeof b, "%s  \xC3\x97%d", lastBeat.c_str(), beats);
        lines.push_back(b);
        beats = 0;
    };
    for (auto it = tr.rbegin(); it != tr.rend(); ++it) {
        if (it->find("RX  [C]") != std::string::npos) { lastBeat = *it; ++beats; continue; }
        flush();
        lines.push_back(*it);
    }
    flush();
    const int rowH = 20, top = narrow ? 92 : 72, rows = (scrH - top - 84) / rowH;
    const int first = int(lines.size()) > rows ? int(lines.size()) - rows : 0;
    u->card(Rect(x0, top, W, rows * rowH + 10), th::surface, th::stroke);
    for (int i = first; i < int(lines.size()); ++i) {
        const std::string& l = lines[size_t(i)];
        const bool tx = l.find("TX") != std::string::npos;
        const bool note = l.find("--") != std::string::npos;
        c->text(x0 + 12, top + 18 + (i - first) * rowH, gfx::Canvas::elide(l, M14, W - 24), M14,
                tx ? th::accent : note ? th::warn : th::text);
    }
    if (lines.empty()) c->label(Rect(x0, top, W, rows * rowH), "nothing sent or received yet", S14, th::textFaint, gfx::Center);

    const int by = scrH - th::touch - th::pad;
    const int sbw = narrow ? 180 : 230;
    if (u->button(ID_MON_R, Rect(x0, by, sbw, th::touch), "Send [R]", true, true)) a.sendRaw("[R]");
    if (u->button(ID_MON_T, Rect(x0 + sbw + 10, by, sbw, th::touch), "Send [T]", false, true)) a.sendRaw("[T]");
    if (!narrow)
        c->textBox(Rect(x0 + 490, by + 6, W - 490 - 180, 44), "Each press puts those three bytes on the wire and nothing "
                   "else. [R] asks for a sweep.", S12, th::textFaint, gfx::Left, 2, 1.2f);
    if (u->button(ID_MON_CLOSE, Rect(scrW - x0 - 160, by, 160, th::touch), "Close", false, true)) m_monitor = false;
    m_timerAt = u->now() + 200;         // it is a live view
}

} // namespace gui
