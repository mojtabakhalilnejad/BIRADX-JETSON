#include "needlecode.h"
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace birx {
namespace {

// ---------------------------------------------------------------- SHA-256
struct Sha256 {
    uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                     0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    uint64_t len = 0;
    uint8_t  buf[64] = {0};
    size_t   n = 0;

    static uint32_t ror(uint32_t x, int s) { return (x >> s) | (x << (32 - s)); }

    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
            0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
            0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
            0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
            0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
            0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
            0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
            0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
            0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16)
                 | (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15] >> 3);
            const uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19)  ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const uint8_t* p, size_t sz) {
        len += sz;
        while (sz) {
            const size_t take = (64 - n < sz) ? 64 - n : sz;
            std::memcpy(buf + n, p, take);
            n += take; p += take; sz -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }

    void finish(uint8_t out[32]) {
        const uint64_t bits = len * 8;
        const uint8_t one = 0x80;
        update(&one, 1);
        const uint8_t zero = 0;
        while (n != 56) update(&zero, 1);
        uint8_t tail[8];
        for (int i = 0; i < 8; ++i) tail[i] = uint8_t(bits >> (56 - i*8));
        update(tail, 8);
        for (int i = 0; i < 8; ++i) {
            out[i*4]   = uint8_t(h[i] >> 24); out[i*4+1] = uint8_t(h[i] >> 16);
            out[i*4+2] = uint8_t(h[i] >> 8);  out[i*4+3] = uint8_t(h[i]);
        }
    }
};

// ---------------------------------------------------------------- base32
const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int valueOf(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    if (c == 'I' || c == 'L') c = '1';
    if (c == 'O') c = '0';
    if (c == 'U') c = 'V';
    const char* p = std::strchr(kAlphabet, c);
    return p ? int(p - kAlphabet) : -1;
}

// Strips separators and folds the forgiving characters. Returns empty on any
// character outside the alphabet.
std::string norm(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '-' || c == ' ' || c == '_' || c == '\t') continue;
        const int v = valueOf(c);
        if (v < 0) return std::string();
        out.push_back(kAlphabet[v]);
    }
    return out;
}

std::string fromHex(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nib = [](char c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nib(hex[i]), lo = nib(hex[i+1]);
        if (hi < 0 || lo < 0) return std::string();
        out.push_back(char((hi << 4) | lo));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
std::string sha256Hex(const std::string& data) {
    Sha256 s;
    s.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    uint8_t d[32];
    s.finish(d);
    static const char* hx = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 32; ++i) { out.push_back(hx[d[i] >> 4]); out.push_back(hx[d[i] & 15]); }
    return out;
}

std::vector<uint8_t> hmacSha256(const std::string& key, const std::string& data) {
    uint8_t k[64] = {0};
    if (key.size() > 64) {
        Sha256 s;
        s.update(reinterpret_cast<const uint8_t*>(key.data()), key.size());
        s.finish(k);
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    uint8_t ip[64], op[64];
    for (int i = 0; i < 64; ++i) { ip[i] = uint8_t(k[i] ^ 0x36); op[i] = uint8_t(k[i] ^ 0x5c); }

    uint8_t inner[32];
    { Sha256 s; s.update(ip, 64);
      s.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
      s.finish(inner); }

    std::vector<uint8_t> out(32);
    { Sha256 s; s.update(op, 64); s.update(inner, 32); s.finish(out.data()); }
    return out;
}

// ---------------------------------------------------------------------------
const char* needleStatusName(NeedleStatus s) {
    switch (s) {
        case NeedleStatus::Ok:          return "OK";
        case NeedleStatus::NoKey:       return "NO KEY";
        case NeedleStatus::BadFormat:   return "INVALID";
        case NeedleStatus::BadCheck:    return "INVALID";
        case NeedleStatus::AlreadyUsed: return "ALREADY USED";
    }
    return "INVALID";
}

// ---------------------------------------------------------------------------
namespace {
// The 20-bit MAC, rendered as four base32 characters.
std::string checkOf(const std::string& key, const std::string& lot,
                    const std::string& serial) {
    const std::vector<uint8_t> mac = hmacSha256(key, "NDL1|" + lot + "|" + serial);
    const uint32_t v = (uint32_t(mac[0]) << 12) | (uint32_t(mac[1]) << 4)
                     | (uint32_t(mac[2]) >> 4);                 // 20 bits
    std::string s(4, '0');
    uint32_t x = v;
    for (int i = 3; i >= 0; --i) { s[i] = kAlphabet[x % 32]; x /= 32; }
    return s;
}
} // namespace

bool NeedleKeyring::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string ver, hex;
        if (!(in >> ver >> hex) || ver.size() != 1) continue;
        const std::string raw = fromHex(hex);
        if (raw.size() < 16) continue;            // refuse a short key outright
        m_keys[char(std::toupper(static_cast<unsigned char>(ver[0])))] = raw;
    }
    return !m_keys.empty();
}

void NeedleKeyring::addKey(char version, const std::string& keyHex) {
    const std::string raw = fromHex(keyHex);
    if (raw.size() >= 16)
        m_keys[char(std::toupper(static_cast<unsigned char>(version)))] = raw;
}

NeedleStatus NeedleKeyring::check(const std::string& lot, const std::string& serial,
                                  std::string* canonical) const {
    if (canonical) canonical->clear();
    if (m_keys.empty()) return NeedleStatus::NoKey;

    const std::string L = norm(lot), S = norm(serial);
    // The serial field may be typed with the check appended, which is how it
    // is printed on the packet; accept 5 or 9 characters there.
    if (L.size() != 4) return NeedleStatus::BadFormat;
    std::string body, chk;
    if (S.size() == 9)      { body = S.substr(0, 5); chk = S.substr(5, 4); }
    else                    return NeedleStatus::BadFormat;

    auto it = m_keys.find(L[0]);
    if (it == m_keys.end()) return NeedleStatus::BadCheck;   // unknown key version

    if (checkOf(it->second, L, body) != chk) return NeedleStatus::BadCheck;
    if (canonical) *canonical = L + body;
    return NeedleStatus::Ok;
}

std::string NeedleKeyring::mint(const std::string& lotBody4,
                                const std::string& serial5) const {
    const std::string L = norm(lotBody4), S = norm(serial5);
    if (L.size() != 4 || S.size() != 5) return std::string();
    auto it = m_keys.find(L[0]);
    if (it == m_keys.end()) return std::string();
    return L + "-" + S + checkOf(it->second, L, S);
}

// ---------------------------------------------------------------------------
bool NeedleRegistry::load() {
    m_used.clear();
    std::ifstream f(m_path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string code, caseId;
        if (!(in >> code)) continue;
        in >> caseId;
        m_used[code] = caseId;
    }
    return true;
}

bool NeedleRegistry::used(const std::string& canonical) const {
    return m_used.find(canonical) != m_used.end();
}

bool NeedleRegistry::markUsed(const std::string& canonical, const std::string& caseId,
                              const std::string& isoTime) {
    if (canonical.empty()) return false;
    std::ofstream f(m_path, std::ios::app);
    if (!f) return false;
    f << canonical << " " << (caseId.empty() ? "-" : caseId) << " " << isoTime << "\n";
    if (!f.good()) return false;
    m_used[canonical] = caseId;
    return true;
}

} // namespace birx
