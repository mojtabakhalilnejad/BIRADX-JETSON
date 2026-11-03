// fonts/fonts.h — pre-rasterised fonts, 4-bit coverage (tools/mkfonts.py).
#ifndef BIRADX_FONTS_H
#define BIRADX_FONTS_H
#include <cstdint>

namespace fonts {

struct Glyph {
    uint16_t cp;        // Unicode code point
    int8_t   left;      // bitmap left bearing
    int8_t   top;       // bitmap top, above the baseline
    uint8_t  w, h;
    uint8_t  adv;       // advance, pixels
    uint32_t off;       // into bits[], rows of (w+1)/2 bytes, high nibble first
};

struct Font {
    int size;           // pixel size
    int ascent, descent, lineHeight;
    int count;
    const Glyph*   glyphs;   // sorted by cp
    const uint8_t* bits;
};

} // namespace fonts

#include "fonts_list.h"

#endif
