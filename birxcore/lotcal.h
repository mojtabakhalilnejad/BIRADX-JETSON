// birx/lotcal.h — the factory's lot calibration, and when a lot needs its own.
//
// Two different calibrations exist and they are easy to confuse:
//
//   PER NEEDLE, on the device, six seconds in air. Removes the stray
//   capacitance of THIS needle, THIS cable and THIS connector, right now.
//   Already implemented in opencal.h.
//
//   PER LOT, in the factory, on a sample of needles from a batch, against
//   physical standards. Establishes what the batch does that the air sweep
//   cannot see: the gain-range phase offsets, the residual series impedance,
//   and whether the batch behaves like the batches already in the field.
//
// The air sweep on the device measures an open circuit. An open circuit tells
// you about what is in parallel with the sample — stray capacitance — and
// nothing about what is in series with it, and nothing about a phase offset
// that is the same on open and on load. Those need a known load, and a known
// load means a resistor on a bench, not a needle in a patient. Hence a factory
// step, and hence a file that has to reach every device.
//
// WHEN DOES A LOT NEED ITS OWN ENTRY
//
// Not every batch does, and adding one costs a file update on every device in
// the field. The threshold is set by what actually moves a decision. Using the
// instrument's own parasitic model at the median lesion impedance of 718 ohm:
//
//     1 pF of stray capacitance  ->  0.148 IPS units
//     3 pF                       ->  0.44  IPS units
//
// and one IPS unit is 0.70 degrees of phase across the fitted band. So:
//
//   * stray capacitance within 3 pF of an existing lot, AND
//   * every gain-range phase offset within 0.15 degrees of it
//     (0.15 degrees is the repeatability floor of the instrument — below it
//      there is nothing to distinguish)
//
// means the new lot is indistinguishable from one already in the file and
// reuses its entry. Outside either bound, the lot gets its own entry and every
// device must be updated, because a device running the old file would apply
// the wrong correction to the new needles and the error goes straight into
// IPS, which is the whole diagnostic signal.
//
// The file is the unit of distribution: it carries EVERY lot, not a delta, so
// a device that missed an update is fixed by loading the latest file rather
// than by replaying a history.

#ifndef BIRX_LOTCAL_H
#define BIRX_LOTCAL_H

#include "types.h"
#include <string>
#include <vector>

namespace birx {

// ---------------------------------------------------------------------------
// What has to be done to a needle on the bench before its lot is calibrated.
// Held as data so the factory screen renders it and the acceptance limits are
// in one place that can be reviewed.
struct FactoryTest {
    std::string id;
    std::string title;
    std::string fixture;     // what to connect
    std::string measures;    // what it establishes
    std::string accept;      // the pass criterion, in words
    bool        perNeedle = true;   // false = once per lot
};

const std::vector<FactoryTest>& factoryTestPlan();

// ---------------------------------------------------------------------------
struct LotCalibration {
    std::string lotId;
    std::string needleRev;
    std::string takenAt;       // ISO date
    std::string operatorId;
    int         nSampled  = 0; // needles measured from the batch

    double cpMeanFarad = kNaN; // stray capacitance, mean over the sample
    double cpSdFarad   = kNaN; // and its spread — the batch's consistency
    double seriesROhm  = kNaN; // residual series resistance from the short
    double phaseRmsDeg = kNaN; // residual phase error against the resistor set
    double repeatDeg   = kNaN; // repeat-to-repeat phase SD on one needle

    // Phase offset per gain range, in degrees, index 1..5 (0 unused).
    double rangeOffsetDeg[6] = { kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };

    bool passed = false;
    std::string note;

    bool complete() const;
};

// How far apart two lots are, expressed in the unit that matters.
double lotIpsShift(const LotCalibration& a, const LotCalibration& b,
                   double sampleOhm = 718.0);
double lotMaxRangeOffsetDiffDeg(const LotCalibration& a, const LotCalibration& b);

// ---------------------------------------------------------------------------
enum class LotVerdict {
    Rejected,        // the batch failed its own bench limits
    MatchesExisting, // indistinguishable from a lot already in the file
    NeedsNewEntry    // different enough to matter — every device must update
};

struct LotDecision {
    LotVerdict  verdict = LotVerdict::Rejected;
    std::string nearestLot;
    double      ipsShift        = kNaN;
    double      rangeOffsetDiff = kNaN;
    std::string message;          // shown verbatim in the factory screen
    bool fleetUpdateRequired() const { return verdict == LotVerdict::NeedsNewEntry; }
};

struct LotAcceptance {
    // Bench limits for a batch to be usable at all.
    double maxPhaseRmsDeg = 0.30;   // twice the repeatability floor
    double maxRepeatDeg   = 0.15;   // the floor itself
    double maxCpSdFarad   = 5e-12;  // spread within the batch: 0.74 IPS units
    int    minSampled     = 5;

    // Limits for a batch to reuse an existing entry.
    double sameCpToleranceFarad = 3e-12;   // 0.44 IPS units at 718 ohm
    double sameOffsetToleranceDeg = 0.15;  // the repeatability floor
};

// ---------------------------------------------------------------------------
class LotCalibrationFile {
public:
    std::string issuedAt;         // ISO date the file was produced
    int         revision = 0;     // bumped every time a lot is added
    std::vector<LotCalibration> lots;

    bool load(const std::string& path, std::string* error = nullptr);
    bool save(const std::string& path) const;

    const LotCalibration* find(const std::string& lotId) const;
    const LotCalibration* nearest(const LotCalibration& c, double* ipsShift = nullptr) const;

    // Decide what a freshly measured batch means for the fleet.
    LotDecision classify(const LotCalibration& c,
                         const LotAcceptance& acc = LotAcceptance()) const;

    // Adds the lot and bumps the revision. Refuses a batch that failed.
    bool add(const LotCalibration& c, const LotAcceptance& acc = LotAcceptance());

    std::string summary() const;
};

} // namespace birx
#endif
