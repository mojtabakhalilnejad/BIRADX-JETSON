#include "lotfit.h"
#include "opencal.h"
#include "calib.h"

#include <complex>

namespace { constexpr double kPi = 3.14159265358979323846; }

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace birx {

// ---------------------------------------------------------------------------
const std::vector<LotPhantom>& lotPhantoms()
{
    static const std::vector<LotPhantom> p = {
        { "Saline 0.9 %",   "sterile normal saline as supplied", 15.9 },
        { "Saline 0.225 %", "1 part 0.9 % saline + 3 parts sterile water, by volume, mixed", 4.3 },
        { "Saline 0.09 %",  "1 part 0.9 % saline + 9 parts sterile water, by volume, mixed", 1.8 },
    };
    return p;
}

const char* lotStepName(int step)
{
    if (step <= 0) return "Air";
    const auto& p = lotPhantoms();
    return step <= int(p.size()) ? p[size_t(step - 1)].name : "?";
}

std::string lotStepHowTo(int step)
{
    if (step <= 0)
        return "The needle DRY, tip in air, at least 100 mm from hands and metal, its cable lying as it "
               "will during a case. Nothing touches the tip.";
    const LotPhantom& p = lotPhantoms()[size_t(step - 1)];
    return std::string(p.name) + " (" + p.prepare + "), at least 30 mm deep in a clean cup. Dip the tip "
           "about 10 mm, so that all of its bare metal is under the surface, away from the sides and the "
           "bottom, and hold it still; no bubble on the tip. Enter the solution's temperature (or its "
           "conductivity, if a meter is at hand). Afterwards rinse the tip in sterile water and dab it dry.";
}

double lotNormaliseZ(double z1k, int step, double tempC, double condMsCm)
{
    if (!std::isfinite(z1k)) return kNaN;
    if (step >= 1 && step <= int(lotPhantoms().size()) && std::isfinite(condMsCm) && condMsCm > 0)
        return z1k * condMsCm / lotPhantoms()[size_t(step - 1)].nominalMsCm;
    if (std::isfinite(tempC)) return z1k * (1.0 + 0.02 * (tempC - 25.0));
    return z1k;
}

// ---------------------------------------------------------------------------
int LotRun::okCount(int needle, int step) const
{
    int n = 0;
    for (const LotTest& t : tests) if (t.ok && t.needle == needle && t.step == step) ++n;
    return n;
}

int LotRun::failCount(int needle, int step) const
{
    int n = 0;
    for (const LotTest& t : tests) if (!t.ok && !t.removed && t.needle == needle && t.step == step) ++n;
    return n;
}

bool LotRun::needleGivenUp(int needle) const
{
    if (reference) return false;
    for (int s = 0; s <= plan.phantoms; ++s)
        if (okCount(needle, s) < plan.repeats && failCount(needle, s) >= kGiveUp) return true;
    return false;
}

bool LotRun::needleDone(int needle) const
{
    if (needleGivenUp(needle)) return true;
    for (int s = 0; s <= plan.phantoms; ++s) if (okCount(needle, s) < plan.repeats) return false;
    return true;
}

bool LotRun::next(int* needle, int* step) const
{
    for (int n = 1; n <= plan.needles; ++n) {
        if (needleGivenUp(n)) continue;
        for (int s = 0; s <= plan.phantoms; ++s)
            if (okCount(n, s) < plan.repeats) { if (needle) *needle = n; if (step) *step = s; return true; }
    }
    return false;
}

bool LotRun::complete() const { return !next(nullptr, nullptr); }

int LotRun::doneTests() const
{
    int n = 0;
    for (int k = 1; k <= plan.needles; ++k) {
        const bool gone = needleGivenUp(k);
        for (int s = 0; s <= plan.phantoms; ++s) n += gone ? plan.repeats : std::min(okCount(k, s), plan.repeats);
    }
    return n;
}

int LotRun::plannedTests() const { return plan.needles * (plan.phantoms + 1) * plan.repeats; }

bool LotRun::needleAir(int needle, double* cpF, double* gpS) const
{
    double c = 0, g = 0;
    int n = 0;
    for (const LotTest& t : tests)
        if (t.ok && t.needle == needle && t.step == 0 && std::isfinite(t.cpF)) {
            c += t.cpF; g += std::isfinite(t.gpS) ? t.gpS : 0.0; ++n;
        }
    if (!n) return false;
    if (cpF) *cpF = c / n;
    if (gpS) *gpS = g / n;
    return true;
}

namespace {
std::string clean(const std::string& s)
{
    std::string o;
    for (char c : s) o.push_back((c == '|' || c == '\n' || c == '\r' || c == ';' || c == ':') ? '/' : c);
    return o;
}
std::string num(double v)
{
    if (!std::isfinite(v)) return "nan";
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
double rd(const std::string& s)
{
    if (s.empty() || s == "nan") return kNaN;
    char* e = nullptr;
    const double v = std::strtod(s.c_str(), &e);
    return (e && e != s.c_str()) ? v : kNaN;
}
std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    out.push_back(cur);
    return out;
}
double mean(const std::vector<double>& v)
{
    if (v.empty()) return kNaN;
    double s = 0; for (double x : v) s += x; return s / double(v.size());
}
double sd(const std::vector<double>& v)
{
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / double(v.size() - 1));
}
uint32_t crc32(const std::string& t)
{
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : t) {
        c ^= ch;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
} // namespace

std::string LotRun::save() const
{
    std::ostringstream o;
    o << "BIRADX-LOTRUN 1\n"
      << "lot=" << clean(lotId) << "\n"
      << "reference=" << (reference ? 1 : 0) << "\n"
      << "control=" << (control ? 1 : 0) << "\n"
      << "created=" << clean(created) << "\n"
      << "plan=" << plan.needles << "|" << plan.repeats << "|" << plan.phantoms << "\n";
    for (const LotTest& t : tests) {
        o << "T=" << t.needle << "|" << t.step << "|" << t.repeat << "|" << (t.ok ? 1 : 0) << "|"
          << num(t.cpF) << "|" << num(t.gpS) << "|" << num(t.z1k) << "|" << num(t.ips) << "|"
          << num(t.ipsFit) << "|" << num(t.tempC) << "|" << num(t.condMsCm) << "|" << num(t.zNorm) << "|"
          << clean(t.when) << "|" << clean(t.why) << "|";
        for (size_t i = 0; i < t.raw.size(); ++i)
            o << (i ? ";" : "") << num(t.raw[i].freqHz) << ":" << num(t.raw[i].magOhm) << ":"
              << num(t.raw[i].phaseDeg) << ":" << t.raw[i].range;
        // H7-2.4.1: removed by hand (1: it had been refused, 2: accepted), and how long the tip soaked
        o << "|" << (t.removed ? (t.removedOk ? 2 : 1) : 0) << "|" << t.soakS << "\n";
    }
    return o.str();
}

bool LotRun::load(const std::string& text)
{
    *this = LotRun();
    std::istringstream in(text);
    std::string line;
    bool magic = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "BIRADX-LOTRUN 1") { magic = true; continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "lot") lotId = v;
        else if (k == "reference") reference = v == "1";
        else if (k == "control") control = v == "1";
        else if (k == "created") created = v;
        else if (k == "plan") {
            const auto f = split(v, '|');
            if (f.size() >= 3) {
                plan.needles = std::max(1, std::min(20, std::atoi(f[0].c_str())));
                plan.repeats = std::max(1, std::min(10, std::atoi(f[1].c_str())));
                plan.phantoms = std::max(1, std::min(int(lotPhantoms().size()), std::atoi(f[2].c_str())));
            }
        } else if (k == "T") {
            const auto f = split(v, '|');
            if (f.size() < 14) continue;
            LotTest t;
            t.needle = std::atoi(f[0].c_str()); t.step = std::atoi(f[1].c_str());
            t.repeat = std::atoi(f[2].c_str()); t.ok = f[3] == "1";
            t.cpF = rd(f[4]); t.gpS = rd(f[5]); t.z1k = rd(f[6]); t.ips = rd(f[7]);
            t.ipsFit = rd(f[8]); t.tempC = rd(f[9]); t.condMsCm = rd(f[10]); t.zNorm = rd(f[11]);
            t.when = f[12]; t.why = f[13];
            if (f.size() >= 15 && !f[14].empty())
                for (const std::string& q : split(f[14], ';')) {
                    const auto y = split(q, ':');
                    if (y.size() < 4) continue;
                    Point pt; pt.freqHz = rd(y[0]); pt.magOhm = rd(y[1]); pt.phaseDeg = rd(y[2]);
                    pt.range = std::atoi(y[3].c_str());
                    t.raw.push_back(pt);
                }
            if (f.size() >= 16) {
                const int r = std::atoi(f[15].c_str());
                if (r == 1 || r == 2) { t.removed = true; t.removedOk = r == 2; t.ok = false; }
            }
            if (f.size() >= 17) t.soakS = std::atoi(f[16].c_str());
            tests.push_back(t);
        }
    }
    return magic && !lotId.empty();
}

// ---------------------------------------------------------------------------
const char* lotVerdictName(LotVerdict2 v)
{
    switch (v) {
    case LotVerdict2::Incomplete:            return "Incomplete";
    case LotVerdict2::NoReference:           return "No reference";
    case LotVerdict2::SameAsReference:       return "Same as reference";
    case LotVerdict2::UsableWithCalibration: return "Usable with calibration";
    case LotVerdict2::RangesMustChange:      return "Rule ranges must change";
    case LotVerdict2::UsableWithPerNeedle:   return "Usable, per-needle check";
    case LotVerdict2::Rejected:              return "Rejected";
    }
    return "?";
}

