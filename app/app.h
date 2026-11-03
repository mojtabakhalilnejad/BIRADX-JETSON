// app/app.h — the instrument's behaviour, without a screen.
//
// This is the MCU counterpart of two classes of BIRADx 2.18.0:
//   BirxBridge  (src/BirxBridge.cpp)  — the reading path, the case, the needle
//                                       calibration, the records, the log
//   Controller  (src/controller.cpp)  — the link to the measurement unit, the
//                                       blockers, the settings, the passcode,
//                                       diagnostics, export
// Function names follow the originals so the two can be read side by side.
// Everything is plain C++17 over plat:: (platform.h), so the same code runs on
// the STM32H743 and in the PC simulator that produced the screenshots.
#ifndef BIRADX_APP_H
#define BIRADX_APP_H

#include "config.h"
#include "platform.h"
#include "../birxcore/types.h"
#include "../birxcore/parser.h"
#include "../birxcore/decision.h"
#include "../birxcore/opencal.h"
#include "../birxcore/lotfit.h"
#include "../birxcore/patient.h"
#include "../birxcore/dataset.h"
#include "../birxcore/needlecode.h"
#include "../birxcore/needlemodel.h"
#include "../birxcore/matlib.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

// One row of the log list: one case.
struct LogCase {
    std::string caseId;
    int64_t     epochMs = 0;          // newest reading
    std::string whenText;             // "today 14:05"
    int         readings = 0;         // valid readings (as the aggregator counts them)
    int         total = 0;            // every reading recorded
    int         target = 0;
    int         nHigh = 0;
    std::string detail;               // "54 y · 4A · 14 mm"
    std::string tier, label, color;   // the case answer, from Aggregator::combine
    std::string summary;
    bool        complete = false;
    bool        stoppedEarly = false;
    std::string pathology;            // "", "malignant", "benign", "borderline"
    bool        edited = false;
    bool        test = false;         // a TEST- id: readings taken with no case
    int         caseRule = 0;         // 0 = the median decided
};

// One healthy-tissue sample of a case (app_tissue.cpp), live or from the record.
struct TissueSample {
    int            n = 0;             // 1, 2, 3 … in the order taken
    int64_t        epochMs = 0;
    std::string    when;              // "14:05:31"
    std::string    flags;
    birx::Features features;
    std::vector<birx::Point> points;  // its sweep, for the phase chart (the open case only)
};

// One reading inside an expanded log row.
struct LogReading {
    int64_t     epochMs = 0;
    int         site = 0;
    std::string when;                 // "14:05:31"
    bool        usable = false;
    std::string tier, label, color, flags;
    double      z1k = birx::kNaN, ipsLegacy = birx::kNaN, ipsFit = birx::kNaN;
};

// H7-2.9.0: a case of the logs as tagged (Training): its label, how many of its readings carry it, and
// whether some carry another
struct CaseTag {
    std::string label;
    int  tagged = 0;
    bool mixed = false;
};

enum class CalStage { Idle, Waiting, Sweeping, Done };
// H7-2.6.0: Settling — the tip in the saline for Settings -> Needle's settling time before the sweep
enum class LotStage { Idle, Settling, Waiting, Sweeping };
enum class RefStage { Idle, Waiting, Sweeping, Done };   // the healthy-tissue sample (app_tissue.cpp)
// the Saline check (H7-2.3.0, app_dip.cpp): settling, the first sweep, a pause, the second, done
enum class DipStage { Idle, Settling, Waiting, Sweeping, Between, Done };
// a sweep of Settings -> Training (H7-2.5.0, app_mat.cpp)
enum class MatStage { Idle, Settling, Waiting, Sweeping };

class App {
public:
    App();
    ~App();

    void begin();                     // settings, records, engine; call once
    void poll();                      // link bytes and timers; call often
    // Bumped whenever anything a screen shows has changed.
    uint32_t revision() const { return m_rev; }
    void touch() { ++m_rev; }

    // ---- clock -----------------------------------------------------------
    int64_t     nowMs() const;                 // UTC milliseconds since 1970
    plat::Clock nowLocal() const;
    bool        clockOk() const { return m_clockOk; }
    std::string clockProblem() const;          // empty when plausible
    bool        setClock(const plat::Clock& c, std::string* why);
    std::string timerCheck() const;            // H7-2.7.2: the firmware's timer against the battery clock, in words
    double      timerRatio() const { return m_tcRatio; }   // its ticks per clock millisecond (0 not yet)
    std::string isoUtc(int64_t ms) const;      // 2026-09-24T10:02:11Z
    std::string localText(int64_t ms, const char* fmt) const;   // strftime-like: %Y %m %d %H %M %S

    // ---- the link to the measurement unit ----------------------------------
    bool connected() const;                    // bytes arrived in the last 5 s
    bool running() const { return m_running; }
    bool asking() const { return m_asks > 0; }
    // Anything in flight on the measurement unit, including a stopped sweep
    // it is still finishing: no command may be sent until it is over.
    bool busy() const { return m_running || m_inSweep || m_discard || m_asks > 0 || m_syncSt != SyncSt::Idle || m_waitBeat != 0 || m_unitBackBy != 0 || plat::unitRestarting() || !m_delQueue.empty(); }
    int  unitRestarts() const { return m_unitRestarts; }
    int  framesSent() const { return m_txOk; }
    int  framesFailed() const { return m_txFail; }
    bool finishingStopped() const { return m_discard; }
    bool sweeping() const { return m_inSweep; }
    // H7-2.12.3: how far the sweep under way has come, 0..1: its points arrive one by one (15 a sweep); between
    // two, by the mean time a point has taken so far
    double sweepProgress() const;
    int  dipSweepsDone() const { return int(m_dipSweeps.size()); }   // of the saline check's two
    bool readingSweep() const { return m_inSweep && !m_calibrating && !m_lotTesting && !m_matTesting && !m_dipping && !m_refTaking; }
    const birx::Spectrum& current() const { return m_current; }
    void startRun();                           // Sweep / pedal-equivalent: send [R]
    void stopRun();                            // abandon the sweep in progress
    void sendRaw(const char* frame);           // the link monitor: one frame, nothing else
    int  askingSeconds() const;                // since the [R] that is being waited for
    uint32_t nowTicks() const { return plat::ticks(); }
    std::string linkMessage() const { return m_linkMessage; }
    int  heartbeats() const { return m_heartbeats; }
    int  unparsedCount() const { return m_unparsedTotal; }
    const std::deque<std::string>& unparsedLines() const { return m_unparsed; }
    const std::deque<std::string>& traffic() const { return m_traffic; }
    void startProbe();                         // [T]: the one command that replies
    bool probing() const { return m_syncSt != SyncSt::Idle && m_syncWhy == SyncWhy::Probe; }
    bool checkingChannel() const { return m_syncSt != SyncSt::Idle; }
    // "", "hears", "resynced", "deaf", "silent": what the last check of the command channel found
    std::string commandChannel() const { return m_cmdChannel; }
    std::string probeResult() const { return m_probeResult; }
    uint32_t lastByteAgeMs() const;

