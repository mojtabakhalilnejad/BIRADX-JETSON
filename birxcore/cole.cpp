#include "cole.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace birx {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Meas {
    double w;        // angular frequency
    double re, im;   // measured impedance
    double mag;      // |Z|, used to make the residual relative
};

// Parameter vector, in the space the optimiser works in:
//   p[0] = Rinf          (ohm, > 0)
//   p[1] = dR = R0-Rinf  (ohm, > 0)
//   p[2] = log10(fc)
//   p[3] = alpha         in (0,1]
struct P { double v[4]; };

void clampParams(P& p, double logFcLo, double logFcHi) {
    p.v[0] = std::max(1.0,   p.v[0]);
    p.v[1] = std::max(1.0,   p.v[1]);
    p.v[2] = std::min(logFcHi, std::max(logFcLo, p.v[2]));
    p.v[3] = std::min(1.0,   std::max(0.25, p.v[3]));
}

void model(const P& p, double w, double* re, double* im) {
    const double rInf  = p.v[0];
    const double dR    = p.v[1];
    const double tau   = 1.0 / (2.0 * kPi * std::pow(10.0, p.v[2]));
    const double alpha = p.v[3];

    const double wt = w * tau;
    if (wt <= 0) { *re = rInf + dR; *im = 0.0; return; }

    const double u = std::pow(wt, alpha);
    const double c = std::cos(alpha * kPi / 2.0);
    const double s = std::sin(alpha * kPi / 2.0);

    const double a   = 1.0 + u * c;
    const double b   = u * s;
    const double den = a * a + b * b;
    if (den < 1e-300) { *re = rInf; *im = 0.0; return; }

    *re = rInf + dR * a / den;
    *im = -dR * b / den;          // negative: tissue is capacitive
}

// Relative residuals, real and imaginary interleaved.
void residuals(const P& p, const std::vector<Meas>& m, std::vector<double>& r) {
    r.resize(m.size() * 2);
    for (size_t i = 0; i < m.size(); ++i) {
        double re = 0, im = 0;
        model(p, m[i].w, &re, &im);
        const double inv = 1.0 / std::max(1e-9, m[i].mag);
        r[2*i]     = (re - m[i].re) * inv;
        r[2*i + 1] = (im - m[i].im) * inv;
    }
}

double sumSq(const std::vector<double>& r) {
    double s = 0;
    for (double v : r) s += v * v;
    return s;
}

bool solve4(double A[4][5], double out[4]) {
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(A[row][col]) > std::fabs(A[piv][col])) piv = row;
        if (std::fabs(A[piv][col]) < 1e-18) return false;
        if (piv != col) for (int j = 0; j <= 4; ++j) std::swap(A[col][j], A[piv][j]);
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const double k = A[row][col] / A[col][col];
            for (int j = col; j <= 4; ++j) A[row][j] -= k * A[col][j];
        }
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = A[i][4] / A[i][i];
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

} // namespace

void coleImpedance(double freqHz, double r0, double rInf, double fcHz, double alpha,
                   double* re, double* im) {
    P p;
    p.v[0] = rInf;
    p.v[1] = r0 - rInf;
    p.v[2] = std::log10(std::max(1e-9, fcHz));
    p.v[3] = alpha;
    model(p, 2.0 * kPi * freqHz, re, im);
}

