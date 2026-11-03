// birx/audiocue.h — what the instrument says out loud.
//
// The operator's eyes are on the needle and the ultrasound probe, not on the
// panel. Sound is the only channel that reaches them during a placement, which
// makes it worth designing rather than decorating.
//
// SIX RULES THIS TABLE FOLLOWS
//
// 1. Cues differ by PATTERN, not only by pitch. Two beeps against three beeps
//    survives a noisy room, a cheap panel speaker and an operator with the
//    high-frequency loss that anyone over fifty has. Two cues that differ only
//    in being "higher" or "lower" do not.
//
// 2. Everything sits between 300 Hz and 1 kHz. Hearing is most sensitive
//    there, small speakers cannot reproduce much below it, and above about
//    2 kHz presbycusis starts eating the signal.
//
// 3. Every note is enveloped. A raised-cosine attack and release of 8 ms
//    removes the click that a hard-gated sine makes, which on a small speaker
//    is louder than the tone itself.
//
// 4. The verdict is spoken ONCE PER CASE, when the case is complete — not on
//    every reading. A device that announces a tier five times per lesion
//    teaches the operator to stop hearing it, and then the one announcement
//    that mattered is also unheard. Provisional readings get the neutral
//    capture blip instead.
//
// 5. Nothing loops and nothing repeats until acknowledged. This is a
//    diagnostic aid, not a monitor: there is no physiological alarm here, and
//    a sound that demands acknowledgement would be claiming one.
//
// 6. THIS IS NOT AN IEC 60601-1-8 ALARM SET, and must not be described as one.
//    That standard reserves specific melodies for specific alarm priorities
//    and requires an alarm system behind them. These are informational cues.
//    If the device is ever classified as having alarm conditions, this table
//    is replaced, not extended.

#ifndef BIRX_AUDIOCUE_H
#define BIRX_AUDIOCUE_H

#include "types.h"
#include <string>
#include <vector>

namespace birx {

enum class Cue {
    None,
    ReadingCaptured,   // one reading is in, and it passed its checks
    ReadingRefused,    // that reading was not usable — reposition
    CaseHigh,          // the lesion result, spoken once
    CaseSuspicious,
    CaseLow,
    CaseInvalid,       // the lesion cannot be answered
    NeedMoreReadings,  // quota not met, the case is still open
    CalibrationDone,
    NeedleRejected,    // invalid or already-used needle code
    Error              // the device could not do what was asked
};

struct Note {
    double freqHz = 0.0;    // 0 = silence
    int    ms     = 0;
    double level  = 0.8;    // 0..1, before the master volume
};

struct CueDef {
    Cue         cue;
    const char* id;         // file name stem, e.g. "case_high"
    const char* label;      // what it is, for the settings screen
    const char* rationale;  // why it sounds like this
    std::vector<Note> notes;
};

const std::vector<CueDef>& cueTable();
const CueDef* cueDef(Cue c);
const char*   cueId(Cue c);

// ---------------------------------------------------------------------------
// The policy: given what just happened, what should be heard?
struct CuePolicy {
    bool enabled          = true;
    // Speak the tier only when the case is settled. Off means every reading
    // announces a provisional tier, which is how an operator learns to ignore
    // the sound.
    bool verdictOnCaseOnly = true;
    // The neutral blip after each accepted reading.
    bool captureBlip      = true;
    double volume         = 0.7;   // 0..1
};

// tier: the CASE tier. complete: has the protocol been satisfied?
// readingUsable: did the reading that just arrived pass the quality gate?
Cue cueForReading(const CuePolicy& p, bool readingUsable);
Cue cueForCase(const CuePolicy& p, Tier tier, bool complete);

// ---------------------------------------------------------------------------
// Renders a cue to a 16-bit mono WAV at 44100 Hz. Used by the packaging tool
// so that the table above is the single definition of every sound — there is
// no separate set of audio files that can drift away from it.
bool writeCueWav(const CueDef& d, const std::string& path, double volume = 0.9);

} // namespace birx
#endif