    // ---- the case ----------------------------------------------------------
    bool caseOpen() const { return !m_patient.caseId.empty(); }
    const std::string& caseId() const { return m_patient.caseId; }
    const std::string& measureCaseId() const { return m_caseId; }  // may be TEST-…
    bool caseCommitted() const { return m_committed; }
    std::string beginPatient(std::string* why);   // mint an id, open the intake
    void abandonPatient();                        // drop an uncommitted case (not used by the screens: Cancel keeps it, as on the Jetson)
    void setIntakeValue(const std::string& id, const std::string& v);
    std::string intakeValue(const std::string& id) const { return m_patient.get(id); }
    bool intakeComplete() const { return caseOpen() && m_patient.complete(); }
    std::vector<std::string> intakeMissing() const;
    bool commitPatient(std::string* why);
    std::string caseSummary() const;              // "54 y · BI-RADS 4A · L 10 o'clock · 14 mm"
    void endCase();
    std::string needleStatus() const;
    bool needleAccepted() const;
    std::string rememberedLot() const { return m_rememberedLot; }
    bool needleKeyInstalled() const { return !m_needleKeys.empty(); }

    const birx::Verdict&  last() const { return m_last; }
    bool haveLast() const { return m_haveLast; }
    const birx::CaseResult& caseResult() const { return m_caseResult; }
    const std::vector<birx::Verdict>& caseReadings() const { return m_caseReadings; }
    std::string caseLabel() const;
    std::string ipsText() const { return m_ipsText; }
    std::string z1kText() const { return m_z1kText; }
    double      z1kOhm() const { return m_z1kOhm; }
    static std::string ohmText(double ohm);

    // ---- needle calibration ----------------------------------------------------
    void calOpen(bool training = false);          // the sheet opens (H7-2.9.0: Training's, for its needle)
    void calRun();                                // Start — 6 s
    void calStop();                               // Stop / Cancel / sheet closed
    CalStage calStage() const { return m_calStage; }
    std::string calError() const { return m_calError; }
    int  calSecondsLeft() const;
    std::string calBlocker() const;
    bool needleReady() const { return m_cal.rangeCount() > 0; }
    bool calTraining() const { return m_calTraining; }
    const std::string& calNeedle() const { return m_cal.needleId; }   // the needle of the Air check in force
    // H7-2.6.0: what the Air check in force says about the needle beyond its limits ("" nothing)
    std::string calWarning() const { return m_calWarning; }
    // H7-2.1.0 (Settings -> Rules): whether the rule ranges were found with
    // air-calibrated needles. Off: readings are taken as they come, as in the
    // trial, and the air calibration is neither asked for nor applied (it is
    // still used, when taken, to bring a lot in the lot file to the reference).
    bool airCalUsed() const { return m_s.rulesAirCal; }
    std::string setRulesAirCal(bool on);

    // ---- the healthy-tissue sample of this patient (app_tissue.cpp) -----------
    void refOpen();                               // the sheet opens
    void refRun();                                // Start
    void refStop();                               // Stop / Cancel / sheet closed
    RefStage refStage() const { return m_refStage; }
    std::string refError() const { return m_refError; }
    int  refSecondsLeft() const;
    std::string refBlocker() const;
    // Several may be taken for one case: each is kept, none replaces another.
    bool haveTissueRef() const { return !m_refs.empty(); }
    int  tissueRefCount() const { return int(m_refs.size()); }
    const std::vector<TissueSample>& tissueRefs() const { return m_refs; }   // of the open case
    bool chartShowsTissueRef() const { return m_chartTissue; }   // the sweep on the chart is such a sample
    void refRemoveLast();                         // a sample that went wrong: taken out, and marked so in the record
    std::string tissueRefText() const;            // one: "Z 2.84 kΩ · IPS −3.10 · 10:32:05"; several: "3 samples · median Z … · IPS …"
    // From the record, for the log: every sample of a case that was not removed.
    std::vector<TissueSample> tissueSamplesOf(const std::string& caseId) const;
    // The 22 quantities of one recorded reading (dataset.csv), by case and time.
    bool readingFeatures(const std::string& caseId, int64_t epochMs, birx::Features* f, std::string* flags) const;
    // Each quantity's median over the samples; one sample is itself.
    static birx::Features medianOf(const std::vector<TissueSample>& v);
    static std::string sampleText(const birx::Features& f, bool legacyIps);   // "Z 2.84 kΩ · IPS −3.10"
    bool ipsIsLegacy() const { return m_engine.rulesDeciding(); }
    std::string needleCalibration() const;
    int  calibratedRanges() const { return int(m_cal.rangeCount()); }
    std::string calibrationReport() const;

