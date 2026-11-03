// birx/featureset.h — from one sweep to the numbers a model can read.
//
// The central change from the shipped device is how the phase slope is
// estimated. The firmware uses
//
//     IPS = 1.43 * (phase(500 kHz) - phase(100 kHz))
//
// which is a two-point secant: two of the fifteen frequencies it measures,
// each already averaged over 14 repetitions, and the other thirteen thrown
// away. For a slope fitted to points x_i = log10(f_i) with per-reading phase
// noise sigma, the variance is sigma^2 / sum((x_i - xbar)^2), so the two-point
// secant is the worst available estimator:
//
//     2 points, 100-500 kHz   4.09 sigma^2      (as shipped)
//     8 points, 100-500 kHz   2.99 sigma^2      -14% noise
//    10 points,  50-500 kHz   1.24 sigma^2      -45% noise
//    11 points,  25-500 kHz   0.71 sigma^2      -58% noise
//
// The catch is that phase against log10(f) is only straight over part of the
// range, so widening the band trades variance for bias. The default band of
// 50-500 kHz is the compromise; FeatureConfig::bandLoHz exists so the band can
// be settled from recorded sweeps rather than from an opinion.
//
// Both estimators are computed on every sweep. ipsLegacy reproduces the
// firmware's number exactly, so an installed threshold keeps its meaning and
// any disagreement between old and new is visible in the log from day one.

#ifndef BIRX_FEATURES_H
#define BIRX_FEATURES_H

#include "types.h"
#include "calib.h"
#include "cole.h"

namespace birx {

struct FeatureConfig {
    double bandLoHz     = 50000.0;    // fitting band, inclusive
    double bandHiHz     = 500000.0;
    double refFreqHz    = 100000.0;   // where phaseAtRefDeg is evaluated. 100 kHz is
                                      // the anchor of the firmware's own IPS, so the
                                      // level and the slope are reported about the
                                      // same point and stay interpretable together.
    int    minFitPoints = 5;          // below this the slope is not trustworthy
    int    preferredFitPoints = 8;    // below this raise FewPoints
    bool       fitColeModel = true;   // off only to save cycles on very slow hardware
    ColeConfig cole;                  // its own band: the Cole arc needs the low
                                      // frequencies the phase-slope band excludes
    bool   undoAppliedCorrection = false;
        // The firmware's magnitude-dependent 500 kHz correction is present in
        // the wire phase on ranges 1-2 only. Setting this removes it, giving a
        // phase that is consistent across ranges but no longer carries the
        // front-end calibration. Leave false and let RangeCalibrator handle the
        // step from data; turn on only to measure how large that effect is.
};

class FeatureExtractor {
public:
    explicit FeatureExtractor(FeatureConfig cfg = {}) : m_cfg(cfg) {}

    Features extract(const Spectrum& s) const;

    const FeatureConfig& config() const { return m_cfg; }
    void setConfig(const FeatureConfig& c) { m_cfg = c; }

    // Weighted least squares of y on x. Returns false when x has no spread.
    // rms receives the root-mean-square residual.
    static bool leastSquares(const std::vector<double>& x,
                             const std::vector<double>& y,
                             double* slope, double* intercept, double* rms);

    // Median of all pairwise slopes. Robust to a single bad reading, which is
    // exactly the failure mode a two-point secant cannot survive.
    static double theilSen(const std::vector<double>& x,
                           const std::vector<double>& y);

    // Quadratic coefficient of y = a + b*x + c*x^2, as a straightness check.
    static bool quadratic(const std::vector<double>& x,
                          const std::vector<double>& y, double* c2);

private:
    FeatureConfig m_cfg;
};

} // namespace birx
#endif
