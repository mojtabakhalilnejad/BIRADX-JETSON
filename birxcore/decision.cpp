#include "decision.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace birx {

// ---------------------------------------------------------------------------
// The rule table.

namespace {
std::string lowered(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}
} // namespace

Tier RuleTable::tierOfLabel(const std::string& label) {
    const std::string l = lowered(label);
    if (has(l, "high") || has(l, "positive") || has(l, "malign") || has(l, "suspicious for"))
        return Tier::High;
    if (has(l, "noisy") || has(l, "invalid") || has(l, "no data") || has(l, "refus") || has(l, "no rule"))
        return Tier::Invalid;
    if (has(l, "low") || has(l, "benign") || has(l, "negative") || has(l, "normal"))
        return Tier::Low;
    if (has(l, "susp") || has(l, "border") || has(l, "indeterm"))
        return Tier::Suspicious;
    // An unrecognised label is NOT reassurance. It sits in the middle, which
    // is the only honest place for a name nobody here understands.
    return Tier::Suspicious;
}

int RuleTable::matchCase(const std::vector<std::string>& validLabels) const {
    for (size_t i = 0; i < caseRules.size(); ++i) {
        const CaseRule& c = caseRules[i];
        if (!c.enabled) continue;
        if (c.minCount <= 0) continue;          // a rule that matches nothing
        int n = 0;
        for (const std::string& l : validLabels)
            if (c.ofLabel.empty() || l == c.ofLabel) ++n;
        if (n >= c.minCount) return static_cast<int>(i);
    }
    return -1;
}

int RuleTable::match(double z1k, double ips) const {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].enabled) continue;
        if (rows[i].contains(z1k, ips)) return static_cast<int>(i);
    }
    return -1;
}

RuleHit RuleTable::lookup(double z1k, double ips) const {
    RuleHit h;

    // No impedance or no IPS is not a quiet default — it is the one case the
    // table cannot have an opinion about, and it says so.
    if (!std::isfinite(z1k) || !std::isfinite(ips)) {
        // H7-1.5.0: in the table's own word for a bad reading, not a fourth one.
        h = noisyRow();
        h.row   = -3;
        return h;
    }

    const int hit = match(z1k, ips);
    if (hit >= 0) {
        const RuleRow& r = rows[static_cast<size_t>(hit)];
        h.row   = hit;
        h.label = r.label;
        h.color = r.color;
        h.sound = r.sound;
        // An unset level is guessed from the words. That is what lets a table
        // written before levels existed keep behaving exactly as it did.
        h.tier  = (r.level == Tier::Invalid) ? tierOfLabel(r.label) : r.level;
        return h;
    }

    if (std::isfinite(noisyThresholdOhm) && z1k > noisyThresholdOhm) {
        h.row   = -2;
        h.label = noisyLabel;
        h.color = noisyColor;
        h.sound = noisySound;
        h.tier  = tierOfLabel(noisyLabel);
        return h;
    }

    // H7-1.5.0: a reading that falls in no row (a hair-thin seam between two
    // rows, or a value off the ends of the table) takes the row it is NEAREST
    // to, so a table that covers the plane can only ever answer in its own
    // words. Distance is measured with Z in kilohms against IPS units, the two
    // axes of the map. Only a table with no enabled row at all says "No rule".
    {
        int best = -1;
        double bestD = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            const RuleRow& r = rows[i];
            if (!r.enabled || r.zMin > r.zMax || r.ipsMin > r.ipsMax) continue;
            const double zk = z1k / 1000.0;
            const double dz = zk < r.zMin / 1000.0 ? r.zMin / 1000.0 - zk : zk > r.zMax / 1000.0 ? zk - r.zMax / 1000.0 : 0.0;
            const double di = ips < r.ipsMin ? r.ipsMin - ips : ips > r.ipsMax ? ips - r.ipsMax : 0.0;
            const double d = dz * dz + di * di;
            if (best < 0 || d < bestD) { best = static_cast<int>(i); bestD = d; }
        }
        if (best >= 0) {
            const RuleRow& r = rows[static_cast<size_t>(best)];
            h.row   = best;
            h.label = r.label;
            h.color = r.color;
            h.sound = r.sound;
            h.tier  = (r.level == Tier::Invalid) ? tierOfLabel(r.label) : r.level;
            h.nearest = true;
            return h;
        }
    }

    h.row   = -1;
    h.label = defaultLabel;
    h.color = defaultColor;
    h.sound = defaultSound;
    h.tier  = tierOfLabel(defaultLabel);
    return h;
}

