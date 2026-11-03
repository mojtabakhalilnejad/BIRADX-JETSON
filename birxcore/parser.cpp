#include "parser.h"
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <vector>

namespace birx {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

bool startsWithNoCase(const std::string& s, const char* pre) {
    size_t i = 0;
    for (; pre[i]; ++i) {
        if (i >= s.size()) return false;
        if (std::toupper(static_cast<unsigned char>(s[i])) !=
            std::toupper(static_cast<unsigned char>(pre[i]))) return false;
    }
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) { out.push_back(trim(s.substr(start, i - start))); start = i + 1; }
    }
    return out;
}

} // namespace

// Accepts ASCII and Eastern-Arabic/Persian digits; drops thousands separators
// and zero-width joiners; rejects anything that is not then a plain number.
// Deliberately strict: a malformed point must be dropped, never coerced to 0.
bool Parser::parseNumber(const std::string& s, double* out) {
    std::string n;
    n.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c == ' ' || c == '\t' || c == ',') { ++i; continue; }
            n.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        // decode a 2- or 3-byte UTF-8 sequence far enough to spot the digit blocks
        unsigned cp = 0; int len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else { return false; }
        if (i + len > s.size()) return false;
        for (int k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i+k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += len;
        if (cp >= 0x06F0 && cp <= 0x06F9)      n.push_back(char('0' + (cp - 0x06F0)));
        else if (cp >= 0x0660 && cp <= 0x0669) n.push_back(char('0' + (cp - 0x0660)));
        else if (cp == 0x066B)                 n.push_back('.');
        else if (cp == 0x00A0 || cp == 0x200C || cp == 0x066C) { /* skip */ }
        else return false;
    }
    if (n.empty()) return false;

    // one optional sign, digits with at most one dot, optional exponent
    size_t i = 0;
    if (n[i] == '+' || n[i] == '-') ++i;
    bool digits = false, dot = false;
    for (; i < n.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(n[i]))) { digits = true; continue; }
        if (n[i] == '.' && !dot) { dot = true; continue; }
        if ((n[i] == 'e' || n[i] == 'E') && digits) {
            ++i;
            if (i < n.size() && (n[i] == '+' || n[i] == '-')) ++i;
            bool expDigits = false;
            for (; i < n.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(n[i]))) return false;
                expDigits = true;
            }
            if (!expDigits) return false;
            break;
        }
        return false;
    }
    if (!digits) return false;

    char* end = nullptr;
    const double v = std::strtod(n.c_str(), &end);
    if (end == n.c_str() || !std::isfinite(v)) return false;
    if (out) *out = v;
    return true;
}

int Parser::feed(const char* data, size_t len) {
    int lines = 0;
    for (size_t i = 0; i < len; ++i) {
        const char c = data[i];
        if (c == '\n' || c == '\r') {
            if (!m_buf.empty()) { feedLine(m_buf); m_buf.clear(); ++lines; }
        } else if (c != '\0') {
            m_buf.push_back(c);
            if (m_buf.size() > 4096) m_buf.clear();   // never grow without bound
        }
    }
    return lines;
}

void Parser::feedLine(const std::string& line) {
    if (line.empty()) return;

    // Lift out any [C]/[B]/[E]/[S] markers but keep the rest of the line: the
    // instrument merges "IPS...,Z..." and "[E]" onto one line often enough
    // that discarding such a line loses a whole measurement.
    std::vector<Marker> markers;
    std::string rest;
    rest.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '[' && i + 2 < line.size() && line[i+2] == ']') {
            switch (line[i+1]) {
            case 'C': markers.push_back(Marker::Ready);   i += 2; continue;
            case 'B': markers.push_back(Marker::Begin);   i += 2; continue;
            case 'E': markers.push_back(Marker::End);     i += 2; continue;
            case 'S': markers.push_back(Marker::Stopped); i += 2; continue;
            default: break;
            }
        }
        if (line[i] == '[' || line[i] == ']') continue;   // outer brackets of a data packet
        rest.push_back(line[i]);
    }
    rest = trim(rest);

    if (!rest.empty()) {
        std::vector<std::string> f = split(rest, ',');

        if (startsWithNoCase(rest, "IPS")) {
            // IPS<v>,Z<v>,<text> — located by prefix, not by position, because
            // the text field may itself be empty or contain spaces.
            double ips = 0, z = 0; bool haveI = false, haveZ = false;
            std::string text;
            for (const std::string& fld : f) {
                if (fld.empty()) continue;
                if (startsWithNoCase(fld, "IPS"))      haveI = parseNumber(fld.substr(3), &ips);
                else if (std::toupper(static_cast<unsigned char>(fld[0])) == 'Z' &&
                         parseNumber(fld.substr(1), &z)) haveZ = true;
                else if (text.empty())                 text = fld;
            }
            if (haveI && haveZ) { if (onReading) onReading(ips, z, text); }
            else if (onUnparsed) onUnparsed(rest);

        } else if (f.size() >= 3) {
            std::string f0 = f[0];
            if (startsWithNoCase(f0, "imp")) f0 = f0.substr(3);

            double fr = 0, mg = 0, ph = 0;
            if (parseNumber(f0, &fr) && parseNumber(f[1], &mg) && parseNumber(f[2], &ph)) {
                Point p;
                p.freqHz   = fr / 10.0;     // the firmware sends f*10
                p.magOhm   = mg / 10.0;     // and |Z|*10
                p.phaseDeg = ph;
                p.range    = 0;
                if (f.size() >= 4) {
                    double r = 0;
                    if (parseNumber(f[3], &r) && r >= 1 && r <= 6) p.range = static_cast<int>(r);
                }
                if (onPoint) onPoint(p);
            } else if (onUnparsed) {
                onUnparsed(rest);
            }
        } else if (onUnparsed) {
            onUnparsed(rest);
        }
    }

    // Markers last: data on a merged line always precedes its end marker.
    if (onMarker) for (Marker m : markers) onMarker(m);
}

} // namespace birx
