#include "config.h"
#include "platform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace app {

// --------------------------------------------------------------------------
// The shipped table (2.15.0 onward, MarginConfig::defaults()).

RulesConfig RulesConfig::defaults()
{
    // H7-1.5.0, the table asked for from the bench. Three answers only:
    //   Noisy      Z above 5000
    //   High Risk  Z 0-2000 and IPS -13..+6, or Z 2000-4000 and IPS -5..0
    //   Low Risk   everything else
    // Written as rectangles that tile the plane and do NOT overlap: where two
    // meet, one of them starts a ten-thousandth further on (kGap). A reading in
    // such a seam, or off the ends of the table, takes the nearest row
    // (RuleTable::lookup), so this table can only ever say these three words.
    const double kIpsAll = 300.0;
    const double kZAll   = 1000000.0;
    const double kGap    = 0.0001;

    RulesConfig c;
    auto row = [&](double zl, double zh, double il, double ih, const char* label,
                   const char* color, const char* level, const char* sound) {
        RuleRowCfg r;
        r.zMin = zl; r.zMax = zh; r.ipsMin = il; r.ipsMax = ih;
        r.label = label; r.color = color; r.level = level; r.sound = sound;
        c.rows.push_back(r);
    };
    const char* H = "High Risk"; const char* Hc = "#C62828";
    const char* L = "Low Risk";  const char* Lc = "#2E7D32";
    // H7-1.7.0: each answer its own recording on the voice module
    const char* Hs = kSoundHigh; const char* Ls = kSoundLow;
    row(0,             2000, -13,           6,             H, Hc, "high", Hs);
    row(2000 + kGap,   4000, -5,            0,             H, Hc, "high", Hs);
    row(0,             2000, -kIpsAll,      -13 - kGap,    L, Lc, "low",  Ls);
    row(0,             2000, 6 + kGap,      kIpsAll,       L, Lc, "low",  Ls);
    row(2000 + kGap,   4000, -kIpsAll,      -5 - kGap,     L, Lc, "low",  Ls);
    row(2000 + kGap,   4000, 0 + kGap,      kIpsAll,       L, Lc, "low",  Ls);
    row(4000 + kGap,   5000, -kIpsAll,      kIpsAll,       L, Lc, "low",  Ls);
    row(5000 + kGap,   kZAll, -kIpsAll,     kIpsAll,       "Noisy", "#546E7A", "invalid", kSoundNoisy);

    c.defaultLabel = "Low Risk";
    c.defaultColor = "#2E7D32";
    c.noisyThreshold = 5000.0;
    return c;
}