    // ---- what blocks what ------------------------------------------------------
    std::string measureBlocker() const;
    // H7-2.10.0: what the needle's QC still lacks ("" passed, or not asked for): a sweep then is no reading at
    // all — not decided, shown or recorded as one — whether the screen or the pedal asked for it
    std::string qcBlocker() const;
    std::string caseBlocker() const;
    bool canMeasure() const { return measureBlocker().empty(); }
    bool canStartCase() const { return caseBlocker().empty(); }
    std::string actionReason() const;             // the sentence above the buttons

    // ---- settings ----------------------------------------------------------------
    const Settings& settings() const { return m_s; }
    bool saveSettings(const Settings& s, std::string* why);    // persist + apply
    std::string setDeviceId(const std::string& text);          // "" on success
    bool checkPassword(const std::string& code) const;
    std::string changePassword(const std::string& cur, const std::string& next,
                               const std::string& confirm);
    std::string saveRules(const RulesConfig& r);               // "" on success
    birx::RuleHit previewVerdict(double z1k, double ips) const;
    const birx::RuleTable& ruleTable() const { return m_engine.ruleTable(); }
    std::string ruleSummary() const;
    std::string settingsProblem() const { return m_settingsProblem; }
    std::string rangeCalSummary() const;
    int configBytes() const { return int(m_store.lastBytes()); }

    // ---- sound (the DY voice module) -----------------------------------------------
    bool muted() const { return m_s.muted; }
    void setMuted(bool m);
    void setVolume(int v);
    void setSweepTrack(int track);          // 0 = silent
    static const std::vector<std::string>& soundLibrary();   // track labels, 1-based
    static int trackOf(const std::string& label);            // 0 = not a known sound
    void playSound(const std::string& label);
    void testTrack(int track);
    void sweepSoundStart();                 // the recording that repeats while a sweep runs
    void sweepSoundStop();

    // ---- records and the log ----------------------------------------------------------
    std::string recordsDir() const { return m_records; }
    // Erases the internal memory and starts keeping the records on it. Returns
    // what happened, in words. Nothing calls this without the operator's consent.
    std::string prepareInternalMemory();
    std::string storageProblem() const { return m_storageProblem; }
    void clearStorageProblem() { m_storageProblem.clear(); touch(); }
    int  logCount() const;                      // cases with at least one recorded reading
    void refreshLog();                          // re-read the files if they changed
    std::vector<LogCase> caseLog(const std::string& when, const std::string& tag,
                                 int* hiddenByWindow = nullptr) const;
    std::vector<LogReading> caseReadingsLog(const std::string& caseId) const;
    std::map<std::string, std::string> caseDetails(const std::string& caseId) const;
    std::string updateCaseDetails(const std::string& caseId,
                                  const std::map<std::string, std::string>& values);
    std::string recordOutcome(const std::string& caseId, const std::string& pathology,
                              const std::string& reportDate, const std::string& source,
                              const std::string& note);
    std::string outcomeOf(const std::string& caseId) const;   // "" when none is recorded
    std::vector<std::string> casesAwaitingPathology() const;  // oldest first, as 2.18.0
    int  readingCount() const { return int(m_logRows.size()); }
    int  pendingOutcomes() const;
    std::string exportLogs(const std::string& when, const std::string& tag);
    // DELETING THE LOGS (H7-1.9.0): every case, reading, sweep, healthy-tissue
    // sample, pathology result and the health and diagnostic files. The case
    // numbering, the needle calibrations and the settings are kept. Started
    // here, carried out a few files at a time from poll().
    void logsFootprint(int* files, long* bytes) const;
    std::string deleteLogsBlocker() const;      // "" when it may be done now
    std::string deleteLogsStart();              // "" when started; otherwise why not
    bool deletingLogs() const { return !m_delQueue.empty(); }
    int  deleteDone() const { return m_delTotal - int(m_delQueue.size()); }
    int  deleteTotal() const { return m_delTotal; }
    std::string takeDeleteResult() { std::string r = m_delResult; m_delResult.clear(); return r; }

    // NEEDLE LOTS (H7-2.0.0, app_lots.cpp): the reference needle and each new
    // lot measured in air and saline; the verdict; the lot calibration file.
    const std::vector<birx::LotRun>& lotRuns() const { return m_lotRuns; }
    int  lotReferenceIdx() const;
    // H7-2.4.0: control — a lot of the reference's own needles, measured again to see the spread of a lot
    std::string lotAdd(const std::string& id, bool reference, const birx::LotPlan& plan, bool control = false);
    std::string lotRemove(int idx);
    // H7-2.4.1: one sweep of a run taken out of the evaluation (and put back); the record keeps it
    std::string lotSetRemoved(int idx, int test, bool removed);
    std::string lotTestBlocker() const;
    std::string lotStartTest(int idx, int needle, int step, double tempC, double condMsCm);
    void lotStopTest(const std::string& why = std::string());
    bool lotTesting() const { return m_lotTesting; }
    LotStage lotStage() const { return m_lotStage; }
    int  lotTestIdx() const { return m_lotIdx; }
    int  lotSecondsLeft() const;
    const std::string& lotMessage() const { return m_lotMsg; }
    bool lotMessageOk() const { return m_lotMsgOk; }
    void clearLotMessage() { m_lotMsg.clear(); }
    birx::LotResult lotResult(int idx) const;
    // the comparison with the reference, for the Compare view (cached like the result)
    birx::LotCompare lotCompare(int idx) const;
    std::vector<birx::LotRange> lotEquivalentRanges(int idx) const;
    std::string lotApprove(int idx);
    // H7-2.8.0: the noise floor of the measurement, from a control lot (Settings -> Lots): the verdict's
    // limits are never tighter than what the reference's own needles spread by
    birx::RangeModel rangeModel() const;
    birx::LotLimits lotLimits() const;
    std::string lotUseAsFloor(int idx);
    std::string lotClearFloor();
    // H7-2.6.0: the seconds the tip settles in a solution before its sweep (lot tests, saline checks, Training)
    std::string setSettleSeconds(int sec);
    const birx::LotFile& lotFile() const { return m_lotFile; }
    bool lotFileLoaded() const { return m_lotFileLoaded; }
    std::string lotExport();
    std::string lotImport();
    std::string currentLotId() const;
    std::string lotStatus() const;            // "lot L corrected" / "lot L not in the lot file" / ""
    std::vector<std::string> lotChoices() const;              // the lots of the file, for the intake (H7-2.4.0)
    std::string lotPlanText(const std::string& lot) const;    // what the file says of a lot, in words
    bool lotCovered() const;
    void correctForNeedle(birx::Spectrum* s) const;
    std::string exportAll();

