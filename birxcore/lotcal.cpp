#include "lotcal.h"
#include "opencal.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace birx {

// ---------------------------------------------------------------------------
// The bench procedure.
//
// The resistor values are not arbitrary. The firmware autoranges at 500, 905,
// 1800, 2500 and 4700 ohm, so the set has to put a standard in the middle of
// every range and a pair on either side of every boundary — a phase offset
// that only appears on range 4 is invisible if nothing is ever measured on
// range 4. The values below are E24 parts that land where they are needed.
//
// A metal-film resistor is the phase standard: its own phase is under 0.01
// degrees to 500 kHz, so whatever the instrument reports IS the error. The RC
// networks then check that a KNOWN non-zero phase is reproduced, which a set
// of pure resistors cannot test at all.
const std::vector<FactoryTest>& factoryTestPlan() {
    static const std::vector<FactoryTest> plan = {
        { "isolation", "Inner-outer isolation",
          "Nothing connected. Megohmmeter across the two electrodes, 100 V DC.",
          "That the two needles are electrically separate. A partial short here "
          "makes every later measurement meaningless.",
          "> 100 Mohm. Any lower and the needle is scrapped, not calibrated.", true },

        { "continuity", "Continuity and contact resistance",
          "Four-wire ohmmeter from the connector pin to each electrode tip.",
          "Lead and contact resistance, which sits in series with the tissue.",
          "< 2 ohm per conductor, and the two within 0.5 ohm of each other.", true },

        { "open", "Open sweep, all five gain ranges",
          "Needle mounted in the jig, tip in air, clear of hands and metal by "
          "at least 100 mm.",
          "Stray capacitance Cp and leakage Gp of needle, cable and connector.",
          "Cp within 3 pF of the batch mean. An outlier needle is set aside.", true },

        { "short", "Short sweep",
          "Tip shorted with the 0.05 ohm reference shunt.",
          "Residual series resistance and inductance of the path.",
          "R < 2 ohm, and the inductive phase at 500 kHz under 0.5 degrees.", true },

        { "resistors", "Resistor set, one per gain range",
          "220, 680, 1.2k, 2.2k and 3.9k ohm, 0.1% metal film, in the jig.",
          "Phase error against a standard whose own phase is under 0.01 degrees "
          "to 500 kHz — so what the instrument reports IS the error.",
          "Corrected phase within 0.15 degrees at every sweep frequency.", true },

        { "boundaries", "Range boundary pairs",
          "470 and 560 ohm, then 850 and 980, then 1.7k and 1.9k, then 2.4k and "
          "2.6k, then 4.5k and 4.9k ohm.",
          "The phase step when the instrument changes gain range. This is the "
          "one the air sweep cannot see, because an open circuit never forces a "
          "range change.",
          "Step under 0.15 degrees after the offsets are applied.", true },

        { "rc", "RC network, known non-zero phase",
          "1 kohm in parallel with 2.2 nF (corner 72 kHz), then 470 ohm with "
          "10 nF (corner 34 kHz). 1% C0G capacitors.",
          "That a known phase CURVE is reproduced, not just zero phase. A set of "
          "resistors cannot test this and a needle can pass all of them while "
          "still reporting the wrong slope.",
          "Phase within 0.2 degrees of the network's computed value across the "
          "band; recovered corner frequency within 3%.", true },

        { "cole", "Three-element tissue analogue",
          "1.2 kohm in series with (390 ohm parallel 4.7 nF), and a second "
          "network at (820 ohm parallel 1.5 nF).",
          "That the Cole fit recovers R0, Rinf and the characteristic frequency "
          "from a network whose true values are known to 1%.",
          "R0 and Rinf within 2%, characteristic frequency within 5%.", true },

        { "repeat", "Repeatability",
          "1.2 kohm standard, ten sweeps, needle undisturbed.",
          "The noise floor of the whole chain. Every other limit here is set "
          "relative to it.",
          "Phase standard deviation under 0.15 degrees, which is 0.21 IPS units.", true },

        { "warmup", "Warm-up drift",
          "1.2 kohm standard, measured at power-on and again after 30 minutes.",
          "Whether the instrument needs a warm-up period before a patient.",
          "Phase drift under 0.15 degrees. Above it, the warm-up time goes in "
          "the instructions for use.", false },

        { "sample", "Batch statistics",
          "At least five needles from the batch, spread across the reel.",
          "Whether the batch is consistent enough to share one calibration.",
          "Stray capacitance spread under 5 pF, which is 0.74 IPS units.", false },
    };
    return plan;
}

// ---------------------------------------------------------------------------
bool LotCalibration::complete() const {
    if (lotId.empty() || needleRev.empty()) return false;
    if (nSampled <= 0) return false;
    if (!std::isfinite(cpMeanFarad) || !std::isfinite(cpSdFarad)) return false;
    if (!std::isfinite(phaseRmsDeg) || !std::isfinite(repeatDeg)) return false;
    for (int r = 1; r <= 5; ++r) if (!std::isfinite(rangeOffsetDeg[r])) return false;
    return true;
}