std::string fmtNum(double v)
{
    if (!std::isfinite(v)) return "nan";
    char b[64];
    // The shortest fixed-point text that reads back to the same double, so
    // the editor shows "-11.0001" and "1900", never "1.9e+03".
    if (std::fabs(v) < 1e15) {
        for (int p = 0; p <= 12; ++p) {
            std::snprintf(b, sizeof b, "%.*f", p, v);
            if (std::strtod(b, nullptr) == v) return b;
        }
    }
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

std::vector<std::string> RulesConfig::problems() const
{
    std::vector<std::string> out;
    char b[320];
    for (size_t i = 0; i < rows.size(); ++i) {
        const RuleRowCfg& r = rows[i];
        if (!r.enabled) continue;
        bool blank = true;
        for (char ch : r.label) if (!std::isspace((unsigned char)ch)) blank = false;
        if (blank) { std::snprintf(b, sizeof b, "Row %d has no label", int(i + 1)); out.push_back(b); }
        if (r.isEmptyRange()) {
            std::snprintf(b, sizeof b, "Row %d has an empty range and can never match", int(i + 1));
            out.push_back(b);
        }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const RuleRowCfg& a = rows[i];
        if (!a.enabled || a.isEmptyRange()) continue;
        for (size_t j = i + 1; j < rows.size(); ++j) {
            const RuleRowCfg& c = rows[j];
            if (!c.enabled || c.isEmptyRange()) continue;
            if (a.label == c.label) continue;
            const double zl = std::max(a.zMin, c.zMin), zh = std::min(a.zMax, c.zMax);
            const double il = std::max(a.ipsMin, c.ipsMin), ih = std::min(a.ipsMax, c.ipsMax);
            if (zl <= zh && il <= ih) {
                std::snprintf(b, sizeof b,
                    "Row %d (\"%s\") and row %d (\"%s\") overlap over Z=[%s, %s], "
                    "IPS=[%s, %s]; row %d wins",
                    int(i + 1), a.label.c_str(), int(j + 1), c.label.c_str(),
                    fmtNum(zl).c_str(), fmtNum(zh).c_str(), fmtNum(il).c_str(),
                    fmtNum(ih).c_str(), int(i + 1));
                out.push_back(b);
            }
        }
    }
    if (!std::isfinite(noisyThreshold)) out.push_back("The noisy threshold is not a valid number");
    return out;
}

birx::Tier levelFromName(const std::string& s)
{
    std::string l;
    for (char c : s) if (!std::isspace((unsigned char)c)) l.push_back(char(std::tolower((unsigned char)c)));
    if (l == "low")        return birx::Tier::Low;
    if (l == "suspicious") return birx::Tier::Suspicious;
    if (l == "high")       return birx::Tier::High;
    return birx::Tier::Invalid;
}

const char* levelName(birx::Tier t)
{
    switch (t) {
    case birx::Tier::Low:        return "low";
    case birx::Tier::Suspicious: return "suspicious";
    case birx::Tier::High:       return "high";
    default: break;
    }
    return "invalid";
}

// BirxBridge::setRuleTable, without Qt.
birx::RuleTable RulesConfig::toRuleTable(bool active) const
{
    birx::RuleTable t;
    t.active = active;
    // ONLY THE ROWS DECIDE (H7-1.4.0, asked for from the bench: "let it read
    // from the rules table only; whatever range we want, we add there").
    // 2.20.0 has two answers that are not rows: a fallback label for a reading
    // no row matches (shipped as "Low Risk") and a "noisy" label above a
    // threshold. Neither is used here: a reading that no enabled row matches is
    // "No rule" — shown as that, not counted, silent — and anything a site
    // wants called something is a row it adds.
    t.noisyThresholdOhm = birx::kNaN;
    t.defaultLabel = kNoRuleLabel;
    t.defaultColor = "#546E7A";
    t.defaultSound.clear();
    t.noisyLabel   = noisyLabel;
    t.noisyColor   = noisyColor;
    t.noisySound.clear();
    for (const RuleRowCfg& m : rows) {
        birx::RuleRow r;
        r.zMin = m.zMin; r.zMax = m.zMax; r.ipsMin = m.ipsMin; r.ipsMax = m.ipsMax;
        r.label = m.label; r.color = m.color; r.sound = m.sound;
        r.level = levelFromName(m.level);
        r.enabled = m.enabled;
        t.rows.push_back(r);
    }
    for (const CaseRuleCfg& m : caseRows) {
        birx::CaseRule c;
        c.minCount = m.minCount; c.ofLabel = m.ofLabel; c.label = m.label;
        c.color = m.color; c.sound = m.sound; c.level = levelFromName(m.level);
        c.enabled = m.enabled;
        if (c.label.empty()) continue;
        t.caseRules.push_back(c);
    }
    t.caseHigh = caseHigh; t.caseLow = caseLow; t.caseClose = caseClose;   // H7-2.14.0
    // QString("%1").arg(double, 0, 'f', 0) == "%.0f"
    char v[96];
    std::snprintf(v, sizeof v, "rules/%drows/%dcase/z<=%.0f",
                  int(t.rows.size()), int(t.caseRules.size()), noisyThreshold);
    t.version = v;
    if (caseHigh > 0 || caseLow > 0) t.version += "/h" + std::to_string(caseHigh) + "l" + std::to_string(caseLow);   // H7-2.14.0
    return t;
}

// --------------------------------------------------------------------------
// Serialisation. Short keys: the whole blob has to fit in half the EEPROM.

namespace {

std::string esc(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '|' || c == '%' || c == '\n' || c == '\r' || c == '=') {
            char b[4]; std::snprintf(b, sizeof b, "%%%02X", (unsigned char)c); o += b;
        } else o.push_back(c);
    }
    return o;
}

std::string unesc(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const char h[3] = { s[i + 1], s[i + 2], 0 };
            o.push_back(char(std::strtol(h, nullptr, 16)));
            i += 2;
        } else o.push_back(s[i]);
    }
    return o;
}

std::vector<std::string> splitBar(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == '|') { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    out.push_back(cur);
    return out;
}