    // ---- the Saline check of this needle (H7-2.3.0, app_dip.cpp) -----------------------------
    // After its Air check, the needle's tip 10 s in the check saline (Settings -> Needle: 0.9 % or
    // 0.09 %): two sweeps. Its tip's model then carries its readings to the reference needle's, for
    // a lot approved per needle, and (H7-2.10.0) it is the needle's QC against its lot's limits.
    bool dipNeeded() const;                   // the needle's lot needs it (or every needle: Settings -> Needle)
    bool dipNeededFor(const std::string& lot) const;
    bool dipCorrects(const std::string& lot) const;   // the check also corrects its readings (else a QC only)
    bool dipValidated() const;                // the lot was validated with this check solution
    int  dipStep() const;                     // birx::kCheck9 / kCheck09
    std::string dipSolutionName() const;      // "0.9 %" / "0.09 %"
    void dipOpen();
    void dipRun(double tempC, double condMsCm = birx::kNaN);
    void dipStop();
    DipStage dipStage() const { return m_dipStage; }
    bool dipping() const { return m_dipping; }
    bool dipDone() const { return m_dip.ok; }
    bool dipTraining() const { return m_dipTraining; }
    double dipKZ() const { return m_dip.kZ; }
    int  dipSecondsLeft() const;
    std::string dipBlocker() const;
    std::string dipError() const { return m_dipError; }
    std::string dipText() const;              // what the check found, in words ("" none)