void lotPlain(Spectrum* s, double cpF, double gpS, bool trialCalibrated)
{
    if (!s || !trialCalibrated || !std::isfinite(cpF)) return;
    NeedleCalibration n;
    n.setOpen(3, cpF, gpS);
    n.correct(s);
}

void lotTransfer(Spectrum* s, const LotTransfer& t)
{
    if (!s) return;
    const bool haveNeedle = std::isfinite(t.cpNeedleF), haveRef = std::isfinite(t.cpRefF);
    for (Point& p : s->points) {
        if (!(p.freqHz > 0) || !std::isfinite(p.magOhm) || !std::isfinite(p.phaseDeg)) continue;
        const double w = 2.0 * kPi * p.freqHz;
        std::complex<double> z = std::polar(p.magOhm, p.phaseDeg * kPi / 180.0);
        if (haveNeedle) {
            const std::complex<double> y(std::isfinite(t.gpNeedleS) ? t.gpNeedleS : 0.0, w * t.cpNeedleF);
            const std::complex<double> d = 1.0 - z * y;
            if (std::abs(d) < 1e-9) continue;
            z /= d;
        }
        z *= (std::isfinite(t.kZ) && t.kZ > 0) ? t.kZ : 1.0;
        if (haveRef) {
            const std::complex<double> y(std::isfinite(t.gpRefS) ? t.gpRefS : 0.0, w * t.cpRefF);
            z /= (1.0 + z * y);
        }
        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || std::abs(z) <= 0) continue;
        p.magOhm = std::abs(z);
        p.phaseDeg = std::arg(z) * 180.0 / kPi;
        // the gain range the instrument would have chosen for the reference needle's |Z|
        if (p.range >= 1) {
            int rg = 1;
            for (double th : InstrumentConstants::rangeThresholds()) if (p.magOhm > th) ++rg;
            p.range = rg;
        }
    }
    lotPlain(s, t.cpRefF, t.gpRefS, t.trialCalibrated);
}

namespace {
// per strength: each needle's mean Z (25 °C) and IPS, from a value function
struct StepStats {
    std::vector<double> needleZ, needleIps;
    double worstRepIpsSd = 0, worstRepZCv = 0;
};
// H7-2.5.0: needles set aside are left out, and so are those not in `only` (the needles done,
// while the run is not complete)
template <class F>
StepStats stepStats(const LotRun& r, int step, F value, const std::vector<bool>* only = nullptr)
{
    StepStats st;
    for (int n = 1; n <= r.plan.needles; ++n) {
        if (r.needleGivenUp(n)) continue;
        if (only && (size_t(n) >= only->size() || !(*only)[size_t(n)])) continue;
        std::vector<double> z, ips;
        for (const LotTest& t : r.tests)
            if (t.ok && t.needle == n && t.step == step) {
                double zz = kNaN, ii = kNaN;
                value(r, t, &zz, &ii);
                if (std::isfinite(zz)) z.push_back(zz);
                if (std::isfinite(ii)) ips.push_back(ii);
            }
        if (z.empty() || ips.empty()) continue;
        st.needleZ.push_back(mean(z));
        st.needleIps.push_back(mean(ips));
        st.worstRepIpsSd = std::max(st.worstRepIpsSd, sd(ips));
        st.worstRepZCv = std::max(st.worstRepZCv, mean(z) > 0 ? sd(z) / mean(z) : 0.0);
    }
    return st;
}
std::string f2(const char* fmt, double a, double b = 0, double c = 0, double d = 0)
{
    char buf[700];
    std::snprintf(buf, sizeof buf, fmt, a, b, c, d);
    return buf;
}

// |Z| at the point nearest 100 kHz
double near100kOf(const Spectrum& s)
{
    double best = 1e300, z = kNaN;
    for (const Point& p : s.points) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0)) continue;
        const double d = std::fabs(std::log(p.freqHz / 1e5));
        if (d < best) { best = d; z = p.magOhm; }
    }
    return z;
}

// H7-2.3.0: the mean sweep of one needle (needle 0: all, or those in `only`) at one step, as the
// tip saw it: the unit's and the device's phase corrections taken off, the needle's own stray
// capacitance taken off, the complex Z averaged frequency by frequency. Its points carry range 3,
// on which nothing is corrected.
struct StepMean {
    Spectrum s;
    double kNorm = 1.0;           // the mean factor that brings R to 25 °C
    int    n = 0;                 // sweeps averaged
    std::vector<double> z100k;    // |Z| near 100 kHz of every sweep, refused ones too, in order
};
StepMean stepMean(const LotRun& run, int needle, int step, const RangeModel* ranges,
                  const std::vector<int>* only = nullptr)
{
    StepMean m;
    std::vector<double> fs;
    std::vector<std::complex<double>> acc;
    std::vector<int> cnt;
    double kSum = 0;
    for (const LotTest& t : run.tests) {
        if (t.step != step || t.raw.empty()) continue;
        if (needle != 0 && t.needle != needle) continue;
        if (only && std::find(only->begin(), only->end(), t.needle) == only->end()) continue;
        double cp = kNaN, gp = 0;
        if (!run.needleAir(t.needle, &cp, &gp)) continue;
        Spectrum s;
        s.points = t.raw;
        instrumentToTrue(&s, ranges);
        deembedAll(&s, cp, gp);
        if (t.removed) continue;
        if (!t.ok) { m.z100k.push_back(near100kOf(s)); continue; }
        double z100 = kNaN, best = 1e300;
        for (const Point& p : s.points) {
            if (!(p.freqHz > 0) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
            size_t k = 0;
            for (; k < fs.size(); ++k) if (std::fabs(fs[k] / p.freqHz - 1.0) < 0.02) break;
            if (k == fs.size()) { fs.push_back(p.freqHz); acc.push_back(0.0); cnt.push_back(0); }
            acc[k] += std::polar(p.magOhm, p.phaseDeg * kPi / 180.0);
            ++cnt[k];
            const double d = std::fabs(std::log(p.freqHz / 1e5));
            if (d < best) { best = d; z100 = p.magOhm; }
        }
        m.z100k.push_back(z100);
        kSum += lotNormaliseZ(1.0, step, t.tempC, t.condMsCm);
        ++m.n;
    }
    for (size_t k = 0; k < fs.size(); ++k) {
        const std::complex<double> z = acc[k] / double(cnt[k]);
        Point p;
        p.freqHz = fs[k];
        p.magOhm = std::abs(z);
        p.phaseDeg = std::arg(z) * 180.0 / kPi;
        p.range = 3;
        m.s.points.push_back(p);
    }
    if (m.n) m.kNorm = kSum / m.n;
    return m;
}

// the IPS points (49 kHz and up) of a sweep on gain range 1, where the unit's 500 kHz correction
// lies: an IPS read there is not compared
bool ipsBandOffRange1(const std::vector<Point>& pts)
{
    for (const Point& p : pts) if (p.freqHz >= 49000.0 && p.range == 1) return false;
    return true;
}

} // namespace

RefModel refModel(const LotRun& ref, const RangeModel* ranges)
{
    RefModel m;
    const int w = ref.plan.phantoms;
    if (w < 3) return m;
    std::vector<double> cs, gs;
    for (int n = 1; n <= ref.plan.needles; ++n) {
        double c, g;
        if (ref.needleAir(n, &c, &g)) { cs.push_back(c); gs.push_back(g); }
    }
    m.cpF = mean(cs);
    m.gpS = gs.empty() ? 0.0 : mean(gs);
    const StepMean a = stepMean(ref, 0, w, ranges), b = stepMean(ref, 0, w - 1, ranges);
    if (!a.n || !b.n) return m;
    m.e = fitElectrode(a.s);
    const ElectrodeModel eb = fitElectrode(b.s);
    if (!m.e.ok || !eb.ok) return m;
    m.ok = true;
    m.rNext = eb.rOhm * b.kNorm;
    m.rWeak = m.e.rOhm * a.kNorm;
    m.e.rOhm = m.rWeak;
    const StepMean c = stepMean(ref, 0, kCheck9, ranges);
    if (c.n) {
        m.e9 = fitElectrode(c.s);
        if (m.e9.ok) m.e9.rOhm *= c.kNorm;
    }
    return m;
}

