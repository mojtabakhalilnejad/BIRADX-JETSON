// birx/lotfit.h — a needle lot measured against the REFERENCE needle the trial
// cut-offs were found with, and the correction that makes the lot read as the
// reference would have (H7-2.0.0).
//
// WHAT IS MEASURED, AND WHY THESE THREE THINGS
//
//   AIR (open). The needle, its cable and connector, tip in air. Gives the
//   stray capacitance Cp and leakage Gp, which sit in parallel with the
//   tissue and bend the phase at the top of the band — that is IPS. Already
//   removed per needle by the six-second calibration; measured here so that a
//   lot's spread and its mean are known (the mean stands in for a needle that
//   was not calibrated).
//
//   SALINE PHANTOMS. Saline is a pure resistance from 1 kHz to 500 kHz, so
//   whatever a needle shows in it beyond that is the needle: the LEVEL of Z is
//   the cell constant (the geometry of the tip: exposed length, diameter, gap),
//   which the air sweep cannot see; a residual phase slope is what the air
//   correction left behind. Three strengths are used because tissue is ten to
//   fifty times less conductive than 0.9 % saline: 0.9 %, 0.225 % (1 part to 3
//   parts water) and 0.09 % (1 to 9), which bracket the impedances of breast
//   tissue. Comparing the lot with the reference AT EACH STRENGTH gives the Z
//   ratio and the IPS difference as functions of Z, and shows whether one
//   correction holds across the range (it must, or a calibration cannot fix
//   the lot and the rule ranges themselves have to change).
//
//   TEMPERATURE / CONDUCTIVITY. Saline conducts about 2 % more per degree, so
//   every phantom reading is brought to 25 °C — from its temperature, or, when
//   a conductivity meter reading is entered, from that (the better of the two).
//
// The comparison is always lot against reference in the SAME phantoms, after
// each needle's own air correction. What is handed to every device is, per
// phantom strength, the lot's Z, the factor that brings it to the reference's
// Z, and the IPS difference — interpolated between strengths in log Z and held
// at the end values outside them (never extrapolated).

#ifndef BIRX_LOTFIT_H
#define BIRX_LOTFIT_H

#include "types.h"
#include "needlemodel.h"
#include <cmath>
#include <string>
#include <vector>

namespace birx {

// ---------------------------------------------------------------------------
// The phantoms, in the order they are measured. Step 0 is air.
struct LotPhantom {
    const char* name;          // as shown and stored
    const char* prepare;       // how to make it
    double      nominalMsCm;   // conductivity at 25 °C, for a meter reading
};
const std::vector<LotPhantom>& lotPhantoms();
const char* lotStepName(int step);        // 0 = "Air", 1.. = the phantom's name
std::string lotStepHowTo(int step);       // what to do before pressing Start

// ---------------------------------------------------------------------------
struct LotPlan {
    int needles  = 5;      // needles sampled from the lot (reference: as many as exist, 1 at least)
    int repeats  = 3;      // sweeps per needle per step
    int phantoms = 3;      // saline strengths used, 1..3
};

// One test sweep.
struct LotTest {
    int    needle = 1;     // 1..plan.needles
    int    step   = 0;     // 0 air, 1..3 phantom
    int    repeat = 1;
    bool   ok     = false;
    std::string why;       // refused: the reason
    std::string when;      // ISO time
    double cpF    = kNaN;  // air
    double gpS    = kNaN;
    double z1k    = kNaN;  // phantom, after the needle's own air correction
    double ips    = kNaN;  // firmware IPS definition (what the rules read)
    double ipsFit = kNaN;
    double tempC  = kNaN;  // phantom conditions as entered
    double condMsCm = kNaN;
    double zNorm  = kNaN;  // z1k brought to 25 °C
    std::vector<Point> raw;   // the sweep as it arrived (phantoms), for the transfer
    // H7-2.5.0: a sweep taken out of the run (kept in the file, never counted)
    bool   removed = false;
    bool   removedOk = false;     // it had been accepted (taken back by Restore)
    int    soakS  = -1;    // seconds the tip was in the solution before the sweep (-1: not recorded)
};

double lotNormaliseZ(double z1k, int step, double tempC, double condMsCm);

struct LotRun {
    std::string lotId;
    bool        reference = false;
    bool        control = false;     // H7-2.4.0: the reference needle measured again, in another session
    std::string created;
    LotPlan     plan;
    std::vector<LotTest> tests;

