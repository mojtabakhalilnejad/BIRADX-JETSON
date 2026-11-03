#include <functional>
// app/app_log.cpp — the log, pathology, case-detail corrections, export and
// the diagnostics file (BirxBridge::caseLog & co, Controller::writeDiagnostics).
//
// One deliberate change from 2.18.0 (spec §20.1): the log's answer for a case
// is computed by the SAME Aggregator::combine() the measure screen uses, from
// the recorded tier, probability, IPS fit, label and colour of each reading.
// 2.18.0 used its own median-of-ranks (lower middle), counted readings by
// `usable`, broke label ties alphabetically and skipped the spread check, so
// the log could disagree with what the operator had been shown.
#include "app.h"
#include "version.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

using namespace birx;

namespace app {

namespace {
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...)
{
    char b[640];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

std::vector<std::string> csvSplitLine(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (q) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += ch; ++i; }
                else q = false;
            } else cur += ch;
        } else if (ch == '"') q = true;
        else if (ch == ',') { out.push_back(cur); cur.clear(); }
        else if (ch != '\r') cur += ch;
    }
    out.push_back(cur);
    return out;
}

std::string csvCell(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o.push_back(c); }
    return o + "\"";
}

double toNum(const std::string& s)
{
    if (s.empty()) return kNaN;
    double d = 0;
    return birxStrtod(s, &d) ? d : kNaN;
}

Tier tierFromName(const std::string& t)
{
    if (t == "High Risk")  return Tier::High;
    if (t == "Suspicious") return Tier::Suspicious;
    if (t == "Low Risk")   return Tier::Low;
    return Tier::Invalid;
}

int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

// Reads a text file line by line without holding it all in memory.
template <class F> bool eachLine(const std::string& path, F f)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!f(line)) break;
    }
    return true;
}
} // namespace

// ============================================================================
// the index of dataset.csv. Built once at start and whenever the file on the
// card has a different size from what was indexed; each new reading is added
// as it is written, so the log follows the records (spec §13.6, §16.11).

void App::indexDataset()
{
    m_logRows.clear();
    m_byCase.clear();
    const std::string path = m_records + "/dataset.csv";
    if (!plat::sdReady()) { m_indexedSize = -1; return; }
    std::vector<std::string> head;
    int cCase = -1, cSite = -1, cEpoch = -1, cUsable = -1, cTier = -1, cFlags = -1,
        cZ = -1, cIpsL = -1, cIpsF = -1, cProb = -1, cLabel = -1, cColor = -1,
        cAge = -1, cBirads = -1, cSize = -1;
    bool first = true;
    eachLine(path, [&](const std::string& line) {
        if (line.empty()) return true;
        std::vector<std::string> c = csvSplitLine(line);
        if (first) {
            first = false;
            head = c;
            auto col = [&](const char* n) {
                for (size_t i = 0; i < head.size(); ++i) if (head[i] == n) return int(i);
                return -1;
            };
            cCase = col("case_id"); cSite = col("site"); cEpoch = col("epoch_ms");
            cUsable = col("usable"); cTier = col("tier"); cFlags = col("flags");
            cZ = col("z1k"); cIpsL = col("ipsLegacy"); cIpsF = col("ipsFit");
            cProb = col("probability"); cLabel = col("label"); cColor = col("color");
            cAge = col("pt_age"); cBirads = col("pt_birads"); cSize = col("pt_lesion_size");
            return true;
        }
        if (cCase < 0 || int(c.size()) <= cCase || c[size_t(cCase)].empty()) return true;
        auto at = [&](int i) -> std::string { return (i >= 0 && i < int(c.size())) ? c[size_t(i)] : std::string(); };
        LogRow r;
        r.caseId = at(cCase);
        r.site = int(toNum(at(cSite)));
        { const double e = toNum(at(cEpoch)); r.epochMs = std::isfinite(e) ? int64_t(e) : 0; }
        r.usable = at(cUsable) == "1";
        r.tier = at(cTier);
        r.flags = at(cFlags);
        r.z1k = toNum(at(cZ)); r.ipsLegacy = toNum(at(cIpsL)); r.ipsFit = toNum(at(cIpsF));
        r.probability = toNum(at(cProb));
        r.label = at(cLabel).empty() ? r.tier : at(cLabel);
        r.color = at(cColor);
        r.age = at(cAge); r.birads = at(cBirads); r.size = at(cSize);
        m_byCase[r.caseId].push_back(m_logRows.size());
        m_logRows.push_back(std::move(r));
        return true;
    });
    m_indexedSize = plat::fileSize(path);
}

void App::addLogRow(const Spectrum& s, const Verdict& v)
{
    LogRow r;
    r.caseId = s.caseId;
    r.site = s.siteIndex;
    r.epochMs = s.epochMs;
    r.usable = v.quality.usable;
    r.tier = tierName(v.tier);
    r.flags = v.quality.summary();
    r.z1k = v.features.z1k; r.ipsLegacy = v.features.ipsLegacy; r.ipsFit = v.features.ipsFit;
    r.probability = v.probability;
    r.label = v.label.empty() ? std::string(tierName(v.tier)) : v.label;
    r.color = v.color;
    r.age = m_patient.get("age"); r.birads = m_patient.get("birads");
    r.size = m_patient.get("lesion_size");
    m_byCase[r.caseId].push_back(m_logRows.size());
    m_logRows.push_back(std::move(r));
    m_indexedSize = plat::fileSize(m_records + "/dataset.csv");
}

