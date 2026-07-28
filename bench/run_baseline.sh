#!/usr/bin/env bash
# Runs bin/bench several times back-to-back and reports the MEDIAN of each
# percentile across trials, instead of trusting any single run.
#
# WHY THIS EXISTS: on a personal laptop (browser, IDE, OS background tasks,
# WSL2 virtualization overhead all competing for the CPU), any single
# benchmark run can get unlucky and catch a burst of unrelated system
# activity. One run is a data point, not a result. Taking the median across
# several runs is a standard way to get a number you can actually defend
# and compare against later ("before vs after an optimization") without one
# noisy outlier throwing off the comparison.
#
# Usage:
#   ./bench/run_baseline.sh [num_trials] [events_per_trial]
#   ./bench/run_baseline.sh          # defaults: 7 trials of 500,000 events
#   ./bench/run_baseline.sh 10 1000000

set -euo pipefail

RUNS=${1:-7}
N=${2:-500000}
BIN="$(dirname "$0")/../bin/bench"

if [ ! -x "$BIN" ]; then
    echo "bin/bench not found — run 'make bench' first." >&2
    exit 1
fi

echo "Running $RUNS trials of $N events each..."
echo "(close other apps for cleaner results — browser/IDE compete for CPU with this)"
echo

p50s=(); p90s=(); p99s=(); p999s=()

for i in $(seq 1 "$RUNS"); do
    line=$("$BIN" "$N" | grep "addOrder latency")
    echo "trial $i: $line"
    p50s+=("$(echo "$line" | grep -oP 'p50=\s*\K[0-9]+')")
    p90s+=("$(echo "$line" | grep -oP 'p90=\s*\K[0-9]+')")
    p99s+=("$(echo "$line" | grep -oP 'p99=\s*\K[0-9]+')")
    p999s+=("$(echo "$line" | grep -oP 'p99\.9=\s*\K[0-9]+')")
done

median() {
    printf '%s\n' "$@" | sort -n | awk '
        {a[NR]=$1}
        END {
            if (NR % 2 == 1) print a[(NR+1)/2];
            else print int((a[NR/2] + a[NR/2+1]) / 2);
        }'
}

echo
echo "--- Median across $RUNS trials (addOrder latency, ns) ---"
echo "p50:   $(median "${p50s[@]}")"
echo "p90:   $(median "${p90s[@]}")"
echo "p99:   $(median "${p99s[@]}")"
echo "p99.9: $(median "${p999s[@]}")"
echo
echo "If any single trial looks wildly different from the rest, that's the"
echo "noisy-neighbor effect described above — the median absorbs it, a raw"
echo "average would not."