namespace {

double medianOf(std::vector<double> v)
{
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
} // namespace

LotResult evaluateLot(const LotRun& lot, const LotRun* ref, const LotPipeline& pipe,
                      LotFeatureFn feat, void* ctx, const LotLimits& lim)
{
    LotResult r;
    // H7-2.5.0: the needles done (all their steps, or set aside) are judged before the run is complete
    std::vector<bool> done(size_t(lot.plan.needles + 1), true);
    done[0] = false;
    const bool complete = lot.complete();
    if (!complete)
        for (int n = 1; n <= lot.plan.needles; ++n) {
            done[size_t(n)] = lot.needleDone(n);
            if (done[size_t(n)]) ++r.needlesDone;
        }
    const std::vector<bool>* only = (!complete && !lot.reference) ? &done : nullptr;

    std::vector<double> cps, gps;
    for (int n = 1; n <= lot.plan.needles; ++n) {
        if (!done[size_t(n)] || lot.needleGivenUp(n)) continue;
        double c, g;
        if (lot.needleAir(n, &c, &g)) { cps.push_back(c); gps.push_back(g); }
    }
    r.cpMeanF = mean(cps); r.cpSdF = sd(cps); r.gpMeanS = mean(gps);

    // a sweep as the trial would have read it with that needle
    auto plain = [&](const LotRun& run, const LotTest& t, double* z, double* ips) {
        if (t.raw.empty() || !feat) { *z = t.zNorm; *ips = t.ips; return; }
        Spectrum s; s.points = t.raw;
        double cp = kNaN, gp = 0;
        run.needleAir(t.needle, &cp, &gp);
        lotPlain(&s, cp, gp, pipe.trialCalibrated);
        const Features f = feat(s, ctx);
        *z = lotNormaliseZ(f.z1k, t.step, t.tempC, t.condMsCm);
        *ips = f.ipsLegacy;
    };

    if (complete && lot.reference) {
        r.verdict = LotVerdict2::SameAsReference;
        r.headline = "This is the reference: every other lot is compared with it.";
        r.findings.push_back(f2("Stray capacitance %.1f pF (SD %.1f pF between needles).", r.cpMeanF * 1e12, r.cpSdF * 1e12));
        for (int s = 1; s <= lot.plan.phantoms; ++s) {
            const StepStats st = stepStats(lot, s, plain);
            r.findings.push_back(std::string(lotStepName(s)) + f2(": Z %.0f ohm, IPS %+.2f (repeat SD %.2f).",
                                 mean(st.needleZ), mean(st.needleIps), st.worstRepIpsSd));
        }
        return r;
    } else if (!complete && (lot.reference || r.needlesDone == 0 || !ref || !ref->complete())) {
        r.verdict = LotVerdict2::Incomplete;
        r.headline = f2("%.0f of %.0f tests done.", lot.doneTests(), lot.plannedTests()) +
                     (lot.reference ? std::string() : std::string(" Each needle is judged here as soon as all of its steps are done."));
        return r;
    }
    if (!ref || !ref->complete() || ref->plan.phantoms < lot.plan.phantoms) {
        r.verdict = LotVerdict2::NoReference;
        r.headline = "The reference needle has not been measured with these phantoms yet. Measure the "
                     "reference first (Add reference), with at least as many saline strengths.";
        return r;
    }
    double refGp = 0;
    {
        std::vector<double> rc, rg;
        for (int n = 1; n <= ref->plan.needles; ++n) { double c, g; if (ref->needleAir(n, &c, &g)) { rc.push_back(c); rg.push_back(g); } }
        r.refCpF = mean(rc);
        refGp = mean(rg);
    }

    bool inconsistent = false;
    r.findings.push_back(f2("Stray capacitance %.1f pF, SD %.1f pF between needles (limit %.1f); reference %.1f pF.",
                            r.cpMeanF * 1e12, r.cpSdF * 1e12, lim.maxCpSdF * 1e12, r.refCpF * 1e12));
    if (r.cpSdF > lim.maxCpSdF) inconsistent = true;

    // 1. as the trial would have read them: the plain comparison, and the Z factor
    double worstRepIps = 0, worstRepZ = 0, maxPlainShift = 0;
    std::vector<StepStats> refStats;
    for (int s = 1; s <= lot.plan.phantoms; ++s) {
        const StepStats a = stepStats(lot, s, plain, only), b = stepStats(*ref, s, plain);
        refStats.push_back(b);
        LotPoint p;
        p.zLot = mean(a.needleZ); p.ipsLot = mean(a.needleIps);
        p.zRef = mean(b.needleZ); p.ipsRef = mean(b.needleIps);
        p.zCv = p.zLot > 0 ? sd(a.needleZ) / p.zLot : kNaN;
        p.ipsSd = sd(a.needleIps);
        p.kZ = (p.zLot > 0) ? p.zRef / p.zLot : kNaN;
        p.dIps = p.ipsRef - p.ipsLot;
        r.pts.push_back(p);
        worstRepIps = std::max(worstRepIps, a.worstRepIpsSd);
        worstRepZ = std::max(worstRepZ, a.worstRepZCv);
        if (std::isfinite(p.dIps)) maxPlainShift = std::max(maxPlainShift, std::fabs(p.dIps));
        r.findings.push_back(std::string(lotStepName(s)) +
            f2(": lot Z %.0f ohm (CV %.1f %%), reference %.0f ohm -> factor %.3f;", p.zLot, p.zCv * 100, p.zRef, p.kZ) +
            f2(" lot IPS %+.2f (SD %.2f), reference %+.2f -> difference %+.2f.", p.ipsLot, p.ipsSd, p.ipsRef, p.dIps));
        if (!(p.zCv <= lim.maxZCv) || !(p.ipsSd <= lim.maxIpsSd)) inconsistent = true;
    }
    r.worstRepeatIpsSd = worstRepIps;
    r.worstRepeatZCv = worstRepZ;
    r.findings.push_back(f2("Repeatability on one needle: IPS SD up to %.2f (limit %.2f), Z CV up to %.1f %% (limit %.1f %%).",
                            worstRepIps, lim.maxRepeatIpsSd, worstRepZ * 100, lim.maxRepeatZCv * 100));
    if (worstRepIps > 2 * lim.maxRepeatIpsSd || worstRepZ > 2 * lim.maxRepeatZCv) inconsistent = true;

    double lk = 0; int nk = 0;
    for (const LotPoint& p : r.pts) if (p.kZ > 0) { lk += std::log(p.kZ); ++nk; }
    r.kZMean = nk ? std::exp(lk / nk) : kNaN;
    double spread = 0;
    for (const LotPoint& p : r.pts) if (p.kZ > 0) spread = std::max(spread, std::fabs(p.kZ / r.kZMean - 1));
    r.findings.push_back(f2("Cell-constant ratio (reference / lot) %.3f, the same at every strength within %.1f %% (limit %.1f %%).",
                            r.kZMean, spread * 100, lim.maxZFactorSpread * 100));

    // 2. the transfer: each lot sweep made into the reference needle's, then compared again
    r.worstResidIps = 0; r.worstResidZ = 0;
    if (std::isfinite(r.kZMean)) {
        auto moved = [&](const LotRun& run, const LotTest& t, double* z, double* ips) {
            if (t.raw.empty() || !feat) { *z = t.zNorm * r.kZMean; *ips = t.ips; return; }
            Spectrum s; s.points = t.raw;
            LotTransfer tr;
            run.needleAir(t.needle, &tr.cpNeedleF, &tr.gpNeedleS);
            tr.kZ = r.kZMean; tr.cpRefF = r.refCpF; tr.gpRefS = refGp; tr.trialCalibrated = pipe.trialCalibrated;
            lotTransfer(&s, tr);
            const Features f = feat(s, ctx);
            *z = lotNormaliseZ(f.z1k, t.step, t.tempC, t.condMsCm);
            *ips = f.ipsLegacy;
        };
        for (int s = 1; s <= lot.plan.phantoms; ++s) {
            const StepStats a = stepStats(lot, s, moved, only);
            const StepStats& b = refStats[size_t(s - 1)];
            const double di = mean(b.needleIps) - mean(a.needleIps);
            const double dz = mean(a.needleZ) / mean(b.needleZ) - 1;
            r.residIps.push_back(di);
            r.residZ.push_back(dz);
            if (std::isfinite(di)) r.worstResidIps = std::max(r.worstResidIps, std::fabs(di));
            if (std::isfinite(dz)) r.worstResidZ = std::max(r.worstResidZ, std::fabs(dz));
        }
        r.findings.push_back(f2("After the transfer to the reference needle: IPS within %.2f (limit %.2f), Z within %.1f %% "
                                "(limit %.1f %%) at every strength.", r.worstResidIps, lim.maxResidIps,
                                r.worstResidZ * 100, lim.maxResidZ * 100));
    }

    // 3. H7-2.3.0: the needle model — each needle's tip and parasitic, the reference's, the lot's
    const RefModel rm = (lot.plan.phantoms >= 3 && feat) ? refModel(*ref, pipe.ranges) : RefModel();
    if (rm.ok) {
        r.model = true;
        r.eRef = rm.e;
        r.eRef9 = rm.e9;
        const int W = lot.plan.phantoms;            // the weakest saline
        // the reference in the two weakest salines, by step; and where its IPS can be compared
        std::vector<double> refZ(size_t(W + 1), kNaN), refIps(size_t(W + 1), kNaN);
        std::vector<bool> ipsOk(size_t(W + 1), true);
        for (int s = W - 1; s <= W; ++s) {
            refZ[size_t(s)] = mean(refStats[size_t(s - 1)].needleZ);
            refIps[size_t(s)] = mean(refStats[size_t(s - 1)].needleIps);
            for (const LotTest& t : ref->tests)
                if (t.ok && t.step == s && !ipsBandOffRange1(t.raw)) ipsOk[size_t(s)] = false;
        }
        // what is left between a needle's sweeps in the weak salines, made into the reference's, and the reference
        auto residOf = [&](int needle, const NeedleTransfer& tr, double* wIps, double* wZ) {
            *wIps = 0; *wZ = 0;
            for (int s = W - 1; s <= W; ++s) {
                bool useIps = ipsOk[size_t(s)];
                std::vector<double> zs, is;
                for (const LotTest& t : lot.tests) {
                    if (!t.ok || t.step != s || t.needle != needle || t.raw.empty()) continue;
                    Spectrum sp; sp.points = t.raw;
                    needleTransfer(&sp, tr);
                    if (!ipsBandOffRange1(sp.points)) useIps = false;
                    const Features f = feat(sp, ctx);
                    zs.push_back(std::isfinite(f.z1k) ? lotNormaliseZ(f.z1k, s, t.tempC, t.condMsCm) : kNaN);
                    is.push_back(f.ipsLegacy);
                }
                if (zs.empty()) { *wIps = kNaN; *wZ = kNaN; return; }
                double sz = 0; for (double v : zs) sz += v;
                const double mi = is.empty() ? kNaN : [&] { double a = 0; for (double v : is) a += v; return a / double(is.size()); }();
                double dz = std::fabs((sz / double(zs.size())) / refZ[size_t(s)] - 1.0);
                if (!std::isfinite(dz)) dz = 1e9;
                *wZ = std::max(*wZ, dz);
                if (useIps) {
                    double di = std::fabs(refIps[size_t(s)] - mi);
                    if (!std::isfinite(di)) di = 1e9;
                    *wIps = std::max(*wIps, di);
                }
            }
        };

        for (int n = 1; n <= lot.plan.needles; ++n) {
            if (!done[size_t(n)]) continue;
            LotNeedleQc q;
            q.needle = n;
            lot.needleAir(n, &q.cpF, &q.gpS);
            const StepMean a = stepMean(lot, n, W, pipe.ranges);
            const StepMean b = stepMean(lot, n, W - 1, pipe.ranges);
            {
                const StepMean c9 = stepMean(lot, n, kCheck9, pipe.ranges);
                if (c9.n) {
                    q.e9 = fitElectrode(c9.s);
                    if (q.e9.ok) { q.e9.rOhm *= c9.kNorm; q.r9 = q.e9.rOhm; }
                    if (q.r9 > 0 && rm.e9.ok) q.cell9 = rm.e9.rOhm / q.r9;
                }
            }
            q.e = fitElectrode(a.s);
            if (q.e.ok) {
                q.e.rOhm *= a.kNorm;
                q.rWeak = q.e.rOhm;
                q.cellRatio = rm.rWeak / q.e.rOhm;
            }
            const ElectrodeModel eb = fitElectrode(b.s);
            if (eb.ok) q.rNext = eb.rOhm * b.kNorm;
            if (q.rWeak > 0 && q.rNext > 0) q.leakRatio = (q.rWeak / q.rNext) / (rm.rWeak / rm.rNext);
            {
                int k = 0; double sum = 0, lo = 1e300, hi = 0;
                for (double v : a.z100k)
                    if (v > 0) { ++k; sum += v; lo = std::min(lo, v); hi = std::max(hi, v); }
                if (k > 1) q.drift = (hi - lo) / (sum / k);
            }
            char w[320];
            if (lot.needleGivenUp(n)) {
                std::string last, lastStep;
                for (const LotTest& t : lot.tests)
                    if (t.needle == n && !t.ok && !t.removed) { last = t.why; lastStep = lotStepName(t.step); }
                std::snprintf(w, sizeof w, "refused %d times in %s, set aside", LotRun::kGiveUp, lastStep.c_str());
                q.why.push_back(std::string(w) + (last.empty() ? std::string() : " (last: " + last + ")"));
            }
            if (!q.e.ok || q.e.rms > lim.maxModelRms) {
                std::snprintf(w, sizeof w, "its sweeps in %s do not follow the needle model (residual %.3f, limit %.3f)",
                              lotStepName(W), q.e.rms, lim.maxModelRms);
                q.why.push_back(w);
            }
            if (std::isfinite(q.leakRatio) && std::fabs(q.leakRatio - 1.0) > lim.maxLeakDev) {
                std::snprintf(w, sizeof w, "INSULATION LEAK SUSPECTED: in the weakest saline its resistance is %.0f %% %s than "
                              "the stronger one predicts (limit %.0f %%)", std::fabs(q.leakRatio - 1.0) * 100,
                              q.leakRatio < 1 ? "lower" : "higher", lim.maxLeakDev * 100);
                q.why.push_back(w);
            }
            if (std::isfinite(q.drift) && q.drift > lim.maxDrift) {
                std::snprintf(w, sizeof w, "it drifts %.1f %% over its repeats in the weakest saline (limit %.1f %%): liquid "
                              "creeping under the insulation, or a loose tip", q.drift * 100, lim.maxDrift * 100);
                q.why.push_back(w);
            }
            if (q.e.ok && std::isfinite(q.cpF) && !lot.needleGivenUp(n)) {
                NeedleTransfer t;
                t.cpNeedleF = q.cpF; t.gpNeedleS = q.gpS; t.eNeedle = q.e; t.kZ = q.cellRatio;
                t.cpRefF = rm.cpF; t.gpRefS = rm.gpS; t.eRef = rm.e;
                t.trialCalibrated = pipe.trialCalibrated; t.ranges = pipe.ranges;
                residOf(n, t, &q.ownIps, &q.ownZ);
                if (q.e9.ok && rm.e9.ok && q.e9.rms <= lim.maxModelRms) {
                    NeedleTransfer t9 = t;
                    t9.eNeedle = q.e9; t9.kZ = q.cell9; t9.eRef = rm.e9;
                    residOf(n, t9, &q.own9Ips, &q.own9Z);
                }
                if (q.why.empty() && (q.ownIps > lim.maxNeedleResidIps || q.ownZ > lim.maxNeedleResidZ)) {
                    char b2[700];
                    std::snprintf(b2, sizeof b2, "its own model leaves %.2f IPS and %.1f %% in Z (limits %.2f, %.1f %%)",
                                  q.ownIps, q.ownZ * 100, lim.maxNeedleResidIps, lim.maxNeedleResidZ * 100);
                    q.why.push_back(b2);
                }
            }
            r.needleQc.push_back(q);
        }
        // a needle whose tip reads differently from the lot's others: made differently
        {
            std::vector<double> cr;
            for (const LotNeedleQc& q : r.needleQc) if (std::isfinite(q.cellRatio)) cr.push_back(q.cellRatio);
            const double med = medianOf(cr);
            for (LotNeedleQc& q : r.needleQc) {
                if (!std::isfinite(q.cellRatio) || !std::isfinite(med)) continue;
                const double k = q.cellRatio / med;
                if (std::fabs(k - 1.0) > lim.maxCellDev) {
                    char w[240];
                    std::snprintf(w, sizeof w, "its tip reads %.0f %% %s than the other needles' of the lot (limit %.0f %%): made "
                                  "differently", std::fabs(1.0 / k - 1.0) * 100, k < 1 ? "higher" : "lower", lim.maxCellDev * 100);
                    q.why.push_back(w);
                }
            }
        }
        std::vector<int> good;
        for (const LotNeedleQc& q : r.needleQc) if (q.good()) good.push_back(q.needle);
        r.goodNeedles = int(good.size());

        if (!good.empty()) {
            std::vector<double> cp, gp, rw, ze;
            for (const LotNeedleQc& q : r.needleQc) {
                if (!q.good()) continue;
                cp.push_back(q.cpF); gp.push_back(q.gpS); rw.push_back(q.rWeak); ze.push_back(q.e.ze1k());
            }
            r.lotCpF = mean(cp);
            r.lotGpS = mean(gp);
            const StepMean all = stepMean(lot, 0, W, pipe.ranges, &good);
            r.eLot = fitElectrode(all.s);
            if (r.eLot.ok) r.eLot.rOhm = mean(rw);
            r.kZLot = rm.rWeak / mean(rw);
            r.lotResidIps = 0; r.lotResidZ = 0; r.needleResidIps = 0; r.needleResidZ = 0;
            for (LotNeedleQc& q : r.needleQc) {
                if (!q.good()) continue;
                NeedleTransfer t;
                t.cpNeedleF = q.cpF; t.gpNeedleS = q.gpS; t.eNeedle = r.eLot; t.kZ = r.kZLot;
                t.cpRefF = rm.cpF; t.gpRefS = rm.gpS; t.eRef = rm.e;
                t.trialCalibrated = pipe.trialCalibrated; t.ranges = pipe.ranges;
                residOf(q.needle, t, &q.lotIps, &q.lotZ);
                r.lotResidIps = std::max(r.lotResidIps, q.lotIps);
                r.lotResidZ = std::max(r.lotResidZ, q.lotZ);
                r.needleResidIps = std::max(r.needleResidIps, q.ownIps);
                r.needleResidZ = std::max(r.needleResidZ, q.ownZ);
            }
            if (sd(cp) > lim.maxCpSdF) r.lotResidIps = std::max(r.lotResidIps, 1000.0);   // one parasitic does not fit them
            r.dipRMin = (1.0 - lim.dipRWin) * *std::min_element(rw.begin(), rw.end());
            r.dipRMax = (1.0 + lim.dipRWin) * *std::max_element(rw.begin(), rw.end());
            r.dipZeMin = *std::min_element(ze.begin(), ze.end()) * lim.dipZeLo;
            r.dipZeMax = *std::max_element(ze.begin(), ze.end()) * lim.dipZeHi;
            r.valid09 = r.needleResidIps <= lim.maxNeedleResidIps && r.needleResidZ <= lim.maxNeedleResidZ;
            // H7-2.4.0: the saline check with the 0.9 % (from the shelf)
            bool all9 = rm.e9.ok;
            std::vector<double> r9, ze9;
            r.worst9Ips = 0; r.worst9Z = 0;
            for (const LotNeedleQc& q : r.needleQc) {
                if (!q.good()) continue;
                if (!std::isfinite(q.own9Ips) || !std::isfinite(q.own9Z)) { all9 = false; continue; }
                r.worst9Ips = std::max(r.worst9Ips, q.own9Ips);
                r.worst9Z = std::max(r.worst9Z, q.own9Z);
                r9.push_back(q.r9);
                ze9.push_back(q.e9.ze1k());
            }
            if (all9 && !r9.empty()) {
                r.valid9 = r.worst9Ips <= lim.maxNeedleResidIps && r.worst9Z <= lim.maxNeedleResidZ;
                r.dip9RMin = (1.0 - lim.dipRWin) * *std::min_element(r9.begin(), r9.end());
                r.dip9RMax = (1.0 + lim.dipRWin) * *std::max_element(r9.begin(), r9.end());
                r.dip9ZeMin = *std::min_element(ze9.begin(), ze9.end()) * lim.dipZeLo;
                r.dip9ZeMax = *std::max_element(ze9.begin(), ze9.end()) * lim.dipZeHi;
            } else {
                r.valid9 = false;
                r.worst9Ips = kNaN; r.worst9Z = kNaN;
            }
        }
        // H7-2.4.0: a control run — how far the reference repeats by itself between two sessions
        if (lot.control) {
            double ci = 0, cz = 0, cd = 0, cl = 0;
            for (int s = W - 1; s <= W; ++s) {
                bool useIps = ipsOk[size_t(s)];
                for (const LotTest& t : lot.tests)
                    if (t.ok && t.step == s && !ipsBandOffRange1(t.raw)) useIps = false;
                const LotPoint& p = r.pts[size_t(s - 1)];
                if (useIps && std::isfinite(p.dIps)) ci = std::max(ci, std::fabs(p.dIps));
                if (std::isfinite(p.kZ)) cz = std::max(cz, std::fabs(p.kZ - 1.0));
            }
            for (const LotNeedleQc& q : r.needleQc) {
                if (std::isfinite(q.drift)) cd = std::max(cd, q.drift);
                if (std::isfinite(q.leakRatio)) cl = std::max(cl, std::fabs(q.leakRatio - 1.0));
            }
            r.ctrlIps = ci; r.ctrlZ = cz; r.ctrlDrift = cd; r.ctrlLeak = cl;
        }
        r.findings.push_back(f2("Needle model, reference: R %.0f ohm (25 C) in the weakest saline, interface %.0f ohm at 1 kHz "
                                "(exponent %.2f), stray capacitance %.1f pF.", rm.rWeak, rm.e.ze1k(), rm.e.alpha, rm.cpF * 1e12));
        for (const LotNeedleQc& q : r.needleQc) {
            std::string line = f2("Needle %.0f: Cp %.1f pF, R %.0f ohm, interface %.0f ohm at 1 kHz", q.needle, q.cpF * 1e12,
                                  q.rWeak, q.e.ze1k()) +
                               f2(" (exponent %.2f), cell ratio %.3f, leak ratio %.2f, drift %.1f %%", q.e.alpha, q.cellRatio,
                                  q.leakRatio, std::isfinite(q.drift) ? q.drift * 100 : 0.0);
            line += std::isfinite(q.ownIps) ? f2("; own model leaves %.2f IPS, %.1f %% Z", q.ownIps, q.ownZ * 100) : std::string();
            if (q.good()) line += " - good.";
            else {
                line += " - EXCLUDED: ";
                for (size_t i = 0; i < q.why.size(); ++i) line += (i ? "; " : "") + q.why[i];
                line += ".";
            }
            r.findings.push_back(line);
        }
        if (!good.empty()) {
            r.findings.push_back(std::string("Clinic check with the 0.09 % saline: ") +
                (r.valid09 ? f2("validated (each good needle within %.2f IPS, %.1f %% Z).", r.needleResidIps, r.needleResidZ * 100)
                           : f2("NOT validated (%.2f IPS, %.1f %% Z left).", r.needleResidIps, r.needleResidZ * 100)));
            r.findings.push_back(std::string("Clinic check with the 0.9 % saline (sterile, from the shelf): ") +
                (r.valid9 ? f2("validated (each good needle within %.2f IPS, %.1f %% Z in the weaker salines).", r.worst9Ips, r.worst9Z * 100)
                 : !std::isfinite(r.worst9Ips) ? std::string("not possible (the 0.9 % sweeps do not follow the needle model).")
                 : f2("NOT validated (%.2f IPS, %.1f %% Z left in the weaker salines).", r.worst9Ips, r.worst9Z * 100)));
        }
        if (lot.control)
            r.findings.push_back(f2("Control: the reference needle repeats between the two sessions to within %.2f IPS and %.1f %% Z "
                                    "(drift %.1f %%, leak ratio within %.2f).", r.ctrlIps, r.ctrlZ * 100, r.ctrlDrift * 100, r.ctrlLeak));
        if (!good.empty())
            r.findings.push_back(f2("Lot model (%.0f good needles): cell ratio %.3f; it leaves up to %.2f IPS and %.1f %% Z on a needle",
                                    double(good.size()), r.kZLot, std::min(r.lotResidIps, 99.0), r.lotResidZ * 100) +
                                 f2(" (limits %.2f, %.1f %%); each needle's own model up to %.2f IPS, %.1f %% Z.",
                                    lim.maxResidIps, lim.maxResidZ * 100, r.needleResidIps, r.needleResidZ * 100));
    }

    if (r.model) {
        // the findings of the plain transfer are kept for the record; the verdict comes from the model
        std::vector<std::string> keep;
        for (const std::string& f : r.findings)
            if (f.rfind("Cell-constant ratio (reference / lot)", 0) != 0 && f.rfind("After the transfer to the reference needle:", 0) != 0)
                keep.push_back(f);
        r.findings = keep;
        const int nq = int(r.needleQc.size());
        const int bad = nq - r.goodNeedles;
        std::string excluded;
        for (const LotNeedleQc& q : r.needleQc) {
            if (q.good()) continue;
            excluded += (excluded.empty() ? "" : "; ") + f2("needle %.0f: ", q.needle) + q.why.front();
        }
        double weakIps = 0, weakZ = 0;
        for (int s = lot.plan.phantoms - 1; s <= lot.plan.phantoms; ++s) {
            const LotPoint& p = r.pts[size_t(s - 1)];
            if (std::isfinite(p.dIps)) weakIps = std::max(weakIps, std::fabs(p.dIps));
            if (std::isfinite(p.kZ)) weakZ = std::max(weakZ, std::fabs(p.kZ - 1.0));
        }
        if (2 * bad >= nq || r.goodNeedles == 0) {
            r.verdict = LotVerdict2::Rejected;
            r.headline = f2("%.0f of %.0f needles failed their own checks, too many for the lot to be trusted: ", bad, nq) +
                         excluded + ".";
        } else {
            const std::string tail = bad == 0 ? std::string()
                : " Excluded: " + excluded + " - such a needle is refused in the clinic by the saline check.";
            const double k = r.kZLot;
            if (!(k >= lim.minCellRatio) || k > lim.maxCellRatio) {
                r.verdict = LotVerdict2::RangesMustChange;
                char b[400];
                std::snprintf(b, sizeof b, "Not usable with a calibration: its tips read %.0f %% %s than the reference's in saline "
                              "(limit %.0f %%). A tip that different takes in a different volume of tissue, which no saline "
                              "measurement can correct; the rule ranges themselves would have to change for this lot.",
                              std::fabs(1 / k - 1) * 100, k < 1 ? "higher" : "lower", lim.maxZFactorDev * 100);
                r.headline = b + tail;
            } else if (r.lotResidIps > lim.maxResidIps || r.lotResidZ > lim.maxResidZ) {
                if (!r.valid09 && !r.valid9) {
                    r.verdict = LotVerdict2::RangesMustChange;
                    r.headline = f2("Even each needle's own model leaves %.2f IPS and %.1f %% in Z: no calibration carries this "
                                    "lot onto the reference.", r.needleResidIps, r.needleResidZ * 100) + tail;
                } else {
                    r.mode = 1;
                    r.verdict = LotVerdict2::UsableWithPerNeedle;
                    r.headline = f2("Its needles differ too much for one correction, but each needle's own model brings it within "
                                    "%.2f IPS and %.1f %% of the reference. Before every case: Air check, then the tip 10 s in the ",
                                    r.valid9 ? r.worst9Ips : r.needleResidIps, (r.valid9 ? r.worst9Z : r.needleResidZ) * 100) +
                                 (r.valid9 ? (r.valid09 ? "0.9 % or the 0.09 % saline" : "0.9 % saline") : "0.09 % saline") +
                                 " (Saline check)." + tail;
                }
            } else {
                r.mode = 0;
                if (!(weakZ > lim.sameZ) && !(weakIps > lim.sameIps) && !(std::fabs(k - 1.0) > lim.sameZ)) {
                    r.verdict = LotVerdict2::SameAsReference;
                    r.headline = f2("Z within %.1f %%, IPS within %.2f in the weak salines, with nothing corrected.",
                                    weakZ * 100, weakIps) + tail;
                } else {
                    r.verdict = LotVerdict2::UsableWithCalibration;
                    r.headline = f2("One correction for the whole lot (cell ratio %.3f, its interface and parasitic): every good "
                                    "needle comes within %.2f IPS and %.1f %% of the reference.", k, r.lotResidIps,
                                    r.lotResidZ * 100) + tail;
                }
            }
        }
    } else if (inconsistent) {
        r.verdict = LotVerdict2::Rejected;
        r.headline = "The needles of this lot do not agree with each other well enough for any one "
                     "correction to describe them (see the spread figures). Check the needles' construction.";
    } else if (!(spread <= lim.maxZFactorSpread) || !(std::fabs(r.kZMean - 1) <= lim.maxZFactorDev) ||
               !(r.worstResidIps <= lim.maxResidIps) || !(r.worstResidZ <= lim.maxResidZ)) {
        r.verdict = LotVerdict2::RangesMustChange;
        std::string why;
        if (!(std::fabs(r.kZMean - 1) <= lim.maxZFactorDev)) {
            char b[300];
            std::snprintf(b, sizeof b, "its tip reads %.0f %% %s than the reference's in saline (limit %.0f %%): a tip "
                          "that different takes in a different volume of tissue, which no saline measurement can "
                          "correct; ", std::fabs(1 / r.kZMean - 1) * 100, r.kZMean < 1 ? "higher" : "lower",
                          lim.maxZFactorDev * 100);
            why += b;
        }
        if (!(spread <= lim.maxZFactorSpread))
            why += f2("its Z ratio is not one number across the saline strengths (it varies by %.1f %%, limit %.1f %%); ",
                      spread * 100, lim.maxZFactorSpread * 100);
        if (!(r.worstResidIps <= lim.maxResidIps) || !(r.worstResidZ <= lim.maxResidZ))
            why += f2("after the transfer to the reference needle it still differs by %.2f IPS and %.1f %% in Z; ",
                      r.worstResidIps, r.worstResidZ * 100);
        if (why.size() >= 2) why.erase(why.size() - 2);
        r.headline = "Not usable with a calibration alone: " + why + ". The rule ranges themselves would have to change "
                     "for this lot (their equivalents are listed); confirm them with readings before use.";
    } else if (std::fabs(r.kZMean - 1) <= lim.sameZ && spread <= lim.sameZ && maxPlainShift <= lim.sameIps) {
        r.verdict = LotVerdict2::SameAsReference;
        r.headline = f2("Z within %.1f %%, IPS within %.2f, with nothing corrected. It goes "
                        "into the file so that devices recognise it (and remove its small difference in stray "
                        "capacitance).", std::fabs(r.kZMean - 1) * 100, maxPlainShift);
    } else {
        r.verdict = LotVerdict2::UsableWithCalibration;
        r.headline = f2("As measured it differs by up to %.2f IPS and %.1f %% in Z; after the "
                        "transfer to the reference needle", maxPlainShift, std::fabs(r.kZMean - 1) * 100) +
                     f2(" by %.2f IPS and %.1f %%. The rule ranges stay as they are; every device with this file "
                        "corrects the readings of this lot by itself.", r.worstResidIps, r.worstResidZ * 100);
    }

    // H7-2.5.0: not every needle done yet: what the done ones give, marked provisional
    if (!complete) {
        r.provisional = true;
        r.soFar = r.verdict;
        r.verdict = LotVerdict2::Incomplete;
        std::string list;
        for (int n = 1; n <= lot.plan.needles; ++n)
            if (done[size_t(n)]) list += (list.empty() ? "" : ", ") + std::to_string(n);
        r.headline = f2("PROVISIONAL, %.0f of %.0f needles done (needle", r.needlesDone, lot.plan.needles) +
                     (r.needlesDone == 1 ? " " : "s ") + list + "): as far as they go, " + lotVerdictName(r.soFar) + ". " +
                     r.headline + " The verdict is final when every needle is done.";
    }
    // H7-2.4.0: a control run, said in its own words
    if (lot.control && r.model && std::isfinite(r.ctrlIps) && complete) {
        const std::string head = "Control run (the reference needle measured again). ";
        if (r.verdict == LotVerdict2::SameAsReference)
            r.headline = head + f2("It reads as itself: between the two sessions within %.2f IPS and %.1f %% Z.", r.ctrlIps, r.ctrlZ * 100);
        else
            r.headline = head + f2("Between the two sessions it differs by %.2f IPS and %.1f %% Z", r.ctrlIps, r.ctrlZ * 100) +
                         f2(" (drift %.1f %%, leak ratio within %.2f). That is how far the instrument and this needle repeat by "
                            "themselves: use it as the floor (button below), so that no lot is judged tighter than the reference "
                            "repeats. Verdict as a lot: ", r.ctrlDrift * 100, r.ctrlLeak) + lotVerdictName(r.verdict) + ".";
    }
    return r;
}

LotLimits LotLimits::withFloor(double ips, double z, double drift, double leak) const
{
    LotLimits l = *this;
    if (std::isfinite(ips)) {
        ips = std::min(ips, kFloorIpsMax);
        l.sameIps = std::max(l.sameIps, 1.2 * ips);
        l.maxResidIps = std::max(l.maxResidIps, 1.5 * ips);
        l.maxNeedleResidIps = std::max(l.maxNeedleResidIps, 2.0 * ips);
    }
    if (std::isfinite(z)) {
        z = std::min(z, kFloorZMax);
        l.sameZ = std::max(l.sameZ, 1.2 * z);
        l.maxResidZ = std::max(l.maxResidZ, 1.5 * z);
        l.maxNeedleResidZ = std::max(l.maxNeedleResidZ, 2.0 * z);
    }
    if (std::isfinite(drift)) l.maxDrift = std::max(l.maxDrift, 1.5 * std::min(drift, kFloorDriftMax));
    if (std::isfinite(leak)) l.maxLeakDev = std::max(l.maxLeakDev, 1.5 * std::min(leak, kFloorLeakMax));
    return l;
}

// ---------------------------------------------------------------------------
namespace {
// the mean of several sweeps, frequency by frequency (phase arithmetic, |Z| geometric)
void addToCurve(std::vector<std::pair<double, std::pair<double, double>>>* acc, std::vector<int>* cnt, const Spectrum& s)
{
    for (const Point& p : s.points) {
        if (!(p.freqHz > 0) || !std::isfinite(p.phaseDeg) || !(p.magOhm > 0)) continue;
        size_t k = 0;
        for (; k < acc->size(); ++k) if (std::fabs((*acc)[k].first / p.freqHz - 1) < 0.02) break;
        if (k == acc->size()) { acc->push_back({ p.freqHz, { 0.0, 0.0 } }); cnt->push_back(0); }
        (*acc)[k].second.first += p.phaseDeg;
        (*acc)[k].second.second += std::log(p.magOhm);
        ++(*cnt)[k];
    }
}
LotCurve finishCurve(std::vector<std::pair<double, std::pair<double, double>>> acc, std::vector<int> cnt)
{
    std::vector<size_t> idx(acc.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return acc[a].first < acc[b].first; });
    LotCurve c;
    for (size_t i : idx) {
        if (!cnt[i]) continue;
        c.freqHz.push_back(acc[i].first);
        c.phaseDeg.push_back(acc[i].second.first / cnt[i]);
        c.magOhm.push_back(std::exp(acc[i].second.second / cnt[i]));
    }
    return c;
}
} // namespace

