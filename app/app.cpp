// app/app.cpp — see app.h. The order of the functions follows BirxBridge.cpp
// and controller.cpp of BIRADx 2.18.0 where there is a counterpart.
#include "app.h"
#include "version.h"
#include "../birxcore/calib.h"
#include "../birxcore/readiness.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

using namespace birx;

namespace app {

namespace {

const int kSilenceMs     = 6000;    // controller.cpp
const int kSweepWaitMs   = 30000;
const int kMaxUnitRestarts = 2;     // automatic power cycles of the measurement unit, per session   // after the [R], this long for the sweep to begin
const int kStaleMs       = 12000;
const int kLinkUpMs      = 5000;    // bytes within this window = the link is up
const int kCalStartMs    = 30000;   // CalibrateSheet.qml has 10 s; this unit may take longer to begin
const int kCalSweepMs    = 25000;
const int kProbeWaitMs   = 700;     // [T] is answered within ~50 ms
const int kDiscardWindowMs = 3000;  // BirxBridge::discardSweepInFlight
const size_t kEventsMax  = 200;
const size_t kTrafficMax = 200;

const char* kDash = "\xE2\x80\x94";      // —

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

// Days from the civil calendar (Howard Hinnant's algorithm).
int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

void civilFromDays(int64_t z, int* y, int* m, int* d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp + (mp < 10 ? 3 : -9);
    *y = int(yy + (mm <= 2)); *m = int(mm); *d = int(dd);
}

bool plausibleClock(const plat::Clock& c)
{
    return c.year >= 2000 && c.year <= 2099 && c.month >= 1 && c.month <= 12 &&
           c.day >= 1 && c.day <= 31 && c.hour >= 0 && c.hour <= 23 &&
           c.min >= 0 && c.min <= 59 && c.sec >= 0 && c.sec <= 59;
}













} // namespace

// ============================================================================
// construction

App::App()
    : m_calib(std::make_shared<RangeCalibrator>())
{
    m_engine.setCalibrator(m_calib);

    m_parser.onMarker = [this](Marker m) {
        switch (m) {
        case Marker::Begin:
            onRunStarted();
            m_current.clear();
            m_current.points.clear();
            m_inSweep = true;
            m_sweepT0 = m_pointT = plat::ticks();
            touch();
            break;
        case Marker::End:
        case Marker::Stopped:
            sweepSoundStop();               // before the answer's own sound
            if (m_inSweep) { m_inSweep = false; onSweepEnd(); }
            m_running = false;
            touch();
            break;
        default:
            break;
        }
    };
    m_parser.onPoint = [this](const Point& p) {
        // A point arriving without a [B] still belongs to a sweep (BirxBridge).
        if (!m_inSweep) {
            onRunStarted();
            m_current.clear();
            m_current.points.clear();
            m_inSweep = true;
            m_sweepT0 = plat::ticks();
        }
        m_current.points.push_back(p);
        m_pointT = plat::ticks();
        touch();
    };
    m_parser.onReading = [this](double ips, double z1k, const std::string& text) {
        m_current.fwIps = ips;
        m_current.fwZ1k = z1k;
        m_current.fwText = text;
    };
    m_parser.onUnparsed = [this](const std::string& raw) {
        ++m_unparsedTotal;
        m_unparsed.push_back(raw);
        while (m_unparsed.size() > 50) m_unparsed.pop_front();
    };
}

App::~App() {}

void App::begin()
{
    m_bootAt = plat::ticks();
    // --- clock first: every file name and case id depends on it
    plat::Clock c;
    m_clockOk = plat::clockRead(&c) && plausibleClock(c);
    m_lastClockRead = plat::ticks();

    // --- settings
    std::string why;
    if (!m_store.load(&m_s, &why)) {
        m_s = Settings();
        // A blank EEPROM is a new board: the defaults are written and that is
        // all. Anything else that fails to load is a damaged store, and that
        // is said on screen.
        bool blank = true;
        for (uint32_t addr = 0; addr < plat::nvSize() && blank; addr += 64) {
            uint8_t b[64];
            if (!plat::nvRead(addr, b, sizeof b)) { blank = false; break; }
            for (uint8_t x : b) if (x != 0xFF && x != 0x00) { blank = false; break; }
        }
        std::string w2;
        if (blank && m_store.save(m_s, &w2)) {
            event("settings memory was blank (a new board): the defaults were written");
        } else {
            m_settingsProblem = blank ? w2 : why;
            event("settings: " + m_settingsProblem);
        }
    }
    if (m_s.rulesReplaced) {
        m_s.rulesReplaced = false;
        std::string w3;
        if (m_store.save(m_s, &w3))
            event("rules: the stored table was brought up to date (H7-1.5.0 table; H7-1.7.0 sounds for rows that had none)");
        else
            event("rules: the H7-1.5.0 table is in force but could not be saved: " + w3);
    }
    if (m_clockOk) {
        m_baseLocal = c;
        const int64_t days = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day));
        m_baseEpoch = (days * 86400 + c.hour * 3600 + c.min * 60 + c.sec
                       - int64_t(m_s.utcOffsetMin) * 60) * 1000;
        m_baseTicks = plat::ticks();
        timerCheckStart(c);
    }

    openRecords();

    applySettingsToEngine();
    plat::voiceVolume(m_s.volume);
    plat::backlight(m_s.backlight);
    indexDataset();

    event(std::string(BIRADX_PRODUCT " ") + BIRADX_VERSION + " started (" + plat::resetCause() + "); " +
          (m_clockOk ? "clock " + localText(nowMs(), "%Y-%m-%d %H:%M:%S")
                     : std::string("clock NOT readable")));
    checkPreviousSession();
    resumeDiagnosticMode();
    m_healthAt = plat::ticks() + 2000;
    touch();
}

