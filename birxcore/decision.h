// birx/decision.h — quality gate, then model, then tier. In that order.
//
// The ordering is the safety property. A measurement that fails its own checks
// never reaches the model, so no arrangement of feature values can turn a
// failed placement into a reassuring answer. That was the defect behind 13 of
// the 17 false negatives in the India cohort.
//
// The engine also recomputes the firmware's own rule on every measurement and
// records whether the two agree. Disagreement is not an error and does not
// change the verdict; it is logged so that a change in behaviour after an
// update is measurable rather than anecdotal.
//
// Per-case aggregation lives here too, in Aggregator. The shipped software
// called a case High Risk if ANY of its readings landed in the decision box,
// which lets the number of readings leak into the verdict: a six-reading case
// got six chances to trigger and a one-reading case got one. In the India
// cohort benign cases carried 2.9 readings on average against 2.2 for
// malignant, so the artefact worked specifically against specificity. The
// aggregator uses the median of the valid readings instead, which does not
// depend on how many were taken, and reports Invalid when too few survive.

#ifndef BIRX_DECISION_H
#define BIRX_DECISION_H

#include "types.h"
#include "featureset.h"
#include "quality.h"
#include "model.h"
#include "calib.h"
#include <memory>
#include <string>
#include <vector>

namespace birx {

struct DecisionConfig {
    // Refuse to score when the model was fitted for a different needle. IPS
    // thresholds did not transfer between the two needles in the trial data:
    // the same rule scored Youden +0.83 on one and +0.00 on the other.
    bool enforceNeedleRevision = true;

    // Compare against the firmware's own rule and flag disagreement.
    bool compareWithLegacy = true;

    // The firmware's rule, reproduced for that comparison.
    double legacyFatMaxOhm = 1500.0;
    double legacyIpsMin    = -11.0;
    double legacyIpsMax    =   0.0;
};

// ---------------------------------------------------------------------------
// The rule table — the instrument's own decision boxes, still in charge.
//
// While the installed model is the BENCH BAND rather than a fit, the numbers
// that decide are the ones in the settings file, written in the instrument's
// own coordinates: impedance at 1 kHz in ohms and the firmware's IPS. Those
// are the numbers the factory has bench evidence for and the numbers an
// engineer edits when the bench evidence changes, so while the model is
// provisional they, and not the model, are what the screen must show.
//
// This is deliberately NOT a second model. It carries no probability, no
// calibration and no claim to one; it reports a tier and which row produced
// it. The probability it hands to the aggregator is a placeholder whose only
// job is to survive the median — see pseudoProbability below.
struct RuleRow {
    double zMin = 0, zMax = 0;      // ohms at 1 kHz, both inclusive
    double ipsMin = 0, ipsMax = 0;  // firmware IPS, both inclusive

    /* WHAT THE OPERATOR SEES, and it is theirs to write.
     *
     * The label is free text and is displayed verbatim — not mapped onto a
     * fixed vocabulary of three outcomes. A site that reports in its own
     * words, or in its own language, or that wants a row to say "repeat this
     * placement" rather than a risk grade, writes exactly that here and the
     * screen says exactly that.
     *
     * The colour is that label's colour on the screen, as "#rrggbb", and the
     * sound is the LABEL of an entry in the instrument's sound library — not
     * a file path and not an index into a fixed bank, so renaming or
     * replacing the file does not silently change which rule is silent. An
     * empty colour means "use the colour of the level"; an empty sound means
     * this rule says nothing aloud. */
    std::string label;
    std::string color;              // "#rrggbb", empty -> the level's colour
    std::string sound;              // sound-library label, empty -> silent

    /* HOW IT COMBINES, which is a different question from what it says.
     *
     * A case is five placements and its answer is the MEDIAN of them. Taking
     * a median needs an ordering, and free text has none: the instrument
     * cannot know whether "Needs sampling" outranks "Watch". So each row also
     * carries a LEVEL — Low, Suspicious or High — and the median is taken
     * over levels while the label of that median reading is what is shown.
     *
     * This is what keeps the rest of the instrument honest with arbitrary
     * labels: the early stop on an established positive, the median that does
     * not depend on how many readings were taken, and the `tier` column in
     * dataset.csv that a pathology outcome is compared against all read the
     * level. Tier::Invalid here means "not set": the level is then guessed
     * from the words in the label, which is what makes an existing table keep
     * working unchanged. */
    Tier level = Tier::Invalid;

    bool enabled = true;

