#include "matlib.h"
#include "calib.h"
#include "needlemodel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>

namespace birx {

namespace {
constexpr double kPi = 3.14159265358979323846;

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
} // namespace

// ---------------------------------------------------------------------------
// The library and its file
// ---------------------------------------------------------------------------
bool MatNeedle::hasRef() const
{
    return std::isfinite(refR) && refR > 0 && std::isfinite(refA) && refA >= 0 && std::isfinite(refAlpha);
}

MatNeedle MatSweep::needleInfo() const
{
    MatNeedle n;
    n.cpF = cpF; n.gpS = gpS;
    n.refR = refR; n.refA = refA; n.refAlpha = refAlpha;
    return n;
}

void MatSweep::setNeedle(const MatNeedle& n)
{
    cpF = n.cpF; gpS = n.gpS;
    refR = n.refR; refA = n.refA; refAlpha = n.refAlpha;
}

bool MatLibrary::hasLabel(const std::string& l) const
{
    return std::find(labels.begin(), labels.end(), l) != labels.end();
}

bool MatLibrary::isOff(const std::string& l) const
{
    return std::find(off.begin(), off.end(), l) != off.end();
}

int MatLibrary::count(const std::string& label) const
{
    int n = 0;
    for (const MatSweep& s : sweeps) if (s.label == label) ++n;
    return n;
}

int MatLibrary::placements(const std::string& label) const
{
    std::vector<int> p;
    for (const MatSweep& s : sweeps)
        if (s.label == label && std::find(p.begin(), p.end(), s.sample) == p.end()) p.push_back(s.sample);
    return int(p.size());
}

int MatLibrary::told(int* right) const
{
    int n = 0, r = 0;
    for (const MatSweep& s : sweeps) if (s.saidRight >= 0) { ++n; r += s.saidRight; }
    if (right) *right = r;
    return n;
}

int MatLibrary::toldFinal(int* right) const
{
    int n = 0, r = 0;
    for (const MatSweep& s : sweeps) if (s.saidFRight >= 0) { ++n; r += s.saidFRight; }
    if (right) *right = r;
    return n;
}

std::string matSweepLine(const MatSweep& s)
{
    std::ostringstream o;
    o << "S=" << clean(s.label) << "|" << s.sample << "|" << clean(s.when) << "|" << clean(s.needle) << "|"
      << num(s.cpF) << "|" << s.settleS << "|";
    for (size_t i = 0; i < s.pts.size(); ++i)
        o << (i ? ";" : "") << num(s.pts[i].freqHz) << ":" << num(s.pts[i].magOhm) << ":" << num(s.pts[i].phaseDeg)
          << ":" << s.pts[i].range;
    o << "|" << clean(s.said) << "|" << num(s.saidP) << "|" << s.saidRight;
    // how it is used: auto (by the classifier's own check), use (kept by hand), aside (by hand);
    // and whether the last fit set it aside — for whoever reads the file
    o << "|" << (s.use == MatSweep::kUse ? "use" : s.use == MatSweep::kAside ? "aside" : "auto") << "|"
      << (s.use == MatSweep::kAside ? "set aside by hand" : s.aside ? "set aside" : "");
    o << "|" << clean(s.origin) << "|" << clean(s.device) << "|" << s.origSample;
    o << "|" << num(s.gpS) << "|" << num(s.refR) << "|" << num(s.refA) << "|" << num(s.refAlpha);
    o << "|" << clean(s.saidF) << "|" << num(s.saidFP) << "|" << s.saidFRight;
    o << "|" << clean(s.qc);
    o << "|" << clean(s.qcMove);         // H7-2.12.6 (older versions read the fields before it)
    return o.str();
}

bool matParseSweep(const std::string& v, MatSweep* out)
{
    const auto f = split(v, '|');
    if (f.size() < 7) return false;
    MatSweep s;
    s.label = f[0]; s.sample = std::atoi(f[1].c_str()); s.when = f[2]; s.needle = f[3];
    for (size_t i = 11; i < s.when.size(); ++i) if (s.when[i] == '/') s.when[i] = ':';   // saved without its colons
    s.cpF = rd(f[4]); s.settleS = std::atoi(f[5].c_str());
    for (const std::string& q : split(f[6], ';')) {
        const auto y = split(q, ':');
        if (y.size() < 4) continue;
        Point p; p.freqHz = rd(y[0]); p.magOhm = rd(y[1]); p.phaseDeg = rd(y[2]); p.range = std::atoi(y[3].c_str());
        s.pts.push_back(p);
    }
    if (f.size() >= 10) {
        s.said = f[7]; s.saidP = rd(f[8]);
        const int r = std::atoi(f[9].c_str());
        s.saidRight = r < 0 ? -1 : r ? 1 : 0;
    }
    if (f.size() >= 12) {
        s.use = f[10] == "use" ? MatSweep::kUse : f[10] == "aside" ? MatSweep::kAside : MatSweep::kAuto;
        s.aside = f[11] == "set aside";
    }
    if (f.size() >= 15) {
        s.origin = f[12]; s.device = f[13];
        s.origSample = f[14].empty() ? -1 : std::atoi(f[14].c_str());
    }
    if (f.size() >= 19) { s.gpS = rd(f[15]); s.refR = rd(f[16]); s.refA = rd(f[17]); s.refAlpha = rd(f[18]); }
    if (f.size() >= 22) {
        s.saidF = f[19]; s.saidFP = rd(f[20]);
        const int r = std::atoi(f[21].c_str());
        s.saidFRight = r < 0 ? -1 : r ? 1 : 0;
    }
    if (f.size() >= 23) s.qc = f[22];
    if (f.size() >= 24) s.qcMove = f[23];
    *out = s;
    return true;
}

std::string MatLibrary::save() const
{
    std::ostringstream o;
    o << "BIRADX-MATLIB 1\n";
    if (!device.empty()) o << "device=" << clean(device) << "\n";
    if (!exported.empty()) o << "exported=" << clean(exported) << "\n";
    for (const std::string& l : labels) o << "label=" << clean(l) << "\n";
    for (const std::string& l : off) if (hasLabel(l)) o << "off=" << clean(l) << "\n";   // H7-2.12.0 (older versions skip it)
    for (const MatSweep& s : sweeps) o << matSweepLine(s) << "\n";
    return o.str();
}

bool MatLibrary::load(const std::string& text)
{
    *this = MatLibrary();
    std::istringstream in(text);
    std::string line;
    bool magic = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "BIRADX-MATLIB 1") { magic = true; continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "label" && !v.empty() && !hasLabel(v)) labels.push_back(v);
        else if (k == "off" && !v.empty() && !isOff(v)) off.push_back(v);
        else if (k == "device") device = v;
        else if (k == "exported") exported = v;
        else if (k == "S") {
            MatSweep s;
            if (!matParseSweep(v, &s)) continue;
            if (!hasLabel(s.label)) labels.push_back(s.label);
            sweeps.push_back(s);
        }
    }
    return magic;
}

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------
std::vector<double> matFeatures(const std::vector<Point>& pts, double cpF)
{
    MatNeedle n;
    n.cpF = cpF;
    return matFeatures(pts, n, false);
}

std::vector<double> matFeatures(const std::vector<Point>& pts, const MatNeedle& n, bool normalised)
{
    if (normalised && !n.hasRef()) return {};
    Spectrum s;
    s.points = pts;
    instrumentToTrue(&s, nullptr);
    if (std::isfinite(n.cpF) && n.cpF > 0) deembedAll(&s, n.cpF, std::isfinite(n.gpS) ? n.gpS : 0.0);
    std::vector<double> ph, lz;
    for (double f : InstrumentConstants::sweepFrequencies()) {
        const Point* q = nullptr;
        for (const Point& p : s.points) {
            if (p.freqHz > 0 && std::fabs(p.freqHz / f - 1.0) < 0.02 && p.magOhm > 0 && std::isfinite(p.phaseDeg)) { q = &p; break; }
        }
        if (!q) return {};
        if (normalised) {
            // the needle's own interface taken off: what is left is the material (and R)
            const std::complex<double> z = std::polar(q->magOhm, q->phaseDeg * kPi / 180.0);
            const std::complex<double> zi = std::polar(n.refA * std::pow(2.0 * kPi * f, -n.refAlpha), -n.refAlpha * kPi / 2.0);
            const std::complex<double> zt = z - zi;
            if (!(std::abs(zt) > q->magOhm * 1e-6)) return {};
            ph.push_back(std::arg(zt) * 180.0 / kPi);
            lz.push_back(std::log10(std::abs(zt)));
        } else {
            ph.push_back(q->phaseDeg);
            lz.push_back(std::log10(q->magOhm));
        }
    }
    double m = 0;
    for (double v : lz) m += v;
    m /= double(lz.size());
    std::vector<double> x = ph;
    for (double v : lz) x.push_back(v - m);
    x.push_back(normalised ? m - std::log10(n.refR) : m);
    return x;
}

