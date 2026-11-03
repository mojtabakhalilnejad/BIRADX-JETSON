#include "opencal.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdarg>
#include <cstdio>

namespace birx {
namespace {
constexpr double kPi = 3.14159265358979323846;

std::string csvQuote(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}

std::vector<std::string> csvSplit(const std::string& line) {
    std::vector<std::string> out;
    std::string cur; bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (q) { if (c == '"') { if (i+1 < line.size() && line[i+1]=='"') { cur += '"'; ++i; } else q = false; } else cur += c; }
        else if (c == '"') q = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool toD(const std::string& s, double* v) {
    if (s.empty()) return false;
    // MCU port: see dataset.cpp toDouble.
    double d = 0; if (!birxStrtod(s, &d)) return false; *v = d; return std::isfinite(d);
}
} // namespace

double ParasiticModel::reactanceAtOhm(double freqHz) const {
    if (!ok || !(cpFarad > 0) || freqHz <= 0) return kNaN;
    return 1.0 / (2.0 * kPi * freqHz * cpFarad);
}

double phaseErrorDeg(double dCpFarad, double sampleOhm, double freqHz) {
    // Y = 1/R + j*w*dCp  ->  arg(Z) = -atan(w*R*dCp)
    const double w = 2.0 * kPi * freqHz;
    return -std::atan(w * sampleOhm * dCpFarad) * 180.0 / kPi;
}

// ---------------------------------------------------------------------------

bool NeedleCalibration::addOpenSweep(int range, const Spectrum& s,
                                     const OpenCalConfig& cfg, std::string* why) {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    /* EVERY REFUSAL CARRIES ITS NUMBERS.
     *
     * The messages here used to be bare sentences — "something is bridging
     * the tip", "too few measurable points" — handed to an operator standing
     * at a trolley with a needle in their hand and no way to tell a dirty tip
     * from a long cable from a loose connector. The numbers that produced the
     * refusal are the only thing that distinguishes them, they cost nothing
     * to print, and without them the same message is the answer to several
     * different faults. */
    auto say = [](const char* fmt, ...) {
        char buf[400];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        return std::string(buf);
    };

    if (range < 1 || range > 6)
        return fail(say("gain range %d is out of bounds", range));

    /* THE BRIDGE TEST, at the bottom of the sweep. See OpenCalConfig.
     *
     * An open circuit's impedance is largest at the lowest frequency it was
     * measured at; a bridge is low everywhere. So the lowest frequency in the
     * sweep is where the two part company, and it is the only place this
     * question is asked. */
    double lowestHz = 0, zAtLowest = kNaN;
    for (const Point& p : s.points) {
        if (!(p.freqHz > 0) || !std::isfinite(p.magOhm)) continue;
        if (lowestHz == 0 || p.freqHz < lowestHz) { lowestHz = p.freqHz; zAtLowest = p.magOhm; }
    }
    if (lowestHz > 0 && std::isfinite(zAtLowest) && zAtLowest < cfg.minOpenOhm)
        return fail(say("the tip is not open: |Z| is only %.0f ohm at %.0f Hz, where an "
                        "open needle would be tens of kilohms. Something is bridging the "
                        "tip — dry it, and hold it clear of your hand and of any metal.",
                        zAtLowest, lowestHz));

    // Y = 1/Z. For an ideal open, Im(Y) = w*Cp and Re(Y) = Gp, so Cp comes
    // from a regression of Im(Y) on w through the origin — one parameter, from
    // only the frequencies where the open is actually measurable.
    double sww = 0, swy = 0, gsum = 0;
    int n = 0, inBand = 0, tooNoisy = 0, tooLow = 0;
    std::vector<std::pair<double,double>> used;     // (w, ImY)

    for (const Point& p : s.band(cfg.minFitHz, 1e9)) {
        ++inBand;
        if (p.magOhm > cfg.maxOpenOhm)   { ++tooNoisy; continue; }  // noise
        /* Below the instrument's own floor, and therefore dropped — NOT a
         * reason to refuse the calibration. A capacitive needle reaches this
         * at the top of its own sweep by definition, and whether the sweep
         * really is a parasitic is decided by the fit below. */
        if (p.magOhm < cfg.minUsableOhm) { ++tooLow; continue; }
        const double th = p.phaseDeg * kPi / 180.0;
        const std::complex<double> z = std::polar(p.magOhm, th);
        if (std::abs(z) < 1e-9) continue;
        const std::complex<double> y = 1.0 / z;
        const double w = 2.0 * kPi * p.freqHz;
        sww += w * w;
        swy += w * y.imag();
        gsum += y.real();
        used.emplace_back(w, y.imag());
        ++n;
    }

    if (n < cfg.minFitPoints)
        return fail(say("only %d of the %d points above %.0f kHz were usable (%d too "
                        "noisy, %d below %.0f ohm); %d are needed. Check that the needle "
                        "is fully seated in the connector.",
                        n, inBand, cfg.minFitHz / 1000.0, tooNoisy, tooLow,
                        cfg.minUsableOhm, cfg.minFitPoints));
    if (sww <= 0) return fail("degenerate frequency set");

    ParasiticModel m;
    m.cpFarad   = swy / sww;
    m.gpSiemens = gsum / n;
    m.nPoints   = n;

    double ss = 0, norm = 0;
    for (const auto& kv : used) {
        const double pred = m.cpFarad * kv.first;
        ss += (pred - kv.second) * (pred - kv.second);
        norm += kv.second * kv.second;
    }
    m.rmsRel = (norm > 0) ? std::sqrt(ss / norm) : kNaN;

    if (!(m.cpFarad >= cfg.minCpFarad && m.cpFarad <= cfg.maxCpFarad))
        return fail(say("the fitted stray capacitance is %.0f pF, outside the %.0f–%.0f pF "
                        "a needle and its cable can plausibly be. A figure near zero "
                        "usually means the tip is loaded; a very large one, a damaged "
                        "cable.",
                        m.cpFarad * 1e12, cfg.minCpFarad * 1e12, cfg.maxCpFarad * 1e12));
    if (std::isfinite(m.rmsRel) && m.rmsRel > cfg.maxRmsRel)
        return fail(say("the sweep does not fit a simple stray capacitance: %.0f%% "
                        "residual against a limit of %.0f%%, on %d points. The tip is "
                        "picking something up — hold it clear and try again.",
                        m.rmsRel * 100.0, cfg.maxRmsRel * 100.0, n));

    m.ok = true;
    RangeCal& rc = m_byRange[range];
    rc.range = range;
    rc.open  = m;
    if (why) why->clear();
    return true;
}

bool NeedleCalibration::addShortSweep(int range, const Spectrum& s) {
    // Z_short = R + j*w*L. Regress Im(Z) on w, average Re(Z).
    double sww = 0, swy = 0, rsum = 0;
    int n = 0;
    for (const Point& p : s.band(10000.0, 1e9)) {
        if (p.magOhm > 50.0) continue;                 // not a short
        const double th = p.phaseDeg * kPi / 180.0;
        const std::complex<double> z = std::polar(p.magOhm, th);
        const double w = 2.0 * kPi * p.freqHz;
        sww += w * w; swy += w * z.imag(); rsum += z.real();
        ++n;
    }
    if (n < 3 || sww <= 0) return false;
    RangeCal& rc = m_byRange[range];
    rc.range = range;
    rc.seriesLHenry = swy / sww;
    rc.seriesROhm   = rsum / n;
    rc.haveShort    = true;
    return true;
}

bool NeedleCalibration::calibrated(int range) const {
    auto it = m_byRange.find(range);
    return it != m_byRange.end() && it->second.open.ok;
}

void NeedleCalibration::setOpen(int range, double cpFarad, double gpSiemens) {
    RangeCal& rc = m_byRange[range];
    rc.range = range;
    rc.open = ParasiticModel();
    rc.open.cpFarad = cpFarad;
    rc.open.gpSiemens = std::isfinite(gpSiemens) ? gpSiemens : 0.0;
    rc.open.ok = std::isfinite(cpFarad);
    rc.open.rmsRel = 0;
    rc.open.nPoints = 0;
}

int NeedleCalibration::embed(Spectrum* s) const {
    if (!s) return 0;
    int untouched = 0;
    for (Point& p : s->points) {
        auto it = m_byRange.find(p.range);
        if (it == m_byRange.end() || !it->second.open.ok) { ++untouched; continue; }
        const RangeCal& rc = it->second;
        const double w  = 2.0 * kPi * p.freqHz;
        std::complex<double> z = std::polar(p.magOhm, p.phaseDeg * kPi / 180.0);
        const std::complex<double> yOpen(rc.open.gpSiemens, w * rc.open.cpFarad);
        const std::complex<double> den = 1.0 + z * yOpen;
        if (std::abs(den) < 1e-12) { ++untouched; continue; }
        z /= den;
        if (rc.haveShort) z += std::complex<double>(rc.seriesROhm, w * rc.seriesLHenry);
        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || std::abs(z) <= 0) { ++untouched; continue; }
        p.magOhm   = std::abs(z);
        p.phaseDeg = std::arg(z) * 180.0 / kPi;
    }
    return untouched;
}

int NeedleCalibration::correct(Spectrum* s) const {
    if (!s) return 0;
    int uncorrected = 0;
    for (Point& p : s->points) {
        auto it = m_byRange.find(p.range);
        if (it == m_byRange.end() || !it->second.open.ok) { ++uncorrected; continue; }
        const RangeCal& rc = it->second;

        const double w  = 2.0 * kPi * p.freqHz;
        const double th = p.phaseDeg * kPi / 180.0;
        std::complex<double> z = std::polar(p.magOhm, th);

        if (rc.haveShort) z -= std::complex<double>(rc.seriesROhm, w * rc.seriesLHenry);

        const std::complex<double> yOpen(rc.open.gpSiemens, w * rc.open.cpFarad);
        const std::complex<double> den = 1.0 - z * yOpen;
        if (std::abs(den) < 1e-12) { ++uncorrected; continue; }
        z /= den;

        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || std::abs(z) <= 0) {
            ++uncorrected; continue;
        }
        p.magOhm   = std::abs(z);
        p.phaseDeg = std::arg(z) * 180.0 / kPi;
    }
    return uncorrected;
}

