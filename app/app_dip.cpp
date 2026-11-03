// app/app_dip.cpp — the saline check (H7-2.3.0).
//
// For a lot approved "per needle" (lotfit.h, LotVerdict2::UsableWithPerNeedle)
// the needles differ from each other too much for one correction: the tip's
// surface (the electrode interface) and its size are measured on EACH needle
// before its case. After the Air check (its stray capacitance), the tip is held
// 10 s in the weakest lot saline (0.09 %); two sweeps give its interface and
// its resistance there, the latter at 25 °C. The check is refused when the
// sweeps disagree (a leak: the liquid creeping under the insulation shows as
// a falling impedance), when they do not look like a tip in saline, or when
// they fall outside what the lot's good needles measured. Accepted, it carries
// every reading of the needle onto the reference needle's (needleTransfer).
//
// Cleared with the needle: at End case (retireNeedle), at a new Air check and
// when the lot changes. Every attempt goes to records/saline_check.csv.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>

using namespace birx;

namespace app {

namespace {
const int kDipStartMs = 30000;
const int kDipSweepMs = 25000;
const int kDipSweeps = 2;
constexpr double kPiD = 3.14159265358979323846;

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
double near100k(const Spectrum& s)
{
    double best = 1e300, z = kNaN;
    for (const Point& p : s.points) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0)) continue;
        const double d = std::fabs(std::log(p.freqHz / 1e5));
        if (d < best) { best = d; z = p.magOhm; }
    }
    return z;
}
} // namespace

int App::dipStep() const { return m_dipTraining ? kCheck9 : m_s.checkSolution == 1 ? kCheck09 : kCheck9; }

std::string App::dipSolutionName() const { return m_s.checkSolution == 1 ? "0.09 %" : "0.9 %"; }

bool App::dipValidated() const
{
    const LotCorrection* c = m_lotFileLoaded ? m_lotFile.find(currentLotId()) : nullptr;
    return c && c->hasModel() && (c->dipMask & (dipStep() == kCheck9 ? 2 : 1)) != 0;
}

// H7-2.4.0: Settings -> Needle -> "Needle check before each case":
//   0 off: the lot corrections only (2.3.0 without its per-needle lots);
//   1 the lots approved per needle;
//   2 every needle (default), whenever the lot file defines the reference needle in the
//     check solution: a lot validated with that solution uses its own window, a lot in
//     the file but not validated with it keeps the lot's mean correction, and a lot not in
//     the file (unless Settings -> Needle requires one) is checked with the generic window
//     and marked NOT validated.
bool App::dipNeeded() const { return dipNeededFor(currentLotId()); }

// H7-2.10.0: with "every needle" (2), every needle is checked before it measures — in the air and in
// the check saline, against its lot's limits or the generic ones — whatever the lot file holds; the
// check carries the reading to the reference needle only where it did before (dipCorrects).
bool App::dipNeededFor(const std::string& lot) const
{
    if (m_s.salineCheck == 2) return true;
    return dipCorrects(lot);
}

bool App::dipCorrects(const std::string& lot) const
{
    if (m_s.salineCheck == 0 || !m_lotFileLoaded || !std::isfinite(m_lotFile.refCpF)) return false;
    const LotCorrection* c = m_lotFile.find(lot);
    if (c && c->usable() && c->hasModel() && c->mode == 1) return true;
    if (m_s.salineCheck < 2 || !m_lotFile.refFor(dipStep()).ok) return false;
    if (c && c->usable()) return c->hasModel() && c->dipValid(dipStep());
    return !m_s.reqLotFile;
}

