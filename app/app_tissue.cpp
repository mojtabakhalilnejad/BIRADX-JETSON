// app/app_tissue.cpp — the healthy-tissue sample of this patient (H7-1.3.0).
//
// One sweep with the needle tip in normal tissue, on the way in, BEFORE the
// lesion is entered. It is the patient's own baseline: what this needle reads
// in this breast where there is no lesion.
//
//   - It is NOT a reading of the lesion. It is not judged by the rule table,
//     not counted in "Reading n of 5", not part of the case answer, and it
//     makes no sound of its own.
//   - Its 22 measured quantities are kept with the case and shown in DETAILS
//     beside those of every reading of the lesion.
//   - It is recorded in records/tissue_reference.csv (one line per sample: the
//     case id, the time, the needle, the quality flags, the 22 quantities and
//     the measurement unit's own IPS line) and its sweep in
//     records/tissue_sweeps/. dataset.csv is not touched, so every tool that
//     reads the Jetson's records reads these the same way.
//   - SEVERAL can be taken for one case (H7-1.3.1): each is kept and numbered
//     H1, H2, …; none replaces another. Where one number stands for them all —
//     the HEALTHY column of DETAILS by default — it is each quantity's MEDIAN,
//     as the case answer is the median of its readings. A sample that went
//     wrong can be removed (the last one); the record keeps its line and adds
//     one that says "removed".
//
// BIRADx 2.20.0 on the Jetson has no such step (docs/DIFFERENCES.md).
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace birx;

namespace app {

namespace {
const int kRefStartMs = 30000;     // as the needle calibration: the unit may take its time to begin
const int kRefSweepMs = 25000;

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
std::string cell(double v)
{
    if (!std::isfinite(v)) return std::string();
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
std::string quote(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char ch : s) { if (ch == '"') o += '"'; o += ch; }
    return o + "\"";
}
// one line of comma-separated text, quotes honoured
std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (q) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
            else if (ch == '"') q = false;
            else cur += ch;
        } else if (ch == '"') q = true;
        else if (ch == ',') { out.push_back(cur); cur.clear(); }
        else if (ch != '\r') cur += ch;
    }
    out.push_back(cur);
    return out;
}
} // namespace

std::string App::refBlocker() const
{
    if (!connected())
        return "No link to the measurement unit. The sample is a real sweep and needs the "
               "measurement unit to answer \xE2\x80\x94 check its cable and power.";
    if (m_dipping)
        return "A saline check is running. Wait for it to finish.";
    if (m_calibrating)
        return "The needle is being calibrated. Finish that first.";
    if (!caseOpen())
        return "No case is open. The sample belongs to a patient: press Start case first.";
    if (m_s.reqNeedleCal && airCalUsed() && !needleReady())
        return "This needle has not been calibrated. Calibrate it in air first, so that the "
               "sample is corrected like every reading after it.";
    if (!qcBlocker().empty())
        return "QC: " + qcBlocker() + ". Check it first (on the needle line above): the sample is taken only with a "
               "needle that has passed its checks.";
    if ((m_running || m_inSweep || m_discard) && !m_refTaking)
        return "A measurement is already running. Wait for it to finish.";
    if (m_syncSt != SyncSt::Idle)
        return "The link to the measurement unit is being checked. Try again in a few seconds.";
    return std::string();
}

void App::refOpen()
{
    refStop();
    m_refError.clear();
    m_refStage = RefStage::Idle;
    touch();
}

void App::refRun()
{
    if (!refBlocker().empty()) return;
    m_refTaking = true;
    m_refError.clear();
    m_refStage = RefStage::Waiting;
    m_refWaitStart = plat::ticks();
    event("healthy-tissue sample for " + m_caseId + " started");
    startRun();
    touch();
}

// Cancel, closing the sheet or a timeout: the next sweep must never be taken
// for the sample.
void App::refStop()
{
    const bool active = m_refStage == RefStage::Waiting || m_refStage == RefStage::Sweeping;
    if (active) stopRun();
    if (m_refTaking) {
        m_refTaking = false;
        event("healthy-tissue sample abandoned; nothing was stored");
    }
    if (m_refStage != RefStage::Done) m_refStage = RefStage::Idle;
    touch();
}

int App::refSecondsLeft() const
{
    const int limit = (m_refStage == RefStage::Sweeping) ? kRefSweepMs : kRefStartMs;
    const int waited = int(uint32_t(plat::ticks() - m_refWaitStart));
    return std::max(0, (limit - waited + 999) / 1000);
}

