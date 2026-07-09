#!/bin/bash
# Full-matrix regression for automatic pump recovery: every fixture must
# recover to within TOLERANCE dB of the clean original, fully automatically.
# Usage: tools/render/matrix_sweep.sh [workdir]
set -u

RENDER="$(dirname "$0")/../../build/depump_render_artefacts/Release/depump_render"
WORK="${1:-$(mktemp -d)}"
TOLERANCE=0.5
FAILURES=0
TOTAL=0

for SR in 44100 48000 96000; do
  for RATE in 1 2 4; do
    for DEPTH in 3 6 12; do
      for PHASE in 0 0.25 0.6; do
        TOTAL=$((TOTAL + 1))
        DIR="$WORK/sr${SR}_r${RATE}_d${DEPTH}_p${PHASE}"
        "$RENDER" --make-fixture --out-dir "$DIR" --sr "$SR" --seconds 8 \
          --rate "$RATE" --depth "$DEPTH" --phase "$PHASE" > /dev/null || { echo "FIXTURE FAIL $DIR"; FAILURES=$((FAILURES+1)); continue; }
        "$RENDER" --in "$DIR/pumped.wav" --auto --out "$DIR/recovered.wav" \
          --envelope "$DIR/recovered.csv" > "$DIR/auto.log" || { echo "AUTO FAIL $DIR"; FAILURES=$((FAILURES+1)); continue; }
        "$RENDER" --in "$DIR/clean.wav" --envelope "$DIR/clean.csv" > /dev/null
        RESULT=$("$RENDER" --compare "$DIR/recovered.csv" --with "$DIR/clean.csv" --tolerance "$TOLERANCE")
        if [ $? -ne 0 ]; then
          echo "RECOVERY FAIL sr=$SR rate=$RATE depth=$DEPTH phase=$PHASE: $RESULT"
          FAILURES=$((FAILURES + 1))
        fi
      done
    done
  done
done

echo "matrix sweep: $((TOTAL - FAILURES))/$TOTAL fixtures recovered within ${TOLERANCE} dB"
[ "$FAILURES" -eq 0 ]
