// birx/quality.h — the gate that decides whether a verdict may be given.
//
// This is the single most consequential module in the rewrite, and it exists
// because of one line in the shipped Classifier::classify: a point that fails
// the impedance bound simply does not match the High-Risk row, falls through
// to the default, and is reported as Low Risk. There is no path in the shipped
// code that distinguishes "measured the lesion and found nothing" from "never
// measured the lesion".
//
// In the India cohort 52% of readings were above the adipose threshold, 15 of
// 88 cases had every reading in fat, and 13 of the 17 false negatives were
// those cases. Reporting them as Invalid instead, and changing nothing else,
// moves sensitivity on the remaining cases from 0.78 to 0.94.
//
// So: Invalid is a first-class outcome here, not an error path. A gate failure
// must never be able to become a reassuring answer.

#ifndef BIRX_QUALITY_H
#define BIRX_QUALITY_H

#include "types.h"
#include "featureset.h"
#include "calib.h"

namespace birx {

struct QualityConfig {
    // Placement. Above this |Z| at 1 kHz the tip is in adipose tissue and the
    // reading is not of the lesion. The shipped software used 1900 as an upper
    // bound of a High-Risk box; the firmware used 1500. Neither treated it as
    // a gate. Set it from the needle's own characterisation.
    double fatThresholdOhm = 1900.0;

    // A lesion reading below this is a short or a broken tip, not tissue.
    double minZ1kOhm = 20.0;

    // Passive tissue cannot produce a phase outside this. A reading that does
    // is an instrument fault, not a finding.
    double phaseAbsMaxDeg = 95.0;

    // How rough the phase-vs-log(f) line may be before the slope is meaningless.
    // In degrees; 2.5 is roughly twice the repeatability seen on clean sweeps.
    double maxFitRmsDeg = 2.5;

    int minFitPoints       = 5;
    int preferredFitPoints = 8;

    // Require the gain ranges used to have a learned phase offset before the
    // reading is accepted. Off by default so a fresh install still works; turn
    // on once RangeCalibrator has enough samples in the field.
    bool requireCalibratedRanges = false;
};

class QualityGate {
public:
    explicit QualityGate(QualityConfig cfg = {}) : m_cfg(cfg) {}

    Quality evaluate(const Spectrum& s, const Features& f,
                     const RangeCalibrator* calib = nullptr) const;

    const QualityConfig& config() const { return m_cfg; }
    void setConfig(const QualityConfig& c) { m_cfg = c; }

private:
    QualityConfig m_cfg;
};

} // namespace birx
#endif
