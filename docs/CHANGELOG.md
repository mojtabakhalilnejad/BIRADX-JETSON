# Changelog

Each firmware file carries its version in its name (`biradx-h7-<version>.hex`). The version is
also shown on the top line of Settings, in Settings → About, in every diagnostics file and on the debug
port (USART6) at start. `docs/VERSIONS.md` lists every release with the checksum of its file.

## ITDS-2.16.0 — 2026-11-02 — The screens upright, on an upright monitor

On the ITDS computer the monitor stands upright; the 1024 × 600 frame was drawn small across the
middle of it, with black above and below. The screens now have a second layout, upright, and the
program chooses it by the monitor's shape (`[screen] layout=auto` in `itds.ini`; `portrait` or
`landscape` to force one). Upright the frame is 600 wide and as tall as the monitor's shape makes it
(960 to 1100: 1024 on a 600 × 1024 panel, 1067 on 1080 × 1920), so the picture fills the monitor.

- **Measure**: one column — the case, the needle, the answer (larger), IPS and Z with DETAILS, the
  progress, the IPS/Z map and under it the phase chart (or the material), and the buttons at the bottom.
- **Settings**: the tabs on two rows; a row whose control is wide puts the control under its text,
  across the row; the rules' cards on three lines; the case rules' sentence on two lines; the sweep's
  track chips on two rows; About's long lines wrap.
- **Logs**: the filters and the six buttons wrap (two rows of three); an open case's sentence across
  the row with its three buttons under it.
- **Keyboard, number pad, clock, passcode, lock**: keys fitted to the width (and taller); the clock's
  date on one row and the time under it; the dials in the middle of the screen.
- **Sheets** (DETAILS, the checks, healthy tissue, pathology, case details, delete, all files, link
  monitor, training data, tag, lots, a lot's tests, result, comparison and raw sweeps): fitted to the
  width — one column of 22 values in DETAILS, the lot's panel under its grid, two lines a needle or a
  sweep in the lot tables.
- **All files** on the ITDS computer: says where the records are on its disk (the programming cable's
  section was the display board's).
- The frame chosen is said at start (`biradx-itds: screen 1080x1920 … drawn upright, 600 x 1067`).

The landscape layout is unchanged: every screen of the simulator's scripts, and every screen and sheet
of a script that opens each of them, is pixel-identical at 1024 × 600 to H7-2.15.0's. The display
board's version stays H7-2.15.0 (its image differs only by the code of the upright layout, which a
1024 × 600 panel never runs). `BIRADX_SIM_PORTRAIT=1` (or a height, 960–1100) runs the simulator
upright; its `show <screen>` command opens any screen or sheet directly.

## ITDS-2.15.0 — 2026-10-29 — The same source on the ITDS computer

H7-2.15.0 built for the ITDS instrument's own computer (the Jetson, Linux) in place of the display
board: `itds/` (see `itds/README.md`). The application, the decision core, the screens and the fonts
are compiled as they are; what differs is the platform (`itds/platform_itds.cpp`: the serial port,
the files, the clock, the recordings as WAV files played with aplay, the USB stick as the desktop
mounts it) and the window (`itds/main_itds.cpp`: the 1024 × 600 frame scaled to the monitor, touch
and mouse). Sweep is sent as the ITDS software 2.22 sent it (`[serial] commands=burst`): the frame
six times, just after a heartbeat, again at the next heartbeats with two alignment bytes until the
sweep begins; Stop cancels it. A first start next to the previous ITDS software takes its device ID,
passcode, needle revision and last case id. In the shared source, the lines that differ are marked
`BIRADX_ITDS`; the display board's image built from it is byte-identical to H7-2.15.0's.

## H7-2.15.0 — 2026-10-29 — The QC by the bench's records (records_7)

From the analysis of records_7 (27–29 October): needle V1.1 wore out over two days (its leakage 8 → 95 µS) and
passed the generic 100 µS all along; a sound needle reads ±4 % from one dip in the saline to the next, more than the
3 % the bracket allowed (10 sweeps of the sound V1.2 marked SUSPECT for it); a saline check of 43.7 Ω passed the
generic 30–1500 Ω and readings followed; a resistor of 1 kΩ and of 3 kΩ on the bench was taken as readings (one High
Risk, others Low Risk); the temperature was 22.0 °C in every check.

- **Air check**: the generic leakage limit is 40 µS (was 100; the needles and cables on the bench measured 7 to 37
  µS dry). And each needle against its own: its lowest Air check on record (needle_cal.csv) is the needle sound;
  above three times that (10 µS more at least) the check is refused, "wet or worn".
- **Saline check**: the generic R window in 0.9 % saline is 90–250 Ω at 25 °C (was 30–1500; the needles on the bench
  read 110–162 Ω). A lot's own limits, when chosen, are unchanged.
- **The bracket** (two checks around Training sweeps): R within 8 % (was 3 %); the tip surface within 20 % as before.
- **The suspect sweeps judged again** at start by these rules: learnt when R held within 8 % between the checks
  around them and the needle's leakage was within its limits. Sweeps marked before H7-2.12.6 (no record of how far
  the needle moved) are judged from saline_check.csv; they stay suspect when a check was refused between, or an
  Air check came before their closing check (the tip may have been cleaned for a fault). From now on a sweep marked
  for a fouled tip or a refused check says so (and stays). On records_7: 20 of 55 learnt (cola 27 Oct, saline V1.2
  29 Oct); coffee stays (an Air check before its closing check), the worn V1.1's stay.