void App::refreshLog()
{
    if (!plat::sdReady()) return;
    const long sz = plat::fileSize(m_records + "/dataset.csv");
    if (sz != m_indexedSize) indexDataset();
    m_patientsSize = -2;
    indexPatients();
    if (m_outcomes) m_outcomes->load();
    touch();
}

// The latest patients.csv row per case, merged field by field (2.18.0
// latestPatientRows): a later edit that filled one field must not blank the rest.
void App::indexPatients() const
{
    const std::string path = m_records + "/patients.csv";
    const long sz = plat::sdReady() ? plat::fileSize(path) : -1;
    if (sz == m_patientsSize) return;
    m_patientsSize = sz;
    m_patientRows.clear();
    m_patientCols.clear();
    bool first = true;
    int cCase = -1;
    eachLine(path, [&](const std::string& line) {
        if (line.empty()) return true;
        std::vector<std::string> c = csvSplitLine(line);
        if (first) {
            first = false;
            m_patientCols = c;
            for (size_t i = 0; i < c.size(); ++i) if (c[i] == "case_id") cCase = int(i);
            return true;
        }
        if (cCase < 0 || int(c.size()) <= cCase || c[size_t(cCase)].empty()) return true;
        std::map<std::string, std::string>& dst = m_patientRows[c[size_t(cCase)]];
        for (size_t i = 0; i < m_patientCols.size() && i < c.size(); ++i)
            if (!c[i].empty() || !dst.count(m_patientCols[i])) dst[m_patientCols[i]] = c[i];
        // A correction appended to a file whose header predates the edited_at
        // column carries it as one extra, unnamed cell (2.18.0 keeps the old
        // header on purpose). Read it as what it is, or the EDITED badge can
        // never appear for a case that was committed on this instrument.
        if (c.size() == m_patientCols.size() + 1 &&
            std::find(m_patientCols.begin(), m_patientCols.end(), "edited_at") == m_patientCols.end() &&
            !c.back().empty())
            dst["edited_at"] = c.back();
        return true;
    });
}

int App::logCount() const
{
    return int(m_byCase.size());
}

LogCase App::summarise(const std::string& id, const std::vector<size_t>& rows) const
{
    LogCase lc;
    lc.caseId = id;
    lc.test = id.compare(0, 5, "TEST-") == 0;
    lc.target = m_aggCfg.minValidReadings;
    std::vector<Verdict> vs;
    for (size_t k : rows) {
        const LogRow& r = m_logRows[k];
        lc.epochMs = std::max(lc.epochMs, r.epochMs);
        ++lc.total;
        Verdict v;
        v.tier = tierFromName(r.tier);
        v.probability = r.probability;
        v.features.ipsFit = r.ipsFit;
        v.features.ipsLegacy = r.ipsLegacy;
        v.features.z1k = r.z1k;
        v.label = r.label;
        v.color = r.color;
        vs.push_back(v);
        if (v.tier == Tier::High && std::isfinite(v.probability)) ++lc.nHigh;
        if (lc.detail.empty()) {
            std::vector<std::string> bits;
            if (!r.age.empty()) bits.push_back(r.age + " y");
            if (!r.birads.empty()) bits.push_back(r.birads);
            if (!r.size.empty()) bits.push_back(r.size + " mm");
            for (size_t i = 0; i < bits.size(); ++i) lc.detail += (i ? " \xC2\xB7 " : "") + bits[i];
        }
    }
    auto pit = m_patientRows.find(id);
    if (pit != m_patientRows.end()) {
        const auto& det = pit->second;
        auto val = [&](const char* k) { auto i = det.find(k); return i == det.end() ? std::string() : i->second; };
        std::vector<std::string> bits;
        if (!val("age").empty()) bits.push_back(val("age") + " y");
        if (!val("birads").empty()) bits.push_back(val("birads"));
        if (!val("lesion_size").empty()) bits.push_back(val("lesion_size") + " mm");
        if (!bits.empty()) {
            lc.detail.clear();
            for (size_t i = 0; i < bits.size(); ++i) lc.detail += (i ? " \xC2\xB7 " : "") + bits[i];
        }
        lc.edited = !val("edited_at").empty();
    }

    const CaseResult cr = Aggregator(m_aggCfg).combine(vs, m_engine.scoringModel(),
                                                       &m_engine.ruleTable());
    lc.tier = tierName(cr.tier);
    lc.label = cr.label.empty() ? lc.tier : cr.label;
    lc.color = cr.color;
    lc.readings = cr.nValid;
    lc.complete = cr.complete;
    lc.stoppedEarly = cr.stoppedEarly;
    lc.caseRule = cr.caseRuleRow + 1;
    if (cr.nValid == 0) lc.summary = "no reading passed its checks";
    else {
        lc.summary = fmt("%d of %d High \xC2\xB7 %d of %d readings", lc.nHigh, cr.nValid,
                         cr.nValid, lc.target);
        if (lc.caseRule > 0) lc.summary = fmt("case rule %d \xC2\xB7 ", lc.caseRule) + lc.summary;
        if (cr.tier == Tier::Invalid && std::isfinite(cr.spreadIps))
            lc.summary += fmt(" \xC2\xB7 IPS spread %.1f", cr.spreadIps);
        else if (!cr.complete) lc.summary += " \xC2\xB7 provisional";
        else if (cr.stoppedEarly) lc.summary += " \xC2\xB7 stopped early";
    }
    const Pathology p = m_outcomes ? m_outcomes->lookup(id) : Pathology::Unknown;
    lc.pathology = p == Pathology::Unknown ? std::string() : std::string(pathologyName(p));

    const plat::Clock n = nowLocal();
    const int64_t today = daysFromCivil(n.year, unsigned(n.month), unsigned(n.day));
    const std::string day = localText(lc.epochMs, "%Y-%m-%d");
    int y = 0, m = 0, d = 0;
    std::sscanf(day.c_str(), "%d-%d-%d", &y, &m, &d);
    const int64_t then = daysFromCivil(y, unsigned(m), unsigned(d));
    const int64_t ago = today - then;
    lc.whenText = (ago == 0 ? std::string("today") : ago == 1 ? std::string("yesterday")
                                                             : fmt("%d days ago", int(ago)))
                  + " " + localText(lc.epochMs, "%H:%M");
    return lc;
}