// The records: on the SD card when one is mounted, otherwise in the
// instrument's internal memory. Called at start, and again when the internal
// memory has just been prepared (Settings -> Device -> Internal memory).
void App::openRecords()
{
    // --- records
    m_records = plat::recordsRoot();
    if (plat::sdReady()) {
        plat::makeDirs(m_records);
        plat::makeDirs(m_records + "/sweeps");
        plat::makeDirs(m_records + "/calibration");
    } else {
        storageFailed(noStoreMessage());
    }
    m_dataset  = std::make_unique<DatasetWriter>(m_records + "/dataset.csv");
    m_outcomes = std::make_unique<OutcomeStore>(m_records + "/outcomes.csv");
    m_outcomes->load();
    m_calib->load(m_records + "/range_calibration.txt");
    m_needles = std::make_unique<NeedleRegistry>(m_records + "/needles_used.txt");
    m_needles->load();
    m_needleKeys.load(m_records + "/needle_keys.txt");
    loadLots();
    loadMaterials();

    // --- device id and case-id state: the settings memory is the authority,
    // the file on the card is the copy the analysis tools read. The later of
    // the two (day, seq) wins, so a card from an older session can never
    // make an id be issued twice.
    if (!m_s.deviceId.empty()) m_ids.setDeviceId(m_s.deviceId);
    {
        CaseIdGenerator fromFile;
        if (fromFile.load(m_records + "/caseid_state.txt") && fromFile.deviceId() == m_s.deviceId) {
            if (fromFile.day() > m_s.caseDay ||
                (fromFile.day() == m_s.caseDay && fromFile.seq() > m_s.caseSeq)) {
                m_s.caseDay = fromFile.day();
                m_s.caseSeq = fromFile.seq();
            }
        }
        m_ids.restore(m_s.caseDay, m_s.caseSeq);
    }
}

std::string App::noStoreMessage() const
{
#if defined(BIRADX_ITDS)
    return "Readings are NOT being recorded: the records folder cannot be written (" + plat::sdStatus() +
           "). Check the disk and the folder's permissions, then restart.";
#endif
    if (plat::internalMemory() == plat::IntMem::Foreign)
        return "Readings are NOT being recorded: there is no SD card, and the internal memory still "
               "holds the previous firmware's data. To record on the internal memory: Settings "
               "\xE2\x86\x92 Device \xE2\x86\x92 Internal memory.";
    return "Readings are NOT being recorded: no SD card is mounted (" + plat::sdStatus() +
           ") and the internal memory did not answer. Insert a card and restart.";
}

std::string App::prepareInternalMemory()
{
    std::string why;
    if (!plat::internalMemoryPrepare(&why)) {
        event("internal memory NOT prepared: " + why);
        return "The internal memory could NOT be prepared: " + why;
    }
    m_storageProblem.clear();
    m_indexedSize = -1;
    m_patientsSize = -2;
    m_logRows.clear();
    m_byCase.clear();
    openRecords();
    indexDataset();
    checkPreviousSession();
    event("internal memory prepared; records are now kept on it (" + plat::sdStatus() + ")");
    touch();
    return "The internal memory is ready and is now where the records are kept (" + plat::sdStatus() + ").";
}

// ============================================================================
// clock

int64_t App::nowMs() const
{
    if (!m_clockOk) return int64_t(plat::ticks());
    return m_baseEpoch + int64_t(uint32_t(plat::ticks() - m_baseTicks));
}

plat::Clock App::nowLocal() const
{
    const int64_t ms = nowMs() + int64_t(m_s.utcOffsetMin) * 60000;
    int64_t secs = ms / 1000;
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    plat::Clock c;
    civilFromDays(days, &c.year, &c.month, &c.day);
    c.hour = int(rem / 3600); c.min = int(rem / 60 % 60); c.sec = int(rem % 60);
    return c;
}

std::string App::localText(int64_t ms, const char* f) const
{
    const int64_t l = ms + int64_t(m_s.utcOffsetMin) * 60000;
    int64_t secs = l / 1000, days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    int y, m, d;
    civilFromDays(days, &y, &m, &d);
    std::string o;
    for (const char* p = f; *p; ++p) {
        if (*p != '%' || !p[1]) { o.push_back(*p); continue; }
        char b[8];
        switch (*++p) {
        case 'Y': std::snprintf(b, sizeof b, "%04d", y); break;
        case 'm': std::snprintf(b, sizeof b, "%02d", m); break;
        case 'd': std::snprintf(b, sizeof b, "%02d", d); break;
        case 'H': std::snprintf(b, sizeof b, "%02d", int(rem / 3600)); break;
        case 'M': std::snprintf(b, sizeof b, "%02d", int(rem / 60 % 60)); break;
        case 'S': std::snprintf(b, sizeof b, "%02d", int(rem % 60)); break;
        default: b[0] = *p; b[1] = 0; break;
        }
        o += b;
    }
    return o;
}

std::string App::isoUtc(int64_t ms) const
{
    int64_t secs = ms / 1000, days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    int y, m, d;
    civilFromDays(days, &y, &m, &d);
    return fmt("%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, int(rem / 3600),
               int(rem / 60 % 60), int(rem % 60));
}

std::string App::clockProblem() const
{
    if (!m_clockOk)
        return "The battery clock could not be read, or holds an impossible date. Case ids, "
               "file names and the log's time filters all depend on it. Set the date and "
               "time in Settings \xE2\x86\x92 Device.";
    // An instrument cannot be running a build from the future (§16.14).
    const char* bd = plat::buildDate();
    int by = 0, bm = 0, bdd = 0;
    if (std::sscanf(bd, "%d-%d-%d", &by, &bm, &bdd) == 3) {
        const plat::Clock n = nowLocal();
        const int64_t now = daysFromCivil(n.year, unsigned(n.month), unsigned(n.day));
        const int64_t built = daysFromCivil(by, unsigned(bm), unsigned(bdd));
        if (now < built)
            return fmt("The clock is WRONG: it says %04d-%02d-%02d, which is %d day(s) BEFORE "
                       "the build this instrument is running (%s). Case ids and every log "
                       "time filter are derived from it. Set the date in Settings \xE2\x86\x92 Device.",
                       n.year, n.month, n.day, int(built - now), bd);
    }
    return std::string();
}

// H7-2.7.2: the timer check. Every time the firmware measures (settling times, timeouts, the QC's minutes)
// counts the HAL tick; the battery clock keeps real time. Against it, the tick's rate is known after a
// minute: Settings -> About shows it, and the event log says it once when it is more than 3 % off.
void App::timerCheckStart(const plat::Clock& c)
{
    m_tcClockS = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day)) * 86400 + c.hour * 3600 + c.min * 60 + c.sec;
    m_tcTicks = plat::ticks();
    m_tcRatio = 0;
    m_tcSpanS = 0;
}

