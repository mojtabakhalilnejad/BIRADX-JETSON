# Deliberate differences from BIRADx 2.20.0

Since H7-1.1.0 this port follows BIRADx **2.20.0** (H7-1.0.0 followed 2.18.0). The instrument
decides as 2.20.0 does: same parser, core, rule table, quality gate, aggregation, texts and file
formats. Everything below differs on purpose.

## Architecture

1. **Two boards, not one.** The spec's target was a single MCU that both measured and
   decided. On this instrument the FK743M1 display board (STM32H743) replaces the Jetson, and
   the ITDS measurement board stays as it is, on USART1 at 57600 8N1. The measurement numerics
   (spec §3, hard constraint 1) are therefore unchanged by construction. Every point also still
   passes through the firmware's own `sprintf` (§4.2), so the quantisation is the wire's own.
2. **Stop** works as in 2.19.0: nothing is sent (the ITDS firmware has no stop; `[S]` starts a
   twelve-second linear sweep), the sweep in progress is discarded, and a sweep that begins within
   3 s of a Stop that found nothing running is discarded too. One difference: 2.19.0 lets a new
   request wait for the stopped sweep's `[E]`; here the Sweep button stays grey until that `[E]`
   (at most ~6 s, or 12 s if it never comes), and the sentence above the buttons says why. The
   effect is the same — no command lands on a firmware that is busy sweeping.
3. **The link counts as up** while bytes (the 1 s `[C]` heartbeat, or points) arrived in the
   last 5 s. On the Jetson it meant "the serial port is open".
4. **Only the rule table decides** (§18.5). The AI-model option and its tab are not carried.

24. **A Sweep is one `[R]` and a patient wait.** 2.20.0 sends `[R]` up to five times, 1.2 s apart,
    and gives a calibration 10 s to begin. Here `[R]` is sent once, as the previous display
    firmware of this instrument did; the wait is 30 s and counts on the screen; a sweep that
    begins late is still taken; no command is sent between the end of a sweep and the next
    heartbeat; nothing is sent unasked; the port is started three seconds after power on. The
    `[T]` probe runs only from Settings → Device → Test. Settings → Device → Monitor shows the
    traffic live.
25. **Records without a card.** The Jetson keeps its records on its own disk. This board keeps them
    on the SD card when one is in, and otherwise on the 8 MB serial flash the previous firmware
    used, as a littlefs volume with the same file names. The previous firmware's data on that chip
    is erased only on the operator's say-so (Settings → Device → Internal memory).
26. **A heartbeat does not keep a sweep open.** In 2.20.0 any line from the instrument re-arms
    the 12 s stale-run timer, a `[C]` included. The measurement unit sends no heartbeat while it
    sweeps, so here only the lines of a sweep re-arm it: a sweep that stops arriving is closed
    12 s after its last point even though the heartbeat has come back.