std::string App::dipBlocker() const
{
    if (!dipNeeded()) return "This needle's lot does not need a saline check.";
    if (dipCorrects(currentLotId())) {
        const LotCorrection* c = m_lotFile.find(currentLotId());
        if (!m_lotFile.refFor(dipStep()).ok)
            return "The lot calibration file has no model of the reference needle in the " + dipSolutionName() +
                   " saline. Put the reference in the lot file again (Settings \xE2\x86\x92 Lots) with this version, or "
                   "choose the other check solution (Settings \xE2\x86\x92 Needle).";
        if (c && c->hasModel() && c->mode == 1 && !c->dipValid(dipStep()))
            return "Lot " + currentLotId() + " was not validated with the " + dipSolutionName() + " saline check. Choose the " +
                   (dipStep() == kCheck9 ? "0.09 %" : "0.9 %") + " one in Settings \xE2\x86\x92 Needle.";
    }
    if (!needleReady()) return "Air check first: the saline check needs this needle's stray capacitance.";
    if (!connected())
        return "No link to the measurement unit. Check its cable and power.";
    if (m_calibrating || m_refTaking || m_lotTesting) return "Another measurement is in progress.";
    if ((m_running || m_inSweep || m_discard) && !m_dipping) return "A measurement is already running. Wait for it to finish.";
    return std::string();
}

void App::dipOpen()
{
    dipStop();
    m_dipError.clear();
    m_dipStage = m_dip.ok ? DipStage::Done : DipStage::Idle;
    touch();
}

void App::dipRun(double tempC, double condMsCm)
{
    if (!dipBlocker().empty()) return;
    m_dipTraining = false;
    dipRunCore(tempC, condMsCm);
}

void App::dipRunCore(double tempC, double condMsCm)
{
    if ((!std::isfinite(tempC) || tempC < 5 || tempC > 45) && !(condMsCm > 0)) {
        m_dipError = "Enter the saline's temperature (5 to 45 \xC2\xB0""C), or its conductivity.";
        touch();
        return;
    }
    clearDip(nullptr);
    m_dipTemp = tempC;
    m_dipCond = condMsCm > 0 ? condMsCm : kNaN;
    m_dipSweeps.clear();
    m_dipping = true;
    m_dipError.clear();
    event(fmt("saline check started (%s, %.1f C, %.2f mS/cm, settling %d s)", dipSolutionName().c_str(), tempC, m_dipCond, m_s.settleS));
    if (m_s.settleS > 0) {          // H7-2.4.1: the same settling time as the lot tests
        m_dipStage = DipStage::Settling;
        m_dipSettleUntil = plat::ticks() + uint32_t(m_s.settleS) * 1000u;
        touch();
        return;
    }
    m_dipStage = DipStage::Waiting;
    m_dipWaitStart = plat::ticks();
    startRun();
    touch();
}

void App::dipStop()
{
    const bool active = m_dipStage == DipStage::Waiting || m_dipStage == DipStage::Sweeping;
    if (active) stopRun();
    if (m_dipping) {
        m_dipping = false;
        m_dipSweeps.clear();
        event("saline check abandoned; nothing was stored");
    }
    m_dipTraining = false;
    if (m_dipStage != DipStage::Done) m_dipStage = DipStage::Idle;
    touch();
}

int App::dipSecondsLeft() const
{
    if (m_dipStage == DipStage::Settling)
        return std::max(0, int(int32_t(m_dipSettleUntil - plat::ticks()) + 999) / 1000);
    const int limit = (m_dipStage == DipStage::Sweeping) ? kDipSweepMs : kDipStartMs;
    const int waited = int(uint32_t(plat::ticks() - m_dipWaitStart));
    return std::max(0, (limit - waited + 999) / 1000);
}

void App::clearDip(const char* why)
{
    const bool had = m_dip.ok;
    m_dip = DipResult();
    if (m_dipStage == DipStage::Done) m_dipStage = DipStage::Idle;
    if (had && why) event(std::string("saline check cleared: ") + why);
}

void App::dipSweepStarted()
{
    if (m_dipping && m_dipStage == DipStage::Waiting) {
        m_dipStage = DipStage::Sweeping;
        m_dipWaitStart = plat::ticks();
    }
}