double lotIpsShift(const LotCalibration& a, const LotCalibration& b, double sampleOhm) {
    if (!std::isfinite(a.cpMeanFarad) || !std::isfinite(b.cpMeanFarad)) return kNaN;
    const double dCp = a.cpMeanFarad - b.cpMeanFarad;
    // IPS is a slope per decade; the fitted band runs 100 kHz to 500 kHz, which
    // is log10(5) of a decade.
    const double e100 = phaseErrorDeg(dCp, sampleOhm, 100000.0);
    const double e500 = phaseErrorDeg(dCp, sampleOhm, 500000.0);
    return std::fabs((e500 - e100) / std::log10(5.0));
}

double lotMaxRangeOffsetDiffDeg(const LotCalibration& a, const LotCalibration& b) {
    double worst = 0.0;
    bool any = false;
    for (int r = 1; r <= 5; ++r) {
        if (!std::isfinite(a.rangeOffsetDeg[r]) || !std::isfinite(b.rangeOffsetDeg[r])) continue;
        worst = std::max(worst, std::fabs(a.rangeOffsetDeg[r] - b.rangeOffsetDeg[r]));
        any = true;
    }
    return any ? worst : kNaN;
}

// ---------------------------------------------------------------------------
const LotCalibration* LotCalibrationFile::find(const std::string& lotId) const {
    for (const LotCalibration& c : lots) if (c.lotId == lotId) return &c;
    return nullptr;
}

const LotCalibration* LotCalibrationFile::nearest(const LotCalibration& c,
                                                  double* ipsShift) const {
    const LotCalibration* best = nullptr;
    double bestShift = kNaN;
    for (const LotCalibration& o : lots) {
        if (o.lotId == c.lotId) continue;
        if (o.needleRev != c.needleRev) continue;   // a different revision is a different part
        const double s = lotIpsShift(c, o);
        if (!std::isfinite(s)) continue;
        if (!best || s < bestShift) { best = &o; bestShift = s; }
    }
    if (ipsShift) *ipsShift = bestShift;
    return best;
}

LotDecision LotCalibrationFile::classify(const LotCalibration& c,
                                         const LotAcceptance& acc) const {
    LotDecision d;
    std::ostringstream o;

    if (!c.complete()) {
        d.verdict = LotVerdict::Rejected;
        d.message = "The bench record is incomplete. A lot is not calibrated until "
                    "every range has an offset and the batch statistics are in.";
        return d;
    }

    // --- did the batch pass its own limits at all? ---
    std::vector<std::string> fails;
    if (c.nSampled < acc.minSampled)
        fails.push_back("only " + std::to_string(c.nSampled) + " needles sampled, minimum "
                        + std::to_string(acc.minSampled));
    if (c.repeatDeg > acc.maxRepeatDeg) {
        std::ostringstream f;
        f << "repeatability " << std::fixed << std::setprecision(3) << c.repeatDeg
          << " deg is above the " << acc.maxRepeatDeg << " deg floor";
        fails.push_back(f.str());
    }
    if (c.phaseRmsDeg > acc.maxPhaseRmsDeg) {
        std::ostringstream f;
        f << "residual phase error " << std::fixed << std::setprecision(3) << c.phaseRmsDeg
          << " deg against the resistor set, limit " << acc.maxPhaseRmsDeg;
        fails.push_back(f.str());
    }
    if (c.cpSdFarad > acc.maxCpSdFarad) {
        std::ostringstream f;
        f << "stray capacitance varies by " << std::fixed << std::setprecision(1)
          << c.cpSdFarad * 1e12 << " pF across the batch, limit " << acc.maxCpSdFarad * 1e12
          << " pF — the needles in this batch are not alike enough to share one calibration";
        fails.push_back(f.str());
    }
    if (!fails.empty()) {
        d.verdict = LotVerdict::Rejected;
        o << "This batch does not pass the bench limits and must not be released:";
        for (const std::string& f : fails) o << "\n  - " << f;
        d.message = o.str();
        return d;
    }

    // --- is it distinguishable from a lot already in the file? ---
    double shift = kNaN;
    const LotCalibration* near = nearest(c, &shift);
    if (!near) {
        d.verdict = LotVerdict::NeedsNewEntry;
        d.ipsShift = kNaN;
        o << "FIRST LOT for needle revision " << c.needleRev << ".\n"
          << "This adds a new calibration to the system. Every device that will "
          << "use needles from this lot must be updated with the new file before "
          << "the lot is shipped.";
        d.message = o.str();
        return d;
    }

    d.nearestLot      = near->lotId;
    d.ipsShift        = shift;
    d.rangeOffsetDiff = lotMaxRangeOffsetDiffDeg(c, *near);

    const bool cpSame = shift <= lotIpsShift(c, *near) + 0.0 &&
                        std::fabs(c.cpMeanFarad - near->cpMeanFarad) <= acc.sameCpToleranceFarad;
    const bool offSame = std::isfinite(d.rangeOffsetDiff) &&
                         d.rangeOffsetDiff <= acc.sameOffsetToleranceDeg;

    o << std::fixed << std::setprecision(2);
    if (cpSame && offSame) {
        d.verdict = LotVerdict::MatchesExisting;
        o << "NO NEW CALIBRATION NEEDED.\n"
          << "This lot is indistinguishable from lot " << near->lotId << ": stray "
          << "capacitance differs by " << std::setprecision(1)
          << std::fabs(c.cpMeanFarad - near->cpMeanFarad) * 1e12 << " pF ("
          << std::setprecision(2) << shift << " IPS units) and the largest gain-range "
          << "offset difference is " << d.rangeOffsetDiff << " deg — both inside the "
          << "instrument's own repeatability.\n"
          << "Devices in the field do NOT need updating for this lot.";
    } else {
        d.verdict = LotVerdict::NeedsNewEntry;
        o << "A NEW CALIBRATION IS REQUIRED, and every device must be updated.\n"
          << "Against the nearest lot " << near->lotId << ": ";
        if (!cpSame)
            o << "stray capacitance differs by " << std::setprecision(1)
              << std::fabs(c.cpMeanFarad - near->cpMeanFarad) * 1e12 << " pF, which moves IPS by "
              << std::setprecision(2) << shift << " units. ";
        if (!offSame)
            o << "the largest gain-range phase offset differs by " << d.rangeOffsetDiff
              << " deg. ";
        o << "\nA device running the previous file would apply the wrong correction to "
          << "needles from this lot, and the error lands directly on IPS, which is the "
          << "whole diagnostic signal. Do not ship this lot to a site until its devices "
          << "carry the new file.";
    }
    d.message = o.str();
    return d;
}