std::vector<LogCase> App::caseLog(const std::string& when, const std::string& tag,
                                  int* hiddenByWindow) const
{
    indexPatients();
    // windowStartMs / windowEndMs, in local days
    const plat::Clock n = nowLocal();
    const int64_t today = daysFromCivil(n.year, unsigned(n.month), unsigned(n.day));
    auto dayStartMs = [&](int64_t day) {
        return (day * 86400 - int64_t(m_s.utcOffsetMin) * 60) * 1000;
    };
    int64_t from = 0, to = std::numeric_limits<int64_t>::max();
    if      (when == "today") from = dayStartMs(today);
    else if (when == "yest")  { from = dayStartMs(today - 1); to = dayStartMs(today); }
    else if (when == "d3")    from = dayStartMs(today - 2);
    else if (when == "d7")    from = dayStartMs(today - 6);
    else if (when == "m1")    from = dayStartMs(today - 30);
    else if (when == "y1")    from = dayStartMs(today - 365);

    std::vector<LogCase> keep;
    int hidden = 0;
    for (const auto& kv : m_byCase) {
        LogCase lc = summarise(kv.first, kv.second);
        if (tag != "all") {
            const bool byTier = lc.tier == tag || lc.label == tag;
            const bool byPath = lc.pathology == tag || (tag == "awaiting" && lc.pathology.empty());
            if (!byTier && !byPath) continue;
        }
        if (!(lc.epochMs >= from && lc.epochMs < to)) { ++hidden; continue; }
        keep.push_back(std::move(lc));
    }
    std::sort(keep.begin(), keep.end(),
              [](const LogCase& a, const LogCase& b) { return a.epochMs > b.epochMs; });
    if (hiddenByWindow) *hiddenByWindow = hidden;
    return keep;
}

std::vector<LogReading> App::caseReadingsLog(const std::string& caseId) const
{
    std::vector<LogReading> out;
    auto it = m_byCase.find(caseId);
    if (it == m_byCase.end()) return out;
    for (size_t k : it->second) {
        const LogRow& r = m_logRows[k];
        LogReading x;
        x.site = r.site; x.usable = r.usable; x.tier = r.tier; x.label = r.label;
        x.color = r.color; x.flags = r.flags; x.z1k = r.z1k; x.ipsLegacy = r.ipsLegacy;
        x.ipsFit = r.ipsFit;
        x.when = r.epochMs ? localText(r.epochMs, "%H:%M:%S") : std::string();
        x.epochMs = r.epochMs;
        out.push_back(x);
    }
    return out;
}

// The 22 quantities of one recorded reading, read back from dataset.csv —
// every one of them is in its row. The row is found by case and time (a site
// number can repeat under a TEST- id after a restart; the time cannot).
bool App::readingFeatures(const std::string& caseId, int64_t epochMs, Features* out, std::string* flags) const
{
    if (!plat::sdReady()) return false;
    std::vector<std::string> head;
    bool first = true, found = false;
    int cCase = -1, cEpoch = -1, cFlags = -1;
    eachLine(m_records + "/dataset.csv", [&](const std::string& line) {
        if (line.empty()) return true;
        std::vector<std::string> c = csvSplitLine(line);
        if (first) {
            first = false; head = c;
            for (size_t i = 0; i < c.size(); ++i) {
                if (c[i] == "case_id") cCase = int(i);
                else if (c[i] == "epoch_ms") cEpoch = int(i);
                else if (c[i] == "flags") cFlags = int(i);
            }
            return true;
        }
        if (cCase < 0 || cEpoch < 0 || int(c.size()) <= std::max(cCase, cEpoch)) return true;
        if (c[size_t(cCase)] != caseId || std::atoll(c[size_t(cEpoch)].c_str()) != epochMs) return true;
        Features f;
        for (size_t i = 0; i < head.size() && i < c.size(); ++i)
            if (!c[i].empty()) { bool known = false; f.byName(head[i], &known); if (known) f.setByName(head[i], std::atof(c[i].c_str())); }
        if (out) *out = f;
        if (flags) *flags = (cFlags >= 0 && int(c.size()) > cFlags) ? c[size_t(cFlags)] : std::string();
        found = true;
        return false;                 // the first match is the one
    });
    return found;
}