ColeFit fitCole(const Spectrum& s, const ColeConfig& cfg) {
    ColeFit out;

    // --- gather the band ----------------------------------------------------
    std::vector<Meas> m;
    for (const Point& pt : s.band(cfg.loHz, cfg.hiHz)) {
        const double th = pt.phaseDeg * kPi / 180.0;
        Meas z;
        z.w   = 2.0 * kPi * pt.freqHz;
        z.re  = pt.magOhm * std::cos(th);
        z.im  = pt.magOhm * std::sin(th);
        z.mag = pt.magOhm;
        if (z.mag <= 0 || !std::isfinite(z.re) || !std::isfinite(z.im)) continue;
        m.push_back(z);
    }
    out.nPoints = static_cast<int>(m.size());
    if (out.nPoints < cfg.minPoints) return out;

    // --- starting point -----------------------------------------------------
    // Ordered by ascending frequency, so the ends bracket the dispersion.
    const Meas& lo = m.front();
    const Meas& hi = m.back();

    P p;
    p.v[0] = std::max(1.0, hi.re);                       // Rinf
    p.v[1] = std::max(1.0, lo.re - hi.re);               // dR
    // fc where -Im is largest: the apex of the arc.
    {
        double bestIm = -1e300; double bestW = m[m.size()/2].w;
        for (const Meas& z : m) if (-z.im > bestIm) { bestIm = -z.im; bestW = z.w; }
        p.v[2] = std::log10(bestW / (2.0 * kPi));
    }
    p.v[3] = 0.75;

    const double logFcLo = std::log10(lo.w / (2.0 * kPi)) - 1.0;
    const double logFcHi = std::log10(hi.w / (2.0 * kPi)) + 1.0;
    clampParams(p, logFcLo, logFcHi);

    // --- Levenberg-Marquardt, numerical Jacobian ----------------------------
    std::vector<double> r, rTry;
    residuals(p, m, r);
    double cost = sumSq(r);
    double lambda = 1e-3;

    const double step[4] = { 1e-4, 1e-4, 1e-5, 1e-5 };  // relative/absolute mix
    int it = 0;
    for (; it < cfg.maxIterations; ++it) {
        // J is (2n x 4); build J^T J and J^T r directly.
        std::vector<std::vector<double>> J(r.size(), std::vector<double>(4, 0.0));
        for (int k = 0; k < 4; ++k) {
            const double h = (k < 2) ? std::max(1e-6, std::fabs(p.v[k]) * step[k])
                                     : step[k] * 10.0;
            P a = p, b = p;
            a.v[k] += h; b.v[k] -= h;
            clampParams(a, logFcLo, logFcHi);
            clampParams(b, logFcLo, logFcHi);
            std::vector<double> ra, rb;
            residuals(a, m, ra);
            residuals(b, m, rb);
            const double denom = (a.v[k] - b.v[k]);
            if (std::fabs(denom) < 1e-12) continue;
            for (size_t i = 0; i < r.size(); ++i) J[i][k] = (ra[i] - rb[i]) / denom;
        }

        double JtJ[4][4] = {{0}}, Jtr[4] = {0};
        for (size_t i = 0; i < r.size(); ++i)
            for (int a = 0; a < 4; ++a) {
                Jtr[a] += J[i][a] * r[i];
                for (int b = 0; b < 4; ++b) JtJ[a][b] += J[i][a] * J[i][b];
            }

        bool improved = false;
        for (int attempt = 0; attempt < 12 && !improved; ++attempt) {
            double A[4][5];
            for (int a = 0; a < 4; ++a) {
                for (int b = 0; b < 4; ++b) A[a][b] = JtJ[a][b];
                A[a][a] += lambda * (JtJ[a][a] > 0 ? JtJ[a][a] : 1.0);
                A[a][4]  = -Jtr[a];
            }
            double d[4];
            if (!solve4(A, d)) { lambda *= 10.0; continue; }

            P q = p;
            for (int a = 0; a < 4; ++a) q.v[a] += d[a];
            clampParams(q, logFcLo, logFcHi);

            residuals(q, m, rTry);
            const double c2 = sumSq(rTry);
            if (c2 < cost) {
                const double rel = (cost - c2) / std::max(1e-300, cost);
                p = q; r = rTry; cost = c2;
                lambda = std::max(1e-9, lambda * 0.4);
                improved = true;
                if (rel < 1e-10) { it++; goto done; }
            } else {
                lambda *= 10.0;
            }
        }
        if (!improved) break;
    }
done:
    out.iterations = it;
    out.rmsRel = std::sqrt(cost / static_cast<double>(r.size()));
    out.rInf   = p.v[0];
    out.r0     = p.v[0] + p.v[1];
    out.fcHz   = std::pow(10.0, p.v[2]);
    out.alpha  = p.v[3];

    // A fit that landed on a boundary is not a fit, it is the optimiser giving
    // up against a constraint; report nothing rather than a confident number.
    const bool onBoundary = (out.alpha <= 0.2501) ||
                            (p.v[2] <= logFcLo + 1e-6) || (p.v[2] >= logFcHi - 1e-6);
    out.ok = std::isfinite(out.rmsRel) && out.rmsRel <= cfg.maxRmsRel &&
             out.r0 > out.rInf && out.rInf > 0 && !onBoundary;

    if (!out.ok) {
        // Keep rmsRel: a failed fit is itself informative and is recorded.
        out.r0 = out.rInf = out.fcHz = out.alpha = kNaN;
    }
    return out;
}

} // namespace birx
