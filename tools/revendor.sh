#!/bin/sh
# Copy the audio parts of a pinned fliperama86/pico_hdmi commit into
# src/pico_hdmi, unmodified. Upstream .c files are renamed .c.inc so Arduino
# does not compile them directly; src/drivers/dvhstx/audio_*.c include them
# after audio_config.h. The video engine is not copied: DVI-HSTX has its own.
# src/pico_hdmi/video_output.h is ours (a stand-in), so it is left alone.
set -e
REV=${1:-a18d800e4db88591296e5830bd834a0b9bc075da}   # tag v0.0.19
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST=$ROOT/src/pico_hdmi
TMP=$(mktemp -d)
git clone -q https://github.com/fliperama86/pico_hdmi "$TMP/pico_hdmi"
git -C "$TMP/pico_hdmi" checkout -q "$REV"
for f in hstx_packet hstx_data_island_queue; do
  cp "$TMP/pico_hdmi/include/pico_hdmi/$f.h" "$DST/"
  cp "$TMP/pico_hdmi/src/$f.c" "$DST/$f.c.inc"
done
cp "$TMP/pico_hdmi/LICENSE" "$DST/LICENSE"
echo "$REV" > "$DST/REVISION"
rm -rf "$TMP"
echo "vendored pico_hdmi $REV"