// From App::poll(): the sheet's own way out when no sweep comes, or one never ends.
void App::refWatch(uint32_t waitedMs)
{
    if (m_refStage != RefStage::Waiting && m_refStage != RefStage::Sweeping) return;
    const int limit = (m_refStage == RefStage::Sweeping) ? kRefSweepMs : kRefStartMs;
    if (waitedMs < uint32_t(limit)) return;
    m_refError = (m_refStage == RefStage::Sweeping)
        ? fmt("The sweep began but never finished, after %d seconds. Check the cable, then try again.",
              kRefSweepMs / 1000)
        : fmt("No sweep started in %d seconds. The measurement unit is not answering \xE2\x80\x94 "
              "check the cable, then try again.", kRefStartMs / 1000);
    event("healthy-tissue sample: " + m_refError);
    refStop();
}

void App::clearTissueRef()
{
    if (m_refTaking || m_refStage == RefStage::Waiting || m_refStage == RefStage::Sweeping) refStop();
    m_refs.clear();
    m_chartTissue = false;
    m_refStage = RefStage::Idle;
    m_refError.clear();
}

// The sweep that was asked for as the sample has ended (App::onSweepEnd).
void App::finishTissueSweep()
{
    m_refTaking = false;
    {   // H7-2.11.0: a shorted or bridged tip measures itself, not the tissue
        const std::string tw = tipFaultOf(m_current.points);
        if (!tw.empty()) {
            m_refError = "Not kept: " + tw + " Clean the tip, then check the needle again (Air check, then Saline check).";
            m_refStage = RefStage::Idle;
            caseTipFault(tw, "healthy-tissue sample");
            return;
        }
        const std::string nw = notATipOf(m_current.points);   // H7-2.15.0
        if (!nw.empty()) {
            m_refError = "Not kept: " + nw + " Place the needle in healthy tissue and take the sample again.";
            m_refStage = RefStage::Idle;
            caseNotATip(nw, "healthy-tissue sample");
            return;
        }
    }
    Spectrum s = m_current;
    s.caseId    = m_caseId;
    s.siteIndex = 0;                      // site 0: not one of the lesion's readings
    s.needleRev = m_s.needleRev;
    correctForNeedle(&s);

    // The same arithmetic as a reading — the 22 quantities and the quality
    // flags — and nothing of its verdict.
    const Verdict v = m_engine.decide(s);
    if (s.points.size() < 15 || !std::isfinite(v.features.z1k)) {
        m_refError = fmt("The sweep was not complete (%u of 15 points), so it was not kept. "
                         "Hold the needle still and take it again.", unsigned(s.points.size()));
        event("healthy-tissue sample rejected: " + m_refError);
        m_refStage = RefStage::Idle;
        return;
    }
    TissueSample smp;
    smp.n = int(m_refs.size()) + 1;
    smp.epochMs = s.epochMs;
    smp.when = localText(s.epochMs, "%H:%M:%S");
    smp.flags = v.quality.summary();
    smp.features = v.features;
    smp.points = s.points;
    m_refs.push_back(smp);
    m_refError.clear();
    m_refStage = RefStage::Done;
    m_chartTissue = true;

    if (plat::sdReady()) {
        const std::string path = m_records + "/tissue_reference.csv";
        const bool fresh = !plat::fileExists(path);
        std::ofstream f(path, std::ios::app);
        if (f) {
            const std::vector<std::string> names = Features::names();
            if (fresh) {
                f << "case_id,epoch_ms,time_local,needle_lot,needle_serial,flags";
                for (const std::string& n : names) f << ',' << n;
                f << ",fw_ips,fw_z1k,fw_text\n";
            }
            f << quote(m_caseId) << ',' << s.epochMs << ',' << localText(s.epochMs, "%Y-%m-%d %H:%M:%S") << ','
              << quote(m_patient.get("needle_lot")) << ',' << quote(m_patient.get("needle_serial")) << ','
              << quote(v.quality.summary());
            for (const std::string& n : names) f << ',' << cell(v.features.byName(n));
            f << ',' << cell(s.fwIps) << ',' << cell(s.fwZ1k) << ',' << quote(s.fwText) << '\n';
            f.flush();
        }
        if (!f || !f.good())
            storageFailed("The healthy-tissue sample could NOT be recorded (tissue_reference.csv) \xE2\x80\x94 check the records memory.");
        plat::makeDirs(m_records + "/tissue_sweeps");
        if (m_dataset && !m_dataset->appendSweep(m_records + "/tissue_sweeps", s))
            storageFailed("The sweep of the healthy-tissue sample could NOT be written \xE2\x80\x94 check the records memory.");
        m_refFileSize = -2;               // the log reads the file again
    } else {
        storageFailed("The healthy-tissue sample was NOT recorded: there is no records memory "
                      "(no SD card, and the internal memory is not in use).");
    }
    event(fmt("healthy-tissue sample H%d for %s kept: %s", smp.n, m_caseId.c_str(),
              sampleText(smp.features, ipsIsLegacy()).c_str()));
}

