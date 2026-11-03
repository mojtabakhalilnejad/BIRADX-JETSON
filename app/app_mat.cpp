// app/app_mat.cpp — Settings -> Training (was Materials, H7-2.5.0): the material library.
//
// Sweeps of known materials (tissues, phantoms, saline) are labelled on the
// instrument and kept in records/materials.txt (birx::MatLibrary); an unknown
// sweep is compared with them by the classifier learnt from them
// (birx::trainMaterials, matlib.h). Every sweep is kept as measured, with the
// needle it was taken with: its number (H7-2.9.0), its Air check (stray
// capacitance and leakage) and its saline reference (H7-2.6.0), so that sweeps
// can also be compared needle-normalised.
//
// H7-2.5.3: every reading of a case keeps its sweep as measured in
// records/reading_raw.txt; a case of the logs is tagged afterwards with what it
// was (H7-2.9.0: the whole case, all its usable readings one placement), and
// joins the library. The library is exported to USB and imported on another
// device, where it is ADDED: nothing there is deleted.
//
// H7-2.10.0: the Training needle's QC — its Air check and a two-sweep saline
// check against its lot's limits before any sweep, due again by sweeps and by
// minutes, each check closing the sweeps since the one before. H7-2.11.0: a
// sweep of a shorted or bridged tip is not kept, and such sweeps already in
// the library are not learnt and can be deleted.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace birx;

namespace app {

namespace {
const int kMatStartMs = 30000;
const int kMatSweepMs = 25000;
const size_t kMaxSweeps = 2000;               // the library's size (records/materials.txt)
const uint32_t kSameSampleMs = 120000;        // a labelled sweep within two minutes of one of its label: the same placement

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

std::string readAll(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// A label or a needle's number as kept: no field separators, no spaces at the ends.
std::string cleanName(const std::string& s0)
{
    std::string s;
    for (char c : s0)
        if (c != '|' && c != ':' && c != ';' && c != '\n' && c != '\r') s.push_back(c);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}

// H7-2.9.0: needle numbers are compared without regard to case
bool sameNeedle(const std::string& a, const std::string& b)
{
    const std::string x = cleanName(a), y = cleanName(b);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::toupper((unsigned char)x[i]) != std::toupper((unsigned char)y[i])) return false;
    return true;
}

// A sweep, wherever it goes: the device it was taken on, when, where it came from, and its points.
std::string sweepKey(const std::string& dev, const MatSweep& s)
{
    char b[160];
    const size_t n = s.pts.size();
    std::snprintf(b, sizeof b, "|%d|%.6g|%.6g|%.6g|%.6g", int(n), n ? s.pts.front().magOhm : 0.0, n ? s.pts.front().phaseDeg : 0.0,
                  n ? s.pts.back().magOhm : 0.0, n ? s.pts.back().phaseDeg : 0.0);
    return dev + "|" + s.when + "|" + s.origin + b;
}

// H7-2.5.3: a reading of a case, as its sweep is kept: "<case>/<site>/<epoch ms>"
std::string readingKey(const std::string& caseId, int site, int64_t epochMs)
{
    return caseId + "/" + std::to_string(site) + "/" + std::to_string(epochMs);
}

// H7-2.12.0: a needle's number as a key (compared without regard to case, as sameNeedle)
std::string needleKey(const std::string& n)
{
    std::string k = cleanName(n);
    for (char& c : k) c = char(std::toupper((unsigned char)c));
    return k;
}

// H7-2.12.0: an ISO time as kept ("2026-10-20T12:28:11Z", UTC) in ms since 1970; 0 when it is not one
int64_t isoMs(const std::string& w)
{
    if (w.size() < 19 || w[4] != '-' || w[7] != '-' || (w[10] != 'T' && w[10] != ' ')) return 0;
    auto num = [&](size_t at, size_t n) { int v = 0; for (size_t i = at; i < at + n; ++i) { if (w[i] < '0' || w[i] > '9') return -1; v = v * 10 + (w[i] - '0'); } return v; };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || mi < 0 || se < 0) return 0;
    const int yy = y - (mo <= 2);
    const int era = yy / 400, yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = int64_t(era) * 146097 + doe - 719468;
    return ((days * 24 + h) * 60 + mi) * 60000LL + se * 1000LL;
}

const int64_t kQcSettleGapMs = 120000;        // H7-2.12.0: a new tip's two checks, 2 minutes apart at least

// H7-2.8.0: what the final answer had said of a sweep labelled after it
void recordSaidFinal(const MatMatch& m, const std::string& label, MatSweep* w)
{
    if (!m.ok || !m.fOk || m.fBest < 0) return;
    const std::string f = m.labels[size_t(m.fBest)];
    const bool known = std::find(m.labels.begin(), m.labels.end(), label) != m.labels.end();
    w->saidF = m.unlike ? std::string("(none)") : f;
    w->saidFP = m.fProb[size_t(m.fBest)];
    w->saidFRight = m.unlike ? (known ? 0 : 1) : (f == label ? 1 : 0);
}

std::string saidFinalText(const MatSweep& w)
{
    if (w.saidFRight < 0) return std::string();
    if (w.saidF == "(none)") return std::string(" Final: like none") + (w.saidFRight ? " (right)." : " (wrong).");
    return fmt(" Final: %s, %.0f %% (%s).", w.saidF.c_str(), w.saidFP * 100, w.saidFRight ? "right" : "wrong");
}

// H7-2.5.2: a sweep the classifier's own check set aside, in words (for the event log)
std::string asideNote(const MatModel& md, const MatLibrary& lib, size_t i)
{
    if (i >= md.state.size() || md.state[i] != MatModel::kAsideAuto) return std::string();
    std::vector<int> places;
    for (const MatSweep& s : lib.sweeps)
        if (s.label == lib.sweeps[i].label && std::find(places.begin(), places.end(), s.sample) == places.end())
            places.push_back(s.sample);
    const double lim = int(places.size()) < kAsideMinPlaces ? md.asideGross : md.asideLimit;
    return fmt(" Set aside by the check: far from the other sweeps of \"%s\" (distance %.0f, limit %.0f), not learnt.",
               lib.sweeps[i].label.c_str(), md.ownD2[i], lim);
}
} // namespace

// ---------------------------------------------------------------------------
// the library and its classifiers

const MatModel& App::matModel() const
{
    if (m_matModelRev != m_matRev) {
        m_matModel = trainMaterials(m_matLib, false);
        m_matModelRev = m_matRev;
    }
    return m_matModel;
}

// H7-2.6.0: learnt from the needle-normalised sweeps (those with their needle's saline reference)
const MatModel& App::matModelN() const
{
    if (m_matModelNRev != m_matRev) {
        m_matModelN = trainMaterials(m_matLib, true);
        m_matModelNRev = m_matRev;
    }
    return m_matModelN;
}

// the best answer for a sweep: needle-normalised when the needle has a reference and that model is learnt
MatMatch App::matClassify(const std::vector<Point>& pts, const MatNeedle& n) const
{
    MatMatch m;
    if (n.hasRef() && matModelN().ok) m = matModelN().classify(pts, n);
    if (!m.ok) {
        m = matModel().classify(pts, n);
        if (!m.ok) return m;
    }
    // H7-2.6.0: a point measured on another gain range than the best material's sweeps were
    if (m.best >= 0) {
        const std::string& label = m.labels[size_t(m.best)];
        const MatModel& md = matModel();
        for (double f : InstrumentConstants::sweepFrequencies()) {
            for (const Point& p : pts) {
                if (!(p.freqHz > 0) || !(std::fabs(p.freqHz / f - 1) < 0.02)) continue;
                const int mine = p.range;
                if (mine) {
                    int votes[8] = { 0 };
                    for (size_t i = 0; i < m_matLib.sweeps.size(); ++i) {
                        const MatSweep& s = m_matLib.sweeps[i];
                        if (s.label != label) continue;
                        if (i < md.state.size() && md.state[i] != MatModel::kUsed && md.state[i] != MatModel::kUsedByHand) continue;
                        for (const Point& q : s.pts)
                            if (q.freqHz > 0 && std::fabs(q.freqHz / f - 1) < 0.02) {
                                if (q.range >= 1 && q.range <= 6) ++votes[q.range];
                                break;
                            }
                    }
                    int top = 0;
                    for (int r = 1; r < 7; ++r) if (votes[r] > votes[top]) top = r;
                    if (top && votes[top] > 0 && mine != top) ++m.rangeDiff;
                }
                break;
            }
        }
    }
    return m;
}

// the main screen: the last sweep (any sweep) compared with the library, when it can be
const MatMatch& App::matForCurrent() const
{
    double cp = -2.0;
    if (m_cal.calibrated(3)) {
        const double c = m_cal.ranges().at(3).open.cpFarad;
        if (std::isfinite(c)) cp = c;
    }
    if (m_mainSeq != m_sweepSeq || m_mainRev != m_matRev || m_mainCp != cp) {
        m_mainSeq = m_sweepSeq;
        m_mainRev = m_matRev;
        m_mainCp = cp;
        m_mainMatch = MatMatch();
        if (!m_inSweep && m_rawPts.size() >= 15 && matModel().ok)
            m_mainMatch = matClassify(m_rawPts, currentNeedle());
    }
    return m_mainMatch;
}

// H7-2.12.1: why the main screen shows no material for the last sweep ("" when it does, or is not asked to)
std::string App::matMainWhy() const
{
    if (!m_s.matOnMain || m_inSweep || m_rawPts.size() < 15) return std::string();
    const MatMatch& mm = matForCurrent();
    if (mm.ok) return std::string();
    const MatModel& md = matModel();
    if (md.ok) return mm.why.empty() ? std::string("this sweep cannot be compared") : mm.why;
    int ready = 0, labels = 0;
    for (const std::string& l : m_matLib.labels) {
        if (m_matLib.isOff(l)) continue;
        ++labels;
        int n = 0;
        for (const MatSweep& s : m_matLib.sweeps) if (s.label == l && s.qc != "pending" && s.qc != "suspect") ++n;
        if (n >= 2) ++ready;
    }
    std::string w = fmt("Settings \xE2\x86\x92 Training has %d of the 2 materials it needs, with two learnt sweeps each.",
                        std::min(ready, 2));
    if (md.nQcPending) w += fmt(" %d sweep%s wait%s for a saline check to be learnt.", md.nQcPending, md.nQcPending == 1 ? "" : "s",
                                md.nQcPending == 1 ? "s" : "");
    if (!labels) w = "Settings \xE2\x86\x92 Training has no material yet: label two, with two sweeps each.";
    return w;
}

std::string App::setMatOnMain(bool on)
{
    Settings n = m_s;
    n.matOnMain = on;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event(std::string("materials: the main screen ") + (on ? "shows" : "does not show") + " the material");
    touch();
    return std::string();
}

// H7-2.5.3: the library's sweeps that came from the logs (tagged readings), and from other devices
int App::matTaggedReadings() const
{
    int n = 0;
    for (const MatSweep& s : m_matLib.sweeps) if (!s.origin.empty()) ++n;
    return n;
}