void App::pollDip(uint32_t now)
{
    if (!m_dipping) return;
    if (m_dipStage == DipStage::Settling) {
        if (int32_t(now - m_dipSettleUntil) >= 0 && !busy()) {
            m_dipStage = DipStage::Waiting;
            m_dipWaitStart = now;
            startRun();
        } else if (int32_t(now - m_dipSettleUntil) >= kDipStartMs) {
            dipStuck();             // H7-2.11.2: the unit stayed busy after the settling
            return;
        }
        static uint32_t lastSec = 0;
        if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
        return;
    }
    if (m_dipStage == DipStage::Between && !busy()) {      // the second sweep, once the unit is idle again
        m_dipStage = DipStage::Waiting;
        m_dipWaitStart = now;
        startRun();
        touch();
        return;
    }
    if (m_dipStage == DipStage::Between) {                 // H7-2.11.2: the unit never became idle again
        if (uint32_t(now - m_dipWaitStart) >= uint32_t(kDipSweepMs + kDipStartMs)) dipStuck();
        return;
    }
    if (m_dipStage == DipStage::Waiting || m_dipStage == DipStage::Sweeping) {
        const int limit = (m_dipStage == DipStage::Sweeping) ? kDipSweepMs : kDipStartMs;
        if (uint32_t(now - m_dipWaitStart) >= uint32_t(limit)) {
            m_dipError = m_dipStage == DipStage::Sweeping
                ? "The sweep began but never finished. Check the cable, then try again."
                : "No sweep started. The measurement unit is not answering \xE2\x80\x94 check the cable, then try again.";
            event("saline check: " + m_dipError);
            if (m_dipTraining) { m_matMsg = "Saline check: " + m_dipError; m_matMsgOk = false; }
            dipStop();
        }
        static uint32_t lastSec = 0;
        if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
    }
}

// H7-2.11.2: a saline check that cannot go on (the unit stayed busy) is stopped, not left running
void App::dipStuck()
{
    m_dipError = "No sweep started: the measurement unit stayed busy \xE2\x80\x94 check the cable, then try again.";
    event("saline check: " + m_dipError);
    if (m_dipTraining) { m_matMsg = "Saline check: " + m_dipError; m_matMsgOk = false; }
    dipStop();
}

// H7-2.12.0: the tip out of the saline or shorted: the sweep says nothing about the needle
namespace {
std::string notInSaline(const std::vector<Point>& pts)
{
    std::string why;
    if (matTipFault(pts, kNaN, kTipBridgedRatio, &why) == TipFault::Short) return why;
    int n = 0, cap = 0;
    double c = 0;
    for (const Point& p : pts) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        ++n;
        if (p.phaseDeg <= -75.0 && p.magOhm >= 2000.0) {
            ++cap;
            if (std::fabs(p.freqHz / 500000.0 - 1) < 0.02) c = 1.0 / (2 * kPiD * p.freqHz * p.magOhm);
        }
    }
    if (n >= 10 && cap >= 10)
        return c > 0 ? fmt("the tip is not in the saline: the sweep reads a capacitance alone (%.0f pF at 500 kHz), the needle in "
                           "air.", c * 1e12)
                     : std::string("the tip is not in the saline: the sweep reads a capacitance alone, the needle in air.");
    return std::string();
}
} // namespace

void App::dipNotACheck(const std::string& why)
{
    m_dipError = "Not a check: " + why + " Nothing is counted, for or against the needle: put only its tip, about 10 mm, into "
                 "the 0.9 % saline and press the check again.";
    dipRecord(m_dipTraining ? m_s.trainLot : currentLotId(), false, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, false,
              "not a check: " + why);
    event("saline check: " + m_dipError);
    m_dipSweeps.clear();
    m_dipping = false;
    m_dipStage = DipStage::Idle;
    if (m_dipTraining) {
        m_dipTraining = false;
        MatNeedle nd;
        if (m_cal.calibrated(3)) { nd.cpF = m_cal.ranges().at(3).open.cpFarad; nd.gpS = m_cal.ranges().at(3).open.gpSiemens; }
        m_matRefPts = m_current.points;
        recordRefAttempt(m_current.points, nd, false, "not a check: " + why);
        m_matMsg = "Saline check: " + m_dipError;
        m_matMsgOk = false;
    }
    touch();
}

