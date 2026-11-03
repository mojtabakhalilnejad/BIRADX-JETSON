// itds/platform_itds.h — plat:: on the ITDS computer (the Jetson, Linux).
//
// The application, the decision core and the screens are the BIRADx-H7 source,
// unchanged apart from a few lines marked BIRADX_ITDS. What differs is here:
//
//   the link        the serial port (ttyTHS1, 57600 8N1), read by a thread of
//                   its own; [R] sent as the ITDS software sent it (below)
//   the voice       the twelve tracks of the voice module are WAV files in
//                   voice/01.wav … voice/12.wav, played with aplay
//   the store       records/ and the settings image (eeprom.bin) in the data
//                   folder, on this computer's disk; exports to a USB stick
//                   mounted by the desktop under /media
//   the clock       this computer's clock; when the program may not set it,
//                   the difference is kept (clock_offset.txt) and applied
//   the screen      itds/main_itds.cpp: a Qt window showing the 1024 x 600
//                   frame, scaled to the monitor, with its touch and mouse
//
// Settings that belong to this computer and not to the instrument — the port,
// the audio device, the screen — are in itds.ini in the data folder. It is
// written with its defaults on the first start; the serial port and the audio
// device are taken from the previous ITDS software's device.ini when it is
// found.
#ifndef BIRADX_PLATFORM_ITDS_H
#define BIRADX_PLATFORM_ITDS_H

#include <cstdint>
#include <string>
#include <vector>

namespace itds {

struct Config {
    std::string dataDir;                    // records/, eeprom.bin, itds.ini, voice/
    std::string port = "ttyTHS1";           // [serial] port: a name under /dev, or a full path
    int         baud = 57600;               // [serial] baud
    // [serial] commands: "burst" = each command as the ITDS software sent it (the frame six
    // times, 3 ms apart; a sweep asked for just after a heartbeat, again at the next ones until
    // it begins); "single" = one frame, as the display board sends it.
    std::string commands = "burst";
    std::string audioDevice = "default";    // [audio] device: as aplay -D takes it ("plughw:2,0")
    std::string mixerControl;               // [audio] mixer: an amixer control for the volume ("" = not set here)
    std::string voiceDir;                   // [audio] voice_dir: default <dataDir>/voice
    std::string usbSearch = "/media,/run/media,/mnt";   // [usb] search: where sticks are mounted
    bool        fullscreen = true;          // [screen] fullscreen
    int         rotate = 0;                 // [screen] rotate: 0, 90, 180, 270
    bool        cursor = false;             // [screen] cursor: show the mouse pointer
    std::string layout = "auto";            // [screen] layout: auto (by the monitor's shape), portrait, landscape
    std::string importFrom;                 // [import] from: the previous ITDS software's folder
};

// Reads <dataDir>/itds.ini, writing it with its defaults (and what was imported) when absent.
// Returns what was done, in one line, for the event log.
std::string loadConfig(const std::string& dataDir, Config* c);
const Config& config();

// Opens the port (and keeps reopening it), starts the voice player. Before App::begin().
void start(const Config& c);
void stop();

// The previous ITDS software's settings, for a first start: its device ID, the last case id it
// issued, its passcode and needle revision. Empty fields when absent.
struct Imported {
    std::string from, deviceId, passcode, needleRev;
    int caseDay = -1, caseSeq = 0;
};
Imported importPrevious(const Config& c);

// The settings image did not exist when this program started (a first start).
bool nvWasFresh();

// Counters for the health log, from the window.
void noteFrame(uint32_t ms);
void noteLoop(uint32_t ms);
void noteTouch(bool touchEvent, bool bounce);

// The power-off hook the application registered (App::shutdownClean), run once at exit.
void runPowerOffHook();

// The UTC offset of this computer's time zone, in minutes (for a first start).
int systemUtcOffsetMin();

} // namespace itds

#endif
