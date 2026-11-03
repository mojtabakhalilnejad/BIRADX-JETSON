// gui/splash.cpp — see splash.h. Nothing else is on this screen: a black
// panel, and the logo with its line in full white, centred.
#include "splash.h"

namespace gui {

void drawSplash(gfx::Canvas& c)
{
    c.resetClip();
    c.fill(gfx::Rect(0, 0, c.width(), c.height()), gfx::Color(0, 0, 0));
    // H7-1.6.0: the logo at about a third of the panel's width (380 of 1024 px;
    // it was 720), as start screens are drawn now, and a little above the
    // middle, where the eye puts the centre.
    const int x0 = (c.width() - kLogoW) / 2, y0 = (c.height() - kLogoH) / 2 - c.height() / 30;
    uint16_t* px = c.pixels();
    for (int y = 0; y < kLogoH; ++y) {
        const int yy = y0 + y;
        if (yy < 0 || yy >= c.height()) continue;
        const unsigned char* src = kLogo + y * kLogoW;
        uint16_t* dst = px + yy * c.width() + x0;
        for (int x = 0; x < kLogoW; ++x) {
            const unsigned v = src[x];
            if (!v || x0 + x < 0 || x0 + x >= c.width()) continue;
            dst[x] = uint16_t(((v & 0xF8) << 8) | ((v & 0xFC) << 3) | (v >> 3));
        }
    }
}

} // namespace gui
