// birx/needlemodel.h — a needle tip in a solution as an equivalent circuit (H7-2.3.0).
//
//     Z(f) = R + A (j 2 pi f)^-alpha,  in parallel with the needle's stray Cp || Gp
//
// R is the solution seen through the tip (its cell constant times the
// solution's resistivity); A (jw)^-alpha is the metal-solution interface, a
// constant-phase element. Saline has no dispersion of its own between 500 Hz
// and 500 kHz, so a sweep of a tip in saline is this circuit and nothing else:
// fitted to it, R gives the cell constant and the interface terms describe the
// tip's surface. Two needles of a lot can then be made into each other at
// every frequency: take off one needle's stray capacitance and interface,
// scale by the ratio of the cell constants, put the other's back.
//
// The phase on the wire carries the measurement unit's own 500 kHz correction
// (calib.h) and the gain-range offsets the device adds; both depend on the gain
// range, which follows |Z|, so they are taken off before a sweep is changed and
// put back for the range the changed |Z| falls in.

#ifndef BIRX_NEEDLEMODEL_H
#define BIRX_NEEDLEMODEL_H

#include "types.h"
#include <complex>
#include <vector>

namespace birx {

struct ElectrodeModel {
    double rOhm  = kNaN;   // R: the solution through the tip
    double a     = kNaN;   // the interface: A (jw)^-alpha
    double alpha = kNaN;
    double rms   = kNaN;   // the fit's residual, relative to |Z|
    int    n     = 0;      // points fitted
    bool   ok    = false;

    std::complex<double> zi(double freqHz) const;   // the interface, A (jw)^-alpha; 0 when not ok
    double ze1k() const;                            // its magnitude at 1 kHz
};

// The model fitted to the points of a sweep between fLoHz and fHiHz (weighted
// by 1/|Z|^2; alpha searched from 0.30 to 1.00). ok when at least five points
// fit with a residual below 0.2.
ElectrodeModel fitElectrode(const Spectrum& s, double fLoHz = 900.0, double fHiHz = 110000.0);

// The device's gain-range phase offsets (calib.h, RangeCalibrator), index 1..6.
struct RangeModel {
    double offsetDeg[7] = { 0, 0, 0, 0, 0, 0, 0 };
};

// The phase as the tip saw it (the unit's 500 kHz correction and the device's
// gain-range offsets taken off), and back again for each point's range.
void instrumentToTrue(Spectrum* s, const RangeModel* rm);
void trueToInstrument(Spectrum* s, const RangeModel* rm);

// The needle's stray capacitance and leakage taken off every point.
void deembedAll(Spectrum* s, double cpF, double gpS);

// A sweep of one needle made into the sweep another (the reference) would have
// given in the same tissue. With both models: the needle's interface is taken
// off, R scaled by kZ, the reference's interface put back; without: |Z| is
// scaled by kZ (the transfer of H7-2.0.0).
struct NeedleTransfer {
    double cpNeedleF = kNaN, gpNeedleS = 0;
    ElectrodeModel eNeedle;
    double kZ = 1.0;
    double cpRefF = kNaN, gpRefS = 0;
    ElectrodeModel eRef;
    bool   trialCalibrated = true;
    const RangeModel* ranges = nullptr;        // the device's gain-range offsets
};
void needleTransfer(Spectrum* s, const NeedleTransfer& t);

} // namespace birx
#endif
