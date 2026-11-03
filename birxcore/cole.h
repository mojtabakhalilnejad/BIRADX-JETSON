// birx/cole.h — the Cole model, fitted to one sweep.
//
//     Z(w) = Rinf + (R0 - Rinf) / (1 + (j*w*tau)^alpha)
//
//   R0     resistance as frequency -> 0. Current cannot cross the cell
//          membranes there, so it flows only in the extracellular space:
//          R0 is a measure of extracellular volume.
//   Rinf   resistance as frequency -> infinity. The membranes are shorted by
//          their own capacitance, so current crosses everything: Rinf reflects
//          total tissue volume, intracellular included.
//   fc     characteristic frequency, 1/(2*pi*tau). Where the membranes stop
//          behaving like insulators — a property of cell size and membrane
//          capacitance.
//   alpha  in (0,1]. How spread out the relaxation is. alpha = 1 is a single
//          time constant; real tissue sits around 0.6-0.9 because it is a
//          population of cells, not one cell.
//
// Why this is worth the arithmetic, given that a phase slope already reaches
// AUC 0.92:
//
//   1. It is far less sensitive to electrode contact than a single-frequency
//      quantity. Contact impedance sits mostly in series and mostly at low
//      frequency; the ARC's shape survives what a point measurement does not.
//      That is exactly the failure mode the India cohort showed.
//
//   2. R0/Rinf is DIMENSIONLESS. Every absolute quantity this instrument
//      produces depends on needle geometry — which is why thresholds did not
//      transfer between the two needle revisions, one scoring Youden +0.83 and
//      the other +0.00. A ratio cancels the geometry factor to first order and
//      is the most likely quantity to survive a needle change.
//
//   3. The four parameters summarise fifteen complex measurements with
//      physical meaning attached, so a coefficient can be argued about by
//      someone who knows tissue rather than only by someone who knows fitting.
//
// The fit is Levenberg-Marquardt on relative residuals, so a 1 kHz point at
// 2000 ohm and a 500 kHz point at 200 ohm carry comparable weight. It reports
// its own goodness of fit; a sweep that is not an arc returns no parameters
// rather than plausible-looking ones. A tissue spectrum that fails to fit a
// Cole arc is usually a contact artefact, which makes coleRmsRel useful as a
// quality signal in its own right.

#ifndef BIRX_COLE_H
#define BIRX_COLE_H

#include "types.h"

namespace birx {

struct ColeFit {
    bool   ok      = false;
    double r0      = kNaN;   // ohm
    double rInf    = kNaN;   // ohm
    double fcHz    = kNaN;
    double alpha   = kNaN;
    double rmsRel  = kNaN;   // RMS of |Z_fit - Z_meas| / |Z_meas|
    int    nPoints = 0;
    int    iterations = 0;

    double ratio() const { return (rInf > 0) ? r0 / rInf : kNaN; }
};

struct ColeConfig {
    // Electrode polarisation dominates below roughly a kilohertz with a needle
    // electrode and is not part of the tissue's own dispersion. Excluding that
    // region is standard practice; where exactly to cut is a question the
    // recorded sweeps should answer, which is why it is a field and not a
    // constant.
    double loHz = 1000.0;
    double hiHz = 500000.0;
    int    minPoints = 6;
    int    maxIterations = 200;
    double maxRmsRel = 0.15;   // above this the sweep is not an arc; reject
};

// Complex impedance of the model at one frequency.
void coleImpedance(double freqHz, double r0, double rInf, double fcHz, double alpha,
                   double* re, double* im);

ColeFit fitCole(const Spectrum& s, const ColeConfig& cfg = {});

} // namespace birx
#endif
