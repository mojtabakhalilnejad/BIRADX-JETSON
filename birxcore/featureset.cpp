#include "featureset.h"
#include <algorithm>
#include <cmath>

namespace birx {

bool FeatureExtractor::leastSquares(const std::vector<double>& x,
                                    const std::vector<double>& y,
                                    double* slope, double* intercept, double* rms) {
    const size_t n = x.size();
    if (n < 2 || y.size() != n) return false;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) { const double dx = x[i]-mx; sxx += dx*dx; sxy += dx*(y[i]-my); }
    if (sxx < 1e-9) return false;
    const double b = sxy / sxx;
    const double a = my - b * mx;
    double ss = 0;
    for (size_t i = 0; i < n; ++i) { const double r = y[i] - (a + b*x[i]); ss += r*r; }
    if (slope)     *slope = b;
    if (intercept) *intercept = a;
    if (rms)       *rms = std::sqrt(ss / n);
    return std::isfinite(b);
}

double FeatureExtractor::theilSen(const std::vector<double>& x,
                                  const std::vector<double>& y) {
    const size_t n = x.size();
    if (n < 2 || y.size() != n) return kNaN;
    std::vector<double> s;
    s.reserve(n * (n - 1) / 2);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const double dx = x[j] - x[i];
            if (std::fabs(dx) < 1e-9) continue;
            s.push_back((y[j] - y[i]) / dx);
        }
    if (s.empty()) return kNaN;
    std::sort(s.begin(), s.end());
    const size_t m = s.size();
    return (m % 2) ? s[m/2] : 0.5 * (s[m/2 - 1] + s[m/2]);
}

bool FeatureExtractor::quadratic(const std::vector<double>& x,
                                 const std::vector<double>& y, double* c2) {
    const size_t n = x.size();
    if (n < 4 || y.size() != n) return false;
    // Centre x to keep the 3x3 normal equations well conditioned.
    double mx = 0;
    for (double v : x) mx += v;
    mx /= n;
    double S0 = static_cast<double>(n), S1 = 0, S2 = 0, S3 = 0, S4 = 0;
    double T0 = 0, T1 = 0, T2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double u = x[i] - mx, u2 = u*u;
        S1 += u; S2 += u2; S3 += u2*u; S4 += u2*u2;
        T0 += y[i]; T1 += u*y[i]; T2 += u2*y[i];
    }
    const double m[3][4] = {
        { S0, S1, S2, T0 },
        { S1, S2, S3, T1 },
        { S2, S3, S4, T2 }
    };
    double a[3][4];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) a[i][j] = m[i][j];
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r) if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) return false;
        if (piv != col) for (int j = 0; j < 4; ++j) std::swap(a[col][j], a[piv][j]);
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const double k = a[r][col] / a[col][col];
            for (int j = col; j < 4; ++j) a[r][j] -= k * a[col][j];
        }
    }
    const double c = a[2][3] / a[2][2];
    if (!std::isfinite(c)) return false;
    if (c2) *c2 = c;
    return true;
}

Features FeatureExtractor::extract(const Spectrum& s) const {
    Features f;
    if (s.empty()) return f;

    // --- placement and impedance ------------------------------------------
    if (const Point* p1k = s.at(1000.0, 200.0)) {
        f.z1k       = p1k->magOhm;
        f.logZ1k    = std::log10(std::max(1e-6, p1k->magOhm));
        f.rangeAt1k = p1k->range;
    }
    if (const Point* p100k = s.at(100000.0, 2000.0))
        if (std::isfinite(f.z1k) && p100k->magOhm > 0) f.zRatio = f.z1k / p100k->magOhm;
    if (const Point* p500k = s.at(500000.0, 2000.0)) {
        f.rangeAt500k = p500k->range;
        if (std::isfinite(f.z1k) && p500k->magOhm > 0) f.zRatioHigh = f.z1k / p500k->magOhm;
    }

    // --- the firmware's own IPS, reproduced exactly -------------------------
    {
        const Point* a = s.at(100000.0, 2000.0);
        const Point* b = s.at(500000.0, 2000.0);
        if (a && b) f.ipsLegacy = InstrumentConstants::kIpsPerDecade * (b->phaseDeg - a->phaseDeg);
    }

    // --- the fitted slope over the band ------------------------------------
    std::vector<Point> band = s.band(m_cfg.bandLoHz, m_cfg.bandHiHz);
    std::vector<double> x, y;
    x.reserve(band.size()); y.reserve(band.size());
    int lastRange = 0, switches = 0;
    double phaseMin = 1e9, logFMin = kNaN;

    for (const Point& p : band) {
        double ph = p.phaseDeg;
        if (m_cfg.undoAppliedCorrection)
            ph += InstrumentConstants::appliedPhaseCorrection(p.freqHz, p.magOhm, p.range);
        x.push_back(std::log10(p.freqHz));
        y.push_back(ph);
        if (ph < phaseMin) { phaseMin = ph; logFMin = std::log10(p.freqHz); }
        if (p.range > 0) {
            if (lastRange > 0 && p.range != lastRange) ++switches;
            lastRange = p.range;
        }
    }

    f.nFit = static_cast<int>(x.size());
    f.rangeSwitches = switches;
    if (phaseMin < 1e8) { f.phaseMinDeg = phaseMin; f.logFAtPhaseMin = logFMin; }

    if (f.nFit >= 2) {
        double slope = 0, icept = 0, rms = 0;
        if (leastSquares(x, y, &slope, &icept, &rms)) {
            f.ipsFit    = slope;          // degrees per decade — same unit as IPS
            f.fitRmsDeg = rms;
            // The level of the phase curve, read off the same fit. Taking it
            // from the fit rather than from the single measured point at the
            // reference frequency uses all the points in the band, so it
            // carries the same noise reduction the slope does.
            f.phaseAtRefDeg = icept + slope * std::log10(m_cfg.refFreqHz);
        }
        f.ipsTheilSen = theilSen(x, y);
        double c2 = 0;
        if (quadratic(x, y, &c2)) f.curvature = c2;
    }
    // --- the Cole model over the whole arc ---------------------------------
    // Fitted on its own band, not the phase-slope band: the arc needs the low
    // frequencies, while the slope is defined on the high-frequency side.
    if (m_cfg.fitColeModel) {
        const ColeFit c = fitCole(s, m_cfg.cole);
        f.coleRmsRel = c.rmsRel;        // recorded even when the fit is rejected
        if (c.ok) {
            f.coleR0    = c.r0;
            f.coleRinf  = c.rInf;
            f.coleRatio = c.ratio();
            f.coleLogFc = std::log10(c.fcHz);
            f.coleAlpha = c.alpha;
        }
    }
    return f;
}

} // namespace birx
