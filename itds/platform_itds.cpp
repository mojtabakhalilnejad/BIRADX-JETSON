// itds/platform_itds.cpp — plat:: (app/platform.h) on the ITDS computer: Linux on the Jetson.
// See platform_itds.h for what lives where.
#include "platform_itds.h"
#include "../app/platform.h"
#include "../birxcore/patient.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace itds {

// ============================================================================
// small helpers

namespace {

using Clk = std::chrono::steady_clock;
const Clk::time_point g_t0 = Clk::now();

int64_t monoMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clk::now() - g_t0).count();
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string unquote(std::string v)
{
    v = trim(v);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

// A plain INI file: [section] then key=value. Keys are returned as "section/key".
std::map<std::string, std::string> readIni(const std::string& path)
{
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    std::string line, sec;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            const size_t e = line.find(']');
            sec = e == std::string::npos ? std::string() : trim(line.substr(1, e - 1));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[sec + "/" + trim(line.substr(0, eq))] = unquote(line.substr(eq + 1));
    }
    return out;
}

std::string fmtKb(long kb)
{
    char b[32];
    if (kb >= 1024L * 1024L) std::snprintf(b, sizeof b, "%.1f GB", kb / 1048576.0);
    else if (kb >= 1024) std::snprintf(b, sizeof b, "%.0f MB", kb / 1024.0);
    else std::snprintf(b, sizeof b, "%ld kB", kb);
    return b;
}

bool spaceKb(const std::string& path, long* freeKb, long* totalKb)
{
    struct statvfs v;
    if (statvfs(path.c_str(), &v) != 0) return false;
    if (freeKb) *freeKb = long((unsigned long long)v.f_bavail * v.f_frsize / 1024ULL);
    if (totalKb) *totalKb = long((unsigned long long)v.f_blocks * v.f_frsize / 1024ULL);
    return true;
}

bool writable(const std::string& dir) { return access(dir.c_str(), W_OK | X_OK) == 0; }

// Runs a program and waits for it (no shell): its exit status, or -1.
int runWait(const std::vector<std::string>& args, int timeoutMs = 5000)
{
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    const int64_t until = monoMs() + timeoutMs;
    for (;;) {
        int st = 0;
        const pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (r < 0) return -1;
        if (monoMs() > until) { kill(pid, SIGKILL); waitpid(pid, &st, 0); return -1; }
        usleep(10000);
    }
}

std::string which(const std::string& prog)
{
    const char* p = std::getenv("PATH");
    std::string path = p ? p : "/usr/bin:/bin";
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        const std::string f = (dir.empty() ? std::string(".") : dir) + "/" + prog;
        if (access(f.c_str(), X_OK) == 0) return f;
    }
    return std::string();
}

Config g_cfg;
std::atomic<bool> g_nvFresh{false};

} // namespace

const Config& config() { return g_cfg; }
bool nvWasFresh() { return g_nvFresh.load(); }

int systemUtcOffsetMin()
{
    const time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return int(lt.tm_gmtoff / 60);
}

// ============================================================================
// itds.ini

static std::string findPrevious(const std::string& hint)
{
    std::vector<std::string> cands;
    if (!hint.empty()) cands.push_back(hint);
    const char* home = std::getenv("HOME");
    if (home) {
        cands.push_back(std::string(home) + "/Desktop/BIRADx_v1");
        cands.push_back(std::string(home) + "/BIRADx_v1");
    }
    for (const auto& d : cands)
        if (access((d + "/device.ini").c_str(), R_OK) == 0) return d;
    return std::string();
}