std::string App::timerCheck() const
{
    if (!m_clockOk) return "not measured (the battery clock cannot be read)";
    if (m_tcRatio > 0)
        return fmt("%.3f \xC3\x97 the battery clock, over %lld min%s", m_tcRatio, (long long)(m_tcSpanS / 60),
                   std::fabs(m_tcRatio - 1.0) > 0.03 ? " \xE2\x80\x94 WRONG" : " \xE2\x80\x94 correct");
    return "being measured (the first result after a minute)";
}

bool App::setClock(const plat::Clock& c, std::string* why)
{
    if (!plausibleClock(c)) {
        if (why) *why = fmt("%04d-%02d-%02d %02d:%02d is not a valid date and time.",
                            c.year, c.month, c.day, c.hour, c.min);
        return false;
    }
    if (!plat::clockWrite(c)) {
        if (why) *why = "The battery clock did not accept the new time (it was written "
                        "and read back different). Check the clock chip.";
        return false;
    }
    m_clockOk = true;
    m_baseLocal = c;
    const int64_t days = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day));
    m_baseEpoch = (days * 86400 + c.hour * 3600 + c.min * 60 + c.sec
                   - int64_t(m_s.utcOffsetMin) * 60) * 1000;
    m_baseTicks = plat::ticks();
    timerCheckStart(c);
    event(fmt("clock set to %04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day,
              c.hour, c.min, c.sec));
    touch();
    return true;
}

// ============================================================================
// the main loop

// Milliseconds from `then` to `now`, and ZERO when `then` is the later of the
// two. The loop reads the tick once at its top; a line taken from the link a
// moment later is stamped with a NEWER tick. Subtracting those as unsigned
// numbers gave four thousand million milliseconds, and the sweep watchdog
// closed a run that was arriving perfectly well (H7-1.2.3 and earlier, seen in
// the link monitor on the bench as "no [E] after 12 s" one millisecond after a
// point). On the PC the tick only moved between loops, so it never showed.
// The clock read again (once a minute, from poll): the tick's rate since the start. A clock set or
// jumping back, or more than 20 days, starts the measurement again.
void App::timerCheckUpdate(const plat::Clock& c)
{
    if (m_tcClockS >= 0) {
        const int64_t s = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day)) * 86400 + c.hour * 3600 +
                          c.min * 60 + c.sec - m_tcClockS;
        if (s >= 0 && s <= 20 * 86400) {
            if (s >= 60) {
                m_tcRatio = double(uint32_t(plat::ticks() - m_tcTicks)) / (double(s) * 1000.0);
                m_tcSpanS = s;
                if (!m_tcWarned && s >= 300 && std::fabs(m_tcRatio - 1.0) > 0.03) {
                    m_tcWarned = true;
                    event(fmt("timer check: the firmware's timer runs %.3f x the battery clock (over %lld min); every "
                              "time it measures is off by that factor", m_tcRatio, (long long)(s / 60)));
                }
            }
            return;
        }
    }
    timerCheckStart(c);
}

static inline uint32_t msSince(uint32_t now, uint32_t then)
{
    const int32_t d = int32_t(now - then);
    return d > 0 ? uint32_t(d) : 0u;
}

