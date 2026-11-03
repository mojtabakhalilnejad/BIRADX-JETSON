// birx/calib.h — instrument constants, and self-calibration of the gain ranges.
//
// TWO separate phase corrections exist in this instrument. Keeping them apart
// is the whole point of this file.
//
// 1. Applied inside getImpedance() and therefore PRESENT in the phase on the
//    wire, but only on ranges 1 and 2 and only at 500 kHz:
//
//        if (|Z| < 200)  phase -= (-8e-5*|Z|^2 + 4e-3*|Z| + 3.4)
//        else            phase -= 0.9
//
//    Because it stops at range 2, the phase at 500 kHz steps by roughly 0.9
//    to 3.4 degrees when the gain crosses from range 2 to range 3 — about 1.3
//    to 4.9 in IPS units. That step is real, it is on the wire, and this
//    software has to deal with it.
//
// 2. Applied in SweepFreq() to the variable ph500, which only reportIPS()
//    reads. It never reaches reportImp(), so it is NOT on the wire:
//
//        range 1: -1.00   range 2: -1.50   range 3: 0   range 4: 0
//        range 5: +0.70   range 6: +15.00        (already in IPS units)
//
//    The +15 on range 6 is wider than the entire High-Risk band. Recomputing
//    IPS from the transmitted phases avoids it completely, which is the main
//    reason the decision moved into this software.
//
// RangeCalibrator handles what remains of (1), and does it from the data
// rather than from a constant. Within one sweep the gain only ever ratchets
// up as frequency falls, so every sweep contains a handful of adjacent pairs
// of points that straddle a range boundary. Phase against log10(f) is smooth
// in tissue, so the jump seen at such a boundary estimates the phase offset
// between those two ranges. Averaged over many sweeps the estimate converges,
// and it needs no phantom and no service visit.

#ifndef BIRX_CALIB_H
#define BIRX_CALIB_H

#include "types.h"
#include <array>
#include <string>

namespace birx {

struct InstrumentConstants {
    // The 15 frequencies SweepFreq() is called with, highest first.
    static const std::vector<double>& sweepFrequencies();

    // Autorange thresholds on |Z| at the frequency being measured.
    static const std::array<double, 5>& rangeThresholds();   // 500, 905, 1800, 2500, 4700

    // The firmware's ph500 offsets, in IPS units. Documented here so the
    // legacy verdict can be reproduced exactly; never applied to wire phase.
    static double firmwarePh500OffsetIps(int range);

    // The magnitude-dependent 500 kHz correction the firmware already applied.
    // Returns the number of degrees that were SUBTRACTED from the reported
    // phase, or 0 when the firmware did not touch it.
    static double appliedPhaseCorrection(double freqHz, double magOhm, int range);

    static constexpr double kWireMagCeilingOhm = 999999.0;  // sprintf %08d on mag*10
    static constexpr double kIpsPerDecade      = 1.4306765580733933; // 1/log10(5)
};

// ---------------------------------------------------------------------------

class RangeCalibrator {
public:
    static constexpr int kRanges = 6;

    RangeCalibrator();

    // Learn from one sweep. Safe to call on every measurement, including bad
    // ones: pairs that fail the smoothness precondition are ignored.
    void observe(const Spectrum& s);

    // Phase offset in degrees to ADD to a point measured on `range`, bringing
    // it onto the common scale of range 1. Zero for an uncalibrated range.
    double offsetDeg(int range) const;
    bool   calibrated(int range) const;
    int    samples(int range) const;

    // Apply the learned offsets to a copy of the sweep.
    Spectrum correct(const Spectrum& s) const;

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Merge a step estimate directly; used by the loader and by tests.
    void addStep(int lowRange, int highRange, double stepDeg);

private:
    static constexpr int kMinSamples = 8;

    // Pairwise accumulation: mean step from range r to range r+1.
    std::array<double, kRanges> m_stepSum{};
    std::array<int,    kRanges> m_stepN{};
    std::array<bool,   kRanges + 1> m_seen{};   // ranges actually used, 1-based
    mutable std::array<double, kRanges> m_offset{};
    mutable bool m_dirty = true;
    void rebuild() const;
};

} // namespace birx
#endif