27. **A sound while a sweep runs.** The Jetson is silent during a sweep. This instrument's voice
    module repeats one recording from the start of a sweep to its end, as its previous firmware
    did (track 7 unless changed in Settings → Sound; "silent" gives the Jetson's behaviour).
28. **Healthy-tissue samples.** Before the lesion is entered, one or more sweeps can be taken
    with the needle in the patient's own normal tissue ("Healthy tissue", on the needle line). They
    are kept with the case (H1, H2, …), shown in DETAILS beside every reading — their median, or
    any one of them — and recorded in `records/tissue_reference.csv` and `records/tissue_sweeps/`;
    they are never judged or counted and `dataset.csv` does not change. The Jetson has no such
    step. Off by default as a requirement (Settings → Needle).
29. **Details for every row of the log.** A tap on a reading (or a healthy-tissue sample) of an
    opened case shows its 22 quantities. On the Jetson the log shows time, IPS, Z and the result
    only.
30. **The spread limit is set on the screen** (Settings → Protocol). On the Jetson it is a line of
    `device.ini`; this instrument has no file to edit.
31. **The rule table alone decides.** 2.20.0 refuses a reading whose Z at 1 kHz is above the
    placement gate (1900 Ω, "tip in fat") before the rules are looked at, calls a reading that no
    row matches by the fallback label ("Low Risk" as shipped), and has a noisy threshold apart
    from the rows. Here there is no gate and only rows name anything: a reading in no row takes
    the nearest row, and a refused sweep is shown as the table's Noisy row. The shipped table is
    the bench's (Noisy / High Risk / Low Risk). Asked for by the people using this instrument.
32. **The IPS tile shows the measured IPS whenever there is one** (dimmer when the reading was not
    answered). 2.20.0 shows a dash unless the reading was answered.
33. **The IPS spread does not make a lesion Invalid while the rules decide**, and an even split
    of High and Low is High. In 2.20.0 a spread over the limit makes the case Invalid, and the
    median of an even count is the mean of the middle two. The main screen is also laid out
    around a large Z–IPS map rather than two charts side by side.
34. **A measurement unit that does not answer is power-cycled by this board** (on mains, at most
    twice a session), and the unanswered Sweep asked again. The Jetson cannot switch the unit's
    supply and only reports the silence.
35. **Needle lots are calibrated on the instrument** (Settings → Lots) against the reference
    needle, and a lot calibration file is carried from device to device by USB. The Jetson has a
    factory lot-file format (`lotcal`) but no procedure on the instrument and no correction from it.
36. **Each needle as three parts** (H7-2.3.0): the lot evaluation and the lot file use a model of
    the needle — its stray capacitance, its electrode surface and its tip size — and a lot can be used
    needle by needle, each needle given an Air check and a Saline check before its case and its readings
    carried to the reference needle's with its own
    model. None of this exists on the Jetson. *(The middle of this item did not survive; reworded from
    the H7-2.3.0 line of VERSIONS.md.)*
37. **Every needle is calibrated before its case** (H7-2.4.0): Air check plus a Saline check in the
    0.9 % (or 0.09 %) saline, and the needle's own model carries every reading to the reference
    needle's, which is stored as parameters in the lot file. Control runs of the reference set the
    repeatability floor of the lot tolerances. Since H7-2.10.0 this is the needle's QC: each lot's limits
    (from its good needles in Settings → Lots) travel in the lot file, a needle outside them is refused, and a
    needle that has not passed both checks gives no output, the pedal's sweeps included. None of this exists
    on the Jetson.
38. **A library of labelled materials** (H7-2.5.0, Settings → Materials): bench sweeps labelled with
    what was measured, and a regularised discriminant classifier that says how much an unlabelled
    sweep's whole curve is like each material (or like none), with its leave-one-placement-out
    quality. An identified sweep can be told what it was and is learnt (H7-2.5.1), and the main
    screen's box under the map shows, after each sweep, the material the curve is most like (it takes
    no part in the verdict). A labelled sweep far from its material's others is set aside by itself
    (H7-2.5.2), kept in the file and shown. Readings of the logs can be tagged with what they were and
    join the library, which goes to and from USB, an import adding to what is there (H7-2.5.3). Every
    reading's sweep is also kept as measured, before any correction (records/reading_raw.txt). A needle's
    saline reference lets sweeps be compared needle-normalised (H7-2.6.0). Learnt in a fraction of the
    time since H7-2.7.2 (the same model). Since H7-2.8.0 also an answer by Z and IPS alone, and a final
    answer of the three together by a second-layer model learnt from placements left out; the sweeps set
    aside are no longer listed on the screen. Since H7-2.9.0 the tab is called Training, asks for the needle's
    number before its sweeps, and Logs tags a whole case. Since H7-2.10.0 Training's needle is QC'd too (Air
    check and a two-sweep saline check, due again by sweeps and minutes), and each check closes the sweeps
    since the one before (confirmed with an interpolated reference, or marked suspect and not learnt). Since
    H7-2.11.0 a sweep of a shorted or bridged tip is refused (in Training and in a case) and such sweeps in the
    library are not learnt and can be deleted. Not on the Jetson.

## Following the spec's recommendations

5. *(No longer a difference: since 2.19.0 the Jetson also records the needle's open-air sweep
   only in `needle_cal_attempts.csv`.)*
6. **One aggregation for screen and log** (§20.1). The log's answer for a case comes from the
   same `Aggregator::combine()` as the measure screen, fed with the recorded tier, probability,
   IPS fit, label and colour of each reading. 2.18.0's log took the lower middle of an even
   median, counted by `usable`, broke label ties alphabetically and skipped the spread check.
