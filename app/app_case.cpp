// app/app_case.cpp — the case, the needle, the blockers, the settings and the
// sound. Counterparts: BirxBridge.cpp (case, needle) and controller.cpp
// (blockers, device id, passcode, protocol).
#include "app.h"
#include "version.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

using namespace birx;

namespace app {

namespace {
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...)
{
    char b[640];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
const char* kOperator = "operator";

int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}
} // namespace

// ============================================================================
// the patient and the case

std::string App::beginPatient(std::string* why)
{
    if (!m_ids.ready()) {
        if (why) *why = caseBlocker();
        return std::string();
    }
    // Days since 2024-01-01 in LOCAL time (QDate::currentDate in 2.18.0).
    const plat::Clock n = nowLocal();
    const int days = int(daysFromCivil(n.year, unsigned(n.month), unsigned(n.day)) -
                         daysFromCivil(2024, 1, 1));

    m_patient = PatientRecord();
    m_patient.caseId     = m_ids.next(days);
    m_patient.operatorId = kOperator;
    m_patient.startedAt  = isoUtc(nowMs());
    if (!m_rememberedLot.empty()) m_patient.set("needle_lot", m_rememberedLot);

    if (m_patient.caseId.empty()) {
        if (why) *why = fmt("No case id could be minted: the daily sequence of %d ids for today "
                            "is used up, and an id is never reused. (The clock says day %d "
                            "since 2024-01-01.)", CaseIdGenerator::kMaxPerDay, days);
        m_patient = PatientRecord();
        touch();
        return std::string();
    }
    saveCaseIdState();

    m_caseId = m_patient.caseId;
    m_committed = false;
    clearTissueRef();               // a new patient: the last one's healthy-tissue sample is not theirs
    // H7-2.10.0: a needle checked in Training was checked against Training's lot: the case checks its own
    if (m_calByTraining || (m_dip.ok && m_dip.training)) retireNeedle();
    m_caseReadings.clear();
    m_caseSpectra.clear();
    m_caseResult = CaseResult();
    m_last = Verdict();
    m_haveLast = false;
    clearResult();
    event("case " + m_caseId + " minted; intake form opened");
    touch();
    return m_caseId;
}

// Cancel on the intake form of a case that was never committed. 2.18.0 left
// such a case open (its id on the strip, no patient row written). Here it is
// dropped: the id stays used — it is never issued again — but no readings can
// be recorded under a case nobody filled in. Recorded as a deliberate
// difference in docs/DIFFERENCES.md.
void App::abandonPatient()
{
    if (m_committed || m_patient.caseId.empty()) return;
    event("case " + m_patient.caseId + " abandoned before the intake was committed; "
          "the id will not be reused");
    m_patient = PatientRecord();
    m_caseId.clear();
    clearTissueRef();
    m_caseReadings.clear();
    m_caseSpectra.clear();
    m_caseResult = CaseResult();
    touch();
}

void App::saveCaseIdState()
{
    m_s.caseDay = m_ids.day();
    m_s.caseSeq = m_ids.seq();
    std::string why;
    if (!persist(&why)) {
        m_settingsProblem = "The case-id counter could NOT be saved: " + why +
                            " After a restart this instrument could issue an id twice.";
        event(m_settingsProblem);
    }
    if (plat::sdReady() && !m_ids.save(m_records + "/caseid_state.txt"))
        storageFailed("caseid_state.txt could NOT be written \xE2\x80\x94 check the records memory.");
}

void App::setIntakeValue(const std::string& id, const std::string& v)
{
    // What was set, not what it was set to, for the two free-text fields.
    const bool privateField = id == "national_id" || id == "notes";
    diagWrite("intake", id + "  ->  " + (privateField ? std::string("(value hidden)") : v));
    m_patient.set(id, v);
    touch();
}

std::vector<std::string> App::intakeMissing() const
{
    std::vector<std::string> out;
    for (const std::string& s : m_patient.missing()) {
        const IntakeField* f = intakeField(s);
        out.push_back(f ? f->label : s);
    }
    return out;
}

std::string App::needleStatus() const
{
    if (!m_s.needleCheck) {
        const std::string serial = m_patient.get("needle_serial");
        return serial.empty() ? "NOT ENTERED" : "NOT CHECKED";
    }
    std::string canon;
    const NeedleStatus s = m_needleKeys.check(m_patient.get("needle_lot"),
                                              m_patient.get("needle_serial"), &canon);
    if (s != NeedleStatus::Ok) return needleStatusName(s);
    if (m_needles && m_needles->used(canon)) return "ALREADY USED";
    return "OK";
}

