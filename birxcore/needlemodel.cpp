#include "needlemodel.h"
#include "calib.h"
#include "lotfit.h"

#include <cmath>

namespace birx {

namespace {
constexpr double kPi = 3.14159265358979323846;

// The stray Cp || Gp taken off (remove) or put back on every point.
void stray(std::vector<Point>& pts, double cpF, double gpS, bool remove)
{
    const double g = std::isfinite(gpS) ? gpS : 0.0;
    for (Point& p : pts) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        const std::complex<double> z = std::polar(p.magOhm, p.phaseDeg * kPi / 180.0);
        const std::complex<double> y(g, 2.0 * kPi * p.freqHz * cpF);
        const std::complex<double> d = remove ? 1.0 - z * y : 1.0 + z * y;
        if (std::abs(d) < 1e-9) continue;
        const std::complex<double> r = z / d;
        if (!std::isfinite(r.real()) || !std::isfinite(r.imag()) || !(std::abs(r) > 0)) continue;
        p.magOhm = std::abs(r);
        p.phaseDeg = std::arg(r) * 180.0 / kPi;
    }
}
} // namespace

std::complex<double> ElectrodeModel::zi(double freqHz) const
{
    if (!ok || !(freqHz > 0)) return 0.0;
    return std::polar(a * std::pow(2.0 * kPi * freqHz, -alpha), -alpha * kPi / 2.0);
}

double ElectrodeModel::ze1k() const
{
    if (!ok) return kNaN;
    return std::abs(std::polar(a * std::pow(2.0 * kPi * 1000.0, -alpha), -alpha * kPi / 2.0));
}

void instrumentToTrue(Spectrum* s, const RangeModel* rm)
{
    if (!s) return;
    for (Point& p : s->points) {
        if (!(p.freqHz > 0) || !std::isfinite(p.phaseDeg) || p.range <= 0) continue;
        const double c = InstrumentConstants::appliedPhaseCorrection(p.freqHz, p.magOhm, p.range);
        p.phaseDeg += c;
        if (rm && p.range < 7) p.phaseDeg += rm->offsetDeg[p.range];
    }
}

void trueToInstrument(Spectrum* s, const RangeModel* rm)
{
    if (!s) return;
    for (Point& p : s->points) {
        if (!(p.freqHz > 0) || !std::isfinite(p.phaseDeg) || p.range <= 0) continue;
        if (p.range < 7 && rm) p.phaseDeg -= rm->offsetDeg[p.range];
        p.phaseDeg -= InstrumentConstants::appliedPhaseCorrection(p.freqHz, p.magOhm, p.range);
    }
}

void deembedAll(Spectrum* s, double cpF, double gpS)
{
    if (s && std::isfinite(cpF)) stray(s->points, cpF, gpS, true);
}

void needleTransfer(Spectrum* s, const NeedleTransfer& t)
{
    if (!s) return;
    const bool model = t.eNeedle.ok && t.eRef.ok;
    instrumentToTrue(s, t.ranges);
    if (std::isfinite(t.cpNeedleF)) stray(s->points, t.cpNeedleF, t.gpNeedleS, true);
    for (Point& p : s->points) {
        if (!(p.freqHz > 0) || !(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        std::complex<double> z = std::polar(p.magOhm, p.phaseDeg * kPi / 180.0);
        const bool k = std::isfinite(t.kZ) && t.kZ > 0;
        if (model) {
            z -= t.eNeedle.zi(p.freqHz);
            if (k) z *= t.kZ;
            z += t.eRef.zi(p.freqHz);
        } else if (k) {
            z *= t.kZ;
        }
        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || !(std::abs(z) > 0)) continue;
        p.magOhm = std::abs(z);
        p.phaseDeg = std::arg(z) * 180.0 / kPi;
    }
    if (std::isfinite(t.cpRefF)) stray(s->points, t.cpRefF, t.gpRefS, false);
    // the gain range the instrument would have chosen for the new |Z|
    for (Point& p : s->points) {
        if (p.range <= 0) continue;
        int rg = 1;
        for (double th : InstrumentConstants::rangeThresholds()) if (p.magOhm > th) ++rg;
        p.range = rg;
    }
    trueToInstrument(s, t.ranges);
    lotPlain(s, t.cpRefF, t.gpRefS, t.trialCalibrated);
}

ElectrodeModel fitElectrode(const Spectrum& s, double fLoHz, double fHiHz)
{
    ElectrodeModel m;
    std::vector<std::complex<double>> z;
    std::vector<double> f;
    for (const Point& p : s.points) {
        if (!(p.freqHz >= fLoHz) || !(p.freqHz <= fHiHz)) continue;
        if (!(p.magOhm > 0) || !std::isfinite(p.phaseDeg)) continue;
        z.push_back(std::polar(p.magOhm, p.phaseDeg * kPi / 180.0));
        f.push_back(p.freqHz);
    }
    const size_t n = z.size();
    if (n < 5) return m;
    double best = 1e300;
    double alpha = 0.30;
    for (int i = 0; i <= 140; ++i, alpha += 0.005) {
        std::vector<std::complex<double>> b(n);
        double sw = 0, sb = 0, sbb = 0, sz = 0, szb = 0;
        for (size_t k = 0; k < n; ++k) {
            const double w = 1.0 / std::norm(z[k]);
            b[k] = std::polar(std::pow(2.0 * kPi * f[k], -alpha), -alpha * kPi / 2.0);
            sw += w;
            sb += w * b[k].real();
            sbb += w * std::norm(b[k]);
            sz += w * z[k].real();
            szb += w * (z[k].real() * b[k].real() + z[k].imag() * b[k].imag());
        }
        const double det = sw * sbb - sb * sb;
        if (std::fabs(det) < 1e-300) continue;
        const double r = (sbb * sz - sb * szb) / det;
        if (!(r > 0)) continue;
        const double a = (sw * szb - sb * sz) / det;
        if (!(a > 0)) continue;
        double res = 0;
        for (size_t k = 0; k < n; ++k) res += std::norm(z[k] - r - a * b[k]) / std::norm(z[k]);
        if (res < best) { best = res; m.rOhm = r; m.a = a; m.alpha = alpha; }
    }
    if (best < 1e299) {
        m.rms = std::sqrt(best / double(n));
        m.n = int(n);
        m.ok = std::isfinite(m.rOhm) && m.rms < 0.2;
    }
    return m;
}

} // namespace birx