std::map<std::string, std::string> App::caseDetails(const std::string& caseId) const
{
    std::map<std::string, std::string> out;
    indexPatients();
    auto pit = m_patientRows.find(caseId);
    if (pit != m_patientRows.end()) {
        for (const auto& kv : pit->second) if (!kv.second.empty()) out[kv.first] = kv.second;
        out["_source"] = "patients.csv";
        return out;
    }
    // Fall back to the pt_ columns written beside the readings.
    std::vector<std::string> head;
    bool first = true;
    int cCase = -1;
    eachLine(m_records + "/dataset.csv", [&](const std::string& line) {
        if (line.empty()) return true;
        std::vector<std::string> c = csvSplitLine(line);
        if (first) { first = false; head = c; for (size_t i = 0; i < c.size(); ++i) if (c[i] == "case_id") cCase = int(i); return true; }
        if (cCase < 0 || int(c.size()) <= cCase || c[size_t(cCase)] != caseId) return true;
        for (size_t i = 0; i < head.size() && i < c.size(); ++i)
            if (head[i].compare(0, 3, "pt_") == 0 && !c[i].empty()) out[head[i].substr(3)] = c[i];
        return true;
    });
    if (!out.empty()) out["_source"] = "the readings";
    return out;
}

std::string App::updateCaseDetails(const std::string& caseId,
                                   const std::map<std::string, std::string>& values)
{
    if (!plat::sdReady()) return "There is no records memory (no SD card, and the internal memory is not in use), so nothing can be corrected.";
    if (caseId.empty()) return "No case was chosen.";
    const std::string path = m_records + "/patients.csv";
    m_patientsSize = -2;
    indexPatients();
    std::vector<std::string> columns = m_patientCols;
    if (columns.empty()) {
        std::string h = PatientRecord::csvHeader(), cur;
        for (char c : h) { if (c == ',') { columns.push_back(cur); cur.clear(); } else cur.push_back(c); }
        columns.push_back(cur);
    }
    if (std::find(columns.begin(), columns.end(), "edited_at") == columns.end())
        columns.push_back("edited_at");

    std::map<std::string, std::string> merged;
    auto pit = m_patientRows.find(caseId);
    if (pit != m_patientRows.end()) merged = pit->second;
    merged["case_id"] = caseId;
    int filled = 0;
    for (const auto& kv : values) {
        if (kv.first.empty() || kv.first[0] == '_' || kv.first == "case_id") continue;
        std::string v = kv.second;
        while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
        while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(v.begin());
        merged[kv.first] = v;
        if (!v.empty()) ++filled;
    }
    merged["edited_at"] = isoUtc(nowMs());

    const bool fresh = !plat::fileExists(path);
    std::ofstream f(path, std::ios::app);
    if (!f) return "The correction could NOT be written to patients.csv \xE2\x80\x94 check the records memory.";
    if (fresh) {
        for (size_t i = 0; i < columns.size(); ++i) f << (i ? "," : "") << columns[i];
        f << '\n';
    }
    for (size_t i = 0; i < columns.size(); ++i) {
        auto it = merged.find(columns[i]);
        f << (i ? "," : "") << csvCell(it == merged.end() ? std::string() : it->second);
    }
    f << '\n';
    f.flush();
    if (!f.good()) return "The correction could NOT be written to patients.csv \xE2\x80\x94 check the records memory.";
    f.close();
    m_patientsSize = -2;
    event(fmt("case %s details corrected; %d fields set", caseId.c_str(), filled));
    touch();
    return fmt("Saved. %d field%s recorded for %s. The original measurements are unchanged "
               "\xE2\x80\x94 exports carry the corrected details.", filled, filled == 1 ? "" : "s",
               caseId.c_str());
}

std::string App::recordOutcome(const std::string& caseId, const std::string& pathology,
                               const std::string& reportDate, const std::string& source,
                               const std::string& note)
{
    // BirxBridge::saveOutcome (2.19.0): "" means saved; anything else says WHY
    // it was not, because a Save that changes nothing visible is
    // indistinguishable from a broken one.
    auto refuse = [&](const std::string& why) {
        event("pathology for " + caseId + " = " + pathology + " NOT saved: " + why);
        return why;
    };
    if (!m_outcomes || !plat::sdReady())
        return refuse("there is no records memory (no SD card, and the internal memory is not in use), so there is nowhere to save it");
    std::string id = caseId;
    while (!id.empty() && id.back() == ' ') id.pop_back();
    while (!id.empty() && id.front() == ' ') id.erase(0, 1);
    if (id.empty()) return refuse("choose a case first");
    Outcome o;
    o.caseId = id;
    o.pathology = pathologyFromName(pathology);
    o.reportDate = reportDate;
    o.source = source;
    o.note = note;
    o.recordedMs = nowMs();
    if (o.pathology == Pathology::Unknown)
        return refuse("\"" + pathology + "\" is not a pathology result \xE2\x80\x94 choose Benign, "
                      "Borderline or Malignant");
    std::string why;
    if (!m_outcomes->record(o, &why)) return refuse(why);
    plat::flushAll();
    event("pathology for " + id + " = " + pathology + ": saved to outcomes.csv");
    touch();
    return std::string();
}

