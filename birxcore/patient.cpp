#include "patient.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>

namespace birx {
namespace {

// Crockford base32: no I, L, O or U. Nothing reads as 0/1 confusion, and no
// four-character group can spell an unfortunate word.
const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int valueOf(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    // Crockford's documented input forgiveness.
    if (c == 'I' || c == 'L') c = '1';
    if (c == 'O') c = '0';
    if (c == 'U') c = 'V';
    const char* p = std::strchr(kAlphabet, c);
    return p ? int(p - kAlphabet) : -1;
}

std::string encode(unsigned long v, int width) {
    std::string s(width, '0');
    for (int i = width - 1; i >= 0; --i) { s[i] = kAlphabet[v % 32]; v /= 32; }
    return s;
}

// Position-weighted sum mod 32. A single substitution changes the sum; a swap
// of two different characters at adjacent weights changes it by their
// difference, which is non-zero unless the characters are equal.
char checkChar(const std::string& body) {
    unsigned long sum = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        const int v = valueOf(body[i]);
        if (v < 0) return '?';
        sum += static_cast<unsigned long>(v) * (i + 1);
    }
    return kAlphabet[sum % 32];
}

} // namespace

// ---------------------------------------------------------------------------

bool CaseIdGenerator::setDeviceId(const std::string& deviceId, std::string* why) {
    std::string norm;
    for (char c : deviceId) {
        if (c == '-' || c == ' ') continue;
        const int v = valueOf(c);
        if (v < 0) { if (why) *why = "device id contains a character outside base32"; return false; }
        norm.push_back(kAlphabet[v]);
    }
    if (norm.size() != 4) {
        if (why) *why = "device id must be exactly 4 base32 characters — it is assigned, "
                        "never derived from hardware, because a derived 4-character id "
                        "collides thousands of times over a fleet of 100,000";
        return false;
    }
    m_deviceId = norm;
    if (why) why->clear();
    return true;
}

std::string CaseIdGenerator::next(int daysSinceEpoch) {
    if (!ready() || daysSinceEpoch < 0 || daysSinceEpoch >= 32768) return std::string();
    if (daysSinceEpoch != m_day) { m_day = daysSinceEpoch; m_seq = 0; }
    if (m_seq >= kMaxPerDay) return std::string();     // refuse rather than wrap

    const std::string body = m_deviceId
                           + encode(static_cast<unsigned long>(daysSinceEpoch), 3)
                           + encode(static_cast<unsigned long>(m_seq), 2);
    ++m_seq;
    return body.substr(0,4) + "-" + body.substr(4,3) + "-" + body.substr(7,2) + checkChar(body);
}

std::string CaseIdGenerator::normalise(const std::string& typed) {
    std::string out;
    for (char c : typed) {
        if (c == '-' || c == ' ' || c == '_') continue;
        const int v = valueOf(c);
        if (v < 0) return std::string();
        out.push_back(kAlphabet[v]);
    }
    return out;
}

bool CaseIdGenerator::validate(const std::string& caseId) {
    const std::string s = normalise(caseId);
    if (s.size() != 10) return false;
    return checkChar(s.substr(0, 9)) == s[9];
}

bool CaseIdGenerator::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# birx case id state\ndevice: " << m_deviceId
      << "\nday: " << m_day << "\nseq: " << m_seq << "\n";
    return f.good();
}

bool CaseIdGenerator::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t c = line.find(':');
        if (c == std::string::npos) continue;
        const std::string k = line.substr(0, c);
        std::istringstream v(line.substr(c + 1));
        if      (k == "device") { std::string d; v >> d; setDeviceId(d); }
        else if (k == "day")    v >> m_day;
        else if (k == "seq")    v >> m_seq;
    }
    return ready();
}