void App::poll()
{
    uint32_t now = plat::ticks();

    // --- diagnostic mode: what was written is on the card within half a second
    if (m_diagFlushAt && int32_t(now - m_diagFlushAt) >= 0) diagFlush();
    // --- the health log, every 10 s
    if (m_healthAt && int32_t(now - m_healthAt) >= 0) {
        m_healthAt = now + 10000;
        if (!m_healthAt) m_healthAt = 1;
        healthTick();
    }

    // --- bytes from the measurement unit, assembled into lines
    char buf[256];
    size_t n;
    while ((n = plat::linkRead(buf, sizeof buf)) > 0) {
        m_lastByte = plat::ticks();
        if (!m_everByte) { m_everByte = true; touch(); }
        for (size_t i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!m_lineBuf.empty()) { onLine(m_lineBuf); m_lineBuf.clear(); }
            } else if (c != '\0') {
                m_lineBuf.push_back(c);
                if (m_lineBuf.size() > 512) m_lineBuf.clear();
            }
        }
    }
    // Taking those lines took time, and they were stamped as they came: every
    // watchdog below must measure from a tick that is not older than they are.
    now = plat::ticks();

    // --- the clock is re-read once a minute so a drifting tick cannot run away
    if (m_clockOk && msSince(now, m_lastClockRead) > 60000) {
        m_lastClockRead = now;
        plat::Clock c;
        if (plat::clockRead(&c) && plausibleClock(c)) {
            timerCheckUpdate(c);
            const int64_t days = daysFromCivil(c.year, unsigned(c.month), unsigned(c.day));
            m_baseEpoch = (days * 86400 + c.hour * 3600 + c.min * 60 + c.sec
                           - int64_t(m_s.utcOffsetMin) * 60) * 1000;
            m_baseTicks = plat::ticks();
            m_baseLocal = c;
        }
    }

    // --- a request to sweep, waited for.
    //
    // ONE "[R]" is sent, as the previous display firmware of this instrument
    // did, and then nothing: no repeat, no probe, no extra byte. Seen on the
    // bench with H7-1.2.2: Sweep "did nothing", and a reading was on the screen
    // a little later — the sweep had come, after this board had stopped
    // waiting and gone on sending. So the wait is long (the unit may take its
    // time to begin), it counts on the screen, and whatever arrives late is
    // still taken.
    if (m_asks > 0) {
        if (m_running || m_inSweep) {
            m_asks = 0;
            touch();
        } else if (int32_t(now - m_askAt) >= 0) {
            const bool alive = m_everByte && msSince(now, m_lastByte) < 3000u;
            m_asks = 0;
            m_linkMessage = alive
                ? fmt("The measurement unit sends its heartbeat but did not start a sweep within %d s of "
                      "being asked. Settings \xE2\x86\x92 Device \xE2\x86\x92 Monitor shows what it "
                      "sends and when.", kSweepWaitMs / 1000)
                : std::string("No answer from the measurement unit, and nothing is arriving from it at "
                              "all. Check the cable between the two boards and its power.");
            event(m_linkMessage);
            unitRestart(alive ? "it did not start a sweep when asked" : "nothing arrives from it", true);
            touch();
        } else {
            static uint32_t lastSec = 0;
            if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }     // the count on the screen
        }
    }
    pollSync(now);
    pollUnitRestart(now);
    pollDelete();
    pollLots(now);
    pollDip(now);
    pollMat(now);
    if (m_waitBeat && int32_t(now - m_waitBeat) >= 0) { m_waitBeat = 0; touch(); }
    // --- silence after [R] (Controller::onSilence)
    if (m_silenceAt && int32_t(now - m_silenceAt) >= 0) {
        m_silenceAt = 0;
        if (!m_sawBytesThisRun) {
            m_linkMessage = fmt("Nothing received from the measurement unit for %d s after [R]. "
                                "Check the cable between the two boards, then the power of the "
                                "measurement unit.", kSilenceMs / 1000);
            noteTraffic("--  no reply after [R]");
            event(m_linkMessage);
            touch();
        }
    }
    // --- a stopped sweep whose end never came: give up waiting for it
    if (m_discard && msSince(now, m_lastLineInRun) > uint32_t(kStaleMs)) {
        m_discard = false;
        event("a stopped sweep never ended; no longer waiting for it");
        touch();
    }
    if (m_discardUntil && int32_t(now - m_discardUntil) > 0) m_discardUntil = 0;
    // --- a sweep that stopped arriving (Controller::onStaleRun)
    if ((m_running || m_inSweep) && msSince(now, m_lastLineInRun) > uint32_t(kStaleMs)) {
        const size_t got = m_current.points.size();
        m_running = false;
        m_inSweep = false;
        m_discard = false;
        m_linkMessage = fmt("The measurement unit stopped mid-sweep: %u of 15 points arrived "
                            "and then nothing for %d s. Nothing was recorded. Measure again.",
                            unsigned(got), kStaleMs / 1000);
        sweepSoundStop();
        noteTraffic(fmt("--  no [E] after %d s \xE2\x80\x94 closing the run", kStaleMs / 1000));
        event(m_linkMessage);
        if (m_calStage == CalStage::Waiting || m_calStage == CalStage::Sweeping) {
            m_calError = fmt("The sweep began but never finished, after %d seconds. Check "
                             "the cable, then try again.", kCalSweepMs / 1000);
            calStop();
        }
        if (m_refStage == RefStage::Waiting || m_refStage == RefStage::Sweeping) {
            m_refError = "The sweep began but never finished. Check the cable, then try again.";
            refStop();
        }
        if (m_dipStage == DipStage::Waiting || m_dipStage == DipStage::Sweeping) {
            m_dipError = "The sweep began but never finished. Check the cable, then try again.";
            dipStop();
        }
        touch();
    }
    // --- the calibration sheet's own watchdogs (CalibrateSheet.qml)
    if (m_calStage == CalStage::Waiting || m_calStage == CalStage::Sweeping) {
        const int limit = (m_calStage == CalStage::Sweeping) ? kCalSweepMs : kCalStartMs;
        if (msSince(now, m_calWaitStart) >= uint32_t(limit)) {
            m_calError = (m_calStage == CalStage::Sweeping)
                ? fmt("The sweep began but never finished, after %d seconds. Check the "
                      "cable, then try again.", kCalSweepMs / 1000)
                : fmt("No sweep started in %d seconds. The measurement unit is not "
                      "answering \xE2\x80\x94 check the cable, then try again.", kCalStartMs / 1000);
            event("calibration: " + m_calError);
            calStop();
        }
        static uint32_t lastSec = 0;
        if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
    }
    // --- and the healthy-tissue sheet's
    if (m_refStage == RefStage::Waiting || m_refStage == RefStage::Sweeping) {
        refWatch(msSince(now, m_refWaitStart));
        static uint32_t lastSec = 0;
        if (now / 1000 != lastSec) { lastSec = now / 1000; touch(); }
    }
    // --- a connection state change must redraw the blocker sentence
    static bool wasUp = false;
    const bool up = connected();
    if (up != wasUp) {
        wasUp = up;
        touch();
    }
}

// ============================================================================
// the link (Controller)

bool App::connected() const
{
    if (m_running || m_inSweep) return true;
    return m_everByte && uint32_t(plat::ticks() - m_lastByte) < uint32_t(kLinkUpMs);
}

uint32_t App::lastByteAgeMs() const
{
    return m_everByte ? uint32_t(plat::ticks() - m_lastByte) : 0xFFFFFFFFu;
}

void App::noteTraffic(const std::string& s)
{
    const uint32_t t = plat::ticks();
    m_traffic.push_front(fmt("%6u.%03u  ", unsigned(t / 1000), unsigned(t % 1000)) + s);
    while (m_traffic.size() > kTrafficMax) m_traffic.pop_back();
}

void App::event(const std::string& text)
{
    diagWrite("app", text);
    m_events.push_front(localText(nowMs(), "%Y-%m-%d %H:%M:%S  ") + text);
#ifdef BIRADX_BOARD
    // also to the debug header (USART6, 115200 8N1)
    std::printf("%s\r\n", m_events.front().c_str());
#endif
    while (m_events.size() > kEventsMax) m_events.pop_back();
}

void App::setToast(const std::string& t)
{
    m_toast = t;
    m_toastAt = plat::ticks();
    touch();
}

uint32_t App::toastAge() const { return uint32_t(plat::ticks() - m_toastAt); }

void App::sendFrame(const char* frame, const std::string& why)
{
    // One command is one three-byte frame, never a burst (Controller::sendFrame).
    if (std::strlen(frame) != 3) return;
    const bool sent = plat::linkSend(frame, 3);
    if (sent) ++m_txOk; else ++m_txFail;
    noteTraffic(std::string(sent ? "TX  " : "TX FAILED  ") + frame);
    event(why.empty() ? std::string("TX  ") + frame : std::string("TX  ") + frame + " \xE2\x80\x94 " + why);
}

// From the link monitor: one frame on the wire, with nothing else done or
// expected. Whatever comes back is handled as it always is.
void App::sendRaw(const char* frame)
{
    sendFrame(frame, "sent from the link monitor");
    touch();
}

int App::askingSeconds() const { return m_asks > 0 ? int(uint32_t(plat::ticks() - m_askedAt) / 1000u) : 0; }