std::string loadConfig(const std::string& dataDir, Config* c)
{
    *c = Config();
    c->dataDir = dataDir;
    const std::string path = dataDir + "/itds.ini";
    std::string report;
    if (access(path.c_str(), R_OK) != 0) {
        // A first start: the previous ITDS software's port and audio device, when it is here.
        const std::string prev = findPrevious(std::string());
        std::string port = c->port, audio = c->audioDevice;
        int baud = c->baud;
        if (!prev.empty()) {
            const auto old = readIni(prev + "/device.ini");
            auto get = [&](const char* k) { auto it = old.find(k); return it == old.end() ? std::string() : it->second; };
            if (!get("serial/port").empty()) port = get("serial/port");
            if (!get("serial/baud").empty()) baud = std::atoi(get("serial/baud").c_str());
            if (!get("audio/device").empty()) audio = get("audio/device");
        }
        std::ofstream f(path);
        f << "# BIRADx-ITDS — this computer's own settings. The instrument's settings (device ID, rules,\n"
             "# passcode, protocol) are kept by the program in eeprom.bin and changed from its Settings.\n"
             "# Written with its defaults on the first start; edit, then restart the program.\n\n"
             "[serial]\n"
             "# the measurement unit: a name under /dev (ttyTHS1) or a full path\n"
             "port=" << port << "\n"
             "baud=" << (baud > 0 ? baud : 57600) << "\n"
             "# burst: each command as the ITDS software 2.22 sent it (the frame six times, 3 ms apart;\n"
             "#        Sweep just after a heartbeat, again at the next ones until the sweep begins)\n"
             "# single: one frame, as the display board sends it\n"
             "commands=burst\n\n"
             "[audio]\n"
             "# as aplay -D takes it: default, or plughw:<card>,<device> (aplay -l lists them)\n"
             "device=" << audio << "\n"
             "# an amixer control the Volume setting moves (PCM, Master, Speaker …); empty = not here\n"
             "mixer=\n"
             "# the twelve recordings: 01.wav … 12.wav (empty = the voice folder beside this file)\n"
             "voice_dir=\n\n"
             "[usb]\n"
             "# where the desktop mounts USB sticks\n"
             "search=/media,/run/media,/mnt\n\n"
             "[screen]\n"
             "# auto: the screens laid out for the monitor's shape — upright (taller than wide) the\n"
             "#       portrait layout, else the 1024 x 600 one; or portrait, or landscape\n"
             "layout=auto\n"
             "fullscreen=1\n"
             "# 0, 90, 180 or 270: when the monitor is mounted turned\n"
             "rotate=0\n"
             "cursor=0\n\n"
             "[import]\n"
             "# the previous ITDS software's folder: its device ID, passcode and last case id are taken\n"
             "# on the first start (the records stay where they are)\n"
             "from=" << prev << "\n";
        report = "itds.ini written with its defaults" +
                 (prev.empty() ? std::string(" (no previous ITDS software found)")
                               : " (port " + port + " and audio device " + audio + " taken from " + prev + "/device.ini)");
    }
    const auto ini = readIni(path);
    auto get = [&](const char* k, const std::string& def) { auto it = ini.find(k); return it == ini.end() ? def : it->second; };
    c->port = get("serial/port", c->port);
    c->baud = std::atoi(get("serial/baud", "57600").c_str());
    if (c->baud <= 0) c->baud = 57600;
    c->commands = get("serial/commands", c->commands);
    if (c->commands != "single") c->commands = "burst";
    c->audioDevice = get("audio/device", c->audioDevice);
    c->mixerControl = get("audio/mixer", "");
    c->voiceDir = get("audio/voice_dir", "");
    if (c->voiceDir.empty()) c->voiceDir = dataDir + "/voice";
    c->usbSearch = get("usb/search", c->usbSearch);
    c->fullscreen = get("screen/fullscreen", "1") != "0";
    c->rotate = std::atoi(get("screen/rotate", "0").c_str());
    if (c->rotate != 90 && c->rotate != 180 && c->rotate != 270) c->rotate = 0;
    c->cursor = get("screen/cursor", "0") == "1";
    c->layout = get("screen/layout", "auto");
    if (c->layout != "portrait" && c->layout != "landscape") c->layout = "auto";
    c->importFrom = get("import/from", "");
    g_cfg = *c;
    if (report.empty()) report = "itds.ini read";
    return report;
}

Imported importPrevious(const Config& c)
{
    Imported im;
    const std::string prev = findPrevious(c.importFrom);
    if (prev.empty()) return im;
    im.from = prev;
    const auto old = readIni(prev + "/device.ini");
    auto get = [&](const char* k) { auto it = old.find(k); return it == old.end() ? std::string() : it->second; };
    im.deviceId = get("birx/device_id");
    im.passcode = get("security/passcode");
    im.needleRev = get("birx/needle");
    // The last case id it issued: its own copy, in its records.
    birx::CaseIdGenerator ids;
    if (ids.load(prev + "/records/caseid_state.txt") && (im.deviceId.empty() || ids.deviceId() == im.deviceId)) {
        im.caseDay = ids.day();
        im.caseSeq = ids.seq();
        if (im.deviceId.empty()) im.deviceId = ids.deviceId();
    }
    return im;
}

// ============================================================================
// the link: the serial port, read by its own thread

namespace {

speed_t baudOf(int b)
{
    switch (b) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return B57600;
    }
}

struct Link {
    std::mutex m;                 // everything below
    std::mutex wm;                // one writer at a time on the port
    std::deque<char> rx;
    int fd = -1;
    std::string path;
    std::thread th;
    std::atomic<bool> quit{false};
    uint64_t rxBytes = 0, txBytes = 0;
    uint32_t errors = 0, opens = 0, reopensIdle = 0;
    std::string lastError;
    int64_t lastRxMs = -1, lastReopenMs = -100000, lastOpenTry = -100000, lastHolderCheck = -100000;
    std::string holders;          // other programs that have the port open
    std::string tail;             // the last bytes, for markers split across reads
    // [R] as the ITDS software 2.22 sent it
    bool pending = false;
    int attempts = 0;
    int64_t dueMs = 0, lastAskMs = 0, lastBeatMs = -1;
    bool beatNow = false;
    std::string shape;            // what the last request did, for the diagnostics
} L;