RuleHit RuleTable::noisyRow() const {
    RuleHit h;
    for (size_t i = 0; i < rows.size(); ++i) {
        const RuleRow& r = rows[i];
        if (!r.enabled) continue;
        const Tier t = (r.level == Tier::Invalid) ? tierOfLabel(r.label) : r.level;
        if (t != Tier::Invalid) continue;
        h.row = static_cast<int>(i); h.label = r.label; h.color = r.color; h.sound = r.sound;
        h.tier = Tier::Invalid;
        return h;
    }
    h.row = -2;
    h.label = noisyLabel.empty() ? std::string("Noisy") : noisyLabel;
    h.color = noisyColor.empty() ? std::string("#546E7A") : noisyColor;
    h.sound = noisySound;
    h.tier  = Tier::Invalid;
    return h;
}

Tier RuleTable::tierFor(double z1k, double ips, int* row, std::string* label) const {
    const RuleHit h = lookup(z1k, ips);
    if (row)   *row   = h.row;
    if (label) *label = h.label;
    return h.tier;
}

double RuleTable::pseudoProbability(Tier t) {
    switch (t) {
    case Tier::High:       return 0.95;
    case Tier::Suspicious: return 0.50;
    case Tier::Low:        return 0.05;
    case Tier::Invalid:    break;
    }
    return kNaN;      // an Invalid reading is never counted by the aggregator
}

RiskModel RuleTable::asModel(const std::string& version_) {
    RiskModel m;
    m.isRules   = true;
    m.version   = version_.empty() ? std::string("rules") : version_;
    m.needleRev = "any";
    m.endpoint  = "decision table, not a fitted model — the tier comes from the "
                  "instrument's own rules and carries no probability";
    // Placed so that the median of pseudoProbability() reads back as the
    // median tier, and nothing else can land between them.
    m.thresholdLow  = 0.25;
    m.thresholdHigh = 0.75;
    return m;
}

RiskModel DecisionEngine::scoringModel() const {
    return m_rules.active ? RuleTable::asModel(m_rules.version) : m_model;
}

DecisionEngine::DecisionEngine()
    : m_model(RiskModel::abstaining()) {}

Tier DecisionEngine::legacyTier(const Features& f) const {
    if (!std::isfinite(f.z1k) || !std::isfinite(f.ipsLegacy)) return Tier::Invalid;
    if (f.z1k < m_cfg.legacyFatMaxOhm &&
        f.ipsLegacy < m_cfg.legacyIpsMax &&
        f.ipsLegacy > m_cfg.legacyIpsMin)
        return Tier::High;
    return Tier::Low;
}

Features DecisionEngine::features(const Spectrum& raw) const {
    return m_extractor.extract(m_calib ? m_calib->correct(raw) : raw);
}

Features DecisionEngine::basicFeatures(const Spectrum& raw) const {
    FeatureConfig c = m_extractor.config();
    c.fitColeModel = false;
    FeatureExtractor fx;
    fx.setConfig(c);
    return fx.extract(m_calib ? m_calib->correct(raw) : raw);
}