    static constexpr int kGiveUp = 3;   // refusals in one step that set a needle aside

    int  okCount(int needle, int step) const;
    int  failCount(int needle, int step) const;     // refused, not removed
    // H7-2.5.0: a needle refused kGiveUp times in a step it has not finished is set aside: the
    // plan goes on without it, and the lot is judged on the others.
    bool needleGivenUp(int needle) const;
    bool needleDone(int needle) const;               // all of its steps, or set aside
    bool complete() const;
    int  doneTests() const;          // accepted, counted against the plan
    int  plannedTests() const;
    // The next test the plan still needs (false when complete).
    bool next(int* needle, int* step) const;
    // A needle's air parasitic: the mean of its accepted air sweeps.
    bool needleAir(int needle, double* cpF, double* gpS) const;

    std::string save() const;                    // the file's text
    bool load(const std::string& text);
};

// ---------------------------------------------------------------------------
struct LotLimits {
    // the lot itself must be consistent, or no single correction describes it
    double maxCpSdF        = 5e-12;   // between needles: 0.74 IPS units at 718 ohm
    double maxZCv          = 0.05;    // between needles, per phantom
    double maxIpsSd        = 0.5;     // between needles, per phantom
    double maxRepeatIpsSd  = 0.3;     // one needle, repeated
    double maxRepeatZCv    = 0.02;
    // a calibration can carry the lot
    double sameZ           = 0.03;    // within this the Z of the lot IS the reference's
    double sameIps         = 0.3;     // ... and so is its IPS
    double maxZFactorDev   = 0.30;    // a factor further than 30 % from 1 is a different needle
    double maxIpsShift     = 8.0;     // IPS units: beyond this the lot is a different instrument
    double maxZFactorSpread = 0.05;   // the factor must be one number across the strengths
    double maxResidIps     = 0.5;     // after the transfer, at every strength
    double maxResidZ       = 0.03;
    // H7-2.3.0: each needle against the needle model
    double maxLeakDev      = 0.15;    // R in the weakest saline against what the stronger one predicts
    double maxDrift        = 0.04;    // |Z| at 100 kHz over the repeats in the weakest saline
    double maxModelRms     = 0.05;    // the sweeps must look like a tip in saline
    double minCellRatio    = 0.7;     // the lot's cell ratio against the reference's, for any correction
    double maxCellRatio    = 1.3;
    double maxCellDev      = 0.15;    // one needle's tip against the lot's others
    double maxNeedleResidIps = 0.7;   // what a needle's own model leaves
    double maxNeedleResidZ = 0.04;
    // the saline check window handed to the devices (H7-2.3.0)
    double dipRWin         = 0.1;     // R: the good needles' range widened by 10 %
    double dipZeLo         = 0.5;     // the interface: half the lowest to 1.6 times the highest
    double dipZeHi         = 1.6;
    // H7-2.10.0: the QC limits of a lot, from its good needles
    double qcCpAbsF        = 3e-12;   // the stray capacitance window: its needles' range widened by
    double qcCpRel         = 0.1;     //   the larger of 3 pF and 10 % of the mean
    double qcGpFactor      = 3.0;     // leakage: three times the highest, at least 20 uS more
    double qcGpAddS        = 20e-6;
    double qcAlphaWin      = 0.15;    // the tip exponent: its needles' range widened by 0.15