// ---------------------------------------------------------------------------
// The needle's saline reference and the tip check
// ---------------------------------------------------------------------------
double matBestSalineCp(const std::vector<Point>& pts, double gpS, double* rmsOut)
{
    auto rmsAt = [&](double cp) {
        Spectrum s;
        s.points = pts;
        instrumentToTrue(&s, nullptr);
        deembedAll(&s, cp, std::isfinite(gpS) ? gpS : 0.0);
        const ElectrodeModel e = fitElectrode(s);
        return std::isfinite(e.rms) && e.rOhm > 0 ? e.rms : 1e9;
    };
    double best = kNaN, bestR = 1e9;
    for (double l = std::log10(2e-12); l <= std::log10(1e-8) + 1e-9; l += 0.05) {
        const double r = rmsAt(std::pow(10.0, l));
        if (r < bestR) { bestR = r; best = std::pow(10.0, l); }
    }
    if (std::isfinite(best))
        for (double l = std::log10(best) - 0.05; l <= std::log10(best) + 0.05 + 1e-9; l += 0.005) {
            const double r = rmsAt(std::pow(10.0, l));
            if (r < bestR) { bestR = r; best = std::pow(10.0, l); }
        }
    if (rmsOut) *rmsOut = bestR < 1e8 ? bestR : kNaN;
    return bestR < 1e8 ? best : kNaN;
}

TipFault matTipFault(const std::vector<Point>& pts, double refR, double minRatio, std::string* why)
{
    auto at = [&](double f) {
        double best = 1e300, z = kNaN;
        for (const Point& p : pts) {
            if (!(p.freqHz > 0) || !(p.magOhm > 0)) continue;
            const double d = std::fabs(std::log(p.freqHz / f));
            if (d < 0.05 && d < best) { best = d; z = p.magOhm; }
        }
        return z;
    };
    char b[400];
    double lo = 1e300, hi = 0;
    int n = 0;
    for (double f : { 500.0, 1000.0, 5000.0, 10000.0, 25000.0, 50000.0 }) {
        const double z = at(f);
        if (!std::isfinite(z)) continue;
        lo = std::min(lo, z); hi = std::max(hi, z); ++n;
    }
    if (n >= 4 && hi < 150 && hi / lo < 1.04) {
        std::snprintf(b, sizeof b, "the tip is shorted: |Z| stays at %.0f ohm from 500 Hz to 50 kHz (within %.1f %%), the "
                      "instrument's floor, with no material in it. Something joins the tip's two electrodes.",
                      0.5 * (lo + hi), (hi / lo - 1) * 100);
        if (why) *why = b;
        return TipFault::Short;
    }
    const double z5 = at(500000.0);
    if (std::isfinite(refR) && refR > 0 && std::isfinite(z5) && z5 < minRatio * refR) {
        std::snprintf(b, sizeof b, "the tip is bridged: |Z| at 500 kHz is %.0f ohm, below the %.0f ohm this needle read in 0.9 %% "
                      "saline (the limit is %.0f %% of it). Residue between the tip's electrodes (starch, juice, tissue) is "
                      "measured, not the material.", z5, refR, minRatio * 100);
        if (why) *why = b;
        return TipFault::Bridged;
    }
    if (why) why->clear();
    return TipFault::None;
}

std::string matNotATip(const std::vector<Point>& pts, double zeImagOhm)
{
    char b[480];
    int rep = 0, n = 0;
    double rz = 0, rp = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (!(pts[i].magOhm > 0) || !std::isfinite(pts[i].phaseDeg)) continue;
        ++n;
        int k = 0;
        for (size_t j = 0; j < pts.size(); ++j)
            if (std::fabs(pts[j].magOhm - pts[i].magOhm) <= 1e-6 * pts[i].magOhm && std::fabs(pts[j].phaseDeg - pts[i].phaseDeg) <= 1e-6)
                ++k;
        if (k > rep) { rep = k; rz = pts[i].magOhm; rp = pts[i].phaseDeg; }
    }
    if (rep >= 5) {
        std::snprintf(b, sizeof b, "the sweep came back with one value, %.0f ohm at %.1f\xC2\xB0, at %d of its %d frequencies: the "
                      "measurement unit did not measure it.", rz, rp, rep, n);
        return b;
    }
    double x = -1e300, zx = kNaN;
    int nLow = 0;
    for (const Point& p : pts) {
        if (!(p.freqHz >= 400 && p.freqHz <= 2100) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        const double xi = -p.magOhm * std::sin(p.phaseDeg * kPi / 180.0);
        if (xi > x) { x = xi; zx = p.magOhm; }
        ++nLow;
    }
    const bool known = std::isfinite(zeImagOhm) && zeImagOhm > 0;
    const double thr = known ? std::max(20.0, std::min(60.0, 0.3 * zeImagOhm)) : 50.0;
    if (nLow && x < thr) {
        if (known)
            std::snprintf(b, sizeof b, "there is no tip surface in it: at 500 Hz to 1 kHz %.0f ohm with a reactance of only %.0f ohm "
                          "(this needle's tip surface: %.0f ohm; limit %.0f ohm). A resistor, a wire or the connector joins the "
                          "electrodes, not a material.", zx, std::max(0.0, x), zeImagOhm, thr);
        else
            std::snprintf(b, sizeof b, "there is no tip surface in it: at 500 Hz to 1 kHz %.0f ohm with a reactance of only %.0f ohm "
                          "(a tip in any material: hundreds of ohm; limit %.0f ohm). A resistor, a wire or the connector joins "
                          "the electrodes, not a material.", zx, std::max(0.0, x), thr);
        return b;
    }
    return std::string();
}

bool matNeedleRef(const std::vector<Point>& pts, double cpF, double gpS, MatNeedle* out, std::string* why)
{
    auto fail = [&](const std::string& w) { if (why) *why = w; return false; };
    if (!(std::isfinite(cpF) && cpF > 0)) return fail("Air check first: the reference needs this needle's stray capacitance.");
    Spectrum s;
    s.points = pts;
    instrumentToTrue(&s, nullptr);
    deembedAll(&s, cpF, std::isfinite(gpS) ? gpS : 0.0);
    const ElectrodeModel e = fitElectrode(s);
    if (!e.ok || !(e.rms <= 0.05)) {
        // which is wrong: the Air check, or the needle's way of behaving in saline?
        double bestRms = kNaN;
        const double bestCp = matBestSalineCp(pts, gpS, &bestRms);
        char b[600];
        if (std::isfinite(bestCp) && bestRms <= 0.05 && bestCp > 1.5 * cpF)
            std::snprintf(b, sizeof b, "with the Air check's stray capacitance (%.0f pF) the sweep does not look like a tip in saline "
                          "(model residual %.3f), but it does with %.0f pF (residual %.3f): in the saline the needle has more "
                          "capacitance than in air. Its insulated shaft is in the saline and couples to it (put only the tip in, "
                          "about 10 mm), or the cable was moved since the Air check.", cpF * 1e12, e.rms, bestCp * 1e12, bestRms);
        else if (std::isfinite(bestCp) && bestRms <= 0.05 && bestCp < cpF / 1.5)
            std::snprintf(b, sizeof b, "with the Air check's stray capacitance (%.0f pF) the sweep does not look like a tip in saline "
                          "(model residual %.3f), but it does with %.0f pF (residual %.3f). The Air check was not taken in clean air "
                          "\xE2\x80\x94 a wet tip or shaft, or a hand, the cup or metal near it \xE2\x80\x94 or the cable was moved "
                          "since. Dry the needle, hold it in air and do the Air check again.", cpF * 1e12, e.rms, bestCp * 1e12, bestRms);
        else if (std::isfinite(bestCp) && bestRms <= 0.05)
            std::snprintf(b, sizeof b, "the sweep does not look like a needle tip in saline (model residual %.3f, limit 0.050). Is the "
                          "tip fully in the 0.9 %% saline?", e.rms);
        else
            std::snprintf(b, sizeof b, "the sweep does not look like a tip in saline with any stray capacitance across it (model "
                          "residual %.3f with the Air check's %.0f pF; at best %.3f, with %.0f pF). Is the tip fully in the 0.9 %% saline, "
                          "and the connector dry? A needle of two insulated conductors (coaxial) also couples to the saline through "
                          "its insulation along its whole length, which this reference cannot model.", e.rms, cpF * 1e12,
                          std::isfinite(bestRms) ? bestRms : e.rms, std::isfinite(bestCp) ? bestCp * 1e12 : cpF * 1e12);
        return fail(b);
    }
    if (!(e.rOhm >= 20 && e.rOhm <= 20000)) {
        char b[160];
        std::snprintf(b, sizeof b, "its resistance, %.0f ohm, is not that of a needle tip in 0.9 %% saline.", e.rOhm);
        return fail(b);
    }
    out->cpF = cpF; out->gpS = gpS;
    out->refR = e.rOhm; out->refA = e.a; out->refAlpha = e.alpha;
    if (why) why->clear();
    return true;
}

