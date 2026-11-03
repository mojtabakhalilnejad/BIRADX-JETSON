// birx/types.h — the data that flows through the decision pipeline.
//
// Pure C++17, no Qt. The decision core must stay compilable and testable
// without a GUI toolkit; the Qt layer adapts to these types at the edge.
//
// Units, fixed once and never converted again downstream:
//   frequency  hertz
//   magnitude  ohm          (|Z|)
//   phase      degrees      (arg Z; negative for capacitive tissue)
//   slope/IPS  degrees per decade of log10(frequency)
//
// The last line matters. The firmware's IPS is 1.43*(ph500-ph100), and
// 1/log10(500/100) = 1.43066..., so the shipped IPS is ALREADY a phase slope
// per decade. Every slope in this file is in the same unit, so a threshold
// carried over from the old device still means what it meant.

#ifndef BIRX_TYPES_H
#define BIRX_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <limits>

namespace birx {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// MCU port (BIRADx-H7): the behaviour of std::stod(s, &pos) without
// exceptions. Returns false exactly where std::stod would have thrown
// (nothing converted -> invalid_argument, ERANGE -> out_of_range).
bool birxStrtod(const std::string& s, double* out);

// ---------------------------------------------------------------------------
// One measured frequency point, exactly as the instrument reports it.
// ---------------------------------------------------------------------------
struct Point {
    double freqHz    = 0.0;
    double magOhm    = 0.0;
    double phaseDeg  = 0.0;
    int    range     = 0;    // gain range 1..6, the 4th field of the imp packet.
                             // 0 = the instrument did not report one.
    bool valid() const;
};

// ---------------------------------------------------------------------------
// One complete sweep = one measurement of one site.
// ---------------------------------------------------------------------------
struct Spectrum {
    std::vector<Point> points;          // in arrival order (high -> low frequency)
    std::string        caseId;          // patient/lesion identifier
    int                siteIndex = 0;   // which reading of that lesion (1,2,3...)
    std::string        needleRev;       // needle revision this was taken with
    int64_t            epochMs   = 0;

    // What the firmware itself reported in the IPS packet, kept for audit and
    // for A/B comparison. These are NOT used by the new decision path.
    double fwIps  = kNaN;
    double fwZ1k  = kNaN;
    std::string fwText;

    void clear();
    size_t size() const { return points.size(); }
    bool empty() const  { return points.empty(); }

    // Nearest point to a frequency, within tolHz. nullptr when absent.
    const Point* at(double freqHz, double tolHz = 1000.0) const;
    // Points within [loHz, hiHz], sorted by ascending frequency.
    std::vector<Point> band(double loHz, double hiHz) const;
};

// ---------------------------------------------------------------------------
// Quality: why a measurement may not be answerable.
// ---------------------------------------------------------------------------
enum class Flag {
    None = 0,
    NoSweep,                 // fatal: nothing arrived
    Incomplete,              // fatal: too few usable points in the fitting band
    NoZ1k,                   // fatal: the 1 kHz point is missing
    TipInFat,                // fatal: |Z| at 1 kHz above the adipose threshold
    Saturated,               // fatal: magnitude hit the wire format's ceiling
    PhaseImplausible,        // fatal: a phase outside what a passive network can produce
    UnstableSweep,           // fatal: phase-vs-log(f) too rough to define a slope
    RangeSwitchInBand,       // warning: gain changed inside the fitting band
    UncalibratedRange,       // warning: no phase offset known for a range used
    FewPoints,               // warning: fit used fewer points than preferred
    LegacyDisagrees          // warning: new verdict differs from the firmware's
};

const char* flagName(Flag f);
const char* flagText(Flag f);        // operator-facing sentence
bool        flagIsFatal(Flag f);

struct Quality {
    std::vector<Flag> flags;
    bool usable = false;             // false when any fatal flag is present

