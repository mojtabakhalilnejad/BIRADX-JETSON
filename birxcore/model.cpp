#include "model.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace birx {

double RiskModel::sigmoid(double z) {
    if (z >= 0) { const double e = std::exp(-z); return 1.0 / (1.0 + e); }
    const double e = std::exp(z);
    return e / (1.0 + e);
}

bool RiskModel::valid() const {
    if (isBand) {
        if (version.empty() || needleRev.empty()) return false;
        if (bandFeature.empty()) return false;
        if (!std::isfinite(bandLo) || !std::isfinite(bandHi) || bandHi <= bandLo) return false;
        if (!(bandSharpness > 0.0) || !std::isfinite(bandSharpness)) return false;
        if (!(susMarginIps >= 0.0) || !std::isfinite(susMarginIps)) return false;
        // The edge must be the tier boundary, or the band is not the rule.
        if (std::fabs(thresholdHigh - 0.5) > 1e-9) return false;
        const double wantLow = sigmoid(-bandSharpness * susMarginIps);
        if (std::fabs(thresholdLow - wantLow) > 1e-9) return false;
        return true;
    }
    if (terms.empty()) return false;
    if (version.empty() || needleRev.empty()) return false;
    if (!std::isfinite(intercept) || !std::isfinite(calibA) || !std::isfinite(calibB)) return false;
    if (!(thresholdLow > 0.0 && thresholdLow <= thresholdHigh && thresholdHigh < 1.0)) return false;
    for (const ModelTerm& t : terms) {
        if (t.name.empty()) return false;
        if (!std::isfinite(t.mean) || !std::isfinite(t.coef)) return false;
        if (!std::isfinite(t.scale) || std::fabs(t.scale) < 1e-12) return false;
    }
    return true;
}

double RiskModel::linearScore(const Features& f) const {
    if (!valid()) return kNaN;
    if (isBand) {
        bool found = false;
        const double x = f.byName(bandFeature, &found);
        if (!found || !std::isfinite(x)) return kNaN;
        return bandSharpness * std::min(x - bandLo, bandHi - x);
    }
    double z = intercept;
    for (const ModelTerm& t : terms) {
        bool found = false;
        const double x = f.byName(t.name, &found);
        if (!found || !std::isfinite(x)) return kNaN;   // never impute
        z += t.coef * (x - t.mean) / t.scale;
    }
    return z;
}

double RiskModel::probability(const Features& f) const {
    const double z = linearScore(f);
    if (!std::isfinite(z)) return kNaN;
    // A band is already in probability-slope units; Platt scaling belongs to a
    // fit and would move the edge away from the bench number.
    if (isBand) return sigmoid(z);
    return sigmoid(calibA * z + calibB);
}

RiskModel RiskModel::band(const std::string& feature, double lo, double hi,
                          const std::string& needleRev_, const std::string& version_,
                          double sharpness, double susMargin) {
    RiskModel m;
    m.isBand        = true;
    m.bandFeature   = feature;
    m.bandLo        = lo;
    m.bandHi        = hi;
    m.bandSharpness = sharpness;
    m.susMarginIps  = susMargin;
    m.needleRev     = needleRev_;
    m.version       = version_;
    m.endpoint      = "bench rule, not fitted — risk when " + feature +
                      " lies inside the band and the tip is not in fat";
    m.thresholdHigh = 0.5;
    m.thresholdLow  = sigmoid(-sharpness * susMargin);
    return m;
}

RiskModel RiskModel::abstaining() {
    RiskModel m;
    m.version   = "none";
    m.needleRev = "any";
    m.endpoint  = "no model installed";
    return m;                    // terms empty -> valid() false -> always NaN
}

std::string RiskModel::describe() const {
    std::ostringstream o;
    if (isBand) {
        o << version << " (needle " << needleRev << ", BENCH BAND "
          << bandFeature << " " << std::fixed << std::setprecision(2)
          << bandLo << " to " << bandHi;
        if (susMarginIps > 0) o << ", suspicious shoulder " << susMarginIps;
        o << ", not fitted)";
        return o.str();
    }
    o << version << " (needle " << needleRev << ", n=" << nCases
      << ", " << nPositive << " positive";
    if (std::isfinite(auc)) o << ", AUC " << std::fixed << std::setprecision(3) << auc;
    o << ")";
    return o.str();
}

