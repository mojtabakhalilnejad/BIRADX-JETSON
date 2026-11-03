// tools/matlib_eval.cpp — the materials classifier of the firmware, on a library from a device (records/materials.txt):
// placements left out, needles left out, the suspect sweeps, a label tested against the others. Bench analysis on the PC:
//   g++ -std=c++17 -O2 -Ibirxcore tools/matlib_eval.cpp build-pc/core/*.o -o matlib_eval
//   ./matlib_eval materials.txt all|lono|suspect|okonly|nosuspect|noleg|classify [label ...]   (classify: TEST=<label>)
#include "matlib.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdlib>
using namespace birx;

static MatLibrary sub(const MatLibrary& L, const std::set<std::string>& labels, bool okOnly, bool dropSuspectFlag) {
    MatLibrary o; o.device = L.device;
    for (auto& l : L.labels) if (labels.count(l)) o.labels.push_back(l);
    for (auto s : L.sweeps) if (labels.count(s.label)) {
        if (okOnly && s.qc != "ok") continue;
        if (dropSuspectFlag && s.qc == "suspect") s.qc = "";
        o.sweeps.push_back(s);
    }
    return o;
}
static void report(const char* tag, const MatModel& m) {
    if (!m.ok) { std::printf("%s: model not ok: %s\n", tag, m.why.c_str()); return; }
    std::printf("%s: %s labels=%zu placements=%d LOPO sweeps %d/%d (%.0f%%) blend %d  zips %d/%d  final %d/%d  aside=%d suspect=%d tip=%d lambda=%.2f eps=%.2f\n",
        tag, m.normalised ? "NORM" : "RAW ", m.labels.size(), m.nPlacements, m.nCorrect, m.nTested, 100.0 * m.nCorrect / std::max(1, m.nTested),
        m.nBlendCorrect, m.zips.nCorrect, m.zips.nTested, m.stack.nCorrect, m.stack.nTested, m.nAside, m.nQcSuspect, m.nTipFault, m.lambda, m.eps);
    for (size_t i = 0; i < m.labels.size(); ++i) {
        std::printf("    %-22s", m.labels[i].c_str());
        for (size_t j = 0; j < m.labels.size(); ++j) std::printf(" %3d", i < m.confusion.size() && j < m.confusion[i].size() ? m.confusion[i][j] : -1);
        std::printf("\n");
    }
}
// leave one needle out: train without it, classify its sweeps
static void lono(const MatLibrary& L, bool norm) {
    std::set<std::string> needles;
    for (auto& s : L.sweeps) needles.insert(s.needle);
    int tot = 0, ok = 0, fok = 0, ftot = 0;
    for (auto& nd : needles) {
        MatLibrary tr = L; tr.sweeps.clear();
        std::vector<MatSweep> te;
        for (auto& s : L.sweeps) (s.needle == nd ? te : tr.sweeps).push_back(s);
        // only labels present in training
        MatModel m = trainMaterials(tr, norm);
        if (!m.ok) { std::printf("  needle %-10s: model not ok (%s)\n", nd.c_str(), m.why.c_str()); continue; }
        std::map<std::string, std::pair<int,int>> per;
        int n = 0, r = 0, fr = 0, fn = 0;
        for (auto& s : te) {
            if (std::find(m.labels.begin(), m.labels.end(), s.label) == m.labels.end()) continue;
            if (s.qc == "suspect") continue;
            MatMatch mm = m.classify(s.pts, s.needleInfo());
            if (!mm.ok) continue;
            ++n; const bool right = mm.labels[size_t(mm.best)] == s.label; r += right;
            per[s.label].first += right; per[s.label].second++;
            if (mm.fOk && mm.fBest >= 0) { ++fn; fr += mm.labels[size_t(mm.fBest)] == s.label; }
        }
        std::printf("  needle %-10s: %3d/%3d whole-curve, final %3d/%3d |", nd.empty() ? "(none)" : nd.c_str(), r, n, fr, fn);
        for (auto& p : per) std::printf(" %s %d/%d;", p.first.c_str(), p.second.first, p.second.second);
        std::printf("\n");
        tot += n; ok += r; fok += fr; ftot += fn;
    }
    std::printf("  LONO %s total: whole-curve %d/%d (%.0f%%), final %d/%d (%.0f%%)\n", norm ? "NORM" : "RAW", ok, tot, 100.0 * ok / std::max(1, tot), fok, ftot, 100.0 * fok / std::max(1, ftot));
}
int main(int argc, char** argv) {
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
    MatLibrary L; L.load(ss.str());
    std::string mode = argv[2];
    std::set<std::string> labels;
    for (int i = 3; i < argc; ++i) labels.insert(argv[i]);
    if (labels.empty()) for (auto& l : L.labels) labels.insert(l);
    MatLibrary S = sub(L, labels, mode == "okonly", mode == "nosuspect");
    std::printf("sweeps %zu\n", S.sweeps.size());
    if (mode == "lono") { lono(S, false); lono(S, true); return 0; }
    if (mode == "classify") {   // train on argv labels, classify every sweep of the label in env TEST
        const char* t = std::getenv("TEST");
        for (int norm = 0; norm < 2; ++norm) {
            MatModel m = trainMaterials(S, norm);
            for (auto& s : L.sweeps) if (s.label == t) {
                MatMatch mm = m.classify(s.pts, s.needleInfo()); if (!mm.ok) { std::printf("  %s: %s\n", norm?"N":"R", mm.why.c_str()); continue; }
                std::printf("  %s %s %-10s qc=%-8s -> %-12s p=%.2f final=%-12s d2best=%.0f ownLimit=%.0f unlike=%d\n", norm ? "N" : "R", s.when.c_str(), s.needle.c_str(), s.qc.c_str(),
                    mm.labels[size_t(mm.best)].c_str(), mm.prob[size_t(mm.best)], mm.fOk ? mm.labels[size_t(mm.fBest)].c_str() : "-", mm.d2[size_t(mm.best)], m.ownLimit, int(mm.unlike));
            }
        }
        return 0;
    }
    if (mode == "noleg") {      // without the sweeps that have no needle number (taken before H7-2.9.0)
        MatLibrary T = S; T.sweeps.clear();
        for (auto& s : S.sweeps) if (!s.needle.empty()) T.sweeps.push_back(s);
        std::printf("without legacy: %zu sweeps\n", T.sweeps.size());
        report("noleg", trainMaterials(T, false)); report("noleg", trainMaterials(T, true));
        return 0;
    }
    if (mode == "suspect") {
        for (int norm = 0; norm < 2; ++norm) {
            MatModel m = trainMaterials(S, norm);
            std::map<std::string, std::pair<int,int>> per; int n = 0, r = 0;
            for (auto& s : S.sweeps) if (s.qc == "suspect") {
                MatMatch mm = m.classify(s.pts, s.needleInfo()); if (!mm.ok) continue;
                bool right = mm.labels[size_t(mm.best)] == s.label; ++n; r += right; per[s.label].first += right; per[s.label].second++;
                std::printf("    %s %s %-12s %-12s -> %-14s d2own=%.0f lim=%.0f\n", norm ? "N" : "R", s.when.c_str(), s.needle.c_str(), s.label.c_str(), mm.labels[size_t(mm.best)].c_str(),
                    mm.d2[size_t(std::find(mm.labels.begin(), mm.labels.end(), s.label) - mm.labels.begin())], m.ownLimit);
            }
            std::printf("suspect sweeps, %s model from the others: %d/%d |", norm ? "NORM" : "RAW", r, n);
            for (auto& p : per) std::printf(" %s %d/%d;", p.first.c_str(), p.second.first, p.second.second);
            std::printf("\n");
        }
        return 0;
    }
    report("full", trainMaterials(S, false));
    report("full", trainMaterials(S, true));
    return 0;
}
