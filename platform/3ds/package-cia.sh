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
read_le() {
    od -An -tu"$2" -j"$1" -N"$2" -v "$banner_wav" | tr -d ' \n'
}
channels=$(read_le 22 2)
rate=$(read_le 24 4)
bits=$(read_le 34 2)
data=$(read_le 40 4)
if [ "$channels" -ne 2 ] || [ "$rate" -ne 16364 ] || [ "$bits" -ne 16 ]; then
    echo "banner.wav must be 16 bit stereo at 16364 Hz, found ${bits} bit, ${channels} channel, ${rate} Hz" >&2
    exit 1
fi
if [ "$((data / 4))" -gt 49092 ]; then
    echo "banner.wav is longer than the three seconds the HOME menu allows" >&2
    exit 1
fi

"$BANNERTOOL" makebanner -i "$banner_png" -a "$banner_wav" -o "$banner"

rm -f "$output"
"$MAKEROM" -f cia -o "$output" -target t -desc app:2.50 \
    -rsf "$rsf" -elf "$elf" -icon "$smdh" -banner "$banner" \
    "-DDIR_ROMFS=$romfs" "-DAPP_UNIQUE_ID=$UNIQUE_ID"

ls -l "$output"