double num(const std::string& s, double fb)
{
    double d = 0;
    return birx::birxStrtod(s, &d) ? d : fb;
}

uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

const uint32_t kMagic = 0x31585242u;   // "BRX1"
struct SlotHeader { uint32_t magic; uint32_t seq; uint32_t len; uint32_t crc; };

} // namespace

std::string ConfigStore::serialise(const Settings& s)
{
    std::ostringstream o;
    o << "id=" << esc(s.deviceId) << "\n"
      << "nr=" << esc(s.needleRev) << "\n"
      << "nc=" << (s.needleCheck ? 1 : 0) << "\n"
      << "pc=" << esc(s.passcode) << "\n"
      << "ds=" << esc(s.decisionSource) << "\n"
      << "pr=" << s.readings << "|" << (s.earlyStop ? 1 : 0) << "|" << s.minForEarlyStop
      << "|" << fmtNum(s.maxSpreadIps) << "|" << fmtNum(s.fatThresholdOhm) << "\n"
      << "rq=" << (s.reqNeedleCal ? 1 : 0) << (s.reqIntake ? 1 : 0)
      << (s.reqRangeCal ? 1 : 0) << (s.reqLotFile ? 1 : 0) << (s.reqTissueRef ? 1 : 0)
      << (s.rulesAirCal ? 1 : 0) << "\n"
      << "sn=" << (s.muted ? 1 : 0) << "|" << s.volume << "|" << s.backlight
      << "|" << s.utcOffsetMin << "|" << s.sweepTrack << "\n"
      << "ci=" << s.caseDay << "|" << s.caseSeq << "\n"
      << "tv=" << kRulesTableVersion << "\n"
      << "ck=" << s.salineCheck << "|" << s.checkSolution << "|" << s.settleS << "|" << (s.matOnMain ? 1 : 0) << "\n";
    if (!s.trainNeedle.empty()) o << "tn=" << esc(s.trainNeedle) << "\n";
    if (!s.trainLot.empty()) o << "tl=" << esc(s.trainLot) << "\n";
    o << "qc=" << s.qcSweeps << "|" << s.qcMinutes << "|" << s.qcAirMinutes << "\n";
    o << "st=" << fmtNum(s.salineTempC) << "\n";                                 // H7-2.15.0
    if (std::isfinite(s.floorIps))
        o << "fl=" << fmtNum(s.floorIps) << "|" << fmtNum(s.floorZ) << "|" << fmtNum(s.floorDrift) << "|" << fmtNum(s.floorLeak) << "\n";
    if (!s.diagFile.empty()) o << "dg=" << esc(s.diagFile) << "\n";
    const RulesConfig& r = s.rules;
    o << "rf=" << fmtNum(r.noisyThreshold) << "|" << esc(r.defaultColor) << "|"
      << esc(r.defaultSound) << "|" << esc(r.noisyColor) << "|" << esc(r.noisySound)
      << "|" << esc(r.defaultLabel) << "|" << esc(r.noisyLabel) << "\n";
    for (const RuleRowCfg& w : r.rows)
        o << "R=" << fmtNum(w.zMin) << "|" << fmtNum(w.zMax) << "|" << fmtNum(w.ipsMin)
          << "|" << fmtNum(w.ipsMax) << "|" << (w.enabled ? 1 : 0) << "|" << esc(w.level)
          << "|" << esc(w.color) << "|" << esc(w.sound) << "|" << esc(w.label) << "\n";
    o << "cc=" << r.caseHigh << "|" << r.caseLow << "|" << (r.caseClose ? 1 : 0) << "\n";   // H7-2.14.0
    for (const CaseRuleCfg& c : r.caseRows)
        o << "C=" << c.minCount << "|" << (c.enabled ? 1 : 0) << "|" << esc(c.level) << "|"
          << esc(c.color) << "|" << esc(c.sound) << "|" << esc(c.ofLabel) << "|"
          << esc(c.label) << "\n";
    return o.str();
}