int App::matFromOtherDevices() const
{
    int n = 0;
    for (const MatSweep& s : m_matLib.sweeps) if (!s.device.empty() && s.device != m_s.deviceId) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// the needle (H7-2.6.0 / 2.9.0)

// H7-2.9.0: the Air check in force is this needle's (one taken before numbers were asked for counts as its own)
bool App::trainCalMatches() const
{
    return m_cal.needleId.empty() || sameNeedle(m_cal.needleId, m_s.trainNeedle);
}

bool App::matHasRef() const
{
    return m_matRef.hasRef() && m_cal.calibrated(3) && !m_cal.takenAt.empty() && m_matRefCalAt == m_cal.takenAt &&
           (caseOpen() || sameNeedle(m_matRefNeedle, m_s.trainNeedle));
}

// Cp, Gp and the reference of the needle on now
MatNeedle App::currentNeedle() const
{
    MatNeedle n;
    if (m_cal.calibrated(3)) {
        n.cpF = m_cal.ranges().at(3).open.cpFarad;
        n.gpS = m_cal.ranges().at(3).open.gpSiemens;
    }
    if (matHasRef()) { n.refR = m_matRef.refR; n.refA = m_matRef.refA; n.refAlpha = m_matRef.refAlpha; }
    else if (m_dip.ok && m_dip.step == kCheck9 && m_dip.e.ok && m_cal.calibrated(3)) {
        // the Saline check before a case, in 0.9 % saline (its resistance at the saline's temperature)
        n.refR = std::isfinite(m_dip.rRaw) ? m_dip.rRaw : m_dip.e.rOhm; n.refA = m_dip.e.a; n.refAlpha = m_dip.e.alpha;
    }
    return n;
}

// H7-2.9.0: only the Training needle's own Air check and reference are applied to its sweeps
MatNeedle App::trainingNeedle() const
{
    if (!trainCalMatches()) return MatNeedle();
    return currentNeedle();
}

std::string App::matRefText() const
{
    const MatNeedle n = caseOpen() ? currentNeedle() : trainingNeedle();
    if (!n.hasRef()) return std::string();
    ElectrodeModel e;
    e.a = n.refA; e.alpha = n.refAlpha; e.rOhm = n.refR; e.ok = true;
    if (!matHasRef())
        return fmt("%s: saline %.0f ohm, tip surface %.0f ohm at 1 kHz (CPE exponent %.2f)", "from the Saline check", n.refR,
                   e.ze1k(), n.refAlpha);
    const std::string when = "taken " + m_matRefWhen.substr(11, 5);
    return fmt("%s: saline %.0f ohm, tip surface %.0f ohm at 1 kHz (CPE exponent %.2f)", when.c_str(), n.refR, e.ze1k(), n.refAlpha);
}

std::string App::setTrainNeedle(const std::string& id0)
{
    std::string id;
    for (char c : cleanName(id0)) id.push_back(c == '|' || c == ',' || c == '"' || c == ':' || c == ';' ? '-' : c);
    if (id.empty()) return "Enter the needle's number.";
    if (id.size() > 20) return "At most 20 characters.";
    if (m_matTesting) return "A sweep is running.";
    if (id == m_s.trainNeedle) return std::string();
    const std::string before = m_s.trainNeedle;
    const int left = qcPending();       // H7-2.12.0: its sweeps since its last check wait for its next one
    Settings n = m_s;
    n.trainNeedle = id;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event("training: needle " + id);
    m_matPending = false;               // an identified sweep of the needle before is not labelled with this one
    m_qc = QcCheck();                   // H7-2.10.0: its checks are its own
    m_qcSweeps = 0;
    if (m_cal.calibrated(3) && !trainCalMatches())
        m_matMsg = "Needle " + id + ": the Air check in force was taken with needle " + m_cal.needleId +
                   ". Do this needle's Air check (and its saline reference) before its sweeps.";
    else if (m_cal.calibrated(3) && m_cal.needleId.empty())
        m_matMsg = "Needle " + id + ". The Air check in force has no needle number: if it was not taken with this needle, do it again.";
    else
        m_matMsg = "Needle " + id + ": every sweep from now on is kept with this number.";
    if (left)
        m_matMsg += fmt(" The %d sweep%s of needle %s since its last saline check wait%s for its next one (when it is used again), "
                        "and %s not learnt until then.", left, left == 1 ? "" : "s", before.c_str(), left == 1 ? "s" : "",
                        left == 1 ? "is" : "are");
    m_matMsgOk = trainCalMatches();
    touch();
    return std::string();
}

// every attempt at a saline reference, with its sweep, in records/saline_reference.csv
void App::recordRefAttempt(const std::vector<Point>& pts, const MatNeedle& n, bool ok, const std::string& why)
{
    if (!plat::sdReady()) return;
    const std::string path = m_records + "/saline_reference.csv";
    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    if (fresh) f << "when,needle,cpPfAir,accepted,rOhm,zeAt1kOhm,alpha,bestCpPf,bestRms,why,points(f:mag:phase:range)\n";
    double bestRms = kNaN;
    const double bestCp = matBestSalineCp(pts, n.gpS, &bestRms);
    ElectrodeModel e; e.a = n.refA; e.alpha = n.refAlpha; e.rOhm = n.refR; e.ok = n.hasRef();
    std::string q;
    for (char ch : why) q += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    f << isoUtc(nowMs()) << ',' << (m_patient.get("needle_serial").empty() ? m_s.trainNeedle : m_patient.get("needle_serial")) << ','
      << fmt("%.1f", n.cpF * 1e12) << ',' << (ok ? "yes" : "no") << ','
      << (ok ? fmt("%.1f,%.1f,%.3f", n.refR, e.ze1k(), n.refAlpha) : std::string(",,")) << ','
      << fmt("%.1f,%.4f", bestCp * 1e12, bestRms) << ",\"" << q << "\",";
    for (size_t i = 0; i < pts.size(); ++i)
        f << (i ? ";" : "") << fmt("%.0f:%.2f:%.3f:%d", pts[i].freqHz, pts[i].magOhm, pts[i].phaseDeg, pts[i].range);
    f << "\n";
}

// H7-2.5.3: every reading's sweep as measured, with its needle, for tagging it afterwards
void App::recordRawReading()
{
    if (!plat::sdReady() || m_rawPts.empty()) return;
    MatSweep w;
    w.origin = readingKey(m_current.caseId, m_current.siteIndex, m_current.epochMs);
    w.when = isoUtc(m_current.epochMs);
    w.needle = m_patient.get("needle_serial").empty() ? m_cal.needleId : m_patient.get("needle_serial");
    w.setNeedle(currentNeedle());
    w.pts = m_rawPts;
    std::ofstream f(m_records + "/reading_raw.txt", std::ios::binary | std::ios::app);
    if (f) f << matSweepLine(w) << "\n";
}

// ---------------------------------------------------------------------------
// H7-2.10.0: the Training needle's QC. Its Air check (its stray capacitance and leakage within its
// lot's limits) and its saline check (two sweeps in 0.9 % saline at the saline's temperature: R at 25 °C,
// the tip's surface and exponent within the lot's limits, the two sweeps within 3 %) come before any
// sweep; a saline check is due again every qcSweeps sweeps or qcMinutes minutes, an Air check every
// qcAirMinutes. Each saline check also closes the sweeps since the one before: when the needle is found
// as it was (R within 8 % since H7-2.15.0, the surface within 20 %) their reference is taken between the two checks;
// when it moved, they are marked suspect and not learnt.

QcLimits App::qcLimitsFor(const std::string& lot, int step) const
{
    const LotCorrection* c = m_lotFileLoaded && !lot.empty() ? m_lotFile.find(lot) : nullptr;
    QcLimits q = c ? c->qcLimits(step) : qcGeneric(step);
    if (!c) q.lotId = lot;
    // a needle of a lot not validated with this saline: within 30 % of the reference needle's tip and
    // its surface within a factor of four, when the lot file has the reference in it (as before)
    const bool salineFromLot = c && c->hasModel() && c->dipValid(step);
    if (!salineFromLot && !lot.empty() && m_lotFileLoaded && m_lotFile.refFor(step).ok) {
        const ElectrodeModel& re = m_lotFile.refFor(step);
        q.rMin = re.rOhm / 1.3; q.rMax = re.rOhm / 0.7; q.zeMin = re.ze1k() * 0.25; q.zeMax = re.ze1k() * 4.0;
    }
    return q;
}

std::vector<std::string> App::lotIds() const
{
    std::vector<std::string> v;
    if (m_lotFileLoaded) for (const LotCorrection& c : m_lotFile.lots) v.push_back(c.lotId);
    return v;
}

std::string App::setTrainLot(const std::string& lot)
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    if (!lot.empty()) {
        const std::vector<std::string> ids = lotIds();
        if (std::find(ids.begin(), ids.end(), lot) == ids.end()) return "Lot " + lot + " is not in the lot file.";
    }
    if (lot == m_s.trainLot) return std::string();
    Settings n = m_s;
    n.trainLot = lot;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    // its checks were judged with the other lot's limits: both again
    m_calAtMs = 0;
    m_qc = QcCheck();
    m_qcSweeps = 0;
    m_matMsg = lot.empty() ? std::string("No lot: the needle is checked against the generic limits. Do its Air check and saline check.")
                           : "Lot " + lot + ": the needle is checked against this lot's own limits. Do its Air check and saline check.";
    m_matMsgOk = true;
    event("training: lot " + (lot.empty() ? std::string("(none)") : lot));
    touch();
    return std::string();
}

// H7-2.15.0: the saline's temperature, kept across a power cycle. Until it is entered after the device is switched
// on, the check says so (records_7: every check went with the 22.0 the screen started with; 1 degree C off moves R at
// 25 degrees C by 2 %).
std::string App::setSalineTemp(double c)
{
    if (!(c >= 5 && c <= 45)) return "The temperature must be between 5 and 45 \xC2\xB0""C.";
    m_salineTempSet = true;
    if (std::fabs(c - m_s.salineTempC) < 1e-9) return std::string();
    Settings n = m_s;
    n.salineTempC = c;
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event(fmt("saline temperature set to %.1f C", c));
    touch();
    return std::string();
}

std::string App::setQcIntervals(int sweeps, int minutes, int airMinutes)
{
    Settings n = m_s;
    n.qcSweeps = std::max(1, std::min(200, sweeps));
    n.qcMinutes = std::max(1, std::min(600, minutes));
    n.qcAirMinutes = std::max(1, std::min(1440, airMinutes));
    std::string why;
    if (!saveSettings(n, &why)) return why;
    event(fmt("training: a saline check every %d sweeps or %d minutes, an Air check every %d minutes", n.qcSweeps, n.qcMinutes,
              n.qcAirMinutes));
    touch();
    return std::string();
}

int App::qcMinutesSince() const { return m_qc.ok ? int((nowMs() - m_qc.atMs) / 60000) : -1; }
int App::qcAirMinutes() const { return m_calAtMs > 0 ? int((nowMs() - m_calAtMs) / 60000) : -1; }

bool App::qcAirOk() const
{
    return m_cal.calibrated(3) && trainCalMatches() && m_calAtMs > 0 && nowMs() - m_calAtMs < int64_t(m_s.qcAirMinutes) * 60000;
}

bool App::qcCheckOk() const
{
    return m_qc.ok && !m_qc.repeat && m_cal.calibrated(3) && m_qc.calAt == m_cal.takenAt && sameNeedle(m_qc.needle, m_s.trainNeedle);
}

std::string App::matRefBlocker() const
{
    const std::string b = matBlockerBase();
    if (!b.empty()) return b;
    if (m_matTesting) return "A sweep is already running.";
    if (!m_cal.calibrated(3)) return "Air check first: the saline check needs this needle's stray capacitance.";
    if (!trainCalMatches()) return "The Air check in force was taken with needle " + m_cal.needleId + ": do this needle's Air check first.";
    return std::string();
}

std::string App::matStartRef(double tempC, double condMsCm)
{
    const std::string w = matRefBlocker();
    if (!w.empty()) return w;
    m_dipTraining = true;
    dipRunCore(tempC, condMsCm);
    if (!m_dipping) {
        m_dipTraining = false;
        return m_dipError.empty() ? std::string("The saline check did not start.") : m_dipError;
    }
    m_matMsg.clear();
    event("training: saline check started (needle " + m_s.trainNeedle + ")");
    return std::string();
}