// ---------------------------------------------------------------------------
// The intake schema.
//
// Page 1 is what the operator must answer with a needle waiting, so it is five
// fields and nothing else. Page 2 is the set that plausibly explains variance
// in the measurement itself. Page 3 is identification and free text.
//
// Two flags matter and are easy to confuse:
//
//   modelCandidate — may be offered to the trainer as a feature
//   stratifier     — used to SPLIT the analysis, and normally NOT a feature
//
// BI-RADS is the important case. On the Trial 2 cohort BI-RADS alone reaches
// cross-validated AUC 0.969 against the device's 0.952, and adding the device
// to BI-RADS moves it only to 0.975 — which reads as though the device were
// redundant. It is not: that comparison pools BI-RADS 3 through 5, where the
// category naturally spans the whole risk range. WITHIN BI-RADS 4a — 75 of the
// 138 cases, 54% of the cohort, cancer rate 1.3%, and every one of them
// biopsied — IPS reaches AUC 0.906 [0.82, 0.97] for "needs tissue".
//
// That is the device's job: not to beat the radiologist across the whole
// spectrum, but to sort the large ambiguous middle the radiologist cannot
// sort. So BI-RADS is collected as a stratifier. Putting it in the model would
// mostly let the model restate the referral decision.
// ---------------------------------------------------------------------------

const std::vector<IntakeField>& intakeSchema() {
    static const std::vector<IntakeField> s = {
        // ---- page 1: required, one screen ----
        { "age", "Age", "Breast tissue composition changes with age; this is the strongest covariate after the device itself.",
          FieldType::Number, "years", true, 12, 110, {}, 1, true, false },

        { "birads", "BI-RADS", "Used to split the analysis, not as a model input. See the note in patient.cpp.",
          FieldType::Choice, "", true, kNaN, kNaN, {"3","4A","4B","4C","5"}, 1, false, true },

        { "lesion_size", "Lesion size", "Largest dimension on imaging.",
          FieldType::Number, "mm", true, 1, 200, {}, 1, true, false },

        { "side", "Side", "",
          FieldType::Choice, "", true, kNaN, kNaN, {"Left","Right"}, 1, false, true },

        { "clock", "Position", "Clock position of the lesion.",
          FieldType::Choice, "", true, kNaN, kNaN,
          {"12","1","2","3","4","5","6","7","8","9","10","11","Central"}, 1, false, false },

        // The needle. Two fields rather than one because the lot is typed
        // once per box and remembered, while the serial changes every needle:
        // that is 9 characters per patient instead of 13. Neither is a model
        // input — they are provenance, and the gate that enforces single use.
        { "needle_lot", "Needle lot", "Printed on the box. Remembered until the box changes.",
          FieldType::Text, "", true, kNaN, kNaN, {}, 1, false, false },

        { "needle_serial", "Needle serial", "Printed on the packet, with its check group. Each serial works once on this device.",
          FieldType::Text, "", true, kNaN, kNaN, {}, 1, false, false },

        // ---- page 2: recommended, explains variance in the measurement ----
        { "anaesthetic", "Local anaesthetic", "Injected fluid dilutes the extracellular space and changes impedance directly. Probably the largest uncontrolled variable in the whole procedure.",
          FieldType::Choice, "", false, kNaN, kNaN,
          {"None yet","Under 5 min","5-15 min","Over 15 min"}, 2, true, false },

        { "density", "Breast density", "ACR category. Describes the tissue the needle passes through — more relevant than breast size.",
          FieldType::Choice, "", false, kNaN, kNaN, {"a","b","c","d"}, 2, true, false },

        { "menopause", "Menopausal status", "A cleaner proxy for tissue composition than age alone.",
          FieldType::Choice, "", false, kNaN, kNaN, {"Pre","Peri","Post"}, 2, true, false },

        { "cycle_day", "Cycle day", "Premenopausal only. Breast water content varies across the cycle.",
          FieldType::Number, "day", false, 1, 40, {}, 2, true, false },

        { "lesion_depth", "Lesion depth", "From skin to lesion centre.",
          FieldType::Number, "mm", false, 1, 150, {}, 2, true, false },

        { "prior_surgery", "Prior surgery or radiation at this site", "Scar tissue has a very different impedance from either normal or tumour tissue.",
          FieldType::Toggle, "", false, kNaN, kNaN, {}, 2, false, true },

        { "hormone_therapy", "Hormone therapy", "",
          FieldType::Toggle, "", false, kNaN, kNaN, {}, 2, true, false },

        // Where this lesion sits in the treatment course. It belongs on the
        // form for two separate reasons, and only one of them is statistical.
        //
        // NEOADJUVANT means chemotherapy has already been given and this
        // needle is going into a tumour that has been treated. Treated tumour
        // is not untreated tumour electrically — necrosis, fibrosis and oedema
        // all change the extracellular path the measurement runs through — so
        // a model fitted across both is fitted across two different tissues.
        // These cases must be identifiable afterwards, and in the first
        // instance probably excluded.
        //
        // A stratifier and not a model input: it says which population the
        // reading belongs to, and a model that used it would be predicting
        // from the treatment decision rather than from the tissue.
        { "therapy", "Treatment setting",
          "Neoadjuvant means the tumour has already been treated. Treated tissue "
          "is not untreated tissue electrically, so these cases are analysed apart.",
          FieldType::Choice, "", false, kNaN, kNaN,
          {"Untreated", "Neoadjuvant", "Adjuvant"}, 2, false, true },

        // ---- page 3: identification and notes ----
        { "national_id", "National ID", "Optional. Never the key — the case id is. Kept so a result can be matched back if a form is lost.",
          FieldType::Text, "", false, kNaN, kNaN, {}, 3, false, false },

        { "notes", "Notes", "",
          FieldType::Text, "", false, kNaN, kNaN, {}, 3, false, false },
    };
    return s;
}

