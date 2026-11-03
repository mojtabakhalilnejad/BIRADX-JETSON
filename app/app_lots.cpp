// app/app_lots.cpp — needle lots measured against the reference needle, and
// the lot calibration file every device applies by itself (H7-2.0.0).
//
// Settings -> Lots. The REFERENCE is the needle (with its cable and connector)
// the trial cut-offs were found with; each new lot is sampled (N needles,
// each in air and in up to three saline strengths, R sweeps each) and compared
// with it in the same phantoms. birx::evaluateLot (lotfit.h) says whether the
// lot is the same, usable with a correction, needs different rule ranges, or
// is rejected. An approved lot goes into records/lotcal.txt, which is exported
// to a USB stick and loaded on every device; there, the lot number typed at
// intake picks the lot's correction and every reading is corrected by itself.
//
// H7-2.3.0: each needle as three parts — its stray capacitance (the Air check),
// its electrode surface and its tip size (the needle model, needlemodel.h) — so
// that a lot whose needles differ can be approved "per needle": each needle is
// then checked in saline before its case (app_dip.cpp) and carried to the
// reference needle with its own model. H7-2.4.0: control runs of the reference
// (the same needle measured again) set the repeatability floor under every
// lot's tolerances. H7-2.4.1: a settling time in saline before every sweep, and
// any sweep can be taken out by hand (the others of its cell judged again).
// H7-2.10.0: each lot's QC limits, from its good needles, travel in the lot file.
//
// Files: records/lots/<lot>.txt (each run, every sweep's numbers) and
// records/lotcal.txt (the approved lots). Both are kept by Delete logs.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace birx;