// H7-2.12.0: the library sweeps of the Training needle taken since its last passed check on record: the
// ones its next check closes
std::vector<size_t> App::qcOpen() const
{
    std::vector<size_t> v;
    if (m_s.trainNeedle.empty()) return v;
    const auto it = m_qcLast.find(needleKey(m_s.trainNeedle));
    if (it == m_qcLast.end()) return v;
    for (size_t i = 0; i < m_matLib.sweeps.size(); ++i) {
        const MatSweep& s = m_matLib.sweeps[i];
        if (s.qc != "pending" || !sameNeedle(s.needle, m_s.trainNeedle)) continue;
        const int64_t t = isoMs(s.when);
        if (t > 0 && t + 1000 >= it->second.atMs) v.push_back(i);
    }
    return v;
}

int App::qcMarkOpen(const char* qc, const char* move)
{
    const std::vector<size_t> open = qcOpen();
    for (size_t i : open) { m_matLib.sweeps[i].qc = qc; if (move) m_matLib.sweeps[i].qcMove = move; }
    if (!open.empty()) {
        ++m_matRev;
        if (!saveMaterials()) storageFailed("The material library could NOT be written to records/materials.txt.");
    }
    return int(open.size());
}

// H7-2.12.0: a pending sweep some check of its needle on record (one before it) can still close
bool App::qcClosable(const MatSweep& s) const
{
    const auto it = m_qcLast.find(needleKey(s.needle));
    const int64_t t = isoMs(s.when);
    return !s.needle.empty() && it != m_qcLast.end() && t > 0 && t + 1000 >= it->second.atMs;
}

int App::qcWaiting() const
{
    int n = 0;
    for (const MatSweep& s : m_matLib.sweeps) if (s.qc == "pending" && qcClosable(s)) ++n;
    return n;
}

int App::qcUnconfirmed() const
{
    int n = 0;
    for (const MatSweep& s : m_matLib.sweeps) if (s.qc == "pending" && !qcClosable(s)) ++n;
    return n;
}

std::string App::matKeepUnconfirmed()
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    int n = 0;
    for (MatSweep& s : m_matLib.sweeps)
        if (s.qc == "pending" && !qcClosable(s)) { s.qc = "kept"; ++n; }
    if (!n) return "There is no unconfirmed sweep.";
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event(fmt("training: %d unconfirmed sweep%s learnt by hand (no saline check can close them)", n, n == 1 ? "" : "s"));
    touch();
    return fmt("%d sweep%s learnt without a closing check. The classifier's own check still sets aside any far from its "
               "material.", n, n == 1 ? " is" : "s are");
}

std::string App::qcSettleText(bool countdown) const
{
    if (!m_qc.ok || m_qc.settled) return std::string();
    // H7-2.12.4: the 2 minutes count from the first of the checks that agree, not from the last one (a check
    // repeated early no longer starts them again)
    const int64_t from = m_qc.settleFromMs > 0 ? m_qc.settleFromMs : m_qc.atMs;
    const int64_t at = from + kQcSettleGapMs, left = std::max<int64_t>(0, at - nowMs());
    const std::string when = !countdown ? "from " + localText(at, "%H:%M:%S") + " on"
                           : left > 0 ? fmt("in %d s", int((left + 999) / 1000)) : std::string("now");
    const std::string out = " (Or: Sweep without waiting.)";
    if (!std::isfinite(m_qc.moved))
        return "New tip: its surface changes in its first minutes in the saline. Saline check again " + when +
               " (2 minutes after this one at least); the sweeps begin once two checks agree within 20 %." + out;
    if (m_qc.movedR > QcLimits().maxBracketR)
        return fmt("Its R moved %.1f %% since the check before (limit %.0f %%): the needle is not yet steady. Saline check again %s.",
                   m_qc.movedR * 100, QcLimits().maxBracketR * 100, when.c_str()) + out;
    if (m_qc.moved > 0.2)
        return fmt("The tip is still settling: its surface moved %.0f %% since the check before (limit 20 %%). Saline check "
                   "again %s.", m_qc.moved * 100, when.c_str()) + out;
    return "Two checks agree, but less than 2 minutes after the first of them: saline check again " + when + ", once, and "
           "the needle may sweep." + out;
}

// H7-2.12.5: the suspect sweeps, learnt by hand. A needle's tip surface moves a lot in its first half hour while
// its R stays put; on the bench of 20 October the sweeps so marked were told right all the same.
std::string App::matKeepSuspect()
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    int n = 0;
    for (MatSweep& s : m_matLib.sweeps)
        if (s.qc == "suspect" && matTipFault(s.pts, s.refR, kTipBridgedRatio, nullptr) == TipFault::None) { s.qc = "kept"; ++n; }
    if (!n) return "There is no suspect sweep to learn (a sweep of a faulty tip is never learnt).";
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event(fmt("training: %d suspect sweep%s learnt by hand", n, n == 1 ? "" : "s"));
    touch();
    return fmt("%d suspect sweep%s learnt by hand. The classifier's own check still sets aside any far from its material.",
               n, n == 1 ? " is" : "s are");
}

// H7-2.12.4: the way out of the settling, by hand. The needle sweeps now; its next saline check still closes
// these sweeps (the bracket): if the tip moved more than 20 % by then, they are marked suspect and not learnt.
std::string App::qcSkipSettling()
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    if (!m_qc.ok) return "Saline check this needle first.";
    if (m_qc.settled) return std::string();
    m_qc.settled = true;
    const auto it = m_qcLast.find(needleKey(m_s.trainNeedle));
    if (it != m_qcLast.end()) { it->second.settled = true; it->second.anchorMs = it->second.atMs; saveTrainQc(); }
    m_matMsg = "Settling skipped by hand: the needle may sweep. Its next saline check still closes these sweeps: if the tip "
               "moved more than 20 % by then, they are marked SUSPECT and not learnt.";
    m_matMsgOk = true;
    event("training: the tip's settling skipped by hand (needle " + m_s.trainNeedle + ")");
    touch();
    return std::string();
}

// H7-2.12.0: the last passed check of each Training needle, on record
void App::loadTrainQc()
{
    m_qcLast.clear();
    if (!plat::sdReady()) return;
    std::istringstream in(readAll(m_records + "/training_qc.txt"));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 2, "N=") != 0) continue;
        std::vector<std::string> f;
        std::string cur;
        for (char ch : line.substr(2)) { if (ch == '|') { f.push_back(cur); cur.clear(); } else cur.push_back(ch); }
        f.push_back(cur);
        if (f.size() < 8 || f[0].empty()) continue;
        QcLast q;
        q.atMs = std::atoll(f[1].c_str());
        q.r25 = std::atof(f[2].c_str()); q.ze = std::atof(f[3].c_str());
        q.refR = std::atof(f[4].c_str()); q.refA = std::atof(f[5].c_str()); q.refAlpha = std::atof(f[6].c_str());
        q.settled = f[7] == "1";
        q.anchorMs = f.size() >= 9 ? std::atoll(f[8].c_str()) : q.atMs;   // H7-2.12.4
        if (q.atMs > 0) m_qcLast[needleKey(f[0])] = q;
    }
}

void App::saveTrainQc()
{
    if (!plat::sdReady()) return;
    std::ostringstream o;
    o << "BIRADX-TRAINQC 1\n# each Training needle's last passed saline check: needle|ms|R25|ze1k|refR|refA|refAlpha|settled|"
         "first ms of the checks that agree\n";
    for (const auto& kv : m_qcLast)
        o << "N=" << kv.first << '|' << kv.second.atMs << '|' << fmt("%.3f|%.3f|%.4f|%.6g|%.4f", kv.second.r25, kv.second.ze,
                                                                    kv.second.refR, kv.second.refA, kv.second.refAlpha)
          << '|' << (kv.second.settled ? 1 : 0) << '|' << (kv.second.anchorMs > 0 ? kv.second.anchorMs : kv.second.atMs) << '\n';
    std::ofstream f(m_records + "/training_qc.txt", std::ios::binary | std::ios::trunc);
    if (f) { f << o.str(); f.flush(); }
    if (!f || !f.good()) storageFailed("training_qc.txt could NOT be written \xE2\x80\x94 check the records memory.");
}

// ---------------------------------------------------------------------------
// H7-2.15.0: each needle's leakage against its own. A needle's insulation wears: on the bench of 27-28 October
// (records_7) V1.1 went from 8 to 95 uS in two days and passed the generic 100 uS all along, while its sweeps
// moved. Its lowest Air check on record is the needle sound; three times that (and 10 uS more at least) is a
// wet or worn needle.
namespace {
const double kLeakFactor = 3.0, kLeakAddS = 10e-6;

// a line of a CSV file as fields: the first n-1 split at the commas, the rest (a quoted reason) as the last
std::vector<std::string> csvFields(const std::string& line, size_t n)
{
    std::vector<std::string> c;
    std::string cur;
    size_t i = 0;
    for (; i < line.size() && c.size() + 1 < n; ++i) {
        if (line[i] == ',') { c.push_back(cur); cur.clear(); } else cur.push_back(line[i]);
    }
    cur += line.substr(i);
    c.push_back(cur);
    for (std::string& f : c) {
        while (!f.empty() && (f.back() == '\r' || f.back() == '\n')) f.pop_back();
        if (f.size() >= 2 && f.front() == '"' && f.back() == '"') f = f.substr(1, f.size() - 2);
    }
    return c;
}

int colOf(const std::vector<std::string>& head, const char* name)
{
    for (size_t i = 0; i < head.size(); ++i) if (head[i] == name) return int(i);
    return -1;
}

// the accepted Air checks on record (needle_cal.csv, gain range 3): needle, when, leakage
struct AirRec { std::string key; int64_t at; double gpS; };
std::vector<AirRec> readAirChecks(const std::string& path)
{
    std::vector<AirRec> v;
    std::ifstream f(path, std::ios::binary);
    std::string line;
    int iId = -1, iRange = -1, iGp = -1, iAt = -1;
    bool head = true;
    while (std::getline(f, line)) {
        const std::vector<std::string> c = csvFields(line, 64);
        if (head) {
            head = false;
            iId = colOf(c, "needle_id"); iRange = colOf(c, "range"); iGp = colOf(c, "gp_us"); iAt = colOf(c, "taken_at");
            if (iId < 0 || iGp < 0) return v;
            continue;
        }
        if (int(c.size()) <= std::max(std::max(iId, iGp), iAt)) continue;
        if (iRange >= 0 && int(c.size()) > iRange && std::atoi(c[size_t(iRange)].c_str()) != 3) continue;
        AirRec r;
        r.key = needleKey(c[size_t(iId)]);
        r.gpS = std::atof(c[size_t(iGp)].c_str()) * 1e-6;
        r.at = iAt >= 0 ? isoMs(c[size_t(iAt)]) : 0;
        if (!r.key.empty()) v.push_back(r);
    }
    return v;
}
} // namespace

void App::loadLeakBase()
{
    m_leakBase.clear();
    if (!plat::sdReady()) return;
    for (const AirRec& r : readAirChecks(m_records + "/needle_cal.csv")) noteNeedleLeak(r.key, r.gpS);
}

void App::noteNeedleLeak(const std::string& needle, double gpS)
{
    const std::string k = needleKey(needle);
    if (k.empty() || !(gpS > 0)) return;
    const auto it = m_leakBase.find(k);
    if (it == m_leakBase.end() || gpS < it->second) m_leakBase[k] = gpS;
}

