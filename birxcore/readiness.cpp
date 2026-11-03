#include "readiness.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace birx {

bool Readiness::ready() const {
    for (const Criterion& c : criteria) if (c.blocking && !c.met) return false;
    return !criteria.empty();
}

int Readiness::metCount() const {
    int n = 0;
    for (const Criterion& c : criteria) if (c.blocking && c.met) ++n;
    return n;
}

int Readiness::blockingCount() const {
    int n = 0;
    for (const Criterion& c : criteria) if (c.blocking) ++n;
    return n;
}

double Readiness::progress() const {
    // The worst blocking criterion, not the average. An average would read
    // "90% ready" while the one thing that actually matters sat at zero.
    double worst = 1.0;
    for (const Criterion& c : criteria) {
        if (!c.blocking) continue;
        const double p = (c.need > 0) ? std::min(1.0, c.have / c.need) : 1.0;
        worst = std::min(worst, p);
    }
    return worst;
}

std::vector<std::string> Readiness::missing() const {
    std::vector<std::string> out;
    for (const Criterion& c : criteria) if (c.blocking && !c.met) out.push_back(c.label);
    return out;
}

std::string Readiness::headline() const {
    std::ostringstream o;
    if (ready()) {
        o << "Ready to fit. Run a training pass off the device; the bench band "
             "stays installed until a fit is shown to beat it.";
    } else {
        o << metCount() << " of " << blockingCount() << " conditions met — "
          << "keep collecting. Still short on: ";
        const std::vector<std::string> m = missing();
        for (size_t i = 0; i < m.size(); ++i) { if (i) o << ", "; o << m[i]; }
        o << ".";
    }
    return o.str();
}

// ---------------------------------------------------------------------------

Readiness assessReadiness(const std::vector<TrainingRow>& rows,
                          int measuredCases,
                          const ReadinessConfig& cfg) {
    Readiness R;

    // One entry per CASE. Several readings of one lesion are not several
    // pieces of evidence, and counting them as such is how a device talks
    // itself into a fit it cannot support.
    std::set<std::string> cases, positives, negatives, lots, operators;
    int64_t firstMs = 0, lastMs = 0;

    for (const TrainingRow& r : rows) {
        if (!r.usable) continue;
        if (r.pathology == Pathology::Unknown) continue;
        cases.insert(r.caseId);
        if (r.pathology == Pathology::Benign) negatives.insert(r.caseId);
        else                                  positives.insert(r.caseId);

        auto it = r.covariates.find("needle_lot");
        if (it != r.covariates.end() && !it->second.empty()) lots.insert(it->second);
        if (!r.operatorId.empty()) operators.insert(r.operatorId);

        if (r.epochMs > 0) {
            if (firstMs == 0 || r.epochMs < firstMs) firstMs = r.epochMs;
            if (r.epochMs > lastMs) lastMs = r.epochMs;
        }
    }

    const int nCases    = int(cases.size());
    const int nMinority = int(std::min(positives.size(), negatives.size()));
    const double outcomeFraction = measuredCases > 0
                                 ? double(nCases) / double(measuredCases) : 0.0;
    const double spanDays = (lastMs > firstMs)
                          ? double(lastMs - firstMs) / (1000.0 * 60 * 60 * 24) : 0.0;

    auto add = [&](const char* id, const char* label, const char* why,
                   double have, double need, const char* unit, bool blocking) {
        Criterion c;
        c.id = id; c.label = label; c.why = why;
        c.have = have; c.need = need; c.unit = unit;
        c.blocking = blocking;
        c.met = have >= need;
        R.criteria.push_back(c);
    };

    add("cases", "Cases with a pathology result",
        "Below this the interval on the AUC is wider than the difference a new "
        "model could plausibly make, so a fit could not be shown to be better.",
        nCases, cfg.minCases, "cases", true);

    add("minority", "Cases in the smaller class",
        "15 events per variable for a four-feature model, and a rule-out "
        "sensitivity of 0.98 pinned to about +-0.035.",
        nMinority, cfg.minMinorityClass, "cases", true);

    add("outcomes", "Share of measured cases whose outcome came back",
        "Outcomes do not go missing at random: a lesion called Low Risk is less "
        "likely to be biopsied. Fit on a censored sample and the model learns "
        "the referral pattern instead of the tissue.",
        outcomeFraction * 100.0, cfg.minOutcomeFraction * 100.0, "%", true);

    add("lots", "Distinct needle lots",
        "The same IPS rule scored Youden 0.83 on one trial needle and 0.00 on "
        "the other. A model fitted inside one lot fits that lot.",
        double(lots.size()), cfg.minNeedleLots, "lots", true);

    add("span", "Days between the first and last case",
        "Cases from one fortnight carry that fortnight's instrument state.",
        spanDays, cfg.minSpanDays, "days", true);

    add("operators", "Distinct operators",
        "Placement is the largest source of variation the model cannot see. "
        "One pair of hands means the model is fitted to those hands.",
        double(operators.size()), cfg.minOperators, "operators", false);

    return R;
}

int countMeasuredCases(const std::string& datasetCsv) {
    std::ifstream f(datasetCsv);
    if (!f) return 0;
    std::string line;
    if (!std::getline(f, line)) return 0;

    int caseCol = -1;
    { std::istringstream h(line); std::string cell; int i = 0;
      while (std::getline(h, cell, ',')) { if (cell == "case_id") { caseCol = i; break; } ++i; } }
    if (caseCol < 0) return 0;

    std::set<std::string> ids;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream r(line);
        std::string cell;
        for (int i = 0; i <= caseCol; ++i) if (!std::getline(r, cell, ',')) { cell.clear(); break; }
        if (!cell.empty()) ids.insert(cell);
    }
    return int(ids.size());
}

} // namespace birx
