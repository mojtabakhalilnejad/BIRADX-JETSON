// birx/matlib.h — the materials library: sweeps of known materials (tissues,
// phantoms, saline) labelled on the instrument, and the classifier learnt from
// them (H7-2.5.0; the needle's saline reference H7-2.6.0; the second layer
// H7-2.8.0; the needle's QC H7-2.10.0; the tip check H7-2.11.0).
//
// WHAT A SWEEP IS TURNED INTO
//
//   Each sweep is read at the 15 sweep frequencies (calib.h), its stray
//   capacitance taken off with the needle's Air check, and described by 31
//   numbers: the phase at each frequency (15), log10 |Z| at each frequency less
//   its mean over the sweep (15: the shape of the curve, not its level), and
//   that mean (1: the level). With the needle's saline reference (a sweep of
//   the same needle in 0.9 % saline, fitted with the needle model of
//   needlemodel.h) the tip's interface is taken off first and the level is
//   given relative to the needle's R in saline: the features then describe the
//   material, not the needle ("normalised").
//
// THE CLASSIFIER
//
//   A linear discriminant: one mean per material and one covariance shared by
//   all, its correlations shrunk toward none (as far as the library's own
//   spread says they cannot be trusted, never less than the number of
//   placements allows). Every answer is tested on the library itself, one
//   PLACEMENT (one sample of a material, all its sweeps) left out at a time:
//   sweeps of one placement are not independent, so leaving out single sweeps
//   would flatter it. From that test come the temperature of the
//   probabilities, the share of every answer left to doubt (the rule of
//   succession over placements), how far a material's own sweeps fall from it,
//   and which sweeps are set aside as not like their material.
//
//   Beside the whole curve: the blend (the mix of materials closest to the
//   sweep, weights on the simplex — a phantom between two strengths of saline
//   is read as between them), and the answer by Z at 1 kHz and IPS alone (a
//   quadratic discriminant in two numbers, the features the decision rules of
//   the trial use). A second layer weighs the three, learnt from the same
//   left-out placements.

#ifndef BIRX_MATLIB_H
#define BIRX_MATLIB_H

#include "types.h"

#include <string>
#include <vector>

namespace birx {

// The needle a sweep was taken with: its stray capacitance and leakage (the
// Air check) and its saline reference (the needle model fitted in 0.9 %
// saline; NaN when there is none).
struct MatNeedle {
    double cpF = kNaN, gpS = kNaN;
    double refR = kNaN, refA = kNaN, refAlpha = kNaN;
    bool hasRef() const;
};

struct MatSweep {
    std::string label;                   // the material
    int sample = 0;                      // which placement of it (sweeps of one placement share it)
    std::string when;                    // ISO time, UTC
    std::string needle;                  // the needle's serial, when known
    double cpF = kNaN;                   // the needle's stray capacitance (Air check)
    int settleS = -1;                    // seconds the tip had settled
    std::vector<Point> pts;              // the sweep as measured
    // what the classifier said when it was measured: the whole curve, and (H7-2.8.0) the final answer
    std::string said;
    double saidP = kNaN;
    int saidRight = -1;                  // -1 not known, 0 wrong, 1 right
    std::string saidF;
    double saidFP = kNaN;
    int saidFRight = -1;
    std::string qc;                      // H7-2.10.0: "suspect" when the needle's next saline check found it moved;
                                         // "pending" until a check closes it (H7-2.12.0: not learnt till then);
                                         // "ok" confirmed; "kept" learnt by hand without its closing check;
                                         // H7-2.12.6: "ok-raw" its R held but its tip surface moved: learnt as
                                         // measured, not needle-normalised
    std::string qcMove;                  // H7-2.12.6: how far the needle moved between the two checks around it
    enum { kAuto = 0, kUse = 1, kAside = 2 };
    int use = kAuto;                     // auto: the classifier's own check decides; use / aside: by hand
    bool aside = false;                  // the last fit set it aside (written for whoever reads the file)
    std::string origin;                  // where it came from (another device's library, a dump)
    std::string device;
    int origSample = -1;
    // H7-2.6.0: the needle's leakage and saline reference
    double gpS = kNaN;
    double refR = kNaN, refA = kNaN, refAlpha = kNaN;

    MatNeedle needleInfo() const;
    void setNeedle(const MatNeedle& n);
};

struct MatLibrary {
    std::vector<std::string> labels;     // in the order they were made
    std::vector<MatSweep> sweeps;
    std::string device;                  // the device the library was made on
    std::string exported;                // when it was exported
    std::vector<std::string> off;        // H7-2.12.0: labels left out of the comparison (kept, not learnt)

