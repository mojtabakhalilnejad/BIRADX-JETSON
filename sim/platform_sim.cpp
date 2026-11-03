// sim/platform_sim.cpp — plat:: on a PC, for the simulator and the tests.
//
// Time is simulated (sim::advance), so a scripted session is reproducible to
// the millisecond. The measurement unit is simulated too (sim_device.cpp):
// [R] produces a real 15-point sweep of an RC phantom, sent through exactly
// the sprintf format the STM32 firmware uses.
#include "../app/platform.h"
#include "sim.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace sim {
uint32_t g_ticks = 0;
int g_tickEvery = 0;             // >0: time also moves while the program runs (see sim.h)
std::string g_root = "simroot";
bool g_sd = true, g_usb = true;
int g_intmem = 1;
int g_unitRestarts = 0;                // as on a board that ran the previous firmware
std::deque<char> g_rx;           // bytes from the measurement unit to us
std::string g_tx;                // bytes we sent
int64_t g_clockBase = 0;         // local seconds at g_ticks==0
bool g_clockOk = true;
std::vector<int> g_voice;        // tracks played
int g_voiceLoop = 0, g_voiceLoops = 0;   // the track repeating now (0 = none); how many times one was started
int g_backlight = 0;
std::string g_resetCause = "power-on (the supply was off)";

void advance(uint32_t ms) { for (uint32_t i = 0; i < ms; ++i) { ++g_ticks; device_tick(); } }
}

using namespace sim;

namespace plat {

uint32_t ticks()
{
    static int calls = 0;
    const uint32_t t = g_ticks;
    if (g_tickEvery > 0 && ++calls >= g_tickEvery) { calls = 0; ++g_ticks; }
    return t;
}

static int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

bool clockRead(Clock* c)
{
    if (!g_clockOk) return false;
    int64_t s = g_clockBase + g_ticks / 1000;
    int64_t days = s / 86400, rem = s % 86400;
    // civil from days
    int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp + (mp < 10 ? 3 : -9);
    c->year = int(yy + (mm <= 2)); c->month = int(mm); c->day = int(dd);
    c->hour = int(rem / 3600); c->min = int(rem / 60 % 60); c->sec = int(rem % 60);
    return true;
}

bool clockWrite(const Clock& c)
{
    const int64_t s = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day)) * 86400 +
                      c.hour * 3600 + c.min * 60 + c.sec;
    g_clockBase = s - g_ticks / 1000;
    g_clockOk = true;
    return true;
}

std::string touchStatus() { return "simulated"; }

// the measurement unit's power: 1.5 s off, then 3 s until the link is back (as on the board)
namespace { int s_unitSt = 0; uint32_t s_unitAt = 0; bool s_unitDone = false; }
static void unitService()
{
    if (!s_unitSt || int32_t(sim::g_ticks - s_unitAt) < 0) return;
    if (s_unitSt == 1) { sim::device_power(true); s_unitSt = 2; s_unitAt = sim::g_ticks + 3000; }
    else { s_unitSt = 0; s_unitDone = true; }
}
bool unitRestartStart()
{
    if (s_unitSt) return false;
    ++sim::g_unitRestarts;
    sim::device_power(false);
    s_unitSt = 1; s_unitAt = sim::g_ticks + 1500;
    return true;
}
bool unitRestarting() { unitService(); return s_unitSt != 0; }
bool unitRestartDone() { unitService(); const bool d = s_unitDone; s_unitDone = false; return d; }
std::string linkPortStatus() { return "last send OK; transmitter on; TX pin on USART1 (simulated)"; }
bool linkSend(const char* b, size_t n) { g_tx.append(b, n); device_rx(std::string(b, n)); return true; }
size_t linkRead(char* buf, size_t max)
{
    size_t n = 0;
    while (n < max && !g_rx.empty()) { buf[n++] = g_rx.front(); g_rx.pop_front(); }
    return n;
}
uint32_t linkErrors() { return 0; }

void voicePlay(int track, bool repeat) { if (repeat) { g_voiceLoop = track; ++g_voiceLoops; } else { g_voiceLoop = 0; g_voice.push_back(track); } }
void voiceStop() { g_voiceLoop = 0; }
void voiceVolume(int) {}
void voiceProbe() {}
std::string voiceStatus() { return "ANSWERS: not playing; 12 recordings in its own memory; volume 20; mode line reads 1 (simulated)"; }