const int kBurstFrames = 6, kBurstGapMs = 3, kAsks = 5, kAskFallbackMs = 1300;

void closeLocked(const std::string& why)
{
    if (L.fd >= 0) { ::close(L.fd); L.fd = -1; }
    if (!why.empty()) L.lastError = why;
}

bool openLocked()
{
    L.lastOpenTry = monoMs();
    const int fd = ::open(L.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { L.lastError = std::string("cannot open ") + L.path + ": " + std::strerror(errno); return false; }
    struct termios t;
    if (tcgetattr(fd, &t) != 0) { L.lastError = std::string("not a serial port: ") + std::strerror(errno); ::close(fd); return false; }
    cfmakeraw(&t);
    t.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS | CSIZE);
    t.c_cflag |= CS8 | CLOCAL | CREAD;
    t.c_iflag &= ~(IXON | IXOFF | IXANY);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, baudOf(g_cfg.baud));
    cfsetospeed(&t, baudOf(g_cfg.baud));
    if (tcsetattr(fd, TCSANOW, &t) != 0) { L.lastError = std::string("cannot set 8N1: ") + std::strerror(errno); ::close(fd); return false; }
    tcflush(fd, TCIOFLUSH);
    L.fd = fd;
    ++L.opens;
    L.lastError.clear();
    L.lastRxMs = monoMs();
    return true;
}

// The other programs that have the port open — the previous ITDS software, a serial console.
// Two readers on one port take each other's bytes.
std::string portHolders(const std::string& path)
{
    std::string out;
    char real[4096];
    std::string want = path;
    if (realpath(path.c_str(), real)) want = real;
    DIR* d = opendir("/proc");
    if (!d) return out;
    const pid_t self = getpid();
    while (dirent* e = readdir(d)) {
        const pid_t pid = pid_t(std::atoi(e->d_name));
        if (pid <= 0 || pid == self) continue;
        const std::string fdDir = std::string("/proc/") + e->d_name + "/fd";
        DIR* fdd = opendir(fdDir.c_str());
        if (!fdd) continue;
        bool hit = false;
        while (dirent* f = readdir(fdd)) {
            char buf[4096];
            const ssize_t n = readlink((fdDir + "/" + f->d_name).c_str(), buf, sizeof buf - 1);
            if (n > 0) { buf[n] = 0; if (want == buf) { hit = true; break; } }
        }
        closedir(fdd);
        if (!hit) continue;
        std::ifstream c(std::string("/proc/") + e->d_name + "/comm");
        std::string name;
        std::getline(c, name);
        if (!out.empty()) out += ", ";
        out += name + " (pid " + e->d_name + ")";
    }
    closedir(d);
    return out;
}

void writeAll(const char* b, size_t n)
{
    // called with L.wm held, not L.m
    int fd;
    { std::lock_guard<std::mutex> g(L.m); fd = L.fd; }
    if (fd < 0) return;
    size_t done = 0;
    int spins = 0;
    while (done < n && spins < 200) {
        const ssize_t w = ::write(fd, b + done, n - done);
        if (w > 0) { done += size_t(w); continue; }
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) { usleep(1000); ++spins; continue; }
        std::lock_guard<std::mutex> g(L.m);
        ++L.errors;
        L.lastError = std::string("write failed: ") + std::strerror(errno);
        return;
    }
    std::lock_guard<std::mutex> g(L.m);
    L.txBytes += done;
}

// The frame six times, 3 ms apart, each its own write; `pad` alignment bytes before the first.
void sendBurst(char cmd, int pad)
{
    const char frame[3] = { '[', cmd, ']' };
    std::lock_guard<std::mutex> w(L.wm);
    for (int i = 0; i < kBurstFrames; ++i) {
        if (i == 0 && pad > 0) { const std::string dots(size_t(pad), '.'); writeAll(dots.data(), dots.size()); }
        writeAll(frame, 3);
        if (i + 1 < kBurstFrames) usleep(kBurstGapMs * 1000);
    }
}

void scanMarkers(const char* b, size_t n, int64_t now)
{
    // called with L.m held
    std::string s = L.tail;
    s.append(b, n);
    const bool beat = s.find("[C]") != std::string::npos;
    const bool begin = s.find("[B]") != std::string::npos;
    if (beat) { L.lastBeatMs = now; L.beatNow = true; }
    if (begin && L.pending) {
        L.pending = false;
        const int64_t dt = now - L.lastAskMs;
        char t[160];
        if (dt <= 300)
            std::snprintf(t, sizeof t, "; answered: [B] %lld ms after attempt %d", (long long)dt, L.attempts);
        else
            std::snprintf(t, sizeof t, "; a sweep began %lld ms after attempt %d (too late to be its answer: the pedal?)",
                          (long long)dt, L.attempts);
        L.shape += t;
    }
    L.tail = s.size() > 4 ? s.substr(s.size() - 4) : s;
}