// ---------------------------------------------------------------------------
// The arithmetic of the classifier
// ---------------------------------------------------------------------------
namespace {
using Mat = std::vector<std::vector<double>>;

// the 99.9 % point of chi-square with k degrees of freedom (Wilson-Hilferty)
double chi2_999(int k)
{
    const double a = 2.0 / (double(k) * 9.0);
    const double c = 1.0 - a + std::sqrt(a) * 3.0902;
    return double(k) * c * c * c;
}

// the squared distance of (x, y) to a mean under a 2 x 2 covariance (xx, xy, yy), and the
// score of the quadratic discriminant: the distance plus the log of the covariance's determinant
void qda2(double x, double y, const double* mu, const double* cov, double* d2, double* score)
{
    const double dx = x - mu[0], dy = y - mu[1];
    const double det = cov[0] * cov[2] - cov[1] * cov[1];
    *d2 = ((cov[2] * dx * dx - (cov[1] + cov[1]) * dx * dy) + cov[0] * dy * dy) / det;
    *score = *d2 + std::log(det);
}

double maha(const std::vector<double>& x, const std::vector<double>& m, const Mat& p)
{
    double d = 0;
    const size_t k = x.size();
    for (size_t i = 0; i < k; ++i)
        for (size_t j = 0; j < k; ++j) d += (x[i] - m[i]) * p[i][j] * (x[j] - m[j]);
    return d;
}

// probabilities from squared distances: exp(-d2 / 2T), normalised
void softmaxT(double T, const std::vector<double>& d, std::vector<double>* out)
{
    double mn = 1e300;
    for (double v : d) if (v < mn) mn = v;
    out->assign(d.size(), 0.0);
    double s = 0;
    const double t2 = T + T;
    for (size_t i = 0; i < d.size(); ++i) {
        const double e = std::exp(-(d[i] - mn) / t2);
        s += e;
        (*out)[i] = e;
    }
    for (double& v : *out) v /= s;
}

// the second layer: the softmax of the weighted sum of three answers' log-probabilities
void stackSoftmax(const double* w, const std::vector<double>* const* L, std::vector<double>* out)
{
    const size_t n = L[0]->size();
    out->assign(n, 0.0);
    double mx = -1e300;
    for (size_t c = 0; c < n; ++c) {
        double s = 0;
        for (int j = 0; j < 3; ++j) s += w[j] * (*L[j])[c];
        (*out)[c] = s;
        mx = s > mx ? s : mx;
    }
    double sum = 0;
    for (double& v : *out) { v = std::exp(v - mx); sum += v; }
    for (double& v : *out) v /= sum;
}

// the log of probabilities (or weights) with 5 % spread evenly over all of them: no answer is
// ever taken as impossible, and a weight of 0 does not make the second layer's sum infinite
void logs(const std::vector<double>& in, std::vector<double>* out)
{
    out->resize(in.size());
    const double even = 0.05 / double(unsigned(in.size()));
    for (size_t i = 0; i < in.size(); ++i) (*out)[i] = std::log(even + (in[i] > 0 ? in[i] * 0.95 : 0.0));
}

// the scatter about the mean from the sums: S - s s' / n
void covFromSums(double n, const std::vector<double>& scat, const std::vector<double>& sum, size_t k, std::vector<double>* out)
{
    out->assign(k * k, 0.0);
    for (size_t i = 0; i < k; ++i)
        for (size_t j = i; j < k; ++j) {
            const double v = scat[i * k + j] - (sum[j] * sum[i]) / n;
            (*out)[i * k + j] = v;
            (*out)[j * k + i] = v;
        }
}

// The blend: weights w >= 0, sum 1, minimising w'Hw + c'w with c = -2g + 0.001 d2 (the small
// pull toward the materials nearest the sweep decides between mixes the curve cannot tell
// apart). An active-set method: the system of the free weights solved exactly (Gauss-Jordan),
// a weight dropped when the step would make it negative, added when its gradient says so.
bool simplexQp(const Mat& H, const std::vector<double>& g, const std::vector<double>& d2, std::vector<double>* wOut)
{
    const size_t n = H.size();
    if (n == 0) return false;
    double tr = 0;
    for (size_t i = 0; i < n; ++i) tr += std::fabs(H[i][i]);
    const double ridge = 1e-300 + (tr / double(int(n))) * 1e-10;
    std::vector<double> c(n, 0.0);
    double scale = 1e-300;
    for (size_t i = 0; i < n; ++i) {
        c[i] = g[i] * -2.0 + d2[i] * 0.001;
        const double a = std::fabs(c[i]);
        scale = a > scale ? a : scale;
    }
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            const double a = std::fabs(H[i][j]) + std::fabs(H[i][j]);
            scale = a > scale ? a : scale;
        }
    // start at the best corner
    size_t first = 0;
    for (size_t j = 1; j < n; ++j)
        if (ridge + H[first][first] + c[first] > ridge + H[j][j] + c[j]) first = j;
    std::vector<double> w(n, 0.0);
    w[first] = 1.0;
    std::vector<char> free_(n, 0);
    free_[first] = 1;
    const int maxIter = int(n + 5) * 4;
    int iter = 0;
    std::vector<int> F;
    std::vector<double> A, x;
    for (;;) {
        F.clear();
        for (size_t i = 0; i < n; ++i) if (free_[i]) F.push_back(int(i));
        const size_t m = F.size(), cols = m + 2;
        // [ 2(H + ridge I)  -1 | -c ]
        // [ 1 ... 1          0 |  1 ]
        A.assign((m + 1) * cols, 0.0);
        for (size_t r = 0; r < m; ++r) {
            for (size_t q = 0; q < m; ++q) {
                const double h = (F[q] == F[r] ? ridge : 0.0) + H[size_t(F[r])][size_t(F[q])];
                A[r * cols + q] = h + h;
            }
            A[r * cols + m] = -1.0;
            A[r * cols + m + 1] = -c[size_t(F[r])];
        }
        for (size_t q = 0; q < m; ++q) A[m * cols + q] = 1.0;
        A[m * cols + m + 1] = 1.0;
        for (size_t col = 0; col <= m; ++col) {
            size_t p = col;
            double big = std::fabs(A[col * cols + col]);
            for (size_t r = col + 1; r <= m; ++r) {
                const double a = std::fabs(A[r * cols + col]);
                if (big < a) { big = a; p = r; }
            }
            if (big < 1e-300) return false;
            if (p != col)
                for (size_t k = col; k < cols; ++k) std::swap(A[col * cols + k], A[p * cols + k]);
            for (size_t r = 0; r <= m; ++r) {
                if (r == col) continue;
                const double f = A[r * cols + col] / A[col * cols + col];
                if (f == 0.0) continue;
                for (size_t k = col; k < cols; ++k) A[r * cols + k] -= A[col * cols + k] * f;
            }
        }
        x.assign(m + 1, 0.0);
        for (size_t r = 0; r <= m; ++r) x[r] = A[r * cols + m + 1] / A[r * cols + r];
        const double lambda = x[m];
        bool feasible = true;
        for (size_t r = 0; r < m; ++r) if (x[r] < -1e-14) { feasible = false; break; }
        if (!feasible) {
            // step toward the solution until the first weight reaches 0
            double t = 1.0;
            size_t block = n;
            for (size_t r = 0; r < m; ++r) {
                const size_t j = size_t(F[r]);
                const double d = x[r] - w[j];
                if (d < 0) {
                    const double s = -w[j] / d;
                    if (s < t) { t = s; block = j; }
                }
            }
            for (size_t r = 0; r < m; ++r) {
                const size_t j = size_t(F[r]);
                w[j] = w[j] + (x[r] - w[j]) * t;
            }
            if (block < n) { w[block] = 0.0; free_[block] = 0; }
            for (size_t r = 0; r < m; ++r) {
                const size_t j = size_t(F[r]);
                if (!(w[j] > 0.0)) { w[j] = 0.0; free_[j] = 0; }
            }
            bool any = false;
            for (char f : free_) if (f) any = true;
            if (!any) return false;
        } else {
            for (size_t r = 0; r < m; ++r) w[size_t(F[r])] = x[r] > 0.0 ? x[r] : 0.0;
            // a weight held at 0 that would lower the objective
            double most = scale * -1e-12;
            size_t add = n;
            for (size_t i = 0; i < n; ++i) {
                if (free_[i]) continue;
                double gr = c[i];
                for (size_t j = 0; j < n; ++j) {
                    const double h = (j == i ? ridge : 0.0) + H[i][j];
                    gr += (h + h) * w[j];
                }
                if (gr - lambda < most) { most = gr - lambda; add = i; }
            }
            if (add == n) {
                double s = 0;
                for (double v : w) s += v;
                for (double& v : w) v /= s;
                *wOut = w;
                return true;
            }
            free_[add] = 1;
        }
        if (++iter >= maxIter) return false;
    }
}

// The same problem by accelerated projected gradient (FISTA), when the active set fails.
void simplexPg(double tol, const Mat& H, const std::vector<double>& g, const std::vector<double>& d2,
               std::vector<double>* wOut, int maxIter)
{
    const size_t n = H.size();
    double L = 2e-9;
    if (n) {
        double tr = 0;
        for (size_t i = 0; i < n; ++i) tr += H[i][i];
        if (!(tr < 1e-9)) L = tr + tr;
    }
    std::vector<double> x(n, 1.0 / double(int(n))), y = x, xprev = x, grad(n, 0.0), u(n, 0.0);
    double t = 1.0;
    int iter = 0;
    for (;;) {
        for (size_t i = 0; i < n; ++i) {
            double s = 0;
            for (size_t j = 0; j < n; ++j) s += H[i][j] * y[j];
            const double d = s - g[i];
            grad[i] = d + d + d2[i] * 0.001;
        }
        xprev = x;
        for (size_t i = 0; i < n; ++i) x[i] = y[i] - grad[i] / L;
        // projection onto the simplex
        u = x;
        if (!u.empty()) {
            std::sort(u.begin(), u.end(), [](double a, double b) { return a > b; });
            double cum = 0, theta = 0;
            for (size_t i = 0; i < u.size(); ++i) {
                cum += u[i];
                const double th = (cum - 1.0) / double(int(i + 1));
                if (u[i] - th > 0) theta = th;
            }
            for (double& v : x) { const double d = v - theta; v = d > 0 ? d : 0.0; }
        }
        const double tn = (std::sqrt(t * 4.0 * t + 1.0) + 1.0) * 0.5;
        double diff = 0;
        const double mom = (t - 1.0) / tn;
        for (size_t i = 0; i < n; ++i) {
            y[i] = x[i] + (x[i] - xprev[i]) * mom;
            diff += std::fabs(x[i] - xprev[i]);
        }
        if ((diff < tol && iter > 20) || ++iter == maxIter) break;
        t = tn;
    }
    *wOut = x;
}