const IntakeField* intakeField(const std::string& id) {
    for (const IntakeField& f : intakeSchema()) if (f.id == id) return &f;
    return nullptr;
}

// ---------------------------------------------------------------------------

void PatientRecord::set(const std::string& id, const std::string& value) {
    if (id == "national_id") { nationalId = value; return; }
    values[id] = value;
}

std::string PatientRecord::get(const std::string& id) const {
    if (id == "national_id") return nationalId;
    auto it = values.find(id);
    return it == values.end() ? std::string() : it->second;
}

double PatientRecord::number(const std::string& id) const {
    const std::string s = get(id);
    if (s.empty()) return kNaN;
    // MCU port: see dataset.cpp toDouble.
    double d = 0; return birxStrtod(s, &d) ? d : kNaN;
}

std::vector<std::string> PatientRecord::missing() const {
    std::vector<std::string> out;
    for (const IntakeField& f : intakeSchema()) {
        if (!f.required) continue;
        const std::string v = get(f.id);
        if (v.empty()) { out.push_back(f.id); continue; }
        if (f.type == FieldType::Number) {
            const double d = number(f.id);
            if (!std::isfinite(d)) { out.push_back(f.id); continue; }
            if (std::isfinite(f.min) && d < f.min) { out.push_back(f.id); continue; }
            if (std::isfinite(f.max) && d > f.max) { out.push_back(f.id); continue; }
        } else if (f.type == FieldType::Choice) {
            if (std::find(f.options.begin(), f.options.end(), v) == f.options.end())
                out.push_back(f.id);
        }
    }
    return out;
}

namespace {
std::string q(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}
} // namespace

std::string PatientRecord::csvHeader() {
    std::string h = "case_id,national_id,operator,started_at,needle_code";
    for (const IntakeField& f : intakeSchema())
        if (f.id != "national_id") h += "," + f.id;
    return h;
}

std::string PatientRecord::csvRow() const {
    std::string r = q(caseId) + "," + q(nationalId) + "," + q(operatorId) + "," + q(startedAt)
                  + "," + q(needleCode);
    for (const IntakeField& f : intakeSchema())
        if (f.id != "national_id") r += "," + q(get(f.id));
    return r;
}

} // namespace birx
