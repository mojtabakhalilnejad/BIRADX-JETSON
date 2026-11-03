# BIRADX-JETSON

BIRADx on the ITDS instrument's own computer (the Jetson, Linux, Qt 5): the program that runs on
its monitor, reads the measurement unit on its serial port (`ttyTHS1`, 57600 8N1, the ITDS
firmware unchanged), decides, records and shows the result.

It started from the BIRADx-H7 display-board source (H7-2.15.0) and is kept here apart from it: this
repository holds only what the Jetson needs. The display board's hardware layer, its build and its
drivers are not here, and nothing here goes back to the H7 repository.

```
 ITDS measurement unit (unchanged)        ITDS computer (Jetson, Linux): this program
 AD9833 → tissue → ADCs     ttyTHS1        birxcore/  the decision core
 15-point sweep, [R]/pedal ───────────►    app/       the instrument's behaviour (no screen)
                            ◄───────────   gui/       the screens: 1024 × 600, or upright
                              [R] [T]      itds/      the window, the port, the files, the sound
```

## Build and install (on the Jetson)

```
cd itds
./install.sh                 # builds, then installs in ~/BIRADx-ITDS
~/BIRADx-ITDS/biradx-itds    # run; Ctrl+Q closes it
```

Needs `qtbase5-dev` (Qt 5.9 or later), `g++` (7 or later) and `alsa-utils`. `install.sh` replaces
only the program: `records/`, `eeprom.bin`, `itds.ini` and `voice/` in `~/BIRADx-ITDS` are kept.
Everything about this computer — the settings file, the screen layout, the first start next to the
previous ITDS software, the tests — is in [`itds/README.md`](itds/README.md).

**Start at power-on.** On this instrument the desktop runs `~/startup.bash` (it turns the screen
upright with `xrandr -o left` and fixes the touch). The program is started on its last line, after
those: `/home/nbel/BIRADx-ITDS/biradx-itds`. Only one program may hold the serial port.

## Folders

| Folder | What it is |
|---|---|
| `birxcore/` | The decision core (BIRADx 2.20.0, ported): the parser, needle correction, features, quality gate, rule table, case aggregation. |
| `app/` | The instrument's behaviour without a screen, in plain C++ over `app/platform.h`. |
| `gui/` | The screens, drawn into an RGB565 frame: 1024 × 600, or upright (600 × 960…1100) for an upright monitor. |
| `fonts/` | Inter, JetBrains Mono and DejaVu Sans (OFL / free licences), pre-rendered by `tools/mkfonts.py`. |
| `itds/` | The Jetson: Qt window, serial port, files, sound (`platform_itds.cpp`, `main_itds.cpp`), `install.sh`, and its end-to-end tests against a stand-in for the firmware. |
| `sim/` | A PC simulator: the same `app/` and `gui/` against a model of the measurement unit, writing screenshots. |
| `tests/` | The decision core's self-test and the application's tests. |
| `tools/`, `assets/` | Font and logo generators and the logo's source. |
| `docs/` | `CHANGELOG.md`, `DIFFERENCES.md` (from the Jetson software 2.x), `LOT_CALIBRATION_FA.md`. |

## Tests (on a PC)

```
make -f Makefile.pc test apptest          # the decision core and the application
sim/run.sh sim/scripts/workflow.txt shots # a whole case in the simulator, as screenshots
BIRADX_SIM_PORTRAIT=1 sim/run.sh sim/scripts/screens.txt shots-upright
cd itds && qmake && make && python3 tests/itds_test.py
```

The simulator is built without `BIRADX_ITDS`, so its screens show the display board's version
line and its board-only rows (internal memory, backlight); the Jetson program does not.