    // H7-2.4.0: how far the instrument and the reference needle repeat by themselves (a control
    // run), as a floor under the tolerances: no lot is judged tighter than that. Clamped: a
    // control wider than this is not a repeat but a change.
    static constexpr double kFloorIpsMax = 0.5, kFloorZMax = 0.05, kFloorDriftMax = 0.06, kFloorLeakMax = 0.2;
    LotLimits withFloor(double ips, double z, double drift, double leak) const;
};

// How a sweep is processed before its features are read, as the trial did it:
// with the needle's own air calibration on gain range 3 (trialCalibrated), or
// as it arrived.
struct LotPipeline {
    bool trialCalibrated = true;
    const RangeModel* ranges = nullptr;   // the device's gain-range offsets (for the needle model)
};
// The features of a sweep (the caller's engine: range offsets, then extraction).
using LotFeatureFn = Features (*)(const Spectrum&, void* ctx);

// THE TRANSFER: a sweep taken with a needle of a lot, made into the sweep the
// REFERENCE needle would have given in the same tissue. The needle's own stray
// capacitance is removed at every point (it is a property of the needle, cable
// and connector, not of the gain range), the impedance is scaled by the ratio
// of the tips' cell constants (kZ, from saline), the reference needle's stray
// capacitance is put back, and then the sweep is processed as the trial
// processed the reference's (pipeline).
struct LotTransfer {
    double cpNeedleF = kNaN, gpNeedleS = 0;   // this needle (its air calibration), or its lot's mean
    double kZ = 1.0;
    double cpRefF = kNaN, gpRefS = 0;         // the reference needle
    bool   trialCalibrated = true;
};
void lotTransfer(Spectrum* s, const LotTransfer& t);
// The trial's processing alone: the needle's own air correction on range 3.
void lotPlain(Spectrum* s, double cpF, double gpS, bool trialCalibrated);

enum class LotVerdict2 {
    Incomplete,          // the plan is not finished
    NoReference,         // there is no complete reference to compare with
    SameAsReference,     // no correction needed
    UsableWithCalibration,
    RangesMustChange,    // consistent, but no calibration maps it onto the reference
    UsableWithPerNeedle, // H7-2.3.0: each needle's own model (its saline check before every case)
    Rejected             // the lot is not consistent within itself
};
const char* lotVerdictName(LotVerdict2 v);

struct LotPoint {          // one phantom strength
    double zLot = kNaN;    // the lot's Z here (25 °C)
    double zRef = kNaN;
    double kZ   = kNaN;    // zRef / zLot
    double dIps = kNaN;    // ipsRef - ipsLot
    double ipsLot = kNaN, ipsRef = kNaN;
    double zCv = kNaN, ipsSd = kNaN;            // between needles of the lot
};

// H7-2.3.0: one needle of the lot against the needle model (needlemodel.h).
struct LotNeedleQc {
    int    needle = 0;
    double cpF = kNaN, gpS = kNaN;      // its air sweeps
    ElectrodeModel e;                   // in the weakest saline (R at 25 °C)
    double rWeak = kNaN;                // R at 25 °C in the weakest saline ...
    double rNext = kNaN;                // ... and in the next stronger one
    double cellRatio = kNaN;            // the reference's R / its R, in the weakest saline
    double leakRatio = kNaN;            // (its R weak / R next) / the reference's: 1 = no leak
    double drift = kNaN;                // |Z| at 100 kHz over its repeats in the weakest saline, (max-min)/mean
    double ownIps = kNaN, ownZ = kNaN;  // left after the transfer with its own model, worst weak saline
    double lotIps = kNaN, lotZ = kNaN;  // ... with the lot's model
    ElectrodeModel e9;                  // in the 0.9 % saline (R at 25 °C)
    double r9 = kNaN;                   // R at 25 °C in the 0.9 %
    double cell9 = kNaN;                // the reference's R / its R, in the 0.9 %
    double own9Ips = kNaN, own9Z = kNaN;   // the transfer with the 0.9 % model, in the weak salines
    std::vector<std::string> why;       // why it is excluded; empty = good

    bool good() const { return why.empty(); }
};

struct LotResult {
    LotVerdict2 verdict = LotVerdict2::Incomplete;
    bool   provisional = false;          // H7-2.5.0: not every needle done; verdict is Incomplete,
    LotVerdict2 soFar = LotVerdict2::Incomplete;   // and this is what the done ones give
    int    needlesDone = 0;
    // H7-2.3.0: the needle model
    bool   model = false;
    int    mode = 0;                     // 0: one correction for the lot, 1: per needle
    std::vector<LotNeedleQc> needleQc;
    ElectrodeModel eRef;                 // the reference needle in the weakest saline (R at 25 °C)
    ElectrodeModel eLot;                 // the lot's good needles together
    double lotCpF = kNaN;                // their mean stray capacitance
    double lotGpS = 0;
    double kZLot = kNaN;                 // reference R / the lot's mean R, weakest saline
    double lotResidIps = kNaN, lotResidZ = kNaN;   // the lot's model on its good needles
    double needleResidIps = kNaN, needleResidZ = kNaN;   // each needle's own model
    double dipRMin = kNaN, dipRMax = kNaN, dipZeMin = kNaN, dipZeMax = kNaN;   // the saline check window
    int    goodNeedles = 0;
    ElectrodeModel eRef9;                // H7-2.4.0: the reference in the 0.9 % saline
    bool   valid09 = false, valid9 = false;     // the saline check validated with the 0.09 % / 0.9 %
    double worst9Ips = kNaN, worst9Z = kNaN;
    double dip9RMin = kNaN, dip9RMax = kNaN, dip9ZeMin = kNaN, dip9ZeMax = kNaN;
    // H7-2.4.0: a control run, against the reference
    double ctrlIps = kNaN, ctrlZ = kNaN, ctrlDrift = kNaN, ctrlLeak = kNaN;