LotCompare compareLot(const LotRun& lot, const LotRun& ref, const LotPipeline& pipe,
                      LotFeatureFn feat, void* ctx, double kZ, const LotResult* res)
{
    LotCompare out;
    const bool model = res && res->model && res->eRef.ok;
    if (!feat || !std::isfinite(kZ)) return out;
    std::vector<double> rc, rg, lc, lg;
    for (int n = 1; n <= ref.plan.needles; ++n) { double c, g; if (ref.needleAir(n, &c, &g)) { rc.push_back(c); rg.push_back(g); } }
    for (int n = 1; n <= lot.plan.needles; ++n) { double c, g; if (lot.needleAir(n, &c, &g)) { lc.push_back(c); lg.push_back(g); } }
    out.transfer.cpRefF = mean(rc); out.transfer.gpRefS = mean(rg);
    out.transfer.cpNeedleF = mean(lc); out.transfer.gpNeedleS = mean(lg);
    out.transfer.kZ = kZ;
    out.transfer.trialCalibrated = pipe.trialCalibrated;
    out.cpSdF = sd(lc);
    out.model = model;
    if (model) out.transfer.kZ = res->mode == 1 ? kNaN : res->kZLot;
    for (int st = 1; st <= lot.plan.phantoms && st <= ref.plan.phantoms; ++st) {
        LotCompareStep cs;
        std::vector<std::pair<double, std::pair<double, double>>> aR, aL, aT;
        std::vector<int> nR, nL, nT;
        std::vector<double> zR, iR, zL, iL, zT, iT;
        for (const LotTest& t : ref.tests) {
            if (!t.ok || t.step != st || t.raw.empty()) continue;
            Spectrum s; s.points = t.raw;
            double cp = kNaN, gp = 0; ref.needleAir(t.needle, &cp, &gp);
            lotPlain(&s, cp, gp, pipe.trialCalibrated);
            addToCurve(&aR, &nR, s);
            const Features f = feat(s, ctx);
            zR.push_back(lotNormaliseZ(f.z1k, st, t.tempC, t.condMsCm)); iR.push_back(f.ipsLegacy);
            ++cs.nRef;
        }
        for (const LotTest& t : lot.tests) {
            if (!t.ok || t.step != st || t.raw.empty()) continue;
            double cp = kNaN, gp = 0; lot.needleAir(t.needle, &cp, &gp);
            Spectrum s; s.points = t.raw;
            lotPlain(&s, cp, gp, pipe.trialCalibrated);
            addToCurve(&aL, &nL, s);
            Features f = feat(s, ctx);
            zL.push_back(lotNormaliseZ(f.z1k, st, t.tempC, t.condMsCm)); iL.push_back(f.ipsLegacy);
            Spectrum m; m.points = t.raw;
            if (!model) {
                LotTransfer tr = out.transfer; tr.cpNeedleF = cp; tr.gpNeedleS = gp;
                lotTransfer(&m, tr);
            } else {
                const LotNeedleQc* q = nullptr;
                for (const LotNeedleQc& x : res->needleQc) if (x.needle == t.needle) q = &x;
                if (!q || !q->e.ok || !std::isfinite(q->cellRatio)) { ++cs.nLot; continue; }
                NeedleTransfer tr;
                tr.cpNeedleF = cp; tr.gpNeedleS = gp;
                if (!q->good()) out.excluded = true;
                if (res->mode != 1 && res->eLot.ok && q->good()) { tr.eNeedle = res->eLot; tr.kZ = res->kZLot; }
                else { tr.eNeedle = q->e; tr.kZ = q->cellRatio; }
                tr.cpRefF = out.transfer.cpRefF; tr.gpRefS = out.transfer.gpRefS;
                tr.eRef = res->eRef;
                tr.trialCalibrated = pipe.trialCalibrated; tr.ranges = pipe.ranges;
                needleTransfer(&m, tr);
            }
            addToCurve(&aT, &nT, m);
            f = feat(m, ctx);
            zT.push_back(lotNormaliseZ(f.z1k, st, t.tempC, t.condMsCm)); iT.push_back(f.ipsLegacy);
            ++cs.nLot;
        }
        cs.ref = finishCurve(aR, nR); cs.lot = finishCurve(aL, nL); cs.lotT = finishCurve(aT, nT);
        cs.zRef = mean(zR); cs.ipsRef = mean(iR); cs.zLot = mean(zL); cs.ipsLot = mean(iL);
        cs.zLotT = mean(zT); cs.ipsLotT = mean(iT);
        out.steps.push_back(cs);
    }
    out.ok = !out.steps.empty();
    return out;
}

