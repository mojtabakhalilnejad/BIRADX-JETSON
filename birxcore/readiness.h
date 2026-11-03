// birx/readiness.h — when is it time to replace the bench band with a fit?
//
// The device starts with the bench band: the rule the needles are known to
// give. It cannot improve on that by itself, and it must not pretend to. What
// it CAN do is say, from its own records, whether enough evidence has
// accumulated for a fit to be worth attempting — and be specific about what is
// still missing, so the gap is a task rather than a mystery.
//
// The criteria below are not round numbers picked to look reasonable. Each one
// is the point at which a quantity that matters becomes estimable:
//
//   150 cases with an outcome
//       At AUC 0.90 with balanced classes, the Hanley-McNeil standard error of
//       the AUC is 0.051 at 40 cases, 0.036 at 80, and 0.026 at 150 — so the
//       95% interval first narrows to about +-0.05 at 150. Below that, a fit
//       and the band cannot be told apart no matter how they actually differ,
//       and "the new model is better" would be a statement about noise.
//
//   60 cases in the smaller class
//       Two things at once. It is 15 events per variable for a four-feature
//       model, above the usual 10 that keeps a logistic fit from overfitting
//       its own sample. And it puts the 95% interval on a rule-out sensitivity
//       of 0.98 at about +-0.035 — the difference between claiming 98% and
//       being able to defend it.
//
//   80% of measured cases have an outcome
//       This is the one that is easy to skip and expensive to skip. Outcomes
//       do not go missing at random: a lesion the device called Low Risk is
//       less likely to be biopsied, so its pathology never arrives. Fit on
//       what came back and the model learns the referral pattern and confirms
//       whatever the incumbent rule already believed. Below 80% the sample is
//       censored enough that this is the likely outcome.
//
//   8 needle lots, 4 operators, 60 days
//       A model fitted inside one lot, one pair of hands, or one fortnight
//       fits that lot, those hands, that fortnight. The two trials already
//       showed the failure: the same IPS rule scored Youden 0.83 on one needle
//       and 0.00 on the other. Spread is not a nicety here.
//
// All of these are about the DATA. The decisive criterion is not here, because
// the device cannot evaluate it: a candidate fit must be shown to beat the
// incumbent band on held-out cases. That comparison belongs to birx-train,
// which does it with a paired bootstrap and refuses to write a model that
// cannot show the improvement.

#ifndef BIRX_READINESS_H
#define BIRX_READINESS_H

#include "dataset.h"
#include <string>
#include <vector>

namespace birx {

struct ReadinessConfig {
    int    minCases            = 150;
    int    minMinorityClass    = 60;
    double minOutcomeFraction  = 0.80;
    int    minNeedleLots       = 8;
    int    minOperators        = 4;
    int    minSpanDays         = 60;
};

struct Criterion {
    std::string id;
    std::string label;
    std::string why;        // one line, shown under the row
    double have    = 0.0;
    double need    = 0.0;
    std::string unit;
    bool   met     = false;
    bool   blocking= true;  // advisory criteria are shown but do not hold up a fit
};

struct Readiness {
    std::vector<Criterion> criteria;

    bool   ready() const;         // every blocking criterion met
    int    metCount() const;
    int    blockingCount() const;
    // 0..1 — the WORST blocking criterion, not the average. An average would
    // read 90% while the one thing that matters sat at zero.
    double progress() const;
    std::string headline() const;
    std::vector<std::string> missing() const;
};

// rows: the joined training table (readings with an outcome, quality passed).
// measuredCases: distinct cases measured, whether or not an outcome arrived —
// needed for the censoring criterion, and it cannot be derived from rows.
Readiness assessReadiness(const std::vector<TrainingRow>& rows,
                          int measuredCases,
                          const ReadinessConfig& cfg = ReadinessConfig());

// Counts the distinct cases in dataset.csv, including those with no outcome.
int countMeasuredCases(const std::string& datasetCsv);

} // namespace birx
#endif