    // ---- Settings -> Training: the material library (H7-2.5.0, app_mat.cpp) ----------------------
    const birx::MatLibrary& matLibrary() const { return m_matLib; }
    const birx::MatModel& matModel() const;   // learnt from the library (kept until it changes)
    std::string matStart(const std::string& label, bool newSample = false);   // "" = identify only
    void matStop(const std::string& why = std::string());
    bool matTesting() const { return m_matTesting; }
    MatStage matStage() const { return m_matStage; }
    int  matSecondsLeft() const;
    std::string matBlocker() const;
    const std::string& matRunLabel() const { return m_matLabel; }
    const std::string& matLastLabel() const { return m_matLastLabel; }
    const std::string& matMessage() const { return m_matMsg; }
    bool matMessageOk() const { return m_matMsgOk; }
    bool matHasMatch() const { return m_matMatch.ok; }
    const birx::MatMatch& matLastMatch() const { return m_matMatch; }
    const std::vector<birx::Point>& matLastSweep() const { return m_matLastPts; }
    // H7-2.5.1: the sweep just identified can be told what it was, and joins the library
    bool matCanKeepLast() const { return m_matPending; }
    void matDiscardLast() { m_matPending = false; touch(); }
    std::string matKeepLast(const std::string& label);
    std::string matAddLabel(const std::string& label);
    std::string matDeleteLabel(const std::string& label);
    std::string matDeleteLast();
    // H7-2.11.0: the library's sweeps of a shorted or bridged tip (birx::matTipFault): not learnt, and deleted on request
    int matTipFaults() const { return matModel().nTipFault; }
    std::string matDeleteTipFaults();
    std::string matExport();                  // to the USB stick
    std::string matImport();                  // from it: ADDED to this library
    int  matTaggedReadings() const;           // sweeps of the library that came from a case's reading
    int  matFromOtherDevices() const;
    bool matCpKnown() const { return m_cal.calibrated(3); }
    // H7-2.7.0: Logs -> All files by the programming cable: the records as a ZIP archive in memory
    std::string dumpPrepare(int part);        // "" when part `part` (1 = the first) is in memory
    void dumpFree();
    bool dumpReady() const { return m_dumpBuf != nullptr; }
    size_t dumpLength() const { return m_dumpLen; }
    int  dumpPart() const { return m_dumpPart; }
    int  dumpParts() const { return m_dumpParts; }
    int  dumpFiles() const { return m_dumpFiles; }
    const std::vector<std::string>& dumpSkipped() const { return m_dumpSkipped; }
    const uint8_t* dumpData() const { return m_dumpBuf; }
    std::string dumpCommand() const;
    // H7-2.6.0: this needle's saline reference (one sweep in 0.9 % saline after its Air check): with
    // it, sweeps are also compared NEEDLE-NORMALISED (its tip's surface off, |Z| as a ratio to the
    // saline's). Valid until the next Air check or End case. The Saline check before a case serves too.
    // H7-2.10.0: the Training needle's saline check (two sweeps in 0.9 % saline, at its temperature): its QC
    // against its lot's limits, its reference for the needle-normalised comparison, and the check that
    // closes the sweeps taken since the one before (bracketing)
    std::string matStartRef(double tempC = 25.0, double condMsCm = birx::kNaN);
    std::string matRefBlocker() const;        // "" when the saline check can be taken
    // the QC state of the Training needle
    bool qcAirOk() const;                     // its own Air check, not older than qcAirMinutes
    bool qcCheckOk() const;                   // a saline check passed since it
    int  qcSweepsSince() const { return m_qcSweeps; }
    int  qcMinutesSince() const;              // since the last saline check (-1 none)
    int  qcAirMinutes() const;                // since the Air check (-1 none)
    int  qcPending() const { return int(qcOpen().size()); }   // this needle's sweeps its next check closes
    // H7-2.12.0: a new tip settles first: two saline checks, 2 minutes apart at least, its surface within 20 %
    bool qcSettled() const { return m_qc.ok && m_qc.settled; }
    std::string qcSettleText(bool countdown = true) const;   // why it is not settled yet ("" settled, or no check)
    int  qcWaiting() const;                   // pending sweeps a later check of their own needle will close
    int  qcUnconfirmed() const;               // pending sweeps no check of their needle on record can close
    std::string matKeepUnconfirmed();         // learn those anyway (by hand)
    // H7-2.12.4: the way out of the settling: the needle sweeps now; its next check still closes those sweeps
    std::string qcSkipSettling();
    // H7-2.13.0: what to do first so that a sweep can be taken: the button that does it is offered where the sweep is
    enum class NeedleStep { None, Number, Air, Saline };
    NeedleStep matNextStep() const;
    int qcPasses() const { return m_qcPasses; }   // H7-2.13.0: Training's saline checks passed (a sheet's own one is told by it)
    // H7-2.12.5: the sweeps marked suspect, learnt anyway (by hand)
    std::string matKeepSuspect();
    // H7-2.15.0: the suspect sweeps judged again by today's rules at start (how many were, the last time)
    int qcRejudged() const { return m_qcRejudged; }
    // H7-2.15.0: a needle's leakage against its own when sound (its lowest Air check on record): "" within
    std::string needleLeakWhy(const std::string& needle, double gpS) const;
    double needleLeakBase(const std::string& needle) const;   // its lowest leakage on record, S (NaN: none)
    // H7-2.15.0: the saline's temperature, kept; and whether it was entered since the device was switched on
    std::string setSalineTemp(double c);
    bool salineTempEntered() const { return m_salineTempSet; }
    // H7-2.12.0: a material left out of the comparison (kept, not learnt, not answered)
    bool matIsOff(const std::string& label) const { return m_matLib.isOff(label); }
    std::string matSetOff(const std::string& label, bool off);
    std::string setTrainLot(const std::string& lot);
    std::string setQcIntervals(int sweeps, int minutes, int airMinutes);
    // the QC limits of a needle of `lot` for a check in the saline of `step` (H7-2.10.0)
    birx::QcLimits qcLimitsFor(const std::string& lot, int step) const;
    std::vector<std::string> lotIds() const;  // the lots of the lot file
    bool matHasRef() const;
    bool matRefRunning() const { return (m_matTesting && m_matRefSweep) || (m_dipping && m_dipTraining); }
    std::string matRefText() const;            // what the reference is, in words ("" none)
    // H7-2.6.2: the sweep of the last saline check that was refused, as it came
    const std::vector<birx::Point>& matRefAttempt() const { return m_matRefPts; }
    birx::MatNeedle currentNeedle() const;     // Cp, Gp and the reference of the needle on now
    const birx::MatModel& matModelN() const;   // learnt from the needle-normalised sweeps
    // the best answer for a sweep: needle-normalised when the needle has a reference and that model is learnt
    birx::MatMatch matClassify(const std::vector<birx::Point>& pts, const birx::MatNeedle& n) const;
    // the main screen: the last sweep (any sweep) compared with the library, when it can be
    const birx::MatMatch& matForCurrent() const;
    std::string matMainWhy() const;            // H7-2.12.1: why it shows no material ("" it does, or is not asked to)
    bool matOnMain() const { return m_s.matOnMain; }
    std::string setMatOnMain(bool on);
    uint32_t sweepSeq() const { return m_sweepSeq; }   // bumped by every sweep that ends
    // H7-2.9.0: Settings -> Training. The needle's number, asked for before any sweep there.
    const std::string& trainNeedle() const { return m_s.trainNeedle; }
    std::string setTrainNeedle(const std::string& id);
    bool trainCalMatches() const;              // the Air check in force was taken with the Training needle
    birx::MatNeedle trainingNeedle() const;
    // H7-2.5.3: the readings of the cases, tagged afterwards with what they were (Logs), join the library
    CaseTag caseTag(const std::string& caseId) const;
    std::string readingTag(const std::string& caseId, int site, int64_t epochMs) const;
    std::string tagReading(const std::string& caseId, int site, int64_t epochMs, const std::string& label);
    std::string tagCase(const std::string& caseId, const std::string& label);

    // ---- diagnostic mode (2.19.0, Controller::startDiagnosticMode & co) -------------------
    // Started from Logs -> Diagnostics: a snapshot is written to a new file and
    // from then on every action (each tap, and whether the control was live;
    // each screen; each command and each line back, heartbeats counted; each
    // sweep, verdict, refusal, setting written, calibration attempt, intake
    // field, export, pathology entry) is appended to it with its result, until
    // Stop diagnostics. It survives a restart. The passcode, the passcode
    // dials, the on-screen keypads, the national ID and notes are never written.
    bool diagnosticMode() const { return m_diagOn; }
    std::string diagnosticFile() const { return m_diagPath; }
    std::string startDiagnosticMode();
    std::string stopDiagnosticMode();
    void uiAction(const std::string& what, const std::string& result = std::string());
    void diagWrite(const std::string& area, const std::string& text);

    // ---- the health log (2.20.0), always on: records/health.csv every 10 s ------------------
    std::string lastSessionEnd() const { return m_lastSessionEnd; }
    void shutdownClean();                       // the power key: the session ended properly
    std::string healthTail(int lines) const;

