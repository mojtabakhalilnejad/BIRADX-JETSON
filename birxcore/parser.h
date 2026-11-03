// birx/parser.h — the wire protocol, parsed completely.
//
// What the instrument actually sends (verified against main.c):
//
//   [C]                                        link alive, UART only, ~1 Hz
//   [B]                                        sweep begins, UART only
//   [imp<f*10>,<mag*10>,<phase>,<range>]       one per frequency, 15 per sweep
//   [IPS<ips>,Z<z1k>,<text>]                   the firmware's own verdict
//   [E]                                        sweep ends, UART only
//
// The fourth field of the imp packet is muxtest+1, the gain range 1..6. The
// shipped Qt parser reads only three fields and drops it. It is needed: the
// front end applies a magnitude-dependent phase correction at 500 kHz on
// ranges 1 and 2 only, so phase is discontinuous across a range switch and
// cannot be corrected without knowing which range produced it.
//
// Packets are accepted with or without the surrounding brackets, with any
// line ending, and with the markers merged onto a data line — the instrument
// does all three.

#ifndef BIRX_PARSER_H
#define BIRX_PARSER_H

#include "types.h"
#include <functional>
#include <string>

namespace birx {

enum class Marker { Ready, Begin, End, Stopped };

class Parser {
public:
    // Callbacks. Any may be left unset.
    std::function<void(const Point&)>                          onPoint;
    std::function<void(double ips, double z1k, const std::string& text)> onReading;
    std::function<void(Marker)>                                onMarker;
    std::function<void(const std::string& raw)>                onUnparsed;

    // Feed arbitrary chunks; lines are assembled internally, so a packet split
    // across two reads is not lost. Returns the number of complete lines seen.
    int feed(const char* data, size_t len);
    int feed(const std::string& chunk) { return feed(chunk.data(), chunk.size()); }

    void feedLine(const std::string& line);
    void reset() { m_buf.clear(); }

    // Exposed for testing: strict numeric parse, tolerant of Eastern-Arabic
    // digits and stray separators, rejecting anything else.
    static bool parseNumber(const std::string& s, double* out);

private:
    std::string m_buf;
};

} // namespace birx
#endif
