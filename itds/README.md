# BIRADx-ITDS — the BIRADx-H7 source on the ITDS computer

The ITDS instrument as it is — the measurement board with its unchanged firmware (`fw_main.c`), the
Jetson, its monitor and touch screen, its serial cable on `ttyTHS1` — running the BIRADx-H7
application instead of the Qt/QML program (`tnims`, BIRADx 2.x).

```
 ITDS measurement board (unchanged)       ITDS computer (Jetson, Linux): this program
 AD9833 → tissue → ADCs     ttyTHS1        app/ + birxcore/  — the H7 application, unchanged
 15-point sweep, [R]/pedal ───────────►    gui/ + fonts/     — the H7 screens, 1024 × 600 or upright
                            ◄───────────   itds/             — the window, the port, the files,
                              [R] [T]                          the sound, on Linux
```

**One source, two computers.** `app/`, `gui/`, `fonts/` and `birxcore/` are compiled as they are, with
`BIRADX_ITDS` defined. The few places where the two computers differ — the version line, the About
line, the board-only Settings rows (internal memory chip, backlight), and four sentences that name
the board's hardware — are marked `#if defined(BIRADX_ITDS)`. The screens have two layouts: the
display board's 1024 × 600, and (ITDS-2.16.0) an upright one for an upright monitor; on 1024 × 600
every screen is pixel-identical to H7-2.15.0's.

## What runs where

| | Display board (H7) | ITDS computer (this folder) |
|---|---|---|
| version | `H7-2.15.0` | `ITDS-2.16.0` — the same application and screens |
| screen | 1024 × 600 LTDC panel, GT911 | a Qt window, full screen; the screens laid out for the monitor's shape (`[screen] layout`): 1024 × 600 on a wide monitor, upright 600 × 960…1100 on an upright one, scaled to fill it (kept in proportion); touch or mouse |
| link | USART1 57600 8N1 | the serial port in `itds.ini` (`ttyTHS1`), 57600 8N1, read by its own thread; reopened after 12 s of silence |
| Sweep (`[R]`) | one frame | `commands=burst`: as the ITDS software 2.22 sent it — the frame six times, 3 ms apart, just after a heartbeat; again at the next heartbeats (two alignment bytes first) until the sweep begins, five at most; Stop cancels it. `commands=single`: one frame |
| sound | DY voice module, tracks 1–12 | `voice/01.wav` … `voice/12.wav`, played with `aplay -D <device>` |
| settings (device ID, passcode, rules, protocol, case-id counter) | 2 KB EEPROM | `eeprom.bin` in the data folder (2 KB, the same format, written whole and renamed into place) |
| records | SD card / internal memory | `records/` in the data folder, on the computer's disk — the same files and columns |
| exports | USB stick on the board | a USB stick mounted by the desktop under `/media` |
| clock | DS1307 | the computer's clock; when the program may not set it (no root, no password-less sudo), the difference is kept in `clock_offset.txt` |
| measurement unit power restart | the board switches it | not available here (the Jetson cannot switch the measurement board's supply) |

## Build and install (on the Jetson)

```
cd itds
./install.sh                 # builds, then installs in ~/BIRADx-ITDS
./install.sh --autostart     # … and starts it with the desktop
~/BIRADx-ITDS/biradx-itds    # run; Ctrl+Q closes it
```

Needs what `tnims` needed: `qtbase5-dev` (Qt 5.9 or later), `g++` (7 or later), and `alsa-utils`.
Built and tested here with gcc 7.5 + Qt 5.9.5 (Ubuntu 18.04's, as on the Jetson) and with gcc 13 +
Qt 5.15. `install.sh` replaces only the program: `records/`, `eeprom.bin`, `itds.ini` and `voice/`
in the folder are kept.

**Only one program on the port.** If `tnims` (the previous ITDS software) starts with the desktop,
take it out of the autostart first: two programs reading one serial port take each other's bytes.
Settings → Device → Measurement unit says "ALSO OPEN in …" when another program holds the port.

## The screen

`[screen] layout` in `itds.ini`: `auto` (the default; also when the line is missing, as in an
`itds.ini` written by ITDS-2.15.0) lays the screens out by the monitor's shape after `rotate` —
taller than wide: upright, 600 wide and as tall as the monitor's proportion (960 to 1100), else the
display board's 1024 × 600. `portrait` or `landscape` forces one. The program says at start which it
chose (`biradx-itds: screen 1080x1920 (rotate 0, layout auto): the screens drawn upright, 600 x 1067`).

## The data folder

The folder the program is in (or `--data <folder>`, or `BIRADX_ITDS_DATA`):

| | |
|---|---|
| `itds.ini` | this computer's settings: serial port, `commands`, audio device and mixer, USB search path, screen (layout, full screen, rotation, cursor). Written with its defaults on the first start. |
| `eeprom.bin` | the instrument's settings (as the board's EEPROM). |
| `records/` | `dataset.csv`, `patients.csv`, `outcomes.csv`, `sweeps/`, `needle_cal.csv`, `saline_check.csv`, `health.csv`, the diagnostic files … |
| `voice/` | the twelve recordings (see `voice/README.txt`). |
| `clock_offset.txt` | only when the clock was set in Settings and the program may not set the computer's. |

**First start next to the previous ITDS software.** When `~/Desktop/BIRADx_v1/device.ini` (or the
folder in `[import] from`) is found, the first start takes from it: the serial port and the audio
device (into `itds.ini`), and the device ID, the passcode, the needle revision and the last case
id it issued (`records/caseid_state.txt`) into the instrument's settings — so case ids continue
and none is issued twice. The time zone is taken from the computer's. Its records stay where
they are: this program writes its own `records/`. Its rule table (`margins.json`) is not taken:
the H7 rules are set in Settings → Rules.

## Tests

```
cd itds && qmake && make
python3 tests/itds_test.py
```

`tests/itds_test.py` runs the real program, the real port code and the real files against
`tests/fakefw.py`, a stand-in for the firmware on a pseudo-terminal (its three-byte receiver,
heartbeat, sweep and `[T]` reply): one sweep per Sweep with the framing in step, one or two stray
bytes, and the first byte of every transmission lost; a deaf unit; Stop while asking; one bare
frame losing its first byte (why `burst` is the default); a whole case (Air check, Saline check,
two readings) into `records/` and exported to a stick; the first-start import and a restart; the
recordings played with `aplay`; the window full screen at 1920 × 1080 under X, a click on the
scaled gear opening the lock; and on an upright 1080 × 1920 monitor, the upright frame (600 × 1067)
with its gear where the upright layout puts it, and `layout=landscape` keeping 1024 × 600. The application's own tests are the H7 ones (`make -f Makefile.pc
test apptest` in the folder above).

`BIRADX_ITDS_SCRIPT=<file>` runs a scripted session on the real platform (the simulator's
`wait`, `tap`, `press`, `release`, `drag`, `shot`, `id`, `print`, and `sweep`, `stop`,
`begincase`, `intake`, `commit`, `cal`, `dip`, `settle`, `endcase`, `export`, `exportall`, `usb`,
`voice`, `screen`, `geom`, `quit`), writing PNG screenshots to `BIRADX_ITDS_SHOTS`.
