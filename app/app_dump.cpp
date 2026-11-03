// app/app_dump.cpp — the records for the programming cable (H7-2.7.0).
//
// Logs → All files copies the records folder to a USB stick. Where there is no
// stick (or the USB port is the one in question), the same files can leave by
// the programming cable instead: they are put in the board's SDRAM as one ZIP
// archive (stored, not compressed: every ZIP tool opens it), and
// STM32_Programmer_CLI reads that memory over SWD while the program keeps
// running (mode=HOTPLUG, -u address size file). The screen shows the command.
//
//   - The archive holds every file of the records folder, sub-folders
//     included, with its path, its date (the clock's) and its CRC.
//   - At most about 1.5 MB of files go in one part: the records of a long
//     study are copied in several parts, one command each. A single file of
//     more than 3 MB is not put in at all and is named on the screen (it is
//     copied with a USB stick).
//   - The archive's comment says the device, the time and the part; it is
//     padded so that the archive's length is a whole number of words, which is
//     what the programmer reads.
//   - Nothing is written to the records: the memory is freed by the next part,
//     or when the Logs screen is left.
#include "app.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace app {

struct DumpFile {
    std::string name;       // its path below the records folder
    long size;
};

namespace {
const long kPartBytes = 1536L * 1024;      // the files of one part, at most (one larger file is a part alone)
const long kFileMax   = 3L * 1024 * 1024;  // a larger file is left to the USB stick

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

// every file below root/rel, sub-folders included
void collect(const std::string& root, const std::string& rel, std::vector<DumpFile>* out)
{
    const std::string path = rel.empty() ? root : root + "/" + rel;
    plat::listDir(path, [&](const std::string& name, bool isDir, long size) {
        const std::string r = rel.empty() ? name : rel + "/" + name;
        if (isDir) collect(root, r, out);
        else out->push_back(DumpFile{r, size});
    });
}

uint32_t crc32(const uint8_t* p, size_t n)
{
    static bool ready = false;
    static uint32_t table[256];
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

void put16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
} // namespace

void App::dumpFree()
{
    if (m_dumpBuf) std::free(m_dumpBuf);
    m_dumpBuf = nullptr;
    m_dumpLen = 0;
    m_dumpPart = 0;
    m_dumpParts = 0;
    m_dumpFiles = 0;
}

// What reads the part over the cable, typed on the PC (in a command window).
std::string App::dumpCommand() const
{
    if (!m_dumpBuf) return std::string();
    return fmt("STM32_Programmer_CLI.exe -c port=SWD freq=1800 mode=HOTPLUG -u 0x%08lX 0x%lX "
               "\"%%USERPROFILE%%\\Downloads\\records_%d.bin\"",
               (unsigned long)(uintptr_t)m_dumpBuf, (unsigned long)m_dumpLen, m_dumpPart);
}

// Part `part` (1 = the first) of the records as a ZIP archive in memory; "" when it is there.
std::string App::dumpPrepare(int part)
{
    dumpFree();
    if (!plat::sdReady()) return "There are no records to copy: there is no records memory.";
    if (m_running || m_inSweep || m_matTesting || m_lotTesting || m_dipping || m_calibrating)
        return "Wait until the measurement in progress is over.";
    matRewriteIfStale();                   // the library file as the classifier marks it

    std::vector<DumpFile> files;
    collect(m_records, std::string(), &files);
    // the parts: the files in the order found, about kPartBytes each
    std::vector<std::vector<size_t>> parts;
    std::vector<std::string> skipped;
    long acc = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        const long sz = std::max(0L, files[i].size);
        if (sz > kFileMax) { skipped.push_back(files[i].name); continue; }
        if (parts.empty() || (acc + sz > kPartBytes && !parts.back().empty())) {
            parts.push_back(std::vector<size_t>());
            acc = 0;
        }
        acc += sz;
        parts.back().push_back(i);
    }
    if (parts.empty()) return "There are no records to copy.";
    const int p = (part < 1 || part > int(parts.size())) ? 1 : part;
    const std::vector<size_t>& sel = parts[size_t(p - 1)];