void App::dipRecord(const std::string& lot, bool validated, double cp, double r25, double ze, double alpha, double rms,
                    double drift, double kZ, bool accepted, const std::string& why)
{
    if (!plat::sdReady()) return;
    const std::string path = m_records + "/saline_check.csv";
    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    if (fresh) f << "when,needle,lot,solution,validated,tempC,condMsCm,cpPf,rOhm25,ze1kOhm,alpha,rms,drift,kZ,accepted,why\n";
    std::string q;
    for (char ch : why) q += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    f << isoUtc(nowMs()) << ',' << (m_dipTraining ? m_s.trainNeedle : m_patient.get("needle_serial")) << ',' << lot << ','
      << dipSolutionName() << ',' << (validated ? "yes" : "no") << ','
      << fmt("%.1f,%.3f,%.2f,%.1f,%.1f,%.3f,%.4f,%.4f,%.4f", m_dipTemp, m_dipCond, cp * 1e12, r25, ze, alpha, rms, drift, kZ)
      << ',' << (accepted ? "yes" : "no") << ",\"" << q << "\"\n";
}

// H7-2.12.0: the main needle's tip surface, with how long it has been in an electrolyte, at each of its checks
// and readings: what its first minutes change in a reading (Z at 1 kHz has the tip's surface in it)
void App::recordTip(const char* what, bool accepted, double r25, double ze, double alpha)
{
    if (!plat::sdReady()) return;
    const std::string path = m_records + "/needle_tip.csv";
    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    if (fresh) f << "when,what,case,needle,s_since_first_wet,s_since_check,check_r25_ohm,check_ze1k_ohm,check_alpha,accepted,"
                    "z1k_ohm,z500k_ohm\n";
    const int64_t now = nowMs();
    const bool reading = std::string(what) != "check";
    double z1k = kNaN, z500 = kNaN;
    if (reading) {
        z1k = m_last.features.z1k;
        for (const Point& p : m_current.points)
            if (p.freqHz > 0 && std::fabs(p.freqHz / 500000.0 - 1) < 0.02) z500 = p.magOhm;
    }
    if (reading && m_dip.ok && m_dip.e.ok) { r25 = m_dip.e.rOhm; ze = m_dip.e.ze1k(); alpha = m_dip.e.alpha; accepted = true; }
    const double wet = m_needleWetMs > 0 ? (now - m_needleWetMs) / 1000.0 : kNaN;
    const double chk = reading ? (m_dip.ok && m_dipAtMs > 0 ? (now - m_dipAtMs) / 1000.0 : kNaN) : 0.0;
    f << isoUtc(now) << ',' << what << ',' << m_patient.caseId << ',' << m_patient.get("needle_serial") << ','
      << fmt("%.0f,%.0f,%.1f,%.1f,%.3f", wet, chk, r25, ze, alpha) << ',' << (accepted ? "yes" : "no") << ','
      << fmt("%.1f,%.1f", z1k, z500) << '\n';
}