bool App::needleAccepted() const
{
    if (!m_s.needleCheck) return true;
    return needleStatus() == "OK";
}

bool App::commitPatient(std::string* why)
{
    if (m_patient.caseId.empty() || !m_patient.complete()) {
        if (why) *why = "The required fields are not complete.";
        return false;
    }
    std::string canon;
    if (!m_s.needleCheck) {
        const std::string lot = m_patient.get("needle_lot");
        const std::string serial = m_patient.get("needle_serial");
        canon = lot + "-" + serial;
        m_patient.needleCode = canon;
        if (!lot.empty()) m_rememberedLot = lot;
        if (m_needles && !serial.empty() && plat::sdReady())
            m_needles->markUsed(canon, m_patient.caseId, m_patient.startedAt);
    } else {
        const NeedleStatus ns = m_needleKeys.check(m_patient.get("needle_lot"),
                                                   m_patient.get("needle_serial"), &canon);
        if (ns != NeedleStatus::Ok) {
            if (why) *why = std::string("The needle code was refused: ") + needleStatusName(ns) + ".";
            return false;
        }
        if (!m_needles) { if (why) *why = "There is no needle register, so single use cannot be enforced."; return false; }
        if (m_needles->used(canon)) { if (why) *why = "This needle has already been used on this instrument."; return false; }
        if (!m_needles->markUsed(canon, m_patient.caseId, m_patient.startedAt)) {
            if (why) *why = "The needle register could NOT be written \xE2\x80\x94 check the records memory.";
            return false;
        }
        m_patient.needleCode = canon;
        m_rememberedLot = canon.substr(0, 4);
    }
    if (!writePatientRow()) {
        if (why) *why = "The patient record could NOT be written to patients.csv \xE2\x80\x94 "
                        "check the records memory. The test was not started.";
        return false;
    }
    m_committed = true;
    event("case " + m_patient.caseId + " started (intake committed)");
    touch();
    return true;
}

bool App::writePatientRow()
{
    if (!plat::sdReady()) return false;
    const std::string path = m_records + "/patients.csv";
    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) return false;
    if (fresh) f << PatientRecord::csvHeader() << "\n";
    f << m_patient.csvRow() << "\n";
    f.flush();
    const bool ok = f.good();
    f.close();
    m_patientsSize = -2;          // the log re-reads it
    return ok;
}

std::string App::caseSummary() const
{
    if (!caseOpen()) return std::string();
    const std::string side = m_patient.get("side");
    return intakeValue("age") + " y \xC2\xB7 BI-RADS " + intakeValue("birads") + " \xC2\xB7 " +
           (side.empty() ? std::string() : side.substr(0, 1)) + " " + intakeValue("clock") +
           " o'clock \xC2\xB7 " + intakeValue("lesion_size") + " mm";
}

// BirxBridge::endCase — aggregate once more, then close the case for real.
void App::endCase()
{
    m_caseResult = Aggregator(m_aggCfg).combine(m_caseReadings, m_engine.scoringModel(),
                                                &m_engine.ruleTable());
    if (plat::sdReady() && !m_calib->save(m_records + "/range_calibration.txt"))
        storageFailed("range_calibration.txt could NOT be written \xE2\x80\x94 check the records memory.");
    event("case " + m_caseId + " ended: " + caseLabel() +
          fmt(" on %d valid reading(s)", m_caseResult.nValid));

    m_caseReadings.clear();
    m_caseSpectra.clear();
    m_caseResult = CaseResult();
    m_last = Verdict();
    m_haveLast = false;
    m_caseId.clear();
    m_patient = PatientRecord();
    m_committed = false;
    clearTissueRef();
    clearResult();
    retireNeedle();
    touch();
}

// ============================================================================
// needle calibration (BirxBridge + CalibrateSheet.qml)

std::string App::calBlocker() const
{
    if (!connected())
        return "No link to the measurement unit. The calibration is a real six-second "
               "sweep and needs the measurement unit to answer \xE2\x80\x94 check its cable and power.";
    if (m_dipping)
        return "A saline check is running. Wait for it to finish.";
    if (m_running || m_inSweep || m_discard)
        return "A measurement is already running. Wait for it to finish.";
    if (m_syncSt != SyncSt::Idle)
        return "The link to the measurement unit is being checked. Try again in a few seconds.";
    return std::string();
}

