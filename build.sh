#!/bin/sh
# Rebuild TINY for macOS with clang (the Mac BUILD.BAT).
# TINY.C is not changed: mac/ stands in for Turbo C's conio.h, dos.h and
# io.h (screen, keyboard, timer and PC speaker). Makes ./tiny
set -e
cd "$(dirname "$0")"

if ! command -v cc >/dev/null 2>&1; then
    echo "No C compiler. Install the Xcode command line tools:  xcode-select --install" >&2
    exit 1
fi

obj=$(mktemp -d)
trap 'rm -rf "$obj"' EXIT

cc -O2 -Wall -c mac/tcmac.c -o "$obj/tcmac.o"
# -x c: clang would take the capital .C for C++
cc -O2 -Wall -Wno-deprecated-declarations -Wno-unused-but-set-variable \
   -Imac -include mac/prelude.h -x c -c TINY.C -o "$obj/tiny.o"
cc "$obj/tiny.o" "$obj/tcmac.o" -framework AudioToolbox -o tiny

echo "Built ./tiny  -  run it with:  ./tiny"
