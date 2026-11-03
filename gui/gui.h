// gui/gui.h — every screen of BIRADx 2.18.0, drawn for the 1024 x 600 panel, and (ITDS-2.16.0)
// upright for an upright monitor: 600 wide, 960 to 1100 high (portrait(); scrW, scrH).
//
// Screens (Main.qml): measure, logs, lock, settings, intake. Sheets drawn over
// them: details (DetailsBox.qml), needle calibration (CalibrateSheet.qml),
// number pad and keyboard (NumberPadSheet / TextPadSheet / InputPad), passcode
// change (PasscodeSheet), pathology entry (OutcomeSheet), case details
// (CaseEditSheet). Texts are the 2.18.0 texts unless docs/DIFFERENCES.md says
// otherwise.
#ifndef BIRADX_GUI_H
#define BIRADX_GUI_H

#include "../app/app.h"
#include "gfx.h"
#include "ui.h"

#include <functional>
#include <map>
#include <string>

namespace gui {

class Gui {
public:
    explicit Gui(app::App& a);
    // True when the screen must be drawn again (input, new data, a timer).
    bool wants(const ui::Touch& t, uint32_t now) const;
    void frame(gfx::Canvas& c, const ui::Touch& t, uint32_t now);

    // for the simulator's scripted sessions
    enum Scr { Measure, Logs, Lock, Settings, Intake };
    Scr screen() const { return m_scr; }
    // The simulator and the ITDS tests: straight to a screen, a Settings tab or a sheet
    // ("settings 3", "logs", "details", "calib", "keyboard", "numpad", "clock", "passcode" …).
    void debugShow(const std::string& what);

private:
    // The frame's size: 1024 x 600 on the display board; on the ITDS computer the window's
    // shape, 1024 x 600 or, on a screen standing upright, 600 x 1024 (portrait()).
    int scrW = 1024, scrH = 600;
    bool portrait() const { return scrW < scrH; }
    // A button of a row laid out for the wide screen, narrowed in proportion when the whole row
    // (rowTotal, gaps included) does not fit across this one. On 1024 x 600 it is w.
    // The box of a sheet drawn for the wide screen at wide: upright (ITDS-2.16.0) as wide as the
    // screen less its margins, tallH high (or as high as before), in the middle of the screen.
    gfx::Rect sheetBox(const gfx::Rect& wide, int tallH = 0) const {
        if (!portrait()) return wide;
        const int h = std::min(scrH - 2 * ui::th::pad, tallH > 0 ? tallH : wide.h);
        return gfx::Rect(ui::th::pad, (scrH - h) / 2, scrW - 2 * ui::th::pad, h);
    }
    int fitW(int w, int rowTotal) const {
        const int room = scrW - 2 * ui::th::pad;
        return rowTotal <= room ? w : w * room / rowTotal;
    }
    app::App& a;
    ui::Ui m_ui{ gfx::Canvas(nullptr, 1024, 600) };
    ui::Ui* u = &m_ui;
    gfx::Canvas* c = nullptr;
    Scr  m_scr = Measure;
    bool m_again = true;
    uint32_t m_lastRev = 0, m_lastDraw = 0;
    ui::Touch m_lastTouch;
    uint32_t m_timerAt = 0;          // a redraw wanted at this time (0 = none)

    // ---- measure
    bool m_details = false;
    // DETAILS: of the last reading (from the main screen), or of one recorded
    // reading or healthy-tissue sample (from the log).
    bool m_detLog = false;
    std::string m_detTitle, m_detFlags;
    birx::Features m_detMain;
    bool m_detMainIsSample = false;
    std::vector<app::TissueSample> m_detRefs;
    int  m_detRef = -1;              // which healthy sample the HEALTHY column shows; -1 = the median of all
    void openLogDetails(const std::string& caseId, const app::LogReading* reading, const app::TissueSample* sample);
    bool m_calib = false;
    bool m_calDip = false;          // the calibration sheet shows the Saline check (H7-2.3.0)
    bool m_calRef = false;          // ... or the needle's saline reference for the material library (H7-2.6.1)
    bool m_calRefStarted = false;
    int  m_calRefPass0 = 0;           // H7-2.13.0: Training's passed checks when this one started
    bool m_calAutoClose = false;      // H7-2.13.0: opened from a material's card: back to it once the check passes
    uint32_t m_calDoneAt = 0;
    void refSheet();
    static std::string tempHint(double tC);   // H7-2.15.0: the saline's temperature not entered since power-on
    double m_dipCond = birx::kNaN;  // the check saline's conductivity at 25 °C, when a meter gives it
    bool m_tissue = false;           // the healthy-tissue sample sheet
    void measure();
    void measurePortrait(const gfx::Rect& body);
    void header(const gfx::Rect& r);
    void caseStrip(const gfx::Rect& r);
    void needleStrip(const gfx::Rect& r);
    void verdictCard(const gfx::Rect& r);
    void valueTiles(const gfx::Rect& r);
    void progressStrip(const gfx::Rect& r);
    int  actionRow(const gfx::Rect& r, bool measureOnly);   // returns height used
    void phaseChart(const gfx::Rect& r);
    void zipsMap(const gfx::Rect& r);
    void detailsBox(const gfx::Rect& r);
    void calibrateSheet();
    void dipSheet();
    void tissueSheet();
    void openIntakeForNewCase();
    void statusBanners(gfx::Rect* body);

