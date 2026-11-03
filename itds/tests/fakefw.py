"""tools/fakefw.py — a stand-in for the STM32 firmware, faithful where it hurts.

It reproduces the behaviour of fw_main.c that the application has to live
with, not an idealised instrument:

  * commands are exactly three bytes "[X]", received by interrupt even while
    the main loop is blocked; frames that arrive during an [R] sweep are
    DROPPED (rx_packet_ready = 0 at the end of the command branch), while
    frames that arrive during a pedal sweep are acted on after it;
  * [R] (and the pedal) send [B], fifteen [imp...] points high -> low, the
    IPS line and [E];
  * [S] is NOT stop: it runs the linear sweep from sweep_start to sweep_stop
    (1 kHz to 1 MHz in 10 kHz steps, 100 points) with no [B] and no [E];
  * [T] answers "TDP is connected";
  * [C] every second, from the main loop only — never during a sweep.

The load is chosen by `load`: "open" (a needle in air, 60 pF) or "tissue".
Timings follow the firmware's delays (about 0.35 s per point on range 6,
about 0.12 s per point of the linear sweep); `speed` divides them.
"""
import math, os, select, threading, time

FREQS = [500000, 300000, 250000, 225000, 199000, 175000, 125000, 100000,
         75000, 50000, 25000, 10000, 5000, 1000, 500]