bool ConfigStore::parse(const std::string& text, Settings* out)
{
    Settings s;                       // defaults for anything absent
    s.rules.rows.clear();
    s.rules.caseRows.clear();
    bool sawRules = false;
    int tableVersion = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const std::vector<std::string> f = splitBar(v);
        if (k == "id") s.deviceId = unesc(v);
        else if (k == "dg") s.diagFile = unesc(v);
        else if (k == "nr") s.needleRev = unesc(v);
        else if (k == "nc") s.needleCheck = v == "1";
        else if (k == "pc") s.passcode = unesc(v);
        else if (k == "ds") s.decisionSource = unesc(v);
        else if (k == "tn") s.trainNeedle = unesc(v);
        else if (k == "tl") s.trainLot = unesc(v);
        else if (k == "qc" && f.size() >= 3) {
            s.qcSweeps = std::max(1, std::min(200, int(num(f[0], 10))));
            s.qcMinutes = std::max(1, std::min(600, int(num(f[1], 30))));
            s.qcAirMinutes = std::max(1, std::min(1440, int(num(f[2], 60))));
        }
        else if (k == "st") { const double t = num(v, 22.0); if (t >= 5 && t <= 45) s.salineTempC = t; }
        else if (k == "tv") tableVersion = int(num(v, 0));
        else if (k == "ck" && f.size() >= 2) {
            s.salineCheck = std::max(0, std::min(2, int(num(f[0], 2))));
            s.checkSolution = int(num(f[1], 0)) == 1 ? 1 : 0;
            if (f.size() >= 3) s.settleS = std::max(0, std::min(60, int(num(f[2], 10))));
            if (f.size() >= 4) s.matOnMain = f[3] != "0";
        }
        else if (k == "fl" && f.size() >= 4) {
            s.floorIps = num(f[0], birx::kNaN); s.floorZ = num(f[1], birx::kNaN);
            s.floorDrift = num(f[2], birx::kNaN); s.floorLeak = num(f[3], birx::kNaN);
        }
        else if (k == "pr" && f.size() >= 5) {
            s.readings = int(num(f[0], 5)); s.earlyStop = f[1] == "1";
            s.minForEarlyStop = int(num(f[2], 3)); s.maxSpreadIps = num(f[3], 4.0);
            s.fatThresholdOhm = num(f[4], 1900.0);
        } else if (k == "rq" && v.size() >= 4) {
            s.reqNeedleCal = v[0] == '1'; s.reqIntake = v[1] == '1';
            s.reqRangeCal = v[2] == '1'; s.reqLotFile = v[3] == '1';
            s.reqTissueRef = v.size() >= 5 && v[4] == '1';
            s.rulesAirCal = v.size() >= 6 && v[5] == '1';
        } else if (k == "sn" && f.size() >= 4) {
            s.muted = f[0] == "1"; s.volume = int(num(f[1], 20));
            s.backlight = int(num(f[2], 150)); s.utcOffsetMin = int(num(f[3], 0));
            if (f.size() >= 5) s.sweepTrack = std::max(0, std::min(12, int(num(f[4], 7))));
        } else if (k == "ci" && f.size() >= 2) {
            s.caseDay = int(num(f[0], -1)); s.caseSeq = int(num(f[1], 0));
        } else if (k == "rf" && f.size() >= 7) {
            sawRules = true;
            s.rules.noisyThreshold = num(f[0], 5000.0);
            s.rules.defaultColor = unesc(f[1]); s.rules.defaultSound = unesc(f[2]);
            s.rules.noisyColor = unesc(f[3]);   s.rules.noisySound = unesc(f[4]);
            s.rules.defaultLabel = unesc(f[5]); s.rules.noisyLabel = unesc(f[6]);
        } else if (k == "R" && f.size() >= 9) {
            RuleRowCfg w;
            w.zMin = num(f[0], 0); w.zMax = num(f[1], 0);
            w.ipsMin = num(f[2], 0); w.ipsMax = num(f[3], 0);
            w.enabled = f[4] == "1"; w.level = unesc(f[5]); w.color = unesc(f[6]);
            w.sound = unesc(f[7]); w.label = unesc(f[8]);
            s.rules.rows.push_back(w);
        } else if (k == "cc" && f.size() >= 3) {      // H7-2.14.0
            s.rules.caseHigh = std::max(0, std::min(20, int(num(f[0], 0))));
            s.rules.caseLow = std::max(0, std::min(20, int(num(f[1], 0))));
            s.rules.caseClose = f[2] != "0";
        } else if (k == "C" && f.size() >= 7) {
            CaseRuleCfg c;
            c.minCount = int(num(f[0], 2)); c.enabled = f[1] == "1"; c.level = unesc(f[2]);
            c.color = unesc(f[3]); c.sound = unesc(f[4]); c.ofLabel = unesc(f[5]);
            c.label = unesc(f[6]);
            s.rules.caseRows.push_back(c);
        }
    }
    if (!sawRules) s.rules = RulesConfig::defaults();
    else if (tableVersion < 2) {
        // H7-1.5.0: the table asked for from the bench replaces the stored one,
        // once. Saved with the new version number, it is left alone after that.
        s.rules = RulesConfig::defaults();
        s.rulesReplaced = true;
    } else if (tableVersion < 3) {
        // H7-1.7.0: a row with no sound gets the one for its level; a row that
        // has one keeps it, and nothing else in the table is touched.
        for (RuleRowCfg& w : s.rules.rows) {
            if (!w.sound.empty()) continue;
            birx::Tier t = levelFromName(w.level);
            if (t == birx::Tier::Invalid) t = birx::RuleTable::tierOfLabel(w.label);
            w.sound = t == birx::Tier::High ? kSoundHigh : t == birx::Tier::Low ? kSoundLow
                    : t == birx::Tier::Suspicious ? kSoundSuspicious : kSoundNoisy;
        }
        s.rulesReplaced = true;
    }
    *out = s;
    return true;
}

