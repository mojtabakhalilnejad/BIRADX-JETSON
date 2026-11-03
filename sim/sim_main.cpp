#include <chrono>
// sim/sim_main.cpp — runs the instrument on a PC and writes screenshots.
//   sim_main <script>     script lines:  load lesion 850 -5 | load air 60 | wait 1500 |
//                                        tap x y | press x y | release | drag x0 y0 x1 y1 |
//                                        shot name | id A3K7 | clock 2026 9 24 10 0
#include "../app/app.h"
#include "../gui/gui.h"
#include "../gui/splash.h"
#include "sim.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<uint16_t> fb(1024 * 600);
static int fbW = 1024, fbH = 600;          // BIRADX_SIM_PORTRAIT=1: 600 x 1024 (the ITDS computer's upright screen);
                                           // =<height> (960 .. 1100): 600 x that, as on other upright monitors
static ui::Touch touch;

static void savePpm(const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << fbW << " " << fbH << "\n255\n";
    for (uint16_t p : fb) {
        const unsigned char r = (unsigned char)(((p >> 11) & 31) * 255 / 31);
        const unsigned char g = (unsigned char)(((p >> 5) & 63) * 255 / 63);
        const unsigned char b = (unsigned char)((p & 31) * 255 / 31);
        f.put(char(r)); f.put(char(g)); f.put(char(b));
    }
}