Verdict DecisionEngine::decide(const Spectrum& raw) const {
    Verdict v;

    const Spectrum s = m_calib ? m_calib->correct(raw) : raw;

    v.features = m_extractor.extract(s);
    v.quality  = m_gate.evaluate(s, v.features, m_calib.get());
    v.modelVersion = m_model.version;

    if (m_cfg.compareWithLegacy)
        v.legacyTier = legacyTier(v.features);

    // --- the gate has the last word ----------------------------------------
    if (!v.quality.usable) {
        v.tier   = Tier::Invalid;
        if (m_rules.active) {
            // H7-1.5.0: with the rule table deciding, a refused sweep is shown in
            // the table's own word for a bad reading (its "noisy" row), so the
            // screen only ever says what the table says. Still not counted.
            const RuleHit nz = m_rules.noisyRow();
            v.label  = nz.label;
            v.color  = nz.color;
            v.sound  = nz.sound;
            v.modelVersion = m_rules.version;
            v.reason = nz.label + " \u2014 " + v.quality.operatorMessage();
            return v;
        }
        // The gate's refusal is not a rule's label and must never borrow one:
        // a site that has written "Benign" on a rule must not see that word
        // on a placement the instrument threw away.
        v.label  = tierName(Tier::Invalid);
        v.reason = v.quality.operatorMessage();
        return v;
    }

    // --- the rule table, while the model is provisional ---------------------
    //
    // It runs AFTER the quality gate and never before it: a badly placed
    // needle must not be able to reach a rule box and come out reassured. The
    // rules are read in the instrument's own coordinates — the firmware's IPS,
    // recomputed here from the CALIBRATED sweep — because that is the
    // definition the numbers in the settings file were written in.
    if (m_rules.active) {
        const RuleHit hit = m_rules.lookup(v.features.z1k, v.features.ipsLegacy);
        const int row = hit.row;
        const std::string& label = hit.label;
        const Tier t = hit.tier;
        v.modelVersion = m_rules.version;

        // Carried whatever the tier turns out to be, INCLUDING a refusal:
        // "Noisy" with the site's own colour and its own tone is still the
        // answer the operator is given, and it has to look and sound like the
        // table says it should.
        v.label = hit.label;
        v.color = hit.color;
        v.sound = hit.sound;

        if (t == Tier::Invalid) {
            v.tier   = Tier::Invalid;
            if (row == -3) v.reason = "This sweep did not produce an impedance and an IPS.";
            else if (row == -1) {
                // H7-1.4.0: no row of the table covers this reading. It is not an
                // answer of any kind; whoever wants this range named adds a row.
                std::ostringstream o;
                o << "No rule matched (IPS " << std::fixed << std::setprecision(2) << v.features.ipsLegacy
                  << ", Z " << std::setprecision(0) << v.features.z1k << " Ω). Not counted.";
                v.reason = o.str();
            }
            else v.reason = label + " — the rule table refuses this reading.";
            return v;
        }

        v.tier        = t;
        v.probability = RuleTable::pseudoProbability(t);

        std::ostringstream o;
        o << label << " — ";
        if      (row >= 0 && hit.nearest) o << "nearest rule " << (row + 1);
        else if (row >= 0) o << "rule " << (row + 1);
        else if (row == -2) o << "above the noisy threshold";
        else                o << "no rule matched";
        o << " (IPS " << std::fixed << std::setprecision(2) << v.features.ipsLegacy
          << ", Z " << std::setprecision(0) << v.features.z1k << " Ω)";
        v.reason = o.str();

        if (m_cfg.compareWithLegacy && v.legacyTier != Tier::Invalid) {
            const bool newPositive = (v.tier != Tier::Low);
            const bool oldPositive = (v.legacyTier == Tier::High);
            if (newPositive != oldPositive) v.quality.add(Flag::LegacyDisagrees);
        }
        return v;
    }

    // --- the model must be the right model ---------------------------------
    if (!m_model.valid()) {
        v.tier   = Tier::Invalid;
        v.reason = "No risk model is installed on this instrument.";
        return v;
    }
    if (m_cfg.enforceNeedleRevision &&
        m_model.needleRev != "any" &&
        !s.needleRev.empty() &&
        m_model.needleRev != s.needleRev) {
        v.tier   = Tier::Invalid;
        v.reason = "The installed model is for needle " + m_model.needleRev +
                   "; this reading was taken with " + s.needleRev + ".";
        return v;
    }

    const double p = m_model.probability(v.features);
    if (!std::isfinite(p)) {
        v.tier   = Tier::Invalid;
        v.reason = "The model needs a value this sweep did not produce.";
        return v;
    }

    v.probability = p;
    if      (p >= m_model.thresholdHigh) v.tier = Tier::High;
    else if (p >= m_model.thresholdLow)  v.tier = Tier::Suspicious;
    else                                 v.tier = Tier::Low;

    if (m_cfg.compareWithLegacy && v.legacyTier != Tier::Invalid) {
        const bool newPositive = (v.tier != Tier::Low);
        const bool oldPositive = (v.legacyTier == Tier::High);
        if (newPositive != oldPositive) v.quality.add(Flag::LegacyDisagrees);
    }

    // With a MODEL deciding there is no site-written label: the tier's own
    // name is the label, and the colour is left empty so the interface uses
    // its own. The field is filled in rather than left blank so that every
    // reading has a label and nothing downstream has to special-case which
    // path produced it.
    v.label = tierName(v.tier);

    std::ostringstream o;
    o << tierName(v.tier) << " — probability "
      << std::fixed << static_cast<int>(std::lround(p * 100)) << "%";
    v.reason = o.str();
    return v;
}

