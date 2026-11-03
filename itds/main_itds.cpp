// itds/main_itds.cpp — BIRADx-H7 on the ITDS computer: the window, the input, the main loop.
//
// The same loop as board/board_main.cpp — poll the application, then draw a frame when the
// screens want one — with a Qt window in place of the LTDC panel and the GT911. The screens
// draw into an RGB565 frame — 1024 x 600 as on the display board, or, on a monitor standing
// upright, 600 wide and as tall as the monitor's shape makes it (ITDS-2.16.0; [screen] layout in
// itds.ini) — and the window shows it scaled to the monitor (kept in proportion, black bars where
// the shapes still differ) and maps the finger back into it.
//
//   biradx-itds [--windowed] [--data <folder>]
//
// The data folder (records/, eeprom.bin, itds.ini, voice/) is the folder the program is in,
// unless --data or BIRADX_ITDS_DATA says otherwise.
//
// BIRADX_ITDS_SCRIPT=<file> runs a scripted session with the real platform (the real port, the
// real files) — the simulator's commands wait, tap, press, release, drag, shot, id, print, quit —
// writing PNG screenshots to BIRADX_ITDS_SHOTS. For the tests in itds/tests.
#include "../app/app.h"
#include "../app/version.h"
#include "../gui/gui.h"
#include "../gui/splash.h"
#include "platform_itds.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRasterWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QTouchEvent>
#include <QTransform>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace {

int kW = 1024, kH = 600;                     // the frame: chooseFrame()
std::vector<uint16_t> g_fb(size_t(kW) * kH, 0);

// ITDS-2.16.0: the frame for this monitor. Landscape: the board's 1024 x 600. Upright (auto: the
// monitor, after [screen] rotate, taller than wide; or layout=portrait): 600 wide, and as tall as
// the monitor's own shape, 960 to 1100 (1024 on 600 x 1024, 1067 on 1080 x 1920) — the screens
// are laid out for whatever height they are given, so the picture fills the monitor.
std::string chooseFrame(const itds::Config& cfg, const QScreen* scr)
{
    int effW = 1024, effH = 600;
    if (scr) {
        const QSize sz = scr->geometry().size();
        const bool turned = cfg.rotate == 90 || cfg.rotate == 270;
        if (sz.width() > 0 && sz.height() > 0) { effW = turned ? sz.height() : sz.width(); effH = turned ? sz.width() : sz.height(); }
    }
    const bool tall = cfg.layout == "portrait" || (cfg.layout == "auto" && effH > effW);
    if (tall) {
        kW = 600;
        kH = effH > effW ? std::max(960, std::min(1100, int(600.0 * effH / effW + 0.5))) : 1024;
    } else {
        kW = 1024;
        kH = 600;
    }
    g_fb.assign(size_t(kW) * kH, 0);
    char b[160];
    std::snprintf(b, sizeof b, "screen %dx%d (rotate %d, layout %s): the screens drawn %s, %d x %d", effW, effH, cfg.rotate,
                  cfg.layout.c_str(), tall ? "upright" : "wide", kW, kH);
    return b;
}

// ---------------------------------------------------------------------------
// The window: the frame, scaled; the finger, mapped back.
class Screen : public QRasterWindow {
public:
    explicit Screen(int rotate) : m_rotate(rotate) {}

    // What the finger is doing, in frame coordinates. A lift counts once it has lasted 50 ms
    // (as on the board: a "no finger" in the middle of a press is not a release).
    bool rawDown = false;
    bool pressed = false;           // a press since the loop last looked: a click shorter than one pass still counts
    int  rawX = 0, rawY = 0;
    qint64 rawUpAt = 0;
    QElapsedTimer clock;

    void showFrame() { update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(QRect(0, 0, width(), height()), Qt::black);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(toWindow());
        const QImage img(reinterpret_cast<const uchar*>(g_fb.data()), kW, kH, kW * 2, QImage::Format_RGB16);
        p.drawImage(QRect(0, 0, kW, kH), img);
    }

