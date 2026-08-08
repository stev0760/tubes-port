#!/usr/bin/env bash
# Run one comment-cleanup pass over a source file through a second model via
# OpenCode, then check mechanically that it changed nothing it was told not to.
#
# The model works on a COPY in a scratch sandbox and never sees this repo, so a
# bad run cannot dirty the tree. Review the diff, then apply it yourself.
#
#     tools/comment_pass.sh src/edition.h
#     tools/comment_pass.sh src/edition.h --apply    # copy result over the source
#     tools/comment_pass.sh src/main.cpp --lines 1,832
#     PASS_MODEL=ollama-cloud/kimi-k2.7-code tools/comment_pass.sh src/game.cpp
#
# Two models have been used on this codebase and they behave very differently.
# `deepseek-v4-flash` is the default because it does one job exactly: it lowers
# capitals used as emphasis and touches nothing else - 192 changed lines across
# board_test.cpp without moving a brace, a backtick or a proper noun.
# `kimi-k2.7-code` is more ambitious and will restructure a tangled paragraph,
# rewrap, and backtick bare addresses correctly; it also, over one sweep,
# deleted a statement, deleted a comment block, stripped 102 backticks and
# once reported an edit it had not made. Pick by the job: kimi when prose
# genuinely needs rewriting, deepseek to finish.
#
# Either way the guards below are the reason this is safe to run at all. Every
# one exists because something got through: C++ code identical, indented code
# samples identical, addresses and numbers preserved, backticks not stripped,
# ASCII only, and no capitalised proper noun lost to the de-shouting.

set -uo pipefail

MODEL=${PASS_MODEL:-ollama-cloud/deepseek-v4-flash:0731}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BRIEF="$ROOT/tools/comment_brief.txt"
SANDBOX=${PASS_SANDBOX:-/tmp/comment-pass}

SRC=""; APPLY=""; RANGE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) APPLY=--apply ;;
        --lines) RANGE=${2:-}; shift ;;
        --lines=*) RANGE=${1#--lines=} ;;
        -*) echo "unknown option: $1"; exit 2 ;;
        *) SRC=$1 ;;
    esac
    shift
done
[ -f "$SRC" ] || { echo "usage: $0 <source-file> [--lines A,B] [--apply]"; exit 2; }
[ -f "$BRIEF" ] || { echo "missing brief: $BRIEF"; exit 2; }

# --lines A,B works on a slice instead of the whole file. main.cpp is 6344
# lines with no section dividers and a main() that runs 3100 of them, so it goes
# through in nine passes rather than one unreviewable rewrite. Slice boundaries
# are chosen so a slice never begins mid-paragraph; the guards below are all
# line-oriented comparisons and work on a slice unchanged.
BASE=$(basename "$SRC")
if [ -n "$RANGE" ]; then
    L1=${RANGE%,*}; L2=${RANGE#*,}
    case "$L1$L2" in *[!0-9]*) echo "bad --lines: $RANGE"; exit 2 ;; esac
    NLINES=$(wc -l < "$SRC")
    [ "$L2" -le "$NLINES" ] || { echo "--lines $RANGE past EOF ($NLINES)"; exit 2; }
    TAG="$BASE.L$L1-$L2"
else
    TAG="$BASE"
fi

WORK="$SANDBOX/$TAG.d"
rm -rf "$WORK"; mkdir -p "$WORK"
if [ -n "$RANGE" ]; then
    sed -n "${L1},${L2}p" "$SRC" > "$WORK/$BASE"
    sed -n "${L1},${L2}p" "$SRC" > "$SANDBOX/$TAG.orig"
    echo "== slice $L1-$L2 of $SRC ($((L2 - L1 + 1)) lines)"
else
    cp "$SRC" "$WORK/$BASE"
    cp "$SRC" "$SANDBOX/$TAG.orig"
fi

# The file has to be NAMED. The brief says "this file", and with nothing
# attached the model answered "What file should I rewrite the comments in?" and
# stopped - which looked exactly like a model deciding the file needed no work.
# Three files were recorded as clean no-ops that way before the logs were read.
# A slice is an EXTRACT: its braces do not balance and it may open or close
# mid-function. Say so, or the model tries to repair it.
SLICE_NOTE=""
if [ -n "$RANGE" ]; then
    SLICE_NOTE="

This file is an EXTRACT - lines $L1 to $L2 of a larger source file. Its braces
do not balance and it may begin or end in the middle of a function. That is
expected and is not something to fix. Edit the comments in what you have been
given and change nothing else."
fi

