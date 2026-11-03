#include <chrono>
// tests/app_test.cpp — the instrument's workflow on a PC, against the
// simulated measurement unit. Covers the acceptance items of spec §19 that
// do not need a screen: 4, 5, 6, 7 (Stop), 8 (log), 9 (passcode), 10, 12.
#include "../app/app.h"
#include "../sim/sim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

static std::string slurp(const std::string& p)
{
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int g_pass = 0, g_fail = 0;
#define CHECK(c, what) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); } } while (0)

static void run(app::App& a, uint32_t ms) { for (uint32_t i = 0; i < ms; i += 5) { sim::advance(5); a.poll(); } }

static void reading(app::App& a, double z, double ips)
{
    sim::Load l; l.kind = "lesion"; l.z1k = z; l.ips = ips;
    sim::device_setLoad(l);
    a.startRun();
    run(a, 3000);
}

// H7-2.10.0: every needle is checked in the saline too before it measures (QC); a tip in 0.9 % saline
static bool salineOk(app::App& a, double z = 140, double cp = 60, double ze = 150, double alpha = 0.6, double t = 25)
{
    sim::Load l; l.kind = "saline"; l.z1k = z; l.cpPf = cp; l.ze1k = ze; l.zeAlpha = alpha;
    sim::device_setLoad(l);
    a.dipOpen(); a.dipRun(t);
    for (int k = 0; k < 80 && (a.busy() || a.dipStage() == app::DipStage::Waiting || a.dipStage() == app::DipStage::Sweeping ||
                               a.dipStage() == app::DipStage::Settling || a.dipStage() == app::DipStage::Between); ++k) run(a, 500);
    if (!a.dipError().empty()) std::printf("saline check: %s\n", a.dipError().c_str());
    return a.dipDone();
}

// APPTEST_ROOT: on the PC a folder; on the emulated Cortex-M7 (tests/qemu) the
// littlefs volume "2:", so that the same checks run through newlib,
// board/syscalls.c and board/intmem.c with the instrument's own compiler.
#ifndef APPTEST_ROOT
#define APPTEST_ROOT "simroot"
#define APPTEST_HOST 1
#else
#include <sys/stat.h>
extern "C" int mkdir(const char*, mode_t);
#endif

int main()
{
#ifdef APPTEST_HOST
    if (system("rm -rf simroot && mkdir -p simroot/usb") != 0) return 2;
#else
    mkdir(APPTEST_ROOT "/usb", 0777);
#endif
    sim::g_root = APPTEST_ROOT;
    sim::g_clockBase = (int64_t(20720) * 86400) + 10 * 3600;   // 2026-09-24 10:00 local
    app::App a;
    a.begin();
    run(a, 1500);

    // --- §19.4 device id
    CHECK(!a.canStartCase(), "no device id -> Start case disabled");
    CHECK(a.caseBlocker().find("no device ID") != std::string::npos, "the reason is on screen");
    std::string m = a.setDeviceId("VIRA");
    CHECK(m.find("cannot contain I") != std::string::npos, "VIRA refused naming I");
    CHECK(m.find("Try V1RA") != std::string::npos, "suggests V1RA");
    CHECK(a.setDeviceId("A3K").find("exactly four") != std::string::npos, "length named");
    CHECK(a.setDeviceId("a3k7").empty(), "A3K7 accepted");
    {
        app::App b; b.begin();
        CHECK(b.settings().deviceId == "A3K7", "device id survives a restart");
    }

    // H7-2.1.0: a new board's rules are marked as found without air calibration
    CHECK(!a.airCalUsed() && a.measureBlocker().find("not been calibrated") == std::string::npos,
          "default: rules found without air calibration, which is then not asked for");
    // the checks below are of the air-calibrated workflow
    CHECK(a.setRulesAirCal(true).empty(), "marked as found with air-calibrated needles");

    // --- a reading with no case -> TEST- id, uncalibrated needle still recorded
    CHECK(a.connected(), "heartbeat -> link up");
    CHECK(!a.canMeasure(), "needle not calibrated blocks");
    // H7-2.10.0: a needle that has not passed its QC gives no output, even from the pedal
    sim::device_pressPedal(); run(a, 3000);
    CHECK(!a.haveLast() && a.actionReason().find("not a reading") != std::string::npos,
          "a pedal sweep of an unchecked needle is no reading: nothing decided or shown");
    std::printf("pedal, unchecked needle: %s\n", a.actionReason().c_str());
    {   // with the needle checks off (Settings -> Needle, bench use) it is read as before
        app::Settings st = a.settings(); st.salineCheck = 0; std::string w; a.saveSettings(st, &w);
    }
    reading(a, 850, -5);
    {
        app::Settings st = a.settings(); st.salineCheck = 2; std::string w; a.saveSettings(st, &w);
    }
    CHECK(a.measureCaseId().compare(0, 5, "TEST-") == 0, "no-case reading under TEST-");
    CHECK(a.haveLast() && a.last().tier == birx::Tier::High, "z850 ips-5 -> High");
    CHECK(a.ipsText() == "-5.00" || std::fabs(std::atof(a.ipsText().c_str()) + 5) < 0.2, "ips tile");
    std::printf("TEST reading: ips %s  z %s  case %s  reason: %s\n", a.ipsText().c_str(),
                a.z1kText().c_str(), a.caseLabel().c_str(), a.caseResult().reason.c_str());

    // --- start a case, intake
    std::string why;
    const std::string id = a.beginPatient(&why);
    CHECK(id.size() == 12 && id.compare(0, 5, "A3K7-") == 0, "case id minted");
    std::printf("case id %s\n", id.c_str());
    a.setIntakeValue("age", "54"); a.setIntakeValue("birads", "4A");
    a.setIntakeValue("lesion_size", "14"); a.setIntakeValue("side", "Left");
    a.setIntakeValue("clock", "10"); a.setIntakeValue("needle_lot", "L7");
    CHECK(!a.intakeComplete(), "serial missing");
    a.setIntakeValue("needle_serial", "N-123");
    CHECK(a.intakeComplete(), "intake complete");
    CHECK(a.commitPatient(&why), "commit");

    // --- §19.5 calibration: wet tip refused with numbers, air accepted
    sim::Load wet; wet.kind = "wet"; sim::device_setLoad(wet);
    a.calOpen(); a.calRun(); run(a, 4000);
    CHECK(!a.needleReady(), "wet tip refused");
    std::printf("wet: %s\n", a.calError().c_str());
    CHECK(a.calError().find("ohm") != std::string::npos, "refusal carries the numbers");
    sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air);
    a.calRun(); run(a, 9000);
    CHECK(a.needleReady(), "air accepted");
    std::printf("air: %s\n", a.needleCalibration().c_str());
    CHECK(a.caseResult().nReadings == 0, "calibration sweeps are not case readings (§20.6)");
    CHECK(!a.canMeasure() && a.measureBlocker().find("Saline check") != std::string::npos,
          "H7-2.10.0: every needle is checked in the saline too before it measures");
    CHECK(salineOk(a), "its saline check (0.9 %, generic limits) is accepted");
    std::printf("saline: %s\n", a.dipText().c_str());
    CHECK(a.caseResult().nReadings == 0, "nor are the saline check's sweeps");
    CHECK(a.canMeasure(), "can measure now");

    // --- §19.6 five readings -> complete; High early stop
    reading(a, 800, 2); reading(a, 820, 1); reading(a, 810, 3);
    CHECK(a.caseResult().complete && a.caseResult().stoppedEarly, "three High -> stopped early");
    std::printf("after 3: %s | %s\n", a.caseLabel().c_str(), a.caseResult().reason.c_str());
    // H7-2.14.0: final, so closed (Settings -> Rules -> Close the case once final, on by default): no more readings
    CHECK(a.caseResult().closed && !a.canMeasure() && a.measureBlocker().find("The case is closed") != std::string::npos,
          "a final case is closed: no more readings");
    sim::device_pressPedal(); run(a, 3000);
    CHECK(a.caseResult().nReadings == 3 && a.actionReason().find("not a reading") != std::string::npos,
          "and a sweep from the pedal is no reading either");
    a.endCase();
    {   // the checks below take readings on past a final case, as before H7-2.14.0
        app::Settings st = a.settings(); st.rules.caseClose = false; std::string w;
        CHECK(a.saveSettings(st, &w), "Close the case once final: off for the rest of these checks");
    }
    CHECK(!a.needleReady(), "End case retires the needle");
    CHECK(!a.caseOpen(), "case closed");

    // low risk case of five
    a.beginPatient(&why);
    for (auto kv : { std::make_pair("age","61"), std::make_pair("birads","3"), std::make_pair("lesion_size","9"),
                     std::make_pair("side","Right"), std::make_pair("clock","2"), std::make_pair("needle_lot","L7"),
                     std::make_pair("needle_serial","N-124") }) a.setIntakeValue(kv.first, kv.second);
    CHECK(a.commitPatient(&why), "commit 2");
    sim::device_setLoad(air); a.calRun(); run(a, 9000);
    salineOk(a);
    for (int i = 0; i < 4; ++i) { reading(a, 900, 10 + i * 0.3); }
    CHECK(!a.caseResult().complete, "4 of 5 not complete");
    std::printf("provisional: %s\n", a.caseResult().reason.c_str());
    reading(a, 900, 10.5);
    CHECK(a.caseResult().complete && a.caseResult().tier == birx::Tier::Low, "five Low -> complete Low");
    const std::string lowCase = a.caseId();
    a.endCase();

    // --- H7-2.14.0: the case result by count, set in Settings -> Rules, end to end
    {
        if (a.caseOpen()) a.endCase();
        app::Settings st = a.settings();
        st.rules.caseHigh = 1; st.rules.caseLow = 3; st.rules.caseClose = true;
        std::string w;
        CHECK(a.saveSettings(st, &w), "High Risk from 1, Low Risk from 3, closed once final");
        std::string why2;
        a.beginPatient(&why2);
        for (auto kv : { std::make_pair("age","50"), std::make_pair("birads","4A"), std::make_pair("lesion_size","12"),
                         std::make_pair("side","Left"), std::make_pair("clock","3"), std::make_pair("needle_lot","L7"),
                         std::make_pair("needle_serial","N-131") }) a.setIntakeValue(kv.first, kv.second);
        CHECK(a.commitPatient(&why2), "a case");
        sim::device_setLoad(air); a.calRun(); run(a, 9000);
        salineOk(a);
        reading(a, 900, 10.2);
        CHECK(!a.caseResult().complete && a.caseResult().nLow == 1, "one low reading: provisional");
        reading(a, 800, 2);
        CHECK(a.caseResult().complete && a.caseResult().closed && a.caseResult().tier == birx::Tier::High && a.caseResult().byCount,
              "then one high reading: High Risk, final, the case closed");
        std::printf("count: %s | %s\n", a.caseLabel().c_str(), a.caseResult().reason.c_str());
        CHECK(!a.canMeasure(), "no more readings");
        a.endCase();
        st = a.settings(); st.rules.caseHigh = 0; st.rules.caseLow = 0; st.rules.caseClose = false;
        CHECK(a.saveSettings(st, &w), "counts off again (and closing too, for the checks below)");
    }


    // spread -> Invalid
    a.beginPatient(&why);
    for (auto kv : { std::make_pair("age","48"), std::make_pair("birads","4B"), std::make_pair("lesion_size","20"),
                     std::make_pair("side","Left"), std::make_pair("clock","Central"), std::make_pair("needle_lot","L7"),
                     std::make_pair("needle_serial","N-125") }) a.setIntakeValue(kv.first, kv.second);
    a.commitPatient(&why);
    sim::device_setLoad(air); a.calRun(); run(a, 9000);
    salineOk(a);
    reading(a, 900, -5); reading(a, 900, 5);
    std::printf("two apart: %s\n", a.caseResult().reason.c_str());
    reading(a, 900, -9);
    std::printf("three: tier %s spread %.2f  %s\n", birx::tierName(a.caseResult().tier),
                a.caseResult().spreadIps, a.caseResult().reason.c_str());

    // 4.2 kohm, IPS -5: Z 4000-5000 is Low Risk at any IPS (H7-1.5.0 table)
    reading(a, 4200, -5);
    CHECK(a.last().tier == birx::Tier::Low, "4.2 k is Low Risk by the table");
    CHECK(a.last().label == "Low Risk" && a.last().reason.find("rule 7") != std::string::npos, "and it names the row");

    // §19.7 Stop within one frequency step
    { sim::Load l; l.kind = "lesion"; l.z1k = 850; l.ips = -5; sim::device_setLoad(l); }
    const int before = a.caseResult().nReadings;
    a.startRun(); run(a, 600);
    std::printf("before stop: running %d sweeping %d points %d\n", a.running(), a.sweeping(), int(a.current().points.size()));
    a.stopRun();
    CHECK(!a.running(), "Stop -> idle at once");
    CHECK(a.busy() && !a.canMeasure(), "no new [R] while the unit finishes the stopped sweep");
    const size_t txBefore = sim::g_tx.size();
    a.startRun();
    CHECK(sim::g_tx.size() == txBefore, "Sweep refused while the stopped sweep finishes");
    run(a, 3000);
    std::printf("after stop: readings %d (before %d)\n", a.caseResult().nReadings, before);
    CHECK(a.caseResult().nReadings == before, "the stopped sweep was not counted");
    CHECK(!a.busy(), "ready again after the stopped sweep's [E]");

    // 2.19.0: a Stop pressed before the answer to [R] began discards the sweep
    // that answers it, but a pedal sweep after the 3 s window is a reading.
    {
        const int n0 = a.caseResult().nReadings;
        a.startRun(); run(a, 50);
        a.stopRun();                                     // [R] sent, no [B] yet
        run(a, 3000);
        CHECK(a.caseResult().nReadings == n0, "the sweep answering a stopped [R] is discarded");
        CHECK(!a.busy(), "idle after the discarded answer");
        run(a, 3000);
        sim::device_pressPedal(); run(a, 3000);
        CHECK(a.caseResult().nReadings == n0 + 1, "a pedal sweep after the window is a reading");
        // the [T] reply is not an unparsed line
        const int u0 = a.unparsedCount();
        a.startProbe(); run(a, 2000);
        CHECK(a.probeResult().find("ANSWERED") == 0, "probe answered");
        CHECK(a.unparsedCount() == u0, "\"TDP is connected\" is not counted as unparsed");
    }

    // H7-1.2.3: a Sweep is ONE "[R]" and a long, patient wait. Nothing else is
    // sent, and a sweep that begins late is still taken.
    {
        int n0 = a.caseResult().nReadings;
        size_t tx0 = sim::g_tx.size();
        a.startRun(); run(a, 4000);
        CHECK(a.caseResult().nReadings == n0 + 1 && sim::g_tx.size() - tx0 == 3,
              "one [R], three bytes, a reading");
        n0 = a.caseResult().nReadings;
        sim::device_setAlive(true, false);
        tx0 = sim::g_tx.size();
        a.startRun(); run(a, 12000);
        CHECK(a.busy() && a.actionReason().find("12 s ago") != std::string::npos, "still waiting after 12 s, and counting");
        sim::device_pressPedal(); run(a, 4000);          // the sweep begins late
        CHECK(a.caseResult().nReadings == n0 + 1, "a sweep that begins late is taken");
        CHECK(sim::g_tx.size() - tx0 == 3, "and only the one [R] was ever sent");
        n0 = a.caseResult().nReadings;
        tx0 = sim::g_tx.size();
        const int r0 = sim::g_unitRestarts;
        a.startRun(); run(a, 30500);
        CHECK(sim::g_tx.size() - tx0 == 3, "unanswered for 30 s: still exactly one [R]");
        // H7-1.6.0: then the measurement unit is restarted (its power off and on) and asked again
        CHECK(sim::g_unitRestarts == r0 + 1 && a.actionReason().find("being restarted") != std::string::npos,
              "then the screen says so, and the measurement unit is restarted");
        CHECK(a.busy(), "nothing else is sent meanwhile");
        run(a, 12000);
        CHECK(a.caseResult().nReadings == n0 + 1, "once it is back the Sweep is asked again by itself, and answered");
        CHECK(sim::g_tx.size() - tx0 == 6, "with one more [R], nothing else");
        CHECK(a.actionReason().find("restarted") == std::string::npos, "and the message has gone");
        // nothing arriving at all and Sweep pressed: restarted at once
        n0 = a.caseResult().nReadings;
        sim::device_setAlive(false, false); run(a, 6000);
        a.startRun(); run(a, 200);
        CHECK(sim::g_unitRestarts == r0 + 2, "Sweep with nothing arriving: restarted at once");
        run(a, 12000);
        CHECK(a.caseResult().nReadings == n0 + 1, "and the Sweep is taken once it is back");
        // the third time in a session it is not restarted again
        sim::device_setAlive(false, false); run(a, 6000);
        a.startRun(); run(a, 200);
        CHECK(sim::g_unitRestarts == r0 + 2 && !plat::unitRestarting(), "no more than two restarts in a session");
        sim::device_setAlive(true, true);
        tx0 = sim::g_tx.size();
        run(a, 5000);
        CHECK(sim::g_tx.size() == tx0, "nothing is ever sent unasked");
        // the link monitor's raw send
        n0 = a.caseResult().nReadings;
        a.sendRaw("[R]"); run(a, 4000);
        CHECK(a.caseResult().nReadings == n0 + 1, "a raw [R] from the monitor produces a reading");
        // Test still re-aligns on a unit that answers [T]
        sim::device_strayByte();
        a.startProbe(); run(a, 4000);
        CHECK(a.probeResult().find("ANSWERED after re-aligning") == 0, "Test re-aligns the framing");
        // after a sweep, no command until the next heartbeat
        n0 = a.caseResult().nReadings;
        a.startRun();
        for (int i = 0; i < 800 && !(a.caseResult().nReadings == n0 + 1); ++i) run(a, 5);
        tx0 = sim::g_tx.size();
        a.startRun();
        CHECK(sim::g_tx.size() == tx0, "no [R] between a sweep's end and the next heartbeat");
        run(a, 3200);
        CHECK(!a.busy(), "free again after the heartbeat");
    }

    // H7-1.2.4: the tick moves WHILE the loop runs, as on the board. A line is
    // then stamped later than the loop's own reading of the time, and the
    // sweep watchdog must not take that for twelve seconds of silence.
    {
        auto closed = [&a] { int n = 0; for (const auto& t : a.traffic()) if (t.find("closing the run") != std::string::npos) ++n; return n; };
        for (int every : {1, 2, 3, 7}) {
            sim::g_tickEvery = every;
            run(a, 3200);
            const int c0 = closed();
            const int n0 = a.caseResult().nReadings;
            a.startRun();
            for (int i = 0; i < 2000 && a.caseResult().nReadings == n0; ++i) run(a, 5);
            CHECK(closed() == c0, "no run is closed while its points are arriving");
            CHECK(a.caseResult().nReadings == n0 + 1, "the sweep is one whole reading with the tick moving under it");
            CHECK(a.linkMessage().find("stopped mid-sweep") == std::string::npos, "and no mid-sweep message");
            CHECK(a.haveLast() && a.last().tier != birx::Tier::Invalid, "and it is judged, all 15 points");
        }
        // Stop while one is in flight, with the tick moving: discarded, not "never ended"
        run(a, 3200);
        const int n1 = a.caseResult().nReadings;
        a.startRun();
        for (int i = 0; i < 400 && !a.running(); ++i) run(a, 5);
        run(a, 300);
        a.stopRun();
        run(a, 6000);
        CHECK(a.caseResult().nReadings == n1 && !a.busy(), "a stopped sweep is still discarded cleanly");
        bool never = false;
        for (const auto& e : a.events()) if (e.find("never ended; no longer waiting") != std::string::npos) never = true;
        CHECK(!never, "its discard is not abandoned by a false timeout");
        sim::g_tickEvery = 0;
        // and the real watchdog still works: a sweep that truly stops is closed after 12 s
        run(a, 3200);
        const int c1 = closed();
        for (char ch : std::string("[B]\r\n[imp05000000,00030134,-005.12,5]\r\n")) sim::g_rx.push_back(ch);
        run(a, 11000);
        CHECK(closed() == c1 && a.running(), "11 s of silence mid-sweep: still waiting");
        run(a, 2000);
        CHECK(closed() == c1 + 1 && !a.running(), "after 12 s it is closed, once — the heartbeat does not keep it open");
        run(a, 3200);
    }

    // The sweep the real measurement unit sent on the bench (link monitor,
    // H7-1.2.3): its own line format, 0.2 s a point, with the tick moving.
    {
        const double fr[15] = { 500000, 300000, 250000, 225000, 199000, 175000, 125000, 100000,
                                75000, 50000, 25000, 10000, 5000, 1000, 500 };
        auto feed = [](const std::string& l) { for (char ch : l) sim::g_rx.push_back(ch); };
        sim::g_tickEvery = 2;
        run(a, 3200);
        const int n0 = a.caseResult().nReadings, u0 = a.unparsedCount();
        feed("[B]\r\n");
        run(a, 600);
        for (int i = 0; i < 15; ++i) {
            char b[64];
            // H7-2.15.0: as the unit sent it but for a tip surface below 5 kHz (the bench's was a resistor, now refused)
            std::snprintf(b, sizeof b, "[imp%08d,%08d,%+07.2f,5]\r\n", int(fr[i] * 10), 30134 + i * 27,
                          -5.12 * (14 - i) / 14.0 - 0.02 - (i >= 12 ? 10.0 * (i - 11) : 0.0));
            feed(b);
            run(a, 200);
        }
        feed("[IPS-0006.16,Z03048.39,Negative]\r\n[E]\r\n");
        run(a, 300);
        CHECK(a.unparsedCount() == u0, "every line of the real unit's sweep is understood");
        CHECK(a.current().points.size() == 15, "all fifteen points are kept");
        CHECK(a.caseResult().nReadings == n0 + 1, "and it is one reading");
        CHECK(std::fabs(a.current().fwZ1k - 3048.39) < 0.01 && a.current().fwText == "Negative", "with the unit's own IPS line read");
        CHECK(a.linkMessage().empty(), "and nothing to complain about");
        std::printf("real-format sweep judged: tier %d  %s\n", int(a.last().tier), a.last().reason.c_str());
        sim::g_tickEvery = 0;
        run(a, 3200);
    }

    // H7-1.2.7: one recording repeats while a sweep runs, as the previous firmware did
    {
        run(a, 3200);
        CHECK(a.settings().sweepTrack == 7, "the sweeping sound is track 7 unless changed");
        const int loops0 = sim::g_voiceLoops;
        const int n0 = a.caseResult().nReadings;
        a.startRun();
        for (int i = 0; i < 400 && !a.running(); ++i) run(a, 5);
        run(a, 300);
        CHECK(sim::g_voiceLoop == 7 && sim::g_voiceLoops == loops0 + 1, "it starts with the sweep, once");
        for (int i = 0; i < 2000 && a.caseResult().nReadings == n0; ++i) run(a, 5);
        CHECK(sim::g_voiceLoop == 0, "and stops when the sweep ends");
        run(a, 3200);
        // the pedal too
        sim::device_pressPedal(); run(a, 600);
        CHECK(sim::g_voiceLoop == 7, "a pedal sweep sounds the same");
        a.stopRun();
        CHECK(sim::g_voiceLoop == 0, "Stop silences it at once");
        run(a, 6000);
        // muted, and set to silent
        a.setMuted(true);
        sim::device_pressPedal(); run(a, 600);
        CHECK(sim::g_voiceLoop == 0, "muted: nothing while sweeping");
        run(a, 6000);
        a.setMuted(false);
        a.setSweepTrack(0);
        sim::device_pressPedal(); run(a, 600);
        CHECK(sim::g_voiceLoop == 0, "set to silent: nothing while sweeping");
        run(a, 6000);
        a.setSweepTrack(3);
        {
            app::App b; b.begin();
            CHECK(b.settings().sweepTrack == 3, "the choice survives a restart");
        }
        sim::device_pressPedal(); run(a, 600);
        CHECK(sim::g_voiceLoop == 3, "another track can be chosen");
        run(a, 6000);
        CHECK(sim::g_voiceLoop == 0, "and it too stops at the end");
        a.setSweepTrack(7);
        CHECK(app::App::trackOf("Track 3 (warning)") == 3 && app::App::trackOf("Track 3 (strongly positive)") == 3,
              "a rule's sound saved under the old names still plays");
    }

    // §19.10 rule edit re-judges without sound
    const size_t voices = sim::g_voice.size();
    app::RulesConfig r = a.settings().rules;
    r.rows[0].label = "Needs core biopsy";
    CHECK(a.saveRules(r).empty(), "rules saved");
    CHECK(sim::g_voice.size() == voices, "no sound on re-judge");
    a.endCase();

    // §19.9 passcode
    CHECK(a.checkPassword("7324"), "default 7324");
    CHECK(!a.changePassword("7324", "1234", "1243").empty(), "confirmation must match");
    CHECK(a.changePassword("7324", "1234", "1234").empty(), "changed");
    { app::App b; b.begin(); CHECK(b.checkPassword("1234"), "passcode survives a restart"); }

    // §19.8 log follows the records, export
    a.refreshLog();
    int hidden = 0;
    auto rows = a.caseLog("all", "all", &hidden);
    std::printf("log: %d cases\n", int(rows.size()));
    for (auto& c : rows) std::printf("  %-20s %-14s %-18s %s | %s | %s%s\n", c.caseId.c_str(), c.whenText.c_str(),
                                     c.label.c_str(), c.summary.c_str(), c.detail.c_str(), c.test ? "NO CASE " : "",
                                     c.edited ? "EDITED" : "");
    CHECK(rows.size() == 5, "5 cases in the log (H7-2.14.0: with the case closed by count)");
    CHECK(a.recordOutcome(lowCase, "benign", "2026-09-30", "core biopsy", "").empty(), "pathology recorded");
    CHECK(a.updateCaseDetails(lowCase, {{"age", "62"}}).find("Saved") == 0, "details corrected");
    rows = a.caseLog("all", "benign");
    for (auto& c : rows) std::printf("  benign: %s edited %d detail %s path %s\n", c.caseId.c_str(), c.edited, c.detail.c_str(), c.pathology.c_str());
    CHECK(rows.size() == 1 && rows[0].edited && rows[0].detail.find("62 y") == 0, "edited + pathology in log");
    std::printf("%s\n", a.exportLogs("all", "all").c_str());
    std::printf("%s\n", a.exportAll().c_str());

    // 2.19.0 pathology Save says why when it cannot save
    CHECK(a.recordOutcome("", "benign", "2026-09-30", "core biopsy", "") == "choose a case first", "no case -> reason");
    CHECK(a.recordOutcome(lowCase, "maybe", "2026-09-30", "core biopsy", "").find("not a pathology result") != std::string::npos,
          "bad result -> reason");
    CHECK(a.outcomeOf(lowCase) == "benign", "outcomeOf");
    {
        auto p = a.casesAwaitingPathology();
        bool hasLow = false; for (auto& x : p) if (x == lowCase) hasLow = true;
        CHECK(!hasLow && !p.empty(), "awaiting list excludes the recorded case");
        // H7-1.2.10: readings taken with no case open can be given an outcome too (as on the Jetson)
        std::string testId;
        for (auto& x : p) if (x.compare(0, 5, "TEST-") == 0) testId = x;
        CHECK(!testId.empty(), "a no-case reading is in the awaiting list");
        const int pend0 = a.pendingOutcomes();
        CHECK(pend0 == int(p.size()), "and is counted as awaiting");
        CHECK(a.recordOutcome(testId, "malignant", "2026-09-24", "core biopsy", "").empty(), "its pathology is saved");
        CHECK(a.outcomeOf(testId) == "malignant", "and read back");
        CHECK(a.pendingOutcomes() == pend0 - 1, "one fewer is awaiting");
        bool still = false; for (auto& x : a.casesAwaitingPathology()) if (x == testId) still = true;
        CHECK(!still, "and it has left the list");
        CHECK(a.updateCaseDetails(testId, {{"age", "47"}}).find("Saved") == 0, "its details can be entered afterwards");
        CHECK(a.caseDetails(testId)["age"] == "47", "and are kept");
    }

    // 2.20.0 health log
    const std::string rec = std::string(APPTEST_ROOT) + "/sd/records";
    {
        const std::string h = slurp(rec + "/health.csv");
        int lines = 0; for (char ch : h) if (ch == '\n') ++lines;
        std::printf("health.csv: %d lines, last: %s\n", lines, h.substr(h.rfind('\n', h.size() - 2) + 1).c_str());
        CHECK(h.compare(0, 13, "time,uptime_s") == 0 && lines > 3, "health.csv every 10 s");
        app::App b; b.begin();
        CHECK(b.lastSessionEnd().find("lost power") != std::string::npos, "an unclean end is reported");
        std::printf("previous session: %s\n", b.lastSessionEnd().c_str());
        b.shutdownClean();
        app::App c2; c2.begin();
        CHECK(c2.lastSessionEnd().empty(), "a power-key shutdown is a clean end");
    }

    // 2.19.0 diagnostic mode
    {
        const std::string on = a.startDiagnosticMode();
        std::printf("%s\n", on.c_str());
        CHECK(a.diagnosticMode() && on.find("ON") != std::string::npos, "diagnostic mode on");
        const std::string path = a.diagnosticFile();
        a.beginPatient(&why);
        a.setIntakeValue("national_id", "8811223344");
        a.setIntakeValue("age", "57");
        a.abandonPatient();
        a.changePassword("1234", "5678", "5678");
        a.uiAction("[measure] tap \"Sweep\"", "disabled \xE2\x80\x94 nothing happens");
        sim::device_pressPedal(); run(a, 4000);
        run(a, 12000);
        const std::string f = slurp(path);
        CHECK(f.find("DIAGNOSTIC MODE") == 0, "starts with the snapshot");
        CHECK(f.find("8811223344") == std::string::npos && f.find("national_id  ->  (value hidden)") != std::string::npos,
              "national id never written");
        CHECK(f.find("age  ->  57") != std::string::npos, "intake field written");
        CHECK(f.find("5678") == std::string::npos && f.find("pc=(hidden)") != std::string::npos, "passcode never written");
        CHECK(f.find("measure blocker") != std::string::npos || f.find("no blocker is set") != std::string::npos,
              "a grey control's reason is written");
        CHECK(f.find("RX  [B]") != std::string::npos, "link lines written");
        CHECK(f.find("[C] heartbeat x") != std::string::npos, "heartbeats counted, not listed");
        CHECK(f.find("[health]") != std::string::npos, "health lines in the mode");
        app::App b; b.begin();
        CHECK(b.diagnosticMode() && b.diagnosticFile() == path, "the mode survives a restart");
        const std::string off = b.stopDiagnosticMode();
        std::printf("%s\n", off.c_str());
        CHECK(!b.diagnosticMode() && off.find("USB stick") != std::string::npos, "stopped, copied to the stick");
        const std::string f2 = slurp(path);
        CHECK(f2.find("APPLICATION STARTED") != std::string::npos && f2.find("diagnostic mode ended") != std::string::npos,
              "restart and closing snapshot in the file");
        b.uiAction("after stop");
        run(b, 1000);
        CHECK(slurp(path).size() == f2.size(), "nothing written after Stop diagnostics");
        app::App c3; c3.begin();
        CHECK(!c3.diagnosticMode(), "off stays off after a restart");
        std::printf("diagnostic file: %d bytes\n", int(f2.size()));
#ifdef APPTEST_HOST
        if (system(("cp " + path + " simroot/diagmode_sample.txt").c_str()) != 0) std::printf("(sample not copied)\n");
#endif
    }

    // H7-1.2.0: no SD card. The internal memory holds the previous firmware's
    // data and is not touched until the operator says so; then it is the store.
    {
        sim::g_sd = false;
        sim::g_intmem = 1;
        app::App n; n.begin();
        CHECK(!n.storageProblem().empty() && n.storageProblem().find("Internal memory") != std::string::npos,
              "no card, memory not prepared: the banner says where to go");
        CHECK(plat::internalMemory() == plat::IntMem::Foreign, "the old data is left alone");
        const std::string r = n.prepareInternalMemory();
        std::printf("%s\n", r.c_str());
        CHECK(r.find("is ready") != std::string::npos && plat::recordsOnInternal(), "prepared on request");
        CHECK(n.storageProblem().empty(), "the banner is gone");
        CHECK(n.recordsDir().find("/int/records") != std::string::npos, "records are on the internal memory");
        run(n, 1500);
        { sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air); n.calOpen(); n.calRun(); run(n, 9000); salineOk(n); }
        sim::Load l; l.kind = "lesion"; l.z1k = 850; l.ips = -5; sim::device_setLoad(l);
        n.startRun(); run(n, 4000);
        CHECK(slurp(n.recordsDir() + "/dataset.csv").find("TEST-") != std::string::npos, "a reading is recorded there");
        app::App m2; m2.begin();
        CHECK(m2.storageProblem().empty() && m2.logCount() == 1, "and is there after a restart");
        sim::g_sd = true;
    }

    // H7-1.3.0: the healthy-tissue sample of the patient, before the lesion
    {
        run(a, 3200);
        if (a.caseOpen()) a.endCase();
        CHECK(!a.refBlocker().empty() && a.refBlocker().find("No case is open") == 0, "no case: the sample is refused, and says why");
        a.refRun();
        CHECK(a.refStage() == app::RefStage::Idle, "and nothing starts");
        a.beginPatient(&why);
        const std::string hc = a.caseId();
        sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air);
        a.calOpen(); a.calRun(); run(a, 9000);
        salineOk(a);
        CHECK(a.needleReady(), "needle calibrated for the sample");
        CHECK(a.refBlocker().empty(), "then nothing blocks it");
        const int n0 = a.caseResult().nReadings;
        const size_t voices0 = sim::g_voice.size();
        sim::Load normal; normal.kind = "lesion"; normal.z1k = 2800; normal.ips = 6; sim::device_setLoad(normal);
        a.refOpen(); a.refRun();
        CHECK(a.refStage() == app::RefStage::Waiting && a.busy(), "the sample is being taken");
        run(a, 5000);
        CHECK(a.refStage() == app::RefStage::Done && a.haveTissueRef(), "and is kept");
        CHECK(a.tissueRefCount() == 1 && std::fabs(a.tissueRefs()[0].features.z1k - 2800) < 30, "with its own Z at 1 kHz");
        CHECK(std::isfinite(a.tissueRefs()[0].features.ipsLegacy), "and its own IPS");
        CHECK(a.caseResult().nReadings == n0 && !a.haveLast(), "it is not a reading of the lesion");
        CHECK(sim::g_voice.size() == voices0, "and no answer is spoken for it");
        CHECK(a.chartShowsTissueRef(), "the chart is marked as showing it");
        const std::string tr = slurp(a.recordsDir() + "/tissue_reference.csv");
        CHECK(tr.find("case_id,epoch_ms,time_local,needle_lot,needle_serial,flags,z1k,") == 0, "the record has its header");
        CHECK(tr.find("\n" + hc + ",") != std::string::npos, "and a line for this case");
        CHECK(slurp(a.recordsDir() + "/dataset.csv").find(hc) == std::string::npos, "dataset.csv has nothing of it");
        CHECK(a.tissueSamplesOf(hc).size() == 1 && std::fabs(a.tissueSamplesOf(hc)[0].features.z1k - 2800) < 30,
              "the log finds it by case, with its quantities");
        // a reading after it is an ordinary reading, with the sample beside it
        run(a, 3200);
        reading(a, 850, -5);
        CHECK(a.caseResult().nReadings == n0 + 1 && a.haveLast() && a.haveTissueRef(), "the first reading follows; the sample stays");
        CHECK(!a.chartShowsTissueRef(), "the chart is the reading's again");
        // H7-1.3.1: several samples — each is kept, none replaces another
        run(a, 3200);
        normal.z1k = 3300; sim::device_setLoad(normal);
        a.refOpen(); a.refRun(); run(a, 5000);
        CHECK(a.tissueRefCount() == 2 && a.tissueRefs()[1].n == 2, "a second sample is H2, beside H1");
        CHECK(std::fabs(a.tissueRefs()[0].features.z1k - 2800) < 30 && std::fabs(a.tissueRefs()[1].features.z1k - 3300) < 40,
              "both keep their own values");
        run(a, 3200);
        normal.z1k = 4100; sim::device_setLoad(normal);
        a.refOpen(); a.refRun(); run(a, 5000);
        CHECK(a.tissueRefCount() == 3, "and a third");
        CHECK(std::fabs(app::App::medianOf(a.tissueRefs()).z1k - 3300) < 40, "the median of three is the middle one");
        CHECK(a.tissueRefText().find("3 samples") == 0, "the summary says how many");
        {
            const auto rec = a.tissueSamplesOf(hc);
            CHECK(rec.size() == 3 && rec[2].n == 3 && std::fabs(rec[2].features.z1k - 4100) < 50, "all three are in the record, in order");
            CHECK(std::isfinite(rec[0].features.ipsFit) && std::isfinite(rec[0].features.zRatio), "each with all its quantities");
        }
        // one that went wrong is taken out; the record keeps its line and says so
        a.refRemoveLast();
        CHECK(a.tissueRefCount() == 2 && std::fabs(app::App::medianOf(a.tissueRefs()).z1k - 3050) < 40, "the last is removed; the median is of the two left");
        CHECK(a.tissueSamplesOf(hc).size() == 2, "and the log no longer lists it");
        CHECK(slurp(a.recordsDir() + "/tissue_reference.csv").find(",removed,") != std::string::npos, "the record says it was removed");
        CHECK(a.caseResult().nReadings == n0 + 1, "still one reading");
        // a recorded reading's quantities are read back for the log
        {
            const auto rs = a.caseReadingsLog(hc);
            birx::Features rf; std::string rflags;
            CHECK(rs.size() == 1 && a.readingFeatures(hc, rs[0].epochMs, &rf, &rflags), "the reading's row is found by case and time");
            CHECK(std::fabs(rf.z1k - 850) < 15 && std::isfinite(rf.ipsFit) && std::isfinite(rf.phaseAtRefDeg) && rf.nFit > 0,
                  "with all its quantities");
            CHECK(!a.readingFeatures(hc, rs[0].epochMs + 1, &rf, &rflags), "a time that is not there finds nothing");
        }
        // cancelled: nothing kept, and the next sweep is a reading again
        run(a, 3200);
        a.endCase();
        CHECK(!a.haveTissueRef(), "End case: the sample goes with the case");
        a.beginPatient(&why);
        a.calOpen(); sim::device_setLoad(air); a.calRun(); run(a, 9000);
        salineOk(a);
        normal.z1k = 2800; sim::device_setLoad(normal);
        a.refOpen(); a.refRun(); run(a, 250); a.refStop();
        CHECK(a.refStage() == app::RefStage::Idle && !a.haveTissueRef(), "cancelled: nothing is kept");
        run(a, 8000);
        CHECK(!a.haveTissueRef() && a.caseResult().nReadings == 0, "and its sweep is neither a sample nor a reading");
        // required: no reading of the case until it is taken
        {
            app::Settings s2 = a.settings(); s2.reqTissueRef = true; a.saveSettings(s2, &why);
            CHECK(!a.canMeasure() && a.measureBlocker().find("healthy-tissue sample") != std::string::npos, "required and missing: readings are blocked");
            a.refOpen(); a.refRun(); run(a, 5000);
            CHECK(a.haveTissueRef() && a.canMeasure(), "taken: readings are allowed");
            app::App b; b.begin();
            CHECK(b.settings().reqTissueRef, "the requirement survives a restart");
            s2.reqTissueRef = false; a.saveSettings(s2, &why);
        }
        // no answer from the unit: the sheet gets out by itself
        a.endCase();
        a.beginPatient(&why);
        a.calOpen(); sim::device_setLoad(air); a.calRun(); run(a, 9000);
        salineOk(a);
        run(a, 3200);
        sim::device_setAlive(true, false);
        a.refOpen(); a.refRun(); run(a, 31000);
        CHECK(a.refStage() == app::RefStage::Idle && a.refError().find("No sweep started") == 0, "no sweep: it says so and stops waiting");
        sim::device_setAlive(true, true);
        a.endCase();
        run(a, 3200);
    }

    // H7-1.5.0: the shipped table says Noisy / High Risk / Low Risk and nothing else
    {
        run(a, 3200);
        if (a.caseOpen()) a.endCase();
        sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air);
        a.calOpen(); a.calRun(); run(a, 9000);
        salineOk(a);
        run(a, 3200);
        // the table itself: every point of the plane, and well off its ends, is one of the three
        {
            const birx::RuleTable t = app::RulesConfig::defaults().toRuleTable(true);
            CHECK(app::RulesConfig::defaults().problems().empty(), "the shipped table has no overlap and no problem");
            bool onlyThree = true, rightPlace = true;
            for (int zi = 0; zi <= 960; ++zi)
                for (int ii = -800; ii <= 800; ++ii) {
                    const double z = zi * 12.5, ips = ii / 20.0;
                    const birx::RuleHit h = t.lookup(z, ips);
                    if (h.label != "Noisy" && h.label != "High Risk" && h.label != "Low Risk") onlyThree = false;
                    const bool hi = (z <= 2000 && ips >= -13 && ips <= 6) || (z > 2000 && z <= 4000 && ips >= -5 && ips <= 0);
                    const char* want = z > 5000 ? "Noisy" : hi ? "High Risk" : "Low Risk";
                    // the seams are a ten-thousandth wide; points on a grid of 0.05 never land in one
                    if (h.label != want) rightPlace = false;
                }
            for (double z : { -5.0, 0.0, 2000.00005, 4000.00005, 5000.00005, 2e6, 1e9 })
                for (double ips : { -1e4, -300.5, -13.00005, -5.00005, 0.00005, 6.00005, 300.5, 1e4 }) {
                    const birx::RuleHit h = t.lookup(z, ips);
                    if (h.label != "Noisy" && h.label != "High Risk" && h.label != "Low Risk") onlyThree = false;
                }
            CHECK(onlyThree, "no reading anywhere is called anything but Noisy, High Risk or Low Risk");
            CHECK(rightPlace, "and each is where the bench asked for it");
            CHECK(t.lookup(5000, 3).label == "Low Risk" && t.lookup(5000.5, 3).label == "Noisy", "Noisy is ABOVE 5000");
            CHECK(t.lookup(2000.5, -2).label == "High Risk" && t.lookup(2000.5, 2).label == "Low Risk", "2000-2001 is not a gap");
        }
        // 3.05 kohm with IPS -5.4: Z 2000-4000 but IPS below -5 -> Low Risk
        reading(a, 3050, -5.4);
        CHECK(a.last().label == "Low Risk" && a.last().answered(), "3.05 k at IPS -5.4 is Low Risk by the table");
        CHECK(a.last().reason.find("fat") == std::string::npos, "and nothing calls it fat: there is no placement gate");
        CHECK(a.ipsText() != "\xE2\x80\x94" && std::fabs(std::atof(a.ipsText().c_str()) + 5.4) < 0.3, "its IPS is in the tile, as on the charts");
        const int valid0 = a.caseResult().nValid;
        run(a, 3200);
        reading(a, 3050, -3.0);
        CHECK(a.last().label == "High Risk" && a.last().answered(), "3.05 k at IPS -3 is High Risk");
        CHECK(a.caseResult().nValid == valid0 + 1, "and it counts");
        CHECK(a.caseLabel() == "High Risk", "one Low and one High: the lesion is High, not Suspicious or Invalid");
        // readings far apart in IPS no longer make the lesion Invalid, and it never sticks
        run(a, 3200);
        reading(a, 1500, 4.0);
        run(a, 3200);
        reading(a, 1500, -25.0);
        CHECK(a.caseLabel() == "High Risk" || a.caseLabel() == "Low Risk", "four readings spread over 29 IPS: still an answer");
        CHECK(a.caseResult().tier != birx::Tier::Invalid, "never Invalid for disagreeing");
        CHECK(a.caseResult().nValid == valid0 + 3, "every one counted");
        // a high impedance is whatever its row says: the shipped Noisy row, from 5 kohm
        run(a, 3200);
        reading(a, 7000, -5);
        CHECK(a.last().label == "Noisy" && !a.last().answered(), "7 kohm is the Noisy row's, because a row says so");
        CHECK(!sim::g_voice.empty() && sim::g_voice.back() == 12, "and it plays the Noisy sound (track 12), though not counted");
        // a refused sweep (no IPS) is shown in the table's word for a bad reading
        CHECK(app::RulesConfig::defaults().toRuleTable(true).lookup(birx::kNaN, 1).label == "Noisy",
              "no number at all is Noisy, not a fourth word");
        // an older stored table is replaced by this one, once
        {
            app::Settings old;
            std::string txt = app::ConfigStore::serialise(old);
            const size_t p = txt.find("tv=");
            txt.erase(p, txt.find('\n', p) - p + 1);
            txt += "R=0|1900|-11|6|1|high|#C62828||High Risk\n";
            app::Settings back;
            app::ConfigStore::parse(txt, &back);
            CHECK(back.rulesReplaced && back.rules.rows.size() == 8, "an older stored table is replaced by the new one");
            app::Settings again;
            app::ConfigStore::parse(app::ConfigStore::serialise(back), &again);
            CHECK(!again.rulesReplaced, "and only once");
            // H7-1.7.0: a table saved by 1.5.0/1.6.0 keeps its rows; only empty sounds are filled by level
            std::string v2 = app::ConfigStore::serialise(back);
            v2.replace(v2.find("tv=3"), 4, "tv=2");
            const size_t r1 = v2.find("R=");
            v2.insert(r1, "R=0|100|-1|1|1|high|#123456||Mine\nR=0|100|2|3|1|low|#123456|Track 9|Kept\n");
            app::Settings s3;
            app::ConfigStore::parse(v2, &s3);
            CHECK(s3.rulesReplaced && s3.rules.rows.size() == 10 && s3.rules.rows[0].label == "Mine",
                  "a 1.5.0 table is kept, row for row");
            CHECK(s3.rules.rows[0].sound == app::kSoundHigh && s3.rules.rows[1].sound == "Track 9",
                  "an empty sound gets its level's, a chosen one is kept");
            bool allSound = true;
            for (const auto& w : app::RulesConfig::defaults().rows) {
                const birx::Tier t = app::levelFromName(w.level);
                const char* want = t == birx::Tier::High ? app::kSoundHigh : t == birx::Tier::Low ? app::kSoundLow : app::kSoundNoisy;
                if (w.sound != want) allSound = false;
            }
            CHECK(allSound, "the shipped rows: High Risk track 1, Low Risk track 6, Noisy track 12");
        }
        run(a, 3200);
    }

    // H7-1.9.0: deleting the logs
    {
        run(a, 3200);
        if (!a.caseOpen()) { std::string w; a.beginPatient(&w); }
        CHECK(!a.deleteLogsBlocker().empty(), "not while a case is open");
        a.endCase();
        run(a, 3200);
        reading(a, 900, -5);                       // a reading with no case: a log too
        run(a, 3200);
        a.refreshLog();
        int files = 0; long bytes = 0;
        a.logsFootprint(&files, &bytes);
        CHECK(a.logCount() > 0 && files > 3 && bytes > 0, "there are logs to delete");
        const std::string rec = a.recordsDir();
        const std::string idBefore = a.caseId();
        if (a.diagnosticMode()) {
            CHECK(a.deleteLogsBlocker().find("Diagnostic mode") == 0, "not while diagnostic mode writes its file");
            a.stopDiagnosticMode();
        }
        CHECK(a.deleteLogsBlocker().empty() && a.deleteLogsStart().empty(), "with no case open it starts");
        CHECK(a.deletingLogs() && a.busy(), "and nothing is measured meanwhile");
        run(a, 500);
        CHECK(!a.deletingLogs(), "it finishes");
        const std::string res = a.takeDeleteResult();
        std::printf("%s\n", res.c_str());
        CHECK(res.find("The logs are deleted") == 0, "and says so");
        CHECK(a.logCount() == 0 && a.readingCount() == 0, "the Logs list is empty");
        CHECK(!plat::fileExists(rec + "/dataset.csv") && !plat::fileExists(rec + "/patients.csv"), "dataset and patients are gone");
        CHECK(plat::fileExists(rec + "/caseid_state.txt"), "the case numbering is kept");
        a.logsFootprint(&files, &bytes);
        CHECK(files == 0, "nothing of the logs is left");
        // and the instrument goes on recording
        std::string w; a.beginPatient(&w);
        CHECK(a.caseId() != idBefore, "the next case gets a new number, never one used before");
        { sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air); a.calOpen(); a.calRun(); run(a, 9000); salineOk(a); }
        reading(a, 900, -5);
        run(a, 500);
        a.refreshLog();
        CHECK(a.logCount() == 1 && plat::fileExists(rec + "/dataset.csv"), "and the next reading is recorded as before");
        a.endCase();
        run(a, 3200);
    }

    // H7-2.0.0: needle lots against the reference, and the lot calibration file
    {
        if (a.caseOpen()) a.endCase();
        run(a, 3500);
        // one test sweep: load, start, wait for it and for the unit to be idle again
        auto lotSweep = [&](int idx, int needle, int step, double z, double cp, double tempC = 25) -> bool {
            sim::Load l;
            if (step == 0) { l.kind = "air"; l.cpPf = cp; }
            else { l.kind = "saline"; l.z1k = z; l.cpPf = cp; }
            sim::device_setLoad(l);
            const std::string w = a.lotStartTest(idx, needle, step, tempC, birx::kNaN);
            if (!w.empty()) { std::printf("lot test refused: %s\n", w.c_str()); return false; }
            run(a, 1000);
            for (int k = 0; k < 60 && (a.busy() || a.lotTesting()); ++k) run(a, 500);
            if (!a.lotMessageOk()) std::printf("lot test: %s\n", a.lotMessage().c_str());
            return a.lotMessageOk();
        };
        // the saline strengths' resistance for a tip of relative size g: 0.9 %, 0.225 %, 0.09 %
        const double zs[4] = { 0, 320, 1250, 3100 };
        auto measureLot = [&](int idx, double geomFor(int), double cp) {
            const birx::LotRun r = a.lotRuns()[size_t(idx)];
            for (int n = 1; n <= r.plan.needles; ++n)
                for (int st = 0; st <= r.plan.phantoms; ++st)
                    for (int k = 0; k < r.plan.repeats; ++k)
                        lotSweep(idx, n, st, zs[st] * geomFor(n), cp + n * 0.5);
        };
        auto findLot = [&](const std::string& id, bool ref) {
            for (size_t i = 0; i < a.lotRuns().size(); ++i)
                if (a.lotRuns()[i].lotId == id && a.lotRuns()[i].reference == ref) return int(i);
            return -1;
        };
        birx::LotPlan refPlan; refPlan.needles = 1; refPlan.repeats = 2; refPlan.phantoms = 3;
        CHECK(a.lotAdd("TRIAL-1", true, refPlan).empty(), "the reference needle is added");
        CHECK(!a.lotAdd("TRIAL-2", true, refPlan).empty(), "only one reference");
        int ri = findLot("TRIAL-1", true);
        // a saline sweep before the needle's air is refused
        CHECK(!a.lotStartTest(ri, 1, 1, 25, birx::kNaN).empty(), "saline before air is refused");
        measureLot(ri, [](int) { return 1.0; }, 60);
        ri = findLot("TRIAL-1", true);
        CHECK(a.lotRuns()[size_t(ri)].complete(), "the reference's plan is complete");
        // the air test catches a moved cable
        birx::LotPlan p; p.needles = 3; p.repeats = 2; p.phantoms = 3;
        CHECK(a.lotAdd("L-SAME", false, p).empty() && a.lotAdd("L-BIG", false, p).empty() &&
              a.lotAdd("L-FAR", false, p).empty() && a.lotAdd("L-MIXED", false, p).empty(), "four lots added");
        measureLot(findLot("L-SAME", false), [](int) { return 1.01; }, 59);
        measureLot(findLot("L-BIG", false), [](int) { return 1.15; }, 62);
        measureLot(findLot("L-FAR", false), [](int) { return 1.6; }, 60);
        measureLot(findLot("L-MIXED", false), [](int n) { return n == 2 ? 1.25 : 1.0; }, 60);
#ifdef APPTEST_HOST
        {
            // H7-2.1.1: the Lots tab asks for every run's result on every frame; that must cost nothing
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 200; ++k)
                for (int i = 0; i < int(a.lotRuns().size()); ++i) (void)a.lotResult(i);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("200 frames of lot results: %.2f ms\n", ms);
            CHECK(ms < 20, "lot results are worked out once, not on every frame");
        }