namespace app {

namespace {
const int kLotStartMs = 30000;
const int kLotSweepMs = 25000;

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
std::string readText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
bool writeText(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    f.flush();
    return f.good();
}
std::string safeName(const std::string& id)
{
    std::string s;
    for (char c : id) s.push_back((std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_');
    return s.empty() ? std::string("lot") : s;
}
} // namespace

std::string App::lotPath(const LotRun& r) const
{
    return m_records + "/lots/" + (r.reference ? std::string("REF_") : std::string()) + safeName(r.lotId) + ".txt";
}

void App::loadLots()
{
    m_lotRuns.clear();
    ++m_lotRev;
    m_lotFile = LotFile();
    m_lotFileLoaded = false;
    m_lotInForce = "\x01";            // forces the engine's correction to be chosen again
    if (!plat::sdReady()) return;
    plat::listDir(m_records + "/lots", [&](const std::string& name, bool dir, long) {
        if (dir || name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0) return;
        LotRun r;
        if (r.load(readText(m_records + "/lots/" + name))) m_lotRuns.push_back(r);
    });
    std::sort(m_lotRuns.begin(), m_lotRuns.end(), [](const LotRun& a, const LotRun& b) {
        if (a.reference != b.reference) return a.reference;
        return a.created < b.created;
    });
    const std::string t = readText(m_records + "/lotcal.txt");
    if (!t.empty()) {
        std::string why;
        if (m_lotFile.load(t, &why)) m_lotFileLoaded = true;
        else event("lot calibration file NOT loaded: " + why);
    }
}

bool App::saveLotRun(const LotRun& r)
{
    if (!plat::sdReady()) return false;
    plat::makeDirs(m_records + "/lots");
    return writeText(lotPath(r), r.save());
}

bool App::saveLotFile()
{
    if (!plat::sdReady()) return false;
    m_lotFile.trialUncalibrated = !airCalUsed();      // the file says what its lots were measured for
    const bool ok = writeText(m_records + "/lotcal.txt", m_lotFile.save());
    m_lotFileLoaded = ok || m_lotFileLoaded;
    m_lotInForce = "\x01";
    return ok;
}

int App::lotReferenceIdx() const
{
    for (size_t i = 0; i < m_lotRuns.size(); ++i) if (m_lotRuns[i].reference) return int(i);
    return -1;
}

std::string App::lotAdd(const std::string& id0, bool reference, const LotPlan& plan, bool control)
{
    // H7-2.4.0: a control run is the reference needle measured again, in another session
    if (control) {
        if (reference) return "A control run is not a reference.";
        const int ri = lotReferenceIdx();
        if (ri < 0 || !m_lotRuns[size_t(ri)].complete())
            return "Measure the reference first: the control run is the reference needle measured again.";
    }
    std::string id;
    for (char c : id0) if (c != '|' && c != '\n' && c != '\r') id.push_back(c);
    while (!id.empty() && id.back() == ' ') id.pop_back();
    while (!id.empty() && id.front() == ' ') id.erase(0, 1);
    if (id.empty()) return "Type the lot number.";
    if (!plat::sdReady()) return "There is no records memory to keep the lot's tests in.";
    if (reference && lotReferenceIdx() >= 0) return "There is already a reference. Delete it first to measure a new one.";
    for (const LotRun& r : m_lotRuns)
        if (r.lotId == id && r.reference == reference) return "Lot " + id + " is already in the list.";
    LotRun r;
    r.lotId = id;
    r.reference = reference;
    r.control = control;
    r.created = isoUtc(nowMs());
    r.plan = plan;
    r.plan.needles = std::max(1, std::min(20, plan.needles));
    r.plan.repeats = std::max(1, std::min(10, plan.repeats));
    r.plan.phantoms = std::max(1, std::min(int(lotPhantoms().size()), plan.phantoms));
    if (!reference && lotReferenceIdx() >= 0)
        r.plan.phantoms = std::min(r.plan.phantoms, m_lotRuns[size_t(lotReferenceIdx())].plan.phantoms);
    if (control) r.plan = m_lotRuns[size_t(lotReferenceIdx())].plan;   // the reference's own plan, again
    if (!saveLotRun(r)) return "The lot could NOT be written to the records memory.";
    m_lotRuns.push_back(r);
    ++m_lotRev;
    std::sort(m_lotRuns.begin(), m_lotRuns.end(), [](const LotRun& a, const LotRun& b) {
        if (a.reference != b.reference) return a.reference;
        return a.created < b.created;
    });
    event(fmt("lots: %s %s added (%d needles x %d repeats, air + %d saline)",
              reference ? "reference" : control ? "control run" : "lot",
              id.c_str(), r.plan.needles, r.plan.repeats, r.plan.phantoms));
    touch();
    return std::string();
}

std::string App::lotRemove(int idx)
{
    if (idx < 0 || idx >= int(m_lotRuns.size())) return "No such lot.";
    if (m_lotTesting) return "A test is running.";
    const LotRun r = m_lotRuns[size_t(idx)];
    plat::removeFile(lotPath(r));
    m_lotRuns.erase(m_lotRuns.begin() + idx);
    ++m_lotRev;
    event("lots: " + std::string(r.reference ? "reference " : "lot ") + r.lotId + " deleted (its tests are gone; the lot file is unchanged)");
    touch();
    return std::string();
}

std::string App::lotTestBlocker() const
{
    if (!plat::sdReady()) return "There is no records memory.";
    if (caseOpen()) return "A case is open. End it first: lot tests are not patient readings.";
    if (m_calibrating || m_refTaking || m_dipping)
        return "A needle calibration, saline check or healthy-tissue sample is in progress.";
    if (busy() && !m_lotTesting) return "Wait until the measurement in progress is over.";
    if (!connected()) return "Nothing is arriving from the measurement unit.";
    return std::string();
}

std::string App::lotStartTest(int idx, int needle, int step, double tempC, double condMsCm)
{
    if (idx < 0 || idx >= int(m_lotRuns.size())) return "No such lot.";
    if (m_lotTesting) return "A test is already running.";
    const std::string why = lotTestBlocker();
    if (!why.empty()) return why;
    const LotRun& r = m_lotRuns[size_t(idx)];
    if (needle < 1 || needle > r.plan.needles || step < 0 || step > r.plan.phantoms) return "No such test.";
    if (step > 0 && !r.needleAir(needle, nullptr, nullptr))
        return fmt("Needle %d has no accepted air sweep yet. Air first, with the needle dry.", needle);
    if (step > 0 && !std::isfinite(tempC) && !std::isfinite(condMsCm))
        return "Enter the solution's temperature (or its conductivity) first.";
    m_lotIdx = idx;
    m_lotNeedle = needle;
    m_lotStep = step;
    m_lotTemp = tempC;
    m_lotCond = condMsCm;
    m_lotTesting = true;
    m_lotMsg.clear();
    m_lotMsgOk = true;
    // H7-2.4.1: in a saline, the tip settles first (Settings -> Lots), as before the Saline check
    if (step > 0 && m_s.settleS > 0) {
        m_lotStage = LotStage::Settling;
        m_lotSettleUntil = plat::ticks() + uint32_t(m_s.settleS) * 1000u;
        event(fmt("lots: %s, needle %d, %s, settling %d s", r.lotId.c_str(), needle, lotStepName(step), m_s.settleS));
    } else {
        m_lotStage = LotStage::Waiting;
        m_lotWaitStart = plat::ticks();
        event(fmt("lots: %s, needle %d, %s, sweep started", r.lotId.c_str(), needle, lotStepName(step)));
        startRun();
    }
    touch();
    return std::string();
}

void App::lotStopTest(const std::string& why)
{
    if (!m_lotTesting) return;
    if (m_lotStage == LotStage::Waiting || m_lotStage == LotStage::Sweeping) stopRun();
    m_lotTesting = false;
    m_lotStage = LotStage::Idle;
    if (!why.empty()) { m_lotMsg = why; m_lotMsgOk = false; event("lots: " + why); }
    touch();
}

int App::lotSecondsLeft() const
{
    if (m_lotStage == LotStage::Settling)
        return std::max(0, (int(int32_t(m_lotSettleUntil - plat::ticks())) + 999) / 1000);
    const int limit = (m_lotStage == LotStage::Sweeping) ? kLotSweepMs : kLotStartMs;
    const int waited = int(uint32_t(plat::ticks() - m_lotWaitStart));
    return std::max(0, (limit - waited + 999) / 1000);
}

void App::pollLots(uint32_t now)
{
    if (m_lotTesting && m_lotStage == LotStage::Settling) {
        if (int32_t(now - m_lotSettleUntil) >= 0) {
            if (busy()) m_lotSettleUntil = now + 200;        // the link is not free yet: a moment more
            else {
                m_lotWaitStart = now;
                m_lotStage = LotStage::Waiting;
                event("lots: settled; sweep started");
                startRun();
            }
        }
        static uint32_t lastSettleSec = 0;
        if (now / 1000 != lastSettleSec) { lastSettleSec = now / 1000; touch(); }
    }
    if (m_lotTesting && (m_lotStage == LotStage::Waiting || m_lotStage == LotStage::Sweeping)) {
        const int limit = (m_lotStage == LotStage::Sweeping) ? kLotSweepMs : kLotStartMs;
        if (uint32_t(now - m_lotWaitStart) >= uint32_t(limit))
            lotStopTest(m_lotStage == LotStage::Sweeping
                        ? "The sweep began but never finished. Check the cable, then try again."
                        : "No sweep started. The measurement unit is not answering \xE2\x80\x94 check the cable, then try again.");
        static uint32_t lastSec = 0;
        if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
    }
    // H7-2.4.0: a saline check holds for the solution it was taken in
    if (m_dip.ok && m_dip.step != dipStep()) {
        clearDip("the check solution changed");
        touch();
    }
    // the correction the engine applies follows the lot of the needle in use
    const std::string lot = currentLotId();
    if (lot != m_lotInForce) {
        if (m_dip.ok && m_dip.lotId != lot) clearDip("the needle's lot changed");   // H7-2.3.0: its check was for that lot
        m_lotInForce = lot;
        const LotCorrection* c = m_lotFileLoaded ? m_lotFile.find(lot) : nullptr;
        if (!lot.empty() && m_lotFileLoaded) {
            if (c && c->usable()) event(fmt("lot %s: in the lot calibration file (rev %d); its readings are transferred "
                                            "to the reference needle (cell-constant ratio %.3f)", lot.c_str(), m_lotFile.rev, c->kZ));
            else event("lot " + lot + ": NOT in the lot calibration file (rev " + std::to_string(m_lotFile.rev) +
                       "); readings are not lot-corrected");
        }
        touch();
    }
}

void App::lotSweepStarted()
{
    if (m_lotTesting && m_lotStage == LotStage::Waiting) {
        m_lotStage = LotStage::Sweeping;
        m_lotWaitStart = plat::ticks();
    }
}

namespace {
// H7-2.4.1: one sweep against those of the same needle in the same solution taken before it (the
// first n tests of the run): one needle's repeats must agree, or something moved
std::string repeatWhy(const std::vector<LotTest>& tests, size_t n, const LotTest& t)
{
    std::vector<double> zs, is;
    for (size_t i = 0; i < n && i < tests.size(); ++i) {
        const LotTest& o = tests[i];
        if (!o.ok || o.needle != t.needle || o.step != t.step) continue;
        if (t.step == 0) { if (std::isfinite(o.cpF)) zs.push_back(o.cpF); continue; }
        if (std::isfinite(o.zNorm)) zs.push_back(o.zNorm);
        if (std::isfinite(o.ips)) is.push_back(o.ips);
    }
    double zm = 0;
    for (double z : zs) zm += z;
    if (!zs.empty()) zm /= double(zs.size());
    if (t.step == 0) {
        // the air sweeps: a cable that moved, a hand near the tip
        if (!zs.empty() && std::fabs(t.cpF - zm) > 3e-12)
            return fmt("Cp %.1f pF, %.1f pF from this needle's earlier air sweeps (%.1f pF): the cable or a hand "
                       "moved. Keep it as it lay and repeat.", t.cpF * 1e12, std::fabs(t.cpF - zm) * 1e12, zm * 1e12);
        return std::string();
    }
    double im = 0;
    for (double x : is) im += x;
    if (!is.empty()) im /= double(is.size());
    if (!zs.empty() && std::fabs(t.zNorm / zm - 1) > 0.05)
        return fmt("Z %.0f ohm, %.1f %% from this needle's earlier sweeps in this solution: the depth or the "
                   "solution changed. Hold the tip at the same depth and repeat.", t.zNorm, std::fabs(t.zNorm / zm - 1) * 100);
    if (!is.empty() && std::fabs(t.ips - im) > 1.0)
        return fmt("IPS %+.2f, %.2f from this needle's earlier sweeps in this solution: the tip moved or "
                   "touched the cup. Hold it still, clear of the sides, and repeat.", t.ips, std::fabs(t.ips - im));
    return std::string();
}
} // namespace

// H7-2.4.1: a sweep taken out of the run by hand (kept in the file, never counted), or put back as it
// was; the later sweeps of its needle in its solution, judged against the earlier ones, are judged again
std::string App::lotSetRemoved(int idx, int test, bool removed)
{
    if (idx < 0 || idx >= int(m_lotRuns.size())) return "No such lot.";
    if (m_lotTesting) return "A test is running.";
    LotRun& r = m_lotRuns[size_t(idx)];
    if (test < 0 || test >= int(r.tests.size())) return "No such sweep.";
    LotTest& t = r.tests[size_t(test)];
    if (t.removed == removed) return std::string();
    if (removed) { t.removedOk = t.ok; t.ok = false; }
    else t.ok = t.removedOk;
    t.removed = removed;
    int changed = 0;
    for (size_t i = 0; i < r.tests.size(); ++i) {
        LotTest& o = r.tests[i];
        if (o.removed || o.needle != t.needle || o.step != t.step) continue;
        if (!o.ok && o.why.find("this needle's earlier") == std::string::npos) continue;   // refused for another reason
        const std::string w = repeatWhy(r.tests, i, o);
        if (o.ok != w.empty()) ++changed;
        o.ok = w.empty();
        o.why = w;
    }
    if (changed)
        event(fmt("lots: %s, needle %d, %s: %d sweep(s) judged again after the change", r.lotId.c_str(), t.needle,
                  lotStepName(t.step), changed));
    ++m_lotRev;
    if (!saveLotRun(r)) return "The lot could NOT be written to the records memory.";
    event(fmt("lots: %s, needle %d, %s, sweep %d %s by hand (it had been %s)", r.lotId.c_str(), t.needle, lotStepName(t.step),
              t.repeat, removed ? "REMOVED" : "restored", t.removedOk ? "accepted" : "refused"));
    touch();
    return std::string();
}

void App::finishLotSweep()
{
    m_lotTesting = false;
    m_lotStage = LotStage::Idle;
    if (m_lotIdx < 0 || m_lotIdx >= int(m_lotRuns.size())) return;
    LotRun& r = m_lotRuns[size_t(m_lotIdx)];
    LotTest t;
    t.needle = m_lotNeedle;
    t.step = m_lotStep;
    // H7-2.4.2: every sweep of a cell its own number, removed ones included
    t.repeat = 1;
    for (const LotTest& o : r.tests) if (o.needle == t.needle && o.step == t.step) ++t.repeat;
    t.when = isoUtc(nowMs());
    t.soakS = m_lotStep > 0 ? m_s.settleS : 0;
    Spectrum s = m_current;
    if (m_lotStep == 0) {
        // AIR: the same fit as the six-second calibration, on gain range 3
        for (Point& p : s.points) if (p.range <= 0) p.range = 3;
        NeedleCalibration nc;
        std::string why;
        if (nc.addOpenSweep(3, s, {}, &why) && nc.ranges().count(3)) {
            const RangeCal& rc = nc.ranges().at(3);
            t.ok = true; t.cpF = rc.open.cpFarad; t.gpS = rc.open.gpSiemens;
            // one needle's repeats must agree: a cable that moved, a hand near the tip
            t.why = repeatWhy(r.tests, r.tests.size(), t);
            if (!t.why.empty()) t.ok = false;
        } else {
            t.why = why.empty() ? std::string("this sweep does not look like an open needle") : why;
        }
        m_lotMsg = t.ok ? fmt("Air, needle %d: Cp %.1f pF \xE2\x9C\x93", t.needle, t.cpF * 1e12)
                        : "Air sweep refused: " + t.why;
    } else {
        // SALINE: this needle's own air correction, as a patient reading would get, then Z and IPS
        t.raw = s.points;
        double cp = kNaN, gp = 0;
        r.needleAir(m_lotNeedle, &cp, &gp);
        lotPlain(&s, cp, gp, airCalUsed());
        const Features fe = m_engine.basicFeatures(s);
        t.z1k = fe.z1k;
        t.ips = fe.ipsLegacy;
        t.ipsFit = fe.ipsFit;
        t.tempC = m_lotTemp;
        t.condMsCm = m_lotCond;
        t.zNorm = lotNormaliseZ(t.z1k, t.step, t.tempC, t.condMsCm);
        if (!std::isfinite(t.z1k) || !std::isfinite(t.ips))
            t.why = "the sweep gave no impedance or no IPS";
        else if (t.z1k < 5 || t.z1k > 50000)
            t.why = fmt("Z %.0f ohm is not what a needle in saline reads: is the tip under the surface?", t.z1k);
        else {
            // and the needle's repeats in this solution must agree
            t.why = repeatWhy(r.tests, r.tests.size(), t);
            t.ok = t.why.empty();
        }
        m_lotMsg = t.ok ? fmt("%s, needle %d: Z %.0f ohm (%.0f at 25 \xC2\xB0""C), IPS %+.2f \xE2\x9C\x93", lotStepName(t.step),
                              t.needle, t.z1k, t.zNorm, t.ips)
                        : std::string(lotStepName(t.step)) + " sweep refused: " + t.why;
    }
    m_lotMsgOk = t.ok;
    r.tests.push_back(t);
    // H7-2.5.0: a needle refused too often is set aside, and the plan goes on without it
    if (!t.ok && r.needleGivenUp(t.needle))
        m_lotMsg += fmt(" Needle %d has now been refused %d times here: it is set aside as faulty, the plan goes on "
                        "without it, and the result says what was wrong with it.", t.needle, LotRun::kGiveUp);
    ++m_lotRev;
    if (!saveLotRun(r)) storageFailed("The lot test could NOT be written to records/lots \xE2\x80\x94 check the records memory.");
    event("lots: " + r.lotId + ", " + m_lotMsg);
    if (r.complete()) {
        const LotResult res = lotResult(m_lotIdx);
        event("lots: " + r.lotId + " complete: " + lotVerdictName(res.verdict) + " \xE2\x80\x94 " + res.headline);
    }
}

namespace {
Features engineFeatures(const Spectrum& s, void* ctx)
{
    return static_cast<const DecisionEngine*>(ctx)->basicFeatures(s);
}
} // namespace

// H7-2.3.0: the device's gain-range phase offsets for the needle model. The measurement unit
// corrects its own ranges; none are added here.
RangeModel App::rangeModel() const
{
    return RangeModel();
}

// H7-2.4.0: the tolerances, with the reference's own repeatability (a control run) as their floor
LotLimits App::lotLimits() const
{
    return LotLimits().withFloor(m_s.floorIps, m_s.floorZ, m_s.floorDrift, m_s.floorLeak);
}

// Evaluated once per change of the lots, not once per screen drawn
// (H7-2.1.1: with a complete reference the Lots tab evaluated every run on
// every frame, several seconds a frame on the board, and the tab froze).
LotResult App::lotResult(int idx) const
{
    if (idx < 0 || idx >= int(m_lotRuns.size())) return LotResult();
    const bool air = airCalUsed();
    if (m_lotCacheRev != m_lotRev || m_lotCacheAir != air || m_lotCache.size() != m_lotRuns.size()) {
        m_lotCache.assign(m_lotRuns.size(), LotResult());
        m_lotCacheOk.assign(m_lotRuns.size(), false);
        m_lotCacheRev = m_lotRev;
        m_lotCacheAir = air;
    }
    if (m_lotCacheOk[size_t(idx)]) return m_lotCache[size_t(idx)];
    m_lotCache[size_t(idx)] = evaluateNow(idx);
    m_lotCacheOk[size_t(idx)] = true;
    return m_lotCache[size_t(idx)];
}

LotCompare App::lotCompare(int idx) const
{
    const int ri = lotReferenceIdx();
    if (idx < 0 || idx >= int(m_lotRuns.size()) || ri < 0 || idx == ri) return LotCompare();
    if (m_cmpIdx == idx && m_cmpRev == m_lotRev && m_cmpAir == airCalUsed()) return m_cmp;
    const LotResult res = lotResult(idx);
    LotPipeline pipe;
    pipe.trialCalibrated = airCalUsed();
    const RangeModel rm = rangeModel();
    pipe.ranges = &rm;
    // H7-2.3.0: each needle transferred as the verdict says (the lot's model, or its own)
    m_cmp = compareLot(m_lotRuns[size_t(idx)], m_lotRuns[size_t(ri)], pipe, engineFeatures,
                       const_cast<DecisionEngine*>(&m_engine), std::isfinite(res.kZMean) ? res.kZMean : 1.0, &res);
    m_cmpIdx = idx; m_cmpRev = m_lotRev; m_cmpAir = airCalUsed();
    return m_cmp;
}

LotResult App::evaluateNow(int idx) const
{
    LotPipeline pipe;
    pipe.trialCalibrated = airCalUsed();
    const int ri = lotReferenceIdx();
    const RangeModel rm = rangeModel();
    pipe.ranges = &rm;
    return evaluateLot(m_lotRuns[size_t(idx)], ri >= 0 ? &m_lotRuns[size_t(ri)] : nullptr, pipe,
                       engineFeatures, const_cast<DecisionEngine*>(&m_engine), lotLimits());
}

// H7-2.4.0: a complete control run sets the floor under every lot's tolerances: no lot is judged
// tighter than the reference needle repeats by itself. A floor is never lowered by a later control.
std::string App::lotUseAsFloor(int idx)
{
    if (idx < 0 || idx >= int(m_lotRuns.size()) || !m_lotRuns[size_t(idx)].control) return "That is not a control run.";
    const LotRun& r = m_lotRuns[size_t(idx)];
    const LotResult res = lotResult(idx);
    if (!r.complete() || !std::isfinite(res.ctrlIps) || !std::isfinite(res.ctrlZ)) return "The control run is not complete.";
    // H7-2.4.4: beyond what the same needle can differ from itself it is a change, not a repeat
    if (res.ctrlIps > LotLimits::kFloorIpsMax || res.ctrlZ > LotLimits::kFloorZMax ||
        res.ctrlDrift > LotLimits::kFloorDriftMax || res.ctrlLeak > LotLimits::kFloorLeakMax)
        return fmt("Not used: the control differs from the reference by %.2f IPS and %.1f %% Z (drift %.1f %%, leak ratio "
                   "%.2f). The same needle does not differ that much from itself (at most %.2f IPS, %.0f %% Z): something "
                   "changed between the two sessions. Check the solutions, their temperature, the cable and the needle, "
                   "delete this control and measure it again.", res.ctrlIps, res.ctrlZ * 100, res.ctrlDrift * 100, res.ctrlLeak,
                   LotLimits::kFloorIpsMax, LotLimits::kFloorZMax * 100);
    Settings n = m_s;
    n.floorIps = std::isfinite(m_s.floorIps) ? std::max(m_s.floorIps, res.ctrlIps) : res.ctrlIps;
    n.floorZ = std::isfinite(m_s.floorZ) ? std::max(m_s.floorZ, res.ctrlZ) : res.ctrlZ;
    n.floorDrift = std::isfinite(m_s.floorDrift) ? std::max(m_s.floorDrift, res.ctrlDrift) : res.ctrlDrift;
    n.floorLeak = std::isfinite(m_s.floorLeak) ? std::max(m_s.floorLeak, res.ctrlLeak) : res.ctrlLeak;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    ++m_lotRev;
    event(fmt("lots: repeatability floor from control %s: IPS %.2f, Z %.1f %%, drift %.1f %%, leak %.2f", r.lotId.c_str(),
              n.floorIps, n.floorZ * 100, n.floorDrift * 100, n.floorLeak));
    touch();
    return fmt("Every lot is now judged with tolerances no tighter than the reference's own repeatability: IPS %.2f, "
               "Z %.1f %%, drift %.1f %%. The results are worked out again.", n.floorIps, n.floorZ * 100, n.floorDrift * 100);
}

std::string App::lotClearFloor()
{
    Settings n = m_s;
    n.floorIps = n.floorZ = n.floorDrift = n.floorLeak = kNaN;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    ++m_lotRev;
    event("lots: repeatability floor cleared; the default tolerances apply");
    touch();
    return "The default tolerances apply again.";
}

// H7-2.4.1: the time the tip settles in a saline before every lot sweep and before the Saline check
std::string App::setSettleSeconds(int sec)
{
    Settings n = m_s;
    n.settleS = std::max(0, std::min(60, sec));
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event(fmt("settling time in saline set to %d s", n.settleS));
    return std::string();
}

std::vector<LotRange> App::lotEquivalentRanges(int idx) const
{
    std::vector<LotRange> boxes;
    const LotResult res = lotResult(idx);
    if (res.pts.empty()) return boxes;
    LotCorrection c;
    c.pts = res.pts;
    for (const RuleRowCfg& w : m_s.rules.rows) {
        if (!w.enabled) continue;
        LotRange b;
        b.label = w.label;
        b.zMin = w.zMin; b.zMax = w.zMax; b.ipsMin = w.ipsMin; b.ipsMax = w.ipsMax;
        b.lzMin = b.lzMax = b.lipsMin = b.lipsMax = kNaN;
        boxes.push_back(b);
    }
    return lotRanges(c, boxes);
}

std::string App::lotApprove(int idx)
{
    if (idx < 0 || idx >= int(m_lotRuns.size())) return "No such lot.";
    const LotRun& r = m_lotRuns[size_t(idx)];
    const LotResult res = lotResult(idx);
    if (r.control)
        return "A control run is the reference needle itself: it does not go into the file. Use it as the repeatability "
               "floor instead.";
    if (!r.reference && res.verdict != LotVerdict2::SameAsReference && res.verdict != LotVerdict2::UsableWithCalibration &&
        res.verdict != LotVerdict2::UsableWithPerNeedle)
        return std::string("Only a lot that is the same as the reference or usable with calibration can go into the "
                           "file. This one: ") + lotVerdictName(res.verdict) + ".";
    if (r.reference && !r.complete()) return "The reference's tests are not complete.";
    LotCorrection c;
    c.lotId = r.lotId;
    c.verdict = r.reference ? std::string("Reference") : std::string(lotVerdictName(res.verdict));
    c.date = isoUtc(nowMs()).substr(0, 10);
    c.needles = r.plan.needles;
    c.cpF = res.cpMeanF;
    c.gpS = res.gpMeanS;
    c.pts = res.pts;
    c.kZ = r.reference ? 1.0 : res.kZMean;
    c.residIps = r.reference ? 0.0 : res.worstResidIps;
    c.residZ = r.reference ? 0.0 : res.worstResidZ;
    const int ri = lotReferenceIdx();
    const RangeModel rm = rangeModel();
    const RefModel mr = ri >= 0 ? refModel(m_lotRuns[size_t(ri)], &rm) : RefModel();
    if (!r.reference && res.model) {
        // H7-2.3.0: the needle model — the lot's (one correction) or each needle's own (per needle)
        c.mode = res.mode;
        c.e = res.eLot;
        c.cpF = res.lotCpF;
        c.gpS = res.lotGpS;
        c.kZ = res.kZLot;
        c.residIps = res.mode == 1 ? res.needleResidIps : res.lotResidIps;
        c.residZ = res.mode == 1 ? res.needleResidZ : res.lotResidZ;
        c.dipRMin = res.dipRMin; c.dipRMax = res.dipRMax; c.dipZeMin = res.dipZeMin; c.dipZeMax = res.dipZeMax;
        c.dipMask = (res.valid09 ? 1 : 0) | (res.valid9 ? 2 : 0);
        c.dip9RMin = res.dip9RMin; c.dip9RMax = res.dip9RMax; c.dip9ZeMin = res.dip9ZeMin; c.dip9ZeMax = res.dip9ZeMax;
        c.needles = res.goodNeedles;
        lotQcFill(r, &res, lotLimits(), &c);            // H7-2.10.0: the QC every needle of the lot must pass
    } else if (r.reference && mr.ok) {
        c.e = mr.e;                       // the reference's own needles: nothing to change
        c.mode = 0;
        // its saline check window: its tip within the larger of 15 % and 1.5 x the lots' window, its
        // surface as for a lot
        const LotLimits lim = lotLimits();
        const double w = std::max(0.15, lim.dipRWin * 1.5);
        c.dipRMin = mr.e.rOhm * (1 - w); c.dipRMax = mr.e.rOhm * (1 + w);
        c.dipZeMin = mr.e.ze1k() * lim.dipZeLo; c.dipZeMax = mr.e.ze1k() * lim.dipZeHi;
        c.dipMask = 1;
        if (mr.e9.ok) {
            c.dip9RMin = mr.e9.rOhm * (1 - w); c.dip9RMax = mr.e9.rOhm * (1 + w);
            c.dip9ZeMin = mr.e9.ze1k() * lim.dipZeLo; c.dip9ZeMax = mr.e9.ze1k() * lim.dipZeHi;
            c.dipMask |= 2;
        }
        // H7-2.10.0: its needles' QC — their air parasitic, and the reference tip's exponent in each saline
        lotQcFill(r, nullptr, lim, &c);
        if (mr.e.ok) { c.qcAlphaMin = mr.e.alpha - lim.qcAlphaWin; c.qcAlphaMax = std::min(1.0, mr.e.alpha + lim.qcAlphaWin); }
        if (mr.e9.ok) { c.qcAlpha9Min = mr.e9.alpha - lim.qcAlphaWin; c.qcAlpha9Max = std::min(1.0, mr.e9.alpha + lim.qcAlphaWin); }
    }
    if (!c.hasQc()) lotQcFill(r, r.reference ? nullptr : &res, lotLimits(), &c);   // a lot without the needle model: its air at least
    m_lotFile.put(c);
    if (ri >= 0) {
        const LotResult rr = lotResult(ri);
        m_lotFile.refLot = m_lotRuns[size_t(ri)].lotId;
        m_lotFile.refCpF = rr.cpMeanF;
        m_lotFile.refGpS = rr.gpMeanS;
        if (mr.ok) { m_lotFile.refE = mr.e; m_lotFile.refE9 = mr.e9; }   // H7-2.3.0 / 2.4.0: the reference needle's model
    }
    ++m_lotFile.rev;
    m_lotFile.issued = isoUtc(nowMs());
    m_lotFile.device = m_s.deviceId;
    if (!saveLotFile()) return "The lot calibration file could NOT be written \xE2\x80\x94 check the records memory.";
    event(fmt("lots: %s approved into the lot calibration file (rev %d, %s)", r.lotId.c_str(), m_lotFile.rev, c.verdict.c_str()));
    touch();
    return fmt("Lot %s is in the lot calibration file, rev %d. Export it to a USB stick and load it on every device.",
               r.lotId.c_str(), m_lotFile.rev);
}

std::string App::setRulesAirCal(bool on)
{
    if (m_s.rulesAirCal == on) return std::string();
    if (caseOpen()) return "A case is open. End it first: its readings must all be taken the same way.";
    Settings n = m_s;
    n.rulesAirCal = on;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event(std::string("rules: the rule ranges are marked as found with ") +
          (on ? "AIR-CALIBRATED needles: the six-second air calibration is used on every reading"
              : "needles NOT calibrated in air: readings are taken as they come, as in the trial; "
                "the air calibration is not asked for and not applied"));
    if (m_lotFileLoaded) saveLotFile();
    touch();
    return std::string();
}

std::string App::lotExport()
{
    if (!plat::usbReady()) return "No USB stick is mounted (" + plat::usbStatus() + "). Plug one in and press this again.";
    if (!m_lotFileLoaded) return "There is no lot calibration file yet: approve a lot first.";
    const std::string dev = m_s.deviceId.empty() ? std::string("NODEV") : m_s.deviceId;
    const std::string name = "BIRADx_lotcal_" + dev + "_rev" + std::to_string(m_lotFile.rev) + ".txt";
    if (!writeText(plat::usbRoot() + "/" + name, m_lotFile.save()))
        return "The file could NOT be written to the USB stick.";
    // and a report a person can read: every lot's numbers and verdict
    std::ostringstream o;
    o << "BIRADx lot calibration report, " << isoUtc(nowMs()) << ", device " << dev << ", file rev " << m_lotFile.rev << "\n"
      << "Trial cut-offs found with " << (m_lotFile.trialUncalibrated ? "UNcalibrated" : "air-calibrated") << " needles.\n\n";
    for (size_t i = 0; i < m_lotRuns.size(); ++i) {
        const LotRun& r = m_lotRuns[i];
        const LotResult res = lotResult(int(i));
        o << (r.reference ? "REFERENCE " : "LOT ") << r.lotId << " — " << lotVerdictName(res.verdict) << "\n  "
          << res.headline << "\n";
        for (const std::string& f : res.findings) o << "  - " << f << "\n";
        o << "  in the file: " << (m_lotFile.find(r.lotId) ? "yes" : "no") << "\n\n";
    }
    const std::string rep = "BIRADx_lotcal_" + dev + "_rev" + std::to_string(m_lotFile.rev) + "_report.txt";
    writeText(plat::usbRoot() + "/" + rep, o.str());
    plat::flushAll();
    event("lots: exported " + name + " and " + rep);
    return "Written to the USB stick: " + name + " (load it on every device: Settings \xE2\x86\x92 Lots \xE2\x86\x92 Load from USB) "
           "and " + rep + " \xE2\x80\x94 it is safe to remove the stick.";
}

std::string App::lotImport()
{
    if (!plat::usbReady()) return "No USB stick is mounted (" + plat::usbStatus() + "). Plug one in and press this again.";
    if (!plat::sdReady()) return "There is no records memory to keep the file in.";
    std::string best;
    int bestRev = -1;
    LotFile bestFile;
    std::string lastWhy;
    plat::listDir(plat::usbRoot(), [&](const std::string& name, bool dir, long) {
        if (dir || name.compare(0, 14, "BIRADx_lotcal_") != 0 || name.find("_report") != std::string::npos) return;
        LotFile f;
        std::string why;
        if (!f.load(readText(plat::usbRoot() + "/" + name), &why)) { lastWhy = name + ": " + why; return; }
        if (f.rev > bestRev) { bestRev = f.rev; best = name; bestFile = f; }
    });
    if (best.empty())
        return lastWhy.empty() ? std::string("No lot calibration file (BIRADx_lotcal_….txt) is on the USB stick.")
                               : "No usable lot calibration file on the stick. " + lastWhy;
    if (m_lotFileLoaded && bestFile.rev < m_lotFile.rev && bestFile.device == m_lotFile.device)
        return fmt("The file on the stick is rev %d, older than the one in force (rev %d). Nothing was changed.",
                   bestFile.rev, m_lotFile.rev);
    m_lotFile = bestFile;
    if (!saveLotFile()) return "The file could NOT be written to the records memory.";
    m_lotFileLoaded = true;
    event(fmt("lots: lot calibration file loaded from the USB stick: %s (rev %d, %d lots)", best.c_str(),
              m_lotFile.rev, int(m_lotFile.lots.size())));
    rejudgeCase();
    touch();
    std::string warn;
    if (m_lotFile.trialUncalibrated == airCalUsed())
        warn = std::string(" NOTE: the file was made on a device whose rules were found ") +
               (m_lotFile.trialUncalibrated ? "WITHOUT" : "WITH") + " air calibration, and this one's Settings \xE2\x86\x92 "
               "Rules say the opposite. Check which is right before measuring.";
    return fmt("Loaded %s: rev %d, %d lots, from device %s. A lot typed at intake that is in it is corrected by itself.",
               best.c_str(), m_lotFile.rev, int(m_lotFile.lots.size()), m_lotFile.device.c_str()) + warn;
}

// The lot of the needle in use: the open case's, else the one last typed.
std::string App::currentLotId() const
{
    const std::string l = m_patient.get("needle_lot");
    return l.empty() ? m_rememberedLot : l;
}

std::string App::lotStatus() const
{
    const std::string lot = currentLotId();
    if (!m_lotFileLoaded || lot.empty()) return std::string();
    const LotCorrection* c = m_lotFile.find(lot);
    // H7-2.3.0: the needle's own model, when its saline check carries its readings
    if (m_dip.ok && !m_dip.qcOnly && m_dip.lotId == lot)
        return "lot " + lot + (m_dip.validated ? " corrected, this needle's own model" : " corrected by its own model, lot NOT validated");
    if (!c) return "lot " + lot + " not in the lot file";
    if (!c->usable()) return "lot " + lot + " in the lot file";
    if (c->hasModel() && c->mode == 1) return "lot " + lot + " corrected, needle by needle";
    return "lot " + lot + " corrected";
}

// H7-2.4.4: the lots a needle can be of at intake: the lot file's, the reference's first
std::vector<std::string> App::lotChoices() const
{
    std::vector<std::string> v;
    if (!m_lotFileLoaded) return v;
    if (!m_lotFile.refLot.empty() && m_lotFile.find(m_lotFile.refLot)) v.push_back(m_lotFile.refLot);
    for (const LotCorrection& c : m_lotFile.lots)
        if (c.lotId != m_lotFile.refLot) v.push_back(c.lotId);
    return v;
}

// H7-2.4.4: what the device will do with a needle of this lot, in words (at intake)
std::string App::lotPlanText(const std::string& lot) const
{
    if (!m_lotFileLoaded)
        return "No lot calibration file on this device: the needle's readings are taken as they come" +
               std::string(airCalUsed() ? ", with its air calibration." : ".");
    if (lot.empty()) return "Choose the lot printed on the box: the device then knows how to calibrate this needle.";
    const LotCorrection* c = m_lotFile.find(lot);
    const std::string sol = dipSolutionName();
    const std::string check = fmt("Before the case: Air check (6 s in air), then Saline check (the tip in the %s saline, %d s "
                                  "settling and two sweeps). Every reading is then carried to the reference needle's with this "
                                  "needle's own stray capacitance, tip surface and tip size.",
                                  sol.c_str(), m_s.settleS);
    if (!c) {
        if (m_s.reqLotFile)
            return "Lot " + lot + " is NOT in the lot file (rev " + std::to_string(m_lotFile.rev) +
                   "): no reading can be taken with it (Settings \xE2\x86\x92 Needle requires the lot file).";
        if (dipNeededFor(lot))
            return "Lot " + lot + " is NOT in the lot file: it has never been measured against the reference. " + check +
                   " It is checked against generic limits only, and its readings are marked NOT validated.";
        return "Lot " + lot + " is NOT in the lot file: its readings are taken as they come, not corrected.";
    }
    const std::string head = "Lot " + lot + " (" + c->verdict + ", lot file rev " + std::to_string(m_lotFile.rev) + "): ";
    if (lot == m_lotFile.refLot && !dipNeededFor(lot)) return head + "the reference itself: nothing to correct.";
    if (dipNeededFor(lot)) {
        std::string t = head + check;
        if (c->hasModel() && (c->dipMask & (dipStep() == kCheck9 ? 2 : 1)) == 0)
            t += " This lot was NOT validated with the " + sol + " check: choose the other one in Settings \xE2\x86\x92 Needle.";
        return t;
    }
    if (c->hasModel())
        return head + fmt("one correction for the whole lot, applied by itself: stray capacitance %.1f pF, tip surface %.0f ohm "
                          "at 1 kHz and cell ratio %.3f are replaced by the reference needle's. An Air check makes it this "
                          "needle's own stray capacitance.", c->cpF * 1e12, c->e.ze1k(), c->kZ);
    return head + fmt("one correction for the whole lot, applied by itself (cell ratio %.3f, stray capacitance %.1f pF).",
                      c->kZ, c->cpF * 1e12);
}

bool App::lotCovered() const
{
    return m_lotFileLoaded && m_lotFile.find(currentLotId()) != nullptr;
}

// What every sweep of a patient (and a healthy-tissue sample) goes through
// before it is judged: this needle's own air calibration, or — when it has none
// — its lot's mean; and, when the trial cut-offs were found with needles that
// were NOT air-calibrated, the reference needle's parasitic put back, so that
// the reading is what the reference needle would have read.
// H7-2.3.0: with the needle model, the needle's interface is taken off and the
// reference's put back: the needle's own (its saline check) when its lot asks
// for it, else the lot's.
void App::correctForNeedle(Spectrum* s) const
{
    const LotCorrection* c = m_lotFileLoaded ? m_lotFile.find(currentLotId()) : nullptr;
    const RangeModel rm = rangeModel();
    if (m_dip.ok && !m_dip.qcOnly && m_lotFileLoaded && m_dip.lotId == currentLotId()) {
        const ElectrodeModel& re = m_lotFile.refFor(m_dip.step);
        if (re.ok && std::isfinite(m_lotFile.refCpF) && std::isfinite(m_dip.cpF)) {
            NeedleTransfer t;
            t.cpNeedleF = m_dip.cpF;
            if (m_cal.rangeCount() > 0 && m_cal.calibrated(3)) {
                const RangeCal& rc = m_cal.ranges().at(3);
                t.cpNeedleF = rc.open.cpFarad; t.gpNeedleS = rc.open.gpSiemens;
            }
            t.eNeedle = m_dip.e;
            t.kZ = m_dip.kZ;
            t.cpRefF = m_lotFile.refCpF;
            t.gpRefS = std::isfinite(m_lotFile.refGpS) ? m_lotFile.refGpS : 0.0;
            t.eRef = re;
            t.trialCalibrated = airCalUsed();
            t.ranges = &rm;
            needleTransfer(s, t);
            return;
        }
    }
    if (c && c->usable() && std::isfinite(m_lotFile.refCpF) && c->hasModel() && m_lotFile.refE.ok) {
        NeedleTransfer t;
        if (m_cal.rangeCount() > 0 && m_cal.calibrated(3)) {       // this needle's own, measured now
            const RangeCal& rc = m_cal.ranges().at(3);
            t.cpNeedleF = rc.open.cpFarad; t.gpNeedleS = rc.open.gpSiemens;
        } else {                                                   // else its lot's mean
            t.cpNeedleF = c->cpF; t.gpNeedleS = std::isfinite(c->gpS) ? c->gpS : 0.0;
        }
        t.eNeedle = c->e;
        t.kZ = c->kZ;
        t.cpRefF = m_lotFile.refCpF;
        t.gpRefS = std::isfinite(m_lotFile.refGpS) ? m_lotFile.refGpS : 0.0;
        t.eRef = m_lotFile.refE;
        t.trialCalibrated = airCalUsed();
        t.ranges = &rm;
        if (std::isfinite(t.cpNeedleF)) { needleTransfer(s, t); return; }
    }
    if (c && c->usable() && std::isfinite(m_lotFile.refCpF)) {
        LotTransfer t;
        if (m_cal.rangeCount() > 0 && m_cal.calibrated(3)) {
            const RangeCal& rc = m_cal.ranges().at(3);
            t.cpNeedleF = rc.open.cpFarad; t.gpNeedleS = rc.open.gpSiemens;
        } else {
            t.cpNeedleF = c->cpF; t.gpNeedleS = std::isfinite(c->gpS) ? c->gpS : 0.0;
        }
        t.kZ = c->kZ;
        t.cpRefF = m_lotFile.refCpF;
        t.gpRefS = std::isfinite(m_lotFile.refGpS) ? m_lotFile.refGpS : 0.0;
        t.trialCalibrated = airCalUsed();
        if (std::isfinite(t.cpNeedleF)) { lotTransfer(s, t); return; }
    }
    // no lot correction: this needle's own air calibration, when the rules were found with it
    if (airCalUsed() && m_cal.rangeCount() > 0) m_cal.correct(s);
}

} // namespace app