void App::startRun()
{
    if (!connected() && !busy() && !plat::unitRestarting()) {
        // Sweep pressed and nothing is arriving from the measurement unit:
        // say so, and restart it (H7-1.6.0) rather than do nothing at all.
        m_linkMessage = "Nothing is arriving from the measurement unit.";
        unitRestart("Sweep was pressed and nothing arrives from it", true);
        touch();
        return;
    }
    if (!connected() || busy()) return;
    m_linkMessage.clear();
    m_sawBytesThisRun = false;
    m_silenceAt = plat::ticks() + kSilenceMs;
    if (!m_silenceAt) m_silenceAt = 1;
    askForSweep();
    touch();
}

void App::askForSweep()
{
    m_asks = 0;
    m_padGap = false;
    m_discardUntil = 0;       // a new request: what arrives next is wanted (acceptNextSweep)
    sendFrame("[R]", "waiting for the measurement unit");
    m_asks = 1;
    m_askedAt = plat::ticks();
    m_askAt = m_askedAt + kSweepWaitMs;
}

// Stop. On this instrument the sweep runs on the measurement unit, whose
// firmware has no stop command ([S] starts a 100-point linear sweep, spec
// §3.5 and §16.13). So Stop stops asking, and the rest of the sweep that is
// still arriving is thrown away: it is never decided, recorded or counted.
// As in 2.19.0, a sweep that BEGINS within 3 s of a Stop that found nothing
// running is the answer to the request that was stopped, and is thrown away
// too; later than that it is the pedal, and is kept.
void App::stopRun()
{
    const bool was = m_running || m_inSweep || m_asks > 0;
    const bool inFlight = m_running || m_inSweep;
#if defined(BIRADX_ITDS)
    plat::linkCancelPending();
#endif
    sweepSoundStop();
    m_padGap = false;
    m_asks = 0;
    m_silenceAt = 0;
    if (inFlight) { m_discard = true; m_discardPts = int(m_current.points.size()); m_lastLineInRun = plat::ticks(); m_discardUntil = 0; }
    else { m_discardUntil = plat::ticks() + kDiscardWindowMs; if (!m_discardUntil) m_discardUntil = 1; }
    m_running = false;
    m_inSweep = false;
    m_current.points.clear();
    if (was || inFlight) {
        event(inFlight ? "Stop: nothing sent (the firmware has no stop); the sweep in progress will be discarded"
                       : "Stop: nothing sent (the firmware has no stop); a sweep starting in the next 3 s will be discarded");
        noteTraffic("--  stop");
    }
    touch();
}

void App::onRunStarted()
{
    m_asks = 0;
    m_linkMessage.clear();          // a sweep began: the last diagnosis is out of date
    m_running = true;
    m_chartTissue = false;
    m_lastLineInRun = plat::ticks();
    sweepSoundStart();
    clearResult();
    if (m_calStage == CalStage::Waiting) {
        m_calStage = CalStage::Sweeping;
        m_calWaitStart = plat::ticks();
    }
    if (m_refStage == RefStage::Waiting) {
        m_refStage = RefStage::Sweeping;
        m_refWaitStart = plat::ticks();
    }
    lotSweepStarted();
    dipSweepStarted();
    matSweepStarted();
    event("Measurement started");
    touch();
}

void App::onLine(const std::string& line)
{
    noteTraffic("RX  " + line);
    const uint32_t now = plat::ticks();
    const bool beat = line.compare(0, 3, "[C]") == 0;
    if (beat) { ++m_heartbeats; if (m_waitBeat) { m_waitBeat = 0; touch(); } }
    if (m_diagOn) {
        if (beat) { if (m_diagBeats++ == 0) m_diagBeatsSince = localText(nowMs(), "%H:%M:%S"); }
        else diagWrite("link", "RX  " + line);
    }
    m_sawBytesThisRun = true;
    m_silenceAt = 0;
    if (line.find("TDP is connected") != std::string::npos) {
        // The firmware's reply to [T]: not a packet, and not a framing fault.
        m_sawTdp = true;
        event("The measurement unit answered the link probe.");
        return;
    }
    // A heartbeat is not part of a sweep: the unit sends none while it sweeps
    // (link monitor, H7-1.2.3 on the bench). So it does not keep a run open —
    // a unit that drops a sweep and goes back to its heartbeat would otherwise
    // leave the screen "measuring" for as long as it stayed switched on.
    if (!beat && (m_running || m_inSweep || m_discard)) m_lastLineInRun = now;

    const bool begin = line.find("[B]") != std::string::npos;
    const bool end   = line.find("[E]") != std::string::npos;
    const bool point = line.size() > 4 && line[0] == '[' &&
                       (line[1] == 'i' || line[1] == 'I') && (line[2] == 'm' || line[2] == 'M') &&
                       (line[3] == 'p' || line[3] == 'P');

    // A sweep beginning just after a Stop that found nothing running.
    if (m_discardUntil && !m_discard && !m_inSweep && (begin || point)) {
        if (int32_t(now - m_discardUntil) <= 0) {
            m_discard = true;
            m_discardPts = 0;
            m_lastLineInRun = now;
            noteTraffic("--  [B] of a stopped measurement: ignored");
            touch();
        }
        m_discardUntil = 0;
        if (m_discard) {
            if (point) ++m_discardPts;
            return;
        }
    }

    // Stop was pressed: swallow the rest of this sweep, up to and including
    // its [E]. A new [B] ends the discard.
    if (m_discard) {
        if (begin) { discardEnded(false); }
        else {
            if (point) ++m_discardPts;
            if (end) discardEnded(true);
            return;
        }
    }
    m_parser.feedLine(line);
}

void App::discardEnded(bool sawEnd)
{
    m_discard = false;
    if (sawEnd) { m_waitBeat = plat::ticks() + 3000; if (!m_waitBeat) m_waitBeat = 1; }
    event(fmt("sweep of %d points DISCARDED \xE2\x80\x94 its measurement had been stopped%s",
              m_discardPts, sawEnd ? "" : " (it never ended; a new one began)"));
    noteTraffic("--  [E] of a stopped measurement: discarded, not recorded");
    m_discardPts = 0;
    touch();
}

