// tests/selftest.cpp — the properties that must not regress.
//
// Every test here corresponds to something that went wrong in a real trial.

#include "lotfit.h"
#include "../birxcore/parser.h"
#include "../birxcore/featureset.h"
#include "../birxcore/quality.h"
#include "../birxcore/decision.h"
#include "../birxcore/dataset.h"
#include "../birxcore/cole.h"
#include "../birxcore/opencal.h"
#include "../birxcore/patient.h"
#include "../birxcore/needlecode.h"
#include "../birxcore/readiness.h"
#include "../birxcore/lotcal.h"
#include "../birxcore/audiocue.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <complex>

using namespace birx;

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg) do { ++checks; if (!(cond)) { \
    std::cout << "  FAIL  " << msg << "   [" << #cond << " @" << __LINE__ << "]\n"; ++failures; } } while (0)

#define NEAR(a, b, tol, msg) do { ++checks; if (!(std::fabs((a)-(b)) <= (tol))) { \
    std::cout << "  FAIL  " << msg << "   got " << (a) << " want " << (b) << " @" << __LINE__ << "\n"; ++failures; } } while (0)

// Build a synthetic sweep whose phase is exactly linear in log10(f).
static Spectrum makeSweep(double slopePerDecade, double phaseAt100k,
                          double z1kOhm, int range = 3, double noise = 0.0,
                          unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, noise);
    Spectrum s;
    s.caseId = "T1"; s.siteIndex = 1; s.needleRev = "NDL-C"; s.epochMs = 1000;
    for (double f : InstrumentConstants::sweepFrequencies()) {
        Point p;
        p.freqHz = f;
        // impedance falls with frequency; anchored so that |Z|(1 kHz) = z1kOhm
        p.magOhm = z1kOhm * std::pow(1000.0 / f, 0.25);
        p.phaseDeg = phaseAt100k + slopePerDecade * (std::log10(f) - std::log10(100000.0));
        if (noise > 0) p.phaseDeg += nd(rng);
        p.range = range;
        s.points.push_back(p);
    }
    return s;
}

static void testParser() {
    std::cout << "parser\n";
    Parser P;
    std::vector<Point> pts;
    int ends = 0;
    double ips = 0, z = 0; std::string text;
    P.onPoint   = [&](const Point& p){ pts.push_back(p); };
    P.onMarker  = [&](Marker m){ if (m == Marker::End) ++ends; };
    P.onReading = [&](double i, double zz, const std::string& t){ ips = i; z = zz; text = t; };

    // the 4th field is the gain range, and it must survive
    P.feedLine("[imp05000000,00085000,-021.50,4]");
    CHECK(pts.size() == 1, "an imp packet yields one point");
    if (!pts.empty()) {
        NEAR(pts[0].freqHz, 500000.0, 1e-6, "frequency is divided by ten");
        NEAR(pts[0].magOhm, 8500.0, 1e-6, "magnitude is divided by ten");
        NEAR(pts[0].phaseDeg, -21.5, 1e-9, "phase passes through unchanged");
        CHECK(pts[0].range == 4, "the gain range is kept, not dropped");
    }

    // a three-field packet still parses, with range unknown
    pts.clear();
    P.feedLine("imp00010000,00004200,-005.00");
    CHECK(pts.size() == 1 && pts[0].range == 0, "a 3-field packet parses with range 0");

    // reading and end marker merged on one line: both must be seen, data first
    P.feedLine("IPS-05.00,Z00850.00,High Risk[E]");
    NEAR(ips, -5.0, 1e-9, "merged line yields the reading");
    NEAR(z, 850.0, 1e-9, "merged line yields z1k");
    CHECK(text == "High Risk", "the device text is kept");
    CHECK(ends == 1, "the end marker on a merged line is not lost");

    // a line split across two reads
    pts.clear();
    P.feed("[imp00100000,000");
    P.feed("42000,-010.00,2]\r\n");
    CHECK(pts.size() == 1, "a packet split across two reads is reassembled");

    // malformed numbers must be rejected, never coerced to zero
    pts.clear();
    P.feedLine("imp00010000,ABCDEF,-005.00,3");
    CHECK(pts.empty(), "a malformed point is dropped, not turned into zero");

    double v = 0;
    CHECK(Parser::parseNumber("\xDB\xB1\xDB\xB2\xDB\xB3", &v) && v == 123, "Persian digits parse");
    CHECK(!Parser::parseNumber("12x3", &v), "trailing junk is rejected");
}

static void testSlope() {
    std::cout << "slope estimation\n";
    FeatureExtractor fx;

    // A clean linear sweep: both estimators must recover the slope, and the
    // fitted one must equal the legacy two-point one when there is no noise.
    Spectrum s = makeSweep(-7.0, -30.0, 800.0);
    Features f = fx.extract(s);
    NEAR(f.ipsFit, -7.0, 1e-6, "fitted slope recovers a clean line");
    NEAR(f.ipsLegacy, -7.0, 1e-6, "legacy IPS recovers the same clean line");
    NEAR(f.ipsTheilSen, -7.0, 1e-6, "Theil-Sen recovers the same clean line");
    NEAR(f.z1k, 800.0, 1e-6, "z1k is read from the 1 kHz point");

    // The 1.43 constant is 1/log10(5) — i.e. the firmware's IPS is already a
    // slope per decade, so the two definitions share a unit.
    NEAR(InstrumentConstants::kIpsPerDecade, 1.0/std::log10(5.0), 1e-12,
         "1.43 is 1/log10(5), a per-decade normalisation");

    // With noise, the fitted slope must be materially less variable than the
    // two-point secant. This is the whole argument for the change.
    const double sigma = 1.0;
    double sseFit = 0, sseLegacy = 0;
    const int N = 400;
    for (int i = 0; i < N; ++i) {
        Spectrum n = makeSweep(-7.0, -30.0, 800.0, 3, sigma, 100 + i);
        Features g = fx.extract(n);
        sseFit    += (g.ipsFit - (-7.0)) * (g.ipsFit - (-7.0));
        sseLegacy += (g.ipsLegacy - (-7.0)) * (g.ipsLegacy - (-7.0));
    }
    const double sdFit = std::sqrt(sseFit/N), sdLegacy = std::sqrt(sseLegacy/N);
    std::cout << "    noise sd: fitted " << sdFit << "  legacy " << sdLegacy
              << "   ratio " << (sdFit/sdLegacy) << "\n";
    CHECK(sdFit < 0.75 * sdLegacy, "the fitted slope is at least 25% less noisy than the 2-point secant");

    // The LEVEL of the phase curve is a feature in its own right, separate
    // from its slope. Two sweeps with identical slopes and different offsets
    // must be distinguishable.
    {
        Spectrum a = makeSweep(-7.0, -30.0, 800.0);
        Spectrum b = makeSweep(-7.0, -45.0, 800.0);
        Features fa = fx.extract(a), fb2 = fx.extract(b);
        NEAR(fa.phaseAtRefDeg, -30.0, 1e-6, "phase level is recovered at the reference frequency");
        NEAR(fb2.phaseAtRefDeg, -45.0, 1e-6, "a different level is recovered");
        NEAR(fa.ipsFit, fb2.ipsFit, 1e-9, "the slope is unchanged by the level");
    }

    // One corrupted reading: Theil-Sen must survive it better than least squares.
    Spectrum bad = makeSweep(-7.0, -30.0, 800.0);
    for (Point& p : bad.points) if (std::fabs(p.freqHz - 500000.0) < 1) p.phaseDeg += 25.0;
    Features fb = fx.extract(bad);
    CHECK(std::fabs(fb.ipsTheilSen + 7.0) < std::fabs(fb.ipsFit + 7.0),
          "Theil-Sen resists a single corrupted reading better than least squares");
    CHECK(std::fabs(fb.ipsLegacy + 7.0) > 10.0,
          "the 2-point secant is destroyed by a single corrupted 500 kHz reading");
}