size_t ConfigStore::capacity()
{
    return plat::nvSize() / 2 - sizeof(SlotHeader);
}

bool ConfigStore::load(Settings* s, std::string* why)
{
    const size_t half = plat::nvSize() / 2;
    int best = -1;
    uint32_t bestSeq = 0;
    std::string bestText;
    for (int slot = 0; slot < 2; ++slot) {
        SlotHeader h{};
        if (!plat::nvRead(uint32_t(slot * half), &h, sizeof h)) continue;
        if (h.magic != kMagic || h.len == 0 || h.len > half - sizeof h) continue;
        std::string t(h.len, '\0');
        if (!plat::nvRead(uint32_t(slot * half + sizeof h), &t[0], h.len)) continue;
        if (crc32(reinterpret_cast<const uint8_t*>(t.data()), t.size()) != h.crc) continue;
        if (best < 0 || h.seq > bestSeq) { best = slot; bestSeq = h.seq; bestText = t; }
    }
    if (best < 0) {
        if (why) *why = "The settings memory holds no saved settings (a new board, or both "
                        "copies were damaged). The defaults are in force.";
        return false;
    }
    m_seq = bestSeq;
    m_bytes = bestText.size();
    m_text = bestText;
    return parse(bestText, s);
}

bool ConfigStore::save(const Settings& s, std::string* why)
{
    const std::string t = serialise(s);
    const size_t half = plat::nvSize() / 2;
    char b[256];
    if (t.size() > half - sizeof(SlotHeader)) {
        std::snprintf(b, sizeof b,
            "The settings no longer fit in the instrument's memory: %u bytes against "
            "room for %u. Remove a rule row or shorten a label, then save again.",
            unsigned(t.size()), unsigned(half - sizeof(SlotHeader)));
        if (why) *why = b;
        return false;
    }
    // Write to the slot that does NOT hold the newest copy, so a power cut in
    // the middle of this write leaves the previous settings intact.
    const uint32_t seq = m_seq + 1;
    const int slot = int(seq & 1u);
    SlotHeader h{ kMagic, seq, uint32_t(t.size()),
                  crc32(reinterpret_cast<const uint8_t*>(t.data()), t.size()) };
    const uint32_t base = uint32_t(slot * half);
    if (!plat::nvWrite(base + sizeof h, t.data(), t.size()) ||
        !plat::nvWrite(base, &h, sizeof h)) {
        if (why) *why = "The settings could NOT be written to the settings memory "
                        "(the I2C EEPROM did not accept or did not return the data). "
                        "Nothing set now will survive a restart.";
        return false;
    }
    // Read back the whole slot through the same path load() uses.
    SlotHeader rh{};
    std::string rt(t.size(), '\0');
    if (!plat::nvRead(base, &rh, sizeof rh) || !plat::nvRead(base + sizeof rh, &rt[0], rt.size()) ||
        std::memcmp(&rh, &h, sizeof h) != 0 || rt != t) {
        if (why) *why = "The settings were written but read back DIFFERENT from what was "
                        "written. The settings memory is failing; nothing set now can be trusted "
                        "to survive a restart.";
        return false;
    }
    m_seq = seq;
    m_bytes = t.size();
    m_text = t;
    return true;
}

} // namespace app