// ============================================================================
// THE MEASUREMENT UNIT RESTARTED (H7-1.6.0)
//
// Reported from the bench: sometimes, when the instrument has just been
// plugged in and switched on, Sweep gets nothing from the measurement unit;
// switching off and on with the power key mends it. That is done here by
// itself: the unit's supply is switched off for 1.5 s and on again
// (plat::unitRestartStart), and once its heartbeat is back the Sweep that was
// not answered is asked for again. Only on mains power (on the battery the
// same supply may be what keeps this board on), at most twice in a session,
// and never while anything is in flight. If it does not help, the screen says
// so and what to check.
bool App::unitRestart(const std::string& why, bool reask)
{
    if (plat::unitRestarting() || m_unitRestarts >= kMaxUnitRestarts) return false;
    if (!plat::onMains()) {
        m_linkMessage += " Switch the instrument off and on with its power key.";
        return false;
    }
    if (!plat::unitRestartStart()) return false;
    ++m_unitRestarts;
    m_unitReask = reask && !m_calibrating && m_refStage == RefStage::Idle;
    m_unitBackBy = 0;
    m_unitRestartAt = plat::ticks();
    m_linkMessage = "The measurement unit did not answer (" + why + "). It is being restarted \u2014 "
                    "its power switched off and on, as the power key does \u2014 and "
                    + std::string(m_unitReask ? "the Sweep will be asked for again by itself." : "then it can be asked again.");
    event("measurement unit RESTARTED: " + why + fmt(" (restart %d of %d this session)", m_unitRestarts, kMaxUnitRestarts));
    noteTraffic("--  measurement unit: power off and on (no answer)");
    touch();
    return true;
}

void App::pollUnitRestart(uint32_t now)
{
    // Nothing at all from the unit since this board started: on mains it is
    // restarted once, before anyone has to press Sweep to find out.
    if (!m_bootChecked && msSince(now, m_bootAt) > 12000u) {
        m_bootChecked = true;
        if (!m_everByte && !busy()) {
            m_linkMessage = "Nothing has arrived from the measurement unit since this instrument started.";
            unitRestart("nothing arrived from it after start", false);
        }
    }
    if (plat::unitRestartDone()) {
        m_unitBackBy = now + 8000;
        if (!m_unitBackBy) m_unitBackBy = 1;
        touch();
    }
    if (!m_unitBackBy) return;
    if (m_everByte && int32_t(m_lastByte - m_unitRestartAt) > 0 && msSince(now, m_lastByte) < 1500u) {
        // it is back
        m_unitBackBy = 0;
        event("measurement unit back after the restart: its heartbeat arrives");
        m_linkMessage.clear();
        if (m_unitReask && !busy()) {
            m_unitReask = false;
            event("the Sweep that was not answered is asked for again");
            startRun();
        }
        touch();
    } else if (int32_t(now - m_unitBackBy) >= 0) {
        m_unitBackBy = 0;
        m_unitReask = false;
        m_linkMessage = "The measurement unit was restarted but still sends nothing. Check the cable "
                        "between the two boards, then switch the instrument off and on.";
        event(m_linkMessage);
        touch();
    }
}

void App::startProbe()
{
    if (m_syncSt != SyncSt::Idle || m_running || m_inSweep || m_discard || m_asks > 0) return;
    m_probeBeats = m_heartbeats;
    m_probeResult.clear();
    beginSync(SyncWhy::Probe);
}

// THE COMMAND CHANNEL, CHECKED AND PUT BACK IN STEP.
//
// The measurement unit's firmware receives commands three bytes at a time
// (HAL_UART_Receive_IT(.., 3)) and accepts a frame only as '[' x ']'. One
// stray byte on its receive line — and this board's transmit pin floats
// while this processor is in reset or being programmed — shifts every frame
// after it by one, for good: "[R]" arrives as "R][" and is ignored, while the
// heartbeat goes on arriving. The Jetson's serial adapter never floats, so
// 2.20.0 does not meet this; a display board on the same cable does.
//
// It can be repaired from this side. [T] is answered at once and does nothing
// else. If it is not answered, one extra byte is sent and [T] tried again, and
// once more: one of the three alignments is the right one. If none is, a third
// extra byte restores the alignment that was found, and the unit is reported
// as not hearing (its receiver has stopped after a line error, which only a
// power cycle clears — or the wire is open).
void App::beginSync(SyncWhy why)
{
    m_syncWhy = why;
    m_syncPads = 0;
    m_sawTdp = false;
    sendFrame("[T]", "link probe");
    m_syncSt = SyncSt::WaitTdp;
    m_syncAt = plat::ticks() + kProbeWaitMs;
    touch();
}

void App::pollSync(uint32_t now)
{
    if (m_syncSt == SyncSt::Idle) return;
    if (m_syncSt == SyncSt::WaitTdp) {
        if (m_sawTdp) { finishSync(true); return; }
        if (int32_t(now - m_syncAt) < 0) return;
        const bool alive = m_everByte && msSince(now, m_lastByte) < 3000u;
        if (alive && m_syncPads < 2) {
            if (plat::linkSend(" ", 1)) ++m_txOk; else ++m_txFail;
            ++m_syncPads;
            noteTraffic("TX  (one byte, to re-align the three-byte command framing)");
            event(fmt("no reply to [T]: sent 1 extra byte to re-align the command framing (%d of 2)", m_syncPads));
            m_syncSt = SyncSt::PadGap;
            m_syncAt = now + 150;
        } else {
            if (m_syncPads == 2) {       // put the framing back where it was found
                plat::linkSend(" ", 1);
                noteTraffic("TX  (one byte, restoring the framing as found)");
            }
            finishSync(false);
        }
        return;
    }
    if (int32_t(now - m_syncAt) < 0) return;      // PadGap
    m_sawTdp = false;
    sendFrame("[T]", "link probe after re-aligning");
    m_syncSt = SyncSt::WaitTdp;
    m_syncAt = now + kProbeWaitMs;
}