echo "== running $MODEL on $SRC"
( cd "$WORK" && opencode run -m "$MODEL" --dir "$WORK" \
    "$(cat "$BRIEF")

The file to edit is $BASE, in the current directory.$SLICE_NOTE" ) \
    > "$SANDBOX/$TAG.log" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "opencode failed (exit $rc); see $SANDBOX/$TAG.log"
    tail -20 "$SANDBOX/$TAG.log"
    exit 1
fi

A="$SANDBOX/$TAG.orig"
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
# Losing them is the failure. Gaining them is usually right - the model has
# backticked bare addresses and resource names that the convention wanted
# marked all along - so that is a note to read, not a stop.
ticks() { grep -o '`' "$1" | wc -l; }
if [ "$(ticks "$B")" -lt "$(ticks "$A")" ]; then
    echo "FAIL: backticks $(ticks "$A") -> $(ticks "$B"), inline-code markup was stripped"
    fail=1
elif [ "$(ticks "$B")" -gt "$(ticks "$A")" ]; then
    echo "note: backticks $(ticks "$A") -> $(ticks "$B"), newly marked:"
    diff -u "$A" "$B" | grep -E '^\+' | grep -oE '`[^`]+`' | sort -u | tr '\n' ' ' \
        | sed -e 's/^/      /' -e 's/$/\n/'
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

# 7. A slice may change length - rewrapping a paragraph is most of the point -
# and the splice below handles that. What it cannot do is renumber the slices
# still to come, so WORK THROUGH A FILE'S SLICES LAST TO FIRST. Then every
# range still refers to the lines it was computed against.
if [ -n "$RANGE" ]; then
    la=$(wc -l < "$A"); lb=$(wc -l < "$B")
    if [ "$la" -ne "$lb" ]; then
        echo "info: slice length $la -> $lb (later slices shift; apply last-to-first)"
    else
        echo "info: slice length unchanged ($la lines)"
    fi
fi

# 8. proper nouns lost to the de-shouting pass.
#
# The job is to lowercase capitals used as emphasis, and some capitals are
# NAMES: `AntiMatter` became "antimatter" and `HITATOM` became "HitAtom" in one
# run of game.cpp. Nothing else in this script can see that - a case change
# loses no token, no backtick, no line and no word.
#
# Two shapes are checked, and both only inside comments:
#
#   MixedCase   an internal capital (AntiMatter, HitAtom, MysteryBall). That
#               shape is a name almost by definition, so losing one is a fail.
#   ALLCAPS     only flagged when the same token also appears OUTSIDE a comment
#               somewhere in src/ - that is what separates a resource like
#               HITATOM.SFX from ordinary emphasis like NOT or THREE, which is
#               exactly what this pass is supposed to remove.
if command -v python3 >/dev/null; then
    names_out=$(python3 - "$A" "$B" "$ROOT" <<'PY'
import re, sys, pathlib
from collections import Counter

a, b, root = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])

def comments(path):
    out = []
    for line in pathlib.Path(path).read_text(errors="replace").splitlines():
        s = line.strip()
        if s.startswith("//"):
            out.append(s)
        elif "//" in line:
            out.append(line.split("//", 1)[1])
    return "\n".join(out)

MIXED = re.compile(r"\b[A-Z][a-z]+[A-Z][A-Za-z]*\b")
CAPS = re.compile(r"\b[A-Z]{3,}[0-9]*\b")

# All-caps English words this pass exists to remove. Without this the guard
# reports every successful de-shout as a lost name.
EMPHASIS = {
    "NOT", "ONLY", "ONE", "TWO", "ALL", "AND", "THE", "OUT", "OVER", "OFF",
    "NOW", "WHY", "HOW", "SAME", "EACH", "BOTH", "RUNS", "TEST", "SEED",
    "THREE", "FOUR", "FIVE", "SIX", "TEN", "ANY", "NONE", "EVERY", "MUST",
    "NEVER", "ALWAYS", "FIRST", "LAST", "NEXT", "THEN", "BEFORE", "AFTER",
    "PER", "WAS", "ARE", "YES", "NO", "ZERO", "ADD", "SET", "SELECT",
    "GAME", "NAME", "WAVE", "PAGE", "ROW", "TYPE", "SIZE", "FILE",
}
# Deliberately NOT stop-listed even though they read as ordinary words:
# MARKER, DEMO, BONUS. Each is also a resource stem (`MARKER.CSP`,
# `DEMO.SCR`), so a false positive on them is cheaper than missing a real one.

# tokens that appear outside comments anywhere in src/ - identifiers, string
# literals, resource names. Built once; cheap enough at this repo's size.
in_code = set()
for f in (root / "src").glob("*.[ch]*"):
    for line in f.read_text(errors="replace").splitlines():
        code = line.split("//", 1)[0]
        in_code.update(CAPS.findall(code))
        in_code.update(MIXED.findall(code))

ca, cb = comments(a), comments(b)
lost = []
for pat, kind in ((MIXED, "mixed-case"), (CAPS, "all-caps")):
    before, after = Counter(pat.findall(ca)), Counter(pat.findall(cb))
    for tok, n in (before - after).items():
        if kind == "all-caps":
            if tok in EMPHASIS or tok not in in_code:
                continue      # ordinary emphasis; removing it is the point
        lost.append((tok, n, kind))

for tok, n, kind in sorted(lost):
    print(f"{tok}\t{n}\t{kind}")
PY
)
    if [ -n "$names_out" ]; then
        echo "FAIL: capitalised names lost from comments - a de-shout hit a proper noun"
        echo "$names_out" | while IFS=$'\t' read -r tok n kind; do
            echo "      $tok (x$n, $kind)"
        done
        fail=1
    else
        echo "ok:   no capitalised names lost"
    fi
fi

# 9. prose score, punctuation rules deliberately not counted
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
    if [ -n "$RANGE" ]; then
        tmp=$(mktemp)
        { [ "$L1" -gt 1 ] && sed -n "1,$((L1 - 1))p" "$SRC"
          cat "$B"
          sed -n "$((L2 + 1)),\$p" "$SRC"; } > "$tmp"
        # never leave a truncated source behind if something above went wrong
        if [ ! -s "$tmp" ]; then
            echo "refusing to apply: spliced result is empty"
            rm -f "$tmp"; exit 1
        fi
        mv "$tmp" "$SRC"
        echo "spliced lines $L1-$L2 back into $SRC"
    else
        cp "$B" "$SRC"
        echo "applied to $SRC"
    fi
fi
exit $fail