class FakeFirmware(threading.Thread):
    def __init__(self, fd, load="open", cp=60e-12, speed=1.0, log=None, pedal_every=0.0,
                 offset=0, deaf=False, tissue_z1k=1200.0, tissue_ips=3.0, saline_ohm=250.0,
                 plan=None, drop_lead=False):
        super().__init__(daemon=True)
        # `plan`: what is on the needle for each successive sweep, as dicts
        # {"load": ..., "z1k": ..., "ips": ...}; after it runs out the last
        # load stays. Lets one run walk air -> saline -> tissue.
        self.plan = list(plan or [])
        self.tissue_z1k, self.tissue_ips, self.saline_ohm = tissue_z1k, tissue_ips, saline_ohm
        # `offset` stray bytes already in the receiver when the application
        # starts talking: the firmware's 3-byte framing is out of step by that
        # much, as after a glitch on the line. `deaf`: its receiver has died
        # (an overrun, HAL aborts reception, nothing re-arms it) — only the
        # pedal still works.
        self.junk = b"\x00" * offset
        # `drop_lead`: the first byte of any transmission that starts after
        # 100 ms of silence on the line is lost (a transmitter that wakes up
        # late). Single frames then never arrive intact.
        self.drop_lead = drop_lead
        self.last_rx = 0.0
        self.deaf = deaf
        self.pedal_now = False
        # > 0: the front-panel pedal is pressed every `pedal_every` seconds,
        # which sends a sweep whatever the application asked for.
        self.pedal_every = pedal_every
        self.pedal_on = False
        self.fd, self.load, self.cp, self.speed = fd, load, cp, speed
        self.log = log if log is not None else []
        self.pending = None
        self.buf = b""
        self.stop_flag = False
        self.sweeps = 0
        self.linear = 0

    def w(self, s):
        os.write(self.fd, s.encode("latin-1"))

    def rx(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return
        try:
            data = os.read(self.fd, 4096)
        except OSError:
            return
        if self.deaf:
            self.log.append((time.time(), "RX ignored (receiver dead): %r" % data))
            return
        now = time.time()
        if self.drop_lead and now - self.last_rx > 0.1 and data:
            self.log.append((now, "RX lead byte lost: %r" % data[:1]))
            data = data[1:]
        self.last_rx = now
        if self.junk:
            data, self.junk = self.junk + data, b""
        self.buf += data
        while len(self.buf) >= 3:
            fr, self.buf = self.buf[:3], self.buf[3:]
            if fr[0:1] == b"[" and fr[2:3] == b"]":
                self.pending = chr(fr[1])
                self.log.append((time.time(), "RX [%s]" % self.pending))

    def busy(self, seconds):
        """Blocked in SweepFreq(): bytes arrive, only the last frame survives."""
        end = time.time() + seconds / self.speed
        while time.time() < end:
            self.rx(max(0.0, end - time.time()))

    def point(self, f):
        """One [imp...] line for the current load. `load` may be changed
        between sweeps: "open" (needle in air), "saline" (0.9% NaCl: a
        resistance with the needle's stray capacitance across it and a little
        electrode polarisation at low frequency) or "tissue"."""
        w = 2 * math.pi * f
        if self.load == "open_field":
            # The needle in air EXACTLY as the unit 1W23 reported it on
            # 2026-09-15 (diagnostics file): 1.4 MΩ at 500 kHz, phases that
            # mean nothing, and the 10 MΩ ceiling from 125 kHz down.
            field = {500000: (1425388.9, -146.48), 300000: (3994207.9, 119.00),
                     250000: (7539440.6, 82.81), 225000: (7925388.0, 54.56),
                     199000: (7942180.1, 80.09), 175000: (8842350.7, 90.04),
                     125000: (9999999.0, 82.01), 100000: (9999999.0, -75.39),
                     75000: (9999999.0, 68.41), 50000: (9999999.0, -37.39),
                     25000: (9999999.0, 34.54), 10000: (9999999.0, 24.20),
                     5000: (9999999.0, 4.68), 1000: (9999999.0, 53.63),
                     500: (9999999.0, 33.22)}
            z, ph = field[f]
            return "[imp%08d,%08d,%+07.02f,%d]\r\n" % (int(f * 10), int(z * 10), ph, 6)
        if self.load == "open":
            z = 1.0 / (w * self.cp)
            ph, rng = -88.0, 6
        elif self.load == "saline":
            zr = complex(self.saline_ohm, 0)
            # polarisation, in series: about 110 ohm at 1 kHz (the tip surface the H7-2.15
            # saline check expects: 20 to 3000 ohm)
            zp = 1.2e5 / (complex(0, w) ** 0.8)
            zs = zr + zp
            zc = 1.0 / complex(0, w * self.cp)            # stray, in parallel
            zt = zs * zc / (zs + zc)
            z, ph = abs(zt), math.degrees(math.atan2(zt.imag, zt.real))
            rng = 1 if z < 500 else 2
        else:
            # |Z| falls gently with f; phase is a straight line in log f whose
            # slope per decade is exactly the requested IPS.
            z = self.tissue_z1k * (1000.0 / f) ** 0.25
            ph = -12.0 + self.tissue_ips * (math.log10(f) - 5.0)
            rng = 3 if f < 50000 else 2
        z = min(z, 9999999)
        return "[imp%08d,%08d,%+07.02f,%d]\r\n" % (int(f * 10), int(z * 10), ph, rng)

    def sweep(self):
        if self.plan:
            step = self.plan.pop(0)
            self.load = step.get("load", self.load)
            self.tissue_z1k = step.get("z1k", self.tissue_z1k)
            self.tissue_ips = step.get("ips", self.tissue_ips)
        self.sweeps += 1
        self.log.append((time.time(), "sweep begins"))
        self.w("[B]\r\n")
        self.busy(0.25)                       # the two 80 ms beeps
        for f in FREQS:
            self.busy(0.35 if self.load == "open" else 0.08)
            self.w(self.point(f))
        self.w("[IPS+000.00,Z%08.2f,Low Risk]\r\n" % 1000.0)
        self.w("[E]\r\n")
        self.log.append((time.time(), "sweep ends"))

    def linear_sweep(self):
        self.linear += 1
        self.log.append((time.time(), "LINEAR sweep begins ([S])"))
        f = 1000.0
        while f <= 1000000.0:
            self.busy(0.12)
            self.w(self.point(f))
            f += 10000.0
        self.log.append((time.time(), "LINEAR sweep ends"))

    def run(self):
        last_c = time.time()
        last_pedal = time.time()
        while not self.stop_flag:
            self.rx(0.05)
            if self.pedal_now:
                self.pedal_now = False
                self.sweep()
                continue
            if self.pedal_on and self.pedal_every > 0 and time.time() - last_pedal >= self.pedal_every:
                last_pedal = time.time()
                self.sweep()
                continue
            if time.time() - last_c >= 1.0:
                self.w("[C]\r\n\x00\x00")
                last_c = time.time()
            cmd, self.pending = self.pending, None
            if cmd == "R":
                self.sweep()
                self.pending = None          # rx_packet_ready = 0 after the branch
            elif cmd == "S":
                self.linear_sweep()
            elif cmd == "T":
                self.w("TDP is connected\r\n")