void linkThread()
{
    for (;;) {
        if (L.quit.load()) break;
        int fd;
        {
            std::lock_guard<std::mutex> g(L.m);
            const int64_t now = monoMs();
            if (L.fd < 0 && now - L.lastOpenTry >= 2000) openLocked();
            if (now - L.lastHolderCheck >= 30000) { L.lastHolderCheck = now; L.holders = portHolders(L.path); }
            fd = L.fd;
        }
        if (fd < 0) { usleep(100000); continue; }
        struct pollfd p = { fd, POLLIN, 0 };
        const int r = ::poll(&p, 1, 5);
        if (r > 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            std::lock_guard<std::mutex> g(L.m);
            ++L.errors;
            closeLocked("the port reported an error or was removed; reopening");
            continue;
        }
        if (r > 0 && (p.revents & POLLIN)) {
            char buf[512];
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n > 0) {
                std::lock_guard<std::mutex> g(L.m);
                const int64_t now = monoMs();
                L.rx.insert(L.rx.end(), buf, buf + n);
                while (L.rx.size() > 65536) L.rx.pop_front();
                L.rxBytes += uint64_t(n);
                L.lastRxMs = now;
                scanMarkers(buf, size_t(n), now);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                std::lock_guard<std::mutex> g(L.m);
                ++L.errors;
                closeLocked(std::string("read failed: ") + std::strerror(errno));
                continue;
            }
        }
        // A sweep asked for: just after a heartbeat (the firmware has just gone into its 50 ms
        // wait and sends nothing), or when none has come for a while.
        bool send = false, giveUp = false;
        int pad = 0, attempt = 0;
        {
            std::lock_guard<std::mutex> g(L.m);
            const int64_t now = monoMs();
            if (L.pending && (L.beatNow || now >= L.dueMs)) {
                if (L.attempts >= kAsks) giveUp = true;
                else { send = true; pad = L.attempts == 0 ? 0 : 2; attempt = L.attempts + 1; }
            }
            L.beatNow = false;
            if (giveUp) {
                L.pending = false;
                L.shape += "; no answer to " + std::to_string(kAsks) + " attempts";
            }
            // The receive side of the Tegra UART can stop; only a reopen restarts it (ITDS 2.21).
            if (!L.pending && L.lastRxMs >= 0 && now - L.lastRxMs > 12000 && now - L.lastReopenMs > 30000) {
                L.lastReopenMs = now;
                ++L.reopensIdle;
                closeLocked("nothing received for 12 s, not even the heartbeat: port reopened");
                openLocked();
            }
        }
        if (send) {
            usleep(3000);
            sendBurst('R', pad);
            std::lock_guard<std::mutex> g(L.m);
            L.attempts = attempt;
            L.lastAskMs = monoMs();
            L.dueMs = L.lastAskMs + kAskFallbackMs;
            const bool afterBeat = L.lastBeatMs >= 0 && L.lastAskMs - L.lastBeatMs < 40;
            char t[200];
            std::snprintf(t, sizeof t, "%s[R] x%d attempt %d%s%s", attempt == 1 ? "" : "; ", kBurstFrames, attempt,
                          pad ? " (2 alignment bytes first)" : "", afterBeat ? " just after a heartbeat" : " (no heartbeat)");
            if (attempt == 1) L.shape = t; else L.shape += t;
        }
    }
}

} // namespace

// ============================================================================
// the voice: tracks 1..12 as WAV files, played with aplay

namespace {

struct Voice {
    std::mutex m;
    std::condition_variable cv;
    std::thread th;
    bool quit = false;
    int want = 0;             // the track asked for (0 none)
    bool repeat = false;
    uint64_t gen = 0;         // bumped by every play and stop
    pid_t child = -1;
    std::string aplay;
    std::string last;         // what happened last, for the diagnostics
    int volume = 20;
} V;

std::string trackPath(int t)
{
    char b[16];
    std::snprintf(b, sizeof b, "/%02d.wav", t);
    return g_cfg.voiceDir + b;
}

void voiceThread()
{
    std::unique_lock<std::mutex> lk(V.m);
    for (;;) {
        V.cv.wait(lk, [] { return V.quit || V.want > 0; });
        if (V.quit) break;
        const int track = V.want;
        const uint64_t gen = V.gen;
        const std::string file = trackPath(track);
        if (V.aplay.empty() || access(file.c_str(), R_OK) != 0) {
            V.last = V.aplay.empty() ? "aplay is not installed (alsa-utils): nothing can be played"
                                     : "track " + std::to_string(track) + ": no file " + file;
            V.want = 0;
            continue;
        }
        std::vector<std::string> args = { V.aplay, "-q" };
        if (!g_cfg.audioDevice.empty() && g_cfg.audioDevice != "default") { args.push_back("-D"); args.push_back(g_cfg.audioDevice); }
        args.push_back(file);
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid = -1;
        const int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        if (rc != 0) { V.last = std::string("aplay could not be started: ") + std::strerror(rc); V.want = 0; continue; }
        V.child = pid;
        V.last = "PLAYING track " + std::to_string(track) + (V.repeat ? " (repeating)" : "");
        lk.unlock();
        int st = 0;
        waitpid(pid, &st, 0);
        lk.lock();
        V.child = -1;
        const bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        if (V.gen != gen) continue;                 // stopped, or another track asked for
        if (!ok) {
            V.last = "aplay FAILED on track " + std::to_string(track) + " (device " + g_cfg.audioDevice +
                     "; aplay -l lists the devices)";
            V.want = 0;
            continue;
        }
        V.last = "played track " + std::to_string(track);
        if (!V.repeat) V.want = 0;                  // repeat: the same track again
    }
}

} // namespace

