#!/bin/sh
# Copy the driver into a MAME source tree and build the sun386i subtarget.
#
#   MAME_TREE=/path/to/mame tools/build.sh
#
set -e
: "${MAME_TREE:?set MAME_TREE to the root of a MAME source tree}"
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../driver"

mkdir -p "$MAME_TREE/src/mame/sun"
cp "$SRC/sun386i.cpp"      "$MAME_TREE/src/mame/sun/sun386i.cpp"
cp "$SRC/sun386i_prom.hxx" "$MAME_TREE/src/mame/sun/sun386i_prom.hxx"
cp "$SRC/sun386i_font.hxx" "$MAME_TREE/src/mame/sun/sun386i_font.hxx"

cd "$MAME_TREE"
if ! grep -q '^@source:sun/sun386i.cpp' src/mame/mame.lst; then
    printf '\n@source:sun/sun386i.cpp\nsun386i\n' >> src/mame/mame.lst
fi

exec make SUBTARGET=sun386i SOURCES=src/mame/sun/sun386i.cpp REGENIE=1 \
     NOWERROR=1 -j"${JOBS:-2}"
