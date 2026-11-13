#!/bin/bash
# Generates the CSV workloads used by bench.py (same shape as data/small.csv).
W=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W/wl" "$W/bin"
for n in 200 1500 5000; do python3 "$W/gen_wl.py" $n $n "$W/wl/csv$n.csv"; done
for n in 100 300 500 700 1000 2000 3000 4000 6500 8000; do python3 "$W/gen_wl.py" $n $((n+7)) "$W/wl/csv$n.csv"; done
echo "workloads written to $W/wl"