// ---------------------------------------------------------------------------
// The answer by Z and IPS
// ---------------------------------------------------------------------------
bool zipsOf(const std::vector<Point>& pts, double* logZ, double* ips)
{
    Spectrum s;
    s.points = pts;
    const Point* a = s.at(1000.0, 200.0);
    const Point* b = s.at(100000.0, 2000.0);
    const Point* c = s.at(500000.0, 2000.0);
    if (!a || !b || !c || !(a->magOhm > 0) || !std::isfinite(b->phaseDeg) || !std::isfinite(c->phaseDeg)) return false;
    *logZ = std::log10(a->magOhm);
    *ips = (c->phaseDeg - b->phaseDeg) * InstrumentConstants::kIpsPerDecade;
    return std::isfinite(*logZ) && std::isfinite(*ips);
}

void zipsScores(const MatZipsModel& z, double x, double y, std::vector<double>* score, std::vector<double>* d2)
{
    const size_t C = z.mean.size();
    score->assign(C, 0.0);
    d2->assign(C, 0.0);
    for (size_t c = 0; c < C; ++c) {
        const double mu[2] = { z.mean[c][0], z.mean[c][1] };
        const double cv[3] = { z.cov[c][0], z.cov[c][1], z.cov[c][2] };
        qda2(x, y, mu, cv, &(*d2)[c], &(*score)[c]);
    }
}

struct ZStat {
    int n = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
};

struct ZipsCtx {
    const std::vector<ZStat>* cls;
    const std::vector<ZStat>* place;
    const std::vector<int>* placeClass;
    const int* C;
    std::vector<double>* mu;             // 2 per material
    std::vector<double>* cov;            // 3 per material
};

