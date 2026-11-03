// birx/opencal.h — per-needle open-circuit calibration, and the log that will
// later say whether per-needle was necessary at all.
//
// WHY AN OPEN AND NOT A FULL OPEN/SHORT/LOAD
//
// A single-use needle arrives sterile. Shorting its two concentric electrodes
// means touching the tip, which breaks sterility, so the short cannot be part
// of the per-needle routine. It does not need to be. At 500 kHz on a 600 Ω
// sample the two standards are worth very different amounts:
//
//     open  (shunt C, 50–200 pF)      −5 to −21 degrees
//     short (series R+L, ~1 uH)       +0.15 to +0.6 degrees
//
// The open carries the error. It also needs nothing to touch the needle — the
// operator holds it in air. The short belongs to the cable, which is reusable,
// so it can be taken once at the connector before a needle is attached, and
// this class accepts it optionally.
//
// WHY PER GAIN RANGE
//
// The open is taken on the same forced range the tissue will be measured on,
// so the correction removes the cable parasitic AND that range's own phase
// offset in one step. Two problems, one measurement.
//
// WHAT THE OPEN CAN AND CANNOT SEE
//
// On an open circuit the only current path is the parasitic itself, so |Z| is
// enormous at low frequency and unmeasurable:
//
//     100 pF at 500 kHz ->  3.2 kΩ    measurable
//     100 pF at  10 kHz -> 159 kΩ     marginal
//     100 pF at   1 kHz -> 1.6 MΩ     not measurable
//
// That is not a problem: the parasitic error is negligible at 1 kHz anyway. So
// the correction is NOT applied point by point from a noisy open sweep. A
// two-parameter model (Cp, Gp) is fitted to the frequencies where the open is
// actually measurable and then evaluated across the whole band. That is what a
// real impedance analyser does, and it is far more robust.

#ifndef BIRX_OPENCAL_H
#define BIRX_OPENCAL_H

#include "types.h"
#include <map>
#include <string>

namespace birx {

struct OpenCalConfig {
    // Only fit where the open is genuinely measurable.
    double minFitHz     = 25000.0;
    double maxOpenOhm   = 300000.0;   // above this the reading is noise

    /* THE BRIDGE TEST, AND WHERE IT BELONGS.
     *
     * `minOpenOhm` asks "is something across the tip?" — saline on the shaft,
     * a finger, tissue. It is checked at the BOTTOM of the sweep and nowhere
     * else, and the physics is the reason. An open circuit is a capacitance,
     * |Z| = 1/(2.pi.f.Cp), so its impedance is LARGEST at the lowest frequency
     * and falls from there; a bridge is a resistance, roughly the same at
     * every frequency and low everywhere. The low end is therefore the one
     * place where the two look different, and it is the only place worth
     * asking.
     *
     * It used to be applied to every point in the fit band, and any point
     * below it failed the whole calibration with "something is bridging the
     * tip". That made the two constants below contradict it: the model calls
     * a parasitic of up to 2000 pF plausible, and a 2000 pF needle is 159 ohm
     * at 500 kHz — so every needle above about 318 pF was refused, at the top
     * of its own sweep, with a message about a bridge that was not there.
     * A perfectly good needle on a slightly long cable could not be
     * calibrated at all, and the operator was told to clean a tip that was
     * already clean.
     */
    double minOpenOhm   = 1000.0;     // at the LOW end only: below this, something bridges

    /* The instrument's own floor. A point this low carries no usable phase
     * whatever produced it, so it is dropped from the fit the same way a
     * noisy one above maxOpenOhm is. Whether what remains really is a
     * parasitic capacitance is then decided by the fit itself — by
     * maxRmsRel and by the Cp bounds — which is a test of the shape of the
     * whole sweep rather than of one number on one point. */
    double minUsableOhm = 50.0;

    int    minFitPoints = 4;
    double maxRmsRel    = 0.25;       // relative residual of the Cp fit

    // Sanity bounds on what a cable + connector + needle can plausibly be.
    double minCpFarad = 5e-12;
    double maxCpFarad = 2000e-12;
};

// The parasitic seen looking out of the instrument with nothing connected at
// the tip:   Y_open(f) = Gp + j*2*pi*f*Cp
struct ParasiticModel {
    double cpFarad  = kNaN;
    double gpSiemens = 0.0;
    double rmsRel   = kNaN;
    int    nPoints  = 0;
    bool   ok       = false;

    double reactanceAtOhm(double freqHz) const;   // |Z| of the parasitic alone
};

struct RangeCal {
    int            range = 0;
    ParasiticModel open;
    double         seriesROhm  = 0.0;   // from the optional short
    double         seriesLHenry = 0.0;
    bool           haveShort = false;
};

// ---------------------------------------------------------------------------
class NeedleCalibration {
public:
    std::string needleId;     // the individual needle — a serial or a scan
    std::string batchId;      // the lot it came from; this is what the log is for
    std::string needleRev;
    std::string takenAt;      // ISO timestamp
    std::string operatorId;

    // Feed one open sweep taken on a forced gain range. Returns false and
    // leaves that range uncalibrated when the sweep does not look like an open.
    bool addOpenSweep(int range, const Spectrum& s, const OpenCalConfig& cfg = {},
                      std::string* why = nullptr);

    // Optional, from the cable alone: shorted at the connector.
    bool addShortSweep(int range, const Spectrum& s);

    // H7-2.0.0: a range's open set from known values (a lot's mean, or the
    // mean of a needle's repeated air sweeps) rather than fitted to one sweep.
    void setOpen(int range, double cpFarad, double gpSiemens);

    // The inverse of correct(): puts this parasitic back onto a sweep that has
    // none, so a reading can be shown as THIS needle would have measured it.
    // Same ranges, same model.
    int embed(Spectrum* s) const;

    bool   calibrated(int range) const;
    size_t rangeCount() const { return m_byRange.size(); }
    const std::map<int, RangeCal>& ranges() const { return m_byRange; }

    // Correct a measured sweep in place, using each point's own gain range.
    // Points whose range has no calibration are left alone and counted.
    int correct(Spectrum* s) const;

    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // Append one row per range to the fleet-wide log. THIS is the file that
    // answers "is per-needle calibration necessary, or is a per-batch nominal
    // enough" — a question that cannot be settled by argument, only by looking
    // at how much Cp actually varies between needles of one lot.
    bool appendToLog(const std::string& logPath) const;
    static std::string logHeader();

    std::string describe() const;

private:
    std::map<int, RangeCal> m_byRange;
};

// ---------------------------------------------------------------------------
// One row of needle_cal.csv, read back for the batch analysis.
struct CalLogRow {
    std::string needleId, batchId, needleRev, takenAt;
    int    range   = 0;
    double cpFarad = kNaN;
    double gpSiemens = kNaN;
    double rmsRel  = kNaN;
    int    nPoints = 0;
};

std::vector<CalLogRow> readCalLog(const std::string& path, std::string* error = nullptr);

// The phase error that a capacitance error of dCp produces on a sample of
// `sampleOhm` at `freqHz`. This is the whole decision criterion: if the
// spread of Cp within a batch implies less than the instrument's phase budget,
// a per-batch nominal is enough and the per-needle step can be dropped.
double phaseErrorDeg(double dCpFarad, double sampleOhm, double freqHz);

} // namespace birx
#endif
