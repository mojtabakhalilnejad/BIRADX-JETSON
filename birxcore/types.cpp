#include "types.h"
#include <cmath>
#include <algorithm>

#include <cerrno>
#include <cstdlib>

namespace birx {

bool birxStrtod(const std::string& s, double* out)
{
    const char* b = s.c_str();
    char* e = nullptr;
    errno = 0;
    const double d = std::strtod(b, &e);
    if (e == b) return false;           // std::invalid_argument
    if (errno == ERANGE) return false;  // std::out_of_range
    *out = d;
    return true;
}


bool Point::valid() const {
    return std::isfinite(freqHz) && freqHz > 0 &&
           std::isfinite(magOhm) && magOhm > 0 &&
           std::isfinite(phaseDeg);
}

void Spectrum::clear() {
    points.clear();
    fwIps = fwZ1k = kNaN;
    fwText.clear();
}

const Point* Spectrum::at(double freqHz, double tolHz) const {
    const Point* best = nullptr;
    double bestD = tolHz;
    for (const Point& p : points) {
        if (!p.valid()) continue;
        const double d = std::fabs(p.freqHz - freqHz);
        if (d <= bestD) { bestD = d; best = &p; }
    }
    return best;
}

std::vector<Point> Spectrum::band(double loHz, double hiHz) const {
    std::vector<Point> out;
    for (const Point& p : points)
        if (p.valid() && p.freqHz >= loHz && p.freqHz <= hiHz)
            out.push_back(p);
    std::sort(out.begin(), out.end(),
              [](const Point& a, const Point& b){ return a.freqHz < b.freqHz; });
    return out;
}

// ---------------------------------------------------------------------------

const char* flagName(Flag f) {
    switch (f) {
    case Flag::None:              return "none";
    case Flag::NoSweep:           return "no_sweep";
    case Flag::Incomplete:        return "incomplete";
    case Flag::NoZ1k:             return "no_z1k";
    case Flag::TipInFat:          return "tip_in_fat";
    case Flag::Saturated:         return "saturated";
    case Flag::PhaseImplausible:  return "phase_implausible";
    case Flag::UnstableSweep:     return "unstable_sweep";
    case Flag::RangeSwitchInBand: return "range_switch_in_band";
    case Flag::UncalibratedRange: return "uncalibrated_range";
    case Flag::FewPoints:         return "few_points";
    case Flag::LegacyDisagrees:   return "legacy_disagrees";
    }
    return "unknown";
}

const char* flagText(Flag f) {
    switch (f) {
    case Flag::NoSweep:
        return "No data arrived from the instrument. Check the cable and try again.";
    case Flag::Incomplete:
        return "The sweep is incomplete. Hold still and repeat the measurement.";
    case Flag::NoZ1k:
        return "The 1 kHz point is missing, so tip placement cannot be checked. Repeat the measurement.";
    case Flag::TipInFat:
        return "The needle tip is in fat, not in the lesion. Reposition and measure again.";
    case Flag::Saturated:
        return "The impedance is beyond the instrument's range. Check the needle connection.";
    case Flag::PhaseImplausible:
        return "The phase reading is not physically possible. Check the needle and cable.";
    case Flag::UnstableSweep:
        return "The reading moved during the sweep. Hold still and measure again.";
    case Flag::RangeSwitchInBand:
        return "The gain range changed during this reading.";
    case Flag::UncalibratedRange:
        return "A gain range was used that has not been calibrated yet.";
    case Flag::FewPoints:
        return "Fewer frequency points than usual were used.";
    case Flag::LegacyDisagrees:
        return "This verdict differs from the instrument's own.";
    default: break;
    }
    return "";
}

bool flagIsFatal(Flag f) {
    switch (f) {
    case Flag::NoSweep:
    case Flag::Incomplete:
    case Flag::NoZ1k:
    case Flag::TipInFat:
    case Flag::Saturated:
    case Flag::PhaseImplausible:
    case Flag::UnstableSweep:
        return true;
    default:
        return false;
    }
}

void Quality::add(Flag f) {
    if (f == Flag::None || has(f)) return;
    flags.push_back(f);
    if (flagIsFatal(f)) usable = false;
}

bool Quality::has(Flag f) const {
    return std::find(flags.begin(), flags.end(), f) != flags.end();
}

bool Quality::anyFatal() const {
    for (Flag f : flags) if (flagIsFatal(f)) return true;
    return false;
}

std::string Quality::summary() const {
    std::string s;
    for (size_t i = 0; i < flags.size(); ++i) {
        if (i) s += ",";
        s += flagName(flags[i]);
    }
    return s.empty() ? "ok" : s;
}

std::string Quality::operatorMessage() const {
    for (Flag f : flags) if (flagIsFatal(f)) return flagText(f);
    for (Flag f : flags) { const char* t = flagText(f); if (*t) return t; }
    return "";
}

// ---------------------------------------------------------------------------

std::vector<std::string> Features::names() {
    return { "z1k", "logZ1k", "ipsLegacy", "ipsFit", "ipsTheilSen",
             "fitRmsDeg", "curvature", "phaseAtRefDeg", "phaseMinDeg", "logFAtPhaseMin",
             "zRatio", "zRatioHigh", "nFit", "rangeAt500k", "rangeAt1k",
             "rangeSwitches",
             "coleR0", "coleRinf", "coleRatio", "coleLogFc", "coleAlpha", "coleRmsRel" };
}

double Features::byName(const std::string& n, bool* found) const {
    if (found) *found = true;
    if (n == "z1k")            return z1k;
    if (n == "logZ1k")         return logZ1k;
    if (n == "ipsLegacy")      return ipsLegacy;
    if (n == "ipsFit")         return ipsFit;
    if (n == "ipsTheilSen")    return ipsTheilSen;
    if (n == "fitRmsDeg")      return fitRmsDeg;
    if (n == "curvature")      return curvature;
    if (n == "phaseAtRefDeg")  return phaseAtRefDeg;
    if (n == "phaseMinDeg")    return phaseMinDeg;
    if (n == "logFAtPhaseMin") return logFAtPhaseMin;
    if (n == "zRatio")         return zRatio;
    if (n == "zRatioHigh")     return zRatioHigh;
    if (n == "nFit")           return static_cast<double>(nFit);
    if (n == "rangeAt500k")    return static_cast<double>(rangeAt500k);
    if (n == "rangeAt1k")      return static_cast<double>(rangeAt1k);
    if (n == "rangeSwitches")  return static_cast<double>(rangeSwitches);
    if (n == "coleR0")         return coleR0;
    if (n == "coleRinf")       return coleRinf;
    if (n == "coleRatio")      return coleRatio;
    if (n == "coleLogFc")      return coleLogFc;
    if (n == "coleAlpha")      return coleAlpha;
    if (n == "coleRmsRel")     return coleRmsRel;
    if (found) *found = false;
    return kNaN;
}

bool Features::setByName(const std::string& n, double v) {
    if (n == "z1k")            { z1k = v;            return true; }
    if (n == "logZ1k")         { logZ1k = v;         return true; }
    if (n == "ipsLegacy")      { ipsLegacy = v;      return true; }
    if (n == "ipsFit")         { ipsFit = v;         return true; }
    if (n == "ipsTheilSen")    { ipsTheilSen = v;    return true; }
    if (n == "fitRmsDeg")      { fitRmsDeg = v;      return true; }
    if (n == "curvature")      { curvature = v;      return true; }
    if (n == "phaseAtRefDeg")  { phaseAtRefDeg = v;  return true; }
    if (n == "phaseMinDeg")    { phaseMinDeg = v;    return true; }
    if (n == "logFAtPhaseMin") { logFAtPhaseMin = v; return true; }
    if (n == "zRatio")         { zRatio = v;         return true; }
    if (n == "zRatioHigh")     { zRatioHigh = v;     return true; }
    if (n == "nFit")           { nFit = int(v);          return true; }
    if (n == "rangeAt500k")    { rangeAt500k = int(v);   return true; }
    if (n == "rangeAt1k")      { rangeAt1k = int(v);     return true; }
    if (n == "rangeSwitches")  { rangeSwitches = int(v); return true; }
    if (n == "coleR0")         { coleR0 = v;         return true; }
    if (n == "coleRinf")       { coleRinf = v;       return true; }
    if (n == "coleRatio")      { coleRatio = v;      return true; }
    if (n == "coleLogFc")      { coleLogFc = v;      return true; }
    if (n == "coleAlpha")      { coleAlpha = v;      return true; }
    if (n == "coleRmsRel")     { coleRmsRel = v;     return true; }
    return false;
}

const char* tierName(Tier t) {
    switch (t) {
    case Tier::Invalid:    return "Invalid";
    case Tier::Low:        return "Low Risk";
    case Tier::Suspicious: return "Suspicious";
    case Tier::High:       return "High Risk";
    }
    return "?";
}

} // namespace birx