    void add(Flag f);
    bool has(Flag f) const;
    bool anyFatal() const;
    std::string summary() const;     // comma-separated names, for the log
    std::string operatorMessage() const;   // the first fatal flag's sentence
};

// ---------------------------------------------------------------------------
// Features. Every field is a number the model may use; the model file names
// which ones it actually wants, so adding a feature here never invalidates an
// installed model.
// ---------------------------------------------------------------------------
struct Features {
    double z1k          = kNaN;   // ohm, magnitude at 1 kHz
    double logZ1k       = kNaN;   // log10(ohm)
    double ipsLegacy    = kNaN;   // 1.43*(ph500-ph100) — the firmware's own definition
    double ipsFit       = kNaN;   // weighted least-squares slope over the band
    double ipsTheilSen  = kNaN;   // median of pairwise slopes over the band
    double fitRmsDeg    = kNaN;   // RMS residual of the least-squares fit
    double curvature    = kNaN;   // quadratic term of phase vs log10 f
    double phaseAtRefDeg = kNaN;  // fitted phase at the reference frequency — the
                                  // LEVEL of the phase curve, as opposed to its slope
    double phaseMinDeg  = kNaN;   // most negative phase in the band
    double logFAtPhaseMin = kNaN; // log10 of the frequency where that occurs
    double zRatio       = kNaN;   // |Z|(1kHz) / |Z|(100kHz)
    double zRatioHigh   = kNaN;   // |Z|(1kHz) / |Z|(500kHz)
    int    nFit         = 0;      // points that entered the fit
    int    rangeAt500k  = 0;
    int    rangeAt1k    = 0;
    int    rangeSwitches = 0;     // gain changes inside the fitting band

    // Cole model, fitted to the whole sweep. NaN when the sweep is not an arc.
    double coleR0       = kNaN;   // ohm, extracellular path
    double coleRinf     = kNaN;   // ohm, total path
    double coleRatio    = kNaN;   // R0/Rinf — dimensionless, so it should
                                  // survive a change of needle geometry
    double coleLogFc    = kNaN;   // log10 of the characteristic frequency
    double coleAlpha    = kNaN;   // dispersion width, (0,1]
    double coleRmsRel   = kNaN;   // relative fit residual; also a quality signal

    double byName(const std::string& name, bool* found = nullptr) const;
    // Writes one feature by name. Returns false for an unknown name rather
    // than silently doing nothing, so a typo in a tool is an error and not a
    // quietly missing value.
    bool   setByName(const std::string& name, double value);
    static std::vector<std::string> names();
};

// ---------------------------------------------------------------------------
// The verdict.
// ---------------------------------------------------------------------------
enum class Tier {
    Invalid = 0,     // no answer: the measurement failed its own checks
    Low,
    Suspicious,
    High
};

const char* tierName(Tier t);

struct Verdict {
    Tier    tier        = Tier::Invalid;
    double  probability = kNaN;   // calibrated P(needs tissue), NaN when Invalid
    Quality quality;
    Features features;
    std::string modelVersion;
    std::string reason;           // one sentence, shown to the operator

    /* WHAT THE SCREEN SHOWS FOR THIS READING.
     *
     * `tier` is how the reading combines with the other four; `label` is what
     * it is called, in whatever words the rule that matched was written in.
     * With the rule table deciding these differ on purpose — a site may call
     * Tier::High "Needs core biopsy" — and with a model deciding the label is
     * simply the tier's own name. Never derive one from the other: reading
     * the label to work out the tier is how free text turns into a clinical
     * decision nobody authorised.
     *
     * `color` is "#rrggbb" or empty, meaning the interface should use its own
     * colour for the tier. `sound` is a sound-library label or empty for
     * silence. */
    std::string label;
    std::string color;
    std::string sound;

    // The firmware's own rule, recomputed here so the two can be compared in
    // the log without asking the instrument twice.
    Tier legacyTier = Tier::Invalid;

    bool answered() const { return tier != Tier::Invalid; }
};

} // namespace birx
#endif
