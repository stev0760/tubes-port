#!/usr/bin/env bash
# Capture every directly-addressable screen to a BMP, or compare two captures.
#
#     tools/screen_sweep.sh capture BASELINE_DIR
#     ... change the code, rebuild ...
#     tools/screen_sweep.sh capture AFTER_DIR
#     tools/screen_sweep.sh compare BASELINE_DIR AFTER_DIR
#
# This exists because src/main.cpp has NO test coverage: tubes-tests compiles 22
# sources, does not include main.cpp and does not link SDL at all, so all 995
# checks come from board_test.cpp. Refactoring main.cpp therefore has no
# regression signal from the suite, and the only proof available is that every
# screen still renders the same pixels.
#
# It can be exact rather than approximate because `--screenshot` marks the run
# `scripted`, which pins the seed to 0x9E3779B9, disables fades and hands the
# run a blocked `PlayerFiles` so no file of the player's is written. Two builds of an unchanged renderer must produce identical
# bytes, so the comparison is `cmp`, not a tolerance. Verified before this
# script was written: the same capture run twice is byte-identical, play frames
# included.
#
# Not the same thing as ~/Dev/tubes-tooling/diff_frame.py, which compares the
# PORT against the ORIGINAL under DOSBox and needs masking and a tolerance of 6
# because the two expand the 6-bit DAC differently. This compares the port
# against itself, which is stricter and far quicker.
#
# The screens NOT covered here, and why: the end-of-wave banner, the stats
# screen and the Continue prompt are reachable only by playing, and the only
# route to them is `--screenshot-after N`, where N is a raw presented-frame
# count - so any change to frame pacing shifts which screen lands in the file
# and the diff reads as a total mismatch rather than as a rendering change. The
# pause overlay has no flag at all. Those four need eyes on the running game.

set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${PORT:-$ROOT/build/tubes-port}
GAMEDIR=${GAMEDIR:-$ROOT/..}

mode=${1:-}
case "$mode" in
    capture)
        OUT=${2:-}
        [ -n "$OUT" ] || { echo "usage: $0 capture OUTDIR"; exit 2; }
        [ -x "$PORT" ] || { echo "no port binary at $PORT"; exit 2; }
        mkdir -p "$OUT"
        ;;
    compare)
        A=${2:-}; B=${3:-}
        [ -d "$A" ] && [ -d "$B" ] || { echo "usage: $0 compare DIR_A DIR_B"; exit 2; }
        same=0; diffs=0; missing=0
        for f in "$A"/*.bmp; do
            n=$(basename "$f")
            if [ ! -f "$B/$n" ]; then
                echo "MISSING in $B: $n"; missing=$((missing + 1)); continue
            fi
            if cmp -s "$f" "$B/$n"; then
                same=$((same + 1))
            else
                echo "DIFFERS: $n"; diffs=$((diffs + 1))
            fi
        done
        for f in "$B"/*.bmp; do
            n=$(basename "$f")
            [ -f "$A/$n" ] || { echo "NEW in $B: $n"; missing=$((missing + 1)); }
        done
        echo "$same identical, $diffs differing, $missing missing/new"
        [ $diffs -eq 0 ] && [ $missing -eq 0 ]
        exit $?
        ;;
    *)
        echo "usage: $0 capture OUTDIR | compare DIR_A DIR_B"; exit 2 ;;
esac

fails=0
shot() {
    local name=$1; shift
    if ! SDL_VIDEODRIVER=dummy "$PORT" --gamedir "$GAMEDIR" --no-music \
            "$@" --screenshot "$OUT/$name.bmp" >/dev/null 2>&1; then
        echo "  FAILED: $name ($*)"; fails=$((fails + 1)); return
    fi
    [ -s "$OUT/$name.bmp" ] || { echo "  EMPTY: $name"; fails=$((fails + 1)); }
}

echo "== capturing to $OUT"

# title and menu pages
shot title            --title
for p in 1 2 3 4 5 6 7; do shot "title-page$p" --title $p; done
shot title-sw-page1   --shareware --title 1

# high scores, entry and viewer
shot hiscores-endurance --hiscores 0
shot hiscores-wave      --hiscores 1
shot hs-entry           --hs-entry

# overlays and the port's own screens
shot f1                 --f1
shot f1-wave            --f1 --wave 5
shot f2-save            --f2
shot rebind             --rebind
shot graphics           --graphics

# the four decks
for s in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    shot "instructions-$s" --instructions $s
done
shot instructions-joke  --instructions 0 --joke
shot credits            --credits
for p in 0 1 2 3; do shot "ordering-$p" --ordering $p; done
shot registration       --registration

# edition prompt, endings, sign-off
shot edition-prompt-0   --edition-prompt 0
shot edition-prompt-1   --edition-prompt 1
shot ending-text        --ending 1
shot ending-prize       --ending 2
shot exit-screen        --exit-screen

# timed screens, addressed by frame index
for f in 0 1 2 3 4 5 6 7 8 9 10 11; do shot "splash2-$f" --splash2 $f; done
for p in 0 1 2 3 4; do shot "cutscene-$p" --cutscene $p; done

# play frames, backdrop pinned so the roll cannot move them
shot play-open          --gamebg GAMEBG1.GFX
shot play-auto200       --auto 200 --gamebg GAMEBG1.GFX
shot play-auto600       --auto 600 --gamebg GAMEBG1.GFX
for w in 1 12 25 75; do shot "briefing-wave$w" --wave $w --gamebg GAMEBG1.GFX; done

n=$(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)
echo "== $n screens captured, $fails failed"
exit $((fails > 0))
