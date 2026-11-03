#include "audiocue.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace birx {

// Note frequencies used below, so the table reads as music rather than as
// numbers: C5 523, E5 659, G5 784, A5 880, C4 262, E4 330, A3 220, E3 165.
const std::vector<CueDef>& cueTable() {
    static const std::vector<CueDef> t = {
        { Cue::ReadingCaptured, "reading_captured", "Reading captured",
          "One short blip, high and quiet. It happens many times per lesion, so "
          "it has to be the least intrusive sound in the set — its job is only "
          "to say the sweep is over and the operator may move.",
          { {880, 55, 0.45} } },

        { Cue::ReadingRefused, "reading_refused", "Reading refused",
          "Two low notes falling. Distinct from the capture blip in pattern, "
          "pitch and direction at once, because this is the cue that has to cut "
          "through when the operator is already moving on.",
          { {330, 90, 0.7}, {0, 40}, {262, 130, 0.7} } },

        { Cue::NeedMoreReadings, "need_more", "More readings needed",
          "The capture blip with a soft second note after it: the reading was "
          "good, the lesion is not finished. Deliberately close to the capture "
          "blip, because it means almost the same thing.",
          { {880, 55, 0.45}, {0, 35}, {659, 70, 0.4} } },

        { Cue::CaseLow, "case_low", "Lesion result: Low Risk",
          "Two notes rising, calm and unhurried. Rising means resolved in every "
          "interface convention the operator has ever used.",
          { {523, 110, 0.7}, {0, 25}, {784, 190, 0.7} } },

        { Cue::CaseSuspicious, "case_suspicious", "Lesion result: Suspicious",
          "Two notes at the SAME pitch. Neither rising nor falling, which is "
          "exactly the claim being made: the device has not resolved this one.",
          { {659, 120, 0.75}, {0, 70}, {659, 180, 0.75} } },

        { Cue::CaseHigh, "case_high", "Lesion result: High Risk",
          "Three notes falling, then a pause, then the last note again. Three "
          "against two separates it from every other case result by pattern "
          "alone, and the repeat makes it the longest sound the device makes "
          "without being loud — length carries urgency better than volume on a "
          "panel speaker.",
          { {784, 100, 0.8}, {0, 30}, {659, 100, 0.8}, {0, 30}, {523, 160, 0.8},
            {0, 120}, {523, 220, 0.8} } },

        { Cue::CaseInvalid, "case_invalid", "Lesion cannot be answered",
          "A low double pulse with no tonal movement. It should not sound like "
          "a result of any kind, because it is not one.",
          { {262, 130, 0.65}, {0, 60}, {262, 130, 0.65} } },

        { Cue::CalibrationDone, "calibration_done", "Calibration finished",
          "A quiet three-note rise. Housekeeping, not a finding, so it is the "
          "quietest cue after the capture blip.",
          { {523, 70, 0.5}, {0, 15}, {659, 70, 0.5}, {0, 15}, {880, 120, 0.5} } },

        { Cue::NeedleRejected, "needle_rejected", "Needle refused",
          "A single long low note. Nothing else in the set is one sustained "
          "tone, and this stops the test before it starts, so it should not be "
          "confusable with anything that happens during one.",
          { {220, 400, 0.75} } },

        { Cue::Error, "error", "Device error",
          "Three fast low pulses. Fast repetition is the only rhythm reserved "
          "for something being wrong with the instrument rather than with the "
          "measurement.",
          { {165, 80, 0.8}, {0, 45}, {165, 80, 0.8}, {0, 45}, {165, 80, 0.8} } },
    };
    return t;
}

const CueDef* cueDef(Cue c) {
    for (const CueDef& d : cueTable()) if (d.cue == c) return &d;
    return nullptr;
}

const char* cueId(Cue c) {
    const CueDef* d = cueDef(c);
    return d ? d->id : "";
}

// ---------------------------------------------------------------------------
Cue cueForReading(const CuePolicy& p, bool readingUsable) {
    if (!p.enabled) return Cue::None;
    if (!readingUsable) return Cue::ReadingRefused;
    return p.captureBlip ? Cue::ReadingCaptured : Cue::None;
}

Cue cueForCase(const CuePolicy& p, Tier tier, bool complete) {
    if (!p.enabled) return Cue::None;
    // An unfinished case has no verdict to announce. Saying one anyway is how
    // an operator learns that the verdict sound means nothing.
    if (!complete) return p.verdictOnCaseOnly ? Cue::NeedMoreReadings : Cue::None;
    switch (tier) {
        case Tier::High:       return Cue::CaseHigh;
        case Tier::Suspicious: return Cue::CaseSuspicious;
        case Tier::Low:        return Cue::CaseLow;
        case Tier::Invalid:    return Cue::CaseInvalid;
    }
    return Cue::None;
}

// ---------------------------------------------------------------------------
namespace {

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24));
}
void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}
void putStr(std::vector<uint8_t>& v, const char* s) {
    for (const char* p = s; *p; ++p) v.push_back(uint8_t(*p));
}

} // namespace

bool writeCueWav(const CueDef& d, const std::string& path, double volume) {
    const int rate = 44100;
    std::vector<double> samples;

    for (const Note& n : d.notes) {
        const int len = int(double(rate) * n.ms / 1000.0);
        if (n.freqHz <= 0.0) { samples.insert(samples.end(), size_t(len), 0.0); continue; }

        // 8 ms raised-cosine attack and release. A hard-gated sine clicks, and
        // on a small panel speaker the click is louder than the tone.
        const int env = std::min(len / 2, int(rate * 0.008));
        for (int i = 0; i < len; ++i) {
            const double t = double(i) / rate;
            double a = 1.0;
            if (env > 0) {
                if (i < env)            a = 0.5 * (1.0 - std::cos(M_PI * double(i) / env));
                else if (i > len - env) a = 0.5 * (1.0 - std::cos(M_PI * double(len - i) / env));
            }
            // A trace of the second harmonic: a pure sine on a small speaker
            // is quieter than it measures, because the driver reproduces the
            // harmonic better than the fundamental.
            const double s = std::sin(2 * M_PI * n.freqHz * t)
                           + 0.18 * std::sin(4 * M_PI * n.freqHz * t);
            samples.push_back(a * n.level * volume * s / 1.18);
        }
    }
    if (samples.empty()) return false;

    std::vector<uint8_t> out;
    const uint32_t dataBytes = uint32_t(samples.size() * 2);
    putStr(out, "RIFF"); put32(out, 36 + dataBytes); putStr(out, "WAVE");
    putStr(out, "fmt "); put32(out, 16); put16(out, 1); put16(out, 1);
    put32(out, uint32_t(rate)); put32(out, uint32_t(rate * 2));
    put16(out, 2); put16(out, 16);
    putStr(out, "data"); put32(out, dataBytes);
    for (double s : samples) {
        const double c = s < -1.0 ? -1.0 : (s > 1.0 ? 1.0 : s);
        put16(out, uint16_t(int16_t(c * 32767.0)));
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return f.good();
}

} // namespace birx