void App::finishDipSweep()
{
    {   // H7-2.12.0: the tip out of the saline, or shorted, is no check at all
        const std::string w = notInSaline(m_current.points);
        if (!w.empty()) { dipNotACheck(w); return; }
    }
    if (!m_dipTraining && m_needleWetMs == 0) m_needleWetMs = nowMs();   // H7-2.12.0: the main needle's first sweep in saline
    const std::string lot = m_dipTraining ? m_s.trainLot : currentLotId();
    const LotCorrection* c = m_lotFileLoaded ? m_lotFile.find(lot) : nullptr;
    double cp = kNaN, gp = 0;
    if (m_cal.calibrated(3)) {
        const RangeCal& rc = m_cal.ranges().at(3);
        cp = rc.open.cpFarad; gp = rc.open.gpSiemens;
    }
    Spectrum s = m_current;
    const RangeModel rm = rangeModel();
    instrumentToTrue(&s, &rm);
    deembedAll(&s, cp, gp);
    m_dipSweeps.push_back(s);
    if (int(m_dipSweeps.size()) < kDipSweeps) {
        m_dipStage = DipStage::Between;
        return;
    }
    m_dipping = false;
    // the two sweeps, averaged frequency by frequency
    Spectrum m;
    for (const Point& p : m_dipSweeps.front().points) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        std::complex<double> z(0, 0);
        int n = 0;
        for (const Spectrum& q : m_dipSweeps)
            for (const Point& r : q.points)
                if (std::fabs(r.freqHz / p.freqHz - 1) < 0.02 && r.magOhm > 0 && std::isfinite(r.phaseDeg)) {
                    z += std::polar(r.magOhm, r.phaseDeg * kPiD / 180.0); ++n; break;
                }
        if (!n) continue;
        z /= double(n);
        Point o = p; o.magOhm = std::abs(z); o.phaseDeg = std::arg(z) * 180.0 / kPiD;
        m.points.push_back(o);
    }
    ElectrodeModel e = fitElectrode(m);
    const double z1 = near100k(m_dipSweeps[0]), z2 = near100k(m_dipSweeps.back());
    const double drift = (z1 > 0 && z2 > 0) ? std::fabs(z2 - z1) / (0.5 * (z1 + z2)) : kNaN;
    const int step = dipStep();
    const ElectrodeModel& re = m_lotFile.refFor(step);
    const double r25 = e.ok ? lotNormaliseZ(e.rOhm, step, m_dipTemp, m_dipCond) : kNaN;
    const double ze = e.ze1k();
    // H7-2.10.0: the window — the lot's own QC limits (its good needles in Settings -> Lots, carried in
    // the lot file), else a tip within 30 % of the reference needle's and a surface within a factor of
    // four, else the generic limits; with the tip's exponent and the two sweeps' agreement
    const bool validated = c && c->hasModel() && c->dipValid(step);
    const QcLimits q = qcLimitsFor(lot, step);
    std::string why;
    if (!m_dipTraining && !dipNeeded()) why = "this needle's lot no longer needs a saline check (the lot file or the settings changed)";
    else {
        why = q.salineWhy(r25, ze, e.alpha, e.ok ? e.rms : 1.0, drift);
        // not a tip in saline with the Air check's stray capacitance: is it the Air check, or the needle? (H7-2.6.2)
        if (!why.empty() && (!e.ok || !(e.rms <= q.maxRms)) && std::isfinite(cp)) {
            MatNeedle tmp;
            std::string w2;
            if (!matNeedleRef(m_current.points, cp, gp, &tmp, &w2) && !w2.empty()) why = w2;
        }
    }
    // the record of every attempt
    dipRecord(lot, validated, cp, r25, ze, e.alpha, e.rms, drift, re.ok && r25 > 0 ? re.rOhm / r25 : kNaN, why.empty(), why);
    if (!m_dipTraining) recordTip("check", why.empty(), r25, ze, e.alpha);     // H7-2.12.0: the main needle's tip
    m_dipSweeps.clear();
    const ElectrodeModel eRaw = e;
    if (!why.empty()) {
        m_dipError = "Saline check refused: " + why;
        m_dipStage = DipStage::Idle;
        event(m_dipError);
        if (m_dipTraining) { m_dipTraining = false; trainingCheckDone(false, eRaw, r25, ze, why, q); }
        return;
    }
    e.rOhm = r25;
    m_dip.ok = true;
    m_dip.e = e;
    m_dip.rRaw = eRaw.rOhm;
    m_dip.kZ = re.ok ? re.rOhm / r25 : kNaN;
    m_dip.qcOnly = m_dipTraining || !re.ok || !dipCorrects(lot);
    m_dip.training = m_dipTraining;
    m_dip.step = step;
    m_dip.validated = validated;
    m_dip.cpF = cp;
    m_dip.lotId = lot;
    m_dip.when = isoUtc(nowMs());
    m_dipAtMs = nowMs();
    m_dipError.clear();
    m_dipStage = DipStage::Done;
    event("saline check accepted: " + dipText());
    if (m_dipTraining) { m_dipTraining = false; trainingCheckDone(true, eRaw, r25, ze, std::string(), q); }
}

std::string App::dipText() const
{
    if (!m_dip.ok) return std::string();
    return fmt("%s: R %.0f \xCE\xA9 (25 \xC2\xB0""C) \xC2\xB7 tip surface %.0f \xCE\xA9 at 1 kHz%s%s",
               m_dip.step == kCheck9 ? "0.9 %" : "0.09 %", m_dip.e.rOhm, m_dip.e.ze1k(),
               std::isfinite(m_dip.kZ) ? fmt(" \xC2\xB7 cell ratio %.3f", m_dip.kZ).c_str() : "",
               m_dip.validated ? "" : " \xC2\xB7 lot NOT validated");
}

} // namespace app