double App::needleLeakBase(const std::string& needle) const
{
    const auto it = m_leakBase.find(needleKey(needle));
    return it == m_leakBase.end() ? kNaN : it->second;
}

std::string App::needleLeakWhy(const std::string& needle, double gpS) const
{
    const double b = needleLeakBase(needle);
    if (!(b > 0) || !std::isfinite(gpS)) return std::string();
    const double lim = std::max(kLeakFactor * b, b + kLeakAddS);
    if (gpS <= lim) return std::string();
    return fmt("its leakage, %.1f uS, is %.1f times this needle's own when it was sound (%.1f uS, its lowest Air check on "
               "record; the limit is %.1f uS): its insulation is wet or worn.", gpS * 1e6, gpS / b, b * 1e6, lim * 1e6);
}

// H7-2.15.0: the sweeps marked suspect, judged again by today's rules (at start): learnt when the needle's R held
// between the two checks around them (within 8 %; it was 3 %, less than a sound needle's own scatter from one dip
// to the next) and its leakage was within its limits (the generic 40 uS, and three times its own). The checks
// around a sweep marked before H7-2.12.6, which did not keep how far the needle moved, are taken from
// saline_check.csv; a check refused between them (other than for not being one, or for its two sweeps apart)
// leaves them suspect. A faulty tip is never learnt.
int App::qcRejudge()
{
    m_qcRejudged = 0;
    bool any = false;
    for (const MatSweep& s : m_matLib.sweeps) if (s.qc == "suspect") { any = true; break; }
    if (!any || !plat::sdReady()) return 0;
    struct Chk { int64_t at; double r, ze; bool ok; };
    std::map<std::string, std::vector<Chk>> chk;       // the 0.9 % checks of each needle, in time order
    {
        std::ifstream f(m_records + "/saline_check.csv", std::ios::binary);
        std::string line;
        int iWhen = -1, iNeedle = -1, iSol = -1, iR = -1, iZe = -1, iAcc = -1, iWhy = -1;
        size_t nCol = 0;
        bool head = true;
        while (std::getline(f, line)) {
            if (head) {
                head = false;
                const std::vector<std::string> h = csvFields(line, 64);
                nCol = h.size();
                iWhen = colOf(h, "when"); iNeedle = colOf(h, "needle"); iSol = colOf(h, "solution");
                iR = colOf(h, "rOhm25"); iZe = colOf(h, "ze1kOhm"); iAcc = colOf(h, "accepted"); iWhy = colOf(h, "why");
                if (iWhen < 0 || iNeedle < 0 || iR < 0 || iZe < 0 || iAcc < 0) break;
                continue;
            }
            const std::vector<std::string> c = csvFields(line, nCol);
            if (c.size() < nCol) continue;
            if (iSol >= 0 && c[size_t(iSol)].find("0.09") != std::string::npos) continue;
            const std::string k = needleKey(c[size_t(iNeedle)]);
            const int64_t t = isoMs(c[size_t(iWhen)]);
            if (k.empty() || t <= 0) continue;
            Chk q;
            q.at = t; q.r = std::atof(c[size_t(iR)].c_str()); q.ze = std::atof(c[size_t(iZe)].c_str());
            q.ok = c[size_t(iAcc)] == "yes" && q.r > 0 && q.ze > 0;
            if (!q.ok) {
                const std::string w = iWhy >= 0 ? c[size_t(iWhy)] : std::string();
                if (w.find("not a check") != std::string::npos || w.find("Not a check") != std::string::npos ||
                    w.find("the two sweeps differ") != std::string::npos)
                    continue;            // no check, or an unsettled one: says nothing against the needle
            }
            chk[k].push_back(q);
        }
        for (auto& kv : chk)
            std::stable_sort(kv.second.begin(), kv.second.end(), [](const Chk& a, const Chk& b) { return a.at < b.at; });
    }
    const std::vector<AirRec> airs = readAirChecks(m_records + "/needle_cal.csv");
    const QcLimits q = qcLimitsFor(m_s.trainLot, kCheck9);
    int n = 0;
    for (MatSweep& s : m_matLib.sweeps) {
        if (s.qc != "suspect" || s.needle.empty() || !std::isfinite(s.gpS)) continue;
        if (matTipFault(s.pts, s.refR, kTipBridgedRatio, nullptr) != TipFault::None) continue;
        if ((std::isfinite(q.gpMaxS) && s.gpS > q.gpMaxS) || !needleLeakWhy(s.needle, s.gpS).empty()) continue;
        double dR = kNaN, dZ = kNaN;
        if (std::sscanf(s.qcMove.c_str(), "R %lf %%, tip surface %lf %%", &dR, &dZ) == 2) {
            dR /= 100; dZ /= 100;
        } else if (!s.qcMove.empty()) {
            continue;                    // H7-2.15.0 on: marked for a fouled tip or a check refused, which stand
        } else {
            const auto it = chk.find(needleKey(s.needle));
            const int64_t t = isoMs(s.when);
            if (it == chk.end() || t <= 0) continue;
            const Chk* before = nullptr;
            const Chk* after = nullptr;
            bool broken = false;
            for (const Chk& c : it->second) {
                if (c.at <= t) { if (c.ok) { before = &c; broken = false; } else if (before) broken = true; continue; }
                if (!c.ok) { broken = true; break; }
                after = &c;
                break;
            }
            if (!before || !after || broken) continue;
            // an Air check of the needle between the sweep and the check that closed it: its tip may have been found
            // fouled (a fouled tip asks for both checks again, and condemns the sweeps since the last one) - they stand
            bool air = false;
            for (const AirRec& r : airs) if (r.key == it->first && r.at > t && r.at < after->at) { air = true; break; }
            if (air) continue;
            dR = std::fabs(after->r / before->r - 1);
            dZ = std::fabs(after->ze / before->ze - 1);
        }
        if (!(dR <= q.maxBracketR)) continue;
        s.qc = dZ <= q.maxBracketZe ? "ok" : "ok-raw";
        s.qcMove = fmt("R %.1f %%, tip surface %.0f %% (judged again, H7-2.15.0)", dR * 100, dZ * 100);
        ++n;
    }
    if (n) {
        ++m_matRev;
        if (!saveMaterials()) storageFailed("The material library could NOT be written to records/materials.txt.");
        event(fmt("training: %d suspect sweep%s judged again by the H7-2.15.0 rules (R within %.0f %% between the checks around "
                  "them, the needle's leakage within its limits): learnt", n, n == 1 ? "" : "s", q.maxBracketR * 100));
    }
    m_qcRejudged = n;
    return n;
}

void App::trainingCheckDone(bool accepted, const ElectrodeModel& eRaw, double r25, double ze, const std::string& why,
                            const QcLimits& q)
{
    const std::string key = needleKey(m_s.trainNeedle);
    if (!accepted) {
        // only unsettled (the two sweeps apart, all else within the limits): repeat it. Out of the limits:
        // the needle is not what it was when its sweeps were taken — they are suspect, and it is refused
        const bool unsettled = q.salineWhy(r25, ze, eRaw.alpha, eRaw.ok ? eRaw.rms : 1.0, 0.0).empty();
        const int n = int(qcOpen().size());
        std::string note;
        if (n && unsettled) note = fmt(" The %d sweep%s since the last check wait for a check that passes.", n, n == 1 ? "" : "s");
        else if (n) {
            qcMarkOpen("suspect", "the next check refused");
            note = fmt(" The %d sweep%s since the last check %s marked SUSPECT and not learnt.", n, n == 1 ? "" : "s", n == 1 ? "is" : "are");
            event("training:" + note);
        }
        if (!unsettled) {
            // H7-2.12.0: a needle refused settles again (two checks that agree) before its next sweeps
            const auto it = m_qcLast.find(key);
            if (it != m_qcLast.end() && it->second.settled) { it->second.settled = false; saveTrainQc(); }
        }
        {
            MatNeedle nd;
            if (m_cal.calibrated(3)) { nd.cpF = m_cal.ranges().at(3).open.cpFarad; nd.gpS = m_cal.ranges().at(3).open.gpSiemens; }
            m_matRefPts = m_current.points;
            recordRefAttempt(m_current.points, nd, false, why);
        }
        if (!unsettled) { m_qc = QcCheck(); m_qcSweeps = 0; }    // refused: no sweep until a check passes
        else m_qc.repeat = true;
        m_matMsg = "Saline check refused: " + why + note + (unsettled ? std::string() : std::string(" If it is refused again, use another needle."));
        m_matMsgOk = false;
        touch();
        return;
    }
    const int64_t now = nowMs();
    std::string note;
    // H7-2.12.0: compared with this needle's last passed check on record (in this session or before it)
    const auto pit = m_qcLast.find(key);
    const bool hasPrev = pit != m_qcLast.end() && pit->second.atMs > 0 && pit->second.atMs < now;
    double dR = kNaN, dZ = kNaN;
    bool same = false;
    if (hasPrev) {
        dR = std::fabs(r25 / pit->second.r25 - 1);
        dZ = std::fabs(ze / pit->second.ze - 1);
        same = dR <= q.maxBracketR && dZ <= q.maxBracketZe;
    }
    // H7-2.12.4: the 2 minutes are counted from the first of the run of checks that agree with each other
    const int64_t anchor = hasPrev && same ? (pit->second.anchorMs > 0 ? pit->second.anchorMs : pit->second.atMs) : now;
    // H7-2.13.0: a needle sweeps from its first passing check on. Since H7-2.12.6 a tip surface that moves between two
    // checks no longer condemns the sweeps between (they are learnt as measured, only not needle-normalised), so a new
    // tip need not settle before its sweeps: waiting for it made Training slow and taught nothing more.
    const bool settled = true;
    const bool newTip = !hasPrev || dZ > q.maxBracketZe;
    (void)anchor;
    // it closes the sweeps since the check before
    {
        const std::vector<size_t> open = qcOpen();
        if (hasPrev && !open.empty()) {
            const QcLast& p = pit->second;
            // H7-2.12.6: its R held: the sweeps are of the material, and learnt as measured. Its tip surface held
            // too: their reference is taken between the two checks, and they are compared needle-normalised as well.
            const bool rHeld = dR <= q.maxBracketR, zHeld = dZ <= q.maxBracketZe;
            for (size_t i : open) {
                MatSweep& s = m_matLib.sweeps[i];
                s.qcMove = fmt("R %.1f %%, tip surface %.0f %%", dR * 100, dZ * 100);
                if (rHeld && !zHeld) {
                    s.qc = "ok-raw";
                } else if (same) {
                    const int64_t t = isoMs(s.when);
                    const double f = now > p.atMs ? std::max(0.0, std::min(1.0, double(t - p.atMs) / double(now - p.atMs))) : 1.0;
                    if (s.needleInfo().hasRef() && p.refA > 0 && eRaw.a > 0) {
                        s.refR = p.refR + f * (eRaw.rOhm - p.refR);
                        s.refA = std::exp(std::log(p.refA) + f * (std::log(eRaw.a) - std::log(p.refA)));
                        s.refAlpha = p.refAlpha + f * (eRaw.alpha - p.refAlpha);
                    }
                    s.qc = "ok";
                } else {
                    s.qc = "suspect";
                }
            }
            const int n = int(open.size());
            note = same ? fmt(" It closes the %d sweep%s since the last check: the needle moved %.1f %% in R and %.0f %% in its tip "
                              "surface (limits %.0f %%, %.0f %%), so they are confirmed and learnt, each with its reference taken "
                              "between the two checks.", n, n == 1 ? "" : "s", dR * 100, dZ * 100, q.maxBracketR * 100,
                              q.maxBracketZe * 100)
                 : rHeld ? fmt(" It closes the %d sweep%s since the last check. Its R held (%.1f %%, limit %.0f %%): they are "
                               "confirmed and learnt as measured. Its tip surface moved %.0f %% (limit %.0f %%), so they are not "
                               "compared needle-normalised.", n, n == 1 ? "" : "s", dR * 100, q.maxBracketR * 100, dZ * 100,
                               q.maxBracketZe * 100)
                         : fmt(" But the needle's R moved %.1f %% since the last check (limit %.0f %%; its tip surface %.0f %%): "
                               "the %d sweep%s since then %s SUSPECT and not learnt.", dR * 100, q.maxBracketR * 100, dZ * 100,
                               n, n == 1 ? "" : "s", n == 1 ? "is" : "are");
            ++m_matRev;
            if (!saveMaterials()) storageFailed("The material library could NOT be written to records/materials.txt.");
            event("training: " + note.substr(1));
        }
    }
    // the needle's reference for the needle-normalised comparison, at the saline's own temperature, as its materials are
    MatNeedle r;
    if (m_cal.calibrated(3)) { r.cpF = m_cal.ranges().at(3).open.cpFarad; r.gpS = m_cal.ranges().at(3).open.gpSiemens; }
    r.refR = eRaw.rOhm; r.refA = eRaw.a; r.refAlpha = eRaw.alpha;
    m_matRef = r;
    m_matRefCalAt = m_cal.takenAt;
    m_matRefNeedle = m_s.trainNeedle;
    m_matRefWhen = isoUtc(now);
    m_matRefPts = m_current.points;
    recordRefAttempt(m_current.points, r, true, std::string());
    m_qc.ok = true;
    m_qc.repeat = false;
    ++m_qcPasses;
    m_qc.settled = settled;
    m_qc.moved = hasPrev ? dZ : kNaN;
    m_qc.movedR = hasPrev ? dR : kNaN;
    m_qc.settleFromMs = anchor;
    m_qc.atMs = now;
    m_qc.calAt = m_cal.takenAt;
    m_qc.needle = m_s.trainNeedle;
    m_qc.r25 = r25; m_qc.ze = ze;
    m_qc.refR = eRaw.rOhm; m_qc.refA = eRaw.a; m_qc.refAlpha = eRaw.alpha;
    m_qcSweeps = 0;
    {
        QcLast& l = m_qcLast[key];
        l.atMs = now; l.r25 = r25; l.ze = ze; l.refR = eRaw.rOhm; l.refA = eRaw.a; l.refAlpha = eRaw.alpha; l.settled = settled;
        l.anchorMs = settled ? now : anchor;
        saveTrainQc();
    }
    const std::string lim = q.fromLot ? "lot " + q.lotId + "'s limits" : std::string("generic limits");
    // H7-2.12.6: a check within its limits says so for the sweeps to come, and what it found of the sweeps before it
    // apart (a needle can be within its limits and still have changed since the check before)
    const bool condemned = note.find("SUSPECT") != std::string::npos;
    if (condemned)
        m_matMsg = fmt("This check itself is within the limits (%s: R %.0f ohm at 25 \xC2\xB0""C, tip surface %.0f ohm at 1 kHz), "
                       "so the needle can go on from here.", lim.c_str(), r25, ze) + note;
    else if (settled)
        m_matMsg = fmt("Saline check passed (%s): R %.0f ohm at 25 \xC2\xB0""C, tip surface %.0f ohm at 1 kHz. The needle may sweep; "
                       "the next check after %d sweeps or %d minutes.", lim.c_str(), r25, ze, m_s.qcSweeps, m_s.qcMinutes) + note +
                   (newTip ? std::string(" A new tip's surface may still change in its first half hour: if the next check finds it "
                                         "moved, the sweeps between are learnt as measured, without the needle-normalised comparison.")
                           : std::string());
    else
        m_matMsg = fmt("Saline check passed (%s): R %.0f ohm at 25 \xC2\xB0""C, tip surface %.0f ohm at 1 kHz. ", lim.c_str(), r25, ze) +
                   qcSettleText(false) + note;
    m_matMsgOk = settled && note.find("SUSPECT") == std::string::npos;
    event("training: " + m_matMsg);
    touch();
}