void App::finishSync(bool heard)
{
    const SyncWhy why = m_syncWhy;
    const int pads = m_syncPads;
    const bool alive = m_everByte && uint32_t(plat::ticks() - m_lastByte) < 3000u;
    m_syncSt = SyncSt::Idle;
    m_cmdChannel = heard ? (pads ? "resynced" : "hears") : (alive ? "deaf" : "silent");
    const std::string deaf =
        "The measurement unit is sending \xE2\x80\x94 its heartbeat is arriving \xE2\x80\x94 but it does not "
        "answer commands, and re-aligning its command framing did not help. Its command receiver "
        "has stopped, which it does not recover from on its own: switch the instrument off and on. "
        "If it still does not answer after that, the wire from this board to it is open. Its "
        "front-panel button and pedal still work.";
    const std::string silent =
        "No answer from the measurement unit, and nothing is arriving from it at all. Check the "
        "cable between the two boards and the power of the measurement unit.";
    if (heard && pads)
        event(fmt("command framing RE-ALIGNED: %d stray byte(s) had shifted the measurement unit's "
                  "three-byte frames; it answers [T] again", 3 - pads));
    else if (heard) event("The measurement unit answered the link probe.");
    else event(std::string("command channel: ") + (alive ? deaf : silent));

    if (why == SyncWhy::Probe) {
        if (heard && pads)
            m_probeResult = fmt("ANSWERED after re-aligning \xE2\x80\x94 the measurement unit's three-byte "
                                "command framing was out of step by %d byte(s) (a stray byte on its "
                                "receive line, usually from this board restarting). It is corrected "
                                "and the command channel is working.", 3 - pads);
        else if (heard)
            m_probeResult = "ANSWERED \xE2\x80\x94 the measurement unit received [T] and replied. "
                            "Its command channel is working.";
        else if (alive)
            m_probeResult = fmt("NO REPLY, but %d heartbeat(s) arrived while waiting. ", m_heartbeats - m_probeBeats) + deaf;
        else
            m_probeResult = "NO REPLY and nothing arrived at all. " + silent;
    }
    touch();
}

// ============================================================================
// H7-2.11.0: the needle's tip in a case's sweep. Shorted: always; bridged: lower at 500 kHz than 0.65 x its
// saline check's R at 25 degrees C (tissue at 37 degrees conducts about 1.24 x as at 25; no tissue conducts
// more than normal saline), when the saline check was done in the 0.9 % saline.
std::string App::tipFaultOf(const std::vector<Point>& pts) const
{
    const double rRef = (m_dip.ok && m_dip.e.ok && m_dip.step == kCheck9) ? m_dip.e.rOhm : kNaN;   // a 0.9 % check only
    std::string why;
    matTipFault(pts, rRef, kTipBridgedCaseRatio, &why);
    return why;
}

// H7-2.15.0: a case's sweep that is not of a needle tip in tissue (matNotATip): no reading, nothing counted. The
// needle's checks stand: the needle is not at fault (a resistor on the bench, or a sweep not measured).
std::string App::notATipOf(const std::vector<Point>& pts) const
{
    const double xi = (m_dip.ok && m_dip.e.ok) ? std::fabs(m_dip.e.zi(1000.0).imag()) : kNaN;
    return matNotATip(pts, xi);
}

void App::caseNotATip(const std::string& why, const char* what)
{
    m_linkMessage = std::string("That sweep is not a ") + what + ": " + why + " Nothing is counted: place the needle and sweep "
                    "again.";
    event(std::string("sweep refused (") + what + "), " + why);
    m_current.points.clear();
    m_rawPts.clear();
    touch();
}

void App::caseTipFault(const std::string& why, const char* what)
{
    m_linkMessage = std::string("That sweep is not a ") + what + ": " + why +
                    " Clean the tip (rinse, wipe, dry), then check the needle again: Air check, then Saline check.";
    event(std::string("sweep refused (") + what + "), " + why);
    if (dipNeeded()) {                 // its checks no longer hold: done again before it measures
        m_cal = NeedleCalibration();
        m_calAtMs = 0;
        m_calByTraining = false;
        clearDip("its tip was shorted or bridged");
    }
    m_current.points.clear();
    m_rawPts.clear();
    touch();
}

// ============================================================================
// one reading, end to end (BirxBridge::onSweepEnd, spec §4.1)