static void testFatGate() {
    std::cout << "placement gate\n";
    DecisionEngine eng;

    RiskModel m;
    m.version = "test"; m.needleRev = "NDL-C"; m.endpoint = "test";
    m.terms.push_back({ "ipsFit", 0.0, 1.0, -1.0 });
    m.intercept = 0.0; m.thresholdLow = 0.3; m.thresholdHigh = 0.7;
    eng.setModel(m);

    // A lesion reading: answerable.
    Verdict good = eng.decide(makeSweep(-7.0, -30.0, 800.0));
    CHECK(good.answered(), "a clean in-lesion reading is answered");
    CHECK(good.tier == Tier::High, "a strongly negative slope reads High");

    // The same electrical reading, but the tip is in fat. This is the case the
    // shipped software reported as Low Risk; 13 of 17 false negatives in the
    // India cohort were exactly this.
    Verdict fat = eng.decide(makeSweep(-7.0, -30.0, 4000.0));
    CHECK(fat.tier == Tier::Invalid, "a reading in fat is Invalid, never Low Risk");
    CHECK(fat.quality.has(Flag::TipInFat), "the reason is recorded as tip-in-fat");
    CHECK(!fat.reason.empty(), "the operator is told what to do");

    // A benign-looking slope in fat must ALSO be Invalid: the failure mode is
    // silence, not a wrong tier.
    Verdict fatBenign = eng.decide(makeSweep(+3.0, -10.0, 4000.0));
    CHECK(fatBenign.tier == Tier::Invalid, "fat is Invalid regardless of the slope");

    // No model installed: abstain rather than guess.
    DecisionEngine bare;
    Verdict none = bare.decide(makeSweep(-7.0, -30.0, 800.0));
    CHECK(none.tier == Tier::Invalid, "with no model installed the device abstains");

    // Model fitted for another needle.
    RiskModel other = m; other.needleRev = "NDL-A";
    DecisionEngine mism; mism.setModel(other);
    Verdict wrongNeedle = mism.decide(makeSweep(-7.0, -30.0, 800.0));
    CHECK(wrongNeedle.tier == Tier::Invalid, "a model from another needle is refused");
}

static void testAggregation() {
    std::cout << "per-case aggregation\n";
    RiskModel m;
    m.version = "t"; m.needleRev = "NDL-C"; m.endpoint = "t";
    m.terms.push_back({ "ipsFit", 0.0, 1.0, -1.0 });
    m.thresholdLow = 0.3; m.thresholdHigh = 0.7;
    DecisionEngine eng; eng.setModel(m);
    Aggregator agg;

    // Six readings, one of which strays into the positive region. The shipped
    // "any reading in the box" rule would call the whole case positive; the
    // median must not.
    std::vector<Verdict> six;
    for (int i = 0; i < 5; ++i) six.push_back(eng.decide(makeSweep(+2.0, -10.0, 800.0)));
    six.push_back(eng.decide(makeSweep(-1.0, -20.0, 800.0)));   // spread 3.0, inside the agreement limit
    CaseResult r6 = agg.combine(six, m);
    CHECK(r6.tier == Tier::Low, "one stray reading among six does not carry the case");

    // The same lesion with only that one reading taken would be positive — the
    // point being that the number of readings no longer changes the verdict
    // direction by itself.
    std::vector<Verdict> one { eng.decide(makeSweep(-1.0, -20.0, 800.0)) };
    CaseResult r1 = agg.combine(one, m);
    CHECK(!r1.complete && r1.stillNeeded == 4,
          "a single reading is provisional and asks for four more");

    // Early stop: three agreeing positive readings finish the case before the
    // quota of five, because more passes cannot change the management.
    std::vector<Verdict> pos3;
    for (int i = 0; i < 3; ++i) pos3.push_back(eng.decide(makeSweep(-1.0, -20.0, 800.0)));
    CaseResult rp = agg.combine(pos3, m);
    CHECK(rp.tier == Tier::High && rp.complete && rp.stoppedEarly,
          "three positive readings stop the case early");

    // But two do not: with two readings the median is their mean, so one
    // stray measurement half-carries the case.
    std::vector<Verdict> pos2(pos3.begin(), pos3.begin() + 2);
    CaseResult rp2 = agg.combine(pos2, m);
    CHECK(!rp2.complete && !rp2.stoppedEarly,
          "two positive readings are not enough to stop early");

    // A negative case runs to the full five and is not complete at four.
    std::vector<Verdict> neg4;
    for (int i = 0; i < 4; ++i) neg4.push_back(eng.decide(makeSweep(+2.0, -10.0, 800.0)));
    CHECK(!agg.combine(neg4, m).complete, "a negative case is not settled at four readings");
    neg4.push_back(eng.decide(makeSweep(+2.0, -10.0, 800.0)));
    CaseResult rn5 = agg.combine(neg4, m);
    CHECK(rn5.complete && !rn5.stoppedEarly && rn5.tier == Tier::Low,
          "a negative case settles at five readings");

    // H7-2.14.0: the case result by count (Settings -> Rules): High from 1, Low from 3, closed once final
    {
        RuleTable rt;
        rt.caseHigh = 1; rt.caseLow = 3; rt.caseClose = true;
        std::vector<Verdict> h1 { eng.decide(makeSweep(-1.0, -20.0, 800.0)) };
        CaseResult c1 = agg.combine(h1, m, &rt);
        CHECK(c1.tier == Tier::High && c1.complete && c1.closed && c1.byCount && c1.stoppedEarly && c1.nHigh == 1,
              "count: one high reading makes the lesion High Risk, final and closed");
        std::cout << "  " << c1.reason << "\n";
        std::vector<Verdict> l2 { eng.decide(makeSweep(+2.0, -10.0, 800.0)), eng.decide(makeSweep(+2.0, -10.0, 800.0)) };
        CaseResult c2 = agg.combine(l2, m, &rt);
        CHECK(!c2.complete && !c2.closed && c2.nLow == 2 && c2.reason.find("So far 0 high, 2 low") != std::string::npos,
              "count: two low readings are provisional, and say how far each count has come");
        std::cout << "  " << c2.reason << "\n";
        l2.push_back(eng.decide(makeSweep(+2.0, -10.0, 800.0)));
        CaseResult c3 = agg.combine(l2, m, &rt);
        CHECK(c3.tier == Tier::Low && c3.complete && c3.closed && c3.byCount, "count: the third low reading makes it Low Risk, final");
        std::vector<Verdict> mix { eng.decide(makeSweep(+2.0, -10.0, 800.0)), eng.decide(makeSweep(+2.0, -10.0, 800.0)),
                                   eng.decide(makeSweep(-1.0, -20.0, 800.0)) };
        CHECK(agg.combine(mix, m, &rt).tier == Tier::High, "count: high is tested first (two low and one high: High Risk)");
        rt.caseClose = false;
        CHECK(!agg.combine(h1, m, &rt).closed && agg.combine(h1, m, &rt).complete, "not closed when the setting is off");
        RuleTable off;
        CHECK(!agg.combine(h1, m, &off).complete, "counts off: one high reading is provisional, as before");
    }

    // All readings in fat: the case is Invalid, not Low.
    std::vector<Verdict> allFat;
    for (int i = 0; i < 4; ++i) allFat.push_back(eng.decide(makeSweep(-7.0, -30.0, 5000.0)));
    CaseResult rf = agg.combine(allFat, m);
    CHECK(rf.tier == Tier::Invalid, "a case measured entirely in fat is Invalid");
    CHECK(rf.nValid == 0, "no reading counted as valid");

    // Readings that disagree wildly must not be averaged into a confident answer.
    std::vector<Verdict> spread {
        eng.decide(makeSweep(-9.0, -30.0, 800.0)),
        eng.decide(makeSweep(+4.0, -10.0, 800.0)),
        eng.decide(makeSweep(-1.0, -18.0, 800.0))
    };
    CaseResult rs = agg.combine(spread, m);
    CHECK(rs.tier == Tier::Invalid, "readings that disagree too much are refused, not averaged");
}

static void testRangeCalibration() {
    std::cout << "range self-calibration\n";
    // Build a sweep where range 4 reads 2.0 degrees high relative to range 3.
    Spectrum s = makeSweep(-7.0, -30.0, 900.0, 3);
    for (Point& p : s.points) {
        if (p.freqHz < 60000.0) { p.range = 4; p.phaseDeg += 2.0; }
    }
    RangeCalibrator cal;
    for (int i = 0; i < 20; ++i) cal.observe(s);
    const double off = cal.offsetDeg(4);
    std::cout << "    learned offset for range 4: " << off << " deg (want about -2)\n";
    NEAR(off, -2.0, 0.6, "the step between two ranges is recovered from the sweep itself");
    CHECK(cal.calibrated(4), "range 4 is calibrated after enough observations");

    Spectrum fixed = cal.correct(s);
    FeatureExtractor fx;
    const double before = fx.extract(s).fitRmsDeg;
    const double after  = fx.extract(fixed).fitRmsDeg;
    std::cout << "    fit RMS before " << before << "  after " << after << "\n";

    // Round-trip through a file.
    CHECK(cal.save("/tmp/birx_cal_test.txt"), "calibration saves");
    RangeCalibrator loaded;
    CHECK(loaded.load("/tmp/birx_cal_test.txt"), "calibration loads");
    NEAR(loaded.offsetDeg(4), off, 1e-6, "calibration survives a save/load round trip");
    std::remove("/tmp/birx_cal_test.txt");
}