// ---------------------------------------------------------------------------

namespace {
double medianOf(std::vector<double> v) {
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n/2] : 0.5 * (v[n/2 - 1] + v[n/2]);
}
} // namespace

CaseResult Aggregator::combine(const std::vector<Verdict>& readings,
                               const RiskModel& model,
                               const RuleTable* rules) const {
    CaseResult r;
    r.nReadings = static_cast<int>(readings.size());

    std::vector<double> probs, ips;
    std::vector<Tier> tiers;
    for (const Verdict& v : readings) {
        if (!v.answered() || !std::isfinite(v.probability)) continue;
        probs.push_back(v.probability);
        tiers.push_back(v.tier);
        if (std::isfinite(v.features.ipsFit)) ips.push_back(v.features.ipsFit);
    }
    r.nValid = static_cast<int>(probs.size());
    r.stillNeeded = std::max(0, m_cfg.minValidReadings - r.nValid);

    if (r.nValid == 0) {
        r.label  = tierName(Tier::Invalid);
        // H7-1.8.0: with the rule table deciding, a lesion with no counted
        // reading is called by the table's word for its last reading ("Noisy"),
        // not by a fourth word; it is still not an answer.
        if (model.isRules && !readings.empty() && !readings.back().label.empty()) {
            r.label = readings.back().label;
            r.color = readings.back().color;
        }
        r.reason = r.nReadings == 0
            ? "No readings yet. Five valid readings are expected for this lesion."
            : "No reading passed its quality checks. Reposition the needle and measure again.";
        return r;
    }

    /* HOW MUCH THE READINGS OF THIS LESION DISAGREE — measured so that it can
     * RECOVER.
     *
     * This was max - min over every valid reading, and that is a one-way
     * ratchet: a range can only grow as readings are added. So a single bad
     * placement — the needle catching a vessel, a partial contact, a twitch —
     * pushed the case over the tolerance and it stayed over it for ever. The
     * instrument then printed "take another reading before accepting this
     * result" after every one of the readings that followed, which was advice
     * it had made impossible to act on: no further reading can lower a
     * max - min. From the operator's side the lesion went Invalid once and was
     * Invalid for the rest of the case however carefully they measured, and
     * that is what was reported.
     *
     * It is now the range of the readings AFTER DISCARDING THE SINGLE ONE
     * FURTHEST FROM THE MEDIAN, once there are three or more. That keeps the
     * units and the meaning of the tolerance — it is still a spread in IPS
     * units, and the number in the settings file still means what it meant —
     * while allowing exactly one outlier, which is the case this check kept
     * getting wrong. Two readings that genuinely disagree still fail it: drop
     * one of a pair of outliers and the other is still there.
     *
     * With two readings nothing is discarded. Two readings that disagree are
     * not one outlier and three good ones; they are two readings that
     * disagree, and the honest answer is that the lesion is not settled.
     */
    if (ips.size() >= 2) {
        std::vector<double> kept = ips;
        if (kept.size() >= 3) {
            const double med = medianOf(kept);
            size_t worst = 0;
            double worstD = -1.0;
            for (size_t i = 0; i < kept.size(); ++i) {
                const double d = std::fabs(kept[i] - med);
                if (d > worstD) { worstD = d; worst = i; }
            }
            kept.erase(kept.begin() + static_cast<long>(worst));
        }
        const auto mm = std::minmax_element(kept.begin(), kept.end());
        r.spreadIps = *mm.second - *mm.first;
    }
    r.disagreeing = std::any_of(tiers.begin(), tiers.end(),
                                [&](Tier t){ return t != tiers.front(); });

    // Median of the per-reading probabilities: independent of how many
    // readings were taken, and unmoved by one outlier.
    // With the rule table deciding, the probabilities are stand-ins for
    // levels, and the mean of two of them is a level no row produced (High and
    // Low averaged to "Suspicious"). So with an even count the UPPER middle
    // value is taken: a tie between High and Low is High, never a third word.
    if (model.isRules) {
        std::vector<double> sp = probs;
        std::sort(sp.begin(), sp.end());
        r.probability = sp[sp.size() / 2];
    } else {
        r.probability = medianOf(probs);
    }

    if      (r.probability >= model.thresholdHigh) r.tier = Tier::High;
    else if (r.probability >= model.thresholdLow)  r.tier = Tier::Suspicious;
    else                                           r.tier = Tier::Low;

    /* THE WORDS FOR THE CASE.
     *
     * The tier above is a median over levels; the label has to be words a
     * rule actually produced for THIS lesion, so it is taken from the valid
     * readings that landed on the case's own level — the commonest of them,
     * with the first one breaking a tie.
     *
     * It is not translated back from the tier. On a table whose High rows say
     * "Needs core biopsy" and "Repeat under ultrasound", the case must say
     * one of those two and never the word "High Risk", which appears nowhere
     * in the site's own vocabulary. If no reading at the case's level carried
     * a label at all, the tier's name is the honest fallback. */
    {
        std::vector<const Verdict*> atLevel;
        for (const Verdict& v : readings)
            if (v.answered() && std::isfinite(v.probability) && v.tier == r.tier)
                atLevel.push_back(&v);

        const Verdict* chosen = nullptr;
        size_t best = 0;
        for (const Verdict* a : atLevel) {
            if (a->label.empty()) continue;
            size_t n = 0;
            for (const Verdict* b : atLevel) if (b->label == a->label) ++n;
            if (n > best) { best = n; chosen = a; }
        }
        if (chosen) {
            r.label = chosen->label;
            r.color = chosen->color;
            r.sound = chosen->sound;
        } else {
            r.label = tierName(r.tier);
        }
    }

    /* THE CASE RULES, which are about the SET of readings.
     *
     * Applied here — after the median has produced an answer and BEFORE the
     * disagreement check and the completion logic — because a case rule
     * changes what the lesion IS, and both of those read it. A site whose
     * rule says two biopsy-grade placements are enough expects the early stop
     * to see a positive, not to go on asking for five.
     *
     * Only the VALID readings are counted. A refused placement is not
     * evidence of anything, and letting one count towards "at least two"
     * would make a noisy needle into a route to a verdict.
     */
    /* H7-2.14.0: THE CASE RESULT BY COUNT (Settings -> Rules). High first: at least caseHigh readings that count
     * as high make the lesion High Risk, else at least caseLow that count as low make it Low Risk. The words are
     * the commonest a reading of that level carried, as above. */
    bool counted = false;
    for (const Verdict& v : readings) {
        if (!v.answered() || !std::isfinite(v.probability)) continue;
        if (v.tier == Tier::High) ++r.nHigh;
        else if (v.tier == Tier::Low) ++r.nLow;
    }
    if (rules && (rules->caseHigh > 0 || rules->caseLow > 0)) {
        Tier t = Tier::Invalid;
        if (rules->caseHigh > 0 && r.nHigh >= rules->caseHigh) t = Tier::High;
        else if (rules->caseLow > 0 && r.nLow >= rules->caseLow) t = Tier::Low;
        if (t != Tier::Invalid) {
            counted = true;
            r.byCount = true;
            r.tier = t;
            r.probability = t == Tier::High ? 1.0 : 0.0;
            const Verdict* chosen = nullptr;
            size_t best = 0;
            for (const Verdict& a2 : readings) {
                if (!a2.answered() || a2.tier != t || a2.label.empty()) continue;
                size_t n = 0;
                for (const Verdict& b : readings) if (b.answered() && b.tier == t && b.label == a2.label) ++n;
                if (n > best) { best = n; chosen = &a2; }
            }
            if (chosen) { r.label = chosen->label; r.color = chosen->color; r.sound = chosen->sound; }
            else { r.label = tierName(t); r.color.clear(); r.sound.clear(); }
        }
    }

    if (!counted && rules && !rules->caseRules.empty()) {
        std::vector<std::string> labels;
        for (const Verdict& v : readings)
            if (v.answered() && std::isfinite(v.probability)) labels.push_back(v.label);

        const int hit = rules->matchCase(labels);
        if (hit >= 0) {
            const CaseRule& c = rules->caseRules[static_cast<size_t>(hit)];
            r.caseRuleRow = hit;
            r.label = c.label;
            r.color = c.color;
            r.sound = c.sound;
            r.tier  = (c.level == Tier::Invalid) ? RuleTable::tierOfLabel(c.label)
                                                 : c.level;
        }
    }

    // Disagreement beyond the tolerance is not an answer at any count, and it
    // also blocks the early stop: readings that contradict each other cannot
    // establish a positive.
    // H7-1.5.0: NOT with the rule table deciding. There each reading is put in
    // a box by its own IPS and Z, and readings in different boxes are exactly
    // what the table expects to see; turning the lesion Invalid for it made a
    // case go Invalid after two differing readings and STAY Invalid while the
    // map showed red and green points (reported from the bench). The spread
    // is still measured and kept with the case.
    if (!counted && !model.isRules && std::isfinite(r.spreadIps) && r.spreadIps > m_cfg.maxSpreadIps) {
        std::ostringstream o;
        o << "The readings of this lesion disagree too much (IPS spread "
          << std::fixed << std::setprecision(1) << r.spreadIps
          << " after setting aside the one furthest from the middle). Take "
             "another reading \u2014 one odd placement is already allowed for, "
             "so this is two or more that do not agree.";
        r.tier        = Tier::Invalid;
        r.label       = tierName(Tier::Invalid);
        r.color.clear();
        r.sound.clear();
        r.complete    = false;
        r.stillNeeded = std::max(1, r.stillNeeded);
        r.reason      = o.str();
        return r;
    }

    // Completion: the quota, or an established positive before it.
    // H7-2.14.0: or a count reached (Settings -> Rules): that is the lesion's final answer.
    if (counted) {
        r.complete     = true;
        r.stoppedEarly = r.nValid < m_cfg.minValidReadings;
        r.stillNeeded  = 0;
    } else if (r.nValid >= m_cfg.minValidReadings) {
        r.complete    = true;
        r.stillNeeded = 0;
    } else if (m_cfg.earlyStopOnPositive && r.tier == Tier::High &&
               r.nValid >= m_cfg.minForEarlyStop) {
        r.complete     = true;
        r.stoppedEarly = true;
        r.stillNeeded  = 0;
    }

    // With the rule table deciding there is no probability \u2014 the number the
    // median ran on is a placeholder for a tier \u2014 so nothing is allowed to
    // print it as a percentage.
    const bool showPercent = !model.isRules;

    // The sentence says the site's own words, not the level's name: the tier
    // is machinery and the label is what this instrument was told to report.
    // H7-2.14.0: once final, the case may take no more readings
    r.closed = r.complete && rules && rules->caseClose;

    std::ostringstream o;
    if (counted) {
        const int k = r.tier == Tier::High ? rules->caseHigh : rules->caseLow;
        o << r.label << " \u2014 " << (r.tier == Tier::High ? r.nHigh : r.nLow) << " of " << r.nValid << " readings "
          << (r.tier == Tier::High ? "high" : "low") << " (the case result: " << (r.tier == Tier::High ? "high" : "low")
          << " from " << k << ")";
        if (r.closed) o << ". The case is closed: no more readings.";
    } else if (r.caseRuleRow >= 0) {
        // Say which rule, for the same reason a reading says which rule: an
        // answer whose origin cannot be read cannot be checked.
        o << r.label << " \u2014 case rule " << (r.caseRuleRow + 1)
          << " on " << r.nValid << " of " << r.nReadings << " readings";
        if (!r.complete)
            o << ". " << r.stillNeeded << " more expected.";
    } else if (r.complete) {
        o << r.label << " from " << r.nValid << " of " << r.nReadings
          << " readings";
        if (showPercent)
            o << " \u2014 probability "
              << static_cast<int>(std::lround(r.probability * 100)) << "%";
        else
            o << " \u2014 from the rule table";
        if (r.stoppedEarly)
            o << ". Positive on " << r.nValid
              << " readings; no further passes are needed.";
        if (r.disagreeing) o << " (readings not unanimous)";
    } else {
        o << "Provisional " << r.label << " \u2014 ";
        if (showPercent)
            o << static_cast<int>(std::lround(r.probability * 100)) << "% from ";
        o << r.nValid << " valid reading" << (r.nValid == 1 ? "" : "s") << ". "
          << r.stillNeeded << " more needed.";
        if (rules && (rules->caseHigh > 0 || rules->caseLow > 0)) {   // H7-2.14.0: how far each count has come
            o << " So far " << r.nHigh << " high, " << r.nLow << " low (";
            if (rules->caseHigh > 0) o << "high from " << rules->caseHigh;
            if (rules->caseHigh > 0 && rules->caseLow > 0) o << ", ";
            if (rules->caseLow > 0) o << "low from " << rules->caseLow;
            o << ").";
        }
    }
    if (r.closed && !counted) o << " The case is closed: no more readings.";
    r.reason = o.str();
    return r;
}

} // namespace birx
