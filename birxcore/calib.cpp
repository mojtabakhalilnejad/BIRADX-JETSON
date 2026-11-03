#include "calib.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace birx {

const std::vector<double>& InstrumentConstants::sweepFrequencies() {
    static const std::vector<double> f = {
        500000.0, 300000.0, 250000.0, 225000.0, 199000.0, 175000.0, 125000.0,
        100000.0,  75000.0,  50000.0,  25000.0,  10000.0,   5000.0,   1000.0, 500.0
    };
    return f;
}

const std::array<double, 5>& InstrumentConstants::rangeThresholds() {
    static const std::array<double, 5> t = { 500.0, 905.0, 1800.0, 2500.0, 4700.0 };
    return t;
}

double InstrumentConstants::firmwarePh500OffsetIps(int range) {
    switch (range) {
    case 1: return -1.0;
    case 2: return -1.5;
    case 3: return  0.0;
    case 4: return  0.0;
    case 5: return  0.7;
    case 6: return 15.0;
    default: return 0.0;
    }
}

double InstrumentConstants::appliedPhaseCorrection(double freqHz, double magOhm, int range) {
    if (range != 1 && range != 2) return 0.0;
    const bool is500k = (std::fabs(freqHz - 499000.0) < 1500.0) ||
                        (std::fabs(freqHz - 500000.0) < 1500.0) ||
                        (std::fabs(freqHz - 500200.0) < 1500.0);
    if (!is500k) return 0.0;
    if (magOhm < 200.0)
        return -0.000080 * magOhm * magOhm + 0.004000 * magOhm + 3.400000;
    return 0.9;
}

// ---------------------------------------------------------------------------

RangeCalibrator::RangeCalibrator() {
    m_stepSum.fill(0.0);
    m_stepN.fill(0);
    m_seen.fill(false);
    m_offset.fill(0.0);
}

namespace {

// Local linear extrapolation of phase(log10 f) from up to `want` points on one
// side of a boundary, evaluated at x0. Returns false when the points are too
// few or too bunched for the slope to mean anything.
bool extrapolate(const std::vector<Point>& pts, size_t from, size_t to,
                 double x0, size_t want, double* out) {
    std::vector<double> xs, ys;
    for (size_t i = from; i < to && xs.size() < want; ++i) {
        if (!pts[i].valid()) continue;
        xs.push_back(std::log10(pts[i].freqHz));
        ys.push_back(pts[i].phaseDeg);
    }
    if (xs.size() < 2) return false;
    double mx = 0, my = 0;
    for (size_t i = 0; i < xs.size(); ++i) { mx += xs[i]; my += ys[i]; }
    mx /= xs.size(); my /= ys.size();
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < xs.size(); ++i) { sxx += (xs[i]-mx)*(xs[i]-mx); sxy += (xs[i]-mx)*(ys[i]-my); }
    if (sxx < 1e-6) return false;
    const double slope = sxy / sxx;
    if (!std::isfinite(slope) || std::fabs(slope) > 400.0) return false;
    *out = my + slope * (x0 - mx);
    return true;
}

} // namespace

void RangeCalibrator::observe(const Spectrum& s) {
    // Work in ascending frequency; the gain ratchets DOWN as frequency rises.
    std::vector<Point> p = s.band(0.0, 1e9);
    for (const Point& q : p)
        if (q.range >= 1 && q.range <= kRanges) m_seen[q.range] = true;
    if (p.size() < 6) return;

    for (size_t i = 1; i < p.size(); ++i) {
        const int rHi = p[i-1].range;    // lower frequency  -> higher range
        const int rLo = p[i].range;      // higher frequency -> lower  range
        if (rHi <= 0 || rLo <= 0) continue;
        if (rHi != rLo + 1) continue;    // only adjacent single-step boundaries

        // Boundary placed midway in log frequency between the two points.
        const double x0 = 0.5 * (std::log10(p[i-1].freqHz) + std::log10(p[i].freqHz));

        // Extrapolate from each side, using points strictly on that side.
        double vLow = 0, vHigh = 0;
        size_t loFrom = i, loTo = p.size();          // ascending, same range as p[i]
        size_t hiEndExclusive = i;                   // descending side, p[i-1] and below
        // Collect the low-range (higher frequency) side.
        {
            size_t to = loFrom;
            while (to < loTo && p[to].range == rLo) ++to;
            if (!extrapolate(p, loFrom, to, x0, 4, &vLow)) continue;
        }
        // Collect the high-range (lower frequency) side, walking downwards.
        {
            std::vector<Point> side;
            for (size_t k = hiEndExclusive; k-- > 0; ) {
                if (p[k].range != rHi) break;
                side.push_back(p[k]);
                if (side.size() >= 4) break;
            }
            std::reverse(side.begin(), side.end());
            if (!extrapolate(side, 0, side.size(), x0, 4, &vHigh)) continue;
        }

        const double step = vHigh - vLow;    // what the high range reads extra
        if (!std::isfinite(step) || std::fabs(step) > 30.0) continue;
        addStep(rLo, rHi, step);
    }
}

