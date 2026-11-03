The twelve recordings of the voice module, as WAV files: 01.wav … 12.wav.

On the display board (BIRADx-H7) these are stored on the DY voice module itself.
On the ITDS computer the program plays the file of the same number with aplay,
on the sound device named in itds.ini ([audio] device).

  1 positive            5 suspicious           9  —
  2 —                   6 negative             10 —
  3 strongly positive   7 during a sweep       11 test
  4 connected           8 warning              12 noisy

The files shipped here are the tones of the ITDS software 2.x (and a soft tick
for 7, which repeats while a sweep runs), so that every number plays something.
Replace them with the instrument's own recordings, under the same names:
the voice module's files (00001.mp3 … 00012.mp3 on its memory) become
01.wav … 12.wav, for example:

    ffmpeg -i 00001.mp3 -ac 1 -ar 44100 01.wav

Which recording plays for which answer is chosen in Settings -> Rules (open a
rule, "Plays") and Settings -> Sound; Settings -> Sound also tests each one.