// The last sample went wrong (the needle moved, it was not in normal tissue).
// It leaves the case; in the record its line stays and a second line with the
// same case and time says "removed", so nothing is ever rewritten.
void App::refRemoveLast()
{
    if (m_refs.empty() || m_refTaking) return;
    const TissueSample gone = m_refs.back();
    m_refs.pop_back();
    if (m_refStage == RefStage::Done) m_refStage = RefStage::Idle;
    m_chartTissue = false;
    if (plat::sdReady()) {
        std::ofstream f(m_records + "/tissue_reference.csv", std::ios::app);
        f << quote(m_caseId) << ',' << gone.epochMs << ',' << localText(nowMs(), "%Y-%m-%d %H:%M:%S") << ",,,removed";
        for (size_t i = 0; i < Features::names().size() + 3; ++i) f << ',';
        f << '\n';
        f.flush();
        if (!f.good())
            storageFailed("The removal of the healthy-tissue sample could NOT be recorded \xE2\x80\x94 check the records memory.");
        m_refFileSize = -2;
    }
    event(fmt("healthy-tissue sample H%d for %s removed by the operator", gone.n, m_caseId.c_str()));
    touch();
}

std::string App::sampleText(const Features& f, bool legacyIps)
{
    const double ips = legacyIps ? f.ipsLegacy : f.ipsFit;
    std::string t = "Z " + ohmText(f.z1k) + " \xCE\xA9";
    if (std::isfinite(ips)) t += fmt(" \xC2\xB7 IPS %+.2f", ips);
    return t;
}

Features App::medianOf(const std::vector<TissueSample>& v)
{
    Features out;
    if (v.empty()) return out;
    if (v.size() == 1) return v[0].features;
    for (const std::string& name : Features::names()) {
        std::vector<double> x;
        for (const TissueSample& s : v) { const double d = s.features.byName(name); if (std::isfinite(d)) x.push_back(d); }
        if (x.empty()) continue;
        std::sort(x.begin(), x.end());
        const size_t n = x.size();
        out.setByName(name, n % 2 ? x[n / 2] : 0.5 * (x[n / 2 - 1] + x[n / 2]));
    }
    return out;
}

// One sample: "Z 2.84 kΩ · IPS −3.10 · 10:32:05". Several: "3 samples · median Z … · IPS …".
std::string App::tissueRefText() const
{
    if (m_refs.empty()) return std::string();
    if (m_refs.size() == 1) return sampleText(m_refs[0].features, ipsIsLegacy()) + " \xC2\xB7 " + m_refs[0].when;
    return fmt("%d samples \xC2\xB7 median ", int(m_refs.size())) + sampleText(medianOf(m_refs), ipsIsLegacy());
}

// For the log: every sample recorded for a case that was not removed, in the
// order taken.
std::vector<TissueSample> App::tissueSamplesOf(const std::string& caseId) const
{
    if (!plat::sdReady()) return std::vector<TissueSample>();
    const std::string path = m_records + "/tissue_reference.csv";
    const long sz = plat::fileSize(path);
    if (sz != m_refFileSize) {
        m_refFileSize = sz;
        m_refByCase.clear();
        std::ifstream f(path);
        std::string line;
        std::vector<std::string> head;
        while (f && std::getline(f, line)) {
            if (line.empty()) continue;
            const std::vector<std::string> c = split(line);
            if (head.empty()) { head = c; continue; }
            if (c.size() < 6 || c[0].empty()) continue;
            std::vector<TissueSample>& v = m_refByCase[c[0]];
            const int64_t epoch = std::atoll(c[1].c_str());
            if (c[5] == "removed") {
                for (size_t i = 0; i < v.size(); ++i) if (v[i].epochMs == epoch) { v.erase(v.begin() + long(i)); break; }
                continue;
            }
            TissueSample t;
            t.epochMs = epoch;
            t.when = c[2].size() >= 19 ? c[2].substr(11, 8) : std::string();
            t.flags = c[5];
            for (size_t i = 6; i < head.size() && i < c.size(); ++i) {
                double d = 0;
                if (!c[i].empty() && head[i].compare(0, 3, "fw_") != 0) { d = std::atof(c[i].c_str()); t.features.setByName(head[i], d); }
            }
            v.push_back(t);
        }
        for (auto& kv : m_refByCase) for (size_t i = 0; i < kv.second.size(); ++i) kv.second[i].n = int(i) + 1;
    }
    const auto it = m_refByCase.find(caseId);
    return it == m_refByCase.end() ? std::vector<TissueSample>() : it->second;
}

} // namespace app
