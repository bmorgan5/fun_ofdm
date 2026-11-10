#!/usr/bin/env bash
# Runs every benchmark in fun-ofdm-bench once, on workloads of 50 ms, and
# checks that each one ran and that receiver_chain decoded frames. A smoke
# test: the numbers mean nothing on a shared CI machine.
#
# Usage: bench-smoke-test.sh <tools/qemu/run.sh> <fun-ofdm-bench>
set -euo pipefail

run="$1"
bench="$2"
out="${TEST_TMPDIR:-$(mktemp -d)}/bench.json"

# JSON to a file: frame_decoder prints to stdout.
"$run" "$bench" --workload_seconds=0.05 --stage_seconds=0.02 \
  --benchmark_min_time=1x --benchmark_out="$out" --benchmark_out_format=json

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

for workload in W-idle W-6 W-54; do
  for name in frame_detector timing_sync fft_symbols channel_est \
    phase_tracker frame_decoder receiver_chain; do
    # google_benchmark adds /real_time to the name
    grep -q "\"name\": \"$name/$workload/" "$out" ||
      fail "$name/$workload didn't run"
  done
done
grep -q '"error_occurred"' "$out" && fail "a benchmark failed: $(cat "$out")"

# Each receiver_chain benchmark reports how many frames it decoded. W-6 and
# W-54 each send some in 50 ms, so a receiver that decodes none is broken. In
# the JSON, a benchmark's counters follow its name.
received() {
  awk -v name="\"receiver_chain/$1/" '
    index($0, "\"name\": " name) { found = 1 }
    found && /"frames_received":/ { gsub(/[",]/, "", $2); print $2; exit }
  ' "$out"
}
for workload in W-6 W-54; do
  frames=$(received "$workload")
  awk -v n="${frames:-0}" 'BEGIN { exit !(n > 0) }' ||
    fail "receiver_chain/$workload decoded no frames: ${frames:-none}"
done
echo PASS
