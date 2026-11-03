#include "quality.h"
#include <cmath>

namespace birx {

Quality QualityGate::evaluate(const Spectrum& s, const Features& f,
                              const RangeCalibrator* calib) const {
    Quality q;
    q.usable = true;

    if (s.empty()) { q.add(Flag::NoSweep); return q; }

    // --- placement ---------------------------------------------------------
    if (!std::isfinite(f.z1k)) {
        q.add(Flag::NoZ1k);
    } else if (f.z1k > m_cfg.fatThresholdOhm) {
        q.add(Flag::TipInFat);
    } else if (f.z1k < m_cfg.minZ1kOhm) {
        q.add(Flag::PhaseImplausible);   // a short reads as tissue otherwise
    }

    // --- front-end sanity --------------------------------------------------
    for (const Point& p : s.points) {
        if (!p.valid()) continue;
        if (p.magOhm >= InstrumentConstants::kWireMagCeilingOhm) { q.add(Flag::Saturated); break; }
    }
    for (const Point& p : s.band(m_cfg.fatThresholdOhm > 0 ? 0.0 : 0.0, 1e9)) {
        if (std::fabs(p.phaseDeg) > m_cfg.phaseAbsMaxDeg) { q.add(Flag::PhaseImplausible); break; }
    }

    // --- enough of a sweep to fit a slope ----------------------------------
    if (f.nFit < m_cfg.minFitPoints)              q.add(Flag::Incomplete);
    else if (f.nFit < m_cfg.preferredFitPoints)   q.add(Flag::FewPoints);

    // --- the slope has to describe the data --------------------------------
    if (std::isfinite(f.fitRmsDeg) && f.fitRmsDeg > m_cfg.maxFitRmsDeg)
        q.add(Flag::UnstableSweep);
    if (!std::isfinite(f.ipsFit) && f.nFit >= m_cfg.minFitPoints)
        q.add(Flag::UnstableSweep);

    // --- gain ranges -------------------------------------------------------
    if (f.rangeSwitches > 0) q.add(Flag::RangeSwitchInBand);
    if (calib) {
        bool uncal = false;
        for (const Point& p : s.band(0.0, 1e9))
            if (p.range >= 1 && !calib->calibrated(p.range)) { uncal = true; break; }
        if (uncal) {
            q.add(Flag::UncalibratedRange);
            if (m_cfg.requireCalibratedRanges) q.add(Flag::Incomplete);
        }
    }

    q.usable = !q.anyFatal();
    return q;
}

} // namespace birx