int main(int argc, char** argv)
{
    std::string outDir = "shots";
    if (argc < 2) { std::fprintf(stderr, "usage: sim_main script [outdir]\n"); return 2; }
    if (argc > 2) outDir = argv[2];
    if (std::system(("mkdir -p " + outDir).c_str()) != 0) return 3;
    sim::g_clockBase = int64_t(20720) * 86400 + 9 * 3600 + 30 * 60;   // 2026-09-24 09:30 local
    if (argc > 3 && std::string(argv[3]) == "fresh") {
        if (std::system(("rm -rf " + sim::g_root + " && mkdir -p " + sim::g_root + "/usb").c_str()) != 0) return 3;
    }
    // Lines beginning "boot " set the state the instrument is switched on in.
    {
        std::ifstream pre(argv[1]);
        std::string pl;
        while (std::getline(pre, pl)) {
            std::istringstream l(pl);
            std::string w, k; int v = 0;
            if ((l >> w >> k >> v) && w == "boot") {
                if (k == "sd") sim::g_sd = v;
                else if (k == "usb") sim::g_usb = v;
                else if (k == "intmem") sim::g_intmem = v;
            }
        }
    }
    app::App a;
    a.begin();
    gui::Gui g(a);
    if (const char* pv = std::getenv("BIRADX_SIM_PORTRAIT")) {
        const int h = std::atoi(pv);
        if (h == 1) { fbW = 600; fbH = 1024; }
        else if (h >= 960 && h <= 1100) { fbW = 600; fbH = h; }
        fb.assign(size_t(fbW) * fbH, 0);
    }
    gfx::Canvas canvas(fb.data(), fbW, fbH);
    auto step = [&](uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            sim::advance(10);
            a.poll();
            if (g.wants(touch, sim::g_ticks)) g.frame(canvas, touch, sim::g_ticks);
        }
    };
    std::ifstream in(argv[1]);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream l(line);
        std::string cmd;
        if (!(l >> cmd) || cmd[0] == '#') continue;
        if (cmd == "wait") { int ms; l >> ms; step(uint32_t(ms)); }
        else if (cmd == "load") {
            sim::Load ld; l >> ld.kind;
            // lesion <z> <ips> [<cp pF> <ze at 1 kHz> <alpha>]; saline <z> <cp pF> [<ze> <alpha> <leak>]; air <cp pF> [<uS>]; resistor <ohm> [<cp pF>]
            double v[5]; int nv = 0;
            while (nv < 5 && (l >> v[nv])) ++nv;
            if (ld.kind == "lesion") {
                if (nv > 0) ld.z1k = v[0];
                if (nv > 1) ld.ips = v[1];
                if (nv > 2) ld.cpPf = v[2];
                if (nv > 3) ld.ze1k = v[3];
                if (nv > 4) ld.zeAlpha = v[4];
            } else if (ld.kind == "saline") {
                if (nv > 0) ld.z1k = v[0];
                if (nv > 1) ld.cpPf = v[1];
                if (nv > 2) ld.ze1k = v[2];
                if (nv > 3) ld.zeAlpha = v[3];
                if (nv > 4) ld.leak = v[4];
            } else if (ld.kind == "air" && nv > 0) {
                ld.cpPf = v[0];
                if (nv > 1) ld.airGpS = v[1] * 1e-6;   // H7-2.15.0: air <cp pF> [<leakage uS>]
            } else if (ld.kind == "resistor" && nv > 0) {   // H7-2.15.0: resistor <ohm> [<cp pF>]
                ld.z1k = v[0];
                if (nv > 1) ld.cpPf = v[1];
            }
            sim::device_setLoad(ld);
        }
        else if (cmd == "pedal") sim::device_pressPedal();
        else if (cmd == "stray") sim::device_strayByte();
        else if (cmd == "drift") { l >> sim::g_tickEvery; }      // time moves while the program runs, as on the board
        else if (cmd == "alive") { int hb, ans; l >> hb >> ans; sim::device_setAlive(hb, ans); }
        else if (cmd == "tap") { l >> touch.x >> touch.y; touch.down = true; step(80); touch.down = false; step(120); }
        else if (cmd == "press") { l >> touch.x >> touch.y; touch.down = true; step(60); }
        else if (cmd == "release") { touch.down = false; step(60); }
        else if (cmd == "drag") {
            int x0, y0, x1, y1; l >> x0 >> y0 >> x1 >> y1;
            touch.down = true; touch.x = x0; touch.y = y0; step(40);
            for (int k = 1; k <= 10; ++k) { touch.x = x0 + (x1 - x0) * k / 10; touch.y = y0 + (y1 - y0) * k / 10; step(20); }
            touch.down = false; step(60);
        }
        else if (cmd == "shot") {
            std::string name; l >> name;
            g.frame(canvas, touch, sim::g_ticks);
            savePpm(outDir + "/" + name + ".ppm");
        }
        else if (cmd == "splash") {
            std::string name; l >> name;
            gui::drawSplash(canvas);
            savePpm(outDir + "/" + name + ".ppm");
        }
        else if (cmd == "id") { std::string id; l >> id; a.setDeviceId(id); }
        else if (cmd == "show") { std::string v; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1); g.debugShow(v); step(300); }
        else if (cmd == "intake") { std::string f, v; l >> f; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1); a.setIntakeValue(f, v); }
        else if (cmd == "print") { std::printf("[%u] case %s | %s | %s\n", unsigned(sim::g_ticks), a.caseId().c_str(), a.caseLabel().c_str(), a.caseResult().reason.c_str());
            if (a.haveLast()) std::printf("      last reading: %s | %s\n", a.last().label.c_str(), a.last().reason.c_str()); }
        else if (cmd == "lotadd") {               // lotadd <id> <reference 0/1> <needles> <repeats> <strengths>
            std::string id; int ref = 0; birx::LotPlan p; l >> id >> ref >> p.needles >> p.repeats >> p.phantoms;
            const std::string w = a.lotAdd(id, ref != 0, p);
            if (!w.empty()) std::printf("lotadd: %s\n", w.c_str());
        }
        else if (cmd == "lottest") {              // lottest <run index> <needle> <step> <temp C>
            int idx, n, st; double t; l >> idx >> n >> st >> t;
            const std::string w = a.lotStartTest(idx, n, st, t, birx::kNaN);
            if (!w.empty()) std::printf("lottest: %s\n", w.c_str());
        }
        else if (cmd == "airrules") { int on; l >> on; const std::string w = a.setRulesAirCal(on != 0); if (!w.empty()) std::printf("airrules: %s\n", w.c_str()); }
        else if (cmd == "lotbench") {
            int idx, n; l >> idx >> n;
            const auto t0 = std::chrono::steady_clock::now();
            birx::LotResult r;
            for (int k = 0; k < n; ++k) r = a.lotResult(idx);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("lotbench: %s, %.3f ms per evaluation\n", birx::lotVerdictName(r.verdict), ms / n);
        }
        else if (cmd == "settle") { int t; l >> t; a.setSettleSeconds(t); }   // settling time in saline, s
        else if (cmd == "cal") { a.calOpen(); a.calRun(); }     // the needle's Air check (load air first)
        else if (cmd == "tcal") { a.calOpen(true); a.calRun(); }   // H7-2.10.0: Training's Air check
        else if (cmd == "qc") { int n, m, am; l >> n >> m >> am; a.setQcIntervals(n, m, am); }
        else if (cmd == "trainlot") { std::string v; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
                                      const std::string w = a.setTrainLot(v); if (!w.empty()) std::printf("trainlot: %s\n", w.c_str()); }
        else if (cmd == "dip") { double t; l >> t; a.dipRun(t); }      // the saline check, at <temp C>
        else if (cmd == "lotapprove") { int idx; l >> idx; std::printf("%s\n", a.lotApprove(idx).c_str()); }
        else if (cmd == "needle") { std::string v; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
                                    const std::string w = a.setTrainNeedle(v); if (!w.empty()) std::printf("needle: %s\n", w.c_str()); }
        else if (cmd == "tagcase") {              // tagcase <case id> <label>
            std::string id, v; l >> id; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
            const std::string w = a.tagCase(id, v);
            if (!w.empty()) std::printf("tagcase: %s\n", w.c_str());
        }
        else if (cmd == "matlabel") { std::string v; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
                                      const std::string w = a.matAddLabel(v); if (!w.empty()) std::printf("matlabel: %s\n", w.c_str()); }
        else if (cmd == "matsweep") {             // matsweep <new placement 0/1> <label | - to identify>
            int nw = 0; std::string v; l >> nw; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
            const std::string w = a.matStart(v == "-" ? std::string() : v, nw != 0);
            if (!w.empty()) std::printf("matsweep: %s\n", w.c_str());
        }
        else if (cmd == "matref") { const std::string w = a.matStartRef(); if (!w.empty()) std::printf("matref: %s\n", w.c_str()); }
        else if (cmd == "matkeep") { std::string v; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
                                     const std::string w = a.matKeepLast(v); if (!w.empty()) std::printf("matkeep: %s\n", w.c_str()); }
        else if (cmd == "matprint") {
            const birx::MatModel& md = a.matModel();
            std::printf("[%u] library: %d sweeps, %d labels; model %s%s\n", unsigned(sim::g_ticks), int(a.matLibrary().sweeps.size()),
                        int(a.matLibrary().labels.size()), md.ok ? "ok" : "not yet: ", md.ok ? "" : md.why.c_str());
            if (!a.matMessage().empty()) std::printf("      %s\n", a.matMessage().c_str());
            const birx::MatMatch& mm = a.matLastMatch();
            if (mm.ok && mm.best >= 0) std::printf("      last: like %s (%.0f %%)%s\n", mm.labels[size_t(mm.best)].c_str(),
                                                   100 * mm.prob[size_t(mm.best)], mm.unlike ? ", like none of them" : "");
        }
        // a case without the taps (the same commands as the ITDS script runner): portrait shots
        else if (cmd == "sweep") a.startRun();
        else if (cmd == "stop") a.stopRun();
        else if (cmd == "begincase") { std::string why; const std::string id = a.beginPatient(&why);
                                       if (id.empty()) std::printf("begincase: %s\n", why.c_str()); }
        else if (cmd == "commit") { std::string why; if (!a.commitPatient(&why)) std::printf("commit: %s\n", why.c_str()); }
        else if (cmd == "endcase") a.endCase();
        else if (cmd == "sd") { int on; l >> on; sim::g_sd = on; }
        else if (cmd == "usb") { int on; l >> on; sim::g_usb = on; }
        else if (cmd == "intmem") { int st; l >> st; sim::g_intmem = st; }
        else std::fprintf(stderr, "line %d: unknown command %s\n", lineNo, cmd.c_str());
    }
    std::printf("voice tracks played:");
    for (int t : sim::g_voice) std::printf(" %d", t);
    std::printf("\n");
    return 0;
}
