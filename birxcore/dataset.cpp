#include "dataset.h"
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace birx {

const char* pathologyName(Pathology p) {
    switch (p) {
    case Pathology::Benign:     return "benign";
    case Pathology::Borderline: return "borderline";
    case Pathology::Malignant:  return "malignant";
    default:                    return "unknown";
    }
}

Pathology pathologyFromName(const std::string& s) {
    std::string t;
    for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (t == "benign"     || t == "0" || t == "-")   return Pathology::Benign;
    if (t == "borderline" || t == "1" || t == "sus") return Pathology::Borderline;
    if (t == "malignant"  || t == "2" || t == "+")   return Pathology::Malignant;
    return Pathology::Unknown;
}

namespace {

std::string csvQuote(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}

std::string num(double v, int prec = 6) {
    if (!std::isfinite(v)) return "";
    std::ostringstream o;
    o << std::fixed << std::setprecision(prec) << v;
    return o.str();
}

std::vector<std::string> csvSplit(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (q) {
            if (c == '"') { if (i + 1 < line.size() && line[i+1] == '"') { cur += '"'; ++i; } else q = false; }
            else cur += c;
        } else if (c == '"') q = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool toDouble(const std::string& s, double* v) {
    if (s.empty()) return false;
    // MCU port: std::stod + try/catch replaced by strtod with the same
    // acceptance rule (no conversion or ERANGE -> refused), so the core builds
    // with -fno-exceptions. See BIRADx_PORTING_SPEC.md §18.3.
    double d = 0; if (!birxStrtod(s, &d)) return false; *v = d; return std::isfinite(d);
}

} // namespace

// ---------------------------------------------------------------------------

std::string DatasetWriter::header() {
    std::string h = "case_id,site,epoch_ms,needle,operator";
    for (const std::string& n : Features::names()) h += "," + n;
    h += ",usable,flags,tier,probability,legacy_tier,model_version";
    // Patient covariates ride in the same row, so the trainer never has to
    // join two files to fit a model that uses age or to split by BI-RADS.
    for (const IntakeField& f : intakeSchema())
        if (f.id != "national_id" && f.id != "notes") h += ",pt_" + f.id;
    /* WHAT THE OPERATOR WAS SHOWN, recorded next to what the machinery
     * decided.
     *
     * `tier` above is Low/Suspicious/High/Invalid — the level the aggregation
     * is computed in. It is NOT what was on the screen: with the rule table
     * deciding, the screen says whatever the matching rule was written to
     * say, in the site's own words and colour. Recording only the tier means
     * the log, the export and any later audit can all report a word the
     * operator never saw, and there is no way afterwards to find out what
     * they did see.
     *
     * Appended at the END of the row so that a file written by an earlier
     * version still reads correctly: every consumer of this file looks its
     * columns up by name. */
    h += ",label,color";
    return h;
}

bool DatasetWriter::append(const Spectrum& s, const Verdict& v,
                           const PatientRecord* patient) {
    const bool fresh = !std::ifstream(m_path).good();
    std::ofstream f(m_path, std::ios::app);
    if (!f) return false;
    if (fresh) f << header() << "\n";

    f << csvQuote(s.caseId) << ',' << s.siteIndex << ',' << s.epochMs << ','
      << csvQuote(s.needleRev) << ','
      << csvQuote(patient ? patient->operatorId : std::string());
    for (const std::string& n : Features::names()) f << ',' << num(v.features.byName(n));
    f << ',' << (v.quality.usable ? 1 : 0)
      << ',' << csvQuote(v.quality.summary())
      << ',' << tierName(v.tier)
      << ',' << num(v.probability)
      << ',' << tierName(v.legacyTier)
      << ',' << csvQuote(v.modelVersion);
    for (const IntakeField& fl : intakeSchema()) {
        if (fl.id == "national_id" || fl.id == "notes") continue;
        f << ',' << csvQuote(patient ? patient->get(fl.id) : std::string());
    }
    f << ',' << csvQuote(v.label.empty() ? std::string(tierName(v.tier)) : v.label)
      << ',' << csvQuote(v.color);
    f << '\n';
    return f.good();
}

bool DatasetWriter::appendSweep(const std::string& dir, const Spectrum& s) {
    std::ostringstream name;
    name << dir << "/" << (s.caseId.empty() ? "case" : s.caseId)
         << "_" << s.siteIndex << "_" << s.epochMs << ".csv";
    std::ofstream f(name.str());
    if (!f) return false;
    f << "freq_hz,mag_ohm,phase_deg,range\n";
    for (const Point& p : s.points)
        f << num(p.freqHz, 1) << ',' << num(p.magOhm, 3) << ','
          << num(p.phaseDeg, 3) << ',' << p.range << '\n';
    return f.good();
}

// ---------------------------------------------------------------------------

bool OutcomeStore::record(const Outcome& o, std::string* why) {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if (o.caseId.empty()) return fail("no case was chosen");
    if (o.pathology == Pathology::Unknown) return fail("no result was chosen");
    const bool fresh = !std::ifstream(m_path).good();
    errno = 0;
    std::ofstream f(m_path, std::ios::app);
    if (!f) return fail("cannot open " + m_path + " for writing: " +
                        (errno ? std::string(std::strerror(errno)) : std::string("unknown error")));
    if (fresh) f << "case_id,pathology,report_date,source,note,recorded_ms\n";
    f << csvQuote(o.caseId) << ',' << pathologyName(o.pathology) << ','
      << csvQuote(o.reportDate) << ',' << csvQuote(o.source) << ','
      << csvQuote(o.note) << ',' << o.recordedMs << '\n';
    f.flush();
    if (!f.good()) return fail("writing to " + m_path + " failed (is the disk full?)");
    m_byCase[o.caseId] = o;
    if (why) why->clear();
    return true;
}

bool OutcomeStore::load() {
    m_byCase.clear();
    std::ifstream f(m_path);
    if (!f) return false;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (first) { first = false; if (line.rfind("case_id", 0) == 0) continue; }
        const std::vector<std::string> c = csvSplit(line);
        if (c.size() < 2) continue;
        Outcome o;
        o.caseId    = c[0];
        o.pathology = pathologyFromName(c[1]);
        if (c.size() > 2) o.reportDate = c[2];
        if (c.size() > 3) o.source     = c[3];
        if (c.size() > 4) o.note       = c[4];
        if (c.size() > 5) { double d = 0; if (toDouble(c[5], &d)) o.recordedMs = static_cast<int64_t>(d); }
        if (!o.caseId.empty()) m_byCase[o.caseId] = o;   // later line wins
    }
    return true;
}

Pathology OutcomeStore::lookup(const std::string& caseId) const {
    auto it = m_byCase.find(caseId);
    return it == m_byCase.end() ? Pathology::Unknown : it->second.pathology;
}

std::vector<std::string> OutcomeStore::pending(const std::string& datasetCsv,
                                               const OutcomeStore& outcomes) {
    std::vector<std::string> out;
    std::ifstream f(datasetCsv);
    if (!f) return out;
    std::string line;
    bool first = true;
    std::vector<std::string> seen;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        const std::vector<std::string> c = csvSplit(line);
        if (c.empty() || c[0].empty()) continue;
        if (std::find(seen.begin(), seen.end(), c[0]) != seen.end()) continue;
        seen.push_back(c[0]);
        if (outcomes.lookup(c[0]) == Pathology::Unknown) out.push_back(c[0]);
    }
    return out;
}