bool RiskModel::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(10);
    f << "# birx model v1\n";
    if (isBand) {
        f << "# BENCH BAND, NOT A FIT. p = sigmoid(sharpness * distance inside the band).\n"
          << "# The probability is a distance readout, not a calibrated frequency.\n"
          << "version: "   << version   << "\n"
          << "trained: "   << trainedOn << "\n"
          << "needle: "    << needleRev << "\n"
          << "endpoint: "  << endpoint  << "\n"
          << "band: "      << bandFeature << " " << bandLo << " " << bandHi << "\n"
          << "band_sharpness: " << bandSharpness << "\n"
          << "sus_margin: "     << susMarginIps  << "\n"
          << "threshold_low: "  << thresholdLow  << "\n"
          << "threshold_high: " << thresholdHigh << "\n";
        return f.good();
    }
    f << "# score z = intercept + sum coef*(x-mean)/scale ; p = sigmoid(calibA*z+calibB)\n";
    f << "version: "   << version   << "\n";
    f << "trained: "   << trainedOn << "\n";
    f << "needle: "    << needleRev << "\n";
    f << "endpoint: "  << endpoint  << "\n";
    f << "n_cases: "   << nCases    << "\n";
    f << "n_positive: "<< nPositive << "\n";
    if (std::isfinite(auc))     f << "auc: " << auc << "\n";
    if (std::isfinite(aucCiLo)) f << "auc_ci: " << aucCiLo << " " << aucCiHi << "\n";
    f << "intercept: " << intercept << "\n";
    f << "calib: "     << calibA << " " << calibB << "\n";
    f << "threshold_low: "  << thresholdLow  << "\n";
    f << "threshold_high: " << thresholdHigh << "\n";
    for (const ModelTerm& t : terms)
        f << "feature: " << t.name << " " << t.mean << " " << t.scale << " " << t.coef << "\n";
    return f.good();
}

bool RiskModel::load(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }

    *this = RiskModel();
    terms.clear();

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::istringstream v(line.substr(colon + 1));

        if      (key == "version")   v >> version;
        else if (key == "trained")   v >> trainedOn;
        else if (key == "needle")    v >> needleRev;
        else if (key == "endpoint")  { std::string rest; std::getline(v, rest);
                                       size_t a = rest.find_first_not_of(" \t");
                                       endpoint = (a == std::string::npos) ? "" : rest.substr(a); }
        else if (key == "n_cases")   v >> nCases;
        else if (key == "n_positive")v >> nPositive;
        else if (key == "auc")       v >> auc;
        else if (key == "auc_ci")    v >> aucCiLo >> aucCiHi;
        else if (key == "intercept") v >> intercept;
        else if (key == "calib")     v >> calibA >> calibB;
        else if (key == "threshold_low")  v >> thresholdLow;
        else if (key == "threshold_high") v >> thresholdHigh;
        else if (key == "band") {
            isBand = true;
            v >> bandFeature >> bandLo >> bandHi;
            bool known = false;
            Features probe; probe.byName(bandFeature, &known);
            if (!known) {
                if (error) *error = "band model wants unknown feature '" + bandFeature + "'";
                return false;
            }
        }
        else if (key == "band_sharpness") v >> bandSharpness;
        else if (key == "sus_margin")     v >> susMarginIps;
        else if (key == "feature") {
            ModelTerm t;
            v >> t.name >> t.mean >> t.scale >> t.coef;
            if (t.name.empty()) {
                if (error) *error = "malformed feature on line " + std::to_string(lineNo);
                return false;
            }
            bool known = false;
            Features probe; probe.byName(t.name, &known);
            if (!known) {
                if (error) *error = "model wants unknown feature '" + t.name + "'";
                return false;
            }
            terms.push_back(t);
        }
    }

    if (!valid()) {
        if (error) *error = "model file loaded but failed validation";
        return false;
    }
    return true;
}

} // namespace birx