std::string App::outcomeOf(const std::string& caseId) const
{
    if (!m_outcomes) return std::string();
    const Pathology p = m_outcomes->lookup(caseId);
    return p == Pathology::Unknown ? std::string() : std::string(pathologyName(p));
}

// Every case id in the record that has no outcome yet — the readings taken
// with no case open (TEST-…) included, exactly as OutcomeStore::pending() gives
// them on the Jetson. H7-1.0.0 to 1.2.9 left those out, and an instrument on
// which only such readings had been taken offered an empty list and a Save
// button that could never be pressed (seen on the bench).
std::vector<std::string> App::casesAwaitingPathology() const
{
    std::vector<std::pair<int64_t, std::string>> v;
    for (const auto& kv : m_byCase) {
        if (m_outcomes && m_outcomes->lookup(kv.first) != Pathology::Unknown) continue;
        int64_t t = 0;
        for (size_t i : kv.second) t = std::max(t, m_logRows[i].epochMs);
        v.push_back({ t, kv.first });
    }
    std::sort(v.begin(), v.end());
    std::vector<std::string> out;
    for (auto& p : v) out.push_back(p.second);
    return out;
}

int App::pendingOutcomes() const
{
    int n = 0;
    for (const auto& kv : m_byCase) {
        if (!m_outcomes || m_outcomes->lookup(kv.first) == Pathology::Unknown) ++n;
    }
    return n;
}

// ============================================================================
// export to a USB stick

std::string App::exportLogs(const std::string& when, const std::string& tag)
{
    if (!plat::usbReady())
        return "Nothing was exported: no USB stick is mounted (" + plat::usbStatus() + "). "
               "Plug one in and press this again.";
    const std::vector<LogCase> cases = caseLog(when, tag);
    if (cases.empty()) return "No case matches that filter, so there was nothing to write.";
    std::map<std::string, const LogCase*> byId;
    for (const LogCase& c : cases) byId[c.caseId] = &c;

    const std::string dev = m_s.deviceId.empty() ? std::string("NODEV") : m_s.deviceId;
    const std::string folder = plat::usbRoot() + "/BIRADx_" + dev + "_" + localText(nowMs(), "%Y-%m-%d");
    if (!plat::makeDirs(folder)) return "Could not create the export folder on the USB stick.";
    const std::string name = dev + "_" + localText(nowMs(), "%Y-%m-%d_%H-%M-%S") + "_logs_" +
                             when + "_" + tag + ".csv";
    const std::string dest = folder + "/" + name;

    std::ifstream in(m_records + "/dataset.csv");
    if (!in) return "dataset.csv could not be opened.";
    std::string headerLine;
    std::getline(in, headerLine);
    if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
    const std::vector<std::string> head = csvSplitLine(headerLine);
    int cCase = -1;
    for (size_t i = 0; i < head.size(); ++i) if (head[i] == "case_id") cCase = int(i);
    if (cCase < 0) return "dataset.csv has no case_id column.";

    std::ofstream os(dest);
    if (!os) return "Could not write " + name + " on the USB stick.";
    indexPatients();
    os << headerLine << ",case_tier,case_complete,case_valid_readings,case_target,"
          "case_high_readings,case_when,case_detail,pathology,details_edited_at\n";
    int rows = 0;
    std::set<std::string> seen;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> cells = csvSplitLine(line);
        if (int(cells.size()) <= cCase) continue;
        auto it = byId.find(cells[size_t(cCase)]);
        if (it == byId.end()) continue;
        const LogCase& m = *it->second;
        std::map<std::string, std::string> det;
        auto pit = m_patientRows.find(m.caseId);
        if (pit != m_patientRows.end()) det = pit->second;
        for (size_t i = 0; i < head.size() && i < cells.size(); ++i) {
            if (head[i].compare(0, 3, "pt_") != 0) continue;
            auto d = det.find(head[i].substr(3));
            if (d != det.end() && !d->second.empty()) cells[i] = d->second;
        }
        for (size_t i = 0; i < cells.size(); ++i) os << (i ? "," : "") << csvCell(cells[i]);
        auto ed = det.find("edited_at");
        os << ',' << csvCell(m.tier) << ',' << (m.complete ? '1' : '0') << ',' << m.readings
           << ',' << m.target << ',' << m.nHigh << ',' << csvCell(m.whenText) << ','
           << csvCell(m.detail) << ',' << csvCell(m.pathology) << ','
           << csvCell(ed == det.end() ? std::string() : ed->second) << '\n';
        ++rows;
        seen.insert(m.caseId);
    }
    os.flush();
    const bool ok = os.good();
    os.close();
    plat::flushAll();
    if (!ok) return "The export could NOT be written completely \xE2\x80\x94 the stick may be full or was removed.";
    if (rows == 0) { plat::removeFile(dest); return "Those cases have no readings in dataset.csv."; }
    const std::string msg = fmt("%d readings from %d case%s.", rows, int(seen.size()),
                                seen.size() == 1 ? "" : "s") +
                            " Written to BIRADx_" + dev + "_" + localText(nowMs(), "%Y-%m-%d") +
                            "/" + name + " \xE2\x80\x94 it is safe to remove the drive.";
    event("export: " + msg);
    return msg;
}

