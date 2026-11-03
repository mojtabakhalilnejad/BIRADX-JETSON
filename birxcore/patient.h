// birx/patient.h — the case identifier, and what is asked at the start of a test.
//
// ============================ THE CASE ID ==================================
//
//     A3K7-F2M-0C9        device(4) · day(3) · sequence(2) · check(1)
//
// Crockford base32 throughout (0-9 A-Z without I, L, O, U), so nothing in an
// identifier can be confused with 0/1 or read as a word. Ten characters, three
// groups, written on a pathology request form and typed back weeks later.
//
// WHY THE DEVICE PART IS ASSIGNED AND NOT DERIVED
//
// The obvious shortcut is to hash a MAC address or CPU serial into four
// characters. Over 100,000 devices that is catastrophic — the birthday bound
// gives about 4,800 expected collisions, and P(at least one) rounds to 1. A
// derived id would need eleven characters to be safe at that fleet size.
//
// An ASSIGNED four-character id has ZERO collisions by construction, and
// 1,048,576 values is ten times the fleet. So the id is provisioned once, at
// manufacture or first setup, and this code REFUSES to mint case ids without
// one rather than silently falling back to something that collides.
//
// What is left after that:
//   * two devices sharing an id by a provisioning mistake collide only if they
//     also test on the same day at the same sequence number — roughly 1% per
//     shared day at ten tests a day
//   * a storage wipe that resets the counter collides only within one device
//     on one day, because the day is in the id
//
// The check character is a position-weighted sum mod 32. It catches every
// single-character substitution and every transposition of two different
// characters, which are the two ways a hand-copied id goes wrong.

#ifndef BIRX_PATIENT_H
#define BIRX_PATIENT_H

#include "types.h"
#include <map>
#include <string>
#include <vector>

namespace birx {

// ---------------------------------------------------------------------------
class CaseIdGenerator {
public:
    // deviceId: four Crockford base32 characters, assigned. Empty or malformed
    // means no case ids can be minted, which is the intended failure.
    bool setDeviceId(const std::string& deviceId, std::string* why = nullptr);
    const std::string& deviceId() const { return m_deviceId; }
    bool ready() const { return m_deviceId.size() == 4; }

    // daysSinceEpoch: whole days since 2024-01-01 UTC.
    // Returns an empty string when not ready.
    std::string next(int daysSinceEpoch);

    // Sequence state, persisted so a restart does not reuse numbers.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    static bool validate(const std::string& caseId);        // checks shape + check char
    static std::string normalise(const std::string& typed); // strips dashes, fixes I/L/O/U

    static constexpr int kMaxPerDay = 1024;   // 2 base32 characters

    // MCU port (BIRADx-H7): the day/sequence state, so it can also be kept in
    // the settings memory, which a firmware update or a swapped SD card does
    // not reset. Additive; nothing above changes.
    int  day() const { return m_day; }
    int  seq() const { return m_seq; }
    void restore(int day, int seq) { m_day = day; m_seq = seq < 0 ? 0 : seq; }

private:
    std::string m_deviceId;
    int m_day = -1;
    int m_seq = 0;
};

// ---------------------------------------------------------------------------
// The intake form, defined as data so the QML renders it generically and the
// validation lives here where it can be tested. Adding a field is a change to
// one table, not to the interface.
// ---------------------------------------------------------------------------
enum class FieldType { Number, Choice, Text, Toggle };

struct IntakeField {
    std::string id;
    std::string label;
    std::string help;          // one short line, shown under the control
    FieldType   type = FieldType::Number;
    std::string unit;
    bool        required = false;
    double      min = kNaN, max = kNaN;
    std::vector<std::string> options;   // for Choice
    int         page = 1;               // 1 = required, 2 = recommended, 3 = optional
    bool        modelCandidate = false; // may be offered to the trainer
    bool        stratifier = false;     // used to SPLIT the analysis, not as a feature
};

const std::vector<IntakeField>& intakeSchema();
const IntakeField* intakeField(const std::string& id);

// ---------------------------------------------------------------------------
struct PatientRecord {
    std::string caseId;        // system-generated, the key everything joins on
    std::string nationalId;    // entered separately, optional, never the key
    std::string operatorId;
    // Needle provenance. needleCode is the canonical LOT+SERIAL, written by
    // the bridge only after the MAC verified and the register accepted it.
    std::string needleCode;
    std::string startedAt;     // ISO
    std::map<std::string, std::string> values;   // field id -> raw value

    void   set(const std::string& id, const std::string& value);
    std::string get(const std::string& id) const;
    double number(const std::string& id) const;   // NaN when absent or unparsable

    // Every required field on page 1 that is still empty or out of range.
    std::vector<std::string> missing() const;
    bool complete() const { return missing().empty(); }

    static std::string csvHeader();
    std::string csvRow() const;
};

} // namespace birx
#endif