// Build a sweep that IS a Cole arc with known parameters, so the fit has a
// right answer to be judged against.
static Spectrum makeColeSweep(double r0, double rInf, double fc, double alpha,
                              double noiseRel = 0.0, unsigned seed = 7) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, noiseRel);
    Spectrum s;
    s.caseId = "C"; s.siteIndex = 1; s.needleRev = "NDL-C"; s.epochMs = 1;
    for (double f : InstrumentConstants::sweepFrequencies()) {
        double re = 0, im = 0;
        coleImpedance(f, r0, rInf, fc, alpha, &re, &im);
        if (noiseRel > 0) { const double m0 = std::hypot(re, im); re += nd(rng)*m0; im += nd(rng)*m0; }
        Point p;
        p.freqHz   = f;
        p.magOhm   = std::hypot(re, im);
        p.phaseDeg = std::atan2(im, re) * 180.0 / 3.14159265358979323846;
        p.range    = 3;
        s.points.push_back(p);
    }
    return s;
}

static void testCole() {
    std::cout << "Cole fit\n";

    const double R0 = 1400.0, RINF = 420.0, FC = 48000.0, ALPHA = 0.72;
    ColeFit c = fitCole(makeColeSweep(R0, RINF, FC, ALPHA));
    std::cout << "    clean:  R0 " << c.r0 << "  Rinf " << c.rInf
              << "  fc " << c.fcHz << "  alpha " << c.alpha
              << "  rms " << c.rmsRel << "  iters " << c.iterations << "\n";
    CHECK(c.ok, "a clean Cole arc fits");
    NEAR(c.r0,    R0,    R0*0.02,    "R0 is recovered");
    NEAR(c.rInf,  RINF,  RINF*0.02,  "Rinf is recovered");
    NEAR(c.fcHz,  FC,    FC*0.10,    "fc is recovered");
    NEAR(c.alpha, ALPHA, 0.05,       "alpha is recovered");
    NEAR(c.ratio(), R0/RINF, 0.1,    "the R0/Rinf ratio is recovered");

    // 1% measurement noise on every point: the parameters must survive it,
    // which is the reason for fitting an arc instead of reading two points.
    ColeFit n = fitCole(makeColeSweep(R0, RINF, FC, ALPHA, 0.01, 11));
    std::cout << "    1% noise: R0 " << n.r0 << "  Rinf " << n.rInf
              << "  ratio " << n.ratio() << "  rms " << n.rmsRel << "\n";
    CHECK(n.ok, "the fit survives 1% noise");
    NEAR(n.ratio(), R0/RINF, 0.35, "the ratio survives 1% noise");

    // The ratio is dimensionless, so scaling every impedance — which is what a
    // change of needle geometry does to first order — must leave it alone
    // while R0 and Rinf both move.
    ColeFit scaled = fitCole(makeColeSweep(R0*1.8, RINF*1.8, FC, ALPHA));
    CHECK(scaled.ok, "a geometrically scaled arc still fits");
    NEAR(scaled.ratio(), c.ratio(), 0.05, "R0/Rinf is unchanged by a scale factor");
    CHECK(std::fabs(scaled.r0 - c.r0) > 0.3 * c.r0, "R0 itself does move with the scale factor");

    // Something that is not an arc must be refused, not approximated.
    {
        Spectrum junk = makeColeSweep(R0, RINF, FC, ALPHA);
        std::mt19937 rng(3);
        std::uniform_real_distribution<double> u(-40.0, 40.0);
        for (Point& p : junk.points) p.phaseDeg = u(rng);
        ColeFit bad = fitCole(junk);
        CHECK(!bad.ok, "a sweep that is not an arc yields no Cole parameters");
        CHECK(std::isfinite(bad.rmsRel), "but its residual is still reported, as a quality signal");
    }

    // And the features must carry it through.
    FeatureExtractor fx;
    Features f = fx.extract(makeColeSweep(R0, RINF, FC, ALPHA));
    CHECK(std::isfinite(f.coleRatio), "coleRatio reaches the feature set");
    NEAR(f.coleLogFc, std::log10(FC), 0.06, "coleLogFc reaches the feature set");
    CHECK(std::isfinite(f.coleAlpha), "coleAlpha reaches the feature set");
}

// A sweep as the instrument would see it with a parasitic Cp across the tip.
static Spectrum withParasitic(const Spectrum& truth, double cpFarad, double gp = 0.0) {
    Spectrum out = truth;
    for (Point& p : out.points) {
        const double w = 2*3.14159265358979323846*p.freqHz;
        const double th = p.phaseDeg*3.14159265358979323846/180.0;
        std::complex<double> z = std::polar(p.magOhm, th);
        std::complex<double> y = 1.0/z + std::complex<double>(gp, w*cpFarad);
        z = 1.0/y;
        p.magOhm = std::abs(z);
        p.phaseDeg = std::arg(z)*180.0/3.14159265358979323846;
    }
    return out;
}

static Spectrum openSweep(double cpFarad, double gp, int range) {
    Spectrum s;
    s.needleRev = "NDL-C";
    for (double f : InstrumentConstants::sweepFrequencies()) {
        const double w = 2*3.14159265358979323846*f;
        std::complex<double> y(gp, w*cpFarad);
        std::complex<double> z = 1.0/y;
        Point p; p.freqHz=f; p.magOhm=std::abs(z);
        p.phaseDeg=std::arg(z)*180.0/3.14159265358979323846; p.range=range;
        s.points.push_back(p);
    }
    return s;
}

static void testOpenCal() {
    std::cout << "open calibration\n";
    const double CP = 120e-12;

    NeedleCalibration cal;
    cal.needleId="N001"; cal.batchId="B7"; cal.needleRev="NDL-C"; cal.takenAt="2026-09-15";
    std::string why;
    CHECK(cal.addOpenSweep(3, openSweep(CP, 2e-6, 3), {}, &why), "an open sweep calibrates: " + why);
    CHECK(cal.calibrated(3), "range 3 is calibrated");
    CHECK(!cal.calibrated(4), "an uncalibrated range stays uncalibrated");
    NEAR(cal.ranges().at(3).open.cpFarad*1e12, CP*1e12, 2.0, "Cp is recovered from the open sweep");

    /* A NEEDLE ON A LONG CABLE MUST STILL CALIBRATE.
     *
     * This is the fault that made the routine unusable in the field. The
     * config calls a parasitic of up to 2000 pF plausible, and the bridge
     * test used to be applied to every point in the fit band — so a needle
     * above about 318 pF went below 1000 ohm at the top of its own sweep and
     * the whole calibration was refused, with a message telling the operator
     * to clean a tip that was already clean. The two constants contradicted
     * each other and the larger needle lost.
     *
     * Checked at three sizes across the plausible range, because the fault
     * was invisible at the small one the first test happened to use. */
    for (double cp : { 400e-12, 900e-12, 1800e-12 }) {
        NeedleCalibration big;
        big.needleId = "N002"; big.needleRev = "NDL-C";
        std::string bigWhy;
        const bool ok = big.addOpenSweep(3, openSweep(cp, 2e-6, 3), {}, &bigWhy);
        CHECK(ok, "a " + std::to_string(int(cp*1e12)) + " pF needle calibrates: " + bigWhy);
        if (ok)
            NEAR(big.ranges().at(3).open.cpFarad*1e12, cp*1e12, cp*1e12*0.1,
                 "and its Cp is recovered");
    }

    // A sweep that is not an open must be refused, not fitted.
    Spectrum tissue = makeSweep(-7.0, -30.0, 800.0, 3);
    CHECK(!cal.addOpenSweep(5, tissue, {}, &why), "a tissue sweep is refused as an open");
    CHECK(!why.empty(), "and says why");

    /* A GENUINELY BRIDGED TIP IS STILL REFUSED, and this is the check that
     * says loosening the per-point gate did not loosen the safety one. A
     * bridge is a resistance: low at every frequency, including the bottom of
     * the sweep, which is exactly where an open circuit is at its largest. */
    {
        Spectrum bridged;
        for (double f : InstrumentConstants::sweepFrequencies()) {
            Point p; p.freqHz = f; p.magOhm = 220.0; p.phaseDeg = -1.0; p.range = 3;
            bridged.points.push_back(p);
        }
        std::string bWhy;
        NeedleCalibration b;
        CHECK(!b.addOpenSweep(3, bridged, {}, &bWhy), "a bridged tip is refused");
        CHECK(bWhy.find("220") != std::string::npos,
              "and the refusal carries the measured impedance, not just a sentence: " + bWhy);
    }

    // The correction must undo the parasitic on a real measurement.
    Spectrum truth = makeSweep(-7.0, -30.0, 800.0, 3);
    Spectrum seen  = withParasitic(truth, CP, 2e-6);
    for (Point& p : seen.points) p.range = 3;

    FeatureExtractor fx;
    const double ipsTrue = fx.extract(truth).ipsFit;
    const double ipsSeen = fx.extract(seen).ipsFit;
    Spectrum fixed = seen;
    const int missed = cal.correct(&fixed);
    const double ipsFixed = fx.extract(fixed).ipsFit;

    std::cout << "    ipsFit: true " << ipsTrue << "  with " << CP*1e12
              << " pF " << ipsSeen << "  corrected " << ipsFixed << "\n";
    CHECK(missed == 0, "every point was corrected");
    CHECK(std::fabs(ipsSeen - ipsTrue) > 2.0, "the parasitic really does corrupt IPS");
    NEAR(ipsFixed, ipsTrue, 0.15, "the correction recovers the true slope");

    // Round trip through a file.
    CHECK(cal.save("/tmp/birx_cal.txt"), "calibration saves");
    NeedleCalibration back;
    CHECK(back.load("/tmp/birx_cal.txt"), "calibration loads");
    NEAR(back.ranges().at(3).open.cpFarad, cal.ranges().at(3).open.cpFarad, 1e-15,
         "Cp survives the round trip");
    CHECK(back.batchId == "B7", "the batch id survives — the whole point of the log");

    // The fleet log, and the decision arithmetic on top of it.
    std::remove("/tmp/birx_callog.csv");
    CHECK(cal.appendToLog("/tmp/birx_callog.csv"), "a calibration appends to the fleet log");
    NeedleCalibration c2 = cal; c2.needleId = "N002";
    c2.appendToLog("/tmp/birx_callog.csv");
    std::string lerr;
    std::vector<CalLogRow> log = readCalLog("/tmp/birx_callog.csv", &lerr);
    CHECK(lerr.empty() && log.size() == 2, "the log reads back: " + lerr);
    if (log.size() == 2) {
        CHECK(log[0].batchId == "B7", "batch id round-trips through the log");
        NEAR(log[0].cpFarad*1e12, CP*1e12, 2.0, "Cp round-trips through the log");
    }

    // 1 pF at 600 ohm, 500 kHz is about 0.11 deg — the number the whole
    // per-needle-or-per-batch decision turns on.
    NEAR(std::fabs(phaseErrorDeg(1e-12, 600.0, 500e3)), 0.108, 0.01,
         "1 pF of error is 0.11 deg at the reference point");
    CHECK(std::fabs(phaseErrorDeg(1.4e-12, 600.0, 500e3)) > 0.15,
          "1.4 pF already exceeds the phase budget");

    std::remove("/tmp/birx_cal.txt");
    std::remove("/tmp/birx_callog.csv");
}

