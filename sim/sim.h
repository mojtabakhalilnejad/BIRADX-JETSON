// sim/sim.h — the PC simulator's hooks.
#ifndef BIRADX_SIM_H
#define BIRADX_SIM_H
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace sim {
extern uint32_t g_ticks;
// On the board the tick is an interrupt: it moves WHILE the program runs, so
// two reads a few lines apart can differ. Here time only moved between loops,
// and a whole class of mistake could not happen. >0: every Nth read of the
// tick moves it on by a millisecond, in the middle of whatever is running.
extern int g_tickEvery;
extern std::string g_root;
extern bool g_sd, g_usb;
extern int g_intmem;                // 0 no chip, 1 the previous firmware's data, 2 a BIRADx volume
extern std::deque<char> g_rx;
extern std::string g_tx;
extern int64_t g_clockBase;
extern bool g_clockOk;
extern std::vector<int> g_voice;
extern int g_voiceLoop, g_voiceLoops;
extern std::string g_resetCause;

void advance(uint32_t ms);          // move simulated time, running the device model

// The simulated measurement unit.
struct Load {
    std::string kind = "lesion";    // lesion | air | saline | wet | fat | dead | resistor (H7-2.15.0: z1k, flat)
    double z1k = 850;               // lesion: |Z| at 1 kHz
    double ips = -5;                // lesion: IPS (deg/decade) it should read
    double cpPf = 0;                // stray needle capacitance added in parallel
    // H7-2.3.0: the tip's electrode interface, a constant-phase element in series
    // (saline: below 0 = the old model, 3 % of R at 1 kHz; lesion: below 0 = none)
    double ze1k = -1;               // |Z_e| at 1 kHz, ohm
    double zeAlpha = 0.5;
    double leak = 0;                // saline: R falls by this fraction with every sweep (an insulation leak)
    double airGpS = 2e-8;           // H7-2.15.0: air: the needle's leakage (its insulation), S
    double noiseDeg = 0.03;
};
void device_setLoad(const Load& l);
void device_setAlive(bool heartbeat, bool answers);   // heartbeat on/off, receiver working
void device_power(bool on);                             // its supply off / on again
extern bool g_restartRepairs;                           // false: a restart does not help (open wire)
extern int  g_unitRestarts;                             // how many times its power was switched
void device_pressPedal();                             // the front button: a sweep, unasked
void device_strayByte();                              // a glitch on its receive line
void device_rx(const std::string& bytes);
void device_tick();                                   // 1 ms
}
#endif
