// app/config.h — the per-unit settings and the rule table, and where they live.
//
// Both are kept in the 2 KB I2C EEPROM on the carrier board, as one text blob
// written alternately into two slots with a sequence number and a CRC. A new
// firmware image never touches the EEPROM, so nothing set on the instrument
// is lost by an update (BIRADx_PORTING_SPEC.md §16.2). Every save is read back
// and compared; a save that did not stick is reported on screen.
#ifndef BIRADX_CONFIG_H
#define BIRADX_CONFIG_H

#include "../birxcore/decision.h"
#include <string>
#include <vector>

namespace app {

// One row of the rule table as the site edits it (2.18.0 MarginRow).
struct RuleRowCfg {
    double zMin = 0, zMax = 0, ipsMin = 0, ipsMax = 0;
    std::string label, color, sound;
    std::string level;          // "low" / "suspicious" / "high" / anything else = unset
    bool enabled = true;
    bool isEmptyRange() const { return zMin > zMax || ipsMin > ipsMax; }
};

struct CaseRuleCfg {
    int minCount = 2;
    std::string ofLabel, label, color, sound, level;
    bool enabled = true;
};

// What a reading that no enabled row matches is called. Not editable: it is not an answer.
constexpr const char* kNoRuleLabel = "No rule";

// Bumped when the shipped table is one that must replace the stored one.
constexpr int kRulesTableVersion = 3;

// The voice module's recordings for each answer (names as App::soundLibrary).
constexpr const char* kSoundHigh       = "Track 1 (positive)";
constexpr const char* kSoundSuspicious = "Track 5 (suspicious)";
constexpr const char* kSoundLow        = "Track 6 (negative)";
constexpr const char* kSoundNoisy      = "Track 12 (noisy)";

// The table with its fallbacks (2.18.0 MarginConfig).
struct RulesConfig {
    std::vector<RuleRowCfg> rows;
    double      noisyThreshold = 5000.0;
    std::string defaultLabel = "Low Risk", defaultColor = "#2E7D32", defaultSound;
    std::string noisyLabel = "Noisy", noisyColor, noisySound;
    std::vector<CaseRuleCfg> caseRows;
    // H7-2.14.0: the case result by count (High first; 0 = off), and whether a final case takes no more readings
    int  caseHigh = 0, caseLow = 0;
    bool caseClose = true;

    static RulesConfig defaults();                 // the shipped table, §7.3
    std::vector<std::string> problems() const;     // overlaps, empty ranges, no label
    birx::RuleTable toRuleTable(bool active) const;
};

birx::Tier levelFromName(const std::string& s);
const char* levelName(birx::Tier t);               // "low" ... or "invalid"

struct Settings {
    // identity
    std::string deviceId;                 // birx/device_id — empty until provisioned
    std::string needleRev = "NDL-D";      // birx/needle
    bool        needleCheck = false;      // birx/needle_check
    std::string passcode = "7324";        // security/passcode
    std::string decisionSource = "rules"; // decision/source
    // protocol
    int    readings = 5;                  // protocol/readings
    bool   earlyStop = true;              // protocol/early_stop
    int    minForEarlyStop = 3;           // protocol/min_for_early_stop
    double maxSpreadIps = 4.0;            // protocol/max_spread_ips
    double fatThresholdOhm = 1900.0;      // protocol/fat_threshold_ohm
    // what blocks a reading
    bool reqNeedleCal = true;             // require/needle_cal
    // H7-2.1.0: the rule ranges were found with needles calibrated in air
    // (true) or as they came (false). Decides whether this instrument uses the
    // six-second air calibration on a reading at all.
    // Default OFF: the bench's trials were run on the previous software, which
    // had no air calibration (asked 2026-10-11).
    bool rulesAirCal = false;
    bool reqIntake = false;               // require/intake
    bool reqRangeCal = false;             // require/range_cal
    bool reqLotFile = false;              // require/lot_file
    bool reqTissueRef = false;            // the healthy-tissue sample of the patient, before the first reading
    // H7-2.3.0 / 2.4.0: the needle check before each case (Settings -> Needle): 0 off (the bench only), 1 the
    // needles of a lot that needs it (approved per needle), 2 every needle (H7-2.10.0: its QC, the default)
    int  salineCheck = 2;
    int  checkSolution = 0;               // the check saline: 0 = 0.9 % (birx::kCheck9), 1 = 0.09 % (kCheck09)
    int  settleS = 10;                    // seconds the tip settles in a saline before its sweeps (0..60)
    double salineTempC = 22.0;            // H7-2.15.0: the check saline's temperature, as last entered (5..45)
    // H7-2.5.0: the last sweep compared with the material library on the main screen
    bool matOnMain = true;
    // H7-2.9.0: the number of the needle the Training sweeps are taken with (asked for before any
    // sweep there; recorded with every sweep, its Air check and its saline reference)
    std::string trainNeedle;
    // H7-2.10.0: the Training needle's lot (its QC limits; "" none: the generic ones), and how often its
    // checks are due: a saline check every qcSweeps sweeps or qcMinutes minutes, an Air check every qcAirMinutes
    std::string trainLot;
    int  qcSweeps = 10, qcMinutes = 30, qcAirMinutes = 60;
    // the reference needle's own repeatability between sessions (a control run in Lots):
    // no lot is judged tighter than this
    double floorIps = birx::kNaN, floorZ = birx::kNaN, floorDrift = birx::kNaN, floorLeak = birx::kNaN;
    // sound and screen
    bool muted = false;
    int  volume = 20;                     // voice module, 5..30
    int  sweepTrack = 7;                  // repeats while a sweep runs; 0 = silent (the previous firmware used 7)
    int  backlight = 150;                 // 20..200
    int  utcOffsetMin = 0;                // the battery clock keeps local time
    // case ids (also mirrored to records/caseid_state.txt)
    int  caseDay = -1, caseSeq = 0;
    // diagnostic mode: the file it is recording to ("" = off); survives a restart
    std::string diagFile;
    // the rule table
    RulesConfig rules = RulesConfig::defaults();
    // not stored: set by parse() when an older stored table was just replaced
    bool rulesReplaced = false;
};

class ConfigStore {
public:
    // Loads the newest valid slot. Returns false (and leaves the defaults) when
    // the EEPROM holds nothing valid, which is the state of a new board.
    bool load(Settings* s, std::string* why = nullptr);
    // Serialises, writes to the older slot, reads it back. On failure the
    // reason names what went wrong, with the numbers.
    bool save(const Settings& s, std::string* why = nullptr);

    static std::string serialise(const Settings& s);
    static bool        parse(const std::string& text, Settings* s);

    int  lastSeq() const { return m_seq; }
    size_t lastBytes() const { return m_bytes; }
    std::string lastText() const { return m_text; }
    static size_t capacity();                     // bytes of text a slot can hold
private:
    uint32_t m_seq = 0;
    size_t   m_bytes = 0;
    std::string m_text;
};

std::string fmtNum(double v);                     // round-trip text for a double

} // namespace app

#endif