    void touchEvent(QTouchEvent* e) override
    {
        const auto& pts = e->touchPoints();
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
            if (!pts.isEmpty()) press(pts.first().pos(), true);
            break;
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            lift();
            break;
        default: break;
        }
        e->accept();
    }
    // Ctrl+Q: back to the desktop (the instrument itself has no way out; a keyboard does).
    void keyPressEvent(QKeyEvent* e) override
    {
        if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Q) QCoreApplication::quit();
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->source() != Qt::MouseEventNotSynthesized) return;   // the touch already said it
        if (e->button() == Qt::LeftButton) press(e->localPos(), false);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (e->buttons() & Qt::LeftButton) press(e->localPos(), false);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (e->button() == Qt::LeftButton) lift();
    }

private:
    int m_rotate = 0;

    QTransform toWindow() const
    {
        const bool turned = m_rotate == 90 || m_rotate == 270;
        const double cw = turned ? kH : kW, ch = turned ? kW : kH;
        const double s = std::min(width() / cw, height() / ch);
        QTransform t;
        t.translate(width() / 2.0, height() / 2.0);
        t.rotate(m_rotate);
        t.scale(s, s);
        t.translate(-kW / 2.0, -kH / 2.0);
        return t;
    }
    void press(const QPointF& at, bool touch)
    {
        const QPointF f = toWindow().inverted().map(at);
        rawX = std::max(0, std::min(kW - 1, int(f.x())));
        rawY = std::max(0, std::min(kH - 1, int(f.y())));
        if (!rawDown) { itds::noteTouch(touch, false); pressed = true; }
        rawDown = true;
    }
    void lift()
    {
        if (!rawDown) return;
        rawDown = false;
        rawUpAt = clock.elapsed();
    }
};

// ---------------------------------------------------------------------------
// SIGTERM / SIGINT: quit properly (the session marker, the diagnostic file).
int g_sigPipe[2] = { -1, -1 };
void onSignal(int) { const char c = 1; if (::write(g_sigPipe[1], &c, 1)) {} }

std::string exeDir(const char* argv0)
{
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; return QFileInfo(QString::fromLocal8Bit(buf)).absolutePath().toStdString(); }
    return QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath().toStdString();
}

void savePng(const std::string& path)
{
    const QImage img(reinterpret_cast<const uchar*>(g_fb.data()), kW, kH, kW * 2, QImage::Format_RGB16);
    img.save(QString::fromStdString(path));
}

// ---------------------------------------------------------------------------
// A first start: the instrument's settings (eeprom.bin) seeded from the previous ITDS
// software — its device ID, the last case id it issued (so no id is issued twice), its
// passcode and needle revision — and the time zone from this computer's.
std::string seedFirstStart(const itds::Config& cfg)
{
    if (!itds::nvWasFresh()) return std::string();
    app::ConfigStore store;
    app::Settings s;
    std::string why;
    store.load(&s);                       // nothing there yet: the defaults
    std::vector<std::string> done;
    s.utcOffsetMin = itds::systemUtcOffsetMin();
    char tz[48];
    std::snprintf(tz, sizeof tz, "time zone UTC%+d:%02d", s.utcOffsetMin / 60, std::abs(s.utcOffsetMin % 60));
    done.push_back(tz);
    const itds::Imported im = itds::importPrevious(cfg);
    if (!im.from.empty()) {
        birx::CaseIdGenerator probe;
        if (!im.deviceId.empty() && probe.setDeviceId(im.deviceId)) {
            s.deviceId = im.deviceId;
            done.push_back("device ID " + im.deviceId);
            if (im.caseDay >= 0) {
                s.caseDay = im.caseDay;
                s.caseSeq = im.caseSeq;
                done.push_back("case ids continue after day " + std::to_string(im.caseDay) + " #" + std::to_string(im.caseSeq));
            }
        }
        if (im.passcode.size() == 4 && im.passcode.find_first_not_of("0123456789") == std::string::npos) {
            s.passcode = im.passcode;
            done.push_back("its passcode");
        }
        if (!im.needleRev.empty()) { s.needleRev = im.needleRev; done.push_back("needle revision " + im.needleRev); }
    }
    if (!store.save(s, &why)) return "first start: the settings could NOT be seeded: " + why;
    std::string out = "first start on this computer: ";
    for (size_t i = 0; i < done.size(); ++i) out += (i ? ", " : "") + done[i];
    if (!im.from.empty()) out += " (from the previous ITDS software in " + im.from + ")";
    return out;
}

