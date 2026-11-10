#!/bin/sh
# EXACT SID import round trip, for every examples/*.sng and many packer
# options:
#   example.sng -> pack -> a.sid -> sid2sng -exact -> b.sng -> pack -> b.sid
# Passes when a.sid and b.sid are the same byte for byte, and (for a few
# option sets) when sngcmp.py finds b.sng the same as the original: patterns
# row by row, orderlists, instruments, table programs (numbering mapped).
#
# Usage: sh roundtrip.sh [bindir]   (bindir has relocharness & sid2sng, see
# build.sh; .exe names work too). PY = python to use (default python3)
HERE=$(cd "$(dirname "$0")" && pwd)
EX=$(cd "$HERE/../../../examples" && pwd)
BIN=${1:-$HERE/build}
PY=${PY:-python3}
RELOC="$BIN/relocharness"; [ -x "$RELOC" ] || RELOC="$BIN/relocharness.exe"
SID2SNG="$BIN/sid2sng"; [ -x "$SID2SNG" ] || SID2SNG="$BIN/sid2sng.exe"
WORK=$(mktemp -d)
GT_PLAYERDIR="$BIN"; export GT_PLAYERDIR
pass=0; fail=0; same=0

while read -r opts; do
  for f in "$EX"/*.sng; do
    b=$(basename "$f" .sng)
    if ! "$RELOC" "$f" "$WORK/a.sid" $opts >/dev/null 2>&1; then
      echo "PACK FAILED [$opts] $b"; fail=$((fail+1)); continue
    fi
    if ! r=$("$SID2SNG" -exact "$WORK/a.sid" "$WORK/b.sng" 2>&1); then
      echo "FAIL [$opts] $b: $r"; fail=$((fail+1)); continue
    fi
    "$RELOC" "$WORK/b.sng" "$WORK/b.sid" $opts >/dev/null 2>&1
    if cmp -s "$WORK/a.sid" "$WORK/b.sid"; then pass=$((pass+1))
    else echo "DIFF [$opts] $b: $(cmp "$WORK/a.sid" "$WORK/b.sid" | head -1)"; fail=$((fail+1)); fi
    case "$opts" in
      -B0|-I0|"-AF000 -S2 -B1"|"-C1 -ZE0 -D1"|"-H1 -J1")
      if r=$("$PY" "$HERE/sngcmp.py" "$f" "$WORK/b.sng"); then same=$((same+1))
      else echo "SNGCMP [$opts] $b: $r"; fail=$((fail+1)); fi;;
    esac
  done
done <<OPTS
-B0
-B1
-C1 -ZE0
-C1 -Z40
-D1
-E1
-H1
-I0
-J1
-S2
-S0
-S4 -N
-N
-AF000
-AF000 -B1 -S3
-AF000 -S2 -B1
-A0F00 -O0 -R0
-A0800 -O0
-R0
-W20 -Z40
-WC0
-C1 -D1 -E1 -H1 -I0 -Z40
-C1 -I0 -ZE5
-C1 -H1 -Z80 -S2
-C1 -ZE0 -D1
-H1 -J1
-LD500 -W08
-LD420 -B1 -N -S0
-AF000 -I0 -D1
-A0000 -R0 -O0 -J1
-J1 -D1
-I0 -S2
-AF000 -J1 -H1
OPTS
rm -rf "$WORK"
echo "byte-identical repacks: $pass, structurally same: $same, failures: $fail"
[ $fail -eq 0 ]
