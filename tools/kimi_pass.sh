#!/usr/bin/env bash
# Run one comment-cleanup pass over a source file with kimi-k2.7-code via
# OpenCode, then check mechanically that it changed nothing it was told not to.
#
# The model works on a COPY in a scratch sandbox and never sees this repo, so a
# bad run cannot dirty the tree. Review the diff, then apply it yourself.
#
#     tools/kimi_pass.sh src/edition.h
#     tools/kimi_pass.sh src/edition.h --apply     # copy result over the source
#
# Guards, all of which have caught something or exist because something was
# missed: C++ code identical, indented code samples identical (kimi stripped
# semicolons out of quoted Pascal on the first run), and every address, hex
# literal and bare number still present.

set -uo pipefail

MODEL=${KIMI_MODEL:-ollama-cloud/kimi-k2.7-code}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BRIEF="$ROOT/tools/kimi_brief.txt"
SANDBOX=${KIMI_SANDBOX:-/tmp/kimi-pass}

SRC=${1:-}
APPLY=${2:-}
[ -f "$SRC" ] || { echo "usage: $0 <source-file> [--apply]"; exit 2; }
[ -f "$BRIEF" ] || { echo "missing brief: $BRIEF"; exit 2; }

BASE=$(basename "$SRC")
WORK="$SANDBOX/$BASE.d"
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$SRC" "$WORK/$BASE"
cp "$SRC" "$SANDBOX/$BASE.orig"

# The file has to be NAMED. The brief says "this file", and with nothing
# attached the model answered "What file should I rewrite the comments in?" and
# stopped - which looked exactly like a model deciding the file needed no work.
# Three files were recorded as clean no-ops that way before the logs were read.
echo "== running $MODEL on $SRC"
( cd "$WORK" && opencode run -m "$MODEL" --dir "$WORK" \
    "$(cat "$BRIEF")

The file to edit is $BASE, in the current directory." ) \
    > "$SANDBOX/$BASE.log" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "opencode failed (exit $rc); see $SANDBOX/$BASE.log"
    tail -20 "$SANDBOX/$BASE.log"
    exit 1
fi

A="$SANDBOX/$BASE.orig"
B="$WORK/$BASE"
fail=0

# 1. code outside comments must be byte-identical
codeonly() {
    # drop whole-line comments, strip trailing // comments, drop block-comment
    # bodies crudely (this codebase uses // almost exclusively)
    sed -e 's|[[:space:]]*//.*$||' "$1" | grep -vE '^[[:space:]]*$'
}
if ! diff -q <(codeonly "$A") <(codeonly "$B") >/dev/null; then
    echo "FAIL: code outside comments changed"
    diff -u <(codeonly "$A") <(codeonly "$B") | head -40
    fail=1
else
    echo "ok:   code outside comments identical"
fi

# 2. indented code samples inside comments must be byte-identical
#
# An indented block is not always code: bullet continuations and address
# listings with prose descriptions indent the same way, and de-capitalising a
# word in one of those is a legitimate edit. So a difference that survives
# lowercasing is a real change and fails, while a case-only difference is
# reported and allowed. Pascal and asm are case-insensitive, so nothing that
# matters can hide in that gap - the semicolons this guard was written for are
# punctuation and still caught.
samples() { sed -n 's|^//\( \{4,\}\S.*\)|\1|p' "$1"; }
samples_ci() { samples "$1" | tr 'A-Z' 'a-z'; }
if ! diff -q <(samples_ci "$A") <(samples_ci "$B") >/dev/null; then
    echo "FAIL: quoted code samples inside comments changed"
    diff -u <(samples "$A") <(samples "$B")
    fail=1
elif ! diff -q <(samples "$A") <(samples "$B") >/dev/null; then
    echo "note: indented blocks differ only in capitalisation - read these:"
    diff -u <(samples "$A") <(samples "$B") | grep -E '^[-+][^-+]' | sed 's/^/      /'
else
    echo "ok:   quoted code samples identical ($(samples "$A" | wc -l) lines)"
fi

# 3. every fact token must survive
tokens() {
    grep -oE '[0-9a-fA-F]{4}:[0-9a-fA-F]{4}|0x[0-9a-fA-F]+|\b[0-9]+\b' "$1" \
        | sort | uniq -c | sort -k2
}
if ! diff -q <(tokens "$A") <(tokens "$B") >/dev/null; then
    echo "WARN: address/number token counts differ (count then token)"
    diff -u <(tokens "$A") <(tokens "$B") | grep -E '^[-+][^-+]' | head -30
    fail=1
else
    echo "ok:   all address and number tokens preserved"
fi

# 4. the backtick convention. Addresses and identifiers are written `like this`
# throughout src/, and one run stripped all 102 of them out of save.h. Cheap to
# count, and invisible to every other guard here.
ticks() { grep -o '`' "$1" | wc -l; }
if [ "$(ticks "$A")" -ne "$(ticks "$B")" ]; then
    echo "FAIL: backticks $(ticks "$A") -> $(ticks "$B"), the `like this` convention changed"
    fail=1
else
    echo "ok:   backticks preserved ($(ticks "$A"))"
fi

# 5. ASCII only. One run replaced every " - " in board.h with a U+2014 em dash,
# 16 lines of it. The source is ASCII throughout and should stay that way.
nonascii() { grep -cP '[^\x00-\x7F]' "$1" 2>/dev/null || echo 0; }
if [ "$(nonascii "$B")" != "0" ] && [ "$(nonascii "$A")" = "0" ]; then
    echo "FAIL: introduced non-ASCII on $(nonascii "$B") line(s) - probably em dashes"
    grep -nP '[^\x00-\x7F]' "$B" | head -3 | sed 's/^/      /'
    fail=1
else
    echo "ok:   ASCII only"
fi

# 6. wrap width
over() { awk 'length>79' "$1" | wc -l; }
echo "info: lines over 79 cols: $(over "$A") -> $(over "$B")"

# 5. prose score, punctuation rules deliberately not counted
LINT="$ROOT/tools/comment_lint.py"
if [ -f "$LINT" ]; then
    echo "== prose score (PUN rules ignored by choice)"
    python3 "$LINT" "$A" "$B" --quiet --ignore PUN001,PUN002 2>/dev/null \
        | grep -E "words" | sed "s|$SANDBOX/||"
fi

echo
echo "== diff"
diff -u "$A" "$B" | sed -e "s|^--- .*|--- before|" -e "s|^+++ .*|+++ after|"

echo
if [ $fail -ne 0 ]; then
    echo "GUARDS FAILED - do not apply without reading the failures above"
else
    echo "all guards passed"
fi
echo "result: $B"

if [ "$APPLY" = "--apply" ]; then
    if [ $fail -ne 0 ]; then
        echo "refusing --apply while guards fail"
        exit 1
    fi
    cp "$B" "$SRC"
    echo "applied to $SRC"
fi
exit $fail