// ---------------------------------------------------------------------------
void LotCorrection::at(double zLot, double* kZ, double* dIps) const
{
    double k = 1, d = 0;
    std::vector<LotPoint> p;
    for (const LotPoint& q : pts) if (q.zLot > 0 && std::isfinite(q.kZ) && std::isfinite(q.dIps)) p.push_back(q);
    std::sort(p.begin(), p.end(), [](const LotPoint& a, const LotPoint& b) { return a.zLot < b.zLot; });
    if (!p.empty() && zLot > 0 && std::isfinite(zLot)) {
        if (zLot <= p.front().zLot) { k = p.front().kZ; d = p.front().dIps; }
        else if (zLot >= p.back().zLot) { k = p.back().kZ; d = p.back().dIps; }
        else {
            for (size_t i = 0; i + 1 < p.size(); ++i)
                if (zLot >= p[i].zLot && zLot <= p[i + 1].zLot) {
                    const double t = (std::log10(zLot) - std::log10(p[i].zLot)) /
                                     (std::log10(p[i + 1].zLot) - std::log10(p[i].zLot));
                    k = p[i].kZ + t * (p[i + 1].kZ - p[i].kZ);
                    d = p[i].dIps + t * (p[i + 1].dIps - p[i].dIps);
                    break;
                }
        }
    } else if (!p.empty()) { k = p.front().kZ; d = p.front().dIps; }
    if (kZ) *kZ = k;
    if (dIps) *dIps = d;
}