void App::onSweepEnd()
{
    m_running = false;
    // As the previous display firmware did: after a sweep, no command until
    // the measurement unit's next heartbeat says it is idle again (3 s at most).
    m_waitBeat = plat::ticks() + 3000;
    if (!m_waitBeat) m_waitBeat = 1;
    m_current.epochMs = nowMs();
    ++m_sweepSeq;
    m_rawPts = m_current.points;        // as measured: what the material library compares

    // The needle calibration's own sweep. 2.18.0 also decided and recorded it
    // as a patient reading; the port records it only as a calibration attempt
    // and does not let the range calibrator learn from it (spec §20.6).
    if (m_calibrating && (m_calStage == CalStage::Sweeping || m_calStage == CalStage::Waiting)) {
        finishCalibrationSweep();
        touch();
        return;
    }
    // A lot test (Settings -> Lots): measured, kept with the lot, never a reading.
    if (m_lotTesting && (m_lotStage == LotStage::Sweeping || m_lotStage == LotStage::Waiting)) {
        finishLotSweep();
        touch();
        return;
    }
    // The saline check (H7-2.3.0): this needle's tip in saline, never a reading.
    if (m_matTesting && (m_matStage == MatStage::Sweeping || m_matStage == MatStage::Waiting)) {
        finishMatSweep();
        touch();
        return;
    }
    if (m_dipping && (m_dipStage == DipStage::Sweeping || m_dipStage == DipStage::Waiting)) {
        finishDipSweep();
        touch();
        return;
    }
    // The healthy-tissue sample: kept with the case, never a reading of the lesion.
    if (m_refTaking && (m_refStage == RefStage::Sweeping || m_refStage == RefStage::Waiting)) {
        finishTissueSweep();
        touch();
        return;
    }

    // H7-2.10.0: a needle that has not passed its QC gives no output, also when the pedal or the
    // front button asked for the sweep (which no button on this screen can stop)
    {
        const std::string qc = qcBlocker();
        if (!qc.empty()) {
            m_linkMessage = "That sweep is not a reading: " + qc + ". Check the needle first (Air check, then Saline check, "
                            "on the needle line above).";
            event("sweep refused by the QC: " + qc + " (nothing decided, shown or recorded as a reading)");
            m_current.points.clear();         // not even its curve is shown
            m_rawPts.clear();
            touch();
            return;
        }
    }
    // H7-2.14.0: a closed case takes no more readings, also from the pedal
    if (caseOpen() && m_caseResult.closed) {
        m_linkMessage = "That sweep is not a reading: the case is closed, its result is final (" + caseLabel() + "). End case, "
                        "then the next lesion.";
        event("sweep refused: the case is closed (" + caseLabel() + ")");
        m_current.points.clear();
        m_rawPts.clear();
        touch();
        return;
    }
    // H7-2.11.0: a shorted or bridged tip measures itself, not the tissue: no reading, and the needle's checks again
    {
        const std::string tw = tipFaultOf(m_current.points);
        if (!tw.empty()) { caseTipFault(tw, "reading"); return; }
    }
    // H7-2.15.0: nor is a sweep with no tip surface in it (a resistor), or one the measurement unit did not measure
    {
        const std::string nw = notATipOf(m_current.points);
        if (!nw.empty()) { caseNotATip(nw, "reading"); return; }
    }
    if (m_caseId.empty()) {
        m_caseId = "TEST-" + localText(m_current.epochMs, "%Y%m%d-%H%M");
        event("no case was started: this reading and the ones after it are recorded under " +
              m_caseId + " and cannot be matched to a pathology result");
    }
    m_current.caseId    = m_caseId;
    m_current.siteIndex = int(m_caseReadings.size()) + 1;
    m_current.needleRev = m_s.needleRev;

    // 2. the needle's own parasitic, on each point's own gain range
    if (!m_calibrating) correctForNeedle(&m_current);
    // 3. the range calibrator learns from every sweep
    m_calib->observe(m_current);
    // 4. decide
    m_last = m_engine.decide(m_current);
    m_haveLast = true;
    m_caseReadings.push_back(m_last);
    m_caseSpectra.push_back(m_current);

    // 5. record
    if (plat::sdReady() && m_dataset) {
        if (!m_dataset->append(m_current, m_last, &m_patient))
            storageFailed("The reading could NOT be recorded (dataset.csv) \xE2\x80\x94 check the records memory.");
        else addLogRow(m_current, m_last);
        if (!m_dataset->appendSweep(m_records + "/sweeps", m_current))
            storageFailed("The sweep of this reading could NOT be written to sweeps/ \xE2\x80\x94 check the records memory.");
        recordRawReading();             // H7-2.5.3: as measured, for tagging it later
        recordTip("reading", false, kNaN, kNaN, kNaN);   // H7-2.12.0: the tip's surface and age with it
    } else {
        storageFailed("This reading was NOT recorded: there is no records memory (no SD card, and the internal memory is not in use).");
    }

    // 6. the case
    m_caseResult = Aggregator(m_aggCfg).combine(m_caseReadings, m_engine.scoringModel(),
                                                &m_engine.ruleTable());
    publish();

    event(fmt("reading %d: tier=\"%s\" label=\"%s\"  case: tier=\"%s\" label=\"%s\"",
              int(m_caseReadings.size()), tierName(m_last.tier),
              (m_last.label.empty() ? tierName(m_last.tier) : m_last.label.c_str()),
              tierName(m_caseResult.tier), caseLabel().c_str()));

    // 8. the matched rule's sound, on this reading. H7-1.7.0: a Noisy reading
    // plays its row's sound too, so the operator hears that the needle must be
    // placed again (it is still not counted). A row with no sound is silent.
    playSound(m_last.sound);
    touch();
}

// H7-2.12.3
double App::sweepProgress() const
{
    if (!m_inSweep) return 0.0;
    const double n = double(InstrumentConstants::sweepFrequencies().size());
    const size_t got = m_current.points.size();
    double f = double(got);
    if (got > 0) {
        const double mean = double(uint32_t(m_pointT - m_sweepT0)) / double(got);
        if (mean > 0) f += std::min(0.9, double(uint32_t(plat::ticks() - m_pointT)) / mean);
    } else {
        f = std::min(0.9, double(uint32_t(plat::ticks() - m_sweepT0)) / 800.0);   // the first point: a guess
    }
    return std::max(0.0, std::min(1.0, f / (n > 0 ? n : 15.0)));
}

// Controller::publishFromBirx
void App::publish()
{
    const bool rules = m_engine.rulesDeciding();
    const double z = m_last.features.z1k;
    const double ips = rules ? m_last.features.ipsLegacy : m_last.features.ipsFit;
    m_z1kOhm = std::isfinite(z) ? z : kNaN;
    m_z1kText = ohmText(m_z1kOhm);
    // The IPS that was measured, whenever there is one (H7-1.4.0). 2.20.0 shows
    // it only for a reading that was answered; the charts showed it always, and
    // a tile that said nothing beside a chart that gave a number was asked
    // about three times from the bench.
    if (std::isfinite(ips)) m_ipsText = fmt("%.2f", ips);
    else m_ipsText = kDash;
}

void App::clearResult()
{
    m_z1kOhm = kNaN;
    m_z1kText = m_ipsText = kDash;
}

std::string App::ohmText(double ohm)
{
    if (!std::isfinite(ohm)) return kDash;
    const double a = std::fabs(ohm);
    if (a >= 1e6) return fmt("%.*f M", a >= 1e7 ? 1 : 2, ohm / 1e6);
    if (a >= 1e3) return fmt("%.*f k", a >= 1e4 ? 1 : 2, ohm / 1e3);
    return fmt("%.*f", a >= 100 ? 0 : 1, ohm);
}

std::string App::caseLabel() const
{
    return m_caseResult.label.empty() ? std::string(tierName(m_caseResult.tier))
                                      : m_caseResult.label;
}

void App::redecideOpenCase()
{
    if (m_caseSpectra.empty()) return;
    m_caseReadings.clear();
    for (const Spectrum& s : m_caseSpectra) m_caseReadings.push_back(m_engine.decide(s));
    m_last = m_caseReadings.back();
    m_caseResult = Aggregator(m_aggCfg).combine(m_caseReadings, m_engine.scoringModel(),
                                                &m_engine.ruleTable());
    publish();
    event(fmt("re-judged %d reading(s) of the open case under the current settings: %s",
              int(m_caseReadings.size()), tierName(m_caseResult.tier)));
    touch();
}

void App::storageFailed(const std::string& what)
{
    m_storageProblem = what;
    event("STORAGE: " + what);
    touch();
}

} // namespace app