namespace {
bool copyTree(const std::string& from, const std::string& to, int* files, long* bytes)
{
    if (!plat::makeDirs(to)) return false;
    bool ok = true;
    plat::listDir(from, [&](const std::string& name, bool dir, long) {
        if (!ok) return;
        if (dir) { ok = copyTree(from + "/" + name, to + "/" + name, files, bytes); return; }
        long b = 0;
        if (!plat::copyFile(from + "/" + name, to + "/" + name, &b)) { ok = false; return; }
        ++*files; *bytes += b;
    });
    return ok;
}
} // namespace

// ============================================================================
// deleting the logs (H7-1.9.0)

namespace {
// What stays: what the instrument needs to go on working as it did. The case
// numbering (so no case id is ever given twice), the needle calibrations and
// the list of needles already used, the range calibration, and the marker of
// the running session. Everything else in the records folder is a log.
// H7-2.5.0: and the material library (Settings -> Training), which is learnt from, not a log.
bool keptRecord(const std::string& name)
{
    static const char* keep[] = { "caseid_state.txt", "range_calibration.txt", "needle_cal.csv",
                                  "needle_keys.txt", "needles_used.txt", "calibration",
                                  ".session_running", ".write_probe", "lots", "lotcal.txt", "materials.txt",
                                  "training_qc.txt" };   // H7-2.12.0: each Training needle's last check
    for (const char* k : keep) if (name == k) return true;
    return false;
}
} // namespace

void App::logsFootprint(int* files, long* bytes) const
{
    int n = 0;
    long b = 0;
    if (plat::sdReady()) {
        std::function<void(const std::string&)> walk = [&](const std::string& dir) {
            plat::listDir(dir, [&](const std::string& name, bool isDir, long size) {
                if (isDir) walk(dir + "/" + name);
                else { ++n; b += size; }
            });
        };
        plat::listDir(m_records, [&](const std::string& name, bool isDir, long size) {
            if (keptRecord(name)) return;
            if (isDir) walk(m_records + "/" + name);
            else { ++n; b += size; }
        });
    }
    if (files) *files = n;
    if (bytes) *bytes = b;
}

std::string App::deleteLogsBlocker() const
{
    if (!plat::sdReady()) return "There is no records memory.";
    if (caseOpen()) return "A case is open. End it first, then delete the logs.";
    if (busy()) return "Wait until the measurement in progress is over.";
    if (m_diagOn) return "Diagnostic mode is on. Stop it first (its file is one of the logs).";
    return std::string();
}

std::string App::deleteLogsStart()
{
    const std::string why = deleteLogsBlocker();
    if (!why.empty()) return why;
    m_delQueue.clear();
    m_delFailed = 0;
    m_delBytes = 0;
    // files first, deepest first, so that each folder is empty when its turn comes
    std::function<void(const std::string&)> walk = [&](const std::string& dir) {
        std::vector<std::string> subdirs;
        plat::listDir(dir, [&](const std::string& name, bool isDir, long size) {
            if (isDir) subdirs.push_back(dir + "/" + name);
            else { m_delQueue.push_back(dir + "/" + name); m_delBytes += size; }
        });
        for (const std::string& d : subdirs) walk(d);
    };
    std::vector<std::string> topDirs;
    plat::listDir(m_records, [&](const std::string& name, bool isDir, long size) {
        if (keptRecord(name)) return;
        if (isDir) topDirs.push_back(m_records + "/" + name);
        else { m_delQueue.push_back(m_records + "/" + name); m_delBytes += size; }
    });
    for (const std::string& d : topDirs) walk(d);
    m_delTotal = int(m_delQueue.size());
    // the queue is taken from the back
    std::reverse(m_delQueue.begin(), m_delQueue.end());
    event(fmt("delete logs: started, %d files (%ld kB)", m_delTotal, m_delBytes / 1024));
    if (m_delQueue.empty()) {
        m_delResult = "There were no logs to delete.";
        return std::string();
    }
    touch();
    return std::string();
}

