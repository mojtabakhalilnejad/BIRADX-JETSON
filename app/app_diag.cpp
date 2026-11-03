// app/app_diag.cpp — diagnostic mode (2.19.0) and the health log (2.20.0).
//
// Counterparts of Controller::startDiagnosticMode / stopDiagnosticMode /
// resumeDiagnosticMode / diagWrite / uiAction and Controller::healthTick /
// checkPreviousSession / shutdownClean. What differs is only what a
// microcontroller can report instead of what Linux reports: the heap instead
// of /proc/meminfo, the time spent drawing instead of the process's CPU, and
// the processor's own reset flags instead of the kernel's uptime — which say
// directly whether the supply was lost, the reset line pulled, or the program
// restarted itself after a fault.
#include "app.h"
#include "version.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace app {

namespace {
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...)
{
    char b[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
const int kHealthMs    = 10000;
const int kDiagFlushMs = 500;
const size_t kDiagBufMax = 4096;
const long kHealthRotate = 2L * 1024 * 1024;
const char* kHealthHeader =
    "time,uptime_s,heap_used_kb,heap_free_kb,frames_per_s,frame_ms,frame_max_ms,"
    "loop_max_ms,uart_errors,battery_pct,on_mains,sd,usb,running,diag\n";

std::string baseName(const std::string& p)
{
    const size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
} // namespace

// ============================================================================
// diagnostic mode

void App::diagFlush()
{
    if (m_diagBuf.empty()) return;
    if (!m_diagPath.empty() && plat::sdReady()) {
        std::ofstream f(m_diagPath, std::ios::app);
        if (f) f << m_diagBuf;
    }
    m_diagBuf.clear();
    m_diagFlushAt = 0;
}

void App::diagWrite(const std::string& area, const std::string& text)
{
    if (!m_diagOn || m_diagInside) return;
    m_diagInside = true;
    const int64_t ms = nowMs();
    const std::string now = localText(ms, "%H:%M:%S") + fmt(".%03d", int(((ms % 1000) + 1000) % 1000));
    if (m_diagBeats > 0) {
        m_diagBuf += fmt("%s  [link] [C] heartbeat x%d since %s\n", now.c_str(), m_diagBeats,
                         m_diagBeatsSince.c_str());
        m_diagBeats = 0;
    }
    std::string body;
    for (char ch : text) {
        if (ch == '\n') body += "\n                          ";
        else if (ch != '\r') body.push_back(ch);
    }
    m_diagBuf += now + "  [" + area + "] " + body + "\n";
    if (m_diagBuf.size() > kDiagBufMax) diagFlush();
    else if (!m_diagFlushAt) { m_diagFlushAt = plat::ticks() + kDiagFlushMs; if (!m_diagFlushAt) m_diagFlushAt = 1; }
    m_diagInside = false;
}

void App::uiAction(const std::string& what, const std::string& result)
{
    if (!m_diagOn) return;
    std::string r = result;
    // A press on a grey control is what "the button does nothing" looks like
    // from the inside: say what the screen was giving as the reason.
    if (r.find("disabled") != std::string::npos) {
        const std::string mb = measureBlocker(), cb = caseBlocker();
        if (!cb.empty()) r += " | case blocker: " + cb;
        if (!mb.empty()) r += " | measure blocker: " + mb;
        if (cb.empty() && mb.empty()) r += " | no blocker is set";
    }
    diagWrite("ui", r.empty() ? what : what + "  ->  " + r);
}

std::string App::startDiagnosticMode()
{
    if (m_diagOn)
        return "Diagnostic mode is already on, recording to " + baseName(m_diagPath) + ".";
    if (!plat::sdReady())
        return "Could not start diagnostic mode: there is no records memory (" + plat::sdStatus() +
               "). That is itself the finding \xE2\x80\x94 this instrument cannot write to its own "
               "records folder.";
    const std::string stamp = localText(nowMs(), "%Y-%m-%d_%H-%M-%S");
    const std::string path = m_records + "/biradx_diagmode_" +
                             (m_s.deviceId.empty() ? std::string("noid") : m_s.deviceId) + "_" +
                             stamp + ".txt";
    {
        std::ofstream f(path);
        if (!f)
            return "Could not start diagnostic mode: " + path + " cannot be written. That is "
                   "itself the finding \xE2\x80\x94 this instrument cannot write to its own records folder.";
        f << "DIAGNOSTIC MODE \xE2\x80\x94 started " << localText(nowMs(), "%Y-%m-%dT%H:%M:%S")
          << ". The state of the instrument at that moment follows, then every action until the "
             "mode is stopped.\n";
        for (const std::string& l : diagnosticsLines()) f << l << "\n";
        f << "\n=== every action from here on, with its result ===\n";
        if (!f.good())
            return "Could not start diagnostic mode: writing " + path + " failed (is the memory full?).";
    }
    m_diagPath = path;
    m_diagOn = true;
    m_diagBeats = 0;
    m_s.diagFile = path;
    std::string why;
    if (!persist(&why))
        diagWrite("diag", "the mode could not be remembered in the settings memory, so a restart "
                          "ends it: " + why);
    diagWrite("diag", "diagnostic mode ON");
    diagFlush();
    touch();
    return "Diagnostic mode is ON. Everything done on the instrument is being written to " +
           baseName(path) + " until you press Stop diagnostics.";
}

std::string App::stopDiagnosticMode()
{
    if (!m_diagOn) return "Diagnostic mode is not on.";
    diagWrite("diag", "diagnostic mode OFF \xE2\x80\x94 closing snapshot follows");
    diagFlush();
    const std::string path = m_diagPath;
    {
        std::ofstream f(path, std::ios::app);
        if (f) {
            f << "\n=== state when diagnostic mode was stopped, "
              << localText(nowMs(), "%Y-%m-%dT%H:%M:%S") << " ===\n\n";
            for (const std::string& l : diagnosticsLines()) f << l << "\n";
            f << "\n=== diagnostic mode ended ===\n";
        }
    }
    m_diagOn = false;                  // from here on, nothing is written to it
    m_diagPath.clear();
    m_diagBuf.clear();
    m_s.diagFile.clear();
    std::string why;
    persist(&why);
    touch();

    const std::string name = baseName(path);
    if (plat::usbReady()) {
        long bytes = 0;
        const bool ok = plat::copyFile(path, plat::usbRoot() + "/" + name, &bytes);
        plat::flushAll();
        if (ok) return "Diagnostic mode stopped. The file is on the USB stick as " + name +
                       " \xE2\x80\x94 send that file. A copy is in the records folder.";
        return "Diagnostic mode stopped. The file is " + name + " in the records folder; a USB "
               "stick was found but could not be written to.";
    }
    plat::flushAll();
    return "Diagnostic mode stopped. The file is " + name + " in the records folder. Plug in a "
           "USB stick and use All files to copy it off.";
}

// Back in the mode after a restart: a crash inside the mode is the one event
// the file most needs to show.
void App::resumeDiagnosticMode()
{
    if (m_s.diagFile.empty()) return;
    if (!plat::sdReady()) {
        event("diagnostic mode could not be resumed: there is no records memory");
        return;
    }
    {
        std::ofstream f(m_s.diagFile, std::ios::app);
        if (!f) {
            event("diagnostic mode could not be resumed: " + m_s.diagFile + " cannot be written");
            return;
        }
        f << "\n=== APPLICATION STARTED " << localText(nowMs(), "%Y-%m-%dT%H:%M:%S")
          << " \xE2\x80\x94 diagnostic mode continues; the state at start follows ===\n\n";
        for (const std::string& l : diagnosticsLines()) f << l << "\n";
    }
    m_diagPath = m_s.diagFile;
    m_diagOn = true;
    m_diagBeats = 0;
    diagWrite("diag", "diagnostic mode resumed after a restart (" + plat::resetCause() + ")");
    diagFlush();
    touch();
}

// ============================================================================
// the health log

std::string App::healthTail(int lines) const
{
    if (!plat::sdReady()) return std::string();
    std::ifstream f(m_records + "/health.csv");
    if (!f) return std::string();
    f.seekg(0, std::ios::end);
    const long size = long(f.tellg());
    if (size > 8192) f.seekg(size - 8192); else f.seekg(0);
    std::deque<std::string> all;
    std::string l;
    while (std::getline(f, l)) {
        if (l.empty()) continue;
        all.push_back(l);
        while (int(all.size()) > lines) all.pop_front();
    }
    std::string o;
    for (const std::string& x : all) { if (!o.empty()) o += "\n"; o += x; }
    return o;
}

void App::healthTick()
{
    plat::Health h;
    plat::healthTake(&h);
    if (!plat::sdReady()) return;
    const std::string path = m_records + "/health.csv";
    long sz = plat::fileSize(path);
    // On the internal memory the health log may take a sixteenth of it (two files of a thirty-second:
    // 256 kB on an 8 MB chip, 64 kB on a 2 MB one; H7-2.11.1)
    if (sz > (plat::recordsOnInternal() ? plat::recordsTotalKb() * 1024L / 32 : kHealthRotate)) {
        plat::removeFile(m_records + "/health.1.csv");
        std::rename(path.c_str(), (m_records + "/health.1.csv").c_str());
        sz = -1;
    }
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    if (sz <= 0) f << kHealthHeader;
    const double secs = kHealthMs / 1000.0;
    const std::string row = fmt("%s,%u,%u,%u,%.2f,%.1f,%u,%u,%u,%d,%d,%d,%d,%d,%d",
        localText(nowMs(), "%Y-%m-%dT%H:%M:%S").c_str(), unsigned(plat::ticks() / 1000),
        unsigned(h.heapUsedKb), unsigned(h.heapFreeKb), h.frames / secs,
        h.frames ? double(h.frameMsSum) / h.frames : 0.0, unsigned(h.frameMsMax),
        unsigned(h.loopMsMax), unsigned(plat::linkErrors()), plat::batteryPercent(),
        plat::onMains() ? 1 : 0, plat::sdReady() ? 1 : 0, plat::usbReady() ? 1 : 0,
        busy() ? 1 : 0, m_diagOn ? 1 : 0);
    f << row << "\n";
    f.close();
    if (m_diagOn) diagWrite("health", row);
    // Space, said before it runs out rather than as a failed write afterwards.
    if (plat::recordsOnInternal() && m_storageProblem.empty()) {
        const long fr = plat::recordsFreeKb();
        if (fr >= 0 && fr < 400)
            storageFailed(fmt("The internal memory is almost full: %ld kB left of %ld kB. Copy the "
                              "records to a USB stick (Logs \xE2\x86\x92 All files); a new reading "
                              "needs about 6 kB.", fr, plat::recordsTotalKb()));
    }
}

// Did the last session end properly? A marker file is written at start and
// removed when the unit is switched off with its power key. Found at start, it
// means the session did not shut down; the processor's reset flags then say
// what happened instead.
void App::checkPreviousSession()
{
    if (!plat::sdReady()) return;
    const std::string marker = m_records + "/.session_running";
    const std::string cause = plat::resetCause();
    if (plat::fileExists(marker)) {
        std::string last;
        std::istringstream in(healthTail(6));
        std::string l;
        while (std::getline(in, l))
            if (!l.empty() && l[0] != '#' && l.compare(0, 5, "time,") != 0) last = l.substr(0, l.find(','));
        const std::string when = last.empty() ? std::string("unknown") : last;
        if (cause.find("power") != std::string::npos || cause.find("brown-out") != std::string::npos)
            m_lastSessionEnd = "The previous session ended when the board lost power: the last health "
                               "line was " + when + ", and this start was a " + cause + ". If nobody "
                               "switched it off without the power key at that time, the supply "
                               "dropped. The health lines just before it are in records/health.csv.";
        else
            m_lastSessionEnd = "The previous session did not shut down (last health line " + when +
                               "): the processor restarted \xE2\x80\x94 " + cause + ".";
        const bool fresh = plat::fileSize(m_records + "/health.csv") <= 0;
        std::ofstream f(m_records + "/health.csv", std::ios::app);
        if (f && fresh) f << kHealthHeader;
        if (f) f << "# " << localText(nowMs(), "%Y-%m-%dT%H:%M:%S") << " " << m_lastSessionEnd << "\n";
        event("previous session: " + m_lastSessionEnd);
    }
    std::ofstream f(marker);
    if (f) f << localText(nowMs(), "%Y-%m-%dT%H:%M:%S") << " " << BIRADX_VERSION << "\n";
}

void App::shutdownClean()
{
    event("switched off with the power key");
    healthTick();
    diagFlush();
    if (plat::sdReady()) plat::removeFile(m_records + "/.session_running");
    plat::flushAll();
}

} // namespace app
