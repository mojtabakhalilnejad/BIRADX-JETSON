#!/usr/bin/env python3
"""itds/tests/itds_test.py — BIRADx-ITDS (the H7 source on the ITDS computer), end to end.

The real program (itds/biradx-itds), the real serial code, the real files, against a stand-in
for the measurement unit's firmware on a pseudo-terminal (fakefw.py: the same three-byte
receiver, heartbeat, sweep and [T] reply as fw_main.c).

  link      Sweep gets exactly one sweep: framing in step, one or two stray bytes before it,
            the first byte of every transmission lost (drop_lead) at each framing position.
  deaf      a unit whose receiver is dead: five attempts, then it says so; nothing else sent.
  stop      Stop while the port is still asking: it is not asked again.
  case      a whole case — Air check, Saline check, two readings — recorded in records/, and
            exported to a stick when one is mounted at /media/testuser/STICK.
  single    commands=single with a lost first byte: no sweep (why "burst" is the default).
  import    a first start next to the previous ITDS software takes its device ID, passcode,
            needle revision and last case id, and its serial port and audio device.
  restart   a second start keeps the instrument's settings and does not import again.
  voice     the sweep's track (7) is played with aplay, repeating, and stopped.
  window    full screen at 1920 x 1080 under X: a mouse click on the gear (scaled) opens the lock.
  upright   full screen on a monitor standing upright (1080 x 1920): the screens are drawn
            upright, 600 x 1067, and a click on the gear where the upright layout puts it opens
            the lock; and layout=landscape on the same monitor keeps the 1024 x 600 frame.

    python3 itds/tests/itds_test.py [names...]
"""
import os, pty, re, shutil, subprocess, sys, tty, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fakefw import FakeFirmware

ITDS = os.path.dirname(HERE)
EXE = os.path.join(ITDS, "biradx-itds")
WORK = os.environ.get("ITDS_TEST_DIR", "/tmp/biradx-itds-test")
if not os.path.exists(EXE):
    print("itds_test: build first (cd itds && qmake && make)"); sys.exit(2)


def run(name, script, fw_opts=None, ini=None, home=None, path_prefix=None, timeout=60, fresh=True):
    data = os.path.join(WORK, name)
    if fresh:
        shutil.rmtree(data, ignore_errors=True)
    os.makedirs(data, exist_ok=True)
    m, s = pty.openpty(); tty.setraw(m); tty.setraw(s)
    port = os.ttyname(s)
    fw = FakeFirmware(m, load="tissue", **(fw_opts or {}))
    fw.start()
    text = ini if ini is not None else "[serial]\nport=%s\n[screen]\nfullscreen=0\n" % port
    open(os.path.join(data, "itds.ini"), "w").write(text.replace("@PORT@", port))
    open(os.path.join(data, "script.txt"), "w").write(script)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", BIRADX_ITDS_DATA=data,
               BIRADX_ITDS_SCRIPT=os.path.join(data, "script.txt"), BIRADX_ITDS_SHOTS=data,
               HOME=home or os.path.join(WORK, "nohome"))
    if path_prefix:
        env["PATH"] = path_prefix + ":" + env["PATH"]
    # The test keeps its end of the pseudo-terminal open until the program ends: a pty whose
    # last slave descriptor closes hangs up, and the stand-in firmware's writes then fail.
    r = subprocess.run([EXE, "--windowed"], env=env, capture_output=True, text=True, timeout=timeout)
    fw.stop_flag = True
    time.sleep(0.2)
    os.close(s)
    return r.stdout + r.stderr, fw, data


failed = 0
def check(name, ok, detail=""):
    global failed
    print("%-8s %s  %s" % (name, "OK  " if ok else "FAIL", detail))
    if not ok:
        failed += 1

wanted = sys.argv[1:]
def want(n):
    return not wanted or n in wanted

SWEEP = "wait 2000\nsweep\nwait 7500\nprint\nquit\n"