    // ---- intake
    int  m_intakePage = 1;
    std::string m_intakeMsg;
    void intake();
    int  fieldEditor(int id, const gfx::Rect& r, const std::string& fieldId,
                     const std::string& value, const std::function<void(const std::string&)>& set);

    // ---- number pad and keyboard
    struct Pad {
        bool open = false;
        bool numeric = true;
        std::string title, value, unit, help, err;
        double lo = 0, hi = 0;
        bool   bounded = false;
        bool   allowNeg = false, allowDot = true;
        bool   shift = false, sym = false;
        int    maxLen = 64;
        std::function<void(const std::string&)> done;
    } m_pad;
    void openNumber(const std::string& title, const std::string& value, const std::string& unit,
                    double lo, double hi, bool bounded, bool allowNeg, bool allowDot,
                    std::function<void(const std::string&)> done);
    void openText(const std::string& title, const std::string& value, int maxLen,
                  std::function<void(const std::string&)> done, const std::string& help = "");
    void padSheet();

    // ---- lock
    int   m_dial[4] = { 0, 0, 0, 0 };
    int   m_dialDrag = -1;
    int   m_dialDragY0 = 0, m_dialDragV0 = 0;
    uint32_t m_lockSettle = 0;
    bool  m_lockOpened = false;
    uint32_t m_lockOpenAt = 0;
    void lock();
    void dials(const gfx::Rect& area, int* d, bool highlightOk, int idBase);

