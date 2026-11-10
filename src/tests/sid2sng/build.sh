#!/bin/sh
# Builds the SID import test tools into tests/sid2sng/build (or $1):
#   relocharness  the packer: the unmodified greloc.c + asm/ with a small
#                 stand-in for the tracker (no SDL needed), options like
#                 gt2reloc (see harness.c)
#   sid2sng       the SID importer command line tool
# CC defaults to cc (gcc or clang).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
OUT=${1:-$HERE/build}
CC=${CC:-cc}

mkdir -p "$OUT"
# greloc.c includes "goattrk2.h": a copy next to the stand-in picks it up
cp "$SRC/greloc.c" "$HERE/goattrk2.h" "$HERE/harness.c" "$SRC/player.s" "$SRC/altplayer.s" "$OUT/"
$CC -O2 -DGT2RELOC -I"$OUT" -I"$SRC" -I"$SRC/asm" -o "$OUT/relocharness" \
  "$OUT/harness.c" "$OUT/greloc.c" "$SRC"/asm/*.c
$CC -O2 -Wall -o "$OUT/sid2sng" \
  "$SRC/sid2sng.c" "$SRC/gsidimport.c" "$SRC/gsngwrite.c" "$SRC/cpu6502.c" -lm
echo "Built $OUT/relocharness and $OUT/sid2sng"