// ---------------------------------------------------------------------------

std::string NeedleCalibration::logHeader() {
    return "needle_id,batch_id,needle_rev,taken_at,operator,range,"
           "cp_pf,gp_us,rms_rel,n_points,z_open_500k_ohm";
}

bool NeedleCalibration::appendToLog(const std::string& path) const {
    const bool fresh = !std::ifstream(path).good();
    std::ofstream f(path, std::ios::app);
    if (!f) return false;
    if (fresh) f << logHeader() << "\n";
    f << std::fixed;
    for (const auto& kv : m_byRange) {
        const RangeCal& rc = kv.second;
        if (!rc.open.ok) continue;
        f << csvQuote(needleId) << ',' << csvQuote(batchId) << ','
          << csvQuote(needleRev) << ',' << csvQuote(takenAt) << ','
          << csvQuote(operatorId) << ',' << rc.range << ','
          << std::setprecision(3) << rc.open.cpFarad * 1e12 << ','
          << std::setprecision(4) << rc.open.gpSiemens * 1e6 << ','
          << std::setprecision(5) << rc.open.rmsRel << ','
          << rc.open.nPoints << ','
          << std::setprecision(1) << rc.open.reactanceAtOhm(500000.0) << '\n';
    }
    return f.good();
}

bool NeedleCalibration::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(12);
    f << "# birx needle calibration v1\n";
    f << "needle: " << needleId << "\n";
    f << "batch: "  << batchId  << "\n";
    f << "rev: "    << needleRev << "\n";
    f << "taken: "  << takenAt  << "\n";
    f << "operator: " << operatorId << "\n";
    for (const auto& kv : m_byRange) {
        const RangeCal& rc = kv.second;
        if (!rc.open.ok) continue;
        f << "open: " << rc.range << " " << rc.open.cpFarad << " " << rc.open.gpSiemens
          << " " << rc.open.rmsRel << " " << rc.open.nPoints << "\n";
        if (rc.haveShort)
            f << "short: " << rc.range << " " << rc.seriesROhm << " " << rc.seriesLHenry << "\n";
    }
    return f.good();
}