    bool hasLabel(const std::string& l) const;
    bool isOff(const std::string& l) const;
    int count(const std::string& label) const;
    int placements(const std::string& label) const;   // H7-2.11.0: how many placements of it
    // sweeps the classifier's answer is known for, and (right) how many it said right
    int told(int* right) const;
    int toldFinal(int* right) const;     // H7-2.8.0: the final answer

    std::string save() const;            // BIRADX-MATLIB 1
    bool load(const std::string& text);
};

std::string matSweepLine(const MatSweep& s);           // "S=..." as saved
bool matParseSweep(const std::string& v, MatSweep* out);

// The 31 features of a sweep (empty when one of the 15 frequencies is
// missing). normalised: the needle's interface taken off and the level made
// relative to its R in saline (empty without a reference).
std::vector<double> matFeatures(const std::vector<Point>& pts, double cpF);
std::vector<double> matFeatures(const std::vector<Point>& pts, const MatNeedle& n, bool normalised);

// H7-2.6.0: the needle's saline reference from a sweep of it in 0.9 % saline,
// with its Air check's stray capacitance. false with the reason when the sweep
// is not that of a tip in saline.
bool matNeedleRef(const std::vector<Point>& pts, double cpF, double gpS, MatNeedle* out, std::string* why);
// H7-2.6.2: the stray capacitance with which the sweep looks most like a tip in
// saline (2 pF .. 10 nF), and the model's residual with it.
double matBestSalineCp(const std::vector<Point>& pts, double gpS, double* rmsOut);

// H7-2.11.0: is the needle's tip sound in this sweep (as measured)? Found on the bench (records_3,
// potatoes): residue between the tip's two electrodes made most sweeps of the needle itself.
//  - Short: |Z| flat within 4 % from 500 Hz to 50 kHz and below 150 ohm — the instrument's floor
//    (~76 ohm on its lowest range). A tip in any electrolyte rises at the low frequencies (its
//    electrode interface): a flat curve has no material in it.
//  - Bridged: |Z| at 500 kHz below minRatio x refR, the resistance the same needle read in 0.9 %
//    saline (refR NaN: not judged). No tissue or phantom conducts that much more than normal saline.
enum class TipFault { None = 0, Short = 1, Bridged = 2 };
TipFault matTipFault(const std::vector<Point>& pts, double refR, double minRatio, std::string* why);

// H7-2.15.0: a sweep that is not of a needle's tip in any material (records_7: a resistor of 1 and of 3 kOhm on the
// bench was taken as readings, one of them High Risk, another Low Risk). "" when it is one; else why not:
//  - the same |Z| and phase at five frequencies or more: the measurement unit did not measure (a stale sweep);
//  - no tip surface: a tip in any electrolyte, saline or tissue, has its electrode interface in series, which
//    shows at 500 Hz to 1 kHz as a reactance of hundreds of ohm (records_7: 403 ohm the least of 123 sweeps of
//    saline, coffee, cola and rose water; a resistor 0 to 5). Below zeImagOhm x 0.3 (the reactance of this
//    needle's own tip surface at 1 kHz, from its saline check; at most 60 ohm, at least 20) or, unknown, below
//    50 ohm, the electrodes are joined by a plain resistance: a resistor, a wire or the connector.
std::string matNotATip(const std::vector<Point>& pts, double zeImagOhm);
constexpr double kTipBridgedRatio = 0.8;       // Training: materials at room temperature, as its saline check
constexpr double kTipBridgedCaseRatio = 0.65;  // a case: tissue at 37 degrees C against R at 25 (x 1/1.24), and a margin

struct MatMatch {
    bool ok = false;
    std::vector<std::string> labels;
    std::vector<double> prob;            // per material: the whole curve's answer, eps left to doubt
    std::vector<double> d2;              // per material: the squared distance to it (Mahalanobis)
    int best = -1;
    bool unlike = false;                 // farther from the best material than its own sweeps fall
    double d2Limit = kNaN;
    // H7-2.8.0: the blend — the mix of materials closest to the sweep
    std::vector<double> w;
    double blendD2 = kNaN;               // the sweep's distance to the mix
    bool normalised = false;
    int rangeDiff = 0;                   // set by the caller: points measured on another gain range than the material's
    // H7-2.8.0: the answer by Z at 1 kHz and IPS
    bool zOk = false;
    double z1k = kNaN, ips = kNaN;
    std::vector<double> zProb;
    int zBest = -1;
    bool zUnlike = false;
    // H7-2.8.0: the final answer (the second layer)
    bool fOk = false;
    std::vector<double> fProb;
    int fBest = -1;
    std::string why;                     // when !ok
};

// H7-2.8.0: the answer by Z at 1 kHz and IPS: per material the mean of
// (log10 Z, IPS) and its covariance (xx, xy, yy), between the material's own
// and the pooled one (lambda), learnt with the placements left out.
struct MatZipsModel {
    bool ok = false;
    std::vector<std::vector<double>> mean;    // per material: log10 Z, IPS
    std::vector<std::vector<double>> cov;     // per material: xx, xy, yy
    double lambda = 1.0;                 // 1: the pooled covariance, 0: the material's own
    double T = 1.0;
    double eps = 0.5;
    double d2Limit = kNaN;
    int nTested = 0, nCorrect = 0;
};

// H7-2.8.0: the second layer: the final probabilities are the softmax of the
// weighted sum of the three answers' log-probabilities.
struct MatStack {
    bool ok = false;
    double w[3] = { 1.0, 0.5, 0.5 };         // whole curve, blend, Z and IPS
    double eps = 0.5;
    int nTested = 0, nCorrect = 0;           // the left-out sweeps it was learnt from, said right
};

struct MatModel {
    bool ok = false;
    std::string why;                     // when !ok: what the library still needs
    std::vector<std::string> labels;     // the materials with enough sweeps
    int k = 0;                           // features compared (31)
    std::vector<std::vector<double>> mean;    // per material, k
    std::vector<std::vector<double>> prec;    // the shared covariance's inverse, k x k
    double lambda = 1.0;                 // how far its correlations were shrunk toward none (0..1)
    double T = 1.0, d2Limit = kNaN;
    // the leave-out test, by sample
    int nTested = 0, nCorrect = 0;       // sweeps
    int nPlacements = 0;                 // placements left out
    // the share of every answer left to doubt: (errors + 1) / (placements + 2), the rule of
    // succession. With 12 placements all told right no answer is more than 95 % sure; the more
    // placements are labelled, the surer it can be.
    double eps = 0.5;
    std::vector<std::vector<int>> confusion;  // [true][predicted]
    // H7-2.5.2: the sweeps set aside. ownLimit: how far a material's own sweeps fall from it (the
    // 99.9 % chi-square point scaled by the median left-out distance, not below it). A sweep beyond
    // asideLimit (kAsideFactor times that point scaled by the median alone, never below the
    // chi-square point) — asideGross (kAsideGross times, never below 3 chi-square points) when its
    // material has fewer than kAsideMinPlaces placements, since then a needle of its own may look
    // far — is set aside, at most one in five of a material's sweeps and never leaving it fewer
    // than two.
    double ownLimit = kNaN, asideLimit = kNaN, asideGross = kNaN;
    enum { kNotIn = -1, kUsed = 0, kAsideAuto = 1, kAsideByHand = 2, kUsedByHand = 3, kQcSuspect = 4, kTipFault = 5,
           kQcPending = 6, kOff = 7,            // H7-2.12.0: waiting for its closing check; its label left out
           kRawOnly = 8 };                      // H7-2.12.6: (the normalised model) its tip surface moved between the checks
    std::vector<int> state;              // per library sweep
    std::vector<double> ownD2;           // per library sweep: its distance to its own material (left out)
    int nAside = 0;                      // set aside by the check
    int nQcSuspect = 0;                  // H7-2.10.0: not learnt: the needle's next saline check found it moved
    int nTipFault = 0;                   // H7-2.11.0: not learnt: the needle's tip was shorted or bridged (matTipFault)
    int nQcPending = 0;                  // H7-2.12.0: not learnt (yet): no saline check after it has confirmed it
    int nOff = 0;                        // H7-2.12.0: not learnt: its label is left out of the comparison
    int nRawOnly = 0;                    // H7-2.12.6: (normalised) not learnt: its tip surface moved between the checks
    bool normalised = false;             // H7-2.6.0: learnt from needle-normalised features (the sweeps with a reference)
    int nSweeps = 0;                     // the sweeps it was learnt from
    // H7-2.8.0: the blend's largest weight on the right material, placements left out; the
    // answer by Z and IPS; and the final one
    int nBlendCorrect = 0;
    MatZipsModel zips;
    MatStack stack;
    MatMatch classify(const std::vector<Point>& pts, double cpF) const;
    MatMatch classify(const std::vector<Point>& pts, const MatNeedle& n) const;
};

constexpr double kAsideFactor = 3.0, kAsideGross = 6.0;
constexpr int kAsideMinPlaces = 4;

MatModel trainMaterials(const MatLibrary& lib, bool normalised = false);

} // namespace birx
#endif