// ---------------------------------------------------------------------------
// the file

void App::loadMaterials()
{
    m_matLib = MatLibrary();
    ++m_matRev;
    if (!plat::sdReady()) return;
    const std::string t = readAll(m_records + "/materials.txt");
    if (!t.empty() && !m_matLib.load(t)) event("materials.txt could not be read: the library starts empty");
    m_matFileAside.assign(m_matLib.sweeps.size(), 0);
    for (size_t i = 0; i < m_matLib.sweeps.size(); ++i) m_matFileAside[i] = m_matLib.sweeps[i].aside ? 1 : 0;
    m_matFileStale = false;
    loadTrainQc();                      // H7-2.12.0: the needles' last checks, which close their sweeps
    loadLeakBase();                     // H7-2.15.0: each needle's own leakage when sound
    qcRejudge();                        // H7-2.15.0: the suspect sweeps, by today's rules
}

bool App::saveMaterials(bool appendLast)
{
    if (!plat::sdReady()) return false;
    // the classifier's own check, written into the file with every sweep it sets aside
    const MatModel& md = matModel();
    for (size_t i = 0; i < m_matLib.sweeps.size(); ++i)
        m_matLib.sweeps[i].aside = i < md.state.size() && md.state[i] == MatModel::kAsideAuto;
    const std::string path = m_records + "/materials.txt";
    const size_t n = m_matLib.sweeps.size();
    // H7-2.7.2: a sweep added is written by itself at the end of the file. Writing the whole
    // library again each time took seconds on the internal memory (about 600 bytes a sweep,
    // and every 4 kB of it erased first). The file reads back the same either way.
    if (appendLast && n > 0 && m_matFileAside.size() == n - 1 && plat::fileSize(path) > 0) {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        if (f) {
            f << matSweepLine(m_matLib.sweeps.back()) << "\n";
            f.flush();
            if (f.good()) {
                m_matFileAside.push_back(m_matLib.sweeps.back().aside ? 1 : 0);
                // the marks of the sweeps before it, when the fit changed them, are written later (matRewriteIfStale)
                for (size_t i = 0; i + 1 < n; ++i)
                    if ((m_matFileAside[i] != 0) != m_matLib.sweeps[i].aside) { m_matFileStale = true; break; }
                return true;
            }
        }
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << m_matLib.save();
    f.flush();
    if (!f.good()) return false;
    m_matFileAside.assign(n, 0);
    for (size_t i = 0; i < n; ++i) m_matFileAside[i] = m_matLib.sweeps[i].aside ? 1 : 0;
    m_matFileStale = false;
    return true;
}

// H7-2.7.2: the file is written whole again (when nothing is sweeping) only when the marks it holds are no
// longer the classifier's
void App::matRewriteIfStale()
{
    if (m_matTesting || !plat::sdReady() || m_matLib.sweeps.empty()) return;
    if (!m_matFileStale && m_matFileAside.size() == m_matLib.sweeps.size()) {
        const MatModel& md = matModel();
        bool same = true;
        for (size_t i = 0; i < m_matLib.sweeps.size(); ++i) {
            const bool aside = i < md.state.size() && md.state[i] == MatModel::kAsideAuto;
            if ((m_matFileAside[i] != 0) != aside) { same = false; break; }
        }
        if (same) return;
    }
    saveMaterials();
}

// ---------------------------------------------------------------------------
// sweeps

std::string App::matBlockerBase() const
{
    if (!plat::sdReady()) return "There is no records memory to keep the library in.";
    if (caseOpen()) return "A case is open. End it first: material sweeps are not patient readings.";
    // H7-2.9.0: the needle's number before any sweep
    if (m_s.trainNeedle.empty()) return "Enter the needle's number first (NEEDLE, at the top of Training).";
    if (m_calibrating || m_refTaking || m_dipping || m_lotTesting)
        return "A needle calibration, saline check, lot test or healthy-tissue sample is in progress.";
    if (busy() && !m_matTesting) return "Wait until the measurement in progress is over.";
    if (!connected()) return "Nothing is arriving from the measurement unit.";
    return std::string();
}

std::string App::matBlocker() const
{
    const std::string b = matBlockerBase();
    if (!b.empty()) return b;
    // H7-2.10.0: the needle's QC
    if (!m_cal.calibrated(3) || !trainCalMatches() || m_calAtMs <= 0)
        return "Air check this needle first (QC, below): its stray capacitance and leakage against its lot's limits.";
    if (!qcAirOk())
        return fmt("The Air check is %d minutes old (limit %d). Do it again (QC, below), then the saline check, which also "
                   "closes the sweeps since the last one.", qcAirMinutes(), m_s.qcAirMinutes);
    if (!qcCheckOk())
        return m_qc.ok && m_qc.repeat && m_qc.calAt == m_cal.takenAt
            ? "The last saline check was unsettled (its two sweeps apart): leave the tip a minute in the saline and repeat it (QC, below)."
            : "Saline check this needle first (QC, below): the tip in 0.9 % saline, two sweeps.";
    if (m_qcSweeps >= m_s.qcSweeps)
        return fmt("Saline check due: %d sweeps since the last one (one every %d sweeps or %d minutes). The tip in 0.9 %% saline, "
                   "then Saline check.", m_qcSweeps, m_s.qcSweeps, m_s.qcMinutes);
    if (qcMinutesSince() >= m_s.qcMinutes)
        return fmt("Saline check due: %d minutes since the last one (one every %d sweeps or %d minutes). The tip in 0.9 %% saline, "
                   "then Saline check.", qcMinutesSince(), m_s.qcSweeps, m_s.qcMinutes);
    return std::string();
}

// H7-2.13.0
App::NeedleStep App::matNextStep() const
{
    if (!matBlockerBase().empty()) return m_s.trainNeedle.empty() && matBlockerBase().find("needle's number") != std::string::npos
                                              ? NeedleStep::Number : NeedleStep::None;
    if (!m_cal.calibrated(3) || !trainCalMatches() || m_calAtMs <= 0 || !qcAirOk()) return NeedleStep::Air;
    if (!qcCheckOk() || m_qcSweeps >= m_s.qcSweeps || qcMinutesSince() >= m_s.qcMinutes) return NeedleStep::Saline;
    return NeedleStep::None;
}

// label "" identifies the sweep; newSample: the sweep begins a new placement of its material
std::string App::matStart(const std::string& label, bool newSample)
{
    if (m_matTesting) return "A sweep is already running.";
    const std::string why = matBlocker();
    if (!why.empty()) return why;
    if (!label.empty()) {
        if (!m_matLib.hasLabel(label)) return "No such label.";
        if (m_matLib.sweeps.size() >= kMaxSweeps)
            return "The library is full (2000 sweeps). Delete a label, or the last sweeps, first.";
    }
    m_matLabel = label;
    m_matPending = false;
    m_matRefSweep = false;
    m_matMsg.clear();
    m_matNewSample = newSample;
    m_matTesting = true;
    m_matMsgOk = true;
    // H7-2.4.1: the tip settles in the material as in a saline (Settings -> Lots)
    if (m_s.settleS > 0) {
        m_matStage = MatStage::Settling;
        m_matSettleUntil = plat::ticks() + uint32_t(m_s.settleS) * 1000u;
    } else {
        m_matStage = MatStage::Waiting;
        m_matWaitStart = plat::ticks();
        startRun();
    }
    event(label.empty() ? std::string("materials: identify, sweep started") : "materials: sweep of \"" + label + "\" started");
    touch();
    return std::string();
}

void App::matStop(const std::string& why)
{
    if (!m_matTesting) return;
    if (m_matStage == MatStage::Waiting || m_matStage == MatStage::Sweeping) stopRun();
    m_matTesting = false;
    m_matStage = MatStage::Idle;
    m_matRefSweep = false;
    if (!why.empty()) { m_matMsg = why; m_matMsgOk = false; event("materials: " + why); }
    touch();
}

int App::matSecondsLeft() const
{
    if (m_matStage == MatStage::Settling)
        return std::max(0, (int(int32_t(m_matSettleUntil - plat::ticks())) + 999) / 1000);
    const int limit = (m_matStage == MatStage::Sweeping) ? kMatSweepMs : kMatStartMs;
    const int waited = int(uint32_t(plat::ticks() - m_matWaitStart));
    return std::max(0, (limit - waited + 999) / 1000);
}

void App::matSweepStarted()
{
    if (m_matTesting && m_matStage == MatStage::Waiting) {
        m_matStage = MatStage::Sweeping;
        m_matWaitStart = plat::ticks();
    }
}

void App::pollMat(uint32_t now)
{
    if (!m_matTesting) return;
    if (m_matStage == MatStage::Settling) {
        if (int32_t(now - m_matSettleUntil) >= 0 && !busy()) {
            m_matWaitStart = now;
            m_matStage = MatStage::Waiting;
            startRun();
        } else if (int32_t(now - m_matSettleUntil) >= kMatStartMs) {
            // H7-2.11.2: the unit stayed busy after the settling: the sweep cannot start
            matStop("No sweep started: the measurement unit stayed busy. Check the cable, then try again.");
        }
    } else if (m_matStage == MatStage::Waiting || m_matStage == MatStage::Sweeping) {
        const int limit = (m_matStage == MatStage::Sweeping) ? kMatSweepMs : kMatStartMs;
        if (uint32_t(now - m_matWaitStart) >= uint32_t(limit))
            matStop(m_matStage == MatStage::Sweeping ? "The sweep began but never finished. Check the cable, then try again."
                                                     : "No sweep started. The measurement unit is not answering.");
    }
    static uint32_t lastSec = 0;
    if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
}

void App::finishMatSweep()
{
    m_matTesting = false;
    m_matStage = MatStage::Idle;
    // H7-2.9.0: only the Training needle's own Air check and reference are applied to its sweeps
    const MatNeedle needle = trainingNeedle();
    const double cp = needle.cpF;
    const std::vector<Point> pts = m_current.points;
    m_matLastPts = pts;
    m_matLastCp = cp;
    m_matLastNeedle = needle;
    m_matPending = false;
    if (matFeatures(pts, cp).empty()) {
        m_matMsg = "The sweep lacks some of the 15 frequencies: nothing was kept. Measure again.";
        m_matMsgOk = false;
        m_matRefSweep = false;
        return;
    }
    // H7-2.11.0: a shorted or bridged tip measures itself, not the material
    {
        std::string tw;
        if (matTipFault(pts, needle.refR, kTipBridgedRatio, &tw) != TipFault::None) {
            m_matRefSweep = false;
            trainingTipFault(tw);
            return;
        }
        // H7-2.15.0: no tip surface in it, or not measured: not a material; the needle is not at fault
        ElectrodeModel e;
        e.a = needle.refA; e.alpha = needle.refAlpha; e.rOhm = needle.refR; e.ok = needle.hasRef();
        tw = matNotATip(pts, e.ok ? std::fabs(e.zi(1000.0).imag()) : kNaN);
        if (!tw.empty()) {
            m_matRefSweep = false;
            m_matMsg = "Not kept: " + tw + " Put the tip in the material and sweep again.";
            m_matMsgOk = false;
            event("training: sweep refused, " + tw);
            touch();
            return;
        }
    }
    if (m_matRefSweep) {
        // H7-2.6.0: this needle's saline reference
        m_matRefSweep = false;
        MatNeedle r;
        std::string why;
        m_matRefPts = pts;
        const bool okRef = matNeedleRef(pts, needle.cpF, needle.gpS, &r, &why);
        recordRefAttempt(pts, okRef ? r : needle, okRef, why);
        if (!okRef) {
            m_matMsg = "No reference was kept: " + why;
            m_matMsgOk = false;
            event("materials: " + m_matMsg);
            return;
        }
        m_matRef = r;
        m_matRefCalAt = m_cal.takenAt;
        m_matRefNeedle = m_s.trainNeedle;
        m_matRefWhen = isoUtc(nowMs());
        ElectrodeModel e; e.a = r.refA; e.alpha = r.refAlpha; e.rOhm = r.refR; e.ok = true;
        m_matMsg = fmt("This needle's saline reference: %.0f ohm, tip surface %.0f ohm at 1 kHz. Its sweeps from now on (until "
                       "the next Air check) are also compared needle-normalised.", r.refR, e.ze1k());
        m_matMsgOk = true;
        event("materials: " + m_matMsg);
        return;
    }
    if (m_matLabel.empty()) {
        // identify
        ++m_qcSweeps;
        m_matLastMs = nowMs();
        m_matMatch = matClassify(pts, needle);
        if (!m_matMatch.ok) { m_matMsg = m_matMatch.why; m_matMsgOk = false; return; }
        const size_t b = size_t(m_matMatch.best);
        // H7-2.8.0: the final answer (the three together) first, when it can be given
        const MatMatch& mm = m_matMatch;
        if (mm.unlike)
            m_matMsg = fmt("Like none of the labelled materials (nearest: %s).", mm.labels[b].c_str());
        else if (mm.fOk && mm.fBest >= 0)
            m_matMsg = fmt("Final: %s, %.0f %%. By the whole curve %s %.0f %%%s.", mm.labels[size_t(mm.fBest)].c_str(),
                           mm.fProb[size_t(mm.fBest)] * 100, mm.labels[b].c_str(), mm.prob[b] * 100,
                           mm.zOk ? fmt(", by Z and IPS %s %.0f %%", mm.labels[size_t(mm.zBest)].c_str(), mm.zProb[size_t(mm.zBest)] * 100).c_str() : "");
        else
            m_matMsg = fmt("Most like %s: %.0f %%.", mm.labels[b].c_str(), mm.prob[b] * 100);
        if (m_matMatch.rangeDiff)
            m_matMsg += fmt(" Measured on another gain range than the sweeps of %s at %d frequenc%s: the phase there may step.",
                            m_matMatch.labels[b].c_str(), m_matMatch.rangeDiff, m_matMatch.rangeDiff == 1 ? "y" : "ies");
        m_matPending = true;
        m_matMsgOk = !m_matMatch.unlike;
        event("materials: identify: " + m_matMsg);
        return;
    }
    MatSweep w;
    w.label = m_matLabel;
    w.when = isoUtc(nowMs());
    w.needle = m_s.trainNeedle;
    w.setNeedle(needle);
    w.settleS = m_s.settleS;
    w.pts = pts;
    int maxSample = 0;
    for (const MatSweep& s : m_matLib.sweeps) maxSample = std::max(maxSample, s.sample);
    // one placement: sweeps of the same label one after the other, unless a new placement was asked for
    const bool same = !m_matNewSample && !m_matLib.sweeps.empty() && m_matLib.sweeps.back().label == m_matLabel &&
                      uint32_t(plat::ticks() - m_matLastAt) < kSameSampleMs;
    w.sample = same ? m_matLib.sweeps.back().sample : maxSample + 1;
    w.qc = "pending";
    ++m_qcSweeps;
    m_matLib.sweeps.push_back(w);
    m_matLastAt = plat::ticks();
    m_matLastLabel = m_matLabel;
    ++m_matRev;
    if (!saveMaterials(true)) storageFailed("The material library could NOT be written to records/materials.txt.");
    const std::string aside = asideNote(matModel(), m_matLib, m_matLib.sweeps.size() - 1);
    m_matMsg = fmt("Kept as \"%s\" (%s, sweep %d of it, needle %s).%s", m_matLabel.c_str(), same ? "same placement" : "new placement",
                   m_matLib.count(m_matLabel), m_s.trainNeedle.c_str(),
                   std::isfinite(cp) ? "" : " No Air check: the cable's capacitance is in it.");
    m_matMsgOk = true;
    event("materials: " + m_matMsg + aside);
}

// H7-2.5.1: the identified sweep, told what it was, is learnt (with what the classifier had said of it)
std::string App::matKeepLast(const std::string& label0)
{
    if (!m_matPending || m_matTesting) return "There is no identified sweep to label.";
    const std::string label = cleanName(label0);
    if (!m_matLib.hasLabel(label)) return "No such label.";
    if (m_matLib.sweeps.size() >= kMaxSweeps)
        return "The library is full (2000 sweeps). Delete a label, or the last sweeps, first.";
    MatSweep w;
    w.label = label;
    w.when = isoUtc(nowMs());
    w.needle = m_s.trainNeedle;
    w.setNeedle(m_matLastNeedle);
    w.settleS = m_s.settleS;
    w.pts = m_matLastPts;
    int maxSample = 0;
    for (const MatSweep& s : m_matLib.sweeps) maxSample = std::max(maxSample, s.sample);
    w.sample = maxSample + 1;
    if (m_matMatch.ok && m_matMatch.best >= 0) {
        const std::string best = m_matMatch.labels[size_t(m_matMatch.best)];
        const bool known = std::find(m_matMatch.labels.begin(), m_matMatch.labels.end(), label) != m_matMatch.labels.end();
        w.said = m_matMatch.unlike ? std::string("(none)") : best;
        w.saidP = m_matMatch.prob[size_t(m_matMatch.best)];
        w.saidRight = m_matMatch.unlike ? (known ? 0 : 1) : (best == label ? 1 : 0);
    }
    recordSaidFinal(m_matMatch, label, &w);
    w.qc = "pending";
    m_matLib.sweeps.push_back(w);
    m_matPending = false;
    m_matLastAt = plat::ticks();
    m_matLastLabel = label;
    ++m_matRev;
    if (!saveMaterials(true)) return "The library could NOT be written to the records memory.";
    std::string had;
    if (w.saidRight >= 0)
        had = w.said == "(none)" ? std::string(" It had said: like none of them") + (w.saidRight ? " (right: a new material)." : " (wrong).")
                                 : fmt(" It had said %s, %.0f %% (%s).", w.said.c_str(), w.saidP * 100, w.saidRight ? "right" : "wrong");
    const std::string fin = saidFinalText(w);
    const std::string aside = asideNote(matModel(), m_matLib, m_matLib.sweeps.size() - 1);
    m_matMsg = "Kept as \"" + label + "\"." + had + fin;
    m_matMsgOk = true;
    event("materials: identified sweep labelled \"" + label + "\"." + had + fin + aside);
    touch();
    return std::string();
}

std::string App::matAddLabel(const std::string& label0)
{
    const std::string label = cleanName(label0);
    if (label.empty()) return "Type the material's name.";
    if (label.size() > 40) return "At most 40 characters.";
    if (m_matLib.hasLabel(label)) return "\"" + label + "\" is already a label.";
    m_matLib.labels.push_back(label);
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event("materials: label \"" + label + "\" added");
    touch();
    return std::string();
}

std::string App::matDeleteLabel(const std::string& label)
{
    if (m_matTesting) return "A sweep is running.";
    if (!m_matLib.hasLabel(label)) return "No such label.";
    const int n = m_matLib.count(label);
    m_matLib.labels.erase(std::find(m_matLib.labels.begin(), m_matLib.labels.end(), label));
    m_matLib.off.erase(std::remove(m_matLib.off.begin(), m_matLib.off.end(), label), m_matLib.off.end());
    m_matLib.sweeps.erase(std::remove_if(m_matLib.sweeps.begin(), m_matLib.sweeps.end(),
                                         [&](const MatSweep& s) { return s.label == label; }), m_matLib.sweeps.end());
    ++m_matRev;
    m_matMatch = MatMatch();
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event(fmt("materials: label \"%s\" deleted with its %d sweeps", label.c_str(), n));
    touch();
    return std::string();
}

// H7-2.12.0: a material left out of the comparison: kept with its sweeps, neither learnt nor answered
std::string App::matSetOff(const std::string& label, bool off)
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    if (!m_matLib.hasLabel(label)) return "No such label.";
    if (off == m_matLib.isOff(label)) return std::string();
    if (off) m_matLib.off.push_back(label);
    else m_matLib.off.erase(std::remove(m_matLib.off.begin(), m_matLib.off.end(), label), m_matLib.off.end());
    ++m_matRev;
    m_matMatch = MatMatch();
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event(fmt("materials: \"%s\" %s the comparison", label.c_str(), off ? "left out of" : "back in"));
    touch();
    return std::string();
}