    bool contains(double z1k, double ips) const {
        return z1k >= zMin && z1k <= zMax && ips >= ipsMin && ips <= ipsMax;
    }
};

/* A rule about the CASE, not about one placement.
 *
 * A lesion is five needle placements and its answer has to be one sentence.
 * The default way of getting there is the median of the readings, which is
 * the right default — it does not depend on how many were taken and one
 * outlier cannot carry it. But it is not the only policy a site may want, and
 * it is not expressible in the per-reading table: "two placements calling for
 * a biopsy means biopsy, even if the other three did not" is a statement
 * about the SET of readings, and no rectangle in the (Z, IPS) plane can say
 * it.
 *
 * So: "if at least `minCount` of this lesion's valid readings said
 * `ofLabel`, the lesion is `label`". An empty `ofLabel` counts every valid
 * reading, which is how "at least three readings at all" is written. Rules
 * are tested top to bottom and the first match wins, exactly as the
 * per-reading table is, so the strictest rule goes first. When none matches
 * the median stands.
 *
 * `level` is what the case COUNTS as — it feeds the early stop and the tier
 * column that a pathology outcome is compared against — and is separate from
 * the words for the same reason it is on a reading rule: free text has no
 * ordering. Unset means "guess from the words", which cannot read a label
 * that is not in English; the editor prints what the guess came to.
 */
struct CaseRule {
    int         minCount = 2;
    std::string ofLabel;            // empty -> any valid reading
    std::string label;
    std::string color;
    std::string sound;
    Tier        level = Tier::Invalid;   // unset -> from the words
    bool        enabled = true;
};

// What a lookup produced: the tier that aggregates, the text and colour that
// are shown, the sound that is played, and which row did it (-1 the default,
// -2 the noisy fallback, -3 no usable numbers).
struct RuleHit {
    Tier        tier = Tier::Invalid;
    std::string label;
    std::string color;
    std::string sound;
    int         row = -1;
    bool        nearest = false;   // in no row: the nearest row answered
};

struct RuleTable {
    bool active = false;                    // false -> the model decides
    std::vector<RuleRow> rows;
    double      noisyThresholdOhm = 5000.0; // above this, and no row matched
    // The two fallbacks are labels in their own right, with their own colour
    // and their own sound, for the same reason the rows are: "nothing matched"
    // is an answer the operator reads, not an internal state.
    std::string defaultLabel      = "Low Risk";
    std::string defaultColor;
    std::string defaultSound;
    std::string noisyLabel        = "Noisy";
    std::string noisyColor;
    std::string noisySound;
    std::string version           = "rules";

    /* How the readings of one lesion combine into one answer, when the
     * median is not the policy the site wants. Empty is the normal state and
     * means the median decides. */
    std::vector<CaseRule> caseRules;

    /* H7-2.14.0: the case result by COUNT, set in Settings -> Rules: the lesion is High Risk once at least
     * `caseHigh` of its valid readings count as high, else Low Risk once at least `caseLow` count as low
     * (0: that one is off). High is tested first. A count reached is the lesion's final answer; before it,
     * the median says what the lesion is so far. When neither is reached by the quota, the case rules and
     * the median decide as before. `caseClose`: once the result is final, the case takes no more readings. */
    int  caseHigh = 0, caseLow = 0;
    bool caseClose = false;

    /* Which case rule these reading labels satisfy, or -1.
     *
     * Takes the labels of the VALID readings only — a refused placement is
     * not evidence of anything and must never be counted towards a case
     * rule, or a noisy needle becomes a way to reach a verdict. */
    int matchCase(const std::vector<std::string>& validLabels) const;

    // First matching ENABLED row wins, exactly as the shipped instrument did.
    // Overlaps are not resolved here; they are reported by the editor.
    int match(double z1k, double ips) const;

    // Everything a reading needs in order to be shown, sounded and combined.
    RuleHit lookup(double z1k, double ips) const;
    // The table's own word for a bad reading: its first enabled row that does
    // not count (a "Noisy" row), or the noisy label when it has none.
    RuleHit noisyRow() const;

    // The tier, the label that produced it and the row index (-1 default,
    // -2 the noisy fallback). A thin wrapper over lookup().
    Tier tierFor(double z1k, double ips, int* row, std::string* label) const;

    // A label is free text, so the mapping to a tier is by keyword and is
    // documented rather than guessed: "high"/"positive"/"malignant" -> High,
    // "low"/"benign"/"negative" -> Low, "noisy"/"invalid"/"no data" -> Invalid,
    // anything else -> Suspicious, because a label nobody recognises must not
    // silently become reassurance.
    static Tier tierOfLabel(const std::string& label);

    // What a tier is worth to the aggregator's median. The aggregator works on
    // probabilities so that one stray reading cannot carry a case; with the
    // rule table there is no probability, so each tier gets a fixed value and
    // the median of those values is the median TIER. The values are spaced
    // around the thresholds in asModel() and mean nothing outside this pair.
    static double pseudoProbability(Tier t);

    // The thresholds pseudoProbability is read back with. Handed to the
    // aggregator in place of the model while the rules are deciding.
    static RiskModel asModel(const std::string& version);
};

class DecisionEngine {
public:
    DecisionEngine();

    void setModel(const RiskModel& m)          { m_model = m; }
    const RiskModel& model() const             { return m_model; }

