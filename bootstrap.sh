#!/bin/sh
# Populate the runtime tree (lib-run/) from the distribution tree (lib/).
# Idempotent: existing files in lib-run/ are NOT overwritten.

set -e

src=lib
dst=lib-run

if [ ! -d "$src" ]; then
    echo "bootstrap: $src not found; run from repo root." >&2
    exit 1
fi

mkdir -p "$dst"

# cp -n on BSD/GNU both mean "don't overwrite".  -R for recursive, -p to
# preserve mode.
cp -Rnp "$src"/. "$dst"/

# Ensure runtime-only subdirs exist (they may ship empty).
for d in etc plrobjs plralias house \
         plrobjs/A-E plrobjs/F-J plrobjs/K-O plrobjs/P-T plrobjs/U-Z plrobjs/ZZZ \
         plralias/A-E plralias/F-J plralias/K-O plralias/P-T plralias/U-Z plralias/ZZZ; do
    mkdir -p "$dst/$d"
done
mkdir -p log

echo "bootstrap: runtime tree ready at $dst/"
