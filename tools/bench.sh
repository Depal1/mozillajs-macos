#!/bin/bash
# bench.sh — run the 4-cell benchmark kernels against one js shell.
# Usage: bench.sh <path-to-js-shell> <cell-label>
# Each kernel runs 3 times; the best (minimum) wall time in ms is reported.
set -eu

SHELL_BIN="$1"
LABEL="${2:-shell}"
DIR="$(cd "$(dirname "$0")" && pwd)"
KERNELS="math strings arrays objects"

for k in $KERNELS; do
    best=""
    for run in 1 2 3; do
        S=$(perl -MTime::HiRes=time -e 'print time')
        "$SHELL_BIN" "$DIR/bench-$k.js" > /dev/null
        E=$(perl -MTime::HiRes=time -e 'print time')
        ms=$(perl -e 'printf "%.1f", ($ARGV[1] - $ARGV[0]) * 1000' "$S" "$E")
        if [ -z "$best" ] || perl -e 'exit(($ARGV[0] < $ARGV[1]) ? 0 : 1)' "$ms" "$best"; then
            best="$ms"
        fi
    done
    printf "%s\t%s\t%s ms\n" "$LABEL" "$k" "$best"
done
