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

sh "$root/batch/make/version" --verbose "$root" \
    | awk '{ print "#define TRUE_ARMAGETRONAD_" $1 " " substr($0, index($0, $2)) }' \
    > "$header"

if ! grep -q TRUE_ARMAGETRONAD_VERSION "$header"; then
    echo "version header came out empty; batch/make/version produced nothing" >&2
    exit 1
fi

grep TRUE_ARMAGETRONAD_VERSION "$header"