- **No tip surface, no reading**: a sweep with no electrode interface in it (reactance at 500 Hz–1 kHz below 30 % of
  the needle's own tip surface, 20–60 Ω; 50 Ω when unknown) is a resistor, a wire or the connector, not a material:
  refused as a reading, as a healthy-tissue sample and as a Training sweep, nothing counted, the needle's checks stand.
  So is a sweep of one value repeated at five frequencies or more (not measured). On records_7: the 16 such
  sweeps and none of the other 121; records_6: only its shorted tips.
- **Saline temperature** kept across a power cycle (Settings); until it is entered after power-on the button reads
  "Temperature 22.0 °C ?" with a line saying that 1 °C off moves R by 2 %.
- Tests: app_test 326/326 (325 on the emulated Cortex-M7), selftest 345/345, nor_test 38/38.

## H7-2.14.1 — 2026-10-29 — The Identify tests in their own table

Asked about on the bench: sweeps identified with "Sweep it and compare" and then told what they were did not show
in the table under "How well it tells them apart". That table is the classifier's own test, each LEARNT placement
left out in turn. An identified sweep joins it only once it is learnt: after the needle's next saline check has
confirmed it (QC), and only for a material with two learnt sweeps. Sweeps marked SUSPECT never join it (55 on the
bench). The first line counted every sweep of the learnt materials as "learnt from".

- **IDENTIFY TESTS**, a card of its own under the classifier's quality: every sweep identified first and labelled
  after, as it was said at the time (the final answer when it was given), whether learnt yet or not. It shows how
  many were right, and a table of what each material was (rows) against what was said (columns, "(none)" when it
  was like none of them).
- "Learnt from N of the M sweeps of K materials (the others are not learnt: QC, below)": N is what the classifier
  learnt from.
- The table above is headed "learnt, left out: was ↓ said →". "How often" no longer speaks of the settling
  (gone in H7-2.13.0).
- Tests: app_test 309/309, selftest 345/345, nor_test 38/38; the cards checked on the simulator.

## H7-2.14.0 — 2026-10-28 — The case result by count, and a final case closed

Asked for from the bench: Settings → Rules should set how many Low Risk and how many High Risk readings make a
lesion's final answer Low Risk or High Risk. Also: check that the setting for "one High Risk closes the case, and
no more tests are taken" works.

That setting did not do it. Protocol → Stop early on a positive ends a case only once the MEDIAN of at least
three readings is high ("Not before this many readings", 3), not on the first High Risk reading. The case rules of
Rules ("if at least N readings said X, the lesion is Y") went through the same three-reading early stop. And a
finished case still took readings: its main button read "New reading anyway", and a sweep from the pedal was read
too.

- **Rules → THE CASE RESULT → High Risk from / Low Risk from** (0 = off, 1 to 20): the lesion is High Risk, and
  final, once at least that many of its valid readings count as high (their row's level); else Low Risk once at
  least that many count as low. High is tested first. Until then the lesion is provisional (the median so far),
  and the result line says how far each count has come ("So far 0 high, 2 low"). At the quota without either, the
  case rules and the median decide, as before. The words are those a reading of that level carried. The log and
  the dataset's model_version name the counts (…/h1l3).
- **Rules → Close the case once final** (on): once the lesion's answer is final (a count reached, the quota, or
  Stop early on a positive), the case takes no more readings. The main button becomes End case, the screen says
  why, and a sweep from the pedal is no reading. "High Risk from 1" with it on is "one High Risk closes the case".
- Stored in the settings memory (a `cc=` line); settings saved by earlier versions read as counts off.
- Tests: selftest 345/345 (counts: one high, two and three low, high first, closing off, counts off); app_test
  309/309 (a case closed at its first high reading; a final case refusing a pedal sweep); nor_test 38/38.

## H7-2.13.0 — 2026-10-28 — Training without the wait: a needle sweeps from its first passing check, and its checks are done where the sweep is

Asked for from the bench: the Air check and the saline check passed in Training, and the material's card still
would not sweep. Training was slow and hard to follow. Two things held it back:
- a new tip had to settle (two saline checks, 2 minutes apart, agreeing within 20 %);
- each check (the first ones, and the one due every 10 sweeps) was done in the QC card at the top, far from the
  material's card.

- **No settling wait.** A needle sweeps from its first passing saline check on. Since H7-2.12.6 a tip surface that
  moves between two checks no longer condemns the sweeps between: they are learnt as measured, only not
  needle-normalised. So waiting for a new tip to settle taught nothing more. The message says what a new tip's
  first half hour may change. ("Sweep without waiting" is gone with the wait.)
- **The check, where the sweep is.** When the needle's check comes first, the material's card offers it in place
  of its Sweep button: "Air check first", "Saline check first", "Saline check (due)", or "Enter the needle's
  number". Identify does the same. The check's sheet opens, the Air check goes on to the saline check by itself, and
  once the saline check passes the sheet closes back onto the card, which then sweeps. The QC card still does it
  all as before.
- A Training saline check sheet says "Passed" only for the check just taken. Before, a check due or "Not a check"
  could leave an earlier pass standing on it.
- Tests: app_test 300/300 (the next step offered: Air, then saline, then a due check; a needle sweeping from its first
  check), selftest 339/339, nor_test 38/38; the card → Air check → saline check → back to the card flow checked on
  the simulator.

## H7-2.12.6 — 2026-10-27 — A check that passes, and sweeps that are suspect: said apart; only R condemns

Asked about on the bench: the Air check and the saline checks passed, and still 20 sweeps (coffee and cola) were
marked SUSPECT. Both were right, but the screen did not say how. A check passes by itself: the needle is within
its limits now. It also compares the needle with its check before. When the needle changed between the two, the
sweeps between were marked suspect, and a new tip's surface changes by more than 20 % in its first half hour.

- **Only R condemns.** The sweeps between two checks are SUSPECT, and not learnt, when the needle's R moved by
  more than 3 %. When R held but the tip surface moved by more than 20 %, they are now confirmed and **learnt as
  measured**, though not compared needle-normalised: their reference between the two checks is not known
  (`qc=ok-raw`). This follows the bench of 20 October, where every sweep condemned for its tip surface alone was
  told right as measured. Sweeps already marked suspect stay so (Suspect sweeps → Learn them anyway).
- **Said apart.** A check that condemns the sweeps before it says "This check itself is within the limits, so the
  needle can go on from here. But the needle's R moved X % since the last check: the N sweeps since then are
  SUSPECT". The settling text names an R that moved, not only a tip surface. Each closed sweep keeps how far the
  needle moved around it (a 24th field of materials.txt, which older versions skip).
- Tests: app_test 300/300 (a tip surface that moved: learnt as measured only; an R that moved: suspect, the
  message saying the check itself passed); selftest 339/339, nor_test 38/38.

## H7-2.12.5 — 2026-10-27 — Why a library of sweeps is not learnt, and its suspect sweeps learnt by hand

Found on the bench with H7-2.12.4: three materials with 10 to 11 sweeps each, and "Sweep it and compare" still
said "Label at least two materials, with at least two sweeps each". The sweeps were there, but none could be
learnt. A new needle's next saline check marks the sweeps before it SUSPECT when its tip surface moved more than
20 %, which a new tip does in its first half hour. The message named none of that.

- When the library is not learnt, the reason now says which sweeps cannot be learnt and why: suspect, waiting for
  a check, of a faulty tip, of a material left out, set aside by hand, or lacking some of the 15 frequencies. The
  reason shows in the Identify card, the notice of a grey button, and the main screen's line.
- **Suspect sweeps → Learn them anyway** (two taps), in Training under the classifier's quality. The suspect
  sweeps are learnt by hand; a sweep of a faulty tip never is. On the bench of 20 October the sweeps so marked were
  told right all the same: their R had stayed within 3 % while the tip surface moved. The classifier's own check
  still sets aside any far from its material.
- Tests: app_test 298/298, selftest 339/339, nor_test 38/38.

## H7-2.12.4 — 2026-10-27 — The tip's settling: no loop, and a way out

Found on the bench with H7-2.12.3: the QC card stayed at "Two checks agree, but less than 2 minutes apart"
through check after check. The 2 minutes were counted from the last check, so each check taken early started
them again.

- The 2 minutes are counted from the first of the run of checks that agree with each other, so a check taken
  early no longer starts them again. Once they are over, one more check that agrees lets the needle sweep. The
  card and the message say when, by the clock. The start of the run is kept on record too
  (`training_qc.txt`, a ninth field; files of H7-2.12.0–2.12.3 read as before).
- **Sweep without waiting** (two taps), in the QC card while the tip settles, is the way out by hand: the needle
  sweeps at once. Its next saline check still closes those sweeps: if the tip moved more than 20 % by then, they
  are marked suspect and not learnt. The event log records that the settling was skipped.
- Tests: app_test 296/296 (8 new: a check repeated early, the run counted from its first check, the way out, and a
  sweep after it condemned by the next check); selftest 339/339, nor_test 38/38.

## H7-2.12.3 — 2026-10-27 — A sweep under way fills a line round its box

Asked for from the bench: wherever a signal is being taken, the box it is taken in shows the sweep's time going
on.

- A line runs clockwise round the box from its top-left corner as the sweep goes on: bright where it has come,
  dim for what is still to come, with a dot at its head. A settling before the sweep fills the same way in amber;
  the sweep itself is blue.
  - The main screen: a reading's sweep, round the phase chart, where its curve is drawn.
  - The sheets (Air check, saline check, healthy tissue, a lot test): round the screen. The saline check fills
    half for each of its two sweeps.
  - Training: round the status card, and round the card of the material being swept (or the Identify card).
- How far a sweep has come is from its points, which arrive one by one (15 a sweep). Between two of them it
  moves on by the mean time a point has taken so far (App::sweepProgress), redrawn five times a second while it
  runs.
- Tests: app_test 288/288, selftest 339/339, nor_test 38/38; each place checked on the simulator.

## H7-2.12.2 — 2026-10-27 — A grey button, tapped, says why it is off and what to do first

Asked for from the bench: a control drawn grey did nothing when tapped, and said nothing either.

- Every control that can be off now says, when tapped while it is off, in a short notice at the foot of the
  screen (up to three lines, five seconds): why it is off now, and what to do first. For example:
  - Training: "Sweep it: a new placement" gives the reason of the QC ("Air check this needle first…").
  - The main screen: Sweep says which of the needle's checks is missing.
  - The checks' sheets give the reason shown in their own warning.
  - The intake form says which fields are still needed.
  - The USB buttons say that no stick is mounted.
  - A button off while a sweep runs says to stop it or let it finish.
  - Steppers say they are at their limit.
- The toolkit: `Ui::why(id, text)` gives a control its reason for the frame. A control without a reason says
  "Not available at this moment." The notice line (App::setToast) wraps over up to three lines.
- Tests: app_test 288/288, selftest 339/339, nor_test 38/38; Training and the main screen checked on the simulator.

## H7-2.12.1 — 2026-10-27 — The main screen says why it shows no material

Found on the bench, on the first reading under H7-2.12.0: the box under the Z–IPS map drew the phase curve
alone, with nothing to say why it showed no material. It shows a material only when Training's library is
learnt, which takes two materials with two learnt sweeps each. The library on the internal memory had one sweep
(the 481 sweeps of records_6 are on the SD card), and since H7-2.12.0 a sweep is learnt only after the needle's
next saline check confirms it.

- With "Show it on the main screen" on, a line above the phase curve says why no material is shown: how many
  of the two materials needed have two learnt sweeps, how many sweeps wait for a saline check, or why this sweep
  cannot be compared.
- Tests: app_test 288/288, selftest 339/339, nor_test 38/38; the main screen checked on the simulator.

## H7-2.12.0 — 2026-10-27 — Training's QC: a new tip settles first, a check of nothing counts for nothing, a sweep is learnt once confirmed

From the bench records of 20 October (records_6: four agars and their control gel, needles 1.6–1.10). The
classifier was not the problem. Placements left out, it told the four agars apart 98–99 % of the time, and 95–98 %
for a needle it had never seen. The QC flagged 20 sweeps as suspect. Only 9 of them were really off (needle 1.6,
its new tip not settled). The other 11 were good: they were lost to a new tip settling (its surface 1169 → 273 Ω in
4 minutes on needle 1.10, 843 → 374 Ω over 40 minutes on 1.8) and to a closing check taken with the tip out of
the saline. Meanwhile 3 bad sweeps were learnt: needle 1.7 was never closed by a check, and its Air check later
failed with a 300 µS leak.

- **A new tip settles first (Training).** A needle sweeps once two saline checks, 2 minutes apart at least, agree:
  R within 3 %, tip surface within 20 % (the bracket's limits). The first check of a needle, a check after one
  that was refused, and a check after a faulty tip all need the second one. The QC card counts down to it. A
  needle's later checks that agree with its last keep it settled, also after another needle or a restart.
- **A check of nothing is no check (Training and the main screen).** A saline check with the tip out of the
  saline (the sweep a capacitance alone) or shorted (|Z| flat at the floor) says "Not a check" and asks for it
  again. It counts neither for nor against the needle: no sweep is marked suspect, nothing is reset. It replaces
  the "model residual nan" refusal. On records_6 it finds exactly the five such attempts, and none of the other 63.
- **A sweep is learnt once a check of its needle confirms it.** Sweeps since a needle's last check are not learnt
  until its next check closes them. Each needle's last passed check is kept on record
  (`records/training_qc.txt`, kept by Delete logs), so the next check of that needle closes them also after
  another needle was used or the device was restarted. Switching needles says which sweeps wait. Sweeps no check
  can close (pending from earlier versions) are shown as **Unconfirmed sweeps**, with "Learn them anyway" (two taps).
- **A material can be left out of the comparison** (its label's card: a switch). Its sweeps are kept but not
  learnt, and it is never an answer; its chip says "off". All materials share one spread: on records_6 the agars
  alone are told apart 98 %, but only 92 % among all 13 materials.
- **The main screen** is unchanged in what it asks. It now records the needle's tip in `records/needle_tip.csv`:
  at each check and each reading, the tip surface at 1 kHz, R at 25 °C, the time since the check, and the time
  since the needle's first sweep in the saline. This shows what a new tip's first minutes change in Z at 1 kHz,
  before anything about it is decided.
- Tests: app_test 288/288 (25 new: settling, the tip out of the saline, sweeps confirmed by the next check, after
  another needle, after a restart without the record, a material left out), selftest 339/339, nor_test 38/38.

## H7-2.11.2 — 2026-10-26 — Training says why its QC buttons are off

Found on the bench with H7-2.11.1: in Settings → Training "Air check again" and "Saline check" were both grey, with
nothing on the screen saying why. They are off while a case is open (Training's sweeps and checks are not patient
readings), while a sweep or saline check runs, and until the needle has its number; the status card showed an
older message in place of "A case is open".

- The QC card says, under its two buttons, why they cannot be pressed: the open case (with its id: end it on the
  main screen), a sweep or saline check under way, no needle number, or what holds back the saline check alone.
- The status card ("what is going on now") puts an open case before an older message.
- A Training sweep or a saline check whose unit stays busy after the settling, or between the check's two sweeps,
  is stopped after 30 s with a message, where it waited without end (and kept the buttons off).
- "Sweep it and compare" (Identify) is unchanged: it needs two labelled materials with sweeps from two
  placements each, and the needle's QC passed; the card under it says what is missing.
- Tests: selftest 339/339, app_test 263/263, nor_test 38/38; the open-case screen checked on the simulator.

## H7-2.11.1 — 2026-10-26 — The internal memory of a board with a 2 MB chip

Found on the bench, on the first board to run the reconstructed source: Settings → Device → Internal memory said
"not found — no answer from the memory chip (id EF4015)". The chip did answer: EF4015 is a Winbond W25Q16, 2 MB,
where the boards until now carried a W25Q64 (8 MB, EF4017), and only an 8 MB chip was taken.

- The size of the chip is read from its identity (its last byte is the capacity as a power of two): 2, 4, 8 and
  16 MB chips are taken, at their own size (littlefs on all of it; reads and writes past its end refused). A 1 MB
  chip is still not taken.
- The screen says the size the chip has (Settings → Device → Internal memory; the "Use it…" sheet, with how many
  readings it holds, about 190 a megabyte). The copy of its old contents to a USB stick is the whole chip, as
  `old_internal_memory.bin`.
- On the internal memory the health log keeps at most a thirty-second of the chip in each of its two files (256 kB
  on 8 MB, as before; 64 kB on 2 MB).
- A chip that holds the previous firmware's data is, as before, left alone until "Use it…" (which offers the copy
  to USB first, then erases it).
- Built from the reconstructed source of H7-2.11.0 (branch `reconstruct-2.11`; see RECONSTRUCTION.md), whose
  image is not byte-identical to the released 2.11.0 file.
- Tests: the chip test on the PC (tests/nor_test.c) also with a W25Q16 — found, taken at 2 MB, nothing read past
  its end, a volume made, kept through a restart — and a 1 MB chip refused: 38/38.

## H7-2.11.0 — 2026-10-19 — A shorted or bridged needle tip refused, and its sweeps taken out of the library

Found in the bench records of raw and boiled potatoes with two needles (records_3): of 110 potato sweeps only 23
were of the potato. In the others the tip's two electrodes were joined — 24 sweeps read the instrument's floor
(|Z| flat at 76.1 Ω from 500 Hz to 50 kHz, on both needles and both potatoes alike), 63 read lower at 500 kHz
than the same needle in 0.9 % saline, which no potato does — and the Air check of needle 1.2 at the end, in air,
read 72–262 Ω ("something is bridging the tip"). Residue (starch, boiled-potato paste) between the electrodes:
needle 1.1 from its eighth sweep on (all its boiled sweeps), needle 1.2 from 11:01. The library learnt those as
"potato raw" and "potato boiled", and its own check set aside six of the seven good raw sweeps of needle 1.1 as
far from the rest of their label. The good sweeps show what was expected: raw potato a β-dispersion (|Z| falling
6–14× from 1 kHz to 500 kHz, phase −30 to −52° at 175 kHz), boiled potato a resistance above 5 kHz (phase −1°),
which the whole curve tells apart and IPS does not.

- **Each sweep's tip checked** (`matTipFault`). *Shorted:* |Z| within 4 % from 500 Hz to 50 kHz and below
  150 Ω — a tip in any electrolyte rises at the low frequencies, so a flat curve has no material in it.
  *Bridged:* |Z| at 500 kHz below 80 % of the resistance the same needle read in 0.9 % saline (Training: its
  saline check; materials at room temperature), or below 65 % of its saline check's R at 25 °C in a case (tissue
  at 37 °C conducts about 1.24 × as at 25 °C; judged only after a check in the 0.9 % saline).
- **Training:** such a sweep (labelled or Identify) is not kept; the sweeps since the needle's last saline check
  are marked SUSPECT (when the tip fouled is not known) and both checks are due again. The message says which,
  with the numbers, and to clean the tip (rinse, wipe, dry).
- **A case:** such a sweep is no reading (nor a healthy-tissue sample), from the screen or the pedal; the needle's
  Air check and saline check are cleared and must be done again before it measures.
- **The library:** sweeps of a faulty tip already in it — from earlier versions, or imported — are not learnt,
  counted in the quality card (TIP), and **Sweeps of a faulty tip → Delete them** (two taps) takes them out of
  `materials.txt` for good. On the library of records_3: 86 found (24 shorted, 62 bridged), 310 → 224 sweeps.
- Tests: the check on a flat floor, a bridged curve, the saline and a tissue-like curve (selftest); a shorted
  and a bridged tip in Training and in a case, and a library with such sweeps imported and cleaned (app test).

## H7-2.10.0 — 2026-10-19 — Needle QC: every needle checked against its lot's limits; Training bracketed by checks

Asked for from the bench: (1) every needle QC'd with the Air check and a normal-saline check, refused outside
the normal range and no output possible from it; (2) the acceptable range of each lot worked out from its own QC
tests in Settings → Lots and carried to every device; (3) in Training, the Air and saline checks not once only
but repeated, as is standard: a saline check every 10 sweeps or 30 minutes, an Air check every 60 minutes and at
a needle or cable change, each check closing the sweeps since the one before.

- **Each lot's QC limits, in the lot file.** When a lot is put in the lot file (Result → Put in the lot file) its
  good needles set its limits: the Air check's stray capacitance from the smallest to the largest of theirs,
  widened by 10 % of their mean (at least 3 pF); leakage below 3 × the largest of theirs (at least that + 20 µS);
  the tip's exponent in each saline within ±0.15 of theirs (at most 1). The saline limits are the lot's windows
  already in the file (R at 25 °C and tip surface at 1 kHz). A new line per lot, `Q=lot|cpMin|cpMax|gpMax|αmin|αmax|α9min|α9max`,
  inside the file's check sum; a file without it reads as before, and an older device skips the line. A lot
  not in the file is checked against generic limits (Air 20–400 pF, leakage < 100 µS; 0.9 % saline R 30–1500 Ω,
  0.09 % 300–15000 Ω, tip surface 20–3000 Ω, exponent 0.50–1.00) — as before, within 30 % of the reference
  needle's tip and a factor four of its surface when the file has the reference. Lots → Compare says them
  (paragraph 5).
- **Every needle before a case** (Settings → Needle → Needle check before each case → Every needle, the
  default): the Air check is refused outside its lot's limits (no Air check is then in force), the saline check
  too, and so is a saline check whose two sweeps differ by more than 3 % or that does not look like a tip in
  saline (saying, as 2.6.2 did, whether the Air check or the immersion is at fault). Until both pass the needle
  gives **no output at all**: the Sweep and Reading buttons are off, the healthy-tissue sample is refused, and a
  sweep asked for by the pedal or the front button is not decided, shown, counted or recorded as a reading —
  the screen says "That sweep is not a reading" and the event log keeps it; not even its curve is drawn. Where
  the lot asked for it, the saline check still carries the readings to the reference needle's with the needle's
  own model; elsewhere it is a QC only and changes no reading. "Off" is for the bench only: no QC, a needle
  never refused. A needle checked in Training is checked again for a case.
- **Training: the needle's QC, bracketed.** The new card QC OF THIS NEEDLE: its Air check (against the chosen
  lot's limits or the generic ones) and its **saline check** — two sweeps in 0.9 % saline at the saline's own
  temperature (entered, or its conductivity), R at 25 °C, tip surface, exponent, the two sweeps within 3 % — in
  place of the one-sweep saline reference. No sweep, labelled or Identify, until both pass. A saline check is due
  again after 10 sweeps or 30 minutes, an Air check after 60 minutes (How often, below; kept in the settings
  memory); a new Air check asks for a saline check after it. **Each saline check closes the sweeps since the one
  before:** the needle found as it was (R within 3 %, tip surface within 20 %) → they are confirmed and each gets
  the reference interpolated between the two checks by its time; the needle moved → they are marked SUSPECT and
  not learnt; the check refused (the needle out of its limits) → they are suspect and the needle must not sweep;
  only unsettled (the two sweeps apart) → repeat it, they wait. The sweep's line in `materials.txt` carries its
  mark (field 23: pending, ok, suspect; older lines read as before). The quality card counts suspect sweeps and
  those never closed (the needle changed or the device was switched off before the next check: learnt, not
  confirmed). The lot of the Training needle is chosen on the card ("No lot" = generic limits).
- The Air check and saline check sheets say whose they are; after Training's Air check the saline check follows.
- Tests: the QC limits and their lot-file line (selftest), and on the whole application a refused Air check,
  a refused saline check, a pedal sweep from a needle without its checks, the checks due by sweeps and by
  minutes, a bracket confirmed with its interpolated reference, a bracket made suspect and kept through a restart,
  and a lot's own limits refusing a needle the generic ones would pass.

## H7-2.9.0 — 2026-10-19 — Training: the needle's number before every sweep; Logs: one tag per case

Asked for from the bench: rename Materials to Training, with a place for the needle's number before each sweep;
and in Logs, a label for the whole case rather than for each reading, since a case examines one lesion from
several placements. (The analysis of the library read by the cable had shown two needles or cable set-ups in
use, ~124 Ω and ~150 Ω in 0.9 % saline, with no needle number on any of the 197 sweeps.)

- **Settings → Training** (was Materials). At its top, **NEEDLE**: the number of the needle the sweeps are taken
  with (kept in the settings memory). No sweep, Identify or saline reference is taken there until it is
  entered; every sweep is kept with it, the Air check taken from Training records it, and the saline reference
  belongs to it. **Another needle** changes it: an Air check or reference taken with another needle is then not
  applied to this one's sweeps (the card says so and asks for its own); numbers are compared without regard to
  case. An Air check taken before numbers were asked for is taken as the needle's own, and the card says so.
- **Logs → a case → + Tag the case** (was a "+ tag" on each reading). Every reading of the case that passed its
  checks joins the material library with the label, its sweep as measured, all of them **one sample**: the
  quality test leaves the whole case out together. Choosing another label changes all of them; Remove the tag
  takes all of them out. Readings already tagged one by one are brought under the case's label; a case whose
  readings carry different tags shows "TAG: mixed". The case row shows its tag; the message says how many
  readings were learnt, how many were left out (not passing their checks, or without a complete sweep), and what
  the classifier had said of them before learning them.

## H7-2.8.0 — 2026-10-18 — Materials: an answer by Z and IPS, and a final answer of the three together

Asked for from the bench: (1) take the set-aside part out of Materials; (2) learn Z and IPS as a
classifier of their own, show it with the whole curve and the blend on the main screen, and learn a second
model from the three that gives the final answer, in place of the curve drawn on their left.

- **Set aside, no longer shown.** Materials no longer lists a label's sweeps with their distances, the SET ASIDE
  marks and the Use it anyway / Set aside / Back to automatic buttons; the label card no longer counts them, the
  chart no longer draws them dashed, and the messages after a sweep no longer mention them. The check itself
  still runs as before: a sweep far from its material's others is not learnt, is marked in `materials.txt`, and
  the event log says so.
- **By Z and IPS.** The two numbers the instrument has always shown — |Z| at 1 kHz and the firmware's IPS,
  1.43 × (phase at 500 kHz − phase at 100 kHz), of the sweep as measured — learnt by themselves: each material its
  mean, its own spread of the two and their correlation (a regularised quadratic discriminant), shrunk toward
  the spread they share by an amount chosen, with the temperature, from the placements left out (a material
  whose IPS varies widely keeps that width; one with few placements borrows the others'). Floors 2.3 % of Z and
  0.4 IPS; "like none" beyond the 99.9 % point scaled as for the curve; a share of doubt from its own left-out
  errors.
- **The final answer.** A second-layer model learnt from the three answers each left-out sweep received from
  models made without its placement: log p(c) = w₁ log q_curve(c) + w₂ log q_blend(c) + w₃ log q_Z,IPS(c), q
  each answer's probabilities with 5 % spread evenly (so that no one answer alone can rule a material out), the
  weights ≥ 0 those that best predict the left-out sweeps (Newton's method), drawn toward 1 : 0.5 : 0.5 while
  they are few. An answer that does not help gets a weight near 0. A share of doubt from its own left-out errors.
- **Main screen**, the box under the map: on the right BY THE WHOLE CURVE, BY THE BLEND, BY Z AND IPS (with the
  sweep's Z and IPS), each with its per cent and material; on the left, where the curve was, **FINAL · THE THREE
  TOGETHER** with its per cent, and how many left-out sweeps it said right.
- **Materials:** Identify gives the final answer first, then Z and IPS, then the whole curve's per cent; the
  quality card gives each answer's left-out result (whole curve, blend, Z and IPS, final, and the weights). A
  sweep identified and then labelled also keeps what the final answer had said (three new fields at the end of
  each line of `materials.txt`; older lines read as before), and the card counts those too.
- The blend is now solved exactly (an active set: the materials taking part found one at a time, and the best
  blend of them one small linear system), about its materials' own mean; until now an accelerated gradient
  that, with materials far apart, could stop before the end. Learning with the three answers takes about twice …

  *(The rest of the H7-2.8.0 entry did not survive; see `docs/VERSIONS.md`.)*

## H7-2.7.2 — 2026-10-18

The HAL tick at its real 1 kHz (it ran at 2 kHz: every settling time, timeout and interval was half its length), with a Timer check in Settings → About; heap_used_kb right; the D-cache on for the heap, the classifier learnt in a fraction of the time and a sweep added appended to materials.txt — no more seconds-long stops in Materials.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.7.1 — 2026-10-18

The programming-cable read writes to the user's Downloads folder; the sheet says to type its address and length as shown.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.7.0 — 2026-10-18

Logs → All files: to a USB stick, or by the programming cable — the records as a ZIP archive in SDRAM (1.5 MB parts) with the STM32_Programmer_CLI command that reads it while the program runs (mode=HOTPLUG -u).

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.6.2 — 2026-10-18

An Air check above 300 pF warned of; a refused saline reference finds the capacitance the sweep really has and says whether the Air check, the immersion or the needle is at fault, draws the sweep, and records every attempt.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.6.1 — 2026-10-18

The Air check always on offer on the needle strip (not applied to readings when the rules were found without it), going on to the material library's saline reference; an Air check button in Materials.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.6.0 — 2026-10-18

as a ratio to the saline's), with a classifier of its own; gain-range mismatches flagged on Identify.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.5.3 — 2026-10-18

Logs: each reading tagged afterwards with what it was joins the material library (its sweep as measured, kept for every reading); the training data exported to USB from Logs or Materials and imported on another device, added to its library with nothing deleted.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.5.2 — 2026-10-18

Materials: a labelled sweep far from its material's other sweeps is set aside by itself (not learnt), kept in the file marked "set aside" and shown under its label with its distance; Use it anyway / Set aside / Back to automatic.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.5.1 — 2026-10-18

An identified sweep told what it really was is learnt (with what the classifier had said, a test on unseen sweeps); the main screen's box under the map shows, after each sweep, the material most like by the whole curve and by the blend.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.5.0 — 2026-10-17

Settings → Materials: label sweeps of any material (new labels kept and chosen again), and an unknown sweep said, by the shape of its whole curve, to be X % like each material (or like none), with its blend of them and a leave-one-placement-out quality.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.5 — 2026-10-17

%), excluded needles shown with their own model.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.4 — 2026-10-17

Lot chosen at intake from the lot file, with the calibration plan for it; a control run beyond 0.5 IPS / 5 % Z is not used as the floor, a stored one is clamped.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.3 — 2026-10-17

Provisional lot result from the needles already done, needle by needle, while the lot is measured.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.2 — 2026-10-17

Lot test panel: no overlap, removed sweeps hidden (no Restore), unique sweep numbers, every sweep of a cell listed, a set-aside needle still tappable.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.1 — 2026-10-17

The same settling time in saline before every lot sweep and Saline check (Settings → Lots, 10 s); any lot sweep can be removed by hand and restored, the others of its cell judged again.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.4.0 — 2026-10-17

Every needle checked before its case (Air check + Saline check, its own model carries every reading to the reference needle), the sterile 0.9 % saline as the check saline when the lot is validated with it, control runs of the reference setting the repeatability floor.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.3.0 — 2026-10-17

Each needle as three parts (stray capacitance, electrode surface, tip size): every needle of a lot checked (leak, drift, fit, geometry), lots usable needle by needle with an Air check and a 10 s Saline check (0.09 %) before each case.

*(Reconstructed from the line of `docs/VERSIONS.md`: the full entry of this release did not survive.)*

## H7-2.2.1 — 2026-10-17 — the lot tests' raw numbers on the screen

Asked for from the bench, where the instrument has no USB stick: show the raw data of the lot
tests on the screen, to be photographed.

- **Settings → Lots → (a lot) → Raw**, nothing corrected, as the measurement unit sent it:
  - page 1: every needle of the reference and of the lot, one row each — Cp of its air sweeps and,
    for each solution, the mean |Z| at 1 kHz and at 100 kHz (the same difference at both points is
    a series resistance; a difference mostly at 1 kHz is the electrode surface);
  - then one page per needle (the reference's first): every sweep with its temperature, Cp, |Z|
    at 1, 10, 100 and 500 kHz, phase at 100 and 500 kHz, and the gain range at 1, 100 and 500 kHz,
    refused sweeps included;
  - a tap on a sweep: its fifteen frequencies with |Z|, phase and gain range, and why it was
    refused if it was.

## H7-2.2.0 — 2026-10-17 — a lot's comparison with the reference, on a chart

Asked for from the bench: for each lot, its comparison with the reference in detail on a chart,
and what the software will calibrate, and how, for that lot number.

- **Settings → Lots → (a lot) → Compare**, beside Tests and Result. Per saline strength (chips):
  - a phase chart (500 Hz – 500 kHz, the IPS band shaded) with the mean curve of the reference,
    of the lot as measured (dashed) and of the lot after the correction;
  - a table for every strength: Z (25 °C) and IPS of the reference, of the lot as measured and
    corrected, and what is left over;
  - **"What a reading of lot … gets"**: the verdict, and the four steps with this lot's numbers —
    its stray capacitance removed (its own after an Air check, else the lot's mean), Z multiplied
    by the cell-constant ratio, the reference's stray capacitance put back, and the processing of
    the trial (air-calibrated or not, from Settings → Rules) — and whether the lot is in the lot
    file, i.e. whether any device applies it yet.
- Provisional while the lot's tests are incomplete. Worked out once per change, like the result.
- Workflow test: 258 checks.

## H7-2.1.1 — 2026-10-17 — the Lots tab no longer freezes

Reported from the bench: after a reference needle was measured in Settings → Lots, opening Lots
froze the screen; the rest of the instrument worked.

- **Cause:** the tab shows each run's verdict, and it worked every verdict out again on every
  frame it drew. For a run with all its sweeps that means extracting every saline sweep's
  features — including the Cole fit, the costly part — about 17 ms per run on a PC and seconds on
  the board, frame after frame.
- **Fixed twice over:** a run's result is worked out once and kept until a run changes (a sweep
  added, a lot added or deleted, the Rules' air-calibration choice changed); and the lot tests use
  only Z and IPS, so their features are taken without the Cole fit (about 500 times faster).
  The verdicts and numbers are the same.
- Workflow test: 255 checks (one more: 200 frames of the tab's results cost under a millisecond).

## H7-2.1.0 — 2026-10-11 — the rules say what their numbers were measured with

From the bench: the trials that produced the cut-offs were run on the previous software, which had
no air calibration; whether a reading is air-corrected should follow from that, chosen in Rules.

- **Settings → Rules → "The ranges were found with"**: *air-calibrated* or *not calibrated*
  needles. Kept in the settings memory; **default: not calibrated** (the bench's trials).
- **Not calibrated:** every reading (and healthy-tissue sample) is taken as it comes, as in the
  trial: the six-second air calibration is not asked for (the needle line reads "Ready — as in
  the trial: no air calibration"; the requirement in Settings → Needle is greyed out) and is not
  applied. Taken anyway ("Air check", offered only for a lot in the lot file), it is used only to
  bring that lot's readings to the reference needle.
- **Air-calibrated:** as before — asked for, and applied to every reading.
- Lot tests, lot results and the lot transfer follow the same choice; it is written into the lot
  file, and a file loaded from a device with the opposite choice is loaded with a warning. The
  choice on the Lots tab moved here (shown there, read-only).
- Cannot be changed while a case is open. Workflow test: 254 checks.

## H7-2.0.0 — 2026-10-11 — needle lots against the reference needle

Asked for from the bench: a calibration section in Settings where sample lots of needles are added
and tested in air and saline (and more phantoms where they add accuracy), with the number of tests
set there; a final result saying whether the lot is usable with a calibration of Z and IPS, and
whether the Z and IPS ranges would have to change; and a lot calibration file to download and load
on every device, so that the lot typed at intake corrects the readings by itself.
Procedure and limits: `docs/LOT_CALIBRATION_FA.md`.

- **Settings → Lots** (new tab). The REFERENCE is the needle the trial cut-offs were found with;
  each lot is a plan of N needles x (air + up to three saline strengths: 0.9 %, 0.225 %, 0.09 %)
  x R sweeps (default 5 x 3, all three strengths). A grid shows each needle's progress; the panel
  says what to do for the next sweep (how to prepare each solution, how deep, temperature or
  conductivity). Sweeps that disagree with the same needle's earlier ones are refused with the
  numbers (a cable or hand that moved, a tip at another depth).
- **The result** (`birxcore/lotfit.cpp`): the lot's stray capacitance and its spread, each
  strength's Z (brought to 25 °C) and IPS against the reference's, the cell-constant ratio and
  whether it is one number across the strengths, the repeatability, and what is left after the
  transfer. Verdicts: Same as reference / Usable with calibration / Rule ranges must change (with
  the reference's rule boxes mapped into the lot's coordinates, for a decision, not applied) /
  Rejected.
- **The transfer** is spectral and exact for what it models: this needle's stray capacitance is
  removed at every point, Z is scaled by the cell-constant ratio, the reference needle's stray
  capacitance is put back (with the gain range the instrument would then have chosen), and the
  sweep is processed as the trial processed the reference's. In the simulator a needle with a 15 %
  larger tip and 2.5 pF more reads within 0.05 IPS and 0.1 % Z of the reference after it.
- **Lot calibration file** `records/lotcal.txt`: every approved lot, the reference's capacitance,
  and whether the trial's needles were air-calibrated (set on the tab); a check sum; a revision
  number. Exported to USB as `BIRADx_lotcal_<device>_rev<N>.txt` with a readable report; loaded
  from USB on any device (the newest revision on the stick; a changed or damaged file is refused).
- **At intake** the lot typed picks its correction; the needle line says "lot … corrected" or
  "lot … not in the lot file". "Lot covered by the lot calibration file" (Settings → Needle) can
  now be switched on: no reading with a lot that is not in the file.
- Lots and the lot file are kept by Delete logs.
- Core test 339 checks, workflow test 243 (also on the internal memory's file system in QEMU).

## H7-1.9.0 — 2026-10-11 — Delete logs

Asked for from the bench: a button in Logs to delete the logs whenever wanted (the internal
memory is 8 MB; until now only erasing the whole memory freed room).

- **Logs → Delete logs** opens a sheet that says what goes — every case and reading, their
  sweeps, the healthy-tissue samples, the pathology results, the health and diagnostic files, with
  the count of files and their size — and what stays: the case numbering (so no case number is
  ever given twice), the needle calibrations and the list of needles used, the range calibration,
  and all settings and rules.
- **Copy to USB first** on the same sheet (the same copy as All files). **Delete** must be pressed
  twice within 5 s ("Tap again to delete").
- Not while a case is open, a measurement is in progress, or diagnostic mode is writing its file;
  the sheet says which.
- The files are deleted a few at a time, with a count on the screen; nothing is measured
  meanwhile. Then the Logs list is empty, the "almost full" warning goes, and the next reading is
  recorded as before. Each deletion is an event line.
- Workflow test: 217 checks (also run on the internal memory's file system in QEMU).

## H7-1.8.0 — 2026-10-10 — a Noisy reading looks Noisy

Reported from the bench (photo): a reading at 10 MΩ, in the Noisy range, and the card said
"Provisional Low Risk". The reading had been judged Noisy and was not counted; the card showed
the LESION's answer from the four readings before it, so it looked like a Low Risk reading.

- **The verdict card shows the last reading when it was not counted:** "THIS READING — NOT
  COUNTED", its word (Noisy) in its row's colour, and under it "Lesion so far: provisional Low
  Risk (4 counted)". With the next counted reading the card is the lesion's answer again.
- **A lesion with no counted reading is called by its last reading's word** (Noisy), in the log
  too, instead of "Invalid". It is still not an answer and is not counted.
- Core test: 334 checks.

## H7-1.7.0 — 2026-10-10 — the map's window; a sound for each answer

Asked for from the bench.

- **The Z–IPS map shows Z 0–5 kΩ and IPS −20…+20, always.** Above 5 kΩ is Noisy and needs no
  room. A point outside the window is a small arrow just inside the edge it lies beyond.
- **No words in the boxes.** The colours are explained by one key on the title line above the
  plot: a swatch per answer of the table (High Risk, Low Risk, Noisy — "above 5 k" for a row that
  lies wholly above the window), then the healthy-tissue and reading marks.
- **A sound for each answer:** High Risk plays track 1 (positive), Low Risk track 6 (negative),
  Noisy track 12 (noisy). In the shipped table, and in the table already stored on an instrument:
  a row with no sound gets its level's (once, on the first start of this version); a row whose
  sound was chosen keeps it, and nothing else in the table changes.
- **A Noisy reading plays its sound** (it is still not counted), so the operator hears that the
  needle needs placing again. Until now only counted readings made a sound.
- Workflow test: 204 checks.

## H7-1.6.0 — 2026-10-10 — the measurement unit restarted by itself; a smaller start logo

Reported from the bench: sometimes, when the instrument is first plugged in and switches on,
Sweep gets no data from the measurement unit; switching off and on with the power key mends it.

- **The measurement unit is restarted by itself** — its supply switched off for 1.5 s and on
  again with SYS_EN (PG2), the line the power key drops, without switching this board off — when:
  nothing has arrived from it 12 s after start; Sweep is pressed and nothing is arriving from it;
  or a Sweep is not answered within 30 s. The link port is released while the supply is off (so
  its transmit pin cannot feed the unit) and started 3 s after it returns, as at start; the voice
  module's mode line, the touch controller and the card are started again as at start. Once the
  unit's heartbeat is back, the Sweep that was not answered is asked for again by itself. The
  screen says what is happening; if the unit still sends nothing 8 s after the restart, it says to
  check the cable and switch off and on.
- Only on mains power (on the battery that supply may be what keeps this board on), at most twice
  in a session, and never while anything is in flight. Nothing else is sent to the unit: no probe
  and no extra bytes (as 1.2.3). Each restart is an event line; the count is in the diagnostics.
- **Start screen:** the Coosa logo is drawn 380 px wide (was 720), about a third of the panel, a
  little above the middle.
- Workflow test: 200 checks.

## H7-1.5.0 — 2026-10-10 — the map in front; three answers only; Invalid that stuck

Asked for from the bench.

- **The Z–IPS map is the main picture.** The left column (case, needle, verdict, values,
  progress, buttons) takes 40% of the width; the map fills the top of the right 60%, with the
  phase chart as a strip under it. The Z axis reaches just past where the table's last row starts
  (0–6 kΩ with the shipped table) and grows only when a reading of the case is higher, up to
  20 kΩ; it was a fixed 0–20 kΩ, most of it the Noisy band. Points are larger; the keys of both
  charts are on their title lines, where no point can fall.
- **The shipped table** is: Noisy for Z above 5000 Ω; High Risk for Z 0–2000 Ω with IPS −13…+6
  and for Z 2000–4000 Ω with IPS −5…0; Low Risk everywhere else. Eight rows that tile the plane
  without overlapping (where two meet, one starts 0.0001 further on). It replaces the table
  stored in the EEPROM **once**, on the first start of this version (an event line says so);
  after that the table is the site's again, and "Restore defaults" gives this one back.
- **Only the table's words.** A reading that falls in no row — a seam of 0.0001, or a value off
  the ends of the table — takes the NEAREST row (the reason says "nearest rule n"). A sweep the
  quality checks refuse, or one without an impedance and an IPS, is shown in the table's own
  word for a bad reading (its Noisy row) and is not counted. "No rule" is left only for a table
  with no enabled row.
- **Fixed: the lesion went Invalid and stayed Invalid** while the map showed red and green
  points. It was the IPS-spread check: two readings in different boxes are more than 4 IPS
  apart, and from then on every reading of the case said Invalid. With the rule table deciding,
  the spread no longer makes a lesion Invalid (it is still measured).
- **Fixed: one High and one Low made "Suspicious"** (the mean of the two). With the rule table
  the upper middle is taken: an even split is High.

## H7-1.4.1 — 2026-10-07 — healthy tissue on the charts, in a colour of its own

Asked for from the bench: show the healthy-tissue point in a completely different colour, and so
that it can be told apart on the chart, for the doctor to have a feeling there and then for where
the suspicious lesion's samples lie relative to it. Until now the samples were in DETAILS and the
log only, and in green — the colour of Low Risk.

- **A colour nothing else uses** (magenta) for healthy tissue wherever it appears: the button on
  the needle line, its sheet, the HEALTHY column and chips of DETAILS, the H rows of the log, and
  the two charts.
- **Z–IPS map.** Each healthy sample of the open case is a filled DIAMOND labelled H1, H2, …
  (readings of the lesion stay circles). Dashed lines from the sample — from the median, a larger
  hollow diamond, when there are several — mark its IPS and its Z on the axes. A small key says
  which mark is which.
- **Phase chart.** The healthy samples' phase curves are drawn behind the curve of the reading,
  in the same magenta, with a key; while the curve on the chart is itself a healthy sample it is
  drawn in that colour too.

## H7-1.4.0 — 2026-10-06 — the rule table alone decides

Asked for from the bench, after the placement gate and the fallback label had each produced an
answer that no rule had given: "do not make it complicated — let it read from the rules table
only; whatever range we want, we add there."

- **No placement gate.** Z at 1 kHz refuses nothing by itself any more ("tip in fat" is gone).
  The Placement gate row of Settings → Protocol (1.3.2) is removed, with its line on the map. A
  site that wants high impedance refused or named has a row for it — as the shipped table's
  "Noisy" row does from 5 kΩ.
- **No hidden fallback.** A reading that no enabled row matches is **"No rule"**: shown as that,
  in grey, not counted, silent. It is never "Low Risk" unless a row says so. The fallback label and
  the separate noisy threshold are no longer used and their editors are gone from Settings →
  Rules; so is the warning about holes in the table (a hole now says what it is).
- **The IPS tile shows the measured IPS whenever there is one** — dimmer when the reading was not
  answered — instead of a dash beside a chart that gave the number.
- While a case has no reading that counts, the result card says what the last reading was called
  ("No rule", "Noisy") instead of a bare "Invalid".
- The map's plane is empty where no rule is drawn.
- What still refuses a reading before the table is looked at is only a sweep that is itself
  broken: incomplete, unstable, saturated, or a short (below 20 Ω).

The shipped table is unchanged. It leaves Z 1900–5000 Ω with IPS −13 to +6 to no row; readings
there are now "No rule" until a row is written for them.

Workflow test: 180 checks, on the PC and on the emulated Cortex-M7.

## H7-1.3.3 — 2026-10-06 — a hole in the rule table is said, not hidden

From the bench with 1.3.2, the placement gate raised to 5 kΩ and a sixth rule added (High Risk,
Z 1900–4000 Ω, IPS −5 to 0): a 3.05 kΩ resistor reading IPS −5.49 came out "Low Risk", although
it lies in none of the six rules.

That is the table's fallback. A reading that no rule matches is given the fallback label, shipped
as "Low Risk" (BIRADx 2.20.0, marginconfig.cpp: "the fallback is kept, and it is still Low Risk").
As shipped the hole it covers cannot be reached: the five rows leave Z 1900–5000 Ω with IPS −13 to
+6 uncovered, and the placement gate at 1900 Ω refuses everything there first. Raising the gate —
which 1.3.2 made possible on the instrument — opens the hole. −5.49 is just outside the new rule's
−5, so nothing matched and the fallback answered.

- **Settings → Rules warns of a hole**: when a part of the plane that a reading can reach (below
  the placement gate and the noisy threshold) is covered by no enabled rule, the page says where
  (the Z and IPS extent) and what a reading there will be called.
- **The fallback says how it counts.** Under "When no rule matches → Fallback label" (which was
  already editable, with its colour) a line now says what the words make of such a reading —
  counted as Low, High, Watch, or refused — and which words do which.
- The collapsed rule rows show the sound's current name (they showed the name saved with the
  rule, from before the track names were corrected in 1.2.7).

## H7-1.3.2 — 2026-10-06 — the placement gate can be set on the instrument

Asked from the bench: where does "fat" come from, when no rule says so? From the placement gate —
`fat_threshold_ohm`, 1900 Ω, the decision core's own quality check (birxcore/quality.h), applied
before the rule table is looked at. It is a setting, not a rule.

On the Jetson that number is a line of `device.ini`. This instrument has no such file, and its
Protocol page showed the value with the words "not editable here" — so it could not be changed at
all, and an earlier answer here that told the operator to raise it in Settings → Protocol was
wrong.

- **Settings → Protocol → Placement gate** and **Max IPS spread** each have a Change button (number
  pad; 100 Ω to 1 MΩ, and 0.1 to 100 IPS). They go through the same Apply as the rest of the
  protocol, are kept in the settings memory, and re-judge the open case.
- The row says what the gate does: a reading above it is refused as "in fat" — Invalid, no IPS in
  the tile, not counted — and that it is not one of the rules.

Workflow test: 175 checks (4 new), on the PC and on the emulated Cortex-M7.

## H7-1.3.1 — 2026-10-06 — several healthy-tissue samples; details for every row of the log

Two questions from the bench: how are the details of each sample seen in the log, and what if the
doctor wants several healthy-tissue samples? In 1.3.0 the log showed only time, IPS, Z and the
result of each reading (as the Jetson does), the 22 quantities could be seen only for the last
reading on the main screen, and a second healthy-tissue sample replaced the first.

- **Several healthy-tissue samples per case.** Each one is kept and numbered H1, H2, …; none
  replaces another. The sheet lists them with their Z, IPS and flags and, from two on, their
  median; its buttons are Close, Remove H*n* (the last one, if it went wrong) and Take another.
  The button on the needle line shows the count. A removed sample keeps its line in
  `tissue_reference.csv`; a second line with the same case and time says `removed`.
- **DETAILS with several samples.** The HEALTHY column is each quantity's median over the samples
  — as the case answer is the median of its readings — and chips on the title line (median of n,
  H1, H2, …) show any single sample instead.
- **Every row of an opened case in the log opens its details**: the 22 quantities of that reading,
  read back from its row of `dataset.csv`, with the healthy samples of that case beside it. The
  healthy-tissue samples are rows of their own (H1, H2, …, in green) among the readings, in the
  order everything was taken, and open the same way; under them, the median of the samples.
  Rows are 30 px high so that a finger can take one.

Workflow test: 171 checks (11 new), on the PC and on the emulated Cortex-M7.

## H7-1.3.0 — 2026-10-06 — the healthy-tissue sample of the patient

Asked for from the bench: one more step, with the needle in the patient's own normal tissue before
the lesion is entered, whose values are taken and kept with the detail parameters. It was said to
be in the code that was sent; it is not in BIRADx 2.18.0 or 2.20.0, nor in the previous display
firmware, as received here (searched for by every name it might have), so it is built new and is a
difference from the Jetson (docs/DIFFERENCES.md, 28).

- **Healthy tissue** — a button on the needle line of the main screen, beside Re-calibrate, once
  the needle is ready. Its sheet: needle tip in normal tissue, on the way in, hold still, Start.
  One sweep is taken; the sheet shows what was kept (Z at 1 kHz, IPS, the time, the flags). The
  button carries a tick for the rest of the case. Taking it again replaces it.
- **It is not a reading of the lesion.** Not judged by the rule table, not counted in "Reading n of
  5", not part of the case answer, no sound for an answer. The placement gate does not apply to it:
  normal breast tissue is largely fat and is expected to read so (its flags are recorded, not acted
  on). It is corrected with the needle's calibration like every reading.
- **DETAILS** shows its 22 quantities in a column of their own (HEALTHY) beside those of the
  reading (THIS READING), with a line above saying when it was taken. The phase chart is marked
  HEALTHY TISSUE while the curve on it is that sweep.
- **The record.** One line per sample in `records/tissue_reference.csv`: case id, time, needle,
  flags, the 22 quantities, and the measurement unit's own IPS line. Its sweep goes to
  `records/tissue_sweeps/`. `dataset.csv` is untouched. Both are part of "All files". In the log,
  an opened case shows "Healthy tissue of this patient: …".
- **Offered, not demanded** — unless Settings → Needle → "Healthy-tissue sample taken" is switched
  on; then no reading of an open case can be taken before it, and the main screen says why.
- It needs an open case (it belongs to a patient), a link, and a calibrated needle when that is
  required. The sample goes with the case: End case or a new case clears it.

Workflow test: 160 checks (27 new), on the PC and on the emulated Cortex-M7.

## H7-1.2.10 — 2026-10-06 — pathology for readings taken with no case open

From the bench with 1.2.9: the sweep sound stops as it should. The pathology sheet, opened from
the foot of the Logs page, said "0 cases are waiting — every measured case has an outcome" and its
button stayed "Choose a case above": nothing could be saved.

The readings on that instrument had been taken with no case open (the Sweep button, the pedal),
which are recorded under a `TEST-…` id. This port left those ids out of the list of cases awaiting
pathology and gave their rows in the log no "Enter pathology" button and a disabled "Edit case
details". BIRADx 2.20.0 makes no such exception: `OutcomeStore::pending()` lists every case id in
`dataset.csv`, and `LogsPage.qml` shows both buttons on every row.

- The awaiting list, its count ("N awaiting pathology") and the "Awaiting path." filter include
  the `TEST-…` cases.
- Every row of the log, NO CASE rows too, has "Enter pathology" / "Change pathology" and an
  enabled "Edit case details".

Workflow test: 133 checks (8 new), on the PC and on the emulated Cortex-M7.

## H7-1.2.9 — 2026-10-06 — the sweep sound stops; rename works; the file layer is tested on the CPU

From the bench with 1.2.8: the internal memory works (the new driver) and readings are recorded.
The sweep sound starts but never stops. And saving a pathology result from the log "does not
save".

- **The sweep sound stops when the sweep ends.** The previous firmware stopped a sound by first
  setting the play mode back to "once" and then sending "end playing" (`playVoice(0, 0)`); 1.2.7
  sent only the second, and a recording started in repeat mode went on for ever. Stop is now that
  same pair, followed by the module's plain "stop"; 150 ms later the module is asked whether it is
  still playing and, if so, stopped again (up to three times; the count is in the Voice module
  line). A stop is also sent at start, so a reset in mid-sweep cannot leave it sounding.
- **`rename()` never worked.** This C library builds `rename()` from `_link()` and `_unlink()` and
  does not call `_rename()`; every rename on the instrument failed with "too many links". The one
  user was the health log's rotation, so `health.csv` would have grown without limit. Found by the
  new test below.
- **The file layer is now run on the instrument's CPU before the instrument runs it.**
  `tests/qemu/run_fs_test.sh` (88 checks) and `tests/qemu/run_app_test.sh` (the whole 125-check
  workflow test) build with the firmware's compiler and flags and run on an emulated Cortex-M7,
  with every file on a littlefs volume reached through newlib and `board/syscalls.c` — the layer no
  PC test touches. Pathology outcomes are saved, changed, reloaded and survive a restart there, 300
  times in a row. So the "does not save" from the bench is not reproduced; what the sheet says
  after Save is needed.
- After a pathology result is saved, the sentence "Saved — … is …" also stays on the Logs page.

## H7-1.2.8 — 2026-10-05 — the internal memory: a driver of its own, proven before use

From the bench with 1.2.7: Settings → Device → Internal memory → "Use it…" leaves the memory "not
in use". The chip (W25Q64) had answered its identity since 1.2.0, but it could not be prepared.

Until now the chip was driven by the previous firmware's driver, through the SPI1 peripheral with
its hardware chip-select, on the assumption that it was "known to work on this board". It had
never run in this firmware before the bench did it. Reading it again: it leaves chip-select to the
peripheral, which lets go of the pin between two commands; a write-enable that is not ended by
chip-select going high is never taken, and then no erase and no write is. Its read routine also
sends from beyond a four-byte buffer, and none of its waits has a time limit. Which of these
stopped it on the bench is not known — only that it did not work.

- **A new driver** (`board/intmem_board.c`), on the same pins (PD7, PG9, PG10, PG11, PE2), which
  drives them directly, edge by edge, at about 4 MHz: chip-select is an ordinary output that is
  high between commands for a known time; every write-enable is read back; protection bits left
  in the chip's status register are cleared (QE kept); erase and write wait with a time limit. It
  does not use the SPI peripheral or the HAL's SPI code. The previous firmware's driver is removed.
- **Tested before it meets the chip** (`make -f Makefile.pc nortest`, 30 checks): the driver is
  wired, edge by edge, to a model of the W25Q64 that keeps the data sheet's rules — commands act at
  chip-select rising, write-enable is used up, a write only clears bits and wraps in its page,
  nothing is accepted while busy, the data line may change only with the clock low. littlefs is
  formatted, written and read back through it; and a chip that does not erase, one that refuses
  write-enable, one that stays busy, one with protection set and no chip at all are each reported
  as what they are.
- **"Erase it and use it" proves the chip first**: one sector is erased, checked, written, read
  back and erased again. If any step fails, the sentence on the screen says which, with the chip's
  identity and status bytes, and it stays in the Internal memory row (and in the diagnostics
  file) — so one photograph is enough.

## H7-1.2.7 — 2026-10-05 — a sound while the sweep runs

From the bench: nothing is heard while a sweep is running, and there is nowhere to set it. The
previous firmware of this instrument played a recording on repeat from the start of a sweep to its
end (`playSound(TESTING, 1)` … `playSound(0, 0)`); BIRADx on the Jetson has no such sound, so the
port had none.

- **While a sweep runs**, one recording repeats — from the first line of the sweep (button or
  pedal, a calibration sweep too) until its `[E]`, a Stop, or the 12 s watchdog. Then the matched
  rule's own sound plays, if it has one. Settings → Sound → "While a sweep runs": silent or 1–12;
  it starts as 7, the track the previous firmware used. Muting silences it too.
- **The track names were wrong.** They came from `voice.h`; the previous firmware's screens used
  the numbers in `MainScreenView.hpp`: 1 positive, 3 strongly positive, 4 connected, 5 suspicious,
  6 negative, 7 during a sweep, 8 warning, 11 its settings test, 12 noisy. The test buttons now say
  so. A rule's sound saved under an old name still plays (the number is what is kept).
- The Sound page scrolls.

Workflow test: 125 checks (11 new).

## H7-1.2.6 — 2026-10-05 — one press is one press

From the bench with 1.2.5: sound and the case flow are right. But a button sometimes "falls
through": Close in Settings returns to the main screen and the gear, which is in the same place
there, is pressed at once, so Settings opens again. The same with Start case and others.

- **The fault.** The touch controller (GT911) marks each finished scan with a "new report" bit,
  which the host clears after reading. The driver taken over from the previous firmware ignored
  that bit: read again before the next scan was ready, the register is empty, and it called that
  "no finger". Whenever this board asked a little sooner than the controller scanned, one steady
  press became press, release, press, release — the first release fired the button, and the
  second press and release fired whatever had appeared under the finger.
- **The fix.** The controller is asked whether it has a new report (`Touch_Poll`); between
  reports the finger is where it was. And a lift counts only after it has lasted 50 ms, so a
  momentary "no finger" under a light or sliding finger is not a release either. If the controller
  stops reporting altogether for 250 ms with a finger down, that is taken as a lift.
- The diagnostics file has a "touch panel" line: reports read, momentary lifts ignored.

## H7-1.2.5 — 2026-10-05 — the voice module; Cancel keeps the new case

From the bench with 1.2.4: Sweep works. No sound at all, not even from the test buttons in
Settings → Sound. And Start case (or a tap on the case strip) followed by Cancel does not leave a
new case, as it does on the Jetson.

- **Voice module.** The commands and the port were already those of the previous firmware. What
  differed was time. The module reads its three mode lines when it powers up (CON3 high, CON1 and
  CON2 low = serial control). The previous firmware drove CON3 high and let go of it about 0.6 s
  later, after its printer and clock start-up; this firmware let go within milliseconds, so the
  module most likely started in a push-button mode and ignored the serial port. CON3 is now held
  for 0.8 s (behind the logo). The first commands go out after the start screen, three seconds
  after power on, not during start-up. The volume is also sent as a level (`AA 13 01 n`), not only
  as steps from an assumed position.
- **The module is asked.** It answers questions on its own transmit line, which the previous
  firmware never read. Settings → Sound now has a "Voice module" row: whether it answers, whether
  it is playing, how many recordings it holds and where, and — if it does not answer — the state of
  the port and of the mode line. It is asked at start, on "Ask", and 0.4 s after every test track.
  The same line is in the diagnostics file and on the debug port.
- **Cancel on the intake form only goes back** (`Main.qml`: `onCancelled: currentScreen =
  "measure"`). The case whose id was minted stays open, without a patient row; readings can be
  taken under it and its details entered later from Logs. 1.0.0–1.2.4 dropped the case.
- **A tap on the case strip mints a new case id**, also over an open case, exactly as Start case
  does (`Main.qml` `onOpenIntake`). 1.0.0–1.2.4 opened the open case's details there.

After flashing this version the instrument must be switched fully off and on: the voice module
keeps the mode it read at its last power-up.

## H7-1.2.4 — 2026-10-05 — the sweep watchdog closed sweeps that were arriving

The link monitor of 1.2.3 on the bench showed what three versions of guessing had not: the
measurement unit answers `[R]` at once with a complete sweep — `[B]`, fifteen `[imp…]` lines
0.2 s apart, `[IPS-0006.16,Z03048.39,Negative]`, `[E]` — and twice in the middle of it this board
wrote "no [E] after 12 s — closing the run", one millisecond after a point had arrived. The unit
was never at fault, and neither was the command framing.

- **The fault.** The main loop read the tick once, at its top; a line taken from the link a
  moment later was stamped with a newer tick. "Time since the last line" was then a small negative
  number, which as an unsigned number is 49 days, and the 12 s watchdog fired. The run was closed,
  the next point opened a new one, and what reached the judge was a fragment — or nothing. The
  same sum was in the watchdog for a stopped sweep and in the calibration sheet's ("The sweep
  began but never finished"). Present since H7-1.0.0. All of them now measure from a tick read
  after the link has been served, and an interval that comes out negative counts as zero.
- **Why the tests did not see it.** On the PC the simulated tick moved only between loops, so the
  two readings were always equal. The simulator can now move the tick while the program runs
  (`drift N` in a script, `sim::g_tickEvery` in the tests), the workflow tests run with it, and the
  sweep captured on the bench is replayed in the unit's own line format.
- **A heartbeat no longer keeps a run open.** The unit sends no `[C]` while it sweeps, so a `[C]`
  is not a sign that a sweep is still coming. Before, a unit that dropped a sweep and went back to
  its heartbeat left the screen "measuring" until Stop was pressed.

Workflow test: 114 checks (25 new). With the fix taken out, 15 of them fail.

## H7-1.2.3 — 2026-10-04 — one `[R]`, a long wait, and a link monitor

From the bench with 1.2.2: Sweep still "does nothing"; but after pressing Calibrate (an orange
sentence appears on its sheet and goes) the main screen shows a correct impedance. A calibration
sweep never puts a value on the main screen, so that value was an ordinary reading: a sweep did
arrive, later than this board was prepared to wait, and the sentence on the calibration sheet was
"A measurement is already running". So the measurement unit does hear `[R]`; what is not known
is how long it takes to begin, and whether the extra bytes of 1.1.3–1.2.2 delayed or cancelled it.

- **A Sweep is one `[R]`, then a wait of up to 30 s** that counts on the screen ("Asked … 7 s
  ago"). Nothing else is sent: the framing search of 1.2.2 is removed. A sweep that begins after
  the wait has ended is still taken. The calibration sheet waits 30 s for its sweep to begin
  (2.20.0: 10 s).
- **Link monitor** (Settings → Device → Measurement unit → Monitor, now the second row): every
  line to and from the measurement unit with the time it passed, live, heartbeats counted; and
  *Send [R]* / *Send [T]*, which put those three bytes on the wire and do nothing else. It shows
  how long the unit takes to answer, instead of that being guessed.
- The traffic history kept for the monitor and the diagnostics is 200 lines (was 60).

Workflow test: 89 checks.

## H7-1.2.2 — 2026-10-04 — `[R]` finds the command framing

1.2.1 did not make the measurement unit answer either. New from the bench: on the Jetson, too, the
pedal worked and the Sweep button did not. That is the signature of a shifted command framing: the
unit reads commands three bytes at a time and never re-synchronises, so after one stray byte it
ignores every `[R]` while its pedal and heartbeat carry on, and repeating `[R]` (three bytes each
time) lands on the same wrong boundary for ever. Every version so far, and the Jetson, only ever
sent `[R]` on one boundary; 1.1.3–1.2.0 tried to find the right one with `[T]`, which works only
if the unit's firmware answers `[T]`, and this one may not.

- **A Sweep that is not answered within 2.5 s is asked again after one extra byte, and once more
  after another.** One of the three boundaries is right, and the sweep that begins says which;
  from then on a Sweep is one `[R]` again. If none begins, a third extra byte leaves the framing
  as found and the screen says the unit did not answer on any alignment. Stop during the search
  also completes the three bytes.
- Everything else as 1.2.1: the port is started after the start screen, nothing is sent unasked,
  and no command goes out between the end of a sweep and the next heartbeat.

In the simulator a unit with one or two stray bytes is answered on the first Sweep (after 2.6 s
or 5.2 s) and at once afterwards. Not yet confirmed on the board. Workflow test: 90 checks.

## H7-1.2.1 — 2026-10-04 — the link, as the previous firmware drove it

Reported from the bench: with the previous display firmware flashed back, the on-screen button
starts a sweep; with H7-1.2.0 it does not ("sends its heartbeat but does not answer commands"),
also after switching off and on. So the explanation given with 1.1.3 (the measurement unit goes
deaf while this board is being programmed) was wrong, or not the whole of it, and the difference is
in what this firmware does on the wire. The bytes of a Sweep are the same three in both. What
differed, and is now the same as the previous firmware:

- **When the port is started.** The previous firmware started USART1 from its first screen, a
  second or more after power on, and left PB6/PB7 untouched until then. H7-1.0.0..1.2.0 started it
  0.2–0.3 s after power on (and from 1.1.3 drove PB6 high from the first instruction and kept
  pull-ups on both pins). It is now started after the start screen, three seconds after power on,
  with the pins exactly as the previous firmware configured them.
- **What is sent.** One `[R]` for a Sweep, and nothing else, ever, unless Test is pressed. Gone:
  the repeat of `[R]` (2.20.0's five, 1.1.3's two), the `[T]` probe when the link comes up and
  after an unanswered Sweep, and the extra bytes that re-aligned the framing. The re-aligning is
  still there behind Settings → Device → Test, where it is asked for.
- **When it may be sent.** After a sweep ends, not before the measurement unit's next heartbeat
  (the previous firmware re-enabled its button on the same condition).
- An unanswered Sweep says so after 6 s.
- Settings → Device → Measurement unit and the diagnostics now show how many commands were sent,
  how many the port refused, and the port's own state (transmitter on, TX pin on USART1, last
  send OK) — so that "the bytes left this board" can be told from "they never did".

Whether this is what makes the measurement unit answer is not known until it is tried on the
board. Workflow test: 89 checks.

## H7-1.2.0 — 2026-10-04 — records without an SD card

The previous firmware kept its records on the board's own serial flash (W25Q64, 8 MB, SPI1) and
never used an SD card; the instrument on the bench has no card in it.

- **The internal memory is a records store.** With no SD card mounted, the records, the health log
  and the diagnostic-mode files go to the W25Q64, as the same files with the same names
  (`records/dataset.csv`, `records/sweeps/…`). With a card in, the card is used and the internal
  memory is left alone. *All files* / *Export shown* / *Diagnostics* copy to a USB stick from
  whichever is in use.
- **It is a littlefs volume** (`third_party/littlefs` 2.9.3, `board/intmem.c`): erases are spread
  over all 2048 blocks, and a power cut during a write leaves every file as it was at its last
  close. SPI1's settings and the chip driver are the previous firmware's, unchanged.
- **The previous firmware's data is not erased without being asked.** A chip that is completely
  empty is prepared at start. One that holds anything else is left exactly as it is, the measure
  screen says readings are not being recorded and where to go, and Settings → Device → Internal
  memory → *Use it…* explains what will be lost, offers *Save old contents to USB first* (the chip
  byte for byte, `old_internal_memory_w25q64.bin`), and only then erases.
- Capacity: about 1700 readings (measured on the emulated chip with files of the real sizes). The
  health log is limited to 2 × 256 kB there. Below 400 kB free the screen says so.
- Settings → Device shows *Records* and *Internal memory* near the top, with the free space.
- Tested on a PC against an emulated W25Q64 that enforces the rules of NOR flash
  (`make -f Makefile.pc intmemtest`): 2000 appends, remount, rename, list, a full volume, 20 000
  appends with the most-erased block at 81 erases, 300 power cuts with no damaged file. Not yet
  run on the board. One more reading costs about 0.15 s with 100 readings on record and about
  0.55 s with 1500.
- Workflow test: 85 checks.

## H7-1.1.5 — 2026-10-04

- Seen on the board: with the "No SD card" banner above and a five-line sentence above the
  buttons, the verdict card was squeezed to nothing and "Ready" was drawn over its neighbours.
  The value tiles now give way first (down to 92 px), the verdict card keeps at least 72 px, and
  the sentences shown above the buttons are at most three lines.
- A one-line banner is tall enough for its OK button.

## H7-1.1.4 — 2026-10-04

- **Start screen:** the Coosa logo with "Together, transforming care.", full white on a black
  panel, for three seconds (`assets/coosa_full.svg`, rendered by `tools/mklogo.py`). Nothing else
  is on it; the start-up runs behind it. The version is no longer on the start screen — it is on
  the top line of Settings and in Settings → About. A start that fails is still reported by the
  next start, before the logo.

## H7-1.1.3 — 2026-10-04

On the board the measurement unit's heartbeat arrived (0 UART errors) but Sweep was never
answered, after an afternoon of this board being programmed and reset next to a measurement unit
that stayed powered.

- **The command framing of the measurement unit is put back in step.** Its firmware reads
  commands three bytes at a time and never re-synchronises, so one stray byte on its receive line
  makes it ignore every command while its heartbeat carries on. This board's transmit pin floats
  whenever its processor is in reset. Now, when a Sweep is not answered, `[T]` is sent (the one
  command that replies at once); if that is not answered either, one extra byte is sent and `[T]`
  tried again, up to twice. If an alignment answers, the Sweep is asked for again. If none does, a
  third byte restores the alignment as found and the screen says the unit is not hearing (its
  receiver has stopped after a line error: power-cycle it; or the wire is open). The same check
  runs once whenever the link comes up, and from Settings → Device → Test.
- **The transmit line is driven high within microseconds of leaving reset**, and both link pins
  keep a pull-up, so this board no longer sends a stray byte each time it starts.
- **The result of Test is shown next to the button** (ANSWERED / RE-ALIGNED / NO REPLY). In 1.1.2
  the sentence was under the row, below the edge of the screen.
- Sweep is asked for twice at most (2.20.0: five times); the first silence is spent finding out
  why instead.
- Workflow test: 78 checks.

## H7-1.1.2 — 2026-10-04

Seen on the board with 1.1.1: screen, colours, touch, EEPROM, clock and the link's receive side
all work (99 heartbeats, 0 UART errors). Pressing Sweep showed nothing at all.

- **A Sweep that is not answered now says so where the button is.** While the request is being
  repeated the sentence above the buttons counts the requests; after five without an answer it
  gives the diagnosis (heartbeat arriving but no answer: the measurement unit's command receiver
  has stopped — switch it off and on; or nothing arriving at all: cable or power). 1.1.1 wrote
  this only to the event log.
- The "no reading in this case yet" note no longer runs outside the Z–IPS map.

## H7-1.1.1 — 2026-10-04 — first run on the hardware

First flashed on the board as 1.1.0: the start screen appeared (so the clocks, SDRAM, LTDC panel
timing and colours are right) and then the board restarted, over and over.

- **Most likely cause, fixed:** the SDRAM is at 0xC0000000, which the Cortex-M7 treats as *device*
  memory unless told otherwise, and an unaligned access to device memory is a fault. The original
  firmware kept only the frame buffer there. BIRADx keeps its heap there, and string handling
  uses unaligned accesses. The SDRAM is now declared to the MPU as normal memory (read/write, not
  executable, not cached). 1.0.0 had the same fault. Confirmed on the board: 1.1.1 starts.
- **A start that fails now says where.** The start screen names each stage as it begins (touch
  panel, SD card, USB host, settings…). The stage is kept across a reset, so if a start does not
  reach the main screen, the next start shows a page for 10 s with the stage it had reached, the
  reset cause, and — after a fault — the fault registers with the address of the instruction.
- The fault record now includes PC, LR and BFAR; it is also written to the event log.
- The independent watchdog is kept fed from the tick interrupt, in case this board's option bytes
  start it in hardware (BIRADx does not use it). If it is not running this does nothing.

## H7-1.1.0 — 2026-09-27 — follows BIRADx 2.20.0

- **Stop (2.19.0).** A sweep that begins within 3 s of a Stop that found nothing running (the
  answer to the request that was stopped) is discarded, not recorded. Later than that it is the
  pedal, and is kept. Discarded sweeps are named in the event log with their point count.
- **"TDP is connected"** (the reply to the link probe) is no longer counted as an unparsed line.
- **Pathology (2.19.0).** Each case in the log has its own *Enter pathology* / *Change pathology*
  button, which opens the sheet for that case with its current result chosen and closes 1.4 s
  after a save. The footer's *Enter pathology* lists the cases still waiting and stays open for
  the next. Save says what it saved ("Saved — … is malignant.") or why not ("NOT saved: …", with
  the file and the reason). A case that already has a result says "recorded: … — saving replaces
  it". The button says what is missing ("Choose a case above", "Choose Benign, Borderline or
  Malignant"). The sheet now has the 2.20.0 layout (date = today, source = core biopsy).
- **Diagnostic mode (2.19.0).** Logs → *Diagnostics* starts it; the button becomes *Stop
  diagnostics*. See DIFFERENCES.md §21. A red "● DIAGNOSTIC MODE — recording" pill is on every
  screen while it is on. Stopping writes a closing snapshot and copies the file to a USB stick.
- **Health log (2.20.0).** `records/health.csv` every 10 s; at start, how the previous session
  ended (power lost, reset line, watchdog, or a fault). Switching off with the power key marks a
  clean end. The same appears in the diagnostics snapshot.
- The start screen shows the version.
- Workflow test: 70 checks (was 44). Core self-test: 330/330.

## H7-1.0.0 — 2026-09-24

First build of BIRADx for the FK743M1 display board (STM32H743IIT6). Not yet run on the
hardware; see HARDWARE_TEST_FA.md.

- The decision core of BIRADx 2.18.0 (`birx`), ported without changing its logic. Its 330
  self-checks pass on a PC and on an emulated Cortex-M7, built with the firmware's own flags
  (`tests/`, `docs/test-logs/`).
- Fixed inputs run on the PC and on the Cortex-M7 give identical results in every tier, label,
  flag, reason and non-Cole feature, bit for bit. The Cole-fit values differ by at most 7.5e-10
  relative (libm), against a tolerance of 1e-3. The needle-corrected IPS agrees to the last bit
  but one (3.5e-15).
- Everything else on this board is new: the instrument logic that was the Jetson's (case,
  needle calibration, blockers, records, log, export, diagnostics, settings in EEPROM); every
  screen, redrawn for the 1024×600 panel; and the board layer for this hardware.
- A workflow test runs 44 checks against a simulated measurement unit: device ID, calibration,
  early stop, spread, fat, Stop, rule re-judging, passcode, log, pathology and export.
- The deliberate differences from 2.18.0 are listed in DIFFERENCES.md.