#endif
        const birx::LotResult same = a.lotResult(findLot("L-SAME", false));
        const birx::LotResult big  = a.lotResult(findLot("L-BIG", false));
        const birx::LotResult far  = a.lotResult(findLot("L-FAR", false));
        const birx::LotResult mix  = a.lotResult(findLot("L-MIXED", false));
        std::printf("L-SAME: %s | %s\nL-BIG: %s | %s\nL-FAR: %s | %s\nL-MIXED: %s | %s\n",
                    birx::lotVerdictName(same.verdict), same.headline.c_str(), birx::lotVerdictName(big.verdict), big.headline.c_str(),
                    birx::lotVerdictName(far.verdict), far.headline.c_str(), birx::lotVerdictName(mix.verdict), mix.headline.c_str());
        for (const std::string& f : big.findings) std::printf("   %s\n", f.c_str());
        CHECK(same.verdict == birx::LotVerdict2::SameAsReference, "a lot like the reference: no correction needed");
        {
            const birx::LotCompare cmp = a.lotCompare(findLot("L-BIG", false));
            bool close = cmp.ok && cmp.steps.size() == 3;
            for (const auto& q : cmp.steps)
                close = close && !q.ref.freqHz.empty() && q.ref.freqHz.size() == q.lotT.freqHz.size() &&
                        std::fabs(q.ipsLotT - q.ipsRef) < 0.3 && std::fabs(q.zLotT / q.zRef - 1) < 0.02 &&
                        std::fabs(q.zLot / q.zRef - 1.15) < 0.03;
            CHECK(close, "the comparison: the lot as measured 15 % high in Z, corrected onto the reference's curves");
            CHECK(std::fabs(cmp.transfer.kZ - 0.87) < 0.02 && cmp.transfer.cpRefF > 60e-12 && cmp.transfer.cpRefF < 61e-12,
                  "and it says what is applied: Z x 0.87, the reference's 60.5 pF");
            CHECK(!a.lotCompare(findLot("TRIAL-1", true)).ok, "the reference is not compared with itself");
        }
        CHECK(big.verdict == birx::LotVerdict2::UsableWithCalibration && std::fabs(big.kZMean - 1 / 1.15) < 0.02,
              "a lot with 15 % larger Z: usable with a Z factor of about 0.87");
        CHECK(far.verdict == birx::LotVerdict2::RangesMustChange, "60 % larger Z: the ranges must change, not the readings");
        // H7-2.x: a needle that disagrees with the others of its lot is set aside (the saline check refuses such a
        // needle in the clinic); the lot is judged by its good needles
        CHECK(mix.verdict == birx::LotVerdict2::UsableWithCalibration && mix.headline.find("Excluded: needle 2") != std::string::npos,
              "needles that disagree: the odd one is set aside, the others decide");
        const auto eq = a.lotEquivalentRanges(findLot("L-FAR", false));
        CHECK(!eq.empty(), "the equivalent ranges are given");
        for (const auto& b : eq) if (b.zMax < 1e5)
            std::printf("   %-10s Z %5.0f..%5.0f -> %5.0f..%5.0f   IPS %+6.1f..%+6.1f -> %+6.1f..%+6.1f\n", b.label.c_str(),
                        b.zMin, b.zMax, b.lzMin, b.lzMax, b.ipsMin, b.ipsMax, b.lipsMin, b.lipsMax);
        CHECK(!a.lotApprove(findLot("L-FAR", false)).empty() && !a.lotFileLoaded(), "a lot whose ranges must change is not approved");
        CHECK(a.lotApprove(findLot("TRIAL-1", true)).find("is in the lot") != std::string::npos, "the reference goes in");
        CHECK(a.lotApprove(findLot("L-BIG", false)).find("rev 2") != std::string::npos, "L-BIG goes in, rev 2");
        CHECK(a.lotApprove(findLot("L-SAME", false)).find("rev 3") != std::string::npos, "L-SAME goes in, rev 3");
        CHECK(a.lotFile().find("L-BIG") && std::fabs(a.lotFile().find("L-BIG")->kZ - 0.87) < 0.02 &&
              std::fabs(a.lotFile().find("L-SAME")->kZ - 1) < 0.02, "L-BIG carries a Z ratio of 0.87, L-SAME about 1");
        // export, and another device loads it
        const std::string ex = a.lotExport();
        std::printf("%s\n", ex.c_str());
        CHECK(ex.find("BIRADx_lotcal_") != std::string::npos, "exported to the stick");
        // a reading with a needle of L-BIG reads as the reference would
        {
            std::string w; a.beginPatient(&w);
            app::Settings stq = a.settings(); stq.salineCheck = 0; a.saveSettings(stq, &w);   // the lot file's corrections only
            a.setIntakeValue("needle_lot", "L-BIG");
            run(a, 3500);
            CHECK(a.lotStatus() == "lot L-BIG corrected", "lot L-BIG typed: its correction is in force");
            // what the reference needle reads in this tissue ...
            a.setIntakeValue("needle_lot", "TRIAL-1");
            run(a, 3500);
            auto lesion = [&](double z, double ips, double cp) {
                sim::Load l; l.kind = "lesion"; l.z1k = z; l.ips = ips; l.cpPf = cp; sim::device_setLoad(l);
                a.startRun(); run(a, 3000); run(a, 3200);
            };
            for (double zt : { 900.0, 2600.0 }) {
                a.setIntakeValue("needle_lot", "TRIAL-1"); run(a, 300);
                lesion(zt, -2.0, 60.5);
                const double zRef = a.last().features.z1k, ipsRef = a.last().features.ipsLegacy;
                // ... and a needle of L-BIG in the same tissue: a 15 % larger tip, 2.5 pF more
                a.setIntakeValue("needle_lot", "L-BIG"); run(a, 300);
                lesion(zt * 1.15, -2.0, 63.0);
                std::printf("tissue %.0f: reference Z %.0f IPS %+.2f | L-BIG corrected Z %.0f IPS %+.2f\n", zt, zRef, ipsRef,
                            a.last().features.z1k, a.last().features.ipsLegacy);
                CHECK(std::fabs(a.last().features.z1k / zRef - 1) < 0.02 && std::fabs(a.last().features.ipsLegacy - ipsRef) < 0.3,
                      "a needle of L-BIG reads as the reference needle would (Z within 2 %, IPS within 0.3)");
            }
            a.setIntakeValue("needle_lot", "L-UNKNOWN");
            run(a, 200);
            CHECK(a.lotStatus().find("not in the lot file") != std::string::npos, "an unknown lot is said to be unknown");
            app::Settings st0 = a.settings();
            app::Settings st = st0; st.reqLotFile = true; st.reqNeedleCal = false; st.reqIntake = false; st.reqTissueRef = false;
            std::string why2; a.saveSettings(st, &why2);
            CHECK(a.measureBlocker().find("not in the lot calibration file") != std::string::npos, "and, if required, blocks a reading");
            a.setIntakeValue("needle_lot", "L-BIG"); run(a, 300);
            CHECK(a.measureBlocker().empty(), "a lot in the file is not blocked");
            a.saveSettings(st0, &why2);
            stq = a.settings(); stq.salineCheck = 2; a.saveSettings(stq, &why2);
            a.endCase();
            run(a, 3200);
        }
        // the file is checked when loaded
        {
            birx::LotFile f; std::string why;
            std::string t = a.lotFile().save();
            CHECK(f.load(t, &why) && f.rev == 3 && f.lots.size() == 3, "the file reads back");
            t[t.find("L-BIG") + 2] = 'X';
            CHECK(!f.load(t, &why) && why.find("check sum") != std::string::npos, "a changed file is refused");
        }
        // Delete logs keeps the lots and the file
        if (a.diagnosticMode()) a.stopDiagnosticMode();
        CHECK(a.deleteLogsStart().empty(), "delete logs");
        run(a, 500);
        a.takeDeleteResult();
        app::App b; b.begin();
        CHECK(b.lotFileLoaded() && b.lotFile().rev == 3 && b.lotRuns().size() == 5, "lots and the lot file survive Delete logs and a restart");
        run(a, 3200);
    }

    // H7-2.1.0: rule ranges found without air calibration -> readings taken as they come
    {
        run(a, 3500);
        if (a.caseOpen()) a.endCase();
        run(a, 3200);
        std::string w; a.beginPatient(&w);
        CHECK(!a.setRulesAirCal(false).empty(), "not while a case is open");
        a.endCase(); run(a, 3200);
        sim::Load air; air.kind = "air"; air.cpPf = 60; sim::device_setLoad(air);
        a.calOpen(); a.calRun(); run(a, 9000); run(a, 3200);
        CHECK(a.needleReady(), "a needle calibrated in air");
        CHECK(salineOk(a, 320, 60, 20), "and checked in saline");
        auto lesion = [&](double z, double ips, double cp) {
            sim::Load l; l.kind = "lesion"; l.z1k = z; l.ips = ips; l.cpPf = cp; sim::device_setLoad(l);
            a.startRun(); run(a, 3000); run(a, 3200);
            return a.last().features.ipsLegacy;
        };
        const double withAir = lesion(2600, -2.0, 60);
        CHECK(a.setRulesAirCal(false).empty() && !a.airCalUsed(), "the rules are marked as found without air calibration");
        const double asItCame = lesion(2600, -2.0, 60);
        std::string w2; a.beginPatient(&w2); a.endCase(); run(a, 3200);   // End case retires the needle
        CHECK(!a.needleReady(), "the needle is retired");
        {   // (a needle without its checks reads only with them off: Settings -> Needle)
            app::Settings st = a.settings(); st.salineCheck = 0; std::string w3; a.saveSettings(st, &w3);
        }
        const double noCal = lesion(2600, -2.0, 60);
        std::printf("IPS with the air correction %+.2f, rules without it %+.2f, needle not calibrated %+.2f\n", withAir, asItCame, noCal);
        // (asItCame still has its saline check, whose interface model is taken off; noCal has no check at all)
        CHECK(std::fabs(asItCame - noCal) < 0.3, "the air calibration is not applied: the reading is as it came");
        CHECK(a.measureBlocker().find("not been calibrated") == std::string::npos, "and it is not asked for");
        {
            app::Settings st = a.settings(); st.salineCheck = 2; std::string w3; a.saveSettings(st, &w3);
        }
        for (size_t i = 0; i < a.lotRuns().size(); ++i)
            if (a.lotRuns()[i].lotId == "L-BIG") {
                const birx::LotResult r2 = a.lotResult(int(i));
                std::printf("L-BIG with rules found without air calibration: %s, after the transfer %.2f IPS, %.1f %% Z\n",
                            birx::lotVerdictName(r2.verdict), r2.worstResidIps, r2.worstResidZ * 100);
                CHECK(r2.verdict == birx::LotVerdict2::UsableWithCalibration && r2.worstResidIps < 0.3,
                      "a lot is brought to the reference the same way when the rules were found without air calibration");
            }
        app::Settings b; app::ConfigStore::parse(app::ConfigStore::serialise(a.settings()), &b);
        CHECK(!b.rulesAirCal, "the choice is kept in the settings memory");
        CHECK(a.setRulesAirCal(true).empty() && a.airCalUsed(), "and can be put back");
        run(a, 3200);
    }

    // H7-1.6.0: nothing from the measurement unit after start -> restarted once by itself;
    // and when a restart does not help, the screen says what to check
    {
        sim::device_setAlive(false, false);
        sim::g_restartRepairs = false;
        const int r0 = sim::g_unitRestarts;
        app::App n; n.begin();
        run(n, 11000);
        CHECK(sim::g_unitRestarts == r0, "not before 12 s");
        run(n, 2000);
        CHECK(sim::g_unitRestarts == r0 + 1, "nothing arrived after start: the unit is restarted once");
        run(n, 14000);
        CHECK(n.linkMessage().find("still sends nothing") != std::string::npos, "it did not help: the screen says what to check");
        CHECK(!n.busy(), "and the instrument is not left waiting");
        run(n, 30000);
        CHECK(sim::g_unitRestarts == r0 + 1, "and the start check is done once only");
        sim::g_restartRepairs = true;
        sim::device_setAlive(true, true);
    }

    // --- H7-2.12.0: the Training needle's QC — a new tip settles first; a check with the tip out of the saline is no
    // check; a sweep is learnt once a check of its needle confirms it, also after another needle or a restart;
    // a material can be left out of the comparison; the main needle's tip surface is recorded with its readings
    {
        const std::string tip = slurp(rec + "/needle_tip.csv");
        CHECK(tip.compare(0, 5, "when,") == 0 && tip.find(",check,") != std::string::npos && tip.find(",reading,") != std::string::npos,
              "needle_tip.csv: the main needle's checks and readings, with its tip surface and its time in the saline");
        app::App t; t.begin(); run(t, 1500);
        auto air = [&](app::App& x) {
            sim::Load l; l.kind = "air"; l.cpPf = 60; sim::device_setLoad(l);
            x.calOpen(true); x.calRun(); run(x, 9000);
        };
        auto check = [&](app::App& x, double ze, const char* kind = "saline") {
            sim::Load l; l.kind = kind; l.z1k = 140; l.cpPf = 60; l.ze1k = ze; l.zeAlpha = 0.6; sim::device_setLoad(l);
            const std::string w = x.matStartRef(25);
            if (!w.empty()) return w;
            for (int k = 0; k < 120 && x.matRefRunning(); ++k) run(x, 500);
            run(x, 3500);
            return x.matMessage();
        };
        auto sweep = [&](app::App& x, const std::string& label, double z, bool fresh) {
            sim::Load l; l.kind = "saline"; l.z1k = z; l.cpPf = 60; l.ze1k = 290; l.zeAlpha = 0.6; sim::device_setLoad(l);
            const std::string w = x.matStart(label, fresh);
            if (!w.empty()) return w;
            for (int k = 0; k < 120 && x.matTesting(); ++k) run(x, 500);
            run(x, 3500);
            return std::string();
        };
        auto count = [&](app::App& x, const char* qc) {
            int n = 0; for (const birx::MatSweep& s : x.matLibrary().sweeps) if (s.qc == qc) ++n; return n;
        };
        CHECK(t.setTrainNeedle("T1").empty(), "Training needle T1");
        CHECK(t.matNextStep() == app::App::NeedleStep::Air, "H7-2.13.0: the next step, offered where the sweep is: its Air check");
        air(t);
        CHECK(t.qcAirOk(), "its Air check");
        CHECK(t.matNextStep() == app::App::NeedleStep::Saline, "then its saline check");
        const int passes0 = t.qcPasses();
        std::string m = check(t, 600);
        CHECK(t.qcCheckOk() && t.matBlocker().empty() && t.matNextStep() == app::App::NeedleStep::None && t.qcPasses() == passes0 + 1 &&
              m.find("may sweep") != std::string::npos && m.find("A new tip") != std::string::npos,
              "H7-2.13.0: a needle sweeps from its first passing check on (no waiting for its tip to settle)");
        std::printf("first check: %s\n", m.c_str());
        m = check(t, 290);
        CHECK(t.matBlocker().empty(), "a check that finds the tip surface moved, with no sweep between, changes nothing");
        m = check(t, 290, "air");
        CHECK(m.find("Not a check") != std::string::npos && t.qcSettled() && t.matBlocker().empty(),
              "a check with the tip out of the saline is no check: nothing counted for or against the needle");
        std::printf("tip out: %s\n", m.c_str());
        CHECK(t.matAddLabel("A").empty() && t.matAddLabel("B").empty(), "two labels");
        CHECK(sweep(t, "A", 320, true).empty() && sweep(t, "A", 320, false).empty() && sweep(t, "A", 330, true).empty() &&
              sweep(t, "A", 330, false).empty() && sweep(t, "B", 900, true).empty() && sweep(t, "B", 900, false).empty() &&
              sweep(t, "B", 920, true).empty() && sweep(t, "B", 920, false).empty(), "eight sweeps");
        CHECK(count(t, "pending") == 8 && t.qcPending() == 8 && !t.matModel().ok && t.matModel().nQcPending == 8,
              "not learnt until a check confirms them");
        m = check(t, 285);
        CHECK(count(t, "ok") == 8 && t.matModel().ok && t.qcPending() == 0, "the next check confirms them: learnt");
        CHECK(t.matSetOff("B", true).empty() && !t.matModel().ok && t.matModel().nOff == 4, "B left out of the comparison");
        CHECK(t.matLibrary().isOff("B"), "and so marked");
        CHECK(t.matSetOff("B", false).empty() && t.matModel().ok, "and back in");
        {
            birx::MatLibrary L1; L1.labels = { "x", "y" }; L1.off = { "y" };
            birx::MatLibrary L2; L2.load(L1.save());
            CHECK(L2.isOff("y") && !L2.isOff("x") && L2.labels.size() == 2, "a label left out is kept so in materials.txt");
        }
        CHECK(sweep(t, "A", 320, true).empty() && sweep(t, "A", 320, false).empty(), "two more sweeps of T1");
        m = t.setTrainNeedle("T2");
        CHECK(m.empty() && t.matMessage().find("wait for its next one") != std::string::npos && t.qcPending() == 0 &&
              t.qcWaiting() == 2 && t.qcUnconfirmed() == 0, "another needle: T1's two sweeps wait for T1's next check");
        CHECK(t.setTrainNeedle("T1").empty(), "T1 again");
        if (!t.qcAirOk()) air(t);
        m = check(t, 280);
        CHECK(t.qcSettled() && count(t, "ok") == 10 && t.qcWaiting() == 0,
              "its next check (it agrees with its last) closes them, and a settled needle need not settle again");
        std::printf("T1 again: %s\n", m.c_str());
        CHECK(slurp(rec + "/training_qc.txt").find("N=T1|") != std::string::npos, "each needle's last check is on record");
        CHECK(sweep(t, "A", 320, true).empty() && sweep(t, "A", 320, false).empty(), "two more sweeps");
        std::remove((rec + "/training_qc.txt").c_str());
        app::App u; u.begin(); run(u, 1500);
        CHECK(u.qcUnconfirmed() == 2, "after a restart without the record, no check can confirm them");
        CHECK(u.matKeepUnconfirmed().find("learnt") != std::string::npos && u.qcUnconfirmed() == 0 && count(u, "kept") == 2,
              "learnt by hand");
        // H7-2.12.4: the 2 minutes count from the first of the checks that agree; and the way out, by hand
        air(u);
        m = check(u, 280);
        CHECK(u.qcCheckOk() && u.matBlocker().empty(), "no check on record: it sweeps from its first check");
        CHECK(u.setTrainNeedle("T6").empty(), "a new needle");
        air(u);
        m = check(u, 600);
        CHECK(u.matBlocker().empty(), "a new needle sweeps at once");
        {
            app::Settings st = u.settings();
            const int n0 = st.qcSweeps;
            CHECK(u.setQcIntervals(1, st.qcMinutes, st.qcAirMinutes).empty(), "a check every sweep (for the test)");
            CHECK(sweep(u, "A", 330, true).empty() && u.matNextStep() == app::App::NeedleStep::Saline,
                  "a check due: the next step offered where the sweep is is the saline check");
            m = check(u, 600);
            CHECK(u.matNextStep() == app::App::NeedleStep::None && count(u, "pending") == 0, "done: the sweep is confirmed, and sweeping goes on");
            CHECK(u.setQcIntervals(n0, st.qcMinutes, st.qcAirMinutes).empty(), "back to every 10");
        }
        CHECK(sweep(u, "A", 320, true).empty() && count(u, "pending") == 1, "it sweeps; the sweep waits for the next check");
        m = check(u, 300);
        // H7-2.12.6: R held, the tip surface moved: learnt as measured, not needle-normalised
        CHECK(count(u, "ok-raw") == 1 && m.find("learnt as measured") != std::string::npos && u.matModelN().nRawOnly == 1,
              "the next check found only the tip surface moved (600 -> 300 ohm), R held: learnt as measured, not normalised");
        std::printf("tip surface moved: %s\n", m.c_str());
        CHECK(sweep(u, "A", 320, true).empty(), "another sweep");
        {
            sim::Load l; l.kind = "saline"; l.z1k = 160; l.cpPf = 60; l.ze1k = 300; l.zeAlpha = 0.6; sim::device_setLoad(l);
            u.matStartRef(25);
            for (int k = 0; k < 120 && u.matRefRunning(); ++k) run(u, 500);
            run(u, 3500);
            m = u.matMessage();
        }
        CHECK(count(u, "suspect") == 1 && m.find("This check itself is within the limits") != std::string::npos &&
              m.find("SUSPECT") != std::string::npos,
              "its R moved (140 -> 160 ohm, 14 %): the sweep between is suspect, and the message says the check itself passed");
        std::printf("R moved: %s\n", m.c_str());
        {   // H7-2.12.5: a library whose every sweep is suspect says so, and the suspect sweeps can be learnt by hand
            birx::MatLibrary L1;
            L1.labels = { "x", "y" };
            const std::vector<birx::MatSweep>& sw = u.matLibrary().sweeps;
            for (int k = 0; k < 4 && k < int(sw.size()); ++k) { birx::MatSweep q = sw[size_t(k)]; q.label = k < 2 ? "x" : "y"; q.sample = k; q.qc = "suspect"; L1.sweeps.push_back(q); }
            const birx::MatModel m1 = birx::trainMaterials(L1);
            CHECK(!m1.ok && m1.why.find("4 SUSPECT") != std::string::npos, "not learnt: the reason names the 4 suspect sweeps");
            std::printf("all suspect: %s\n", m1.why.c_str());
        }
        CHECK(u.matKeepSuspect().find("learnt by hand") != std::string::npos && count(u, "suspect") == 0 && count(u, "kept") == 3,
              "Learn them anyway: the suspect sweep is learnt by hand");
    }

    // --- H7-2.15.0: the QC by records_7 — the leakage against the needle's own, the generic limits, a resistor is no
    // reading, the saline's temperature kept, and the suspect sweeps judged again by today's rules
    {
        app::App v; v.begin(); run(v, 1500);
        if (v.caseOpen()) v.endCase();
        auto airV = [&](double gpS) {
            sim::Load l; l.kind = "air"; l.cpPf = 60; l.airGpS = gpS; sim::device_setLoad(l);
            v.calOpen(true); v.calRun(); run(v, 9000);
            return v.calError();
        };
        CHECK(v.setTrainNeedle("LK1").empty(), "needle LK1");
        CHECK(airV(8e-6).empty() && std::fabs(v.needleLeakBase("lk1") - 8e-6) < 0.5e-6, "its first Air check: 8 uS, its own on record");
        std::string e = airV(30e-6);
        CHECK(e.find("times this needle's own") != std::string::npos && !v.qcAirOk(),
              "30 uS, under the generic 40 but over three times its own: refused");
        std::printf("worn: %s\n", e.c_str());
        CHECK(airV(20e-6).empty(), "20 uS: within 3 x 8 uS, accepted");
        CHECK(v.setTrainNeedle("LK2").empty(), "a new needle, LK2");
        e = airV(45e-6);
        CHECK(e.find("generic limits (40.0 uS)") != std::string::npos, "45 uS on a needle with no record: over the generic 40 uS, refused");
        // the generic R window in 0.9 % saline: 90 to 250 ohm (main screen)
        {
            sim::Load l; l.kind = "air"; l.cpPf = 60; sim::device_setLoad(l);
            v.calOpen(); v.calRun(); run(v, 9000);
        }
        CHECK(!salineOk(v, 50) && v.dipError().find("90 to 250 ohm") != std::string::npos, "a saline check of 50 ohm is refused");
        CHECK(salineOk(v, 140), "140 ohm passes");
        const int n0 = v.caseResult().nReadings;
        {
            sim::Load l; l.kind = "resistor"; l.z1k = 3050; l.cpPf = 60; sim::device_setLoad(l);
            v.startRun(); run(v, 3000); run(v, 3200);
        }
        CHECK(v.caseResult().nReadings == n0 && v.linkMessage().find("no tip surface") != std::string::npos && v.needleReady(),
              "a resistor across the electrodes is no reading (nothing counted), and the needle's checks stand");
        std::printf("resistor: %s\n", v.linkMessage().c_str());
        reading(v, 900, 10.2);
        CHECK(v.caseResult().nReadings == n0 + 1, "a tip in tissue is a reading");
        {
            std::vector<birx::Point> pts;
            for (double f : { 500000.0, 300000.0, 250000.0, 225000.0, 199000.0, 175000.0, 125000.0, 100000.0, 75000.0, 50000.0,
                              25000.0, 10000.0, 5000.0, 1000.0, 500.0 }) {
                birx::Point p; p.freqHz = f; p.magOhm = f > 400000 ? 2266 : 313; p.phaseDeg = f > 400000 ? -70.7 : 1.3; pts.push_back(p);
            }
            const std::string w = birx::matNotATip(pts, birx::kNaN);
            CHECK(w.find("did not measure") != std::string::npos, "a sweep of one value repeated (records_7, 28 Oct 09:51) is no sweep");
        }
        // the saline's temperature, kept across a power cycle; asked for again once after it
        CHECK(!v.salineTempEntered() && v.setSalineTemp(24.5).empty() && v.salineTempEntered() &&
              std::fabs(v.settings().salineTempC - 24.5) < 1e-9, "the saline's temperature entered: 24.5");
        {
            app::App w2; w2.begin(); run(w2, 500);
            CHECK(std::fabs(w2.settings().salineTempC - 24.5) < 1e-9 && !w2.salineTempEntered(),
                  "kept after a restart, and asked for again (not yet entered since power-on)");
        }
        // the suspect sweeps judged again at start
        {
            birx::MatLibrary L = v.matLibrary();
            const std::vector<birx::MatSweep> base = v.matLibrary().sweeps;
            CHECK(base.size() >= 5, "sweeps to make a library of");
            L.sweeps.clear(); L.labels = { "A" }; L.off.clear();
            const char* when[5] = { "2026-10-27T12:10:00Z", "2026-10-27T12:11:00Z", "2026-10-27T12:12:00Z", "2026-10-27T13:10:00Z",
                                    "2026-10-27T12:13:00Z" };
            const char* move[5] = { "R 5.0 %, tip surface 30 %", "R 12.0 %, tip surface 5 %", "the tip fouled", "", "" };
            for (int k = 0; k < 5 && k < int(base.size()); ++k) {
                birx::MatSweep q = base[size_t(k)];
                q.label = "A"; q.sample = k + 1; q.needle = "RJ1"; q.when = when[k]; q.qc = "suspect"; q.qcMove = move[k];
                q.gpS = 9e-6; q.refR = birx::kNaN;
                L.sweeps.push_back(q);
            }
            { std::ofstream f(rec + "/materials.txt", std::ios::binary | std::ios::trunc); f << L.save(); }
            {
                std::ofstream f(rec + "/saline_check.csv", std::ios::binary | std::ios::trunc);
                f << "when,needle,lot,solution,validated,tempC,condMsCm,cpPf,rOhm25,ze1kOhm,alpha,rms,drift,kZ,accepted,why\n"
                  << "2026-10-27T12:00:00Z,RJ1,,0.9 %,no,22.0,nan,60,140.0,500.0,0.8,0.02,0.01,nan,yes,\"\"\n"
                  << "2026-10-27T12:20:00Z,RJ1,,0.9 %,no,22.0,nan,60,141.0,700.0,0.8,0.02,0.01,nan,yes,\"\"\n"
                  << "2026-10-27T13:00:00Z,RJ1,,0.9 %,no,22.0,nan,60,140.0,500.0,0.8,0.02,0.01,nan,yes,\"\"\n"
                  << "2026-10-27T13:30:00Z,RJ1,,0.9 %,no,22.0,nan,60,140.5,510.0,0.8,0.02,0.01,nan,yes,\"\"\n";
            }
            {
                const bool fresh = !std::ifstream(rec + "/needle_cal.csv").good();
                std::ofstream f(rec + "/needle_cal.csv", std::ios::binary | std::ios::app);
                if (fresh) f << "needle_id,batch_id,needle_rev,taken_at,operator,range,cp_pf,gp_us,rms_rel,n_points,z_open_500k_ohm\n";
                f << "RJ1,,NDL-D,2026-10-27T11:59:00Z,operator,3,60.0,8.0,0.01,11,2650.0\n"
                  << "RJ1,,NDL-D,2026-10-27T13:20:00Z,operator,3,60.0,8.2,0.01,11,2650.0\n";
            }
            app::App r; r.begin(); run(r, 500);
            auto qcOf = [&](int k) { return r.matLibrary().sweeps.size() > size_t(k) ? r.matLibrary().sweeps[size_t(k)].qc : std::string("?"); };
            CHECK(r.qcRejudged() == 2 && qcOf(0) == "ok-raw" && qcOf(4) == "ok-raw",
                  "judged again: R 5 % (within 8 %) is learnt as measured; a sweep marked before H7-2.12.6 too, from the checks "
                  "around it in saline_check.csv (R 0.7 %, its surface 40 %)");
            CHECK(qcOf(1) == "suspect" && qcOf(2) == "suspect" && qcOf(3) == "suspect",
                  "R 12 % stays suspect, a fouled tip's sweeps stand, and so do the ones with an Air check before their closing check");
            std::printf("rejudged: %d (%s | %s)\n", r.qcRejudged(), qcOf(0).c_str(), r.matLibrary().sweeps[0].qcMove.c_str());
            app::App r2; r2.begin(); run(r2, 500);
            CHECK(r2.qcRejudged() == 0, "once: the next start finds nothing more to judge again");
        }
    }

    std::printf("\n%d of %d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