// ============================================================================
// health counters, the power-off hook

namespace {
std::mutex g_hm;
uint32_t g_frames = 0, g_frameSum = 0, g_frameMax = 0, g_loopMax = 0;
uint32_t g_touchEvents = 0, g_mouseEvents = 0, g_bounces = 0;
std::function<void()> g_powerOff;
bool g_started = false;
}

void noteFrame(uint32_t ms)
{
    std::lock_guard<std::mutex> g(g_hm);
    ++g_frames; g_frameSum += ms; g_frameMax = std::max(g_frameMax, ms);
}
void noteLoop(uint32_t ms) { std::lock_guard<std::mutex> g(g_hm); g_loopMax = std::max(g_loopMax, ms); }
void noteTouch(bool touchEvent, bool bounce)
{
    std::lock_guard<std::mutex> g(g_hm);
    if (bounce) ++g_bounces;
    else if (touchEvent) ++g_touchEvents;
    else ++g_mouseEvents;
}

void runPowerOffHook()
{
    if (g_powerOff) { auto f = g_powerOff; g_powerOff = nullptr; f(); }
}

// ============================================================================
// start / stop

void start(const Config& c)
{
    if (g_started) return;
    g_started = true;
    g_cfg = c;
    g_nvFresh = access((c.dataDir + "/eeprom.bin").c_str(), R_OK) != 0;
    plat::makeDirs(c.dataDir + "/records");
    L.path = c.port.find('/') == std::string::npos ? "/dev/" + c.port : c.port;
    {
        std::lock_guard<std::mutex> g(L.m);
        openLocked();
        L.holders = portHolders(L.path);
        L.lastHolderCheck = monoMs();
    }
    L.th = std::thread(linkThread);
    V.aplay = which("aplay");
    V.th = std::thread(voiceThread);
}

void stop()
{
    if (!g_started) return;
    L.quit = true;
    if (L.th.joinable()) L.th.join();
    { std::lock_guard<std::mutex> g(L.m); closeLocked(std::string()); }
    {
        std::lock_guard<std::mutex> g(V.m);
        V.quit = true;
        ++V.gen;
        if (V.child > 0) kill(V.child, SIGTERM);
    }
    V.cv.notify_all();
    if (V.th.joinable()) V.th.join();
    g_started = false;
}

} // namespace itds

// ============================================================================
// plat::

namespace plat {

using itds::g_cfg;

uint32_t ticks() { return uint32_t(itds::monoMs()); }

// ---- the clock: this computer's, plus the difference kept when it may not be set
namespace {
std::mutex g_cm;
bool g_offLoaded = false;
long long g_offsetS = 0;
std::string g_clockNote;

std::string offsetPath() { return g_cfg.dataDir + "/clock_offset.txt"; }
void loadOffset()
{
    if (g_offLoaded) return;
    g_offLoaded = true;
    std::ifstream f(offsetPath());
    long long v = 0;
    if (f >> v) g_offsetS = v;
}
void saveOffset()
{
    const std::string tmp = offsetPath() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << g_offsetS << "\n"; }
    std::rename(tmp.c_str(), offsetPath().c_str());
}
}

bool clockRead(Clock* c)
{
    std::lock_guard<std::mutex> g(g_cm);
    loadOffset();
    const time_t t = time(nullptr) + time_t(g_offsetS);
    struct tm lt;
    if (!localtime_r(&t, &lt)) return false;
    c->year = lt.tm_year + 1900; c->month = lt.tm_mon + 1; c->day = lt.tm_mday;
    c->hour = lt.tm_hour; c->min = lt.tm_min; c->sec = lt.tm_sec;
    return true;
}