if want("link"):
    for label, opts, attempts in (("in step", {}, 1), ("1 stray", {"offset": 1}, 2), ("2 stray", {"offset": 2}, 3),
                                   ("lead lost", {"drop_lead": True}, 2),
                                   ("lead lost, 1 stray", {"drop_lead": True, "offset": 1}, 1),
                                   ("lead lost, 2 stray", {"drop_lead": True, "offset": 2}, 3)):
        out, fw, _ = run("link", SWEEP, opts)
        m = re.search(r"answered: \[B\] \d+ ms after attempt (\d)", out)
        check("link", fw.sweeps == 1 and m is not None and int(m.group(1)) == attempts,
              "%-20s sweeps %d, answered on attempt %s (expected %d)" % (label, fw.sweeps, m.group(1) if m else "-", attempts))

if want("deaf"):
    out, fw, _ = run("deaf", "wait 2000\nsweep\nwait 9000\nprint\nquit\n", {"deaf": True})
    check("deaf", fw.sweeps == 0 and "no answer to 5 attempts" in out,
          "sweeps %d; %s" % (fw.sweeps, "said: no answer to 5 attempts" if "no answer to 5" in out else out[-300:]))

if want("stop"):
    # Stop while the port is still asking (the unit needs a third attempt): nothing is asked again,
    # so no sweep begins seconds later to be taken for the pedal.
    out, fw, _ = run("stop", "wait 2000\nsweep\nwait 400\nstop\nwait 6000\nprint\nquit\n", {"offset": 2})
    check("stop", fw.sweeps == 0 and "Stop pressed: not asked again" in out,
          "sweeps after Stop: %d; %s" % (fw.sweeps, "the request was cancelled" if "Stop pressed" in out else out[-300:]))

if want("case"):
    # A whole case on this computer: the needle's Air and Saline checks, readings, the records.
    plan = [{"load": "open"}, {"load": "saline"}, {"load": "saline"},
            {"load": "tissue", "z1k": 900, "ips": -5.0}, {"load": "tissue", "z1k": 950, "ips": -4.0}]
    script = ("wait 1500\nid A3K7\nsettle 1\nbegincase\nintake age 54\nintake birads 4A\nintake lesion_size 14\n"
              "intake side Left\nintake clock 10\nintake needle_lot L7\nintake needle_serial N-123\ncommit\n"
              "cal\nwait 9000\ndip 25\nwait 16000\nsweep\nwait 6000\nsweep\nwait 6000\nprint\nendcase\nwait 800\n"
              "usb\nexport\nexportall\nquit\n")
    stick = "/media/testuser/STICK"
    if os.path.ismount(stick):
        for d in os.listdir(stick):
            if d.startswith("BIRADx_"):
                shutil.rmtree(os.path.join(stick, d), ignore_errors=True)
    out, fw, data = run("case", script, {"plan": plan, "saline_ohm": 150.0}, timeout=120)
    import csv
    ds = os.path.join(data, "records", "dataset.csv")
    rows = list(csv.DictReader(open(ds))) if os.path.exists(ds) else []
    ids = {r.get("case_id") for r in rows}
    check("case", len(rows) == 2 and len(ids) == 1 and re.match(r"A3K7-\w{3}-\w{3}$", next(iter(ids), "")) is not None,
          "%d readings recorded in records/dataset.csv under %s" % (len(rows), sorted(ids)))
    check("case", all(r.get("tier") == "High Risk" for r in rows), "both read High Risk (Z 900/950 ohm, IPS -5/-4)")
    for f in ("needle_cal.csv", "saline_check.csv", "patients.csv", "caseid_state.txt", "health.csv"):
        check("case", os.path.exists(os.path.join(data, "records", f)), "records/" + f)
    if os.path.ismount(stick):
        exported = [os.path.join(d, f) for d, _, fs in os.walk(stick) for f in fs if "_logs_y1_all.csv" in f]
        check("case", "mounted at " + stick in out and len(exported) >= 1,
              "Export shown wrote %s on the stick" % (os.path.basename(exported[-1]) if exported else "nothing"))
    else:
        print("case     (no stick mounted at %s: export not tested)" % stick)

if want("single"):
    out, fw, _ = run("single", "wait 2000\nsweep\nwait 6000\nprint\nquit\n", {"drop_lead": True},
                     ini="[serial]\nport=@PORT@\ncommands=single\n[screen]\nfullscreen=0\n")
    check("single", fw.sweeps == 0, "one bare frame with its first byte lost: %d sweeps (none expected)" % fw.sweeps)