static void testCaseId() {
    std::cout << "case id\n";
    CaseIdGenerator g;
    std::string why;
    CHECK(!g.ready(), "no case ids before a device id is assigned");
    CHECK(g.next(100).empty(), "and next() refuses rather than minting something colliding");

    // A derived id is the trap this refuses to fall into: four base32
    // characters hashed from a MAC collides ~4800 times over 100k devices.
    CHECK(!g.setDeviceId("ABC", &why), "a 3-character device id is refused");
    CHECK(!why.empty(), "and explains why");
    CHECK(g.setDeviceId("a3k7", &why), "a 4-character device id is accepted, case-insensitively");
    CHECK(g.deviceId() == "A3K7", "and normalised to upper case");

    const std::string a = g.next(1000);
    const std::string b = g.next(1000);
    std::cout << "    minted " << a << "  then  " << b << "\n";
    CHECK(a.size() == 12, "10 characters in 3 dashed groups: XXXX-XXX-XXX");
    CHECK(a != b, "the sequence advances");
    CHECK(CaseIdGenerator::validate(a), "a minted id validates");
    CHECK(CaseIdGenerator::validate(b), "so does the next one");

    // a new day restarts the sequence but cannot collide, because the day is
    // part of the id
    const std::string c = g.next(1001);
    CHECK(c != a && c != b, "a new day does not collide with the previous day");
    CHECK(CaseIdGenerator::validate(c), "and validates");

    // the check character has to earn its place
    std::string bad = CaseIdGenerator::normalise(a);
    CHECK(bad.size() == 10, "normalise strips the dashes");
    for (size_t i = 0; i + 1 < 9; ++i) {
        if (bad[i] == bad[i+1]) continue;
        std::string t = bad; std::swap(t[i], t[i+1]);
        CHECK(!CaseIdGenerator::validate(t), "a transposition is caught");
        break;
    }
    {
        std::string t = bad;
        t[2] = (t[2] == 'Z') ? 'Y' : 'Z';
        CHECK(!CaseIdGenerator::validate(t), "a substitution is caught");
    }
    // Crockford forgiveness: someone writes O for 0 and I for 1
    CHECK(CaseIdGenerator::normalise("O1I0") == "0110", "O reads as 0 and I as 1");

    // state survives a restart, so the counter never reuses a number
    CHECK(g.save("/tmp/birx_caseid.txt"), "id state saves");
    CaseIdGenerator g2;
    CHECK(g2.load("/tmp/birx_caseid.txt"), "id state loads");
    CHECK(g2.next(1001) != c, "a restart does not reissue the last id");
    std::remove("/tmp/birx_caseid.txt");
}