    // ---- events (the diagnostics ring, no heartbeats) ------------------------------------
    void event(const std::string& text);
    const std::deque<std::string>& events() const { return m_events; }
    std::string toast() const { return m_toast; }     // a one-line notice for the screen
    uint32_t toastAge() const;
    void setToast(const std::string& t);

private:
    // link
    void onLine(const std::string& line);
    void onSweepEnd();
    void onRunStarted();
    void askForSweep();
    void sendFrame(const char* frame, const std::string& why);
    void noteTraffic(const std::string& s);
    void discardEnded(bool sawEnd);
    // The command channel: does the measurement unit hear this board, and is
    // its three-byte command framing in step? (see beginSync in app.cpp)
    enum class SyncSt { Idle, WaitTdp, PadGap };
    enum class SyncWhy { Silent, Run, Probe };
    void beginSync(SyncWhy why);
    bool unitRestart(const std::string& why, bool reask);
    void pollDelete();
    // ---- the Saline check (app_dip.cpp)
    struct DipResult {
        bool ok = false;
        birx::ElectrodeModel e;          // e.rOhm at 25 °C
        double rRaw = birx::kNaN;        // H7-2.10.0: R at the saline's own temperature
        bool   qcOnly = false;           // H7-2.10.0: a QC of the needle only; the reading's correction does not use it
        bool   training = false;         // H7-2.10.0: Training's (against its lot's limits, not a case's)
        double kZ = birx::kNaN;
        double cpF = birx::kNaN;
        int    step = 0;                 // the solution: birx::kCheck9 / kCheck09
        bool   validated = false;        // with the lot's window (else the generic one)
        std::string lotId, when;
    };
    double    m_dipCond = birx::kNaN;
    void pollDip(uint32_t now);
    void dipRunCore(double tempC, double condMsCm);
    void dipSweepStarted();
    void finishDipSweep();
    void clearDip(const char* why);       // the check no longer holds (nullptr: say nothing)
    // H7-2.12.0: a saline check with the tip out of the saline (open) or shorted is no check
    void dipNotACheck(const std::string& why);
    void dipRecord(const std::string& lot, bool validated, double cp, double r25, double ze, double alpha, double rms,
                   double drift, double kZ, bool accepted, const std::string& why);
    // H7-2.12.0: the main needle's tip surface, at its checks and its readings (records/needle_tip.csv)
    int64_t m_needleWetMs = 0;            // its first sweep in the saline
    int64_t m_dipAtMs = 0;                // its saline check passed
    void recordTip(const char* what, bool accepted, double r25, double ze, double alpha);
    // materials
    void loadMaterials();
    // appendLast (H7-2.7.2): a sweep was just added at the end, and only it is written (at the
    // end of the file) — the rest of the file is written again only when something else changed
    bool saveMaterials(bool appendLast = false);
    void matRewriteIfStale();
    std::vector<char> m_matFileAside;     // the marks the file holds, per sweep (H7-2.7.2)
    bool m_matFileStale = false;
    std::string matBlockerBase() const;
    void dipStuck();
    void pollMat(uint32_t now);
    void matSweepStarted();
    void finishMatSweep();
    void recordRawReading();              // H7-2.5.3: a reading's sweep as measured (records/reading_raw.txt)
    std::map<std::string, birx::MatSweep> rawSweepsOf(const std::string& caseId) const;
    bool keptSweepOf(const std::string& caseId, int site, int64_t epochMs, birx::MatSweep* w) const;
    void recordRefAttempt(const std::vector<birx::Point>& pts, const birx::MatNeedle& n, bool ok, const std::string& why);
    birx::MatLibrary m_matLib;
    uint32_t m_matRev = 1;                // bumped whenever the library changes
    mutable uint32_t m_matModelRev = 0;
    mutable birx::MatModel m_matModel;
    bool m_matTesting = false;
    MatStage m_matStage = MatStage::Idle;
    std::string m_matLabel;               // the sweep under way: its label ("" identify)
    std::string m_matLastLabel;
    std::string m_matMsg;
    bool m_matMsgOk = true;
    bool m_matNewSample = true;           // the sweep under way starts a new sample of its label
    uint32_t m_matWaitStart = 0;
    uint32_t m_matSettleUntil = 0;
    uint32_t m_matLastAt = 0;             // ticks of the last labelled sweep
    int64_t m_matLastMs = 0;
    birx::MatMatch m_matMatch;            // the last sweep's answer
    std::vector<birx::Point> m_matLastPts;
    bool m_matPending = false;            // the last sweep was identified and can still be kept
    double m_matLastCp = birx::kNaN;
    birx::MatNeedle m_matLastNeedle;
    // the needle's saline reference (H7-2.6.0)
    birx::MatNeedle m_matRef;
    std::string m_matRefCalAt;            // the Air check it belongs to (its takenAt)
    std::string m_matRefWhen;
    std::string m_matRefNeedle;           // H7-2.9.0: the Training needle's number
    bool m_matRefSweep = false;           // the sweep under way is the reference
    bool m_dipTraining = false;           // the saline check under way is Training's
    bool m_calTraining = false;           // the Air check sheet is Training's
    bool m_calByTraining = false;         // the Air check in force was taken in Training
    int64_t m_calAtMs = 0;                // when the Air check in force was taken (0 none)
    // H7-2.10.0: the Training needle's QC
    struct QcCheck {
        bool ok = false;
        bool settled = false;             // H7-2.12.0: it agreed with the needle's check before (the tip has settled)
        double moved = birx::kNaN;        // H7-2.12.0: how far its tip surface moved from that one
        double movedR = birx::kNaN;       // H7-2.12.6: and its R
        int64_t settleFromMs = 0;         // H7-2.12.4: the first of the checks that agree (the 2 minutes count from it)
        int64_t atMs = 0;
        std::string calAt, needle;
        double r25 = birx::kNaN, ze = birx::kNaN;
        double refR = birx::kNaN, refA = birx::kNaN, refAlpha = birx::kNaN;   // at the saline's temperature
        bool repeat = false;              // a later check was unsettled: no sweep until one passes
    } m_qc;
    int m_qcSweeps = 0;                   // sweeps since the last saline check
    // H7-2.12.0: each Training needle's last passed saline check, on record (records/training_qc.txt): the
    // next check of that needle closes its sweeps since, also after another needle or a restart
    struct QcLast {
        int64_t atMs = 0;
        double r25 = birx::kNaN, ze = birx::kNaN;
        double refR = birx::kNaN, refA = birx::kNaN, refAlpha = birx::kNaN;
        bool settled = false;
        int64_t anchorMs = 0;             // H7-2.12.4: the first of the run of checks that agree with each other
    };
    std::map<std::string, QcLast> m_qcLast;
    int m_qcPasses = 0;
    std::map<std::string, double> m_leakBase;   // H7-2.15.0: each needle's lowest leakage on record (needle_cal.csv)
    void loadLeakBase();
    void noteNeedleLeak(const std::string& needle, double gpS);   // an accepted Air check: its lowest on record
    int  qcRejudge();                     // H7-2.15.0: the suspect sweeps judged again by today's rules
    int  m_qcRejudged = 0;
    bool m_salineTempSet = false;
    void loadTrainQc();
    void saveTrainQc();
    std::vector<size_t> qcOpen() const;   // the library sweeps of the Training needle its next check closes
    bool qcClosable(const birx::MatSweep& s) const;
    int qcMarkOpen(const char* qc, const char* move = nullptr);   // marks them (returns how many), saves the library
    void trainingTipFault(const std::string& why);
    std::string tipFaultOf(const std::vector<birx::Point>& pts) const;   // a case's sweep: "" sound
    void caseTipFault(const std::string& why, const char* what);
    std::string notATipOf(const std::vector<birx::Point>& pts) const;   // H7-2.15.0: "" a tip in a material
    void caseNotATip(const std::string& why, const char* what);
    void trainingCheckDone(bool accepted, const birx::ElectrodeModel& eRaw, double r25, double ze, const std::string& why,
                           const birx::QcLimits& q);
    std::string m_calWarning;
    std::vector<birx::Point> m_matRefPts; // the last saline check's sweep (H7-2.6.2)
    mutable uint32_t m_matModelNRev = 0;
    mutable birx::MatModel m_matModelN;
    mutable birx::MatMatch m_mainMatch;   // the main screen's answer, for the sweep m_mainSeq
    mutable uint32_t m_mainSeq = 0, m_mainRev = 0;
    mutable double m_mainCp = -1;
    uint32_t m_sweepSeq = 0;
    std::vector<birx::Point> m_rawPts;    // the last sweep as measured, before any correction
    // the records for the programming cable (app_dump.cpp)
    uint8_t* m_dumpBuf = nullptr;
    size_t m_dumpLen = 0;
    int m_dumpPart = 0, m_dumpParts = 0, m_dumpFiles = 0;
    std::vector<std::string> m_dumpSkipped;
    // the tags of the readings of the cases (from the library)
    mutable std::map<std::string, std::string> m_readingTags;
    mutable uint32_t m_readingTagsRev = 0;
    mutable std::map<std::string, CaseTag> m_caseTags;
    mutable uint32_t m_caseTagsRev = 0;
    // the saline check
    DipResult m_dip;
    DipStage m_dipStage = DipStage::Idle;
    bool m_dipping = false;
    std::vector<birx::Spectrum> m_dipSweeps;
    double m_dipTemp = birx::kNaN;
    uint32_t m_dipWaitStart = 0;
    std::string m_dipError;
    // ---- lots (app_lots.cpp)
    void loadLots();
    bool saveLotRun(const birx::LotRun& r);
    bool saveLotFile();
    std::string lotPath(const birx::LotRun& r) const;
    void pollLots(uint32_t now);
    void lotSweepStarted();
    void finishLotSweep();
    void rejudgeCase() { redecideOpenCase(); }
    std::vector<birx::LotRun> m_lotRuns;
    uint32_t m_lotRev = 1;                       // bumped whenever a run changes
    mutable uint32_t m_lotCacheRev = 0;
    mutable bool m_lotCacheAir = false;
    mutable std::vector<birx::LotResult> m_lotCache;
    mutable std::vector<bool> m_lotCacheOk;
    birx::LotResult evaluateNow(int idx) const;
    mutable int m_cmpIdx = -1;
    mutable uint32_t m_cmpRev = 0;
    mutable bool m_cmpAir = false;
    mutable birx::LotCompare m_cmp;
    birx::LotFile m_lotFile;
    bool m_lotFileLoaded = false;
    std::string m_lotInForce = "\x01";
    bool m_lotTesting = false;
    LotStage m_lotStage = LotStage::Idle;
    int m_lotIdx = -1, m_lotNeedle = 1, m_lotStep = 0;
    double m_lotTemp = birx::kNaN, m_lotCond = birx::kNaN;
    uint32_t m_lotSettleUntil = 0, m_dipSettleUntil = 0;   // H7-2.6.0: the settling time ends
    uint32_t m_lotWaitStart = 0;
    std::string m_lotMsg;
    bool m_lotMsgOk = true;
    std::vector<std::string> m_delQueue;
    int m_delTotal = 0, m_delFailed = 0;
    long m_delBytes = 0;
    std::string m_delResult;
    void pollUnitRestart(uint32_t now);
    int      m_unitRestarts = 0;
    bool     m_unitReask = false, m_bootChecked = false;
    uint32_t m_unitBackBy = 0, m_unitRestartAt = 0, m_bootAt = 0;
    void pollSync(uint32_t now);
    void finishSync(bool heard);
    // diagnostic mode and health
    std::vector<std::string> diagnosticsLines();
    void resumeDiagnosticMode();
    void diagFlush();
    void healthTick();
    void checkPreviousSession();
    void finishCalibrationSweep();
    // case
    void redecideOpenCase();
    void publish();
    void clearResult();
    void retireNeedle();
    void applySettingsToEngine();
    bool persist(std::string* why);
    void saveCaseIdState();
    void recordCalibrationAttempt(int range, const birx::Spectrum& s, bool accepted,
                                  const std::string& why);
    bool writePatientRow();
    void storageFailed(const std::string& what);
    void openRecords();
    std::string noStoreMessage() const;
    // log
    struct LogRow {
        std::string caseId, tier, label, color, flags;
        int site = 0; int64_t epochMs = 0; bool usable = false;
        double z1k = birx::kNaN, ipsLegacy = birx::kNaN, ipsFit = birx::kNaN,
               probability = birx::kNaN;
        std::string age, birads, size;
    };
    void indexDataset();
    void indexPatients() const;
    void addLogRow(const birx::Spectrum& s, const birx::Verdict& v);
    LogCase summarise(const std::string& id, const std::vector<size_t>& rows) const;
    std::string newestDay() const;