// ---------------------------------------------------------------------------
// The scripted session (tests): the simulator's commands, in real time.
struct Script {
    std::vector<std::string> lines;
    size_t next = 0;
    qint64 waitUntil = 0;
    std::string shots;
    int phase = 0;              // tap: 0 none, 1 down, 2 up
    qint64 phaseAt = 0;
    std::vector<std::pair<int, int>> dragPts;
    bool done = false;
};

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--version") { std::printf("%s %s\n", BIRADX_PRODUCT, BIRADX_VERSION); return 0; }
    QGuiApplication qapp(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("biradx-itds"));

    bool windowed = false;
    std::string data = qEnvironmentVariableIsSet("BIRADX_ITDS_DATA")
                     ? qgetenv("BIRADX_ITDS_DATA").toStdString() : exeDir(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--windowed") windowed = true;
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
    }
    data = QDir(QString::fromStdString(data)).absolutePath().toStdString();
    QDir().mkpath(QString::fromStdString(data));

    // One program on the port at a time.
    const std::string lockPath = data + "/.biradx-itds.lock";
    const int lockFd = ::open(lockPath.c_str(), O_RDWR | O_CREAT, 0644);
    if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "biradx-itds: another copy is already running on %s\n", data.c_str());
        return 1;
    }

    itds::Config cfg;
    const std::string cfgReport = itds::loadConfig(data, &cfg);
    if (windowed) cfg.fullscreen = false;
    itds::start(cfg);
    const std::string seeded = seedFirstStart(cfg);

    // The window first, with the start screen on it, in the frame this monitor's shape asks for.
    const std::string frameReport = chooseFrame(cfg, QGuiApplication::primaryScreen());
    std::printf("biradx-itds: %s\n", frameReport.c_str());
    std::fflush(stdout);
    Screen win(cfg.rotate);
    win.clock.start();
    gfx::Canvas canvas(g_fb.data(), kW, kH);
    gui::drawSplash(canvas);
    win.setTitle(QStringLiteral("BIRADx"));
    if (!cfg.cursor && cfg.fullscreen) win.setCursor(Qt::BlankCursor);
    if (cfg.fullscreen) {
        // The whole screen, also where no window manager is running to make it so.
        if (QScreen* scr = QGuiApplication::primaryScreen()) win.setGeometry(scr->geometry());
        win.showFullScreen();
    }
    else { win.resize(cfg.rotate == 90 || cfg.rotate == 270 ? QSize(kH, kW) : QSize(kW, kH)); win.show(); }

    std::unique_ptr<app::App> a(new app::App());
    std::unique_ptr<gui::Gui> g;
    ui::Touch touch;
    bool begun = false;
    qint64 splashUntil = 0;

    // signals
    if (::pipe(g_sigPipe) == 0) {
        auto* sn = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &qapp);
        QObject::connect(sn, &QSocketNotifier::activated, &qapp, []() {
            char c; if (::read(g_sigPipe[0], &c, 1)) {}
            QCoreApplication::quit();
        });
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
        std::signal(SIGHUP, onSignal);
    }
    QObject::connect(&qapp, &QCoreApplication::aboutToQuit, [&]() {
        itds::runPowerOffHook();          // App::shutdownClean: the session ended properly
        itds::stop();
    });

    // scripted session (tests)
    Script sc;
    if (qEnvironmentVariableIsSet("BIRADX_ITDS_SCRIPT")) {
        std::ifstream f(qgetenv("BIRADX_ITDS_SCRIPT").toStdString());
        std::string l;
        while (std::getline(f, l)) sc.lines.push_back(l);
        sc.shots = qEnvironmentVariableIsSet("BIRADX_ITDS_SHOTS") ? qgetenv("BIRADX_ITDS_SHOTS").toStdString() : data;
    }
    const bool scripted = !sc.lines.empty();
    bool scriptTouch = false;
    int sx = 0, sy = 0;

    auto runScript = [&](qint64 now) {
        if (!scripted || sc.done || !g) return;
        if (sc.phase == 1 && now >= sc.phaseAt) { scriptTouch = false; sc.phase = 2; sc.phaseAt = now + 150; return; }
        if (sc.phase == 2) { if (now < sc.phaseAt) return; sc.phase = 0; }
        if (!sc.dragPts.empty()) {
            if (now < sc.phaseAt) return;
            sx = sc.dragPts.front().first; sy = sc.dragPts.front().second;
            scriptTouch = sc.dragPts.size() > 1;
            sc.dragPts.erase(sc.dragPts.begin());
            sc.phaseAt = now + 25;
            return;
        }
        if (now < sc.waitUntil) return;
        while (sc.next < sc.lines.size()) {
            std::istringstream l(sc.lines[sc.next++]);
            std::string cmd;
            if (!(l >> cmd) || cmd[0] == '#') continue;
            if (cmd == "wait") { int ms = 0; l >> ms; sc.waitUntil = now + ms; return; }
            if (cmd == "tap") { l >> sx >> sy; scriptTouch = true; sc.phase = 1; sc.phaseAt = now + 100; return; }
            if (cmd == "press") { l >> sx >> sy; scriptTouch = true; sc.waitUntil = now + 80; return; }
            if (cmd == "release") { scriptTouch = false; sc.waitUntil = now + 80; return; }
            if (cmd == "drag") {
                int x0, y0, x1, y1; l >> x0 >> y0 >> x1 >> y1;
                for (int k = 0; k <= 10; ++k) sc.dragPts.push_back({ x0 + (x1 - x0) * k / 10, y0 + (y1 - y0) * k / 10 });
                sc.dragPts.push_back({ x1, y1 });
                sc.phaseAt = now;
                return;
            }
            if (cmd == "shot") { std::string n; l >> n; g->frame(canvas, touch, plat::ticks()); savePng(sc.shots + "/" + n + ".png"); continue; }
            if (cmd == "id") { std::string id; l >> id; const std::string w = a->setDeviceId(id); if (!w.empty()) std::printf("id: %s\n", w.c_str()); continue; }
            if (cmd == "print") {
                std::printf("[%u] case %s | %s | link %s | %s\n", unsigned(plat::ticks()), a->caseId().c_str(),
                            a->caseLabel().c_str(), a->connected() ? "up" : "down", a->linkMessage().c_str());
                if (a->haveLast()) std::printf("      last reading: %s | %s\n", a->last().label.c_str(), a->last().reason.c_str());
                std::printf("      port: %s\n", plat::linkPortStatus().c_str());
                std::fflush(stdout);
                continue;
            }
            if (cmd == "sweep") { a->startRun(); continue; }
            if (cmd == "stop") { a->stopRun(); continue; }
            // a case, without the taps: for the end-to-end test of the records on this computer
            if (cmd == "begincase") { std::string why; const std::string id = a->beginPatient(&why);
                                      std::printf("begincase: %s %s\n", id.c_str(), why.c_str()); continue; }
            if (cmd == "intake") { std::string k, v; l >> k; std::getline(l, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1);
                                   a->setIntakeValue(k, v); continue; }
            if (cmd == "commit") { std::string why; const bool ok = a->commitPatient(&why);
                                   std::printf("commit: %d %s\n", ok ? 1 : 0, why.c_str()); continue; }
            if (cmd == "settle") { int t = 0; l >> t; a->setSettleSeconds(t); continue; }
            if (cmd == "cal") { a->calOpen(); a->calRun(); continue; }
            if (cmd == "dip") { double t = 25; l >> t; a->dipRun(t); continue; }
            if (cmd == "endcase") { a->endCase(); continue; }
            if (cmd == "export") { std::printf("export: %s\n", a->exportLogs("y1", "all").c_str()); std::fflush(stdout); continue; }
            if (cmd == "exportall") { std::printf("exportall: %s\n", a->exportAll().c_str()); std::fflush(stdout); continue; }
            if (cmd == "usb") { std::printf("usb: %s\n", plat::usbStatus().c_str()); std::fflush(stdout); continue; }
            if (cmd == "screen") {
                static const char* names[] = { "measure", "logs", "lock", "settings", "intake" };
                std::printf("screen: %s\n", names[int(g->screen())]);
                std::fflush(stdout);
                continue;
            }
            if (cmd == "geom") {
                std::printf("geom: window %dx%d at %d,%d; raw %s %d,%d\n", win.width(), win.height(), win.x(), win.y(),
                            win.rawDown ? "down" : "up", win.rawX, win.rawY);
                std::fflush(stdout);
                continue;
            }
            if (cmd == "voice") { std::printf("voice: %s\n", plat::voiceStatus().c_str()); std::fflush(stdout); continue; }
            if (cmd == "event") { std::string v; std::getline(l, v); a->event("script:" + v); continue; }
            if (cmd == "quit") { sc.done = true; QCoreApplication::quit(); return; }
            std::fprintf(stderr, "script: unknown command %s\n", cmd.c_str());
        }
        sc.done = true;
    };

    // The main loop, every 5 ms (board/board_main.cpp).
    QTimer loop;
    loop.setTimerType(Qt::PreciseTimer);
    qint64 lastLoop = win.clock.elapsed();
    QObject::connect(&loop, &QTimer::timeout, [&]() {
        const qint64 now = win.clock.elapsed();
        itds::noteLoop(uint32_t(now - lastLoop));
        lastLoop = now;
        if (!begun) {
            begun = true;
            a->begin();
            plat::setPowerOffHook([&a]() { a->shutdownClean(); });
            a->event("ITDS: " + cfgReport);
            a->event("ITDS: " + frameReport);
            if (!seeded.empty()) a->event("ITDS: " + seeded);
            a->event("ITDS: " + plat::linkPortStatus());
            a->event("ITDS: voice: " + plat::voiceStatus());
            splashUntil = now + (scripted ? 300 : gui::kSplashMs);
            return;
        }
        a->poll();
        if (!g) {
            if (now < splashUntil) return;
            g.reset(new gui::Gui(*a));
        }
        runScript(now);
        // the finger
        // A script's own taps while it makes them; the real finger (or mouse) otherwise.
        if (scripted && (scriptTouch || sc.phase != 0 || !sc.dragPts.empty())) {
            touch.down = scriptTouch;
            touch.x = sx; touch.y = sy;
        } else {
            if (win.rawDown || win.pressed) {
                win.pressed = false;
                touch.down = true;
                touch.x = win.rawX; touch.y = win.rawY;
            } else if (touch.down && now - win.rawUpAt >= 50) {
                touch.down = false;
            }
        }
        const uint32_t t = plat::ticks();
        if (g->wants(touch, t)) {
            QElapsedTimer ft; ft.start();
            g->frame(canvas, touch, t);
            win.showFrame();
            itds::noteFrame(uint32_t(ft.elapsed()));
        }
    });
    loop.start(5);

    const int rc = qapp.exec();
    g.reset();
    a.reset();
    ::close(lockFd);
    return rc;
}