bool LotCalibrationFile::add(const LotCalibration& c, const LotAcceptance& acc) {
    const LotDecision d = classify(c, acc);
    if (d.verdict == LotVerdict::Rejected) return false;
    if (find(c.lotId)) return false;            // never silently replace a released lot
    lots.push_back(c);
    ++revision;
    return true;
}

std::string LotCalibrationFile::summary() const {
    std::ostringstream o;
    o << "revision " << revision << ", issued " << (issuedAt.empty() ? "-" : issuedAt)
      << ", " << lots.size() << " lot" << (lots.size() == 1 ? "" : "s");
    return o.str();
}

// ---------------------------------------------------------------------------
namespace {
std::string q(const std::string& s) { return s.empty() ? std::string("-") : s; }
}

bool LotCalibrationFile::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(10);
    f << "# birx lot calibration file\n"
      << "# one line per lot; the file carries EVERY lot, not a delta, so a device\n"
      << "# that missed an update is fixed by loading this file.\n"
      << "revision: " << revision << "\n"
      << "issued: "   << q(issuedAt) << "\n";
    for (const LotCalibration& c : lots) {
        f << "lot: " << q(c.lotId) << " " << q(c.needleRev) << " " << q(c.takenAt)
          << " " << q(c.operatorId) << " " << c.nSampled
          << " " << c.cpMeanFarad << " " << c.cpSdFarad
          << " " << c.seriesROhm  << " " << c.phaseRmsDeg << " " << c.repeatDeg;
        for (int r = 1; r <= 5; ++r) f << " " << c.rangeOffsetDeg[r];
        f << " " << (c.passed ? 1 : 0) << "\n";
    }
    return f.good();
}

bool LotCalibrationFile::load(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    lots.clear();
    revision = 0;
    issuedAt.clear();

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::istringstream v(line.substr(colon + 1));
        if      (key == "revision") v >> revision;
        else if (key == "issued")   v >> issuedAt;
        else if (key == "lot") {
            LotCalibration c;
            int passed = 0;
            v >> c.lotId >> c.needleRev >> c.takenAt >> c.operatorId >> c.nSampled
              >> c.cpMeanFarad >> c.cpSdFarad >> c.seriesROhm >> c.phaseRmsDeg >> c.repeatDeg;
            for (int r = 1; r <= 5; ++r) v >> c.rangeOffsetDeg[r];
            v >> passed;
            c.passed = passed != 0;
            if (c.lotId.empty()) {
                if (error) *error = "malformed lot line";
                return false;
            }
            lots.push_back(c);
        }
    }
    if (lots.empty()) {
        if (error) *error = "the file contains no lots";
        return false;
    }
    return true;
}

} // namespace birx