std::string App::matDeleteLast()
{
    if (m_matTesting) return "A sweep is running.";
    if (m_matLib.sweeps.empty()) return "The library has no sweeps.";
    const MatSweep s = m_matLib.sweeps.back();
    m_matLib.sweeps.pop_back();
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event("materials: the last sweep (\"" + s.label + "\", " + s.when + ") deleted");
    touch();
    return "The last sweep (" + s.label + ") is deleted.";
}

// H7-2.11.0: the Training needle's tip found shorted or bridged in a sweep: the sweep is not kept, the
// sweeps since its last saline check are suspect (when it fouled is not known), and both checks are due again
void App::trainingTipFault(const std::string& why)
{
    const int n = qcMarkOpen("suspect", "the tip fouled");
    {   // H7-2.12.0: cleaned, the tip settles again (two checks that agree) before its next sweeps
        const auto it = m_qcLast.find(needleKey(m_s.trainNeedle));
        if (it != m_qcLast.end() && it->second.settled) { it->second.settled = false; saveTrainQc(); }
    }
    m_qc = QcCheck();
    m_qcSweeps = 0;
    m_calAtMs = 0;
    m_matPending = false;
    m_matMsg = "Not kept: " + why + " Clean the tip (rinse it in water, wipe it, let it dry), then its Air check and saline check." +
               (n == 1 ? std::string(" The sweep since the last check is marked SUSPECT and not learnt: when the tip fouled is not known.")
                : n ? fmt(" The %d sweeps since the last check are marked SUSPECT and not learnt: when the tip fouled is not known.", n)
                    : std::string());
    m_matMsgOk = false;
    event("training: sweep refused, " + why + (n ? fmt(" (%d sweeps since the last check marked suspect)", n) : std::string()));
    touch();
}

