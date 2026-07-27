#!/bin/sh
# Assembles platform/3ds/romfs from the game data in this repository.
#
# The 3DS build has no install step: everything the client reads at runtime is
# packed into the RomFS image inside the executable, and anything the player
# adds later comes from sdmc:/3ds/armagetronad. This mirrors what
# prepare-romfs.ps1 does on Windows.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
romfs="$here/romfs"
python=${PYTHON:-python3}

rm -rf "$romfs"
mkdir -p "$romfs"

# copy_runtime <source dir> <romfs dir> <extension>...
copy_runtime() {
    src="$root/$1"
    dst="$romfs/$2"
    shift 2
    [ -d "$src" ] || return 0
    for ext in "$@"; do
        find "$src" -type f -name "*.$ext" | while IFS= read -r file; do
            rel=${file#"$src"/}
            mkdir -p "$dst/$(dirname -- "$rel")"
            cp -p "$file" "$dst/$rel"
        done
    done
}

copy_runtime config   config   cfg srv
copy_runtime language language txt
copy_runtime textures textures png jpg ttf cfg txt
copy_runtime models   models   mod
copy_runtime sound    sound    ogg wav
copy_runtime music    music    ogg m3u aatrack

# These two are templates the autotools build would substitute into. Nothing in
# them needs substituting for this target, so they are taken as they are.
mkdir -p "$romfs/config"
cp -p "$root/config/aiplayers.cfg.in" "$romfs/config/aiplayers.cfg"
cp -p "$root/config/rc.config.in" "$romfs/config/rc.config"

# The language index lists which translations to load. Replace its include list
# rather than appending to it, so the set is exactly what is packed, and put
# the 3DS wording last: it overrides the desktop control names, and the last
# definition of a phrase wins.
mkdir -p "$romfs/language"
sed -e 's/@progtitle@/Armagetron Advanced/g' \
    -e '/^include .*\.txt[[:space:]]*$/d' \
    "$root/language/languages.txt.in" > "$romfs/language/languages.txt"
cat >> "$romfs/language/languages.txt" <<'EOF'

include british.txt
include american.txt
include spanish.txt
include russian.txt
include polish.txt
include galician.txt
include french.txt
include deutsch.txt
include 3ds.txt
EOF

"$python" "$root/batch/make/copyresources.py" \
    "$root/resource/proto" "$romfs/resource/included"

if [ -d "$root/resource/binary" ]; then
    copy_runtime resource/binary resource/included png
fi

files=$(find "$romfs" -type f | wc -l)
bytes=$(find "$romfs" -type f -exec cat {} + | wc -c)
echo "Prepared RomFS: $files files, $bytes bytes"
