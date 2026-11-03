// gui/splash.h — the start screen: the Coosa logo and its line, white on black.
#ifndef BIRADX_SPLASH_H
#define BIRADX_SPLASH_H
#include "gfx.h"
namespace gui {
extern const int kLogoW, kLogoH;
extern const unsigned char kLogo[];
const int kSplashMs = 3000;            // how long it stays before the first screen
void drawSplash(gfx::Canvas& c);
}
#endif