// the per-material means and covariances, one placement left out (-1: none): lambda of the
// pooled covariance and the rest the material's own, with floors (Z to 1 %, IPS to 0.4)
bool zipsStats(double lambda, const ZipsCtx& z, int leaveOut)
{
    std::vector<ZStat> s = *z.cls;
    if (leaveOut != -1) {
        ZStat& q = s[size_t((*z.placeClass)[size_t(leaveOut)])];
        const ZStat& p = (*z.place)[size_t(leaveOut)];
        q.n -= p.n; q.sx -= p.sx; q.sy -= p.sy;
        q.sxx -= p.sxx; q.sxy -= p.sxy; q.syy -= p.syy;
    }
    const int C = *z.C;
    if (C < 1) return true;
    bool ok = true;
    double wxx = 0, wxy = 0, wyy = 0;
    int N = 0;
    for (int c = 0; c < C; ++c) {
        const ZStat& q = s[size_t(c)];
        if (q.n < 1) ok = false;
        N += q.n;
        const double n = double(q.n), mx = q.sx / n, my = q.sy / n;
        wxx += q.sxx - n * mx * mx;
        wxy += q.sxy - n * mx * my;
        wyy += q.syy - n * my * my;
        (*z.mu)[2 * size_t(c)] = mx;
        (*z.mu)[2 * size_t(c) + 1] = my;
    }
    int dof = N - C;
    if (dof < 1) dof = 1;
    const double pooled[3] = { wxx / double(dof), wxy / double(dof), wyy / double(dof) };
    for (int c = 0; c < C; ++c) {
        const ZStat& q = s[size_t(c)];
        double own[3] = { pooled[0], pooled[1], pooled[2] };
        if (q.n > 1) {
            const double n = double(q.n), n1 = double(q.n - 1);
            const double mx = q.sx / n, my = q.sy / n, nmx = n * mx;
            own[0] = (q.sxx - mx * nmx) / n1;
            own[1] = (q.sxy - my * nmx) / n1;
            own[2] = (q.syy - n * my * my) / n1;
        }
        double* v = &(*z.cov)[3 * size_t(c)];
        for (int j = 0; j < 3; ++j) v[j] = lambda * pooled[j] + own[j] * (1.0 - lambda);
        if (v[0] < 1e-4) v[0] = 1e-4;
        if (v[2] < 0.16) v[2] = 0.16;
        const double lim = std::sqrt(v[2] * v[0]) * 0.999;
        if (v[1] > lim) v[1] = lim;
        if (v[1] < -lim) v[1] = -lim;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// The discriminant, and its test with each placement left out
// ---------------------------------------------------------------------------

// the sums every fold is made from: what one placement left out takes away is subtracted
struct LdaSums {
    size_t k = 0;
    std::vector<double> m0;              // the mean of all rows (the sums are about it)
    std::vector<int> n;                  // per material: rows
    Mat sum;                             // per material: the sum of (x - m0)
    Mat scat;                            // per material: the sum of (x - m0)(x - m0)', k x k
    Mat within;                          // per material: its scatter about its own mean
    std::vector<double> pooled;          // the sum of those
    std::vector<int> placeClass;         // per placement: its material
    std::vector<std::vector<int>> placeRows;
};

struct LdaFit {
    size_t k = 0;
    std::vector<double> sd;              // per feature: the pooled standard deviation, not below its floor
    std::vector<double> L;               // the shrunk correlation's Cholesky factor (lower), k x k
};

// the smallest standard deviation a feature is given: a phase (degrees), a shape point
// (log10 |Z|), the level (log10 |Z|)
double sdFloor(size_t i) { return i < 15 ? 0.2 : i < 30 ? 0.004 : 0.01; }

// One fit, placement `fold` left out (-1: none): the materials' means, the features' scales and
// the factor of their correlation, shrunk toward none. lambdaIn < 0: the shrinkage estimated from
// the residuals (Ledoit-Wolf, rawOut), else given; never below k / (k + 4 (placements - materials)).
bool foldLda(double lambdaIn, const LdaSums& S, const Mat& X, const std::vector<int>& cls, const std::vector<int>& place,
             int fold, Mat* means, LdaFit* fit, double* lambdaOut, double* rawOut)
{
    const size_t k = S.k;
    const int C = int(S.n.size());
    int foldClass = -1, nFold = 0;
    std::vector<double> sumFold, withinFold;
    if (fold != -1) {
        // the placement's rows taken out of its material's sums
        foldClass = S.placeClass[size_t(fold)];
        std::vector<double> scat = S.scat[size_t(foldClass)];
        sumFold = S.sum[size_t(foldClass)];
        nFold = S.n[size_t(foldClass)];
        const std::vector<int>& rows = S.placeRows[size_t(fold)];
        if (!rows.empty()) {
            std::vector<double> d(k, 0.0);
            for (int r : rows) {
                const std::vector<double>& x = X[size_t(r)];
                for (size_t i = 0; i < k; ++i) d[i] = x[i] - S.m0[i];
                for (size_t i = 0; i < k; ++i) {
                    sumFold[i] -= d[i];
                    for (size_t j = i; j < k; ++j) scat[i * k + j] -= d[j] * d[i];
                }
            }
            nFold -= int(rows.size());
        }
        if (nFold < 1) return false;
        for (size_t i = 1; i < k; ++i)
            for (size_t j = 0; j < i; ++j) scat[i * k + j] = scat[j * k + i];
        covFromSums(double(nFold), scat, sumFold, k, &withinFold);
    }
    means->assign(size_t(C), std::vector<double>(k, 0.0));
    int N = 0;
    for (int c = 0; c < C; ++c) {
        const int nc = c == foldClass ? nFold : S.n[size_t(c)];
        const std::vector<double>& sc = c == foldClass ? sumFold : S.sum[size_t(c)];
        if (nc < 1) return false;
        for (size_t i = 0; i < k; ++i) (*means)[size_t(c)][i] = sc[i] / double(nc) + S.m0[i];
        N += nc;
    }
    std::vector<double> W = S.pooled;
    if (foldClass >= 0) {
        const std::vector<double>& old = S.within[size_t(foldClass)];
        for (size_t i = 0; i < k * k; ++i) W[i] = (withinFold[i] - old[i]) + W[i];
    }
    int nPlaces = 0;
    for (size_t p = 0; p < S.placeClass.size(); ++p)
        if (int(p) != fold && S.placeClass[p] >= 0) ++nPlaces;
    int dof = N - C, dofP = nPlaces - C;
    if (dof < 1) dof = 1;
    if (dofP < 1) dofP = 1;
    fit->k = k;
    fit->sd.assign(k, 0.0);
    const double df = double(dof);
    std::vector<double> R;
    if (k == 0) {
        R.clear();
    } else {
        for (size_t i = 0; i < k; ++i) {
            const double v = W[i * k + i];
            const double s = std::sqrt(v > 0 ? v / df : 0.0 / df);
            const double fl = sdFloor(i);
            fit->sd[i] = fl > s ? fl : s;
        }
        R.assign(k * k, 0.0);
        for (size_t i = 0; i < k; ++i)
            for (size_t j = 0; j < k; ++j) R[i * k + j] = W[i * k + j] / (df * fit->sd[i] * fit->sd[j]);
    }
    double lambda = lambdaIn;
    if (lambdaIn < 0) {
        // how far the correlations are from what the residuals can tell (Ledoit-Wolf)
        Mat Z;
        for (size_t r = 0; r < X.size(); ++r) {
            if (place[r] == fold) continue;
            const std::vector<double>& m = (*means)[size_t(cls[r])];
            std::vector<double> z(k, 0.0);
            for (size_t i = 0; i < k; ++i) z[i] = (X[r][i] - m[i]) / fit->sd[i];
            Z.push_back(z);
        }
        double est = 1.0;
        if (k > 1) {
            const double nz = double(Z.size());
            const double f = Z.size() <= 1 ? 1.0 : nz - 1.0;
            double num = 0, den = 0;
            for (size_t a = 0; a + 1 < k; ++a)
                for (size_t b = a + 1; b < k; ++b) {
                    double v = 0;
                    if (!Z.empty()) {
                        double s = 0;
                        for (const auto& z : Z) s += z[a] * z[b];
                        for (const auto& z : Z) {
                            const double q = z[a] * z[b] - s / nz;
                            v += q * q;
                        }
                    }
                    const double rab = R[a * k + b];
                    den += rab * rab;
                    num += (nz / (f * df * df)) * v;
                }
            est = den > 0 ? num / den : 1.0;
        }
        if (rawOut) *rawOut = est;
        lambda = est;
    }
    const double least = double(unsigned(k)) / double(unsigned(k + size_t(dofP) * 4));
    if (lambda < least) lambda = least;
    double lam = 0.05;
    if (lambda > 0.05) {
        lam = lambda;
        if (!(lambda < 1.0)) lam = 1.0;
    }
    if (k) {
        for (size_t i = 0; i < k; ++i)
            for (size_t j = 0; j < k; ++j) {
                if (i == j) { R[i * k + j] = 1.0; continue; }
                double r = R[i * k + j];
                r = r < 1.0 ? (r > -1.0 ? r : -1.0) : 1.0;
                R[i * k + j] = (1.0 - lam) * r;
            }
        // its Cholesky factor, in place
        double d = R[0];
        for (size_t c = 0;; ++c) {
            if (!(d > 1e-300)) return false;
            R[c * k + c] = std::sqrt(d);
            if (c == k - 1) break;
            for (size_t r = c + 1; r < k; ++r) {
                double v = R[r * k + c];
                for (size_t m = 0; m < c; ++m) v -= R[r * k + m] * R[c * k + m];
                R[r * k + c] = v / std::sqrt(d);
            }
            d = R[(c + 1) * k + c + 1];
            for (size_t m = 0; m <= c; ++m) d -= R[(c + 1) * k + m] * R[(c + 1) * k + m];
        }
    }
    fit->L = R;
    if (lambdaOut) *lambdaOut = lam;
    return true;
}

// the rows of the fit and, per row left out, its distances and its blend: what the second layer
// is learnt from
struct StackRows {
    std::vector<int> sweep, cls, place;  // per row of the fit: its library sweep, material, placement
    std::vector<int> row;                // per left-out row: its row
    Mat d2, w;                           // per left-out row: its distances to the materials, its blend
};

// One fit of the library without the sweeps marked out: the model, its leave-one-placement-out
// test and (own) every sweep's left-out distance to its own material.
MatModel trainOnce(const MatLibrary& lib, const Mat& feat, const std::vector<char>& out, std::vector<double>* own,
                   StackRows* st = nullptr)
{
    MatModel md;
    own->assign(lib.sweeps.size(), kNaN);
    std::vector<std::string> labels;
    for (const std::string& l : lib.labels) {
        int n = 0;
        for (size_t i = 0; i < lib.sweeps.size(); ++i)
            if (!out[i] && !feat[i].empty() && lib.sweeps[i].label == l) ++n;
        if (n >= 2) labels.push_back(l);
    }
    if (labels.size() < 2) {
        md.why = "Label at least two materials, with at least two sweeps each.";
        return md;
    }
    // the rows: every sweep in use, its material and its placement (material, sample)
    std::map<std::pair<int, int>, int> placeOf;
    Mat X;
    std::vector<int> cls, place, idx;
    for (size_t i = 0; i < lib.sweeps.size(); ++i) {
        if (out[i]) continue;
        const auto it = std::find(labels.begin(), labels.end(), lib.sweeps[i].label);
        if (it == labels.end() || feat[i].empty()) continue;
        const int c = int(it - labels.begin());
        const std::pair<int, int> key(c, lib.sweeps[i].sample);
        if (placeOf.find(key) == placeOf.end()) {
            const int p = int(placeOf.size());
            placeOf[key] = p;
        }
        X.push_back(feat[i]);
        cls.push_back(c);
        place.push_back(placeOf[key]);
        idx.push_back(int(i));
    }
    const size_t C = labels.size();
    for (size_t c = 0; c < C; ++c)
        if (std::count(cls.begin(), cls.end(), int(c)) < 2) {
            md.why = "Material \"" + labels[c] + "\" needs at least two complete sweeps.";
            return md;
        }
    const size_t k = X[0].size();
    md.k = int(k);
    const int P = int(placeOf.size());

    LdaSums S;
    S.k = k;
    S.m0.assign(k, 0.0);
    if (k) {
        for (const auto& x : X)
            for (size_t i = 0; i < k; ++i) S.m0[i] = x[i] + S.m0[i];
        for (double& v : S.m0) v /= double(X.size());
    }
    S.n.assign(C, 0);
    S.sum.assign(C, std::vector<double>(k, 0.0));
    S.scat.assign(C, std::vector<double>(k * k, 0.0));
    S.placeClass.assign(size_t(P), -1);
    S.placeRows.assign(size_t(P), std::vector<int>());
    {
        std::vector<double> d(k, 0.0);
        for (size_t r = 0; r < X.size(); ++r) {
            const size_t c = size_t(cls[r]);
            S.placeClass[size_t(place[r])] = cls[r];
            S.placeRows[size_t(place[r])].push_back(int(r));
            ++S.n[c];
            if (!k) continue;
            for (size_t i = 0; i < k; ++i) d[i] = X[r][i] - S.m0[i];
            for (size_t i = 0; i < k; ++i) {
                S.sum[c][i] = d[i] + S.sum[c][i];
                for (size_t j = i; j < k; ++j) S.scat[c][i * k + j] = S.scat[c][i * k + j] + d[j] * d[i];
            }
        }
    }
    for (size_t c = 0; c < C; ++c)
        for (size_t i = 1; i < k; ++i)
            for (size_t j = 0; j < i; ++j) S.scat[c][i * k + j] = S.scat[c][j * k + i];
    S.within.resize(C);
    S.pooled.assign(k * k, 0.0);
    for (size_t c = 0; c < C; ++c) {
        if (S.n[c] < 1) continue;
        covFromSums(double(S.n[c]), S.scat[c], S.sum[c], k, &S.within[c]);
        for (size_t i = 0; i < k * k; ++i) S.pooled[i] = S.within[c][i] + S.pooled[i];
    }

    // the fit of all of it
    LdaFit fit;
    double raw = -1.0;
    if (!foldLda(-1.0, S, X, cls, place, -1, &md.mean, &fit, &md.lambda, &raw)) {
        md.why = "The library cannot be fitted yet.";
        return md;
    }
    // the shared covariance's inverse: D^-1 L^-T L^-1 D^-1
    {
        std::vector<double> Li(k * k, 0.0);
        for (size_t c = 0; c < k; ++c) {
            double v = 1.0;
            for (size_t r = c;;) {
                Li[r * k + c] = v / fit.L[r * k + r];
                if (++r == k) break;
                v = r == c ? 1.0 : 0.0;
                for (size_t m = c; m < r; ++m) v -= fit.L[r * k + m] * Li[m * k + c];
            }
        }
        Mat prec(k, std::vector<double>(k, 0.0));
        for (size_t i = 0; i < k; ++i)
            for (size_t j = i; j < k; ++j) {
                double s = 0;
                for (size_t m = j; m < k; ++m) s += Li[m * k + i] * Li[m * k + j];
                s = s / (fit.sd[j] * fit.sd[i]);
                prec[j][i] = s;
                prec[i][j] = s;
            }
        md.prec = prec;
    }

    // each placement left out: the fit without it, its sweeps told
    if (st) { st->sweep = idx; st->cls = cls; st->place = place; }
    Mat wm(C), H(C, std::vector<double>(C, 0.0));
    std::vector<double> g(C, 0.0), w, z, d2;
    Mat allD2;
    std::vector<int> trueCls;
    for (int p = 0; p < P; ++p) {
        Mat means;
        LdaFit f;
        if (!foldLda(raw, S, X, cls, place, p, &means, &f, nullptr, nullptr)) continue;
        ++md.nPlacements;
        // the means, whitened
        for (size_t c = 0; c < C; ++c) {
            wm[c].resize(k);
            for (size_t i = 0; i < k; ++i) {
                double v = means[c][i] / f.sd[i];
                for (size_t m = 0; m < i; ++m) v -= f.L[i * k + m] * wm[c][m];
                wm[c][i] = v / f.L[i * k + i];
            }
        }
        std::vector<double> cbar(k, 0.0);
        for (size_t c = 0; c < C; ++c)
            for (size_t i = 0; i < k; ++i) cbar[i] = wm[c][i] / double(int(C)) + cbar[i];
        for (size_t c = 0; c < C; ++c)
            for (size_t e = 0; e < C; ++e) {
                double s = 0;
                for (size_t i = 0; i < k; ++i) s += (wm[c][i] - cbar[i]) * (wm[e][i] - cbar[i]);
                H[c][e] = s;
            }
        for (int r : S.placeRows[size_t(p)]) {
            z.resize(k);
            for (size_t i = 0; i < k; ++i) {
                double v = X[size_t(r)][i] / f.sd[i];
                for (size_t m = 0; m < i; ++m) v -= f.L[i * k + m] * z[m];
                z[i] = v / f.L[i * k + i];
            }
            d2.assign(C, 0.0);
            for (size_t c = 0; c < C; ++c) {
                double s = 0;
                for (size_t i = 0; i < k; ++i) s += (z[i] - wm[c][i]) * (z[i] - wm[c][i]);
                d2[c] = s;
            }
            (*own)[size_t(idx[size_t(r)])] = d2[size_t(cls[size_t(r)])];
            if (st) {
                for (size_t c = 0; c < C; ++c) {
                    double s = 0;
                    for (size_t i = 0; i < k; ++i) s += (wm[c][i] - cbar[i]) * (z[i] - cbar[i]);
                    g[c] = s;
                }
                if (!simplexQp(H, g, d2, &w)) simplexPg(1e-8, H, g, d2, &w, 1500);
                st->row.push_back(r);
                st->d2.push_back(d2);
                st->w.push_back(w);
            }
            allD2.push_back(d2);
            trueCls.push_back(cls[size_t(r)]);
        }
    }
    md.confusion.assign(C, std::vector<int>(C, 0));
    std::vector<double> ownList;
    for (size_t t = 0; t < allD2.size(); ++t) {
        const std::vector<double>& d = allD2[t];
        size_t pred = 0;
        for (size_t j = 1; j < d.size(); ++j) if (d[j] < d[pred]) pred = j;
        ++md.confusion[size_t(trueCls[t])][pred];
        ++md.nTested;
        if (trueCls[t] == int(pred)) ++md.nCorrect;
        ownList.push_back(d[size_t(trueCls[t])]);
    }
    const double err = md.nTested ? double(md.nTested - md.nCorrect) / double(md.nTested) : 0.0;
    md.T = 1.0;
    md.eps = (double(md.nPlacements) * err + 1.0) / (double(md.nPlacements) + 2.0);
    // the temperature that makes the left-out answers most likely
    if (!allD2.empty()) {
        static const double kTs[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
        double best = -1e300;
        std::vector<double> pr;
        for (double T : kTs) {
            double ll = 0;
            for (size_t t = 0; t < allD2.size(); ++t) {
                softmaxT(T, allD2[t], &pr);
                const double q = pr[size_t(trueCls[t])];
                ll += std::log(q > 1e-9 ? q : 1e-9);
            }
            if (best < ll) { best = ll; md.T = T; }
        }
    }
    // how far a material's own sweeps fall from it
    if (ownList.empty()) {
        const double chi = chi2_999(int(k));
        md.ownLimit = chi;
        md.d2Limit = chi < 0.0 ? 0.0 : chi;
    } else {
        std::sort(ownList.begin(), ownList.end());
        const double kk = double(int(k));
        const double mx = ownList.back();
        const double med = ownList[ownList.size() / 2];
        double a = 1.0 - 2.0 / (kk * 9.0);
        double s = med / (kk * a * a * a);
        s = s > 1.0 ? s : 1.0;
        md.ownLimit = s * chi2_999(int(k));
        md.d2Limit = md.ownLimit < mx * 1.25 ? mx * 1.25 : md.ownLimit;
        const double chi = chi2_999(int(k));
        a = 1.0 - 2.0 / (kk * 9.0);
        const double x = (chi * med) / (kk * a * a * a);
        const double chiA = chi2_999(int(k));
        md.asideLimit = x * 3.0 < chiA ? chiA : x * 3.0;
        const double chi3 = chi2_999(int(k)) * 3.0;
        md.asideGross = x * 6.0 < chi3 ? chi3 : x * 6.0;
    }
    md.labels = labels;
    md.ok = true;
    return md;
}

// H7-2.8.0: the answer by Z and IPS, and the second layer, learnt from the left-out rows of the
// final fit.
void learnSecondLayer(MatModel* md, const MatLibrary& lib, const StackRows& st)
{
    int C = int(md->labels.size());
    const size_t R = st.sweep.size();
    std::vector<double> lz(R, kNaN), ip(R, kNaN);
    std::vector<char> have(R, 0);
    for (size_t r = 0; r < R; ++r) have[r] = zipsOf(lib.sweeps[size_t(st.sweep[r])].pts, &lz[r], &ip[r]);
    int P = 0;
    for (int p : st.place) if (P < p + 1) P = p + 1;
    std::vector<ZStat> cs(static_cast<size_t>(C)), ps(static_cast<size_t>(P));
    std::vector<int> placeClass(static_cast<size_t>(P), -1);
    std::vector<std::vector<int>> placeRows(static_cast<size_t>(P));
    for (size_t r = 0; r < R; ++r) {
        if (!have[r]) continue;
        const double x = lz[r], y = ip[r];
        ZStat& c = cs[size_t(st.cls[r])];
        c.n += 1; c.sx += x; c.sy += y; c.sxx += x * x; c.sxy += x * y; c.syy += y * y;
        ZStat& q = ps[size_t(st.place[r])];
        q.n += 1; q.syy += y * y; q.sx += x; q.sy += y; q.sxy += x * y; q.sxx += x * x;
        placeClass[size_t(st.place[r])] = st.cls[r];
        placeRows[size_t(st.place[r])].push_back(int(r));
    }
    for (const ZStat& c : cs) if (c.n < 2) return;
    std::vector<double> mu(2 * size_t(C), 0.0), cov(3 * size_t(C), 0.0);
    const ZipsCtx ctx{ &cs, &ps, &placeClass, &C, &mu, &cov };
    static const double kLambdas[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
    static const double kTs[13] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
    double ll[5][13] = {};
    int nRows = 0;
    std::vector<double> score, d2, pr;
    auto scores = [&](int r) {
        score.resize(size_t(C));
        d2.resize(size_t(C));
        for (int c = 0; c < C; ++c)
            qda2(lz[size_t(r)], ip[size_t(r)], &mu[2 * size_t(c)], &cov[3 * size_t(c)], &d2[size_t(c)], &score[size_t(c)]);
    };
    // the mix of pooled and own covariance and the temperature, placements left out
    for (int p = 0; p < P; ++p) {
        const std::vector<int>& rows = placeRows[size_t(p)];
        if (rows.empty()) continue;
        bool any = false;
        for (int li = 0; li < 5; ++li) {
            if (!zipsStats(kLambdas[li], ctx, p)) break;
            any = true;
            for (int r : rows) {
                scores(r);
                for (int ti = 0; ti < 13; ++ti) {
                    softmaxT(kTs[ti], score, &pr);
                    const double q = pr[size_t(st.cls[size_t(r)])];
                    ll[li][ti] += std::log(q > 1e-9 ? q : 1e-9);
                }
            }
        }
        if (any) nRows += int(rows.size());
    }
    md->zips = MatZipsModel();
    int bl = 4, bt = 2;
    if (nRows) {
        double best = -1e300;
        for (int li = 0; li < 5; ++li)
            for (int ti = 0; ti < 13; ++ti)
                if (ll[li][ti] > best) { best = ll[li][ti]; bl = li; bt = ti; }
    }
    md->zips.lambda = kLambdas[bl];
    md->zips.T = kTs[bt];
    // its left-out test
    Mat zrows(R);
    std::vector<double> ownZ;
    int nPl = 0;
    double lim = chi2_999(2);
    if (P > 0) {
        for (int p = 0; p < P; ++p) {
            const std::vector<int>& rows = placeRows[size_t(p)];
            if (rows.empty() || !zipsStats(md->zips.lambda, ctx, p)) continue;
            ++nPl;
            for (int r : rows) {
                scores(r);
                size_t b = 0;
                for (size_t j = 1; j < score.size(); ++j) if (score[j] < score[b]) b = j;
                ++md->zips.nTested;
                if (st.cls[size_t(r)] == int(b)) ++md->zips.nCorrect;
                ownZ.push_back(d2[size_t(st.cls[size_t(r)])]);
                zrows[size_t(r)] = score;
            }
        }
        const double err = md->zips.nTested ? double(md->zips.nTested - md->zips.nCorrect) / double(md->zips.nTested) : 0.0;
        md->zips.eps = (err * double(nPl) + 1.0) / (double(nPl) + 2.0);
        if (!ownZ.empty()) {
            std::sort(ownZ.begin(), ownZ.end());
            const double s = ownZ[ownZ.size() / 2] / (2.0 * (1.0 - 2.0 / 18.0) * (1.0 - 2.0 / 18.0) * (1.0 - 2.0 / 18.0));
            lim = chi2_999(2);
            if (s > 1.0) lim = s * chi2_999(2);
            const double m = ownZ.back() * 1.25;
            lim = m > lim ? m : lim;
        }
    } else {
        md->zips.eps = 0.5;
    }
    md->zips.d2Limit = lim;
    // the final means and covariances, all placements
    zipsStats(md->zips.lambda, ctx, -1);
    md->zips.mean.assign(size_t(C), std::vector<double>(2, 0.0));
    md->zips.cov.assign(size_t(C), std::vector<double>(3, 0.0));
    for (int c = 0; c < C; ++c) {
        md->zips.mean[size_t(c)][0] = mu[2 * size_t(c)];
        md->zips.mean[size_t(c)][1] = mu[2 * size_t(c) + 1];
        for (int j = 0; j < 3; ++j) md->zips.cov[size_t(c)][size_t(j)] = cov[3 * size_t(c) + size_t(j)];
    }
    md->zips.ok = true;
    // the blend told right: its largest weight on the right material
    md->nBlendCorrect = 0;
    for (size_t t = 0; t < st.row.size(); ++t) {
        const std::vector<double>& w = st.w[t];
        size_t b = 0;
        for (size_t j = 1; j < w.size(); ++j) if (w[b] < w[j]) b = j;
        if (st.cls[size_t(st.row[t])] == int(b)) ++md->nBlendCorrect;
    }

    // the second layer: the three answers' log-probabilities, per left-out row
    Mat L0, L1, L2;
    std::vector<int> tc;
    for (size_t t = 0; t < st.row.size(); ++t) {
        const int r = st.row[t];
        if (zrows[size_t(r)].empty()) continue;
        std::vector<double> p, l;
        softmaxT(md->T, st.d2[t], &p);
        logs(p, &l);
        L0.push_back(l);
        logs(st.w[t], &l);
        L1.push_back(l);
        softmaxT(md->zips.T, zrows[size_t(r)], &p);
        logs(p, &l);
        L2.push_back(l);
        tc.push_back(st.cls[size_t(r)]);
    }
    md->stack = MatStack();
    static const double kW0[3] = { 1.0, 0.5, 0.5 };
    double w[3] = { kW0[0], kW0[1], kW0[2] };
    const size_t nt = tc.size();
    std::vector<double> pf;
    auto rowLogs = [&](size_t t, const std::vector<double>** L) { L[0] = &L0[t]; L[1] = &L1[t]; L[2] = &L2[t]; };
    if (nt) {
        // Newton's method on the log-likelihood of the right materials, held near the prior weights
        int iters = 60;
        for (;;) {
            double f = 0, g[3] = { 0, 0, 0 }, H[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
            for (size_t t = 0; t < nt; ++t) {
                const std::vector<double>* L[3];
                rowLogs(t, L);
                stackSoftmax(w, L, &pf);
                const size_t y = size_t(tc[t]);
                const double q = pf[y];
                f += std::log(q > 1e-300 ? q : 1e-300);
                double E[3] = { 0, 0, 0 };
                for (int c = 0; c < C; ++c) {
                    const double pc = pf[size_t(c)];
                    for (int j = 0; j < 3; ++j) E[j] += pc * (*L[j])[size_t(c)];
                }
                for (int j = 0; j < 3; ++j) g[j] = ((*L[j])[y] - E[j]) + g[j];
                for (int c = 0; c < C; ++c) {
                    const double pc = pf[size_t(c)];
                    for (int j = 0; j < 3; ++j) {
                        const double a = (*L[j])[size_t(c)] - E[j];
                        for (int l = 0; l < 3; ++l) H[j][l] -= ((*L[l])[size_t(c)] - E[l]) * a * pc;
                    }
                }
            }
            for (int j = 0; j < 3; ++j) {
                const double d = w[j] - kW0[j];
                f -= d * d;
                H[j][j] -= 2.0;
                g[j] -= d + d;
            }
            double A[3][4];
            for (int j = 0; j < 3; ++j) {
                for (int l = 0; l < 3; ++l) A[j][l] = -H[j][l];
                A[j][3] = g[j];
            }
            for (int col = 0; col < 3; ++col) {
                int p = col;
                for (int r = col + 1; r < 3; ++r) if (std::fabs(A[p][col]) < std::fabs(A[r][col])) p = r;
                for (int q = 0; q < 4; ++q) std::swap(A[col][q], A[p][q]);
                for (int r = 0; r < 3; ++r) {
                    if (r == col) continue;
                    const double fc = A[r][col] / A[col][col];
                    for (int q = 0; q < 4; ++q) A[r][q] -= A[col][q] * fc;
                }
            }
            double d[3];
            for (int j = 0; j < 3; ++j) d[j] = A[j][3] / A[j][j];
            double step = 1.0, fn = 0, wn[3];
            int tries = 30;
            bool accepted = false;
            for (;;) {
                for (int j = 0; j < 3; ++j) {
                    double v = w[j] + d[j] * step;
                    if (v < 20.0) { if (!(v > 0.0)) v = 0.0; }
                    else v = 20.0;
                    wn[j] = v;
                }
                fn = 0;
                for (size_t t = 0; t < nt; ++t) {
                    const std::vector<double>* L[3];
                    rowLogs(t, L);
                    stackSoftmax(wn, L, &pf);
                    const double q = pf[size_t(tc[t])];
                    fn += std::log(q > 1e-300 ? q : 1e-300);
                }
                for (int j = 0; j < 3; ++j) {
                    const double e = wn[j] - kW0[j];
                    fn -= e * e;
                }
                if (!(fn < f)) { accepted = true; break; }
                step *= 0.5;
                if (--tries == 0) break;
            }
            if (!accepted) break;
            double change = 0;
            for (int j = 0; j < 3; ++j) { change += std::fabs(wn[j] - w[j]); w[j] = wn[j]; }
            if (change < 1e-7) break;
            if (fn - f < 1e-10 || --iters == 0) break;
        }
    }
    for (int j = 0; j < 3; ++j) md->stack.w[j] = w[j];
    for (size_t t = 0; t < nt; ++t) {
        const std::vector<double>* L[3];
        rowLogs(t, L);
        stackSoftmax(md->stack.w, L, &pf);
        ++md->stack.nTested;
        size_t b = 0;
        for (size_t j = 1; j < pf.size(); ++j) if (pf[j] > pf[b]) b = j;
        if (tc[t] == int(b)) ++md->stack.nCorrect;
    }
    const double err = md->stack.nTested ? double(md->stack.nTested - md->stack.nCorrect) / double(md->stack.nTested) : 0.0;
    md->stack.ok = true;
    md->stack.eps = (double(md->nPlacements) * err + 1.0) / (double(md->nPlacements) + 2.0);
}
} // namespace

MatModel trainMaterials(const MatLibrary& lib, bool normalised)
{
    const size_t n = lib.sweeps.size();
    std::vector<std::vector<double>> feat(n);
    for (size_t i = 0; i < n; ++i) feat[i] = matFeatures(lib.sweeps[i].pts, lib.sweeps[i].needleInfo(), normalised);
    std::vector<char> out(n, 0), auto_(n, 0);
    for (size_t i = 0; i < n; ++i) if (lib.sweeps[i].use == MatSweep::kAside) out[i] = 1;   // set aside by hand
    for (size_t i = 0; i < n; ++i) if (lib.sweeps[i].qc == "suspect") out[i] = 1;          // H7-2.10.0: its needle moved
    // H7-2.12.0: a sweep no saline check has confirmed yet is not learnt, nor a material left out of the comparison
    for (size_t i = 0; i < n; ++i) if (lib.sweeps[i].qc == "pending" || lib.isOff(lib.sweeps[i].label)) out[i] = 1;
    // H7-2.12.6: its tip surface moved between the checks around it: its reference is not known, so it is not
    // compared needle-normalised (as measured it is learnt)
    if (normalised) for (size_t i = 0; i < n; ++i) if (lib.sweeps[i].qc == "ok-raw") out[i] = 1;
    // H7-2.11.0: a sweep of a shorted or bridged tip has none of its material in it
    std::vector<char> tip(n, 0);
    for (size_t i = 0; i < n; ++i)
        if (matTipFault(lib.sweeps[i].pts, lib.sweeps[i].refR, kTipBridgedRatio, nullptr) != TipFault::None) out[i] = tip[i] = 1;
    std::vector<double> own;
    MatModel md;
    // H7-2.5.2: a sweep far from its own material's other sweeps is set aside by itself, and the
    // library fitted again without it (twice more at most: one far sweep can hide another).
    for (int pass = 0; pass < 4; ++pass) {
        md = trainOnce(lib, feat, out, &own);
        if (!md.ok || pass == 3) break;
        const double far1 = md.asideLimit, far2 = md.asideGross;
        if (!std::isfinite(far1)) break;
        bool more = false;
        for (const std::string& l : md.labels) {
            // this material's sweeps in use, its placements, and how many may be set aside
            std::vector<size_t> mine;
            std::vector<int> places;
            int total = 0, already = 0;
            for (size_t i = 0; i < n; ++i) {
                if (lib.sweeps[i].label != l || lib.sweeps[i].use == MatSweep::kAside) continue;
                if (out[i] && !auto_[i]) continue;          // not learnt for another reason (QC, tip)
                ++total;
                if (auto_[i]) { ++already; continue; }
                mine.push_back(i);
                if (std::find(places.begin(), places.end(), lib.sweeps[i].sample) == places.end()) places.push_back(lib.sweeps[i].sample);
            }
            const int cap = std::max(1, total / 5) - already;            // at most one in five
            const bool enough = places.size() >= size_t(kAsideMinPlaces);  // else only the grossly far
            std::vector<size_t> cand;
            for (size_t i : mine) {
                if (lib.sweeps[i].use == MatSweep::kUse || !std::isfinite(own[i])) continue;   // kept by hand
                if (own[i] > (enough ? far1 : far2)) cand.push_back(i);
            }
            std::sort(cand.begin(), cand.end(), [&](size_t p, size_t q) { return own[p] > own[q]; });
            int left = int(mine.size());
            for (int k = 0; k < int(cand.size()) && k < cap && left > 2; ++k) {
                out[cand[size_t(k)]] = 1; auto_[cand[size_t(k)]] = 1; --left; more = true;
            }
        }
        if (!more) break;
    }
    // H7-2.8.0: the final fit once more, with its left-out blends, for the answer by Z and IPS and
    // the second layer (the same model: the same sweeps)
    if (md.ok) {
        StackRows st;
        md = trainOnce(lib, feat, out, &own, &st);
        if (md.ok) learnSecondLayer(&md, lib, st);
    }
    // every sweep's state and its distance to its own material
    md.state.assign(n, MatModel::kNotIn);
    md.ownD2.assign(n, kNaN);
    for (size_t i = 0; i < n; ++i) {
        const MatSweep& s = lib.sweeps[i];
        if (tip[i]) { md.state[i] = MatModel::kTipFault; ++md.nTipFault; continue; }
        if (s.qc == "suspect") { md.state[i] = MatModel::kQcSuspect; ++md.nQcSuspect; continue; }
        if (lib.isOff(s.label)) { md.state[i] = MatModel::kOff; ++md.nOff; continue; }
        if (s.qc == "pending") { md.state[i] = MatModel::kQcPending; ++md.nQcPending; continue; }
        if (normalised && s.qc == "ok-raw") { md.state[i] = MatModel::kRawOnly; ++md.nRawOnly; continue; }
        if (s.use == MatSweep::kAside) md.state[i] = MatModel::kAsideByHand;
        else if (auto_[i]) md.state[i] = MatModel::kAsideAuto;
        else if (md.ok && std::find(md.labels.begin(), md.labels.end(), s.label) != md.labels.end())
            md.state[i] = s.use == MatSweep::kUse ? MatModel::kUsedByHand : MatModel::kUsed;
        if (!md.ok) continue;
        const auto it = std::find(md.labels.begin(), md.labels.end(), s.label);
        if (it == md.labels.end()) continue;
        if (out[i]) {
            // a sweep set aside: its distance to its own material as the model now has it
            const std::vector<double>& f = feat[i];
            if (!f.empty()) md.ownD2[i] = maha(f, md.mean[size_t(it - md.labels.begin())], md.prec);
        } else {
            md.ownD2[i] = own[i];
        }
        if (md.state[i] == MatModel::kAsideAuto) ++md.nAside;
        if (md.state[i] == MatModel::kUsed || md.state[i] == MatModel::kUsedByHand) {
            if (feat[i].empty()) md.state[i] = MatModel::kNotIn;
            else ++md.nSweeps;
        }
    }
    md.normalised = normalised;
    // H7-2.12.0 / 2.12.5: when it is not learnt, which of the sweeps there are cannot be learnt, and why
    if (!md.ok) {
        int byHand = 0, noFeat = 0;
        for (size_t i = 0; i < n; ++i) {
            if (lib.sweeps[i].use == MatSweep::kAside) ++byHand;
            else if (feat[i].empty() && !tip[i]) ++noFeat;
        }
        std::vector<std::string> w;
        auto add = [&](int k, const char* one, const char* many, const char* why) {
            if (k > 0) w.push_back(std::to_string(k) + (k == 1 ? one : many) + why);
        };
        add(md.nQcSuspect, " SUSPECT", " SUSPECT", " (the needle's next saline check found it moved: see Suspect sweeps, below)");
        add(md.nQcPending, " waits", " wait", " for its needle's next saline check");
        add(md.nTipFault, " of a shorted or bridged tip", " of a shorted or bridged tip", "");
        add(md.nOff, " of a material left out", " of materials left out", "");
        add(byHand, " set aside by hand", " set aside by hand", "");
        add(noFeat, " lacks some of the 15 frequencies", " lack some of the 15 frequencies", "");
        if (!w.empty()) {
            std::string t = " Not learnt now:";
            for (size_t k = 0; k < w.size(); ++k) t += (k ? "; " : " ") + w[k];
            md.why += t + ".";
        }
    }
    return md;
}

MatMatch MatModel::classify(const std::vector<Point>& pts, double cpF) const
{
    MatNeedle n;
    n.cpF = cpF;
    return classify(pts, n);
}

MatMatch MatModel::classify(const std::vector<Point>& pts, const MatNeedle& n) const
{
    MatMatch m;
    if (!ok) { m.why = why; return m; }
    if (normalised && !n.hasRef()) { m.why = "This needle has no saline reference."; return m; }
    const std::vector<double> f = matFeatures(pts, n, normalised);
    if (f.empty() || mean.empty() || f.size() != mean[0].size()) {
        m.why = "The sweep lacks some of the 15 frequencies.";
        return m;
    }
    const size_t C = labels.size();
    for (size_t c = 0; c < C; ++c) m.d2.push_back(maha(f, mean[c], prec));
    softmaxT(T, m.d2, &m.prob);
    for (double& p : m.prob) p = eps / double(C) + (1.0 - eps) * p;
    m.labels = labels;
    m.best = 0;
    for (size_t j = 1; j < m.prob.size(); ++j) if (m.prob[j] > m.prob[size_t(m.best)]) m.best = int(j);
    m.d2Limit = d2Limit;
    // H7-2.8.0: the blend — the mix of the materials' means closest to the sweep
    {
        const size_t k = f.size(), nc = mean.size();
        std::vector<double> mbar(k, 0.0);
        for (size_t c = 0; c < nc; ++c)
            for (size_t i = 0; i < k; ++i) mbar[i] = mean[c][i] / double(nc) + mbar[i];
        Mat W(nc, std::vector<double>(k, 0.0));       // (mean_c - mbar)' P
        for (size_t c = 0; c < nc; ++c)
            for (size_t j = 0; j < k; ++j) {
                double s = W[c][j];
                for (size_t i = 0; i < k; ++i) s = s + (mean[c][i] - mbar[i]) * prec[j][i];
                W[c][j] = s;
            }
        Mat H(nc, std::vector<double>(nc, 0.0));
        std::vector<double> g(nc, 0.0);
        for (size_t c = 0; c < nc; ++c) {
            for (size_t e = 0; e < nc; ++e) {
                double s = H[c][e];
                for (size_t i = 0; i < k; ++i) s = s + (mean[e][i] - mbar[i]) * W[c][i];
                H[c][e] = s;
            }
            double s = g[c];
            for (size_t i = 0; i < k; ++i) s = s + (f[i] - mbar[i]) * W[c][i];
            g[c] = s;
        }
        if (!simplexQp(H, g, m.d2, &m.w)) simplexPg(1e-10, H, g, m.d2, &m.w, 4000);
        std::vector<double> mix(k, 0.0);
        for (size_t c = 0; c < nc; ++c)
            for (size_t i = 0; i < k; ++i) mix[i] = mix[i] + mean[c][i] * m.w[c];
        m.blendD2 = maha(f, mix, prec);
    }
    m.unlike = m.d2[size_t(m.best)] > d2Limit;
    m.ok = true;
    m.normalised = normalised;
    // H7-2.8.0: the answer by Z at 1 kHz and IPS
    double logZ = kNaN, ips = kNaN;
    if (zips.ok && zipsOf(pts, &logZ, &ips)) {
        std::vector<double> score, d2, raw;
        zipsScores(zips, logZ, ips, &score, &d2);
        softmaxT(zips.T, score, &raw);
        m.zProb = raw;
        int zb = 0;
        for (double& p : m.zProb) p = zips.eps / double(C) + (1.0 - zips.eps) * p;
        for (size_t j = 1; j < m.zProb.size(); ++j) if (m.zProb[j] > m.zProb[size_t(zb)]) zb = int(j);
        m.zBest = m.zProb.empty() ? 0 : zb;
        double dmin = d2.empty() ? kNaN : d2[0];
        for (size_t j = 1; j < d2.size(); ++j) if (d2[j] < dmin) dmin = d2[j];
        m.zUnlike = zips.d2Limit < dmin;
        m.z1k = std::pow(10.0, logZ);
        m.zOk = true;
        m.ips = ips;
        // the final answer
        if (stack.ok) {
            std::vector<double> p0, l0, l1, l2;
            softmaxT(T, m.d2, &p0);
            logs(p0, &l0);
            logs(m.w, &l1);
            logs(raw, &l2);
            const std::vector<double>* L[3] = { &l0, &l1, &l2 };
            stackSoftmax(stack.w, L, &m.fProb);
            int fb = 0;
            for (double& p : m.fProb) p = stack.eps / double(C) + (1.0 - stack.eps) * p;
            for (size_t j = 1; j < m.fProb.size(); ++j) if (m.fProb[j] > m.fProb[size_t(fb)]) fb = int(j);
            m.fBest = fb;
            m.fOk = true;
        }
    }
    return m;
}

} // namespace birx
