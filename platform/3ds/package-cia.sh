#!/bin/sh
# Packs the built executable into a CIA for installation on a 3DS.
#
# The 3dsx that make produces runs from the Homebrew Launcher; a CIA installs
# as a title with its own icon and banner on the HOME menu. Both come from the
# same ELF and the same RomFS.
#
#   MAKEROM      path to makerom, default: makerom on PATH
#   BANNERTOOL   path to bannertool, default: bannertool on PATH
#   UNIQUE_ID    five hex digits identifying the title

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

: "${MAKEROM:=makerom}"
: "${BANNERTOOL:=bannertool}"
: "${UNIQUE_ID:=0xF4A4D}"

elf="$here/armagetronad-3ds.elf"
smdh="$here/armagetronad-3ds.smdh"
romfs="$here/romfs"
rsf="$here/armagetronad-3ds.rsf"
banner_png="$here/banner.png"
banner_wav="$here/banner.wav"
build="$here/build-full/cia"
banner="$build/banner.bnr"
output="$here/armagetronad-3ds.cia"

for required in "$elf" "$smdh" "$rsf" "$banner_png" "$banner_wav"; do
    [ -e "$required" ] || { echo "missing CIA input: $required" >&2; exit 1; }
done
[ -d "$romfs" ] || { echo "missing RomFS: run prepare-romfs.sh first" >&2; exit 1; }

case "$UNIQUE_ID" in
    0x[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) ;;
    *) echo "UNIQUE_ID must be five hex digits, for example 0xF4A4D" >&2; exit 1 ;;
esac

mkdir -p "$build"

# The HOME menu wants sixteen bit stereo at 16364 Hz and at most three seconds,
# and bannertool converts whatever it is handed without complaining: hand it a
# mono file at twice the rate and the menu plays noise. Check rather than
# trust, because a wrong banner is not a build error, it is a sound the console
# makes at whoever is holding it.
#
# Walk the chunks rather than reading fixed offsets. A RIFF file may carry a
# LIST or fact chunk between fmt and data, and an editor that adds one moves
# the data chunk along with it: reading the length from a fixed offset then
# lands inside whatever was inserted and passes on a number that means nothing.
"${PYTHON:-python3}" - "$banner_wav" <<'EOF' || exit 1
import struct
import sys

blob = open(sys.argv[1], 'rb').read()
if blob[:4] != b'RIFF' or blob[8:12] != b'WAVE':
    sys.exit('banner.wav is not a RIFF WAVE file')

fmt = data = None
pos = 12
while pos + 8 <= len(blob):
    name = blob[pos:pos + 4]
    size, = struct.unpack_from('<I', blob, pos + 4)
    if name == b'fmt ':
        fmt = struct.unpack_from('<HHIIHH', blob, pos + 8)
    elif name == b'data':
        data = size
    pos += 8 + size + (size & 1)

if fmt is None or data is None:
    sys.exit('banner.wav has no fmt or data chunk')

_, channels, rate, _, _, bits = fmt
if (channels, rate, bits) != (2, 16364, 16):
    sys.exit('banner.wav must be 16 bit stereo at 16364 Hz, found '
             '%d bit, %d channel, %d Hz' % (bits, channels, rate))

frames = data // 4
if frames > 49092:
    sys.exit('banner.wav is %.2f seconds; the HOME menu allows three'
             % (frames / 16364.0))

print('banner audio: %d Hz, %d channels, %d bit, %d frames' %
      (rate, channels, bits, frames))
EOF

"$BANNERTOOL" makebanner -i "$banner_png" -a "$banner_wav" -o "$banner"

rm -f "$output"
"$MAKEROM" -f cia -o "$output" -target t -desc app:2.50 \
    -rsf "$rsf" -elf "$elf" -icon "$smdh" -banner "$banner" \
    "-DDIR_ROMFS=$romfs" "-DAPP_UNIQUE_ID=$UNIQUE_ID"

ls -l "$output"