7. **The saturation ceiling stays at 999 999 Ω** (§20.2), for parity.
8. **Needle calibration covers range 3 only** (§20.3), reproduced as is.

## Fixes

9. **The EDITED badge.** A correction written to a `patients.csv` whose header predates
   `edited_at` carries it as one extra unnamed cell. 2.18.0 ignored that cell, so the badge could
   never appear for a case committed on the instrument. It is now read.
10. **A sweep that stops arriving** (12 s with no line) is discarded with a message that gives the
    number of points received. 2.18.0 re-published the previous result.
11. **The UART receiver re-arms itself** (§16.12). Overrun, framing and noise errors are cleared
    in the interrupt and counted (shown in Settings → Device and in diagnostics). The
    receiver never stops.

## Workflow

12. *(withdrawn in H7-1.2.5)* Cancel on a new intake form used to drop the case. It now only
    goes back and the case stays open with no patient row, as on the Jetson.
13. *(withdrawn in H7-1.2.5)* A tap on the case strip of an open case used to open its details. It
    now mints a new case id, as on the Jetson; details are corrected from Logs.
14. **Clock.** The battery clock (DS1307) is set in Settings → Device → Date and time. A UTC offset
    is kept, so `epoch_ms` and the ISO times in the records stay UTC as on the Jetson, while the
    screen and file names use local time. If the clock cannot be read, or reads earlier than the
    build date, a red line on the measure screen says so (§16.14).

## Hardware that differs

15. **Sound.** A rule's "sound" names a track (1–12) on the DY voice module, whose recordings are
    stored on the module itself. The shipped table assigns none, so the instrument is silent
    until a site chooses, as in 2.18.0. Mute is kept in the settings memory.
16. **Settings** live in the 2 KB EEPROM, not in `device.ini`: two copies, a CRC, and a read-back
    after every write. The rule table is limited to 12 rows and 6 case rules so that it fits.
    If an edit would not fit, saving is refused and the refusal gives the byte count.
17. **Export** goes to a USB stick through USB host, as on the Jetson. The export folder and file
    names are unchanged.
18. **Not carried:** the serial monitor, port finder and baud setting (there is one fixed port);
    the screen orientation setting (the panel is fixed landscape); lot calibration files and
    factory access (§13.5, out of scope); the printer; the Margin / Lymph / Mass modes of the
    previous display firmware (removed at the owner's request).
19. **Added:** a battery indicator and power key, as the previous display firmware had them, and
    a screen brightness setting.

## Diagnostic mode and health log (2.19.0 / 2.20.0)

21. **Diagnostic mode** records the same things as 2.19.0 (every tap and whether the control
    was live, with the blocker for a grey one; screens; commands and received lines, heartbeats
    counted; settings written; intake fields; calibration; export; pathology; health lines), and
    hides the same things (passcode, dials, keypads, national ID, notes). Lines are written to the
    card every 0.5 s instead of one by one, so a sudden reset can lose the last half second. The
    file name is remembered in the EEPROM, so the mode survives a restart as in 2.19.0. There is
    no Qt warning stream to capture; the application's own events go into the file instead.
22. **The health log** (`records/health.csv`, every 10 s) has the columns a microcontroller can
    give: uptime, heap used and free, screens drawn per second and the time to draw them, the
    longest pass of the main loop, UART errors, battery, mains, SD, USB, running, diagnostic mode.
    There are no temperatures, load average or swap. How the previous session ended is decided
    from the power-key marker and the processor's own reset flags (power-on, brown-out, reset
    line, watchdog, or a fault — with the fault registers), which is more direct than the
    Jetson's uptime comparison.
23. **The chart repaint change of 2.20.0 is not needed here**: this screen has always been redrawn
    only when something on it changed (a point, a verdict, a touch, a timer), never on a timer.

## Rule editor

20. The fallback label and colour, and the noisy label and colour, can now be edited on screen,
    as §7.7 asks. 2.18.0 had only the noisy threshold on screen. Rule rows can be moved up and
    down, switched on and off, and deleted.
