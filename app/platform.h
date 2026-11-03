// app/platform.h — everything the application needs from the hardware.
//
// Two implementations exist: board/platform_board.cpp (STM32H743, FK743M1
// carrier) and sim/platform_sim.cpp (a PC, for the screenshots and the
// scripted tests). The application, the decision core and the screens never
// touch a HAL function directly, so all of them can be built and tested on a
// PC without the instrument.
#ifndef BIRADX_PLATFORM_H
#define BIRADX_PLATFORM_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace plat {

// ---- time ------------------------------------------------------------------
uint32_t ticks();                 // milliseconds since boot, wraps after 49 days

struct Clock {                    // the battery clock (DS1307), local time
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
};
bool clockRead(Clock* c);         // false: the clock chip did not answer
bool clockWrite(const Clock& c);  // written and read back

// ---- the link to the measurement unit (USART1, 57600 8N1) -------------------
bool   linkSend(const char* bytes, size_t n);   // false: the UART did not take them
size_t linkRead(char* buf, size_t max);      // non-blocking; returns bytes read
uint32_t linkErrors();                        // UART overrun/framing errors recovered
#if defined(BIRADX_ITDS)
// ITDS (itds/platform_itds.cpp): a Sweep the port is still asking for, at each heartbeat, is not
// asked for again — Stop was pressed.
void linkCancelPending();
#endif
std::string linkPortStatus();                 // the port's own state, for the diagnostics
std::string touchStatus();                    // the touch panel's reports and bounces since start
// The measurement unit's power, switched off and on again (H7-1.6.0): what
// the power key does, without switching this board off. Takes about 4.5 s; the
// link is down meanwhile. false when one is already under way.
bool unitRestartStart();
bool unitRestarting();
bool unitRestartDone();                       // true once, when one has finished

// ---- the voice module (DY, UART5) ------------------------------------------
void voicePlay(int track, bool repeat = false);   // 1..12, as stored on the module; repeat = until voiceStop()
void voiceStop();
void voiceVolume(int v);          // 5..30
void voiceProbe();                // ask the module for its state (blocks up to 0.3 s)
std::string voiceStatus();        // what it answered, or that it did not

// ---- non-volatile store (the 2 KB I2C EEPROM) -------------------------------
size_t nvSize();
bool   nvRead(uint32_t addr, void* dst, size_t n);
bool   nvWrite(uint32_t addr, const void* src, size_t n);   // written, then read back

// ---- files --------------------------------------------------------------------
// The records live on the SD card. Paths are ordinary strings; on the board
// the C library's file calls are routed to FatFs, so std::ofstream works there
// exactly as it does on the Jetson.
std::string recordsRoot();        // e.g. "0:/records" (board) or "sd/records" (sim)
bool   sdReady();                 // the records store (card or internal memory) is mounted
std::string sdStatus();           // one line for the diagnostics and the settings
// The records store is the SD card when one is mounted, otherwise the
// instrument's internal memory (a 2 to 16 MB serial flash, a littlefs volume) once it
// holds a BIRADx volume. sdReady() answers for whichever of the two is in use.
bool   recordsOnInternal();
long   recordsFreeKb();           // -1 when unknown
long   recordsTotalKb();
enum class IntMem { Absent, Foreign, Ready };  // no chip / holds something that is not a BIRADx volume / mounted
IntMem internalMemory();
std::string internalMemoryChip();             // the chip's identity, status and counters
int    internalMemoryMb();                     // its size (H7-2.11.1: 2, 4, 8 or 16; 0 no chip)
std::string internalMemoryStatus();             // one line, for the settings and the diagnostics
bool   internalMemoryPrepare(std::string* why); // ERASES the chip, makes it the BIRADx store
bool   internalMemorySaveOld(std::string* result);  // copies the chip as it is to the USB stick
bool   usbReady();                // a stick is mounted
std::string usbRoot();            // "1:" (board) or "usb" (sim)
std::string usbStatus();

bool   makeDirs(const std::string& path);
bool   fileExists(const std::string& path);
long   fileSize(const std::string& path);       // -1 when absent
bool   removeFile(const std::string& path);
bool   copyFile(const std::string& from, const std::string& to, long* bytes = nullptr);
// Calls f(name, isDir, size) for each entry of a directory (not recursive).
bool   listDir(const std::string& path,
               const std::function<void(const std::string&, bool, long)>& f);
bool   flushAll();                // everything written so far is on the medium
// H7-2.7.0: [p, p+n) written out of the processor's data cache to the memory itself: the
// programming cable reads the SDRAM, not the cache (nothing on the PC)
void   cacheClean(const void* p, size_t n);

// ---- power, screen ------------------------------------------------------------
int  batteryPercent();            // -1 when unknown
bool onMains();
void backlight(int level);        // 20..200, as the old firmware used it
void powerService();              // the power key: long press switches off

// ---- health (2.20.0 health log) -------------------------------------------------
struct Health {
    uint32_t heapUsedKb = 0, heapFreeKb = 0;   // the C library heap in SDRAM
    int      frames = 0;                        // screens drawn since the last call
    uint32_t frameMsSum = 0, frameMsMax = 0;    // time to draw them
    uint32_t loopMsMax = 0;                     // the longest pass of the main loop
};
void healthTake(Health* h);       // the counters since the last call, then reset
std::string resetCause();         // why the processor started this time, in words
// Called just before the power key switches the unit off.
void setPowerOffHook(std::function<void()> f);

// ---- identity of this build --------------------------------------------------
const char* buildDate();          // "YYYY-MM-DD", from the compiler

} // namespace plat

#endif
