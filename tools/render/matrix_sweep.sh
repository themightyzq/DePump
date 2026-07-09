#!/bin/bash
# Full-matrix regression for automatic pump recovery (81 synthetic fixtures).
#
# v1 bar (re-baselined 2026-07-09, owner-approved): recovery is judged as a
# distribution, not all-or-nothing — short fixtures with heavy tonal beating
# are estimation-noise-limited at deep dips. Gate:
#   - at least STRICT_MIN fixtures within 0.5 dB of clean (no regression)
#   - no fixture worse than HARD_CAP dB (from 3-12 dB of baked-in pumping)
# The 0.5 dB-everywhere target remains the v1.1 stretch goal (see ROADMAP).
#
# Usage: tools/render/matrix_sweep.sh [workdir]
set -u

RENDER="$(dirname "$0")/../../build/depump_render_artefacts/Release/depump_render"
WORK="${1:-$(mktemp -d)}"
STRICT_MIN=59
HARD_CAP=1.8

RESULTS="$WORK/results.txt"
mkdir -p "$WORK"
: > "$RESULTS"

for SR in 44100 48000 96000; do
  for RATE in 1 2 4; do
    for DEPTH in 3 6 12; do
      for PHASE in 0 0.25 0.6; do
        DIR="$WORK/sr${SR}_r${RATE}_d${DEPTH}_p${PHASE}"
        "$RENDER" --make-fixture --out-dir "$DIR" --sr "$SR" --seconds 8 \
          --rate "$RATE" --depth "$DEPTH" --phase "$PHASE" > /dev/null || { echo "999 $DIR fixture-fail" >> "$RESULTS"; continue; }
        "$RENDER" --in "$DIR/pumped.wav" --auto --out "$DIR/recovered.wav" \
          --envelope "$DIR/recovered.csv" > "$DIR/auto.log" || { echo "999 $DIR auto-fail" >> "$RESULTS"; continue; }
        "$RENDER" --in "$DIR/clean.wav" --envelope "$DIR/clean.csv" > /dev/null
        MAX=$("$RENDER" --compare "$DIR/recovered.csv" --with "$DIR/clean.csv" | sed 's/.*max //;s/ dB.*//')
        echo "$MAX sr=$SR rate=$RATE depth=$DEPTH phase=$PHASE" >> "$RESULTS"
      done
    done
  done
done

sort -rn "$RESULTS" | head -5 | sed 's/^/  worst: /'
awk -v strict_min="$STRICT_MIN" -v cap="$HARD_CAP" '
  {n++; if ($1 <= 0.5) s++; if ($1 <= 1.0) one++; if ($1 > w) w = $1}
  END {
    printf "matrix sweep: %d/%d within 0.5 dB, %d/%d within 1.0 dB, worst %.3f dB\n", s, n, one, n, w
    if (s < strict_min) { printf "FAIL: strict count %d below regression floor %d\n", s, strict_min; exit 1 }
    if (w > cap)        { printf "FAIL: worst %.3f dB exceeds hard cap %.1f dB\n", w, cap; exit 1 }
    print "PASS (v1 bar)"
  }' "$RESULTS"