static void testNeedleCode() {
    std::cout << "needle code\n";

    // RFC 4231 test case 2 — proves the HMAC, not just that it is consistent
    // with itself. A hand-rolled MAC that only agrees with its own output is
    // worth nothing.
    {
        const std::vector<uint8_t> m = hmacSha256("Jefe", "what do ya want for nothing?");
        static const char* hx = "0123456789abcdef";
        std::string got;
        for (uint8_t b : m) { got.push_back(hx[b >> 4]); got.push_back(hx[b & 15]); }
        CHECK(got == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
              "HMAC-SHA256 matches RFC 4231 case 2");
    }
    CHECK(sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 matches the FIPS 180-4 example");

    NeedleKeyring kr;
    CHECK(kr.check("K4M2", "9F7T03BQZ") == NeedleStatus::NoKey,
          "with no key installed nothing validates — it refuses, it does not accept");

    kr.addKey('K', "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

    const std::string code = kr.mint("K4M2", "9F7T0");
    CHECK(code.size() == 14 && code[4] == '-',
          "a minted code is LOT-SERIALCHECK");

    const std::string lot    = code.substr(0, 4);
    const std::string serial = code.substr(5);
    std::string canon;
    CHECK(kr.check(lot, serial, &canon) == NeedleStatus::Ok, "a minted code validates");
    CHECK(canon == lot + serial.substr(0, 5), "the canonical form is lot + serial");

    // Typed forgiveness, exactly as the case id
    CHECK(kr.check(lot, " " + serial.substr(0,5) + "-" + serial.substr(5)) == NeedleStatus::Ok,
          "spaces and dashes in the typed serial are ignored");

    // The whole point: an invented code does not pass.
    int passes = 0;
    for (int i = 0; i < 4096; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%05d%04d", i, (i * 7919) % 10000);
        if (kr.check(lot, buf) == NeedleStatus::Ok) ++passes;
    }
    CHECK(passes == 0, "4096 invented serials all read INVALID");

    // One character wrong anywhere in the serial fails.
    std::string bad = serial;
    bad[2] = (bad[2] == 'Z') ? 'Y' : 'Z';
    CHECK(kr.check(lot, bad) == NeedleStatus::BadCheck, "one wrong character is INVALID");

    // A lot whose key version this device does not hold.
    CHECK(kr.check("X4M2", serial) == NeedleStatus::BadCheck,
          "a code from an unknown key version is refused");

    // Single use, across a restart.
    const std::string reg = "/tmp/birx_needles.txt";
    std::remove(reg.c_str());
    {
        NeedleRegistry r(reg);
        r.load();
        CHECK(!r.used(canon), "a fresh needle is not in the register");
        CHECK(r.markUsed(canon, "A3K7-F2M-0C9", "2026-09-15T11:24:00Z"),
              "the needle is recorded");
        CHECK(r.used(canon), "and is then used");
    }
    {
        NeedleRegistry r2(reg);
        CHECK(r2.load() && r2.used(canon),
              "the register survives a restart — the needle stays used for the life of the device");
    }
    std::remove(reg.c_str());
}

static void testAudioCues() {
    std::cout << "audio cues\n";

    CHECK(cueTable().size() >= 8, "the cue table is defined");

    // Every cue has a file name, a label and a reason for sounding as it does.
    for (const CueDef& d : cueTable()) {
        CHECK(d.id && *d.id, "every cue has an id");
        CHECK(d.label && *d.label, "every cue has a label");
        CHECK(d.rationale && std::strlen(d.rationale) > 40,
              std::string("cue ") + d.id + " says why it sounds like that");
        CHECK(!d.notes.empty(), std::string("cue ") + d.id + " has notes");
    }

    // The band the sounds live in, and why. Below 300 Hz a panel speaker gives
    // almost nothing; above 2 kHz age-related hearing loss eats the cue.
    for (const CueDef& d : cueTable())
        for (const Note& n : d.notes)
            if (n.freqHz > 0)
                CHECK(n.freqHz >= 150.0 && n.freqHz <= 1200.0,
                      std::string("cue ") + d.id + " stays in the audible-on-a-small-speaker band");

    // Cues must differ by PATTERN, not only pitch: no two case results may
    // have the same number of tones.
    {
        auto tones = [](Cue c) {
            const CueDef* d = cueDef(c);
            int n = 0;
            if (d) for (const Note& x : d->notes) if (x.freqHz > 0) ++n;
            return n;
        };
        CHECK(tones(Cue::CaseHigh) != tones(Cue::CaseLow) &&
              tones(Cue::CaseHigh) != tones(Cue::CaseSuspicious),
              "the High Risk cue differs from the others in tone COUNT, not only pitch");
    }

    // Nothing in this set is long enough to delay the next placement.
    for (const CueDef& d : cueTable()) {
        int ms = 0;
        for (const Note& n : d.notes) ms += n.ms;
        CHECK(ms <= 900, std::string("cue ") + d.id + " is under 900 ms");
    }

    // ---- the policy ----
    CuePolicy p;
    CHECK(cueForReading(p, true)  == Cue::ReadingCaptured, "a good reading blips");
    CHECK(cueForReading(p, false) == Cue::ReadingRefused,  "a refused reading says so");

    // The verdict is spoken once, when the case is settled — never on a
    // provisional tier. This is the rule that keeps the sound meaningful.
    CHECK(cueForCase(p, Tier::High, false) == Cue::NeedMoreReadings,
          "an unfinished case does not announce a tier");
    CHECK(cueForCase(p, Tier::High, true)  == Cue::CaseHigh,   "a settled case does");
    CHECK(cueForCase(p, Tier::Low,  true)  == Cue::CaseLow,    "and says which one");
    CHECK(cueForCase(p, Tier::Invalid, true) == Cue::CaseInvalid,
          "an unanswerable case gets its own sound, not a result sound");

    p.enabled = false;
    CHECK(cueForReading(p, false) == Cue::None && cueForCase(p, Tier::High, true) == Cue::None,
          "sound off means silent, including for a High Risk result");

    p = CuePolicy();
    p.captureBlip = false;
    CHECK(cueForReading(p, true) == Cue::None, "the capture blip can be turned off alone");
    CHECK(cueForReading(p, false) == Cue::ReadingRefused,
          "but a refused reading is still announced");

    // The WAV renders and is a real RIFF file of a plausible length.
    {
        const CueDef* d = cueDef(Cue::CaseHigh);
        const std::string path = "/tmp/birx_cue.wav";
        CHECK(d && writeCueWav(*d, path), "a cue renders to WAV");
        std::ifstream f(path, std::ios::binary);
        char hdr[12] = {0};
        f.read(hdr, 12);
        CHECK(std::strncmp(hdr, "RIFF", 4) == 0 && std::strncmp(hdr + 8, "WAVE", 4) == 0,
              "and it is a RIFF/WAVE file");
        f.seekg(0, std::ios::end);
        const long bytes = long(f.tellg());
        int ms = 0;
        for (const Note& n : d->notes) ms += n.ms;
        const long want = 44 + long(44100.0 * ms / 1000.0) * 2;
        CHECK(std::labs(bytes - want) < 200, "of the length the note list implies");
        std::remove(path.c_str());
    }
}

static void testLotCalibration() {
    std::cout << "factory lot calibration\n";

    CHECK(factoryTestPlan().size() >= 8, "the bench plan is defined");
    bool haveBoundaries = false, haveRc = false;
    for (const FactoryTest& f : factoryTestPlan()) {
        if (f.id == "boundaries") haveBoundaries = true;
        if (f.id == "rc")         haveRc = true;
    }
    CHECK(haveBoundaries, "the plan tests the gain-range boundaries — the air sweep cannot");
    CHECK(haveRc, "and a known non-zero phase, which a resistor set cannot");

    auto make = [](const char* id, double cpPf, double off) {
        LotCalibration c;
        c.lotId = id; c.needleRev = "NDL-D"; c.takenAt = "2026-09-15";
        c.operatorId = "QA1"; c.nSampled = 8;
        c.cpMeanFarad = cpPf * 1e-12; c.cpSdFarad = 2e-12;
        c.seriesROhm = 1.2; c.phaseRmsDeg = 0.11; c.repeatDeg = 0.09;
        for (int r = 1; r <= 5; ++r) c.rangeOffsetDeg[r] = off;
        c.passed = true;
        return c;
    };

    // The physics behind the threshold: at the median lesion impedance, 1 pF of
    // stray capacitance is about 0.15 IPS units.
    NEAR(lotIpsShift(make("A", 101, 0.0), make("B", 100, 0.0)), 0.148, 0.01,
         "1 pF is 0.148 IPS units at 718 ohm");

    LotCalibrationFile file;
    LotDecision first = file.classify(make("L001", 118, 0.0));
    CHECK(first.verdict == LotVerdict::NeedsNewEntry,
          "the first lot of a needle revision always needs an entry");
    CHECK(first.fleetUpdateRequired(), "and the fleet must be updated for it");
    CHECK(file.add(make("L001", 118, 0.0)), "it is added");
    CHECK(file.revision == 1, "and the revision is bumped");

    // A batch 2 pF away is the same needle as far as any decision is concerned.
    LotDecision same = file.classify(make("L002", 120, 0.05));
    CHECK(same.verdict == LotVerdict::MatchesExisting,
          "2 pF and 0.05 deg away is indistinguishable — no fleet update");
    CHECK(!same.fleetUpdateRequired(), "and it says so");
    CHECK(same.nearestLot == "L001", "naming the lot it matches");

    // 6 pF away is 0.9 IPS units: that moves readings and must be corrected.
    LotDecision diff = file.classify(make("L003", 124, 0.0));
    CHECK(diff.verdict == LotVerdict::NeedsNewEntry, "6 pF away needs its own entry");
    CHECK(diff.ipsShift > 0.8, "because it shifts IPS by nearly one unit");

    // Same capacitance, but a gain-range offset that differs by more than the
    // repeatability floor. The air sweep would never see this.
    LotDecision offs = file.classify(make("L004", 118, 0.4));
    CHECK(offs.verdict == LotVerdict::NeedsNewEntry,
          "a 0.4 deg range-offset difference needs its own entry even at identical Cp");

    // A batch that fails its own bench limits is not calibrated at all.
    { LotCalibration bad = make("L005", 118, 0.0);
      bad.repeatDeg = 0.4;
      CHECK(file.classify(bad).verdict == LotVerdict::Rejected,
            "a batch above the repeatability floor is rejected, not entered");
      CHECK(!file.add(bad), "and cannot be added"); }
    { LotCalibration bad = make("L006", 118, 0.0);
      bad.cpSdFarad = 9e-12;
      CHECK(file.classify(bad).verdict == LotVerdict::Rejected,
            "a batch whose needles differ by 9 pF cannot share one calibration"); }
    { LotCalibration bad = make("L007", 118, 0.0);
      bad.nSampled = 2;
      CHECK(file.classify(bad).verdict == LotVerdict::Rejected,
            "two needles are not a batch statistic"); }

    // A released lot is never silently replaced.
    CHECK(!file.add(make("L001", 200, 0.0)), "an existing lot id cannot be overwritten");

    // The file round-trips, because it is what reaches every device.
    file.issuedAt = "2026-09-15";
    CHECK(file.add(make("L003", 124, 0.0)), "the distinguishable lot is added");
    const std::string path = "/tmp/birx_lotcal.txt";
    CHECK(file.save(path), "the lot file writes");
    LotCalibrationFile back;
    std::string why;
    CHECK(back.load(path, &why), "the lot file reads back: " + why);
    CHECK(back.revision == file.revision && back.lots.size() == file.lots.size(),
          "with its revision and every lot");
    if (back.lots.size() == file.lots.size())
        NEAR(back.lots[0].cpMeanFarad, file.lots[0].cpMeanFarad, 1e-15,
             "and the capacitance survives the round trip");
    std::remove(path.c_str());
}

static void testReadiness() {
    std::cout << "fit readiness\n";

    auto make = [](int nCases, int nNeg, int lots, int ops, double days) {
        std::vector<TrainingRow> rows;
        for (int i = 0; i < nCases; ++i) {
            TrainingRow r;
            r.caseId    = "C" + std::to_string(i);
            r.usable    = true;
            r.pathology = (i < nNeg) ? Pathology::Benign : Pathology::Malignant;
            r.covariates["needle_lot"] = "L" + std::to_string(i % std::max(1, lots));
            r.operatorId = "op" + std::to_string(i % std::max(1, ops));
            r.epochMs   = int64_t(1700000000000LL)
                        + int64_t(double(i) / std::max(1, nCases - 1) * days * 86400000.0);
            rows.push_back(r);
        }
        return rows;
    };

    // A device that has just been switched on.
    {
        Readiness R = assessReadiness({}, 0);
        CHECK(!R.ready(), "a device with no outcomes is not ready to fit");
        CHECK(R.progress() == 0.0, "and its progress is zero, not an average of zeros");
    }

    // Plenty of cases, but every one of them from the same needle lot.
    {
        Readiness R = assessReadiness(make(200, 80, 1, 6, 200), 220);
        CHECK(!R.ready(), "200 cases from one lot are not enough");
        bool lotsBlocked = false;
        for (const Criterion& c : R.criteria) if (c.id == "lots" && !c.met) lotsBlocked = true;
        CHECK(lotsBlocked, "and the lot criterion is the one that says so");
        CHECK(R.progress() < 0.2, "progress reports the WORST criterion, not the average");
    }

    // Plenty of cases and lots, but the outcomes mostly never came back. This
    // is the censoring trap and it must block even though the counts look fine.
    {
        Readiness R = assessReadiness(make(200, 80, 12, 6, 200), 500);
        bool censored = false;
        for (const Criterion& c : R.criteria) if (c.id == "outcomes" && !c.met) censored = true;
        CHECK(censored, "40% outcome return blocks the fit");
        CHECK(!R.ready(), "even with 200 cases in hand");
    }

    // One class barely represented — the Trial 1 situation.
    {
        Readiness R = assessReadiness(make(200, 9, 12, 6, 200), 210);
        bool minority = false;
        for (const Criterion& c : R.criteria) if (c.id == "minority" && !c.met) minority = true;
        CHECK(minority, "9 negatives block the fit however many positives there are");
    }

    // Everything satisfied.
    {
        Readiness R = assessReadiness(make(200, 90, 12, 6, 200), 220);
        CHECK(R.ready(), "200 cases, 90 negative, 12 lots, over 200 days is ready");
        CHECK(R.progress() == 1.0, "and progress is complete");
        CHECK(R.missing().empty(), "with nothing left outstanding");
    }

    // The operator criterion is advisory: it is reported, but one operator
    // does not by itself stop a fit that is otherwise well supported.
    {
        Readiness R = assessReadiness(make(200, 90, 12, 1, 200), 220);
        CHECK(R.ready(), "a single operator is advisory, not blocking");
        bool advisory = false;
        for (const Criterion& c : R.criteria)
            if (c.id == "operators" && !c.blocking && !c.met) advisory = true;
        CHECK(advisory, "but it is still reported as unmet");
    }

    // Readings of one lesion are not several pieces of evidence.
    {
        std::vector<TrainingRow> rows = make(200, 90, 12, 6, 200);
        std::vector<TrainingRow> doubled = rows;
        for (const TrainingRow& r : rows) doubled.push_back(r);   // same case ids
        Readiness a = assessReadiness(rows, 220);
        Readiness b = assessReadiness(doubled, 220);
        double ca = 0, cb = 0;
        for (const Criterion& c : a.criteria) if (c.id == "cases") ca = c.have;
        for (const Criterion& c : b.criteria) if (c.id == "cases") cb = c.have;
        CHECK(ca == cb, "duplicating the readings does not duplicate the case count");
    }
}

static void testBandModel() {
    std::cout << "bench band model\n";

    // The band the needles in production are known to give: risk when IPS lies
    // between -11 and +6, provided the tip is not in fat.
    RiskModel m = RiskModel::band("ipsLegacy", -11.0, 6.0, "NDL-D", "bench-test");
    CHECK(m.valid(), "a band model validates");

    auto tierAt = [&](const RiskModel& mm, double ips) {
        Features f; f.z1k = 800.0; f.ipsLegacy = f.ipsFit = f.ipsTheilSen = ips;
        const double p = mm.probability(f);
        return p >= mm.thresholdHigh ? Tier::High
             : p >= mm.thresholdLow  ? Tier::Suspicious : Tier::Low;
    };

    // The tier must flip exactly on the bench numbers — that is the whole
    // reason a band is not fitted as a logistic model.
    CHECK(tierAt(m, -11.0 + 1e-6) == Tier::High, "just inside the lower edge is High");
    CHECK(tierAt(m, -11.0 - 1e-6) == Tier::Low,  "just outside the lower edge is Low");
    CHECK(tierAt(m,   6.0 - 1e-6) == Tier::High, "just inside the upper edge is High");
    CHECK(tierAt(m,   6.0 + 1e-6) == Tier::Low,  "just outside the upper edge is Low");

    // A logistic model cannot do this: it is monotone, so it cannot be Low on
    // BOTH sides of a High region. This is the property being asserted.
    CHECK(tierAt(m, -30.0) == Tier::Low && tierAt(m, 30.0) == Tier::Low,
          "the band is Low on both sides, which no monotone model can be");

    int wrong = 0;
    for (double x = -40.0; x <= 40.0; x += 0.005) {
        const bool inBand = (x >= -11.0 && x <= 6.0);
        if ((tierAt(m, x) == Tier::High) != inBand) ++wrong;
    }
    CHECK(wrong == 0, "16000 IPS values across the band agree with the bench rule exactly");

    // With no shoulder there is no Suspicious tier at all.
    CHECK(m.thresholdLow == m.thresholdHigh, "sus_margin 0 means no Suspicious tier");

    // A shoulder, when switched on, sits exactly where it is asked to.
    RiskModel s = RiskModel::band("ipsLegacy", -11.0, 6.0, "NDL-D", "bench-sus", 0.4, 1.5);
    CHECK(s.valid(), "a band with a shoulder validates");
    CHECK(tierAt(s, -12.0) == Tier::Suspicious, "1.0 outside the edge is Suspicious");
    CHECK(tierAt(s, -12.6) == Tier::Low,        "1.6 outside the edge is Low again");

    // Missing feature: refuse, never impute.
    { Features f; f.z1k = 800.0;   // ipsLegacy left NaN
      CHECK(!std::isfinite(m.probability(f)),
            "a band model refuses when its feature is missing"); }

    // The file round-trips, because a model that cannot be reloaded cannot be
    // installed on a device.
    const std::string path = "/tmp/birx_band.txt";
    CHECK(m.save(path), "the band model writes");
    RiskModel back; std::string why;
    CHECK(back.load(path, &why), "the band model reads back: " + why);
    CHECK(back.isBand && back.bandLo == -11.0 && back.bandHi == 6.0,
          "the edges survive the round trip");
    for (double x = -20.0; x <= 15.0; x += 0.1)
        if (tierAt(back, x) != tierAt(m, x)) { CHECK(false, "reloaded band scores differently"); break; }
    std::remove(path.c_str());
}

static void testIntake() {
    std::cout << "intake form\n";
    CHECK(!intakeSchema().empty(), "the schema is defined");
    const IntakeField* bir = intakeField("birads");
    CHECK(bir && bir->stratifier && !bir->modelCandidate,
          "BI-RADS is a stratifier, not a model input");
    const IntakeField* nl = intakeField("needle_lot");
    const IntakeField* ns = intakeField("needle_serial");
    CHECK(nl && ns && nl->required && ns->required && nl->page == 1 && ns->page == 1,
          "the needle lot and serial are required on the first page");
    CHECK(nl && ns && !nl->modelCandidate && !ns->modelCandidate,
          "needle identity is provenance, never a model input");
    const IntakeField* an = intakeField("anaesthetic");
    CHECK(an && an->modelCandidate, "anaesthetic timing is offered to the model");

    PatientRecord p;
    p.caseId = "A3K7-F2M-0C9";
    CHECK(!p.complete(), "an empty record is incomplete");
    CHECK(p.missing().size() >= 5, "and names every required field");

    p.set("age","54"); p.set("birads","4A"); p.set("lesion_size","18");
    p.set("side","Left"); p.set("clock","2");
    CHECK(!p.complete(), "the clinical fields alone are not enough — the needle is still missing");
    p.set("needle_lot","K4M2"); p.set("needle_serial","9F7T03BQZ");
    CHECK(p.complete(), "the seven required fields complete it: " +
          (p.missing().empty() ? std::string() : p.missing().front()));

    p.set("age","150");
    CHECK(!p.complete(), "an out-of-range age is rejected");
    p.set("age","54");
    p.set("birads","B4a");
    CHECK(!p.complete(), "a choice outside the option list is rejected");
    p.set("birads","4A");

    p.set("national_id","0012345678");
    CHECK(p.get("national_id") == "0012345678", "the national id is stored separately");
    CHECK(p.nationalId == "0012345678", "and is not one of the schema values");
    NEAR(p.number("age"), 54.0, 1e-9, "numbers parse back");

    const std::string row = p.csvRow();
    CHECK(row.find("A3K7-F2M-0C9") != std::string::npos, "the case id is in the row");
    CHECK(PatientRecord::csvHeader().find("anaesthetic") != std::string::npos,
          "every schema field is a column");
}

static void testModelFile() {
    std::cout << "model file\n";
    RiskModel m;
    m.version = "m2026-09-14-NDL-C"; m.trainedOn = "2026-09-14";
    m.needleRev = "NDL-C"; m.endpoint = "P(tissue required)";
    m.nCases = 226; m.nPositive = 169; m.auc = 0.918; m.aucCiLo = 0.851; m.aucCiHi = 0.969;
    m.terms.push_back({ "ipsFit", -1.85, 2.31, -1.742 });
    m.terms.push_back({ "logZ1k",  2.86, 0.31,  0.204 });
    m.intercept = 0.113; m.calibA = 1.02; m.calibB = -0.05;
    m.thresholdLow = 0.18; m.thresholdHigh = 0.55;
    CHECK(m.valid(), "a fully specified model validates");
    CHECK(m.save("/tmp/birx_model_test.txt"), "model saves");

    RiskModel back; std::string err;
    CHECK(back.load("/tmp/birx_model_test.txt", &err), "model loads: " + err);
    CHECK(back.version == m.version && back.needleRev == m.needleRev, "identity round-trips");
    CHECK(back.terms.size() == 2, "terms round-trip");

    Features f; f.ipsFit = -5.0; f.logZ1k = 2.9;
    NEAR(back.probability(f), m.probability(f), 1e-6, "scores round-trip");

    // A missing feature must produce no score at all — never an imputed one.
    Features partial; partial.ipsFit = -5.0;    // logZ1k left NaN
    CHECK(!std::isfinite(back.probability(partial)), "a missing feature yields no score, not a default");

    // A model file asking for a feature this build does not know must be rejected.
    {
        std::ofstream bad("/tmp/birx_model_bad.txt");
        bad << "version: x\nneedle: n\nintercept: 0\ncalib: 1 0\n"
               "threshold_low: 0.2\nthreshold_high: 0.6\n"
               "feature: someFutureFeature 0 1 1\n";
    }
    RiskModel badm; std::string berr;
    CHECK(!badm.load("/tmp/birx_model_bad.txt", &berr), "an unknown feature is refused");
    std::remove("/tmp/birx_model_test.txt");
    std::remove("/tmp/birx_model_bad.txt");
}

static void testLegacyEquivalence() {
    std::cout << "legacy comparison\n";
    DecisionEngine eng;
    // The firmware rule: z1k < 1500 && -11 < IPS < 0 -> High Risk, else Low.
    Features f;
    f.z1k = 800; f.ipsLegacy = -5.0;
    CHECK(eng.legacyTier(f) == Tier::High, "firmware rule: in band -> High");
    f.ipsLegacy = -12.0;
    CHECK(eng.legacyTier(f) == Tier::Low, "firmware rule: below -11 -> Low (its documented behaviour)");
    f.ipsLegacy = 2.0;
    CHECK(eng.legacyTier(f) == Tier::Low, "firmware rule: positive IPS -> Low");
    f.ipsLegacy = -5.0; f.z1k = 1600;
    CHECK(eng.legacyTier(f) == Tier::Low, "firmware rule: above 1500 ohm -> Low");
}

// The instrument's own decision boxes, in charge while the model is the bench
// band. The point of these checks is that what the settings file says is what
// the screen says — a rule table that does not reach the verdict is worse than
// no rule table, because it looks like a control and is not one.
static void testRuleTable() {
    std::cout << "rule table\n";

    RuleTable t;
    t.active = true;
    t.noisyThresholdOhm = 5000;
    t.defaultLabel = "Low Risk";
    t.noisyLabel   = "Noisy";
    RuleRow high;
    high.zMin = 0; high.zMax = 1900; high.ipsMin = -11; high.ipsMax = 6;
    high.label = "High Risk";
    t.rows.push_back(high);

    int row = -9; std::string label;
    CHECK(t.tierFor(800, -5, &row, &label) == Tier::High, "inside the box -> High Risk");
    CHECK(row == 0, "and it names the row that did it");
    // H7-1.5.0: outside every row, the NEAREST row answers (a table can only
    // speak in its own words); the default is for a table with no row at all.
    CHECK(t.tierFor(800, 9, &row, &label) == Tier::High, "IPS above the only box -> the nearest row");
    CHECK(row == 0 && t.lookup(800, 9).nearest, "and it says it was the nearest, not a match");
    CHECK(!t.lookup(800, -5).nearest, "a match is not marked nearest");
    CHECK(t.tierFor(9000, -5, &row, &label) == Tier::Invalid, "above the noisy threshold -> Invalid");
    CHECK(row == -2, "and it says so");
    CHECK(t.tierFor(kNaN, -5, &row, &label) == Tier::Invalid, "a missing number is never a verdict");

    // The boundary is inclusive on both edges, exactly as the shipped
    // instrument's table was. A patient on the edge must not change tier
    // because the software was rewritten.
    CHECK(t.tierFor(1900, -11, &row, &label) == Tier::High, "the lower corner is inside");
    CHECK(t.tierFor(1900, 6,  &row, &label) == Tier::High, "the upper corner is inside");
    CHECK(t.lookup(1900.01, 6).nearest, "a hair outside is outside (answered as the nearest)");

    // A disabled row is not a row; with none left the default speaks.
    t.rows[0].enabled = false;
    CHECK(t.tierFor(800, -5, &row, &label) == Tier::Low && row == -1, "a disabled row does not fire");
    t.rows[0].enabled = true;

    // A Low row below the band, for the end-to-end checks.
    RuleRow below;
    below.zMin = 0; below.zMax = 1900; below.ipsMin = -300; below.ipsMax = -11.0001;
    below.label = "Low Risk";
    t.rows.push_back(below);

    // An unrecognised label must not become reassurance.
    CHECK(RuleTable::tierOfLabel("Zone 4") == Tier::Suspicious,
          "a label nobody recognises sits in the middle, never at Low");

    // End to end: the engine, with no model at all, still answers from the
    // table — which is the whole point while the model is provisional.
    DecisionEngine eng;
    eng.setRuleTable(t);
    CHECK(eng.rulesDeciding(), "the engine reports that the rules are deciding");
    Spectrum s = makeSweep(-5.0, -30.0, 800.0);
    Verdict v = eng.decide(s);
    CHECK(v.tier == Tier::High, "a sweep inside the box is High with no model installed");
    CHECK(v.modelVersion == t.version, "the record says the rules decided");

    // In fat the gate refuses BEFORE the table is consulted, so the answer is
    // Invalid and not a reassuring Low. That ordering is the safety property:
    // no arrangement of rule boxes can turn a bad placement into a verdict.
    v = eng.decide(makeSweep(-5.0, -30.0, 2400.0));
    CHECK(v.tier == Tier::Invalid, "in fat the gate refuses before the table is read");

    // A slope outside the box, with the tip where it should be: the table's
    // default answers, and it is Low.
    v = eng.decide(makeSweep(-14.0, -30.0, 800.0));
    CHECK(v.tier == Tier::Low, "below the band -> the default, Low Risk");

    // The aggregator's median must read the tiers back unchanged, and must not
    // print a percentage for a number that was never a probability.
    const RiskModel placeholder = eng.scoringModel();
    CHECK(placeholder.isRules, "the aggregator is handed the rules placeholder");
    Aggregator agg;
    std::vector<Verdict> five;
    for (int i = 0; i < 5; ++i) five.push_back(eng.decide(makeSweep(-5.0, -30.0, 800.0)));
    CaseResult cr = agg.combine(five, placeholder);
    CHECK(cr.tier == Tier::High, "five High readings make a High case");
    CHECK(cr.complete, "and the case is complete at five");
    CHECK(cr.reason.find('%') == std::string::npos,
          "the rule table never reports a percentage");

    // Three High and two Low is still High; two High and three Low is not.
    // That is the median doing its job, not a count of chances to trigger —
    // the defect that let a six-reading case get six chances to go positive.
    // Both readings are well placed; only the slope differs, so all five are
    // valid and the count is the only thing being tested.
    //
    // The agreement guard has to be relaxed for this: readings this far apart
    // in IPS would normally be reported as contradictory, which is a separate
    // and correct behaviour tested elsewhere.
    AggregatorConfig loose;
    loose.maxSpreadIps = 100.0;
    Aggregator wide(loose);

    std::vector<Verdict> mixed;
    for (int i = 0; i < 3; ++i) mixed.push_back(eng.decide(makeSweep(-5.0,  -30.0, 800.0)));
    for (int i = 0; i < 2; ++i) mixed.push_back(eng.decide(makeSweep(-14.0, -30.0, 800.0)));
    CHECK(wide.combine(mixed, placeholder).nValid == 5, "all five readings are valid");
    CHECK(wide.combine(mixed, placeholder).tier == Tier::High, "3 of 5 High -> High");
    mixed.clear();
    for (int i = 0; i < 2; ++i) mixed.push_back(eng.decide(makeSweep(-5.0,  -30.0, 800.0)));
    for (int i = 0; i < 3; ++i) mixed.push_back(eng.decide(makeSweep(-14.0, -30.0, 800.0)));
    CHECK(wide.combine(mixed, placeholder).tier == Tier::Low, "2 of 5 High -> not High");

    // H7-1.5.0: with the rules, an even split is High (the upper middle), never
    // the "Suspicious" a mean of High and Low would make; and readings in
    // different boxes do not turn the lesion Invalid, however far apart.
    {
        Aggregator strict;     // the default spread tolerance
        std::vector<Verdict> two;
        two.push_back(eng.decide(makeSweep(-5.0,  -30.0, 800.0)));
        two.push_back(eng.decide(makeSweep(-14.0, -30.0, 800.0)));
        CaseResult c2 = strict.combine(two, placeholder);
        CHECK(c2.tier == Tier::High && c2.label == "High Risk", "1 High + 1 Low -> High, not Suspicious");
        two.push_back(eng.decide(makeSweep(-14.0, -30.0, 800.0)));
        two.push_back(eng.decide(makeSweep(-5.0,  -30.0, 800.0)));
        two.push_back(eng.decide(makeSweep(-14.0, -30.0, 800.0)));
        c2 = strict.combine(two, placeholder);
        CHECK(c2.tier == Tier::Low && c2.label == "Low Risk", "2 High + 3 Low, far apart in IPS -> Low, not Invalid");
        CHECK(c2.complete, "and the lesion completes");
    }
    // H7-1.8.0: no counted reading -> the lesion is called by its last reading's word, still not an answer
    {
        Verdict nz; nz.tier = Tier::Invalid; nz.label = "Noisy"; nz.color = "#546E7A";
        std::vector<Verdict> only{ nz };
        CaseResult c3 = Aggregator().combine(only, placeholder);
        CHECK(c3.label == "Noisy" && c3.tier == Tier::Invalid && c3.nValid == 0, "only a Noisy reading: the lesion says Noisy, not Invalid");
    }

    // Turning the table off gives the model back.
    t.active = false;
    eng.setRuleTable(t);
    CHECK(!eng.rulesDeciding(), "the table can be turned off");
    CHECK(!eng.scoringModel().isRules, "and the model comes back");
}

static void testDataset() {
    std::cout << "dataset and outcomes\n";
    const std::string ds = "/tmp/birx_ds_test.csv", oc = "/tmp/birx_oc_test.csv";
    std::remove(ds.c_str()); std::remove(oc.c_str());

    DecisionEngine eng;
    RiskModel m; m.version = "t"; m.needleRev = "NDL-C"; m.endpoint = "t";
    m.terms.push_back({ "ipsFit", 0.0, 1.0, -1.0 });
    m.thresholdLow = 0.3; m.thresholdHigh = 0.7;
    eng.setModel(m);

    DatasetWriter w(ds);
    for (int i = 0; i < 3; ++i) {
        Spectrum s = makeSweep(-6.0 + i, -30.0, 800.0);
        s.caseId = "C1"; s.siteIndex = i + 1;
        CHECK(w.append(s, eng.decide(s)), "a reading is recorded");
    }

    OutcomeStore os(oc);
    Outcome o; o.caseId = "C1"; o.pathology = Pathology::Malignant;
    o.reportDate = "2026-09-20"; o.source = "core biopsy";
    CHECK(os.record(o), "an outcome is recorded");

    // A corrected outcome supersedes the first without touching the readings.
    Outcome o2 = o; o2.pathology = Pathology::Borderline; o2.note = "revised";
    CHECK(os.record(o2), "a corrected outcome is appended");

    OutcomeStore reread(oc);
    CHECK(reread.load(), "outcomes reload");
    CHECK(reread.lookup("C1") == Pathology::Borderline, "the later outcome wins");
    CHECK(reread.lookup("nope") == Pathology::Unknown, "an unknown case is Unknown");

    std::string err;
    std::vector<TrainingRow> rows = buildTrainingTable(ds, reread, &err);
    CHECK(err.empty(), "the training table builds: " + err);
    CHECK(rows.size() == 3, "three usable readings joined to their outcome");
    if (!rows.empty()) {
        CHECK(rows[0].caseId == "C1", "case id survives the round trip");
        CHECK(std::isfinite(rows[0].features.ipsFit), "features survive the round trip");
    }

    // Pending list: a case with readings and no outcome must show up.
    Spectrum s2 = makeSweep(-6.0, -30.0, 800.0);
    s2.caseId = "C2"; s2.siteIndex = 1;
    w.append(s2, eng.decide(s2));
    std::vector<std::string> pend = OutcomeStore::pending(ds, reread);
    CHECK(pend.size() == 1 && pend[0] == "C2", "a case awaiting pathology is listed");

    std::remove(ds.c_str()); std::remove(oc.c_str());
}


// H7-2.0.0: the lot transfer and the lot file
static void testLotFit() {
    std::cout << "needle lots\n";
    // a sweep, transferred with identical needles and kZ 1, is unchanged
    Spectrum s;
    for (double f : { 500.0, 1000.0, 10000.0, 100000.0, 500000.0 }) {
        Point p; p.freqHz = f; p.magOhm = 1200; p.phaseDeg = -6; p.range = 3; s.points.push_back(p);
    }
    Spectrum t = s;
    LotTransfer tr; tr.cpNeedleF = 60e-12; tr.cpRefF = 60e-12; tr.kZ = 1.0; tr.trialCalibrated = false;
    lotTransfer(&t, tr);
    double worst = 0;
    for (size_t i = 0; i < s.points.size(); ++i)
        worst = std::max(worst, std::fabs(t.points[i].phaseDeg - s.points[i].phaseDeg) + std::fabs(t.points[i].magOhm / s.points[i].magOhm - 1));
    CHECK(worst < 1e-9, "the same needle, transferred, is unchanged");
    // kZ scales the impedance
    t = s; tr.cpNeedleF = kNaN; tr.cpRefF = kNaN; tr.kZ = 0.8; lotTransfer(&t, tr);
    CHECK(std::fabs(t.points[0].magOhm - 960) < 1e-6 && std::fabs(t.points[0].phaseDeg + 6) < 1e-9, "the cell-constant ratio scales Z only");
    // the file reads back, and refuses a changed one
    LotFile f; f.rev = 4; f.refLot = "R"; f.refCpF = 61e-12;
    LotCorrection c; c.lotId = "A1"; c.kZ = 0.9; c.cpF = 63e-12; c.verdict = "Usable with calibration";
    f.put(c);
    LotFile g; std::string why;
    CHECK(g.load(f.save(), &why) && g.rev == 4 && g.find("A1") && std::fabs(g.find("A1")->kZ - 0.9) < 1e-12, "the lot file reads back");
    std::string bad = f.save(); bad[bad.find("0.9")] = '1';
    CHECK(!g.load(bad, &why), "a changed lot file is refused");
    CHECK(std::fabs(lotNormaliseZ(1000, 1, 20, kNaN) - 900) < 1e-9, "Z is brought to 25 degrees (2 % per degree)");
}

int main() {
    std::cout << "birx self-test\n\n";
    testParser();
    testSlope();
    testFatGate();
    testAggregation();
    testRangeCalibration();
    testCole();
    testOpenCal();
    testCaseId();
    testAudioCues();
    testLotCalibration();
    testReadiness();
    testBandModel();
    testNeedleCode();
    testIntake();
    testModelFile();
    testLegacyEquivalence();
    testRuleTable();
    testLotFit();
    testDataset();
    std::cout << "\n" << (checks - failures) << " of " << checks << " checks passed\n";
    if (failures) std::cout << failures << " FAILED\n";
    return failures ? 1 : 0;
}