void App::pollDelete()
{
    if (m_delQueue.empty()) return;
    const uint32_t t0 = plat::ticks();
    while (!m_delQueue.empty() && uint32_t(plat::ticks() - t0) < 40u) {
        if (!plat::removeFile(m_delQueue.back())) ++m_delFailed;
        m_delQueue.pop_back();
    }
    touch();
    if (!m_delQueue.empty()) return;
    // the folders the next records go into
    plat::makeDirs(m_records + "/sweeps");
    plat::flushAll();
    m_indexedSize = -2;
    refreshLog();
    if (m_outcomes) m_outcomes->load();
    const std::string size = m_delBytes >= 1048576 ? fmt("%.1f MB", m_delBytes / 1048576.0)
                                                   : fmt("%ld kB", (m_delBytes + 1023) / 1024);
    m_delResult = m_delFailed
        ? fmt("%d of %d files deleted; %d could NOT be deleted (the records memory refused them).",
              m_delTotal - m_delFailed, m_delTotal, m_delFailed)
        : fmt("The logs are deleted: %d files, %s. Case numbering, needle calibrations and settings are kept.",
              m_delTotal, size.c_str());
    event("delete logs: " + m_delResult);
    if (plat::recordsOnInternal() && m_storageProblem.find("almost full") != std::string::npos)
        m_storageProblem.clear();
}

std::string App::exportAll()
{
    if (!plat::usbReady())
        return "Nothing was exported: no USB stick is mounted (" + plat::usbStatus() + "). "
               "Plug one in and press this again.";
    if (!plat::sdReady()) return "There are no records to copy: there is no records memory.";
    matRewriteIfStale();                   // H7-2.7.2: the library file as the classifier marks it
    const std::string dev = m_s.deviceId.empty() ? std::string("NODEV") : m_s.deviceId;
    const std::string day = localText(nowMs(), "%Y-%m-%d");
    const std::string stamp = localText(nowMs(), "%Y-%m-%d_%H-%M-%S");
    const std::string folder = "BIRADx_" + dev + "_" + day;
    const std::string target = plat::usbRoot() + "/" + folder;
    if (!plat::makeDirs(target)) return "Could not create " + folder + " on the USB stick.";
    int files = 0;
    long bytes = 0;
    bool ok = true;
    plat::listDir(m_records, [&](const std::string& name, bool dir, long) {
        if (!ok) return;
        if (dir) { ok = copyTree(m_records + "/" + name, target + "/" + name, &files, &bytes); return; }
        long b = 0;
        if (!plat::copyFile(m_records + "/" + name, target + "/" + dev + "_" + stamp + "_" + name, &b)) { ok = false; return; }
        ++files; bytes += b;
    });
    plat::flushAll();
    if (!ok) return "Copy failed while writing to the USB stick. It may be full or was removed.";
    const std::string size = bytes >= 1048576 ? fmt("%.1f MB", bytes / 1048576.0)
                                              : fmt("%.0f kB", bytes / 1024.0);
    const std::string msg = fmt("Copied %d files (%s) to %s \xE2\x80\x94 it is safe to remove the drive.",
                                files, size.c_str(), folder.c_str());
    event("export all: " + msg);
    return msg;
}

// ============================================================================
// diagnostics (Controller::writeDiagnostics)