    std::vector<LotPoint> pts;       // one per phantom strength, in step order
    double cpMeanF = kNaN, cpSdF = kNaN, gpMeanS = kNaN;
    double refCpF = kNaN;
    double kZMean = kNaN;
    // after the transfer (lotTransfer) with kZMean: what is left between lot and reference
    std::vector<double> residIps, residZ;   // per strength: IPS difference, Z ratio - 1
    double worstResidIps = kNaN, worstResidZ = kNaN;
    double worstRepeatIpsSd = kNaN, worstRepeatZCv = kNaN;
    std::vector<std::string> findings;   // every number that decided it, in words
    std::string headline;
};

LotResult evaluateLot(const LotRun& lot, const LotRun* reference, const LotPipeline& pipe,
                      LotFeatureFn feat, void* ctx, const LotLimits& lim = LotLimits());

// H7-2.3.0: the reference needle as the model: its stray capacitance, its interface and R in the
// weakest saline and R in the next (at 25 °C), and (H7-2.4.0) its model in the 0.9 %. (Also what
// the lot file carries of the reference, and its own saline check window.)
struct RefModel {
    bool   ok = false;
    double cpF = kNaN, gpS = kNaN;
    ElectrodeModel e;
    double rWeak = kNaN, rNext = kNaN;
    ElectrodeModel e9;
};
RefModel refModel(const LotRun& ref, const RangeModel* ranges);

// ---------------------------------------------------------------------------
// The comparison shown on the Lots tab (H7-2.2.0): per saline strength, the
// mean phase curve of the reference, of the lot as measured, and of the lot
// after the transfer, with their Z (25 °C) and IPS.
struct LotCurve {
    std::vector<double> freqHz, phaseDeg, magOhm;   // by frequency, ascending
};
struct LotCompareStep {
    LotCurve ref, lot, lotT;
    double zRef = kNaN, ipsRef = kNaN;
    double zLot = kNaN, ipsLot = kNaN;      // as measured (the trial's processing)
    double zLotT = kNaN, ipsLotT = kNaN;    // after the transfer
    int    nRef = 0, nLot = 0;              // sweeps averaged
};
struct LotCompare {
    bool ok = false;
    bool model = false;                     // H7-2.3.0: transferred with the needle model (res given)
    bool excluded = false;                  // sweeps of needles the evaluation excluded are among them
    std::vector<LotCompareStep> steps;      // index 0 = strength 1
    LotTransfer transfer;                   // what is applied (cpNeedleF = the lot's mean)
    double cpSdF = kNaN;
};
// With the evaluation (res), each needle is transferred as the verdict says: with the lot's model
// (one correction) or its own (per needle).
LotCompare compareLot(const LotRun& lot, const LotRun& reference, const LotPipeline& pipe,
                      LotFeatureFn feat, void* ctx, double kZ, const LotResult* res = nullptr);

// ---------------------------------------------------------------------------
// What a device applies to a reading taken with a needle of this lot.
// The saline check (H7-2.3.0): solutions a needle is checked in before a case.
constexpr int kCheck9 = 1;    // the 0.9 % saline: phantom step 1, sterile from the shelf
constexpr int kCheck09 = 3;   // the 0.09 % saline: phantom step 3, the weakest

// H7-2.10.0: what a needle of a lot must measure in its Air check and in the check saline.
struct QcLimits {
    bool   fromLot = false;        // false: the generic limits
    std::string lotId;
    double cpMinF = kNaN, cpMaxF = kNaN, gpMaxS = kNaN;      // air
    double rMin = kNaN, rMax = kNaN;                         // saline: R at 25 °C
    double zeMin = kNaN, zeMax = kNaN;                       // the interface at 1 kHz
    double alphaMin = kNaN, alphaMax = kNaN;
    double maxRms = 0.05;          // the sweeps must look like a tip in saline
    double maxDrift = 0.03;        // the two sweeps of the check
    // the two saline checks around Training sweeps (H7-2.10.0): the needle must not have changed
    double maxBracketR = 0.08;     // R at 25 °C (H7-2.15.0: from 3 %; a sound needle reads ±4 % from one dip to the next)
    double maxBracketZe = 0.2;     // the interface at 1 kHz