void LotCorrection::apply(double* z1k, double* ipsLegacy, double* ipsFit) const
{
    if (!usable() || !z1k || !std::isfinite(*z1k)) return;
    double k, d;
    at(*z1k, &k, &d);
    *z1k *= k;
    if (ipsLegacy && std::isfinite(*ipsLegacy)) *ipsLegacy += d;
    if (ipsFit && std::isfinite(*ipsFit)) *ipsFit += d;
}

double LotCorrection::lotZFor(double zRef) const
{
    if (!(zRef > 0) || !std::isfinite(zRef)) return zRef;
    double lo = std::log10(zRef) - 2, hi = std::log10(zRef) + 2;
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi), z = std::pow(10.0, mid);
        double k; at(z, &k, nullptr);
        if (z * k < zRef) lo = mid; else hi = mid;
    }
    return std::pow(10.0, 0.5 * (lo + hi));
}

namespace {
std::string qf(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string qf(const char* f, ...)
{
    char b[400];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
} // namespace

QcLimits qcGeneric(int step)
{
    QcLimits q;
    // every needle and cable measured on the bench lies well inside these (Cp 42 to 222 pF with
    // cables of 0.5 to 2 m; leakage under 40 uS dry); a wet or open one does not. H7-2.15.0: the leakage
    // limit from 100 to 40 uS, and R in 0.9 % saline from 30..1500 to 90..250 ohm (records_7: a sound
    // needle 7 to 10 uS and 122 to 160 ohm; a worn one passed at 95 uS, a check of 43.7 ohm was taken)
    q.cpMinF = 20e-12; q.cpMaxF = 400e-12; q.gpMaxS = 40e-6;
    if (step == kCheck9) { q.rMin = 90; q.rMax = 250; }
    else { q.rMin = 300; q.rMax = 15000; }
    q.zeMin = 20; q.zeMax = 3000;
    q.alphaMin = 0.50; q.alphaMax = 1.0;
    return q;
}

std::string QcLimits::airWhy(double cpF, double gpS) const
{
    const std::string src = fromLot ? "what lot " + lotId + "'s needles measured" : std::string("the generic limits");
    if (!(cpF > 0)) return "no stray capacitance was measured";
    if (std::isfinite(cpMinF) && cpF < cpMinF)
        return qf("its stray capacitance, %.1f pF, is below %s (%.1f to %.1f pF): the cable or connector not fully in, or "
                  "another cable than the lot's.", cpF * 1e12, src.c_str(), cpMinF * 1e12, cpMaxF * 1e12);
    if (std::isfinite(cpMaxF) && cpF > cpMaxF)
        return qf("its stray capacitance, %.1f pF, is above %s (%.1f to %.1f pF): a wet tip or shaft, a hand or metal near "
                  "it, or another cable than the lot's.", cpF * 1e12, src.c_str(), cpMinF * 1e12, cpMaxF * 1e12);
    if (std::isfinite(gpMaxS) && gpS > gpMaxS)
        return qf("its leakage, %.1f uS, is above %s (%.1f uS): a wet tip or connector, or a damaged insulation. Dry it "
                  "and repeat.", gpS * 1e6, src.c_str(), gpMaxS * 1e6);
    return std::string();
}

std::string QcLimits::salineWhy(double r25, double ze, double alpha, double rms, double drift) const
{
    const std::string src = fromLot ? "what lot " + lotId + "'s needles measured" : std::string("the generic limits");
    if (!(rms <= maxRms))
        return qf("the sweeps do not look like a needle tip in saline (model residual %.3f, limit %.3f). Is the tip fully in "
                  "the saline, and is it the right saline?", rms, maxRms);
    if (!(drift <= maxDrift))
        return qf("the two sweeps differ by %.1f %% (limit %.0f %%). The tip has not settled, or the insulation leaks: leave "
                  "it in the saline a minute and repeat; if it falls again, use another needle.", drift * 100, maxDrift * 100);
    if (std::isfinite(rMin) && std::isfinite(rMax) && !(r25 >= rMin && r25 <= rMax))
        return qf("its resistance %.0f ohm (at 25 \xC2\xB0""C) is outside %s (%.0f to %.0f ohm): the wrong saline, a wrong "
                  "temperature, or a damaged tip.", r25, src.c_str(), rMin, rMax);
    if (std::isfinite(zeMin) && std::isfinite(zeMax) && !(ze >= zeMin && ze <= zeMax))
        return qf("its tip surface reads %.0f ohm at 1 kHz, outside %s (%.0f to %.0f ohm): a soiled, damaged or not yet "
                  "settled tip. Rinse it and repeat.", ze, src.c_str(), zeMin, zeMax);
    if (std::isfinite(alphaMin) && std::isfinite(alphaMax) && !(alpha >= alphaMin && alpha <= alphaMax))
        return qf("its tip surface's exponent, %.2f, is outside %s (%.2f to %.2f): a soiled or damaged tip. Rinse it and "
                  "repeat.", alpha, src.c_str(), alphaMin, alphaMax);
    return std::string();
}

std::string QcLimits::describe() const
{
    return qf("Air: %.0f to %.0f pF, leakage below %.0f uS. Saline: R %.0f to %.0f ohm (25 \xC2\xB0""C), tip surface %.0f to "
              "%.0f ohm at 1 kHz, exponent %.2f to %.2f, two sweeps within %.0f %%",
              cpMinF * 1e12, cpMaxF * 1e12, gpMaxS * 1e6, rMin, rMax, zeMin, zeMax, alphaMin, alphaMax, maxDrift * 100);
}

QcLimits LotCorrection::qcLimits(int step) const
{
    QcLimits q = qcGeneric(step);
    q.lotId = lotId;
    const bool saline = hasModel() && dipValid(step);
    q.fromLot = hasQc() || saline;
    if (hasQc()) { q.cpMinF = qcCpMinF; q.cpMaxF = qcCpMaxF; if (std::isfinite(qcGpMaxS)) q.gpMaxS = qcGpMaxS; }
    if (saline) {
        if (step == kCheck9) { q.rMin = dip9RMin; q.rMax = dip9RMax; q.zeMin = dip9ZeMin; q.zeMax = dip9ZeMax; }
        else { q.rMin = dipRMin; q.rMax = dipRMax; q.zeMin = dipZeMin; q.zeMax = dipZeMax; }
    }
    const double aMin = step == kCheck9 ? qcAlpha9Min : qcAlphaMin, aMax = step == kCheck9 ? qcAlpha9Max : qcAlphaMax;
    if (std::isfinite(aMin) && std::isfinite(aMax)) { q.alphaMin = aMin; q.alphaMax = aMax; }
    return q;
}

void lotQcFill(const LotRun& run, const LotResult* res, const LotLimits& lim, LotCorrection* c)
{
    std::vector<double> cp, gp, al, al9;
    if (res && !res->needleQc.empty()) {
        for (const LotNeedleQc& q : res->needleQc) {
            if (!q.good()) continue;
            if (q.cpF > 0) { cp.push_back(q.cpF); gp.push_back(std::isfinite(q.gpS) ? q.gpS : 0.0); }
            if (q.e.ok) al.push_back(q.e.alpha);
            if (q.e9.ok) al9.push_back(q.e9.alpha);
        }
    } else {
        for (int n = 1; n <= run.plan.needles; ++n) {
            double a = kNaN, b = kNaN;
            if (run.needleGivenUp(n) || !run.needleAir(n, &a, &b) || !(a > 0)) continue;
            cp.push_back(a); gp.push_back(std::isfinite(b) ? b : 0.0);
        }
    }
    if (!cp.empty()) {
        double m = 0;
        for (double v : cp) m += v / double(cp.size());
        const double w = std::max(lim.qcCpAbsF, lim.qcCpRel * m);
        c->qcCpMinF = *std::min_element(cp.begin(), cp.end()) - w;
        c->qcCpMaxF = *std::max_element(cp.begin(), cp.end()) + w;
        const double g = std::max(0.0, *std::max_element(gp.begin(), gp.end()));
        c->qcGpMaxS = std::max(lim.qcGpFactor * g, g + lim.qcGpAddS);
    }
    if (!al.empty()) {
        c->qcAlphaMin = *std::min_element(al.begin(), al.end()) - lim.qcAlphaWin;
        c->qcAlphaMax = std::min(1.0, *std::max_element(al.begin(), al.end()) + lim.qcAlphaWin);
    }
    if (!al9.empty()) {
        c->qcAlpha9Min = *std::min_element(al9.begin(), al9.end()) - lim.qcAlphaWin;
        c->qcAlpha9Max = std::min(1.0, *std::max_element(al9.begin(), al9.end()) + lim.qcAlphaWin);
    }
}

std::vector<LotRange> lotRanges(const LotCorrection& c, const std::vector<LotRange>& refBoxes)
{
    std::vector<LotRange> out;
    for (LotRange b : refBoxes) {
        b.lzMin = b.zMin <= 0 ? b.zMin : c.lotZFor(b.zMin);
        b.lzMax = b.zMax >= 1e5 ? b.zMax : c.lotZFor(b.zMax);
        const double mid = (b.lzMin > 0 && b.lzMax < 1e5) ? std::sqrt(b.lzMin * b.lzMax)
                         : (b.lzMax < 1e5 ? b.lzMax : b.lzMin);
        double d; c.at(mid, nullptr, &d);
        b.lipsMin = b.ipsMin <= -100 ? b.ipsMin : b.ipsMin - d;
        b.lipsMax = b.ipsMax >= 100 ? b.ipsMax : b.ipsMax - d;
        out.push_back(b);
    }
    return out;
}

// ---------------------------------------------------------------------------
const LotCorrection* LotFile::find(const std::string& lotId) const
{
    if (lotId.empty()) return nullptr;
    for (const LotCorrection& c : lots) if (c.lotId == lotId) return &c;
    return nullptr;
}
void LotFile::put(const LotCorrection& c)
{
    for (LotCorrection& x : lots) if (x.lotId == c.lotId) { x = c; return; }
    lots.push_back(c);
}

std::string LotFile::save() const
{
    std::ostringstream o;
    o << "BIRADX-LOTCAL 1\n"
      << "rev=" << rev << "\n"
      << "issued=" << clean(issued) << "\n"
      << "device=" << clean(device) << "\n"
      << "ref=" << clean(refLot) << "\n"
      << "trial=" << (trialUncalibrated ? "uncalibrated" : "calibrated") << "\n"
      << "refcp=" << num(refCpF) << "|" << num(refGpS) << "\n";
    if (refE.ok)
        o << "refe=" << num(refE.rOhm) << "|" << num(refE.a) << "|" << num(refE.alpha) << "|" << num(refE.rms) << "\n";
    if (refE9.ok)
        o << "refe9=" << num(refE9.rOhm) << "|" << num(refE9.a) << "|" << num(refE9.alpha) << "|" << num(refE9.rms) << "\n";
    for (const LotCorrection& c : lots) {
        o << "L=" << clean(c.lotId) << "|" << clean(c.verdict) << "|" << clean(c.date) << "|" << c.needles
          << "|" << num(c.cpF) << "|" << num(c.gpS) << "|" << num(c.kZ) << "|" << num(c.residIps) << "|"
          << num(c.residZ) << "|";
        for (size_t i = 0; i < c.pts.size(); ++i)
            o << (i ? ";" : "") << num(c.pts[i].zLot) << ":" << num(c.pts[i].kZ) << ":" << num(c.pts[i].dIps);
        if (c.e.ok)      // H7-2.3.0: the model's fields; a file without them reads as before
            o << "|" << c.mode << "|" << num(c.e.rOhm) << "|" << num(c.e.a) << "|" << num(c.e.alpha) << "|" << num(c.e.rms)
              << "|" << num(c.dipRMin) << "|" << num(c.dipRMax) << "|" << num(c.dipZeMin) << "|" << num(c.dipZeMax);
        if (c.e.ok)      // H7-2.4.0: the clinic check solutions it was validated with, and the 0.9 % window
            o << "|" << c.dipMask << "|" << num(c.dip9RMin) << "|" << num(c.dip9RMax) << "|" << num(c.dip9ZeMin) << "|" << num(c.dip9ZeMax);
        o << "\n";
    }
    // H7-2.10.0: each lot's QC limits (a file without them reads as before; an older device skips them)
    for (const LotCorrection& c : lots)
        if (c.hasQc() || std::isfinite(c.qcAlphaMin) || std::isfinite(c.qcAlpha9Min))
            o << "Q=" << clean(c.lotId) << "|" << num(c.qcCpMinF) << "|" << num(c.qcCpMaxF) << "|" << num(c.qcGpMaxS) << "|"
              << num(c.qcAlphaMin) << "|" << num(c.qcAlphaMax) << "|" << num(c.qcAlpha9Min) << "|" << num(c.qcAlpha9Max) << "\n";
    const std::string body = o.str();
    char b[32];
    std::snprintf(b, sizeof b, "crc=%08X\n", (unsigned)crc32(body));
    return body + b;
}

bool LotFile::load(const std::string& text, std::string* why)
{
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    const size_t cp = text.rfind("crc=");
    if (text.compare(0, 15, "BIRADX-LOTCAL 1") != 0) return fail("this is not a BIRADx lot calibration file");
    if (cp == std::string::npos) return fail("the file has no check line: it is incomplete");
    const std::string body = text.substr(0, cp);
    const unsigned want = unsigned(std::strtoul(text.c_str() + cp + 4, nullptr, 16));
    if (crc32(body) != want) return fail("the file's check sum does not match: it was changed or damaged");
    LotFile f;
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "rev") f.rev = std::atoi(v.c_str());
        else if (k == "issued") f.issued = v;
        else if (k == "device") f.device = v;
        else if (k == "ref") f.refLot = v;
        else if (k == "trial") f.trialUncalibrated = v == "uncalibrated";
        else if (k == "refe" || k == "refe9") {
            const auto x = split(v, '|');
            ElectrodeModel& e = k == "refe" ? f.refE : f.refE9;
            if (x.size() >= 3) {
                e.rOhm = rd(x[0]); e.a = rd(x[1]); e.alpha = rd(x[2]);
                if (x.size() > 3) e.rms = rd(x[3]);
                e.ok = e.rOhm > 0 && e.a > 0 && e.alpha > 0 && e.alpha <= 1;
            }
        }
        else if (k == "refcp") { const auto x = split(v, '|'); f.refCpF = rd(x[0]); if (x.size() > 1) f.refGpS = rd(x[1]); }
        else if (k == "L") {
            const auto x = split(v, '|');
            if (x.size() < 10) continue;
            LotCorrection c;
            c.lotId = x[0]; c.verdict = x[1]; c.date = x[2]; c.needles = std::atoi(x[3].c_str());
            c.cpF = rd(x[4]); c.gpS = rd(x[5]); c.kZ = rd(x[6]); c.residIps = rd(x[7]); c.residZ = rd(x[8]);
            if (!x[9].empty())
                for (const std::string& t : split(x[9], ';')) {
                    const auto y = split(t, ':');
                    if (y.size() < 3) continue;
                    LotPoint p; p.zLot = rd(y[0]); p.kZ = rd(y[1]); p.dIps = rd(y[2]);
                    c.pts.push_back(p);
                }
            if (x.size() >= 19) {
                c.mode = std::atoi(x[10].c_str()) == 1 ? 1 : 0;
                c.e.rOhm = rd(x[11]); c.e.a = rd(x[12]); c.e.alpha = rd(x[13]); c.e.rms = rd(x[14]);
                c.e.ok = c.e.rOhm > 0 && c.e.a > 0 && c.e.alpha > 0 && c.e.alpha <= 1;
                c.dipRMin = rd(x[15]); c.dipRMax = rd(x[16]); c.dipZeMin = rd(x[17]); c.dipZeMax = rd(x[18]);
                c.dipMask = c.mode == 1 ? 1 : 0;          // a 2.3.0 file: per-needle lots were validated with the 0.09 %
            }
            if (x.size() >= 24) {
                c.dipMask = std::atoi(x[19].c_str()) & 3;
                c.dip9RMin = rd(x[20]); c.dip9RMax = rd(x[21]); c.dip9ZeMin = rd(x[22]); c.dip9ZeMax = rd(x[23]);
            }
            f.lots.push_back(c);
        }
        else if (k == "Q") {
            const auto x = split(v, '|');
            if (x.size() < 8) continue;
            for (LotCorrection& c : f.lots)
                if (c.lotId == x[0]) {
                    c.qcCpMinF = rd(x[1]); c.qcCpMaxF = rd(x[2]); c.qcGpMaxS = rd(x[3]);
                    c.qcAlphaMin = rd(x[4]); c.qcAlphaMax = rd(x[5]); c.qcAlpha9Min = rd(x[6]); c.qcAlpha9Max = rd(x[7]);
                }
        }
    }
    *this = f;
    return true;
}

} // namespace birx
