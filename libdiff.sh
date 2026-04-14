#!/bin/sh
# Show differences between the distribution tree (lib/) and the runtime
# tree (lib-run/), excluding runtime-only paths.

set -e

src=lib
dst=lib-run

if [ ! -d "$src" ] || [ ! -d "$dst" ]; then
    echo "libdiff: need both $src/ and $dst/ present." >&2
    exit 1
fi

exec diff -r -q \
    --exclude='board.immort' \
    --exclude='players' \
    --exclude='players.*' \
    --exclude='plrmail' \
    --exclude='hcontrol' \
    --exclude='time' \
    --exclude='config' \
    --exclude='*.objs' \
    --exclude='*.alias' \
    --exclude='mudwho.html' \
    --exclude='mudwho.html.last' \
    --exclude='README' \
    "$src" "$dst"