if want("import") or want("restart"):
    home = os.path.join(WORK, "home")
    old = os.path.join(home, "Desktop", "BIRADx_v1")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(os.path.join(old, "records"))
    open(os.path.join(old, "device.ini"), "w").write(
        '[audio]\ndevice="plughw:2,0"\n\n[birx]\ndevice_id=1WE2\nneedle=NDL-X\n\n[security]\npasscode=4826\n\n[serial]\nbaud=57600\nport=ttyTHS1\n')
    open(os.path.join(old, "records", "caseid_state.txt"), "w").write("# birx case id state\ndevice: 1WE2\nday: 1015\nseq: 7\n")
    data = os.path.join(WORK, "import")
    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(data)
    # no itds.ini: the first start writes it, from the old device.ini; the test then points it at the pty
    m, s = pty.openpty(); tty.setraw(m); tty.setraw(s)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", BIRADX_ITDS_DATA=data, HOME=home,
               BIRADX_ITDS_SCRIPT=os.path.join(data, "script.txt"), BIRADX_ITDS_SHOTS=data)
    open(os.path.join(data, "script.txt"), "w").write("wait 1500\nprint\nquit\n")
    r = subprocess.run([EXE, "--windowed"], env=env, capture_output=True, text=True, timeout=60)
    ini = open(os.path.join(data, "itds.ini")).read()
    nv = open(os.path.join(data, "eeprom.bin"), "rb").read().decode("latin-1")
    if want("import"):
        check("import", "port=ttyTHS1" in ini and "device=plughw:2,0" in ini and ("from=" + old) in ini,
              "itds.ini from the old device.ini: port, audio device, import folder")
        check("import", "1WE2" in nv and "4826" in nv and "NDL-X" in nv,
              "device ID, passcode and needle revision in the settings image")
        check("import", re.search(r"\|1015\|7(\||\n)", nv) is not None or ("1015" in nv and "|7" in nv),
              "the last case id (day 1015, #7) carried over")
    if want("restart"):
        open(os.path.join(old, "device.ini"), "w").write("[birx]\ndevice_id=ZZZZ\n")
        r2 = subprocess.run([EXE, "--windowed"], env=env, capture_output=True, text=True, timeout=60)
        nv2 = open(os.path.join(data, "eeprom.bin"), "rb").read().decode("latin-1")
        check("restart", "1WE2" in nv2 and "ZZZZ" not in nv2, "a second start keeps the settings, imports nothing")

if want("voice"):
    fake = os.path.join(WORK, "fakebin")
    shutil.rmtree(fake, ignore_errors=True)
    os.makedirs(fake)
    log = os.path.join(WORK, "aplay.log")
    if os.path.exists(log):
        os.remove(log)
    open(os.path.join(fake, "aplay"), "w").write('#!/bin/sh\necho "$@" >> %s\nsleep 0.3\n' % log)
    os.chmod(os.path.join(fake, "aplay"), 0o755)
    data = os.path.join(WORK, "voice")
    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(os.path.join(data, "voice"))
    for t in range(1, 13):
        open(os.path.join(data, "voice", "%02d.wav" % t), "wb").write(b"RIFF")
    out, fw, _ = run("voice", "wait 1500\nvoice\nsweep\nwait 8000\nvoice\nquit\n", fresh=False, path_prefix=fake,
                     ini="[serial]\nport=@PORT@\n[audio]\ndevice=plughw:2,0\n[screen]\nfullscreen=0\n")
    lines = open(log).read().splitlines() if os.path.exists(log) else []
    sevens = [l for l in lines if l.endswith("/07.wav")]
    check("voice", len(sevens) >= 3 and all("-D plughw:2,0" in l for l in sevens),
          "track 7 played %d times during the sweep, on plughw:2,0" % len(sevens))
    check("voice", "12 of 12 recordings" in out, "the twelve recordings found")