static std::string eepromPath() { return g_root + "/eeprom.bin"; }
size_t nvSize() { return 2048; }
bool nvRead(uint32_t addr, void* dst, size_t n)
{
    std::vector<uint8_t> img(2048, 0xFF);
    std::ifstream f(eepromPath(), std::ios::binary);
    if (f) f.read(reinterpret_cast<char*>(img.data()), 2048);
    if (addr + n > 2048) return false;
    std::memcpy(dst, img.data() + addr, n);
    return true;
}
bool nvWrite(uint32_t addr, const void* src, size_t n)
{
    std::vector<uint8_t> img(2048, 0xFF);
    { std::ifstream f(eepromPath(), std::ios::binary); if (f) f.read(reinterpret_cast<char*>(img.data()), 2048); }
    if (addr + n > 2048) return false;
    std::memcpy(img.data() + addr, src, n);
    std::ofstream f(eepromPath(), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(img.data()), 2048);
    return f.good();
}

// The internal memory is simulated as a folder: 0 = no chip, 1 = holds the
// previous firmware's data, 2 = a BIRADx volume.
static bool onInternal() { return !g_sd && g_intmem == 2; }
std::string recordsRoot() { return onInternal() ? g_root + "/int/records" : g_root + "/sd/records"; }
bool sdReady() { return g_sd || g_intmem == 2; }
std::string sdStatus()
{
    if (g_sd) return "SD card: mounted (simulated), FAT32";
    if (g_intmem == 2) return "internal memory: 8092 kB free of 8192 kB";
    return "SD card NOT mounted: no card, or the card did not answer";
}
bool recordsOnInternal() { return onInternal(); }
long recordsFreeKb() { return onInternal() ? 8092 : g_sd ? 15000000 : -1; }
long recordsTotalKb() { return onInternal() ? 8192 : g_sd ? 15500000 : -1; }
IntMem internalMemory() { return g_intmem == 0 ? IntMem::Absent : g_intmem == 1 ? IntMem::Foreign : IntMem::Ready; }
std::string internalMemoryChip() { return "chip EF4017, status 00 02 (simulated)"; }
int internalMemoryMb() { return 8; }
std::string internalMemoryStatus()
{
    if (g_intmem == 0) return "no answer from the memory chip (id 000000)";
    if (g_intmem == 1) return "holds the previous firmware's data \xE2\x80\x94 not in use";
    return g_sd ? "8092 kB free of 8192 kB (not in use: the records are on the SD card)" : "8092 kB free of 8192 kB";
}
bool internalMemoryPrepare(std::string* why)
{
    if (g_intmem == 0) { if (why) *why = "the memory chip does not answer"; return false; }
    g_intmem = 2;
    mkdir((g_root + "/int").c_str(), 0755);
    return true;
}
bool internalMemorySaveOld(std::string* result)
{
    if (!g_usb) { if (result) *result = "no USB stick is mounted"; return false; }
    std::ofstream f(g_root + "/usb/old_internal_memory_w25q64.bin", std::ios::binary);
    f << "simulated";
    if (result) *result = "old_internal_memory_w25q64.bin (8 MB)";
    return true;
}
bool usbReady() { return g_usb; }
std::string usbRoot() { return g_root + "/usb"; }
std::string usbStatus() { return g_usb ? "mounted (simulated)" : "no stick"; }

bool makeDirs(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty()) { if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false; }
        }
        if (i < path.size()) cur.push_back(path[i]);
    }
    return true;
}
bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
long fileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? long(st.st_size) : -1; }
bool removeFile(const std::string& p) { return ::remove(p.c_str()) == 0; }
bool copyFile(const std::string& a, const std::string& b, long* bytes)
{
    std::ifstream in(a, std::ios::binary);
    std::ofstream out(b, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    if (bytes) *bytes = fileSize(a);
    return out.good();
}
bool listDir(const std::string& path, const std::function<void(const std::string&, bool, long)>& f)
{
    DIR* d = opendir(path.c_str());
    if (!d) return false;
    std::vector<std::pair<std::string, bool>> items;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        stat((path + "/" + n).c_str(), &st);
        items.push_back({ n, S_ISDIR(st.st_mode) });
    }
    closedir(d);
    for (auto& it : items) f(it.first, it.second, it.second ? 0 : fileSize(path + "/" + it.first));
    return true;
}
bool flushAll() { return true; }
void cacheClean(const void*, size_t) {}   // no cache between the CPU and its memory here

int  batteryPercent() { return 80; }
bool onMains() { return true; }
void backlight(int l) { g_backlight = l; }
void powerService() {}
void healthTake(Health* h) { *h = Health(); h->heapUsedKb = 900; h->heapFreeKb = 3200; h->frames = 12; h->frameMsSum = 180; h->frameMsMax = 22; h->loopMsMax = 25; }
std::string resetCause() { return sim::g_resetCause; }
static std::function<void()> s_powerOff;
void setPowerOffHook(std::function<void()> f) { s_powerOff = std::move(f); }
const char* buildDate() { return "2026-09-24"; }

} // namespace plat
