// birx/dataset.h — the learning loop's memory.
//
// Two append-only files, both plain CSV, both readable in a spreadsheet:
//
//   dataset.csv    one row per READING: identity, every feature, the quality
//                  flags, the verdict, and which model produced it.
//   outcomes.csv   one row per OUTCOME: a case id and what the pathology said,
//                  written days or weeks later when the report comes back.
//
// They are separate files on purpose. A reading is a fact about the
// instrument and is written once, at the moment of measurement. An outcome is
// a fact about the patient and arrives later, from a different person, and may
// be corrected. Joining them is the trainer's job, and keeping the join
// explicit means a mistaken outcome can be superseded without rewriting any
// measurement history: the trainer takes the last entry for a case id.
//
// Append-only also means the training set is reconstructible. Any model can be
// re-derived from these two files, which is what makes a version claim in a
// model file checkable rather than asserted.
//
// Nothing here trains anything. The device records; a separate, reviewed step
// produces a model. A diagnostic instrument that silently changes its own
// behaviour between two patients cannot be audited, and an outcome-driven
// update loop with no human in it will happily learn from its own referral
// pattern: cases the device called Low Risk are less likely to be biopsied, so
// their outcomes never arrive, and the model is fed a censored sample that
// confirms whatever it already believed.

#ifndef BIRX_DATASET_H
#define BIRX_DATASET_H

#include "types.h"
#include "decision.h"
#include "patient.h"
#include <string>
#include <vector>
#include <map>

namespace birx {

enum class Pathology { Unknown = -1, Benign = 0, Borderline = 1, Malignant = 2 };

const char* pathologyName(Pathology p);
Pathology   pathologyFromName(const std::string& s);

struct Outcome {
    std::string caseId;
    Pathology   pathology = Pathology::Unknown;
    std::string reportDate;     // ISO
    std::string source;         // "core biopsy", "excision", ...
    std::string note;
    int64_t     recordedMs = 0;
};

// ---------------------------------------------------------------------------
class DatasetWriter {
public:
    explicit DatasetWriter(std::string path) : m_path(std::move(path)) {}

    // Writes the header on first use. Returns false on any I/O failure; the
    // caller must surface that, because a measurement that was not recorded is
    // a measurement that can never be learned from.
    bool append(const Spectrum& s, const Verdict& v,
                const PatientRecord* patient = nullptr);

    // The full sweep beside the row, so a feature can be added later and
    // recomputed over the whole history without re-measuring anyone.
    bool appendSweep(const std::string& dir, const Spectrum& s);

    const std::string& path() const { return m_path; }
    static std::string header();

private:
    std::string m_path;
};

// ---------------------------------------------------------------------------
class OutcomeStore {
public:
    explicit OutcomeStore(std::string path) : m_path(std::move(path)) {}

    // Appends; later entries win. On failure `why` says what failed — the
    // file, and the operating system's own reason — because a Save button
    // that silently does nothing is indistinguishable from a broken one.
    bool record(const Outcome& o, std::string* why = nullptr);
    bool load();                            // reads the file into memory
    Pathology lookup(const std::string& caseId) const;
    const std::map<std::string, Outcome>& all() const { return m_byCase; }
    size_t size() const { return m_byCase.size(); }

    // Cases measured but still without an outcome. This list is the honest
    // measure of how much the device can actually learn from, and it is worth
    // showing in the interface: an outcome that is never entered is a case
    // that never teaches anything.
    static std::vector<std::string> pending(const std::string& datasetCsv,
                                            const OutcomeStore& outcomes);

private:
    std::string m_path;
    std::map<std::string, Outcome> m_byCase;
};

// ---------------------------------------------------------------------------
// One row of the joined training table.
struct TrainingRow {
    std::string caseId;
    int         siteIndex = 0;
    std::string needleRev;
    int64_t     epochMs = 0;       // when it was measured, for the spread check
    std::string operatorId;        // who measured it, for the spread check
    Features    features;
    bool        usable = false;
    Pathology   pathology = Pathology::Unknown;
    // Patient covariates, by field id, exactly as the intake form recorded
    // them. The trainer offers the modelCandidate ones as features and uses
    // the stratifier ones to split the analysis.
    std::map<std::string, std::string> covariates;
    double covariate(const std::string& id) const;
};

// Reads dataset.csv, joins outcomes, and keeps only rows that passed quality.
std::vector<TrainingRow> buildTrainingTable(const std::string& datasetCsv,
                                            const OutcomeStore& outcomes,
                                            std::string* error = nullptr);

} // namespace birx
#endif
