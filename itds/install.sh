#!/bin/sh
# itds/install.sh [folder] [--autostart]
#
# Builds BIRADx-ITDS on this computer (the ITDS Jetson) and installs it in <folder>
# (default: ~/BIRADx-ITDS). What the folder already holds — records/, eeprom.bin (the
# instrument's settings), itds.ini, the voice recordings — is never overwritten: only the
# program is replaced. --autostart also starts it when the desktop starts.
#
# Needs what the previous ITDS software needed: qmake and the Qt 5 headers (qtbase5-dev),
# a C++ compiler, and aplay (alsa-utils) for the recordings.
set -e
here=$(cd "$(dirname "$0")" && pwd)
target="$HOME/BIRADx-ITDS"
autostart=0
for a in "$@"; do
    case "$a" in
        --autostart) autostart=1 ;;
        *) target="$a" ;;
    esac
done

command -v qmake >/dev/null 2>&1 || { echo "qmake not found: sudo apt install qtbase5-dev"; exit 1; }
command -v aplay >/dev/null 2>&1 || echo "note: aplay not found — no sound until: sudo apt install alsa-utils"

# Files dated in the future (this computer's clock behind the one that made the archive) make
# make regenerate the Makefile for ever ("modification time … in the future"): dated now instead.
future=$(find "$here/.." -newermt "now" 2>/dev/null | wc -l)
if [ "$future" -gt 0 ]; then
    echo "$future file(s) dated in the future — this computer's clock is behind: dating them now."
    echo "(Set the clock: sudo date -s \"YYYY-MM-DD HH:MM\")"
    find "$here/.." -newermt "now" -exec touch {} + 2>/dev/null || true
fi

cd "$here"
rm -f Makefile .qmake.stash
qmake biradx-itds.pro
# Two jobs, not one per core: a full-speed build draws enough current to throttle a Jetson on
# a weak supply ("System throttled due to Over-current"). JOBS=4 ./install.sh to change it.
make -j"${JOBS:-2}"

mkdir -p "$target"
if pgrep -x biradx-itds >/dev/null 2>&1; then
    echo "BIRADx-ITDS is running: it is closed before its program is replaced."
    pkill -TERM -x biradx-itds || true
    sleep 2
fi
cp biradx-itds "$target/biradx-itds.new"
mv -f "$target/biradx-itds.new" "$target/biradx-itds"
if [ ! -d "$target/voice" ]; then
    cp -r voice "$target/voice"
fi

if [ "$autostart" = 1 ]; then
    mkdir -p "$HOME/.config/autostart"
    sed "s#@TARGET@#$target#g" biradx-itds.desktop > "$HOME/.config/autostart/biradx-itds.desktop"
    echo "starts with the desktop: $HOME/.config/autostart/biradx-itds.desktop"
fi

echo "installed: $target/biradx-itds ($("$target/biradx-itds" --version 2>/dev/null || echo built))"
echo "run it:    $target/biradx-itds        (Ctrl+Q closes it)"