bool NeedleCalibration::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    m_byRange.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t c = line.find(':');
        if (c == std::string::npos) continue;
        const std::string key = line.substr(0, c);
        std::istringstream v(line.substr(c + 1));
        if      (key == "needle")   v >> needleId;
        else if (key == "batch")    v >> batchId;
        else if (key == "rev")      v >> needleRev;
        else if (key == "taken")    v >> takenAt;
        else if (key == "operator") v >> operatorId;
        else if (key == "open") {
            int r = 0; ParasiticModel m;
            v >> r >> m.cpFarad >> m.gpSiemens >> m.rmsRel >> m.nPoints;
            if (r >= 1 && r <= 6 && m.cpFarad > 0) {
                m.ok = true;
                m_byRange[r].range = r;
                m_byRange[r].open  = m;
            }
        } else if (key == "short") {
            int r = 0; double rr = 0, ll = 0;
            v >> r >> rr >> ll;
            if (r >= 1 && r <= 6) {
                m_byRange[r].range = r;
                m_byRange[r].seriesROhm = rr;
                m_byRange[r].seriesLHenry = ll;
                m_byRange[r].haveShort = true;
            }
        }
    }
    return !m_byRange.empty();
}

std::string NeedleCalibration::describe() const {
    std::ostringstream o;
    o << (needleId.empty() ? "(unidentified needle)" : needleId);
    if (!batchId.empty()) o << " batch " << batchId;
    o << " — ";
    if (m_byRange.empty()) { o << "not calibrated"; return o.str(); }
    bool first = true;
    for (const auto& kv : m_byRange) {
        if (!kv.second.open.ok) continue;
        if (!first) o << ", ";
        o << "r" << kv.first << " " << std::fixed << std::setprecision(1)
          << kv.second.open.cpFarad * 1e12 << " pF";
        first = false;
    }
    return o.str();
}