    std::string airWhy(double cpF, double gpS) const;        // empty: passes
    std::string salineWhy(double r25, double ze, double alpha, double rms, double drift) const;
    std::string describe() const;
};
QcLimits qcGeneric(int step);

struct LotCorrection {
    std::string lotId;
    std::string verdict;             // lotVerdictName at approval
    std::string date;
    int         needles = 0;
    double      cpF = kNaN, gpS = kNaN;   // the lot's mean air parasitic
    double      kZ = kNaN;                // the cell-constant ratio, reference / lot
    double      residIps = kNaN, residZ = kNaN;   // what the transfer left, worst strength
    std::vector<LotPoint> pts;            // the plain comparison, per strength (for the record, and the range map)
    // H7-2.3.0: the needle model
    int         mode = 0;                 // 0: the lot's model for every needle; 1: each needle's own (saline check)
    ElectrodeModel e;                     // the lot's mean interface, R at 25 °C in the weakest saline
    double      dipRMin = kNaN, dipRMax = kNaN, dipZeMin = kNaN, dipZeMax = kNaN;   // the 0.09 % check window
    // H7-2.4.0: the solutions the saline check was validated with (1: 0.09 %, 2: 0.9 %), the 0.9 % window
    int         dipMask = 0;
    double      dip9RMin = kNaN, dip9RMax = kNaN, dip9ZeMin = kNaN, dip9ZeMax = kNaN;
    // H7-2.10.0: the QC limits of the lot's needles
    double      qcCpMinF = kNaN, qcCpMaxF = kNaN, qcGpMaxS = kNaN;
    double      qcAlphaMin = kNaN, qcAlphaMax = kNaN, qcAlpha9Min = kNaN, qcAlpha9Max = kNaN;

    bool usable() const { return std::isfinite(kZ) && kZ > 0; }
    bool hasModel() const { return e.ok; }
    bool dipValid(int step) const { return hasModel() && (dipMask & (step == kCheck9 ? 2 : 1)) != 0; }
    bool hasQc() const { return std::isfinite(qcCpMinF) && std::isfinite(qcCpMaxF); }
    // Z and both IPS definitions, in place, at the measured (lot) Z.
    void apply(double* z1k, double* ipsLegacy, double* ipsFit) const;
    // kZ and dIps at a lot Z (interpolated in log Z, held at the ends).
    void at(double zLot, double* kZ, double* dIps) const;
    // The lot Z whose corrected value is zRef (for mapping rule boundaries).
    double lotZFor(double zRef) const;
    // H7-2.10.0: the limits its needles' QC is held to (the generic ones where the lot has none).
    QcLimits qcLimits(int step) const;
};
// H7-2.10.0: a lot's QC limits from its good needles (or, without a result, from its air sweeps).
void lotQcFill(const LotRun& run, const LotResult* res, const LotLimits& lim, LotCorrection* c);

// One rule box seen through a lot: where its edges lie in the lot's own
// coordinates, for when the ranges must change rather than the readings.
struct LotRange {
    std::string label;
    double zMin, zMax, ipsMin, ipsMax;      // reference
    double lzMin, lzMax, lipsMin, lipsMax;  // in the lot's coordinates
};
std::vector<LotRange> lotRanges(const LotCorrection& c,
                                const std::vector<LotRange>& refBoxes);

// ---------------------------------------------------------------------------
// The file every device loads (records/lotcal.txt; exported to USB as
// BIRADx_lotcal_<device>_rev<N>.txt). It carries EVERY approved lot, so the
// latest file is always the whole truth.
struct LotFile {
    int         rev = 0;
    std::string issued, device, refLot;
    bool        trialUncalibrated = false;   // the cut-offs were found WITHOUT per-needle air calibration
    double      refCpF = kNaN, refGpS = kNaN;
    ElectrodeModel refE;                     // H7-2.3.0: the reference needle in the weakest saline
    ElectrodeModel refE9;                    // H7-2.4.0: ... and in the 0.9 %
    std::vector<LotCorrection> lots;

    const ElectrodeModel& refFor(int step) const { return step == kCheck9 ? refE9 : refE; }
    const LotCorrection* find(const std::string& lotId) const;
    void put(const LotCorrection& c);         // add or replace by lot id
    std::string save() const;                 // with a CRC line
    bool load(const std::string& text, std::string* why = nullptr);
};

} // namespace birx
#endif
