#!/usr/bin/env bash
# Unpack the LZEXE-compressed TUBES.EXE into a plain MZ executable.
#
# TUBES.EXE ships compressed with LZEXE v0.91; a disassembler sees only the
# ~2.5KB decompressor stub and 43KB of entropy until this step is run.
#
# Usage: tools/unpack.sh /path/to/TUBES.EXE [outfile]

set -euo pipefail

SRC="${1:?usage: unpack.sh /path/to/TUBES.EXE [outfile]}"
OUT="${2:-assets-extracted/TUBES_UNP.EXE}"

BUILD="${TMPDIR:-/tmp}/unlzexe-build"
BIN="$BUILD/unlzexe"

if [[ ! -x "$BIN" ]]; then
    echo "building unlzexe..."
    mkdir -p "$BUILD"
    git clone -q --depth 1 https://github.com/mywave82/unlzexe.git "$BUILD/src" 2>/dev/null || true
    cc -O2 -w -o "$BIN" "$BUILD/src/unlzexe.c"
fi

mkdir -p "$(dirname "$OUT")"

# unlzexe truncates the output name to an 8.3 DOS extension, so unpack to a
# scratch name and rename afterwards.
TMP="$BUILD/out.exe"
rm -f "$TMP" "${TMP%.exe}.ex"
"$BIN" "$SRC" "$TMP"
[[ -f "$TMP" ]] || TMP="${TMP%.exe}.ex"
mv "$TMP" "$OUT"

echo "unpacked -> $OUT"
