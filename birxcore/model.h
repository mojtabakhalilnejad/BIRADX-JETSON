// birx/model.h — the risk model, as an installed, versioned, auditable file.
//
// The model is a calibrated linear model over named features. That is a
// deliberate choice, not a limitation:
//
//   * With 88 + 138 cases and four or five usable features, a regularised
//     linear model is at the capacity the data supports. On Trial 2, IPS alone
//     reached cross-validated AUC 0.918 and every richer combination landed
//     between 0.914 and 0.929 — there is nothing for a larger model to find.
//   * Every coefficient can be read, signed, and argued about by a clinician.
//     A sign that contradicts the physics is visible at a glance.
//   * The file is diffable. Two model versions can be compared line by line,
//     which is what a change-control process actually needs.
//
// The file format is plain text for the same reason. A reviewer should be able
// to open the model in an editor and understand it without tooling.
//
// Scoring:
//     z = intercept + sum_i coef_i * (x_i - mean_i) / scale_i
//     p = 1 / (1 + exp(-(calibA * z + calibB)))
//
// calibA/calibB are Platt scaling fitted on held-out folds, so p is a
// probability and not just a monotone score. Tiers come from two thresholds
// on p, chosen by the trainer from an explicit cost ratio.
//
// A model refuses to score when any feature it needs is missing. There is no
// imputation: a missing feature means the measurement failed, which is a job
// for the quality gate, not for a default value.

#ifndef BIRX_MODEL_H
#define BIRX_MODEL_H

#include "types.h"
#include <string>
#include <vector>

namespace birx {

struct ModelTerm {
    std::string name;
    double mean  = 0.0;
    double scale = 1.0;
    double coef  = 0.0;
};

class RiskModel {
public:
    // --- identity, all of it required before a model may be used -----------
    std::string version;        // e.g. "m2026-09-14-a"
    std::string trainedOn;      // ISO date
    std::string needleRev;      // the needle revision this was fitted for
    std::string endpoint;       // what the probability means, in words
    int    nCases     = 0;
    int    nPositive  = 0;
    double auc        = kNaN;
    double aucCiLo    = kNaN;
    double aucCiHi    = kNaN;

    // --- kind 1: a fitted linear model ------------------------------------
    std::vector<ModelTerm> terms;

    // --- kind 2: an empirical BAND ----------------------------------------
    //
    // What the bench and the needles in production actually give is a range,
    // not a coefficient: risk when IPS lies between two numbers, provided the
    // tip is not in fat. A logistic model cannot express that — it is monotone
    // in IPS, so it can put a threshold at one end of the band but never at
    // both. Forcing the band into a logistic fit would mean shipping a model
    // that disagrees with the bench at the upper edge, and then arguing with
    // the bench.
    //
    // So a band is its own kind of model, written out in the same file, with
    // the edges as plain numbers a reader can check against the bench note.
    //
    //   p = sigmoid(sharpness * d),  d = min(x - lo, hi - x)
    //
    // d is the distance INSIDE the band in IPS units, so d = 0 at either edge
    // and p = 0.5 there. thresholdHigh is therefore pinned to 0.5 and the tier
    // flips exactly on the edge — the agreement with the bench rule is exact
    // by construction, not by fitting.
    //
    // THE PROBABILITY IS NOT CALIBRATED. It is a monotone readout of how far
    // inside the band a reading sits; it is not a frequency, and nothing may
    // present it as one until a fitted model replaces this.
    // Set only by RuleTable::asModel(). It is never read from or written to a
    // model file — it marks the placeholder handed to the aggregator while the
    // instrument's own rule table is deciding, so that nothing downstream
    // prints a percentage that was never a probability.
    bool        isRules       = false;

    bool        isBand        = false;
    std::string bandFeature   = "ipsLegacy";
    double      bandLo        = kNaN;
    double      bandHi        = kNaN;
    double      bandSharpness = 0.4;    // probability slope, per IPS unit
    // Width of a Suspicious shoulder OUTSIDE each edge, in IPS units.
    // 0 means no Suspicious tier and exact two-way agreement with the bench.
    double      susMarginIps  = 0.0;
    double intercept = 0.0;
    double calibA    = 1.0;
    double calibB    = 0.0;

    double thresholdLow  = 0.20;   // p below this  -> Low
    double thresholdHigh = 0.60;   // p above this  -> High; between -> Suspicious

    // -----------------------------------------------------------------------
    bool   valid() const;
    // Returns NaN when a required feature is missing or the model is invalid.
    double probability(const Features& f) const;
    double linearScore(const Features& f) const;

    bool load(const std::string& path, std::string* error = nullptr);
    bool save(const std::string& path) const;

    // A model that always answers "cannot tell". Installed on a fresh device so
    // that the absence of a model is an explicit state rather than a crash.
    static RiskModel abstaining();

    // The bench rule, as an installable model. Used to seed a device before
    // any outcome has come back, so that the new software starts by agreeing
    // exactly with what the needles are known to do, and the disagreement
    // when a fitted model later replaces it is visible and dated.
    static RiskModel band(const std::string& feature, double lo, double hi,
                          const std::string& needleRev,
                          const std::string& version,
                          double sharpness = 0.4, double susMargin = 0.0);

    std::string describe() const;

private:
    static double sigmoid(double z);
};

} // namespace birx
#endif
