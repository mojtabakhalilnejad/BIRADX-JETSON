// birx/needlecode.h — the needle's lot and serial, and the promise that each
// needle is used once.
//
//     K4M2 - 9F7T0 - 3BQZ
//     lot    serial  check
//
// Crockford base32 throughout, same alphabet as the case id, so nothing reads
// as 0/1 confusion and the two identifiers look like they belong together.
//
//   lot     4 chars   the manufacturing batch. Printed on the box, typed once,
//                     then remembered — every needle out of the same box shares
//                     it, so the operator types 5+4 characters per needle, not
//                     13.
//   serial  5 chars   33.5 million per lot. Printed on the individual packet.
//   check   4 chars   the first 20 bits of HMAC-SHA256(key, "NDL1|lot|serial").
//
// WHY A MAC AND NOT A CHECKSUM
//
// The requirement is that an operator cannot invent a code. A checksum does not
// do that: the rule is public, so anyone who works it out can generate valid
// codes all day. A MAC with a key the operator does not have means a typed
// guess passes with probability 1 in 32^4 = 1 in 1,048,576, and there is no
// pattern to learn from the codes on the packets.
//
// BE CLEAR ABOUT WHAT THIS IS NOT
//
// The key lives in this device's software. Anyone who can read the device's
// filesystem can extract it and mint codes. This stops an operator typing
// digits to get past a prompt; it does NOT stop a counterfeiter, and it must
// never be described as if it did. Real protection against refilled or
// counterfeit needles needs either a per-needle secret the device cannot
// produce (a crypto element in the connector) or an online check — both are
// hardware or connectivity changes, and neither is in this build.
//
// Two practical consequences of the key being in software:
//   * the key is loaded from a file at startup, NOT compiled in, so it can be
//     rotated without shipping new software, and so a build of this source
//     with no key file cannot validate anything (it refuses, rather than
//     accepting everything);
//   * codes carry the key VERSION in the lot's first character, so a fleet
//     mid-rotation accepts both.
//
// SINGLE USE
//
// Every accepted serial is written to an append-only register on the device.
// A serial already in the register is refused for the life of the device. The
// register is local: it stops the same needle being reused on THIS device,
// which is the reuse that actually happens. It cannot see a needle reused on a
// different device, because the device is offline — say that plainly rather
// than implying a fleet-wide guarantee.

#ifndef BIRX_NEEDLECODE_H
#define BIRX_NEEDLECODE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace birx {

// --------------------------------------------------------------------------
// SHA-256 and HMAC-SHA256, self-contained so the device build has no crypto
// dependency. Verified in the self-test against the RFC 4231 vectors.
std::string sha256Hex(const std::string& data);
std::vector<uint8_t> hmacSha256(const std::string& key, const std::string& data);

// --------------------------------------------------------------------------
enum class NeedleStatus {
    Ok,
    NoKey,          // this device has no needle key installed
    BadFormat,      // wrong length, or a character outside the alphabet
    BadCheck,       // the check characters do not match — INVALID
    AlreadyUsed     // a real code, but this needle has been used here before
};

const char* needleStatusName(NeedleStatus s);

// --------------------------------------------------------------------------
class NeedleKeyring {
public:
    // Lines of "version key-hex". Version is one Crockford base32 character
    // and must match the first character of the lot.
    bool load(const std::string& path);
    void addKey(char version, const std::string& keyHex);   // for tests
    bool empty() const { return m_keys.empty(); }

    // lot and serial as typed: dashes, spaces and lower case are all fine.
    // Writes the normalised "LOTSERIALCHECK" to *canonical when non-null.
    NeedleStatus check(const std::string& lot, const std::string& serial,
                       std::string* canonical = nullptr) const;

    // Mint a code — used by the packing line and by the self-test, never on
    // the device. Returns "LOT-SERIAL-CHECK".
    std::string mint(const std::string& lotBody4, const std::string& serial5) const;

private:
    std::map<char, std::string> m_keys;   // version -> raw key bytes
};

// --------------------------------------------------------------------------
// The append-only register of needles this device has already accepted.
class NeedleRegistry {
public:
    explicit NeedleRegistry(std::string path) : m_path(std::move(path)) {}

    bool load();
    bool used(const std::string& canonical) const;
    // Appends. Returns false on I/O failure — and a needle that could not be
    // recorded must not be accepted, or the single-use promise is empty.
    bool markUsed(const std::string& canonical, const std::string& caseId,
                  const std::string& isoTime);

    size_t size() const { return m_used.size(); }
    // canonical -> case id it was used on
    const std::map<std::string, std::string>& all() const { return m_used; }

private:
    std::string m_path;
    std::map<std::string, std::string> m_used;
};

} // namespace birx
#endif