bool clockWrite(const Clock& c)
{
    std::lock_guard<std::mutex> g(g_cm);
    loadOffset();
    struct tm lt;
    std::memset(&lt, 0, sizeof lt);
    lt.tm_year = c.year - 1900; lt.tm_mon = c.month - 1; lt.tm_mday = c.day;
    lt.tm_hour = c.hour; lt.tm_min = c.min; lt.tm_sec = c.sec;
    lt.tm_isdst = -1;
    const time_t want = mktime(&lt);
    if (want == time_t(-1)) return false;
    // This computer's own clock when the program may set it (as root, or sudo without a
    // password); otherwise the difference is kept and applied by this program alone.
    bool set = false;
    if (geteuid() == 0) {
        struct timeval tv = { want, 0 };
        set = settimeofday(&tv, nullptr) == 0;
    }
    if (!set) {
        const std::string at = "@" + std::to_string((long long)want);
        set = itds::runWait({ "sudo", "-n", "date", "-s", at }, 4000) == 0;
    }
    if (set) {
        itds::runWait({ "sudo", "-n", "hwclock", "-w" }, 4000);   // the battery clock too, where there is one
        g_offsetS = 0;
        g_clockNote = "this computer's clock was set";
    } else {
        g_offsetS = (long long)want - (long long)time(nullptr);
        g_clockNote = "kept as a difference of " + std::to_string(g_offsetS) +
                      " s from this computer's clock (the program may not set it)";
    }
    saveOffset();
    return true;
}

std::string touchStatus()
{
    std::lock_guard<std::mutex> g(itds::g_hm);
    return std::to_string(itds::g_touchEvents) + " touch events, " + std::to_string(itds::g_mouseEvents) +
           " mouse events, " + std::to_string(itds::g_bounces) + " lifts shorter than 50 ms ignored (Qt input)";
}

// ---- the measurement unit's power: not switchable from this computer
bool unitRestartStart() { return false; }
bool unitRestarting() { return false; }
bool unitRestartDone() { return false; }

// ---- the link
std::string linkPortStatus()
{
    using itds::L;
    std::lock_guard<std::mutex> g(L.m);
    std::string s = L.path + " " + std::to_string(g_cfg.baud) + " 8N1 " + (L.fd >= 0 ? "open" : "NOT OPEN");
    if (L.fd < 0 && !L.lastError.empty()) s += " (" + L.lastError + ")";
    s += "; commands: " + g_cfg.commands;
    s += "; received " + std::to_string((unsigned long long)L.rxBytes) + " bytes, sent " +
         std::to_string((unsigned long long)L.txBytes);
    if (L.lastBeatMs >= 0) s += "; last heartbeat " + std::to_string((itds::monoMs() - L.lastBeatMs) / 1000) + " s ago";
    if (L.reopensIdle) s += "; reopened " + std::to_string(L.reopensIdle) + "x after 12 s of silence";
    if (!L.shape.empty()) s += "; last Sweep request: " + L.shape;
    if (!L.holders.empty()) s += "; ALSO OPEN in " + L.holders + " \xE2\x80\x94 two programs on one port take each other's bytes";
    return s;
}

bool linkSend(const char* b, size_t n)
{
    using itds::L;
    {
        std::lock_guard<std::mutex> g(L.m);
        if (L.fd < 0) return false;
        // Sweep, as the ITDS software sent it: queued for the link thread (above).
        if (g_cfg.commands == "burst" && n == 3 && b[0] == '[' && b[1] == 'R' && b[2] == ']') {
            L.pending = true;
            L.attempts = 0;
            L.dueMs = itds::monoMs() + itds::kAskFallbackMs;
            L.shape = "waiting for a heartbeat";
            return true;
        }
    }
    if (g_cfg.commands == "burst" && n == 3 && b[0] == '[' && b[2] == ']') {
        itds::sendBurst(b[1], 0);          // [T] and the rest: at once, six times
        return true;
    }
    std::lock_guard<std::mutex> w(L.wm);
    itds::writeAll(b, n);
    return true;
}

size_t linkRead(char* buf, size_t max)
{
    using itds::L;
    std::lock_guard<std::mutex> g(L.m);
    size_t n = 0;
    while (n < max && !L.rx.empty()) { buf[n++] = L.rx.front(); L.rx.pop_front(); }
    return n;
}

uint32_t linkErrors() { std::lock_guard<std::mutex> g(itds::L.m); return itds::L.errors; }

void linkCancelPending()
{
    using itds::L;
    std::lock_guard<std::mutex> g(L.m);
    if (!L.pending) return;
    L.pending = false;
    L.shape += "; Stop pressed: not asked again";
}

// ---- the voice
void voicePlay(int track, bool repeat)
{
    using itds::V;
    if (track < 1 || track > 12) return;
    {
        std::lock_guard<std::mutex> g(V.m);
        ++V.gen;
        if (V.child > 0) kill(V.child, SIGTERM);
        V.want = track;
        V.repeat = repeat;
    }
    V.cv.notify_all();
}

void voiceStop()
{
    using itds::V;
    std::lock_guard<std::mutex> g(V.m);
    ++V.gen;
    V.want = 0;
    V.repeat = false;
    if (V.child > 0) kill(V.child, SIGTERM);
}

