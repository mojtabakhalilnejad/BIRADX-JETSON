// app/version.h — the one version string (as src/version.h in 2.18.0).
// Shown in Settings -> About, written into the diagnostics file and the
// event log. The rule-table version recorded with every reading stays the
// core's own `model_version` column.
#ifndef BIRADX_H7_VERSION_H
#define BIRADX_H7_VERSION_H
#define BIRADX_PARITY       "2.20.0"      // the Jetson release this port reproduces
// The same source runs on two computers. BIRADX_ITDS is defined by itds/biradx-itds.pro: the
// ITDS computer (the Jetson, Linux) in place of the display board.
#if defined(BIRADX_ITDS)
#define BIRADX_VERSION      "ITDS-2.16.0"
#define BIRADX_PRODUCT      "BIRADx-ITDS"
#define BIRADX_HARDWARE     "ITDS computer (Linux) \xC2\xB7 measurement unit on its serial port (ITDS firmware, unchanged)"
#else
#define BIRADX_VERSION      "H7-2.15.0"
#define BIRADX_PRODUCT      "BIRADx-H7"
#define BIRADX_HARDWARE     "Display board STM32H743 \xC2\xB7 measurement unit on USART1 (ITDS firmware, unchanged)"
#endif
#endif
