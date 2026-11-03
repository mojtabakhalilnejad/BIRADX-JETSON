// sim/sim_device.cpp — a model of the measurement unit (fw_main.c), for the
// simulator only. It answers [R] and [T] and the pedal the way the firmware
// does, and prints points with the firmware's own format string.
#include "sim.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

namespace sim {

namespace {
Load     g_load;
bool     g_heart = true, g_answers = true;
uint32_t g_nextBeat = 1000;
std::string g_cmd;
struct Out { uint32_t at; std::string s; };
std::deque<Out> g_queue;
uint32_t g_seed = 12345;

double rnd()   // deterministic, -1..1
{
    g_seed = g_seed * 1103515245u + 12345u;
    return ((g_seed >> 8) & 0xFFFF) / 32768.0 - 1.0;
}

const double kFreqs[15] = { 500000, 300000, 250000, 225000, 199000, 175000, 125000, 100000,
                            75000, 50000, 25000, 10000, 5000, 1000, 500 };

int g_sweeps = 0;    // sweeps since the load was set (the saline's leak)

// the electrode interface: |Z_e| = ze1k at 1 kHz, falling as f^-alpha, at a phase of -alpha x 90 degrees
std::complex<double> cpe(double f)
{
    const double a = g_load.zeAlpha;
    return std::polar(g_load.ze1k * std::pow(f / 1000.0, -a), -a * M_PI / 2);
}

std::complex<double> tissueZ(double f)
{
    const double w = 2 * M_PI * f;
    std::complex<double> z;
    if (g_load.kind == "air") {
        const double cp = (g_load.cpPf > 0 ? g_load.cpPf : 60) * 1e-12;
        const double gp = g_load.airGpS > 0 ? g_load.airGpS : 2e-8;
        z = 1.0 / std::complex<double>(gp, w * cp);
        return z;
    }
    if (g_load.kind == "saline") {
        // a needle in saline: the solution's resistance (z1k, set by the tip's geometry and the
        // strength; with a leak it falls sweep by sweep), the electrode interface in series, and
        // the needle's stray capacitance below
        const double r = g_load.z1k * std::pow(1.0 - g_load.leak, double(g_sweeps));
        if (g_load.ze1k < 0) {
            const double p = 0.03 * r * std::sqrt(2 * M_PI * 1000.0);
            z = std::complex<double>(r, 0) + p / std::sqrt(std::complex<double>(0, w));
        } else z = std::complex<double>(r, 0) + cpe(f);
    } else if (g_load.kind == "resistor") {
        z = std::complex<double>(g_load.z1k, 0);      // H7-2.15.0: a resistor across the electrodes: no tip surface
    } else if (g_load.kind == "wet") {
        z = std::complex<double>(800.0, -40.0);
    } else {
        const double z1k = g_load.kind == "fat" ? 4200.0 : g_load.z1k;
        const double u = std::log10(f);
        const double mag = z1k * std::pow(f / 1000.0, -0.04);
        double ph;
        if (u >= 4.69897) ph = -8.0 + g_load.ips * (u - 5.0);             // the IPS band
        else ph = -8.0 + g_load.ips * (4.69897 - 5.0) - 3.0 * (4.69897 - u); // below 50 kHz
        z = std::polar(mag, ph * M_PI / 180.0);
        if (g_load.ze1k > 0) z += cpe(f);   // H7-2.3.0: the tip's interface, in series
    }
    if (g_load.cpPf > 0) {
        const std::complex<double> y = 1.0 / z + std::complex<double>(0, w * g_load.cpPf * 1e-12);
        z = 1.0 / y;
    }
    return z;
}

int rangeFor(double mag)
{
    const double th[5] = { 500, 905, 1800, 2500, 4700 };
    int r = 1;
    for (double t : th) if (mag > t) ++r;
    return r;
}

void queue(uint32_t delay, const std::string& s)
{
    uint32_t at = g_ticks;
    if (!g_queue.empty() && g_queue.back().at > at) at = g_queue.back().at;
    at += delay;
    g_queue.push_back({ at, s });
}

void sweep()
{
    const bool air = g_load.kind == "air";
    const uint32_t per = air ? 400 : 100;
    queue(200, "[B]\r\n");
    uint32_t t = 0;
    for (double f : kFreqs) {
        t += per;
        std::complex<double> z = tissueZ(f);
        double mag = std::abs(z);
        double ph = std::arg(z) * 180.0 / M_PI + g_load.noiseDeg * rnd();
        if (mag > 9999999) mag = 9999999;
        char b[64];
        std::snprintf(b, sizeof b, "[imp%08d,%08d,%+07.02f,%d]\r\n",
                      int(f * 10.0), int(mag * 10.0), ph, rangeFor(mag));
        queue(per, b);
    }
    queue(20, "[IPS-05.00,Z00850.00,Low Risk]\r\n");
    queue(20, "[E]\r\n");
    ++g_sweeps;
    (void)t;
}
} // namespace

void device_setLoad(const Load& l) { g_load = l; g_sweeps = 0; }
void device_setAlive(bool heartbeat, bool answers) { g_heart = heartbeat; g_answers = answers; }
void device_pressPedal() { sweep(); }
// its power switched off (on=false) and on again: a fresh start answers,
// unless the fault is not in the unit (g_restartRepairs false: an open wire)
bool g_restartRepairs = true;
static bool s_wasHeart = true;
void device_power(bool on)
{
    if (!on) { s_wasHeart = g_heart; g_heart = false; g_answers = false; g_cmd.clear(); g_queue.clear(); return; }
    if (g_restartRepairs) { g_heart = true; g_answers = true; }
    else g_heart = s_wasHeart;
    g_nextBeat = g_ticks + 1500;           // it boots before its first heartbeat
}
void device_strayByte() { g_cmd += "?"; }          // one glitch byte: every frame after it is shifted

void device_rx(const std::string& bytes)
{
    if (!g_answers || g_load.kind == "dead") return;
    g_cmd += bytes;
    while (g_cmd.size() >= 3) {
        const std::string c = g_cmd.substr(0, 3);
        g_cmd.erase(0, 3);
        if (c == "[R]") sweep();
        else if (c == "[T]") queue(5, "TDP is connected\r\n");
    }
}

void device_tick()
{
    if (g_load.kind != "dead" && g_heart && g_ticks >= g_nextBeat) {
        g_nextBeat = g_ticks + 1000;
        if (g_queue.empty()) {                 // the firmware is blocked during a sweep
            const char hb[7] = { '[', 'C', ']', '\r', '\n', 0, 0 };
            for (char ch : hb) g_rx.push_back(ch);
        }
    }
    while (!g_queue.empty() && g_queue.front().at <= g_ticks) {
        for (char ch : g_queue.front().s) g_rx.push_back(ch);
        g_queue.pop_front();
    }
}

} // namespace sim