    // The rule table, and what the aggregator should combine with. When the
    // rules are active scoringModel() returns the placeholder, so the case
    // answer is the median of the per-reading tiers rather than the median of
    // probabilities that were never computed.
    void setRuleTable(const RuleTable& t)      { m_rules = t; }
    const RuleTable& ruleTable() const         { return m_rules; }
    bool rulesDeciding() const                 { return m_rules.active; }
    RiskModel scoringModel() const;
    void setFeatureConfig(const FeatureConfig& c) { m_extractor.setConfig(c); }
    void setQualityConfig(const QualityConfig& c) { m_gate.setConfig(c); }
    void setDecisionConfig(const DecisionConfig& c) { m_cfg = c; }
    void setCalibrator(std::shared_ptr<RangeCalibrator> c) { m_calib = std::move(c); }
    std::shared_ptr<RangeCalibrator> calibrator() const { return m_calib; }

    // One reading. The spectrum is corrected with the learned range offsets
    // before anything else touches it.
    Verdict decide(const Spectrum& raw) const;
    // The features alone, as decide() computes them (range offsets, then extraction).
    Features features(const Spectrum& raw) const;
    // H7-2.1.1: the same, without the Cole fit (the costly part, and not used
    // where only Z and IPS are wanted: the lot tests and their evaluation).
    Features basicFeatures(const Spectrum& raw) const;

    // The firmware's rule, for comparison only.
    Tier legacyTier(const Features& f) const;

private:
    FeatureExtractor m_extractor;
    QualityGate      m_gate;
    RiskModel        m_model;
    RuleTable        m_rules;
    DecisionConfig   m_cfg;
    std::shared_ptr<RangeCalibrator> m_calib;
};

// ---------------------------------------------------------------------------
// Several readings of one lesion -> one answer.
// ---------------------------------------------------------------------------
struct CaseResult {
    Tier    tier        = Tier::Invalid;
    double  probability = kNaN;      // median of the valid readings
    int     nReadings   = 0;
    int     nValid      = 0;
    /* How much the valid readings disagree, in IPS units: the range of them
       after the single reading furthest from the median has been set aside
       (from three readings up). A plain max-min could only ever grow, so one
       bad placement condemned a lesion for the rest of the case. */
    double  spreadIps   = kNaN;
    bool    disagreeing = false;     // valid readings land in different tiers

    // Has the protocol been satisfied? While this is false the tier above is
    // PROVISIONAL: it is the best answer from what has been measured so far,
    // and the operator is still expected to take another reading. The
    // interface must show the difference, because a provisional Low Risk on
    // two readings and a settled Low Risk on five are not the same claim.
    bool    complete    = false;
    bool    stoppedEarly= false;     // finished on a positive before the quota
    bool    byCount     = false;     // H7-2.14.0: the answer is a count reached (RuleTable::caseHigh / caseLow)
    bool    closed      = false;     // H7-2.14.0: final, and the case takes no more readings
    int     nHigh = 0, nLow = 0;     // H7-2.14.0: valid readings that count as high, as low
    int     stillNeeded = 0;         // valid readings still expected
    std::string reason;

    // What to SHOW and what to PLAY for this case: taken from the readings
    // that landed on the case's own level, so the words on screen are words a
    // rule actually produced for this lesion and not a translation of the
    // level back into a vocabulary the operator did not choose.
    std::string label;
    std::string color;
    std::string sound;

    // Which case rule produced the answer, or -1 for the median.
    int caseRuleRow = -1;
};

struct AggregatorConfig {
    // The protocol: five valid readings of a lesion, unless it turns positive
    // first. Five is what the trial data supports — with a within-lesion IPS
    // standard deviation near 2 units, the standard error of the median falls
    // roughly as sqrt(n), so going from 2 readings to 5 cuts the uncertainty
    // in the case-level estimate by about 40%, and it is the point past which
    // each extra needle pass buys much less than it costs the patient.
    int  minValidReadings = 5;

    // Once the case is already positive, more readings cannot change the
    // management decision — the lesion is going to be sampled either way — so
    // continuing to pass a needle through it has cost and no benefit.
    bool earlyStopOnPositive = true;

    // But not on the first reading. The case answer is the MEDIAN of the
    // readings: with one reading the median is that reading, and with two it
    // is their mean, so a single stray measurement carries or half-carries the
    // case. From three onwards a single outlier cannot move the median across
    // a threshold at all. Three is therefore the smallest number at which
    // "it is already positive" is a statement about the lesion rather than
    // about one needle placement.
    int  minForEarlyStop = 3;

    // Repeat readings of one lesion should agree. In the India cohort the
    // median within-lesion IPS spread was 9.9 - almost the entire between-
    // patient range of the other trial - and 32 of 88 cases contained readings
    // on opposite sides of the decision boundary. Above this, ask for more.
    double maxSpreadIps   = 4.0;
};

class Aggregator {
public:
    explicit Aggregator(AggregatorConfig cfg = {}) : m_cfg(cfg) {}
        /* `rules` is optional and carries the CASE rules; without it the median
       decides, which is what every caller did before they existed. */
    CaseResult combine(const std::vector<Verdict>& readings,
                       const RiskModel& model,
                       const RuleTable* rules = nullptr) const;
private:
    AggregatorConfig m_cfg;
};

} // namespace birx
#endif