void RangeCalibrator::addStep(int lowRange, int highRange, double stepDeg) {
    if (highRange != lowRange + 1) return;
    if (lowRange < 1 || lowRange >= kRanges) return;
    m_stepSum[lowRange] += stepDeg;
    m_stepN[lowRange]   += 1;
    m_dirty = true;
}

void RangeCalibrator::rebuild() const {
    m_offset.fill(0.0);
    // offset(1) = 0 by definition; offset(r+1) = offset(r) - mean step(r->r+1)
    double acc = 0.0;
    for (int r = 1; r < kRanges; ++r) {
        if (m_stepN[r] > 0) acc -= m_stepSum[r] / m_stepN[r];
        m_offset[r] = acc;          // m_offset[r] holds the offset for range r+1
    }
    m_dirty = false;
}

double RangeCalibrator::offsetDeg(int range) const {
    if (range <= 1 || range > kRanges) return 0.0;
    if (m_dirty) rebuild();
    return m_offset[range - 1];
}

// A range is calibrated once every boundary between it and the lowest range
// this instrument has ever used carries enough samples. Anchoring at the
// lowest range SEEN rather than always at range 1 is not a shortcut: the
// feature being protected is a slope, and a constant phase offset applied to
// every point of a sweep shifts the whole line without changing its gradient.
// Only the differences between ranges matter, so where the chain is pinned is
// arbitrary — and an instrument that never reaches range 1 would otherwise
// never become calibrated at all.
bool RangeCalibrator::calibrated(int range) const {
    if (range < 1 || range > kRanges) return false;
    int anchor = 1;
    while (anchor < range && !m_seen[anchor]) ++anchor;
    if (anchor >= range) return true;                 // it is the anchor itself
    for (int r = anchor; r < range; ++r)
        if (m_stepN[r] < kMinSamples) return false;
    return true;
}

int RangeCalibrator::samples(int range) const {
    if (range <= 1 || range > kRanges) return 0;
    return m_stepN[range - 1];
}

Spectrum RangeCalibrator::correct(const Spectrum& s) const {
    Spectrum out = s;
    for (Point& p : out.points)
        if (p.range >= 1 && p.range <= kRanges) p.phaseDeg += offsetDeg(p.range);
    return out;
}

bool RangeCalibrator::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# birx range calibration v1\n";
    f << "# step <lowRange> <highRange> <sumDeg> <n>\n";
    f << "# seen <range>\n";
    for (int r = 1; r < kRanges; ++r)
        f << "step " << r << " " << (r+1) << " " << m_stepSum[r] << " " << m_stepN[r] << "\n";
    for (int r = 1; r <= kRanges; ++r)
        if (m_seen[r]) f << "seen " << r << "\n";
    return f.good();
}

bool RangeCalibrator::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    m_stepSum.fill(0.0); m_stepN.fill(0); m_seen.fill(false); m_dirty = true;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string key; is >> key;
        if (key == "seen") { int r = 0; is >> r; if (r >= 1 && r <= kRanges) m_seen[r] = true; continue; }
        if (key != "step") continue;
        int lo = 0, hi = 0, n = 0; double sum = 0;
        is >> lo >> hi >> sum >> n;
        if (lo >= 1 && lo < kRanges && hi == lo + 1 && n >= 0) {
            m_stepSum[lo] = sum; m_stepN[lo] = n;
        }
    }
    return true;
}

} // namespace birx