void voiceVolume(int v)
{
    using itds::V;
    v = std::max(5, std::min(30, v));
    { std::lock_guard<std::mutex> g(V.m); V.volume = v; }
    if (g_cfg.mixerControl.empty()) return;
    // 5..30, as the voice module takes it, onto 0..100 % of the mixer control
    const int pct = (v - 5) * 100 / 25;
    std::string card;
    const size_t colon = g_cfg.audioDevice.find(':');
    if (colon != std::string::npos) card = g_cfg.audioDevice.substr(colon + 1, g_cfg.audioDevice.find(',') - colon - 1);
    std::vector<std::string> args = { "amixer", "-q" };
    if (!card.empty()) { args.push_back("-c"); args.push_back(card); }
    args.push_back("sset"); args.push_back(g_cfg.mixerControl); args.push_back(std::to_string(pct) + "%");
    itds::runWait(args, 2000);
}

void voiceProbe() {}

std::string voiceStatus()
{
    using itds::V;
    int found = 0;
    for (int t = 1; t <= 12; ++t) if (access(itds::trackPath(t).c_str(), R_OK) == 0) ++found;
    std::lock_guard<std::mutex> g(V.m);
    std::string s = V.aplay.empty() ? std::string("aplay NOT installed (alsa-utils)")
                                    : "aplay on " + g_cfg.audioDevice;
    s += "; " + std::to_string(found) + " of 12 recordings in " + g_cfg.voiceDir;
    s += "; volume " + std::to_string(V.volume) +
         (g_cfg.mixerControl.empty() ? std::string(" (set on the system's mixer)") : " on mixer " + g_cfg.mixerControl);
    if (!V.last.empty()) s += "; " + V.last;
    return s;
}

// ---- the settings image: a 2 KB file, written whole and renamed into place
static std::string eepromPath() { return g_cfg.dataDir + "/eeprom.bin"; }
size_t nvSize() { return 2048; }
bool nvRead(uint32_t addr, void* dst, size_t n)
{
    if (addr + n > 2048) return false;
    std::vector<uint8_t> img(2048, 0xFF);
    std::ifstream f(eepromPath(), std::ios::binary);
    if (f) f.read(reinterpret_cast<char*>(img.data()), 2048);
    std::memcpy(dst, img.data() + addr, n);
    return true;
}
bool nvWrite(uint32_t addr, const void* src, size_t n)
{
    if (addr + n > 2048) return false;
    std::vector<uint8_t> img(2048, 0xFF);
    { std::ifstream f(eepromPath(), std::ios::binary); if (f) f.read(reinterpret_cast<char*>(img.data()), 2048); }
    std::memcpy(img.data() + addr, src, n);
    const std::string tmp = eepromPath() + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const bool ok = ::write(fd, img.data(), 2048) == 2048 && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok) return false;
    return std::rename(tmp.c_str(), eepromPath().c_str()) == 0;
}

// ---- the records: this computer's disk
std::string recordsRoot() { return g_cfg.dataDir + "/records"; }
bool sdReady()
{
    static int64_t at = -100000;
    static bool ok = false;
    const int64_t now = itds::monoMs();
    if (now - at > 2000) {
        at = now;
        ok = makeDirs(recordsRoot()) && itds::writable(recordsRoot());
    }
    return ok;
}
std::string sdStatus()
{
    long fr = -1, tot = -1;
    itds::spaceKb(recordsRoot(), &fr, &tot);
    if (!sdReady()) return "records folder " + recordsRoot() + " NOT writable";
    std::string s = "records on this computer, " + recordsRoot();
    if (fr >= 0) s += ", " + itds::fmtKb(fr) + " free of " + itds::fmtKb(tot);
    std::lock_guard<std::mutex> g(g_cm);
    if (!g_clockNote.empty()) s += "; clock: " + g_clockNote;
    return s;
}
bool recordsOnInternal() { return false; }
long recordsFreeKb() { long f = -1; itds::spaceKb(recordsRoot(), &f, nullptr); return f; }
long recordsTotalKb() { long t = -1; itds::spaceKb(recordsRoot(), nullptr, &t); return t; }
IntMem internalMemory() { return IntMem::Absent; }
std::string internalMemoryChip() { return "none (the records are on this computer's disk)"; }
int internalMemoryMb() { return 0; }
std::string internalMemoryStatus() { return "not on this instrument: the records are on this computer's disk"; }
bool internalMemoryPrepare(std::string* why) { if (why) *why = "this instrument keeps its records on the computer's disk"; return false; }
bool internalMemorySaveOld(std::string* result) { if (result) *result = "there is no internal memory chip on this instrument"; return false; }