std::string App::matDeleteTipFaults()
{
    if (m_matTesting || m_dipping) return "A sweep is running.";
    int sh = 0, br = 0;
    std::vector<MatSweep> keep;
    for (const MatSweep& sw : m_matLib.sweeps) {
        const TipFault t = matTipFault(sw.pts, sw.refR, kTipBridgedRatio, nullptr);
        if (t == TipFault::Short) ++sh;
        else if (t == TipFault::Bridged) ++br;
        else keep.push_back(sw);
    }
    if (!sh && !br) return "No sweep of the library was taken with a shorted or bridged tip.";
    m_matLib.sweeps = keep;
    m_matPending = false;
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    event(fmt("materials: %d sweeps of a shorted tip and %d of a bridged tip deleted", sh, br));
    touch();
    return fmt("%d sweeps of a shorted or bridged tip are deleted (%d shorted, %d bridged); %d are left.", sh + br, sh, br,
               int(m_matLib.sweeps.size()));
}

// ---------------------------------------------------------------------------
// H7-2.5.3: readings of the logs tagged afterwards with what they were. H7-2.9.0: Logs tags a whole case
// (one lesion, from several placements): every reading of it that passed its checks joins the library,
// all of them one placement, so that the classifier's test leaves the whole case out together.

CaseTag App::caseTag(const std::string& caseId) const
{
    if (m_caseTagsRev != m_matRev) {
        m_caseTags.clear();
        std::map<std::string, std::set<std::string>> labels;
        for (const MatSweep& s : m_matLib.sweeps) {
            if (s.origin.empty() || !(s.device.empty() || s.device == m_s.deviceId)) continue;
            const size_t k = s.origin.find('/');
            if (k == std::string::npos) continue;
            const std::string c = s.origin.substr(0, k);
            ++m_caseTags[c].tagged;
            labels[c].insert(s.label);
        }
        for (const auto& l : labels) {
            CaseTag& t = m_caseTags[l.first];
            t.mixed = l.second.size() > 1;
            if (l.second.size() == 1) t.label = *l.second.begin();
        }
        m_caseTagsRev = m_matRev;
    }
    const auto it = m_caseTags.find(caseId);
    return it == m_caseTags.end() ? CaseTag() : it->second;
}

std::string App::readingTag(const std::string& caseId, int site, int64_t epochMs) const
{
    if (m_readingTagsRev != m_matRev) {
        m_readingTags.clear();
        for (const MatSweep& s : m_matLib.sweeps)
            if (!s.origin.empty() && (s.device.empty() || s.device == m_s.deviceId)) m_readingTags[s.origin] = s.label;
        m_readingTagsRev = m_matRev;
    }
    const auto it = m_readingTags.find(readingKey(caseId, site, epochMs));
    return it == m_readingTags.end() ? std::string() : it->second;
}

// The sweeps of a case's readings as they were measured (records/reading_raw.txt), by reading
std::map<std::string, MatSweep> App::rawSweepsOf(const std::string& caseId) const
{
    std::map<std::string, MatSweep> raw;
    const std::string key = caseId + "/";
    std::ifstream f(m_records + "/reading_raw.txt", std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2 || line.compare(0, 2, "S=") != 0) continue;
        if (line.find("|" + key) == std::string::npos) continue;
        MatSweep s;
        if (!matParseSweep(line.substr(2), &s) || s.origin.compare(0, key.size(), key) != 0) continue;
        raw[s.origin] = s;
    }
    return raw;
}

// A reading recorded before H7-2.5.3 has no sweep as measured: the one kept in sweeps/ (as it was corrected)
bool App::keptSweepOf(const std::string& caseId, int site, int64_t epochMs, MatSweep* w) const
{
    std::ifstream f(m_records + "/sweeps/" + caseId + "_" + std::to_string(site) + "_" + std::to_string(epochMs) + ".csv");
    std::string line;
    bool header = true;
    while (std::getline(f, line)) {
        if (header) { header = false; continue; }
        Point p;
        double fq = 0, m = 0, ph = 0;
        int range = 0;
        if (std::sscanf(line.c_str(), "%lf,%lf,%lf,%d", &fq, &m, &ph, &range) >= 3) {
            p.freqHz = fq; p.magOhm = m; p.phaseDeg = ph; p.range = range;
            w->pts.push_back(p);
        }
    }
    w->origin = readingKey(caseId, site, epochMs);
    w->when = isoUtc(epochMs);
    w->settleS = -1;
    return !w->pts.empty();
}

// H7-2.5.3: one reading (Logs before H7-2.9.0; no longer offered on the screen, kept for the PC tests)
std::string App::tagReading(const std::string& caseId, int site, int64_t epochMs, const std::string& label0)
{
    if (!plat::sdReady()) return "There is no records memory to keep the library in.";
    if (m_matTesting) return "A sweep is running.";
    const std::string label = cleanName(label0);
    const std::string k = readingKey(caseId, site, epochMs);
    const std::string key = caseId + "/";
    auto mine = [&](const MatSweep& s) { return s.origin == k && (s.device.empty() || s.device == m_s.deviceId); };
    auto it = std::find_if(m_matLib.sweeps.begin(), m_matLib.sweeps.end(), mine);
    if (label.empty()) {
        if (it == m_matLib.sweeps.end()) return "This reading has no tag.";
        const std::string was = it->label;
        m_matLib.sweeps.erase(it);
        ++m_matRev;
        if (!saveMaterials()) return "The library could NOT be written to the records memory.";
        m_matMsg = fmt("Reading %d of %s is no longer tagged: it left the library.", site, caseId.c_str());
        m_matMsgOk = true;
        event(fmt("materials: reading %d of %s untagged (was \"%s\")", site, caseId.c_str(), was.c_str()));
        touch();
        return std::string();
    }
    if (!m_matLib.hasLabel(label)) return "No such label.";
    // the case's placement: the one its other readings have, else a new one
    int sample = -1, maxSample = 0;
    for (const MatSweep& s : m_matLib.sweeps) {
        maxSample = std::max(maxSample, s.sample);
        if (sample < 0 && s.origin.compare(0, key.size(), key) == 0 && (s.device.empty() || s.device == m_s.deviceId)) sample = s.sample;
    }
    if (sample < 0) sample = maxSample + 1;
    if (it != m_matLib.sweeps.end()) {
        it->label = label;
        it->sample = sample;
        if (it->saidRight >= 0 && it->said != "(none)") it->saidRight = it->said == label;
        if (it->saidFRight >= 0 && it->saidF != "(none)") it->saidFRight = it->saidF == label;
        ++m_matRev;
        if (!saveMaterials()) return "The library could NOT be written to the records memory.";
        m_matMsg = fmt("Reading %d of %s is now tagged \"%s\".", site, caseId.c_str(), label.c_str());
        m_matMsgOk = true;
        event(fmt("materials: reading %d of %s tagged \"%s\"", site, caseId.c_str(), label.c_str()));
        touch();
        return std::string();
    }
    if (m_matLib.sweeps.size() >= kMaxSweeps) return "The library is full (2000 sweeps). Delete a label, or the last sweeps, first.";
    const std::map<std::string, MatSweep> raw = rawSweepsOf(caseId);
    MatSweep w;
    bool older = false;
    const auto r = raw.find(k);
    if (r != raw.end()) w = r->second;
    else older = keptSweepOf(caseId, site, epochMs, &w);
    if (matFeatures(w.pts, w.cpF).empty()) return "The sweep of this reading is not in the records (or lacks some of the 15 frequencies).";
    w.label = label;
    w.sample = sample;
    std::string had;
    if (matModel().ok) {
        const MatMatch m = matClassify(w.pts, w.needleInfo());
        if (m.ok && m.best >= 0) {
            const bool known = std::find(m.labels.begin(), m.labels.end(), label) != m.labels.end();
            w.said = m.unlike ? std::string("(none)") : m.labels[size_t(m.best)];
            w.saidP = m.prob[size_t(m.best)];
            w.saidRight = m.unlike ? (known ? 0 : 1) : (w.said == label ? 1 : 0);
            had = m.unlike ? std::string(" The classifier had said: like none of them.")
                           : fmt(" The classifier had said %s, %.0f %% (%s).", w.said.c_str(), w.saidP * 100, w.saidRight ? "right" : "wrong");
        }
        recordSaidFinal(m, label, &w);
    }
    m_matLib.sweeps.push_back(w);
    ++m_matRev;
    if (!saveMaterials(true)) return "The library could NOT be written to the records memory.";
    m_matMsg = fmt("Reading %d of %s tagged \"%s\" and learnt.", site, caseId.c_str(), label.c_str()) + had +
               (older ? " It was recorded before H7-2.5.3: its sweep is the one kept in sweeps/ (corrected for its needle, if it was), "
                        "and its needle's Cp is not known." : "");
    m_matMsgOk = true;
    event(fmt("materials: reading %d of %s tagged \"%s\"", site, caseId.c_str(), label.c_str()));
    touch();
    return std::string();
}