    // ---- settings
    int m_tab = 0;
    app::Settings m_edit;
    bool m_editLoaded = false;
    std::string m_msg;               // the result of the last action, shown on the tab
    bool m_msgBad = false;
    app::RulesConfig m_rules;
    bool m_rulesLoaded = false;
    int  m_ruleOpen = -1;            // row being edited (-1 none), -10.. fallbacks
    int  m_caseRuleOpen = -1;
    struct PassSheet { bool open = false; int step = 0; int d[3][4] = {}; std::string err; } m_pass;
    bool m_clockEdit = false;
    plat::Clock m_clockVal;
    void settings();
    void tabDevice(const gfx::Rect& r);
    void tabProtocol(const gfx::Rect& r);
    void tabNeedle(const gfx::Rect& r);
    void tabRules(const gfx::Rect& r);
    void tabSound(const gfx::Rect& r);
    void tabSecurity(const gfx::Rect& r);
    void tabAbout(const gfx::Rect& r);
    // Settings -> Lots (H7-2.0.0, gui_lotcal.cpp)
    void tabLots(const gfx::Rect& r);
    void lotSheet();
    birx::LotPlan m_lotPlan;
    int  m_lotSheet = -1;            // the lot whose test sheet is open
    bool m_lotShowResult = false;
    bool m_lotCompare = false;       // the Compare view (H7-2.2.0)
    int  m_lotCmpStep = 1;
    bool m_lotCmpDiff = false;       // H7-2.4.5: the Compare chart shows the differences from the reference
    void lotCompareView(int idx, const gfx::Rect& area);
    bool m_lotRaw = false;           // the Raw view (H7-2.2.1)
    int  m_lotRawPage = 0, m_lotRawRow = -1;
    void lotRawView(int idx, const gfx::Rect& area);
    int  m_lotSel = -1;              // needle*10 + step chosen by a tap; -1: the plan's next
    uint32_t m_lotDelArm = 0;
    int      m_lotRmArm = 0;         // H7-2.4.1: the sweep whose Remove was tapped once (index + 1)
    uint32_t m_lotRmAt = 0;
    double m_lotTempC = birx::kNaN, m_lotCond = birx::kNaN;
    std::string m_lotLocalMsg;
    // Settings -> Materials (H7-2.5.0, gui_materials.cpp)
    void tabMaterials(const gfx::Rect& r);
    std::string m_matSel;            // the label chosen
    uint32_t m_matDelLabArm = 0, m_matDelLastArm = 0;   // the first tap of a delete (a second within 5 s does it)
    uint32_t m_matDelTipArm = 0;     // H7-2.11.0: ... of Delete the sweeps of a faulty tip
    uint32_t m_matKeepUncArm = 0;    // H7-2.12.0: ... of Learn the unconfirmed sweeps anyway
    uint32_t m_matSkipArm = 0;       // H7-2.12.4: ... of Sweep without waiting (the tip's settling)
    uint32_t m_matKeepSusArm = 0;    // H7-2.12.5: ... of Learn the suspect sweeps anyway
    bool m_matChartZ = false;        // the chart shows the shape of |Z| instead of the phase
    // the main screen's box under the map: the material (H7-2.5.1); false = draw the phase chart
    bool materialBox(const gfx::Rect& r);
    uint32_t m_phaseForSeq = 0;      // the sweep for which the phase chart was asked for instead
    // H7-2.5.3: a reading of the logs tagged with what it was; the library to and from USB
    bool m_tagSheet = false;
    std::string m_tagCase, m_tagWhen, m_tagResult, m_tagMsg;
    int m_tagSite = 0;
    int64_t m_tagEpoch = 0;
    void tagSheet();
    bool m_trainSheet = false;
    // H7-2.7.0: Logs -> All files: to a USB stick, or by the programming cable
    bool m_allSheet = false;
    int  m_allPending = 0;           // the part to prepare on the next frame (after "Preparing…" is drawn)
    std::string m_allMsg;
    bool m_allMsgBad = false;
    void allFilesSheet();
    std::string m_trainMsg;
    bool m_trainMsgBad = false;
    void trainSheet();
    uint32_t m_lotLocalAt = 0;
    void passcodeSheet();
    void clockSheet();
    bool m_imSheet = false;          // "use the internal memory" (erases it: asks first)
    std::string m_imMsg;
    bool m_imMsgOk = true;
    void intMemSheet();
    bool m_monitor = false;          // the link monitor
    void monitorSheet();
    int  settingRow(const gfx::Rect& r, const std::string& label, const std::string& note,
                    int rightW);   // returns the row height; right-hand area is r.r()-rightW
    // where the row's control goes: right of the text, or (ITDS-2.16.0, upright and narrow) a band
    // across the bottom of the row, under the text
    gfx::Rect m_rowR;
    void say(const std::string& m, bool bad = false) { m_msg = m; m_msgBad = bad; }

    // ---- logs
    std::string m_when = "today", m_tag = "all";
    std::string m_expanded;
    std::string m_logMsg;
    bool m_outcome = false;
    bool m_outSingle = false;        // opened from one case's row
    std::string m_outCase, m_outPath, m_outMsg;
    bool m_outMsgOk = true;
    uint32_t m_outCloseAt = 0;       // a single-case save closes the sheet 1.4 s later
    std::vector<std::string> m_outPending;
    void openOutcome(const std::string& preselect);
    bool m_caseEdit = false;
    std::string m_editCase, m_editMsg;
    std::map<std::string, std::string> m_editVals;
    int m_editPage = 1;
    bool m_logsShown = false;
    void logs();
    void outcomeSheet();
    // Logs -> Delete logs (H7-1.9.0)
    void deleteLogsSheet();
    bool m_delAsk = false;
    uint32_t m_delArmedAt = 0;      // first press of Delete: a second within 5 s deletes
    std::string m_delMsg;           // what Copy to USB answered, shown on the sheet
    int  m_delFiles = 0;
    long m_delBytes = 0;
    void caseEditSheet();

    // ---- diagnostic mode
    std::string m_lastWhere, m_lastLogMsg;
    std::string where() const;       // "logs + pathology", for the diagnostic file
    void diagPill();

    // ---- helpers
    void toast();
    // H7-2.12.3: a line that fills round a box as the sweep (or the settling) there goes on
    void progressRing(const gfx::Rect& r, double f, gfx::Color col, int radius);
    void sheetRing();                       // round a sheet (the screen) whose sweep is under way
    bool checkRing(double* f, gfx::Color* col) const;   // the saline check's: its settling, then its two sweeps
    bool matRing(double* f, gfx::Color* col) const;     // a material sweep's: its settling, then its sweep
    void needleStep(int step);              // H7-2.13.0: App::NeedleStep, done from where the sweep is asked for
    std::string ohmShort(double ohm) const;
};

} // namespace gui

#endif