// ---------------------------------------------------------------------------

std::vector<CalLogRow> readCalLog(const std::string& path, std::string* error) {
    std::vector<CalLogRow> out;
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return out; }
    std::string line;
    if (!std::getline(f, line)) { if (error) *error = "empty log"; return out; }
    const std::vector<std::string> head = csvSplit(line);
    auto col = [&](const char* n) -> int {
        for (size_t i = 0; i < head.size(); ++i) if (head[i] == n) return int(i);
        return -1;
    };
    const int cN = col("needle_id"), cB = col("batch_id"), cR = col("needle_rev"),
              cT = col("taken_at"), cRg = col("range"), cCp = col("cp_pf"),
              cGp = col("gp_us"), cRms = col("rms_rel"), cNp = col("n_points");
    if (cN < 0 || cCp < 0 || cRg < 0) { if (error) *error = "log is missing required columns"; return out; }

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> c = csvSplit(line);
        auto at = [&](int i) { return (i >= 0 && i < int(c.size())) ? c[i] : std::string(); };
        CalLogRow r;
        r.needleId  = at(cN);
        r.batchId   = at(cB);
        r.needleRev = at(cR);
        r.takenAt   = at(cT);
        double d = 0;
        if (toD(at(cRg), &d))  r.range = int(d);
        if (toD(at(cCp), &d))  r.cpFarad = d * 1e-12;
        if (toD(at(cGp), &d))  r.gpSiemens = d * 1e-6;
        if (toD(at(cRms), &d)) r.rmsRel = d;
        if (toD(at(cNp), &d))  r.nPoints = int(d);
        if (!r.needleId.empty() && std::isfinite(r.cpFarad)) out.push_back(r);
    }
    return out;
}

} // namespace birx