    // the sizes as they are now, and what the archive needs
    std::vector<size_t> sizes;
    size_t total = 22;                     // the end of the central directory
    for (size_t k : sel) {
        std::ifstream f(m_records + "/" + files[k].name, std::ios::binary);
        size_t n = 0;
        if (f) {
            f.seekg(0, std::ios::end);
            const std::streamoff e = f.tellg();
            n = e < 0 ? 0 : size_t(e);
        }
        sizes.push_back(n);
        total += n + 2 * (files[k].name.size() + 38);   // its local header and its entry in the directory, each with its name
    }
    const std::string comment = fmt("BIRADx records from device %s, %s, part %d of %d", m_s.deviceId.c_str(),
                                    isoUtc(nowMs()).c_str(), p, int(parts.size()));
    const size_t need = total + comment.size() + 4;
    uint8_t* const z = static_cast<uint8_t*>(std::malloc(need));
    if (!z)
        return fmt("There is not enough memory for this part (%.1f MB). Copy the records to a USB stick instead.",
                   need / 1048576.0);

    // the clock's time, as a ZIP file has it
    const plat::Clock c = nowLocal();
    const uint32_t dosTime = uint32_t(c.hour << 11 | c.min << 5 | c.sec / 2);
    const uint32_t dosDate = uint32_t((std::max(c.year, 1980) - 1980) << 9 | c.month << 5 | c.day);

    std::vector<uint32_t> offs, crcs, lens;
    size_t at = 0;
    for (size_t j = 0; j < sel.size(); ++j) {
        const DumpFile& df = files[sel[j]];
        uint8_t* const h = z + at;
        uint8_t* const data = h + 30 + df.name.size();
        size_t got = 0;
        if (sizes[j] != 0) {
            std::ifstream f(m_records + "/" + df.name, std::ios::binary);
            if (f) {
                f.read(reinterpret_cast<char*>(data), std::streamsize(sizes[j]));
                got = size_t(std::max<std::streamsize>(0, f.gcount()));
            }
        }
        const uint32_t crc = crc32(data, got);
        offs.push_back(at);
        crcs.push_back(crc);
        lens.push_back(got);
        put32(h, 0x04034b50u);             // a file: its local header
        put16(h + 4, 20);
        put16(h + 6, 0);
        put16(h + 8, 0);                   // stored
        put16(h + 10, dosTime);
        put16(h + 12, dosDate);
        put32(h + 14, crc);
        put32(h + 18, uint32_t(got));
        put32(h + 22, uint32_t(got));
        put16(h + 26, uint32_t(df.name.size()));
        put16(h + 28, 0);
        std::memcpy(h + 30, df.name.data(), df.name.size());
        at += 30 + df.name.size() + got;
    }
    const size_t cd = at;                    // the central directory
    for (size_t j = 0; j < sel.size(); ++j) {
        const DumpFile& df = files[sel[j]];
        uint8_t* const e = z + at;
        put32(e, 0x02014b50u);
        put16(e + 4, 20);
        put16(e + 6, 20);
        put16(e + 8, 0);
        put16(e + 10, 0);
        put16(e + 12, dosTime);
        put16(e + 14, dosDate);
        put32(e + 16, crcs[j]);
        put32(e + 20, lens[j]);
        put32(e + 24, lens[j]);
        put16(e + 28, uint32_t(df.name.size()));
        put16(e + 30, 0);
        put16(e + 32, 0);
        put16(e + 34, 0);
        put16(e + 36, 0);
        put32(e + 38, 0);
        put32(e + 42, offs[j]);
        std::memcpy(e + 46, df.name.data(), df.name.size());
        at += 46 + df.name.size();
    }
    uint8_t* const end = z + at;           // the end of the central directory
    put32(end, 0x06054b50u);
    put16(end + 4, 0);
    put16(end + 6, 0);
    put16(end + 8, uint32_t(sel.size()));
    put16(end + 10, uint32_t(sel.size()));
    put32(end + 12, uint32_t(at - cd));
    put32(end + 16, uint32_t(cd));
    size_t cl = comment.size();            // padded: the archive a whole number of words
    while ((at + 22 + cl) % 4) ++cl;
    put16(end + 20, uint32_t(cl));
    std::memset(end + 22, ' ', cl);
    std::memcpy(end + 22, comment.data(), comment.size());
    const size_t len = at + 22 + cl;
    plat::cacheClean(z, len);              // in the SDRAM itself, where the cable reads it

    m_dumpBuf = z;
    m_dumpPart = p;
    m_dumpLen = len;
    m_dumpParts = int(parts.size());
    m_dumpFiles = int(sel.size());
    m_dumpSkipped = skipped;
    event(fmt("records for the programming cable: part %d of %d, %d files, %lu bytes at 0x%08lX", p,
              int(parts.size()), int(sel.size()), (unsigned long)len, (unsigned long)(uintptr_t)z));
    touch();
    return std::string();
}

} // namespace app