void App::calOpen(bool training)
{
    calStop();
    m_calTraining = training;
    m_calError.clear();
    m_calStage = CalStage::Idle;
    touch();
}

void App::calRun()
{
    if (!calBlocker().empty()) return;
    m_pending = NeedleCalibration();
    m_pending.needleId   = m_patient.get("needle_serial");
    if (m_calTraining || (!caseOpen() && m_pending.needleId.empty())) m_pending.needleId = m_s.trainNeedle;   // H7-2.9.0: Training's needle
    m_pending.batchId    = m_rememberedLot;
    m_pending.needleRev  = m_s.needleRev;
    m_pending.operatorId = kOperator;
    m_pending.takenAt    = isoUtc(nowMs());
    m_calibrating = true;
    m_calError.clear();
    clearDip("a new Air check");          // the saline check belongs to the air check before it
    m_calStage = CalStage::Waiting;
    m_calWaitStart = plat::ticks();
    event("needle calibration started");
    startRun();
    touch();
}

// Cancelling, closing the sheet or a timeout abandons the calibration in the
// model too: the next sweep must never be taken as an open-air reference.
void App::calStop()
{
    const bool active = m_calStage == CalStage::Waiting || m_calStage == CalStage::Sweeping;
    if (active) stopRun();
    if (m_calibrating) {
        m_calibrating = false;
        m_pending = NeedleCalibration();
        event("needle calibration abandoned; nothing was stored");
    }
    if (m_calStage != CalStage::Done) m_calStage = CalStage::Idle;
    touch();
}

int App::calSecondsLeft() const
{
    const int limit = (m_calStage == CalStage::Sweeping) ? 25000 : 30000;
    const int waited = int(uint32_t(plat::ticks() - m_calWaitStart));
    return std::max(0, (limit - waited + 999) / 1000);
}