// ---- USB sticks, as the desktop mounts them
namespace {
struct UsbMount { std::string dir, type, dev; };
std::mutex g_um;
int64_t g_usbAt = -100000;
UsbMount g_usb;

std::string unescapeMount(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
            o.push_back(char((s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0')));
            i += 3;
        } else o.push_back(s[i]);
    }
    return o;
}

const UsbMount& usbNow()
{
    std::lock_guard<std::mutex> g(g_um);
    const int64_t now = itds::monoMs();
    if (now - g_usbAt < 1000) return g_usb;
    g_usbAt = now;
    g_usb = UsbMount();
    std::vector<std::string> prefixes;
    {
        std::stringstream ss(g_cfg.usbSearch);
        std::string p;
        while (std::getline(ss, p, ',')) { p = itds::trim(p); if (!p.empty()) prefixes.push_back(p + "/"); }
    }
    std::ifstream f("/proc/mounts");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line);
        std::string dev, dir, type;
        if (!(l >> dev >> dir >> type)) continue;
        dir = unescapeMount(dir);
        bool under = false;
        for (const auto& p : prefixes) if (dir.compare(0, p.size(), p) == 0) under = true;
        if (!under) continue;
        static const char* kinds[] = { "vfat", "exfat", "ntfs", "ntfs3", "fuseblk", "msdos", "ext4", "ext3", "ext2", "hfsplus" };
        bool kind = false;
        for (const char* k : kinds) if (type == k) kind = true;
        if (!kind || dev.compare(0, 5, "/dev/") != 0) continue;
        if (!itds::writable(dir)) continue;
        g_usb.dir = dir; g_usb.type = type; g_usb.dev = dev;
        break;
    }
    return g_usb;
}
}

bool usbReady() { return !usbNow().dir.empty(); }
std::string usbRoot() { return usbNow().dir; }
std::string usbStatus()
{
    const UsbMount u = usbNow();
    if (u.dir.empty()) return "no stick (none mounted under " + g_cfg.usbSearch + ")";
    long fr = -1, tot = -1;
    itds::spaceKb(u.dir, &fr, &tot);
    return "mounted at " + u.dir + " (" + u.type + ")" + (fr >= 0 ? ", " + itds::fmtKb(fr) + " free" : std::string());
}

// ---- files
bool makeDirs(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && cur != "/") { if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false; }
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
    out.flush();
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
        if (stat((path + "/" + n).c_str(), &st) != 0) continue;
        items.push_back({ n, S_ISDIR(st.st_mode) });
    }
    closedir(d);
    std::sort(items.begin(), items.end());
    for (auto& it : items) f(it.first, it.second, it.second ? 0 : fileSize(path + "/" + it.first));
    return true;
}
bool flushAll() { ::sync(); return true; }
void cacheClean(const void*, size_t) {}

// ---- power, screen
int  batteryPercent() { return -1; }
bool onMains() { return true; }
void backlight(int) {}            // the monitor's brightness is its own
void powerService() {}

void healthTake(Health* h)
{
    *h = Health();
    {
        std::lock_guard<std::mutex> g(itds::g_hm);
        h->frames = int(itds::g_frames);
        h->frameMsSum = itds::g_frameSum;
        h->frameMsMax = itds::g_frameMax;
        h->loopMsMax = itds::g_loopMax;
        itds::g_frames = itds::g_frameSum = itds::g_frameMax = itds::g_loopMax = 0;
    }
    // "heap": this program's resident memory, and what the computer has available
    std::ifstream st("/proc/self/status");
    std::string line;
    while (std::getline(st, line))
        if (line.compare(0, 6, "VmRSS:") == 0) h->heapUsedKb = uint32_t(std::atol(line.c_str() + 6));
    std::ifstream mi("/proc/meminfo");
    while (std::getline(mi, line))
        if (line.compare(0, 13, "MemAvailable:") == 0) h->heapFreeKb = uint32_t(std::atol(line.c_str() + 13));
}

std::string resetCause()
{
    double up = -1;
    std::ifstream f("/proc/uptime");
    f >> up;
    char b[160];
    if (up >= 0 && up < 300)
        std::snprintf(b, sizeof b, "the computer started %.0f s ago (switched on, or restarted)", up);
    else if (up >= 0)
        std::snprintf(b, sizeof b, "the program was started; the computer has been up %d h %02d min",
                      int(up / 3600), int(up / 60) % 60);
    else
        std::snprintf(b, sizeof b, "the program was started");
    return b;
}

void setPowerOffHook(std::function<void()> f) { itds::g_powerOff = std::move(f); }

const char* buildDate()
{
    static char out[32] = { 0 };
    if (!out[0]) {
        static const char* mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const char* d = __DATE__;              // "Oct 29 2026"
        int m = 1;
        for (int i = 0; i < 12; ++i) if (std::strncmp(d, mon[i], 3) == 0) m = i + 1;
        std::snprintf(out, sizeof out, "%.4s-%02d-%02d", d + 7, m, std::atoi(d + 4));
    }
    return out;
}

} // namespace plat