// ---------------------------------------------------------------------------

double TrainingRow::covariate(const std::string& id) const {
    auto it = covariates.find(id);
    if (it == covariates.end() || it->second.empty()) return kNaN;
    double d = 0;
    return toDouble(it->second, &d) ? d : kNaN;
}

std::vector<TrainingRow> buildTrainingTable(const std::string& datasetCsv,
                                            const OutcomeStore& outcomes,
                                            std::string* error) {
    std::vector<TrainingRow> rows;
    std::ifstream f(datasetCsv);
    if (!f) { if (error) *error = "cannot open " + datasetCsv; return rows; }

    std::string line;
    if (!std::getline(f, line)) { if (error) *error = "empty dataset"; return rows; }
    const std::vector<std::string> head = csvSplit(line);

    auto col = [&](const std::string& name) -> int {
        for (size_t i = 0; i < head.size(); ++i) if (head[i] == name) return static_cast<int>(i);
        return -1;
    };
    const int cCase = col("case_id"), cSite = col("site"), cNeedle = col("needle"),
              cUsable = col("usable"), cEpoch = col("epoch_ms"), cOp = col("operator");
    if (cCase < 0) { if (error) *error = "dataset has no case_id column"; return rows; }

    const std::vector<std::string> fnames = Features::names();
    std::vector<int> fcol;
    for (const std::string& n : fnames) fcol.push_back(col(n));

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> c = csvSplit(line);
        if (static_cast<int>(c.size()) <= cCase) continue;

        TrainingRow r;
        r.caseId = c[cCase];
        if (cSite   >= 0 && cSite   < (int)c.size()) { double d = 0; if (toDouble(c[cSite], &d)) r.siteIndex = (int)d; }
        if (cNeedle >= 0 && cNeedle < (int)c.size()) r.needleRev = c[cNeedle];
        if (cUsable >= 0 && cUsable < (int)c.size()) { double d = 0; r.usable = toDouble(c[cUsable], &d) && d != 0; }
        if (cEpoch  >= 0 && cEpoch  < (int)c.size()) { double d = 0; if (toDouble(c[cEpoch], &d)) r.epochMs = (int64_t)d; }
        if (cOp     >= 0 && cOp     < (int)c.size()) r.operatorId = c[cOp];

        // Rebuild the Features struct by name, so a column added later does
        // not shift anything and a column removed simply stays NaN.
        for (size_t i = 0; i < fnames.size(); ++i) {
            const int ci = fcol[i];
            if (ci < 0 || ci >= (int)c.size()) continue;
            double d = 0;
            if (!toDouble(c[ci], &d)) continue;
            const std::string& n = fnames[i];
            Features& F = r.features;
            if      (n == "z1k")            F.z1k = d;
            else if (n == "logZ1k")         F.logZ1k = d;
            else if (n == "ipsLegacy")      F.ipsLegacy = d;
            else if (n == "ipsFit")         F.ipsFit = d;
            else if (n == "ipsTheilSen")    F.ipsTheilSen = d;
            else if (n == "fitRmsDeg")      F.fitRmsDeg = d;
            else if (n == "curvature")      F.curvature = d;
            else if (n == "phaseAtRefDeg")  F.phaseAtRefDeg = d;
            else if (n == "phaseMinDeg")    F.phaseMinDeg = d;
            else if (n == "logFAtPhaseMin") F.logFAtPhaseMin = d;
            else if (n == "zRatio")         F.zRatio = d;
            else if (n == "zRatioHigh")     F.zRatioHigh = d;
            else if (n == "nFit")           F.nFit = (int)d;
            else if (n == "rangeAt500k")    F.rangeAt500k = (int)d;
            else if (n == "rangeAt1k")      F.rangeAt1k = (int)d;
            else if (n == "rangeSwitches")  F.rangeSwitches = (int)d;
            else if (n == "coleR0")         F.coleR0 = d;
            else if (n == "coleRinf")       F.coleRinf = d;
            else if (n == "coleRatio")      F.coleRatio = d;
            else if (n == "coleLogFc")      F.coleLogFc = d;
            else if (n == "coleAlpha")      F.coleAlpha = d;
            else if (n == "coleRmsRel")     F.coleRmsRel = d;
        }

        for (const IntakeField& fl : intakeSchema()) {
            const int ci = col("pt_" + fl.id);
            if (ci >= 0 && ci < (int)c.size() && !c[ci].empty())
                r.covariates[fl.id] = c[ci];
        }
        r.pathology = outcomes.lookup(r.caseId);
        if (r.usable && r.pathology != Pathology::Unknown) rows.push_back(r);
    }
    return rows;
}

} // namespace birx