std::vector<std::string> App::diagnosticsLines()
{
    std::vector<std::string> t;
    auto head = [&t](const std::string& s) {
        t.push_back(std::string());
        t.push_back("=== " + s + " " + std::string(s.size() < 60 ? 60 - s.size() : 0, '='));
    };
    auto kv = [&t](const std::string& k, const std::string& v) {
        std::string key = k;
        if (key.size() < 22) key += std::string(22 - key.size(), ' ');
        t.push_back(key + " : " + v);
    };
    auto yn = [](bool b) { return std::string(b ? "yes" : "no"); };

    t.push_back("BIRADx diagnostics");
    kv("written", localText(nowMs(), "%Y-%m-%dT%H:%M:%S"));
    kv("version", std::string(BIRADX_VERSION) + " (built " + plat::buildDate() +
                  "; behaves as BIRADx " + BIRADX_PARITY + ")");
    const std::string cp = clockProblem();
    if (cp.empty()) kv("clock", "plausible"); else kv("CLOCK", cp);
    kv("utc offset", fmt("%+d min", m_s.utcOffsetMin));

    head("identity and storage");
    kv("device id", m_s.deviceId.empty() ? std::string("NOT SET") : m_s.deviceId);
    kv("needle revision", m_s.needleRev);
    kv("settings memory", fmt("EEPROM, copy #%d, %d of %d bytes", m_store.lastSeq(),
                              int(m_store.lastBytes()), int(ConfigStore::capacity())));
    kv("settings problem", m_settingsProblem.empty() ? std::string("(none)") : m_settingsProblem);
    kv("records memory", plat::sdStatus());
    kv("internal memory", plat::internalMemoryStatus());
    kv("internal memory chip", plat::internalMemoryChip());
    {
        bool couldWrite = false;
        if (plat::sdReady()) {
            const std::string probe = m_records + "/.write_probe";
            std::ofstream f(probe);
            couldWrite = bool(f) && (f << "x").good();
            f.close();
            plat::removeFile(probe);
        }
        kv("write probe", couldWrite ? std::string("succeeded")
                                     : std::string("FAILED \xE2\x80\x94 records cannot be written on this unit"));
    }
    kv("storage problem", m_storageProblem.empty() ? std::string("(none)") : m_storageProblem);
    kv("usb stick", plat::usbStatus());
    kv("records dir", m_records);
    kv("readings indexed", fmt("%d in %d cases", int(m_logRows.size()), int(m_byCase.size())));

    head("settings, verbatim (EEPROM)");
    {
        std::istringstream in(ConfigStore::serialise(m_s));
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, 3, "pc=") == 0) line = "pc=(hidden)";   // never the digits
            t.push_back(line);
        }
    }

    head("the link to the measurement unit");
    kv("port", "USART1, 57600 8N1");
    kv("connected", yn(connected()));
    kv("last byte", m_everByte ? fmt("%u ms ago", unsigned(lastByteAgeMs())) : std::string("never"));
    kv("running", yn(m_running));
    kv("unparsed lines", fmt("%d", m_unparsedTotal));
    kv("heartbeats seen", fmt("%d", m_heartbeats));
    kv("uart errors", fmt("%u (recovered)", unsigned(plat::linkErrors())));
    kv("frames sent", fmt("%d sent, %d refused by the port", m_txOk, m_txFail));
    kv("port state", plat::linkPortStatus());
    kv("last diagnosis", m_linkMessage.empty() ? std::string("(none)") : m_linkMessage);
    kv("last probe", m_probeResult.empty() ? std::string("(not run)") : m_probeResult);
    kv("voice module", plat::voiceStatus());
    kv("lot calibration file", m_lotFileLoaded ? fmt("rev %d, %d lots, from %s, issued %s; trial cut-offs with %s needles",
                                                   m_lotFile.rev, int(m_lotFile.lots.size()), m_lotFile.device.c_str(),
                                                   m_lotFile.issued.c_str(), m_lotFile.trialUncalibrated ? "UNcalibrated" : "air-calibrated")
                                               : std::string("none"));
    kv("needle lot in use", currentLotId().empty() ? std::string("(none typed)") : currentLotId() + (lotStatus().empty() ? std::string() : " \xE2\x80\x94 " + lotStatus()));
    kv("lot runs", fmt("%d on this device", int(m_lotRuns.size())));
    kv("measurement unit restarts", fmt("%d this session (its power switched off and on because it did not answer; at most %d)", m_unitRestarts, 2));
    kv("touch panel", plat::touchStatus());

    head("what the interface is refusing, and why");
    kv("can measure", yn(canMeasure()));
    kv("measure blocker", measureBlocker().empty() ? std::string("(nothing)") : measureBlocker());
    kv("can start a case", yn(canStartCase()));
    kv("case blocker", caseBlocker().empty() ? std::string("(nothing)") : caseBlocker());
    kv("require needle cal", yn(m_s.reqNeedleCal));
    kv("rules found with", m_s.rulesAirCal ? "air-calibrated needles (the air calibration is used)"
                                           : "needles not calibrated in air (readings taken as they come)");
    kv("require intake", yn(m_s.reqIntake));
    kv("require range cal", yn(m_s.reqRangeCal));
    kv("require lot file", yn(m_s.reqLotFile));
    kv("require healthy-tissue sample", yn(m_s.reqTissueRef));
    kv("healthy-tissue samples", m_refs.empty() ? std::string("none for the open case") : tissueRefText());

    head("the open case");
    kv("case", m_caseId.empty() ? std::string("(none)") : m_caseId);
    kv("readings", fmt("%d taken, %d valid, target %d", m_caseResult.nReadings,
                       m_caseResult.nValid, m_aggCfg.minValidReadings));
    kv("answer", std::string(tierName(m_caseResult.tier)) + " / " + caseLabel());
    kv("reason", m_caseResult.reason);

    head("needle calibration");
    {
        std::istringstream in(calibrationReport());
        std::string line;
        while (std::getline(in, line)) t.push_back(line);
    }
    kv("range calibrator", rangeCalSummary());

    head("the last sweep, as it arrived");
    if (m_current.points.empty()) t.push_back("(no sweep has been received)");
    else {
        t.push_back("   freq Hz      |Z| ohm    phase deg  range");
        for (const Point& p : m_current.points)
            t.push_back(fmt("%10.1f %13.2f %12.2f %6d", p.freqHz, p.magOhm, p.phaseDeg, p.range));
    }

    head("the decision table in force");
    t.push_back(ruleSummary());
    for (const std::string& p : m_s.rules.problems()) t.push_back("rule warning: " + p);
    t.push_back("version string: " + m_engine.ruleTable().version);

    head("commands and events, newest first \xE2\x80\x94 no heartbeats");
    if (m_events.empty()) t.push_back("(nothing since this instrument started)");
    for (const std::string& l : m_events) t.push_back(l);

    head("raw traffic, newest first (heartbeats included)");
    for (const std::string& l : m_traffic) t.push_back(l);

    head("unparsed lines, newest last");
    for (const std::string& l : m_unparsed) t.push_back(l);

    head("health \xE2\x80\x94 the last two minutes, and how the last session ended");
    kv("this start", plat::resetCause());
    kv("timer check", timerCheck());          // H7-2.7.2
    kv("previous session", m_lastSessionEnd.empty() ? std::string("ended cleanly") : m_lastSessionEnd);
    kv("diagnostic mode", m_diagOn ? "ON, recording to " + m_diagPath : std::string("off"));
    {
        const std::string tail = healthTail(13);
        t.push_back(tail.empty() ? std::string("(no health log yet)") : tail);
    }

    t.push_back(std::string());
    t.push_back("--- end ---");
    return t;
}

} // namespace app
