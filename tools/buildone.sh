#!/bin/sh
# gcc 11.4 falls over with an internal compiler error at -O3 on some MAME
# sources, among them src/devices/cpu/i386/i386.cpp. Build just that object at
# -O1 and let the normal build carry on.
#
#   MAME_TREE=/path/to/mame tools/buildone.sh src/devices/cpu/i386/i386.cpp
#
set -e
: "${MAME_TREE:?set MAME_TREE to the root of a MAME source tree}"
[ -n "$1" ] || { echo "usage: buildone.sh src/devices/.../file.cpp"; exit 2; }

cd "$MAME_TREE/build/projects/sdl/mamesun386i/gmake-linux"
SRC="../../../../../$1"
OBJ="../../../../linux_gcc/obj/x64/Release/$(echo "$1" | sed 's/\.cpp$/.o/')"
mkdir -p "$(dirname "$OBJ")"

g++ -m64 -pipe -O1 -std=c++20 -fno-strict-aliasing \
    -DMAME_DEBUG -DMAME_PROFILER -DCRLF=2 -DLSB_FIRST -DFLAC__NO_DLL \
    -DPUGIXML_HEADER_ONLY -DASMJIT_STATIC \
    -I../../../../../src/osd -I../../../../../src/emu \
    -I../../../../../src/devices -I../../../../../src/lib \
    -I../../../../../src/lib/util -I../../../../../3rdparty \
    -I../../../../../3rdparty/asmjit -I../../../../generated/emu \
    -I../../../../generated/emu/layout -I../../../../../3rdparty/asio/include \
    -I../../../../../3rdparty/expat/lib -I../../../../../3rdparty/flac/include \
    -c "$SRC" -o "$OBJ"
ls -l "$OBJ"