void App::finishCalibrationSweep()
{
    const int range = 3;        // the range a lesion-sized load sits on (spec §9.3 step 7)
    Spectrum s = m_current;
    for (Point& p : s.points) if (p.range <= 0) p.range = range;

    std::string why;
    if (!m_pending.addOpenSweep(range, s, {}, &why)) {
        m_calError = why;
        recordCalibrationAttempt(range, s, false, why);
        event("open sweep on range 3 rejected: " + why);
        m_calibrating = false;
        m_pending = NeedleCalibration();
        m_calStage = CalStage::Idle;
        return;
    }
    // endNeedleCalibration
    m_calibrating = false;
    if (m_pending.rangeCount() == 0) {
        recordCalibrationAttempt(range, s, false, "no gain range was calibrated");
        m_calError = "No gain range was calibrated. Repeat the routine.";
        m_calStage = CalStage::Idle;
        return;
    }
    // H7-2.10.0: the needle's QC — its stray capacitance and leakage within its lot's limits (Settings -> Lots,
    // carried in the lot file), or the generic ones; outside them nothing is measured with it
    if (m_pending.calibrated(3) && (m_calTraining || m_s.salineCheck != 0)) {
        const RangeCal& rc = m_pending.ranges().at(3);
        const std::string lot = m_calTraining ? m_s.trainLot : currentLotId();
        const QcLimits q = qcLimitsFor(lot, m_calTraining ? kCheck9 : dipStep());
        std::string qw = q.airWhy(rc.open.cpFarad, rc.open.gpSiemens);
        // H7-2.15.0: and against its own leakage when it was sound (a worn needle passes the generic limit)
        if (qw.empty()) qw = needleLeakWhy(m_pending.needleId, rc.open.gpSiemens);
        if (!qw.empty()) {
            recordCalibrationAttempt(range, s, false, "QC: " + qw);
            m_calError = "Air check refused: " + qw + " Use another needle, or dry this one and check it again in clean air.";
            event("needle calibration refused by its QC: " + qw);
            m_pending = NeedleCalibration();
            m_cal = NeedleCalibration();          // the needle is out of its limits: no Air check of it is in force
            m_calAtMs = 0;
            m_calStage = CalStage::Idle;
            return;
        }
    }
    recordCalibrationAttempt(range, s, true, std::string());
    m_cal = m_pending;
    m_calAtMs = nowMs();
    if (m_cal.calibrated(3)) noteNeedleLeak(m_cal.needleId, m_cal.ranges().at(3).open.gpSiemens);   // H7-2.15.0
    m_calByTraining = m_calTraining;
    // a new Air check: the Training needle's saline check is due again (qcCheckOk asks for one taken after
    // it); the last one stays the base of the bracket, and the next one closes the sweeps since it
    m_pending = NeedleCalibration();
    if (plat::sdReady()) {
        const std::string id = m_cal.needleId.empty() ? std::string("needle") : m_cal.needleId;
        // A needle serial is typed by hand; keep the file name to safe characters.
        std::string safe;
        for (char c : id) safe.push_back((std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_');
        if (!m_cal.save(m_records + "/calibration/" + safe + ".txt"))
            storageFailed("The needle calibration file could NOT be written \xE2\x80\x94 check the records memory.");
        if (!m_cal.appendToLog(m_records + "/needle_cal.csv"))
            storageFailed("The calibration could NOT be appended to needle_cal.csv \xE2\x80\x94 check the records memory.");
    }
    m_calError.clear();
    // H7-2.6.2: accepted, but said: a stray capacitance far above a needle's in air is a wet tip or shaft,
    // a hand or metal near it, or a needle that carries much of its own
    m_calWarning.clear();
    if (m_cal.calibrated(3)) {
        const double cpPf = m_cal.ranges().at(3).open.cpFarad * 1e12;
        if (cpPf > 300)
            m_calWarning = fmt("The stray capacitance, %.0f pF, is far above a usual needle and cable (40 to 150 pF). If the "
                               "tip or the shaft was wet, or a hand or metal was near it, dry it and do the Air check again. "
                               "A needle of two insulated conductors, or a long cable, can have that much: the high "
                               "frequencies are then mostly the needle's own capacitance, and what is under it is harder "
                               "to see.", cpPf);
    }
    m_calStage = CalStage::Done;
    event("needle calibrated: " + m_cal.describe() + (m_calWarning.empty() ? std::string() : " WARNING: " + m_calWarning));
}

void App::recordCalibrationAttempt(int range, const Spectrum& s, bool accepted,
                                   const std::string& why)
{
    if (!plat::sdReady()) return;
    const std::string path = m_records + "/needle_cal_attempts.csv";
    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) { storageFailed("needle_cal_attempts.csv could NOT be written \xE2\x80\x94 check the records memory."); return; }
    if (fresh) f << "when,needle,range,accepted,why,freqHz,magOhm,phaseDeg,pointRange\n";
    const std::string when = isoUtc(nowMs());
    const std::string needle = m_pending.needleId.empty() ? std::string("(unidentified)")
                                                          : m_pending.needleId;
    std::string quoted;
    for (char c : why) { if (c == '"') quoted += "\"\""; else quoted.push_back(c); }
    if (s.points.empty())
        f << when << ',' << needle << ',' << range << ',' << (accepted ? "yes" : "no")
          << ",\"" << quoted << "\",,,,\n";
    for (const Point& p : s.points) {
        char b[96];
        std::snprintf(b, sizeof b, "%.1f,%.3f,%.3f,%d", p.freqHz, p.magOhm, p.phaseDeg, p.range);
        f << when << ',' << needle << ',' << range << ',' << (accepted ? "yes" : "no")
          << ",\"" << quoted << "\"," << b << '\n';
    }
    if (!f.good()) storageFailed("needle_cal_attempts.csv could NOT be written \xE2\x80\x94 check the records memory.");
}

void App::retireNeedle()
{
    const bool had = m_cal.rangeCount() > 0;
    if (m_calStage == CalStage::Waiting || m_calStage == CalStage::Sweeping) stopRun();
    m_calibrating = false;
    m_pending = NeedleCalibration();
    m_cal = NeedleCalibration();
    m_calAtMs = 0;
    m_calByTraining = false;
    m_qc = QcCheck();
    m_qcSweeps = 0;
    m_needleWetMs = 0;                  // H7-2.12.0: the next needle's time in the saline starts afresh
    m_dipAtMs = 0;
    m_calError.clear();
    m_calStage = CalStage::Idle;
    if (m_dipping) dipStop();
    clearDip("needle retired");
    if (had) event("needle retired; the next case needs a new needle calibrated");
}

std::string App::needleCalibration() const
{
    return m_cal.rangeCount() ? m_cal.describe() : std::string("no needle calibration");
}

std::string App::calibrationReport() const
{
    std::string o;
    o += fmt("calibrating now : %s\n", m_calibrating ? "yes" : "no");
    o += fmt("needle ready    : %s\n", needleReady() ? "yes" : "no");
    o += "in force        : " + needleCalibration() + "\n";
    o += "last error      : " + (m_calError.empty() ? std::string("(none)") : m_calError) + "\n";
    o += "needle revision : " + m_s.needleRev + "\n";
    auto describe = [&o](const char* which, const NeedleCalibration& c) {
        if (c.rangeCount() == 0) { o += std::string(which) + ": nothing\n"; return; }
        for (const auto& kv : c.ranges()) {
            const ParasiticModel& m = kv.second.open;
            o += fmt("%s range %d: Cp %.1f pF, Gp %.2e S, %d points, residual %.3f, ok=%s\n",
                     which, kv.first, m.cpFarad * 1e12, m.gpSiemens, m.nPoints, m.rmsRel,
                     m.ok ? "yes" : "no");
        }
    };
    describe("stored ", m_cal);
    describe("pending", m_pending);
    return o;
}

// ============================================================================
// blockers (Controller::measureBlocker / caseBlocker, ActionRow.qml)

std::string App::caseBlocker() const
{
    if (m_s.deviceId.empty() || !m_ids.ready())
        return "This instrument has no device ID, so no case id can be minted \xE2\x80\x94 every "
               "case id starts with it, and a made-up one would collide with another "
               "instrument's. Set it in Settings \xE2\x86\x92 Device.";
    return std::string();
}

std::string App::measureBlocker() const
{
    if (!connected())
        return m_everByte
            ? fmt("No link to the measurement unit: nothing has arrived on its cable for %u s. "
                  "Check the cable and that the measurement unit is switched on.",
                  unsigned(lastByteAgeMs() / 1000))
            : "No link to the measurement unit: nothing has arrived on its cable since this "
              "screen was switched on. Check the cable and that the measurement unit is "
              "switched on.";
    if (m_running || m_inSweep)
        return "A measurement is already running.";
    // H7-2.14.0: a case whose result is final takes no more readings (Settings -> Rules -> the case result)
    if (caseOpen() && m_caseResult.closed)
        return "The case is closed: its result is final (" + caseLabel() + ", " + std::to_string(m_caseResult.nValid) +
               " readings). End case, then the next lesion. (Settings \xE2\x86\x92 Rules: when the result is final, close the case.)";
    if (m_lotTesting || m_dipping || m_matTesting)
        return "A lot test, a saline check or a material sweep is in progress.";
    if (m_discard)
        return "The measurement unit is finishing the sweep that was stopped (it cannot be "
               "interrupted); its points are being discarded. Ready again in a few seconds.";
    if (m_s.reqNeedleCal && airCalUsed() && !needleReady())
        return "This needle has not been calibrated. Six seconds in air, once per needle "
               "\xE2\x80\x94 100 pF of stray capacitance is 12 IPS units and the whole diagnostic "
               "span is 4.6.";
    // H7-2.3.0: a lot approved per needle reads right only with this needle's own checks;
    // H7-2.10.0: and every needle measures only after both (its QC), within its lot's limits
    if (dipNeeded() && !needleReady())
        return "QC: this needle needs its checks before it measures: Air check first (six seconds in air), then the "
               "Saline check (the tip 10 s in the " + dipSolutionName() + " saline). A needle outside its lot's limits is refused.";
    if (dipNeeded() && !m_dip.ok)
        return "QC: the Saline check of this needle is not done (the tip 10 s in the " + dipSolutionName() +
               " saline, after the Air check)." + std::string(dipCorrects(currentLotId()) ? " Its readings are carried to the reference needle's with its own model." : "") +
               (m_dipError.empty() ? std::string() : " It was refused: if it is refused again, replace the needle.");
    if (m_s.reqTissueRef && caseOpen() && m_refs.empty())
        return "The healthy-tissue sample of this patient has not been taken. With the needle in "
               "normal tissue, before the lesion: Healthy tissue, on the needle line above.";
    if (m_s.reqIntake && !intakeComplete())
        return "The intake form is not complete, so this reading could never be matched "
               "to a pathology result.";
    // 2.18.0 tests the NEEDLE calibration's range count here (spec §11.5).
    if (m_s.reqRangeCal && calibratedRanges() < 1)
        return "No gain range has a learned phase offset yet.";
    if (m_s.reqLotFile && !m_lotFileLoaded)
        return "No lot calibration file is installed. Load the latest one: Settings \xE2\x86\x92 Lots "
               "\xE2\x86\x92 Load from USB.";
    if (m_s.reqLotFile && currentLotId().empty())
        return "No needle lot is known. Type the lot number at intake.";
    if (m_s.reqLotFile && !lotCovered())
        return "Lot " + currentLotId() + " is not in the lot calibration file (rev " + std::to_string(m_lotFile.rev) +
               "). Load a newer file, or measure the lot (Settings \xE2\x86\x92 Lots).";
    return std::string();
}

std::string App::qcBlocker() const
{
    if (!dipNeeded()) return std::string();
    if (!needleReady()) return "this needle has not passed its Air check";
    if (!m_dip.ok) return "this needle has not passed its Saline check";
    return std::string();
}

std::string App::actionReason() const
{
    // What the last request came to, while it is still the newest thing known:
    // a Sweep that the measurement unit did not answer must say so here, where
    // the button is, and not only in the diagnostics.
    if (m_syncSt != SyncSt::Idle)
        return m_syncPads ? fmt("Testing the link: re-aligning the command framing (step %d of 2)\xE2\x80\xA6", m_syncPads)
                          : std::string("Testing the link to the measurement unit\xE2\x80\xA6");
    if (m_asks > 0)
        return fmt("Asked the measurement unit for a sweep %d s ago \xE2\x80\x94 waiting for it to begin.",
                   askingSeconds());
    if (!m_linkMessage.empty() && !m_running && !m_inSweep) return m_linkMessage;
    if (!connected()) return measureBlocker();
    if (!caseOpen() && !canStartCase()) return caseBlocker();
    if (m_discard) return measureBlocker();
    if (!canMeasure() && !m_running && !m_inSweep) return measureBlocker();
    return std::string();
}

// ============================================================================
// settings

bool App::persist(std::string* why)
{
    const std::string before = m_store.lastText();
    const bool ok = m_store.save(m_s, why);
    if (m_diagOn) {
        // Each setting that changed, as written, and whether it read back.
        std::set<std::string> old;
        std::istringstream a(before);
        std::string l;
        while (std::getline(a, l)) old.insert(l);
        std::istringstream b(ConfigStore::serialise(m_s));
        int n = 0;
        while (std::getline(b, l)) {
            if (old.count(l)) continue;
            ++n;
            if (l.compare(0, 3, "pc=") == 0) l = "pc=(hidden)";
            diagWrite("setting", l + "  ->  " + (ok ? std::string("saved and read back") : (why ? *why : std::string("FAILED"))));
        }
        if (!n) diagWrite("setting", std::string("saved, nothing changed  ->  ") + (ok ? "saved and read back" : "FAILED"));
    }
    return ok;
}

void App::applySettingsToEngine()
{
    // DecisionEngine configured exactly as Controller::setupBirx does it.
    m_quality = QualityConfig();
    // No placement gate (H7-1.4.0): Z at 1 kHz refuses nothing by itself. A site
    // that wants high impedance refused or named writes a row for it.
    m_quality.fatThresholdOhm = std::numeric_limits<double>::infinity();
    m_quality.requireCalibratedRanges = m_s.reqRangeCal;
    m_engine.setQualityConfig(m_quality);

    AggregatorConfig c;
    c.minValidReadings    = std::max(1, std::min(m_s.readings, 20));
    c.earlyStopOnPositive = m_s.earlyStop;
    c.minForEarlyStop     = std::max(1, std::min(m_s.minForEarlyStop, c.minValidReadings));
    c.maxSpreadIps        = (std::isfinite(m_s.maxSpreadIps) && m_s.maxSpreadIps > 0)
                                ? m_s.maxSpreadIps : 4.0;
    m_aggCfg = c;

    // Only the rule table decides on this instrument (spec §18.5).
    m_engine.setRuleTable(m_s.rules.toRuleTable(true));
}

bool App::saveSettings(const Settings& s, std::string* why)
{
    const Settings old = m_s;
    m_s = s;
    std::string w;
    if (!persist(&w)) {
        m_s = old;
        if (why) *why = w;
        m_settingsProblem = w;
        touch();
        return false;
    }
    m_settingsProblem.clear();
    applySettingsToEngine();
    plat::voiceVolume(m_s.volume);
    plat::backlight(m_s.backlight);
    if (!m_caseSpectra.empty()) redecideOpenCase();
    event(fmt("settings saved: %d readings, early stop %s (from %d), spread limit %.1f IPS, "
              "no placement gate (the rule table alone decides)", m_aggCfg.minValidReadings,
              m_aggCfg.earlyStopOnPositive ? "on" : "off", m_aggCfg.minForEarlyStop,
              m_aggCfg.maxSpreadIps));
    touch();
    return true;
}

// Controller::setDeviceId — the refusal names the problem.
std::string App::setDeviceId(const std::string& text)
{
    std::string id;
    for (char c : text) if (!std::isspace((unsigned char)c)) id.push_back(char(std::toupper((unsigned char)c)));
    // count characters, not bytes
    size_t chars = 0;
    for (unsigned char c : id) if ((c & 0xC0) != 0x80) ++chars;
    if (chars != 4)
        return fmt("A device ID is exactly four characters. \"%s\" is %u.", id.c_str(), unsigned(chars));

    static const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string bad;
    for (char c : id)
        if (!std::strchr(kAlphabet, c) && bad.find(c) == std::string::npos) bad.push_back(c);
    if (!bad.empty()) {
        std::string suggestion = id;
        for (char& c : suggestion) { if (c == 'I' || c == 'L') c = '1'; else if (c == 'O') c = '0'; }
        bool usable = true;
        for (char c : suggestion) if (!std::strchr(kAlphabet, c)) usable = false;
        std::string list;
        for (size_t i = 0; i < bad.size(); ++i) { if (i) list += ", "; list.push_back(bad[i]); }
        std::string msg = "A device ID cannot contain " + list + ". The letters I, L, O and U are "
                          "left out so that an id cannot be misread \xE2\x80\x94 I and L look like 1, "
                          "O looks like 0. Allowed: 0-9 and A-Z without I, L, O, U.";
        if (usable && suggestion != id) msg += " Try " + suggestion + ".";
        return msg;
    }
    CaseIdGenerator probe;
    std::string why;
    if (!probe.setDeviceId(id, &why)) return "The instrument refused \"" + id + "\": " + why;

    Settings s = m_s;
    s.deviceId = id;
    if (s.deviceId != m_s.deviceId) { s.caseDay = -1; s.caseSeq = 0; }
    if (!saveSettings(s, &why))
        return id + " was accepted but could NOT be saved: " + why +
               " Until it is, nothing set on this screen survives a restart.";
    // Read it back from the settings memory itself (spec §11.1).
    Settings check;
    ConfigStore probeStore;
    if (!probeStore.load(&check) || check.deviceId != id)
        return id + " was written but did NOT read back from the settings memory. It will "
               "be lost at the next restart.";
    m_ids = CaseIdGenerator();
    m_ids.setDeviceId(id);
    m_ids.restore(m_s.caseDay, m_s.caseSeq);
    if (plat::sdReady()) m_ids.save(m_records + "/caseid_state.txt");
    event("Device ID set to " + id + ". Case ids will start with it.");
    touch();
    return std::string();
}

bool App::checkPassword(const std::string& code) const
{
    std::string pc = m_s.passcode;
    if (pc.size() != 4) pc = "7324";        // a lock with no key is broken, not safer
    return code == pc;
}

std::string App::changePassword(const std::string& cur, const std::string& next,
                                const std::string& confirm)
{
    if (!checkPassword(cur)) return "The current passcode is wrong.";
    if (next.size() != 4) return "The passcode is 4 digits.";
    for (char c : next) if (!std::isdigit((unsigned char)c)) return "The passcode can only be digits.";
    if (next != confirm) return "The two entries are not the same.";
    Settings s = m_s;
    s.passcode = next;
    std::string why;
    if (!saveSettings(s, &why)) return "The new passcode could NOT be saved: " + why;
    Settings check;
    ConfigStore probe;
    if (!probe.load(&check) || check.passcode != next)
        return "The new passcode was written but did NOT read back. The old one may still "
               "be in force after a restart.";
    event("The settings passcode was changed.");
    return std::string();
}

std::string App::saveRules(const RulesConfig& r)
{
    Settings s = m_s;
    s.rules = r;
    std::string why;
    if (!saveSettings(s, &why)) return why;
    event("rule table saved: " + ruleSummary());
    return std::string();
}

birx::RuleHit App::previewVerdict(double z1k, double ips) const
{
    return m_engine.ruleTable().lookup(z1k, ips);
}

std::string App::ruleSummary() const
{
    const RuleTable& t = m_engine.ruleTable();
    std::string o;
    for (size_t i = 0; i < t.rows.size(); ++i) {
        const RuleRow& r = t.rows[i];
        o += fmt("%s%s: Z %.0f\xE2\x80\x93%.0f \xCE\xA9, IPS %.1f\xE2\x80\x93%.1f \xC2\xB7 ",
                 r.label.c_str(), r.enabled ? "" : " (off)", r.zMin, r.zMax, r.ipsMin, r.ipsMax);
    }
    o += "otherwise " + t.defaultLabel;
    return o;
}

std::string App::rangeCalSummary() const
{
    std::string o;
    for (int r = 2; r <= RangeCalibrator::kRanges; ++r) {
        const int n = m_calib->samples(r);
        if (n == 0) continue;
        if (!o.empty()) o += ", ";
        o += fmt("range %d: %s%.2f\xC2\xB0 (n=%d)%s", r, m_calib->offsetDeg(r) >= 0 ? "+" : "",
                 m_calib->offsetDeg(r), n, m_calib->calibrated(r) ? "" : " \xE2\x80\x94 not yet enough");
    }
    return o.empty() ? std::string("no range data yet") : o;
}

// ============================================================================
// sound: the DY voice module plays numbered tracks from its own memory. A
// rule's "sound" names one of them; empty = silent (the shipped table).

// The words in brackets are what the previous firmware of this instrument
// used each recording for (MainScreenView.hpp: CANCER 3, WARNING_DANGER 1,
// WARNING 8, FREE_WARNING 5, HEALTHY 6, CONNECT 4, TESTING 7, NOISY 12; 11 was
// its settings test). What is actually recorded under a number is on the
// module and can only be heard.
const std::vector<std::string>& App::soundLibrary()
{
    static const std::vector<std::string> lib = {
        "Track 1 (positive)", "Track 2", "Track 3 (strongly positive)",
        "Track 4 (connected)", "Track 5 (suspicious)", "Track 6 (negative)",
        "Track 7 (during a sweep)", "Track 8 (warning)", "Track 9",
        "Track 10", "Track 11 (test)", "Track 12 (noisy)"
    };
    return lib;
}

int App::trackOf(const std::string& label)
{
    const auto& lib = soundLibrary();
    for (size_t i = 0; i < lib.size(); ++i) if (lib[i] == label) return int(i + 1);
    // "Track N …" — also a label saved under the names an earlier version gave
    int n = 0;
    if (std::sscanf(label.c_str(), "Track %d", &n) == 1 && n >= 1 && n <= 12) return n;
    return 0;
}

void App::playSound(const std::string& label)
{
    if (m_s.muted || label.empty()) return;
    const int t = trackOf(label);
    if (t > 0) plat::voicePlay(t);
}

void App::testTrack(int track)
{
    m_sweepSound = false;
    plat::voicePlay(track);
}

// As the previous firmware of this instrument: one recording repeats from the
// start of a sweep until it ends ("playSound(TESTING, 1)" … "playSound(0, 0)"),
// and the answer's own sound, if its rule has one, follows.
void App::sweepSoundStart()
{
    if (m_sweepSound || m_s.muted || m_s.sweepTrack < 1 || m_s.sweepTrack > 12) return;
    plat::voicePlay(m_s.sweepTrack, true);
    m_sweepSound = true;
}

void App::sweepSoundStop()
{
    if (!m_sweepSound) return;
    m_sweepSound = false;
    plat::voiceStop();
}

void App::setSweepTrack(int track)
{
    Settings s = m_s;
    s.sweepTrack = std::max(0, std::min(12, track));
    std::string why;
    saveSettings(s, &why);
    touch();
}

void App::setMuted(bool m)
{
    Settings s = m_s;
    s.muted = m;
    std::string why;
    if (!saveSettings(s, &why)) { m_s.muted = m; setToast("Mute set, but not saved: " + why); }
    if (m) { m_sweepSound = false; plat::voiceStop(); }
    touch();
}

void App::setVolume(int v)
{
    Settings s = m_s;
    s.volume = std::max(5, std::min(30, v));
    std::string why;
    saveSettings(s, &why);
}

} // namespace app