if want("window"):
    data = os.path.join(WORK, "window")
    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(data)
    m, s = pty.openpty(); tty.setraw(m); tty.setraw(s)
    fw = FakeFirmware(m, load="tissue"); fw.start()
    open(os.path.join(data, "itds.ini"), "w").write("[serial]\nport=%s\n[screen]\nfullscreen=1\n" % os.ttyname(s))
    open(os.path.join(data, "script.txt"), "w").write("wait 2500\nscreen\nwait 3000\nscreen\nshot window\nquit\n")
    # the gear at (988, 35) on the 1024 x 600 frame, scaled by 1.8 onto 1920 x 1080, 38.4 px from the left
    gx, gy = int(38.4 + 988 * 1.8), int(35 * 1.8)
    sh = ("export QT_QPA_PLATFORM=xcb BIRADX_ITDS_DATA=%s BIRADX_ITDS_SCRIPT=%s/script.txt BIRADX_ITDS_SHOTS=%s HOME=%s/nohome; "
          "%s & p=$!; sleep 3.6; xdotool mousemove %d %d click 1; wait $p"
          % (data, data, data, WORK, EXE, gx, gy))
    r = subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24", "sh", "-c", sh],
                       capture_output=True, text=True, timeout=60)
    fw.stop_flag = True
    screens = re.findall(r"screen: (\w+)", r.stdout)
    check("window", screens == ["measure", "lock"], "screens %s (a click on the scaled gear opens the lock)" % screens)

if want("upright"):
    def upright(layout, click):
        data = os.path.join(WORK, "upright-" + layout)
        shutil.rmtree(data, ignore_errors=True)
        os.makedirs(data)
        m, s = pty.openpty(); tty.setraw(m); tty.setraw(s)
        fw = FakeFirmware(m, load="tissue"); fw.start()
        open(os.path.join(data, "itds.ini"), "w").write("[serial]\nport=%s\n[screen]\nfullscreen=1\nlayout=%s\n" % (os.ttyname(s), layout))
        open(os.path.join(data, "script.txt"), "w").write("wait 2500\nscreen\nwait 3000\nscreen\nshot upright\nquit\n")
        sh = ("export QT_QPA_PLATFORM=xcb BIRADX_ITDS_DATA=%s BIRADX_ITDS_SCRIPT=%s/script.txt BIRADX_ITDS_SHOTS=%s HOME=%s/nohome; "
              "%s & p=$!; sleep 3.6; xdotool mousemove %d %d click 1; wait $p" % (data, data, data, WORK, EXE, click[0], click[1]))
        r = subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1080x1920x24", "sh", "-c", sh],
                           capture_output=True, text=True, timeout=60)
        fw.stop_flag = True
        screens = re.findall(r"screen: (\w+)", r.stdout)
        size = None
        shot = os.path.join(data, "upright.png")
        if os.path.exists(shot):
            with open(shot, "rb") as f:
                head = f.read(24)
            size = (int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big"))
        return screens, size, r.stdout
    # auto on 1080 x 1920: a 600 x 1067 frame scaled by 1080/600 = 1.8 (1067 x 1.8 = 1920.6: the height
    # sets it, 1.7994); the upright header's gear at (565, 35) of the frame
    k = 1920 / 1067.0
    screens, size, ev = upright("auto", (int((1080 - 600 * k) / 2 + 565 * k), int(35 * k)))
    check("upright", size == (600, 1067), "auto on an upright 1080 x 1920 monitor: frame %s (600 x 1067 expected)" % (size,))
    check("upright", screens == ["measure", "lock"], "screens %s (a click on the upright layout's gear opens the lock)" % screens)
    check("upright", "the screens drawn upright, 600 x 1067" in ev, "the frame chosen is said at start")
    # layout=landscape on the same monitor: the 1024 x 600 frame across it (scaled by 1080/1024), gear at (988, 35)
    k2 = 1080 / 1024.0
    screens, size, ev = upright("landscape", (int(988 * k2), int((1920 - 600 * k2) / 2 + 35 * k2)))
    check("upright", size == (1024, 600), "layout=landscape keeps the 1024 x 600 frame: %s" % (size,))
    check("upright", screens == ["measure", "lock"], "screens %s (the landscape gear, letterboxed)" % screens)

print("itds_test: %s" % ("OK" if not failed else "%d FAILED" % failed))
sys.exit(1 if failed else 0)