    uint32_t m_rev = 1;
    // settings
    Settings    m_s;
    ConfigStore m_store;
    std::string m_settingsProblem;
    // clock
    bool     m_clockOk = false;
    int64_t  m_baseEpoch = 0;       // UTC ms at m_baseTicks
    uint32_t m_baseTicks = 0;
    uint32_t m_lastClockRead = 0;
    plat::Clock m_baseLocal;
    // H7-2.7.2: the timer check — the HAL tick against the battery clock
    void timerCheckStart(const plat::Clock& c);
    void timerCheckUpdate(const plat::Clock& c);
    int64_t  m_tcClockS = -1;       // the clock's seconds at the start (-1 none)
    uint32_t m_tcTicks = 0;
    double   m_tcRatio = 0;         // ticks per clock millisecond (0 not yet)
    int64_t  m_tcSpanS = 0;
    bool     m_tcWarned = false;
    // core
    birx::Parser          m_parser;
    birx::DecisionEngine  m_engine;
    birx::AggregatorConfig m_aggCfg;
    birx::QualityConfig   m_quality;
    std::shared_ptr<birx::RangeCalibrator> m_calib;
    birx::Spectrum        m_current;
    bool                  m_inSweep = false;
    uint32_t              m_sweepT0 = 0, m_pointT = 0;   // H7-2.12.3: when it began, when its last point came
    birx::Verdict         m_last;
    bool                  m_haveLast = false;
    std::vector<birx::Verdict>  m_caseReadings;
    std::vector<birx::Spectrum> m_caseSpectra;
    birx::CaseResult      m_caseResult;
    std::string           m_caseId;          // the measurement case (may be TEST-)
    birx::CaseIdGenerator m_ids;
    birx::PatientRecord   m_patient;
    bool                  m_committed = false;
    std::string           m_rememberedLot;
    std::unique_ptr<birx::NeedleRegistry> m_needles;
    birx::NeedleKeyring   m_needleKeys;
    std::unique_ptr<birx::DatasetWriter>  m_dataset;
    std::unique_ptr<birx::OutcomeStore>   m_outcomes;
    // needle calibration
    birx::NeedleCalibration m_cal, m_pending;
    bool        m_calibrating = false;
    CalStage    m_calStage = CalStage::Idle;
    std::string m_calError;
    uint32_t    m_calWaitStart = 0;
    // shown values
    bool m_sweepSound = false;
    // the healthy-tissue sample
    void finishTissueSweep();
    void refWatch(uint32_t waitedMs);
    void clearTissueRef();
    bool        m_refTaking = false;
    bool        m_chartTissue = false;
    RefStage    m_refStage = RefStage::Idle;
    std::string m_refError;
    uint32_t    m_refWaitStart = 0;
    std::vector<TissueSample> m_refs;             // of the open case, in the order taken
    mutable long m_refFileSize = -2;
    mutable std::map<std::string, std::vector<TissueSample>> m_refByCase;   // the record, read back
    std::string m_ipsText = "\xE2\x80\x94", m_z1kText = "\xE2\x80\x94";
    double      m_z1kOhm = birx::kNaN;
    // link state (Controller)
    bool     m_running = false;
    int      m_asks = 0;
    uint32_t m_askAt = 0, m_askedAt = 0;
    bool     m_padGap = false;              // an extra byte was just sent; the [R] follows
    uint32_t m_silenceAt = 0;
    bool     m_sawBytesThisRun = false;
    bool     m_discard = false;             // Stop pressed: drop the rest of this sweep
    int      m_discardPts = 0;
    uint32_t m_discardUntil = 0;            // a sweep beginning before this was stopped
    uint32_t m_lastByte = 0;
    bool     m_everByte = false;
    uint32_t m_lastLineInRun = 0;
    int      m_heartbeats = 0;
    int      m_unparsedTotal = 0;
    std::deque<std::string> m_unparsed, m_traffic, m_events;
    std::string m_linkMessage;
    SyncSt   m_syncSt = SyncSt::Idle;
    SyncWhy  m_syncWhy = SyncWhy::Silent;
    int      m_syncPads = 0;
    uint32_t m_syncAt = 0;
    uint32_t m_waitBeat = 0;                // after a sweep: no command until a heartbeat, or this time
    int      m_txOk = 0, m_txFail = 0;
    std::string m_cmdChannel;
    uint32_t m_probeDeadline = 0;
    int      m_probeBeats = 0;
    bool     m_sawTdp = false;
    std::string m_probeResult;
    std::string m_lineBuf;
    // diagnostic mode and health
    bool        m_diagOn = false;
    std::string m_diagPath;
    int         m_diagBeats = 0;
    std::string m_diagBeatsSince;
    std::string m_diagBuf;               // written out every 500 ms (and at once when large)
    uint32_t    m_diagFlushAt = 0;
    bool        m_diagInside = false;
    std::string m_lastSessionEnd;
    uint32_t    m_healthAt = 0;
    // records
    std::string m_records;
    std::string m_storageProblem;
    std::string m_toast;
    uint32_t    m_toastAt = 0;
    // log index
    std::vector<LogRow> m_logRows;
    std::map<std::string, std::vector<size_t>> m_byCase;
    long        m_indexedSize = -1;
    mutable std::map<std::string, std::map<std::string, std::string>> m_patientRows;
    mutable std::vector<std::string> m_patientCols;
    mutable long m_patientsSize = -2;
};

} // namespace app

#endif