// H7-2.9.0: the whole case; label "" takes it out again
std::string App::tagCase(const std::string& caseId, const std::string& label0)
{
    if (!plat::sdReady()) return "There is no records memory to keep the library in.";
    if (m_matTesting) return "A sweep is running.";
    const std::string label = cleanName(label0);
    const std::string key = caseId + "/";
    auto ofCase = [&](const MatSweep& s) {
        return s.origin.compare(0, key.size(), key) == 0 && (s.device.empty() || s.device == m_s.deviceId);
    };
    int had = 0;
    for (const MatSweep& s : m_matLib.sweeps) if (ofCase(s)) ++had;
    if (label.empty()) {
        if (!had) return "This case has no tag.";
        m_matLib.sweeps.erase(std::remove_if(m_matLib.sweeps.begin(), m_matLib.sweeps.end(), ofCase), m_matLib.sweeps.end());
        ++m_matRev;
        if (!saveMaterials()) return "The library could NOT be written to the records memory.";
        m_matMsg = fmt("%s is no longer tagged: its %d reading%s left the library.", caseId.c_str(), had, had == 1 ? "" : "s");
        m_matMsgOk = true;
        event("materials: " + m_matMsg);
        touch();
        return std::string();
    }
    if (!m_matLib.hasLabel(label)) return "No such label.";
    // the case's placement: the one its readings already have, else a new one
    int sample = -1, maxSample = 0;
    for (const MatSweep& s : m_matLib.sweeps) {
        maxSample = std::max(maxSample, s.sample);
        if (sample < 0 && ofCase(s)) sample = s.sample;
    }
    if (sample < 0) sample = maxSample + 1;
    const std::map<std::string, MatSweep> raw = rawSweepsOf(caseId);
    std::set<std::string> already;
    for (const MatSweep& s : m_matLib.sweeps) if (ofCase(s)) already.insert(s.origin);
    const std::vector<LogReading> rs = caseReadingsLog(caseId);
    std::string needle;
    {
        const std::map<std::string, std::string> row = caseDetails(caseId);
        const auto n = row.find("needle_serial");
        if (n != row.end()) needle = n->second;
    }
    std::vector<MatSweep> add;
    int notPassed = 0, noSweep = 0, older = 0;
    for (const LogReading& r : rs) {
        const std::string k = readingKey(caseId, r.site, r.epochMs);
        if (already.count(k)) continue;
        if (!r.usable) { ++notPassed; continue; }
        MatSweep w;
        const auto it = raw.find(k);
        if (it != raw.end()) w = it->second;
        else if (keptSweepOf(caseId, r.site, r.epochMs, &w)) ++older;
        if (matFeatures(w.pts, w.cpF).empty()) { ++noSweep; continue; }
        if (w.needle.empty()) w.needle = needle;
        w.label = label;
        w.sample = sample;
        w.settleS = -1;
        add.push_back(w);
    }
    if (m_matLib.sweeps.size() + add.size() > kMaxSweeps)
        return "The library is full (2000 sweeps). Delete a label, or the last sweeps, first.";
    // what the classifier said of them before they are learnt: a test on sweeps it has not seen
    int told = 0, right = 0, toldF = 0, rightF = 0;
    if (matModel().ok)
        for (MatSweep& w : add) {
            const MatMatch m = matClassify(w.pts, w.needleInfo());
            if (m.ok && m.best >= 0) {
                const bool known = std::find(m.labels.begin(), m.labels.end(), label) != m.labels.end();
                w.said = m.unlike ? std::string("(none)") : m.labels[size_t(m.best)];
                w.saidP = m.prob[size_t(m.best)];
                w.saidRight = m.unlike ? (known ? 0 : 1) : (w.said == label ? 1 : 0);
                ++told;
                right += w.saidRight;
            }
            recordSaidFinal(m, label, &w);
            if (w.saidFRight >= 0) { rightF += w.saidFRight; ++toldF; }
        }
    // its readings tagged before (one by one, or with another label) are brought under this one
    int retagged = 0;
    for (MatSweep& s : m_matLib.sweeps) {
        if (!ofCase(s) || (s.label == label && s.sample == sample)) continue;
        s.label = label;
        s.sample = sample;
        if (s.saidRight >= 0 && s.said != "(none)") s.saidRight = s.said == label;
        if (s.saidFRight >= 0 && s.saidF != "(none)") s.saidFRight = s.saidF == label;
        ++retagged;
    }
    if (add.empty() && !retagged) {
        if (had) {
            m_matMsg = fmt("%s is tagged \"%s\" already (%d reading%s).", caseId.c_str(), label.c_str(), had, had == 1 ? "" : "s");
            m_matMsgOk = true;
            return std::string();
        }
        return notPassed && !noSweep ? "None of its readings passed their checks: there is nothing to learn from this case."
                                     : "The sweeps of its readings are not in the records (or lack some of the 15 frequencies).";
    }
    for (const MatSweep& w : add) m_matLib.sweeps.push_back(w);
    ++m_matRev;
    if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    const int n = had + int(add.size());
    std::string msg = fmt("%s tagged \"%s\": %d reading%s learnt as one sample (placements of one lesion).", caseId.c_str(),
                          label.c_str(), n, n == 1 ? "" : "s");
    if (retagged) msg += fmt(" %d already tagged now %s too.", retagged, label.c_str());
    if (notPassed) msg += fmt(" %d that did not pass their checks left out.", notPassed);
    if (noSweep) msg += fmt(" %d without a complete sweep in the records left out.", noSweep);
    if (older) msg += fmt(" %d recorded before H7-2.5.3: their sweeps are those kept in sweeps/, without their needle's Cp.", older);
    if (told) {
        msg += fmt(" Before learning them the classifier said %d of %d right", right, told);
        msg += toldF ? fmt(" (final answer %d of %d).", rightF, toldF) : std::string(".");
    }
    m_matMsg = msg;
    m_matMsgOk = true;
    event("materials: " + msg);
    touch();
    return std::string();
}

// ---------------------------------------------------------------------------
// H7-2.5.3: the training data to USB and from it

std::string App::matExport()
{
    if (!plat::usbReady()) return "No USB stick is mounted (" + plat::usbStatus() + "). Plug one in and press this again.";
    if (m_matLib.sweeps.empty() && m_matLib.labels.empty()) return "The material library is empty: there is nothing to export.";
    const std::string dev = m_s.deviceId.empty() ? std::string("NODEV") : m_s.deviceId;
    MatLibrary out = m_matLib;
    out.device = dev;
    out.exported = isoUtc(nowMs());
    for (MatSweep& s : out.sweeps) {
        if (s.device.empty()) s.device = dev;          // whose sweep it is, wherever it goes
        if (s.origSample < 0) s.origSample = s.sample;
    }
    const std::string name = "BIRADx_materials_" + dev + "_" + localText(nowMs(), "%Y%m%d-%H%M") + ".txt";
    {
        std::ofstream f(plat::usbRoot() + "/" + name, std::ios::binary | std::ios::trunc);
        if (!f) return "The file could NOT be written to the USB stick.";
        f << out.save();
        f.flush();
        if (!f.good()) return "The file could NOT be written to the USB stick (is it full?).";
    }
    plat::flushAll();
    event(fmt("materials: library exported to %s (%d sweeps, %d labels)", name.c_str(), int(out.sweeps.size()), int(out.labels.size())));
    return fmt("Written to the USB stick: %s \xE2\x80\x94 %d sweeps of %d labels, the tagged readings among them. On another device: "
               "Import from USB. It is ADDED to that device's library; nothing there is deleted. It is safe to remove the stick.",
               name.c_str(), int(out.sweeps.size()), int(out.labels.size()));
}

std::string App::matImport()
{
    if (!plat::usbReady()) return "No USB stick is mounted (" + plat::usbStatus() + "). Plug one in and press this again.";
    if (!plat::sdReady()) return "There is no records memory to keep the library in.";
    if (m_matTesting) return "A sweep is running.";
    const std::string own = m_s.deviceId.empty() ? std::string("NODEV") : m_s.deviceId;
    auto devOf = [&](const MatSweep& s) { return s.device.empty() ? own : s.device; };
    auto origOf = [](const MatSweep& s) { return s.origSample >= 0 ? s.origSample : s.sample; };
    // what is here already
    std::map<std::string, std::string> here;        // sweep -> its label
    for (const MatSweep& s : m_matLib.sweeps) here[sweepKey(devOf(s), s)] = s.label;
    std::map<std::pair<std::string, int>, int> place;   // (device, its placement there) -> placement here
    int maxSample = 0;
    for (const MatSweep& s : m_matLib.sweeps) {
        maxSample = std::max(maxSample, s.sample);
        place[std::make_pair(devOf(s), origOf(s))] = s.sample;
    }
    int files = 0, added = 0, dup = 0, other = 0, newLabels = 0, full = 0;
    std::string bad;
    std::vector<std::string> names;
    plat::listDir(plat::usbRoot(), [&](const std::string& name, bool dir, long) {
        if (!dir && name.compare(0, 17, "BIRADx_materials_") == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".txt")
            names.push_back(name);
    });
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        MatLibrary in;
        if (!in.load(readAll(plat::usbRoot() + "/" + name))) { bad += " " + name; continue; }
        ++files;
        const std::string fileDev = in.device.empty() ? std::string("?") : in.device;
        for (const std::string& l : in.labels)
            if (!m_matLib.hasLabel(l)) { m_matLib.labels.push_back(l); ++newLabels; }
        for (const MatSweep& s0 : in.sweeps) {
            MatSweep s = s0;
            const std::string dev = s.device.empty() ? fileDev : s.device;
            const std::string k = sweepKey(dev, s);
            const auto h = here.find(k);
            if (h != here.end()) { if (h->second == s.label) ++dup; else ++other; continue; }
            if (m_matLib.sweeps.size() >= kMaxSweeps) { ++full; continue; }
            const auto pk = std::make_pair(dev, origOf(s));
            const auto pi = place.find(pk);
            const int local = pi != place.end() ? pi->second : ++maxSample;
            place[pk] = local;
            s.origSample = origOf(s);
            s.sample = local;
            s.device = dev == own ? std::string() : dev;
            s.aside = false;
            if (!m_matLib.hasLabel(s.label)) { m_matLib.labels.push_back(s.label); ++newLabels; }
            m_matLib.sweeps.push_back(s);
            here[k] = s.label;
            ++added;
        }
    }
    if (!files)
        return bad.empty() ? std::string("No material library file (BIRADx_materials_\xE2\x80\xA6.txt) is on the USB stick.")
                           : "No usable material library file on the stick:" + bad;
    const bool changed = added || newLabels;
    if (changed) {
        ++m_matRev;
        m_matMatch = MatMatch();
        if (!saveMaterials()) return "The library could NOT be written to the records memory.";
    }
    event(fmt("materials: imported %d sweeps and %d labels from %d file(s); %d already here", added, newLabels, files, dup + other));
    touch();
    std::string msg = fmt("Added %d sweep%s and %d new label%s from %d file%s on the stick; %d already here were not added again.",
                          added, added == 1 ? "" : "s", newLabels, newLabels == 1 ? "" : "s", files, files == 1 ? "" : "s", dup + other);
    if (other) msg += fmt(" %d of them have another label here: they keep this device's.", other);
    if (full) msg += fmt(" %d did not fit: the library is full (2000 sweeps).", full);
    if (!bad.empty()) msg += " Not readable:" + bad + ".";
    return msg + (changed ? " Nothing was deleted; the classifier is learnt again." : " Nothing was deleted.");
}

} // namespace app
