#!/bin/sh
# Writes src/tTrueVersion.h, which is how the client knows what version it is.
#
# Upstream generates this in bootstrap.sh as part of the autotools setup. The
# 3DS build does not run autotools, so it is generated here the same way, from
# the same script, rather than by hand: the version shown in the menus and sent
# to servers should say what was actually built.
#
# batch/make/version reads the git history for the revision count, so a shallow
# clone will produce a lower number than the same commit built from a full one.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
header="$root/src/tTrueVersion.h"

# Keys with no value are dropped rather than defined empty. tVersion.cpp has an
# #ifndef fallback for every one of them, so a missing define is handled; a
# define with nothing after it expands to the key's own name at the use site,
# which is how "error: 'ZNR' was not declared in this scope" happens.
sh "$root/batch/make/version" --verbose "$root" \
    | awk '{
        key = $1
        $1 = ""
        sub(/^[ \t]+/, "")
        if (length($0) > 0)
            print "#define TRUE_ARMAGETRONAD_" key " " $0
      }' \
    > "$header"

if ! grep -q TRUE_ARMAGETRONAD_VERSION "$header"; then
    echo "version header came out empty; batch/make/version produced nothing" >&2
    exit 1
fi

grep TRUE_ARMAGETRONAD_VERSION "$header"
