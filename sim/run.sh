#!/bin/sh
# sim/run.sh <script> [outdir] — build the simulator, run a scripted session on
# a fresh simulated instrument, write PNG screenshots.
set -e
cd "$(dirname "$0")/.."
make -s -f Makefile.pc build-pc/sim_main
out=${2:-shots}
rm -rf "$out"; mkdir -p "$out"
(cd build-pc && ./sim_main "../$1" "../$out" fresh)
python3 - "$out" <<'PY'
import sys, glob, os
from PIL import Image
for f in glob.glob(sys.argv[1] + '/*.ppm'):
    Image.open(f).save(f[:-4] + '.png'); os.remove(f)
PY
ls "$out"
