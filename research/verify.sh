#!/bin/bash
# Usage: verify.sh path/to/candidate.cpp  -> compiles (O2 strict warnings + ASan/UBSan), runs all tests broadly.
set -u
W=$(dirname "$0"); C="$1"; N=$(basename "$C" .cpp); B=$W/bin/$N
python3 -I $W/check_rules.py $W/template.cpp "$C" || true
g++ -std=c++17 -O2 -Wall -Wextra -pedantic "$C" -o $B 2> $B.warn; rc=$?
if [ $rc -ne 0 ]; then echo "COMPILE FAIL"; head -30 $B.warn; exit 1; fi
[ -s $B.warn ] && { echo "COMPILER WARNINGS:"; head -20 $B.warn; } || echo "no compiler warnings"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$C" -o $B.asan 2>/dev/null || { echo "ASAN COMPILE FAIL"; exit 1; }
fail=0
out=$($B --test all); echo "$out" | tail -1; echo "$out" | grep -q "SUMMARY 20/20" || { echo "$out" | grep FAIL; fail=1; }
for sc in 1 2 3 4 5 7 8 13 17 31 64 100 257 1000 4096 20000; do
  for sd in 0 1 42 7 123456 4294967295; do
    r=$($B --test all --scale $sc --seed $sd | tail -1); [ "$r" = "SUMMARY 20/20" ] || { echo "FAIL scale=$sc seed=$sd: $r"; fail=1; }
  done
done
for sc in 1 3 100 2000; do for sd in 1 42 99; do
  r=$(ASAN_OPTIONS=detect_leaks=1 $B.asan --test all --scale $sc --seed $sd 2>&1 | grep -v "^PASS" | tail -3); echo "$r" | grep -q "^SUMMARY 20/20$" || { echo "ASAN FAIL scale=$sc seed=$sd: $r"; fail=1; }
done; done
d=$($B --demo); exp=$'> network\nresults 2\n2 notes.txt\n3 report.txt\n> network AND security\nresults 1\n3 report.txt\n> network OR data\nresults 3\n1 book.txt\n2 notes.txt\n3 report.txt'
[ "$d" = "$exp" ] || { echo "DEMO MISMATCH"; fail=1; }
for f in $W/wl/*.csv $W/../data/small.csv; do $B.asan --bench --workload $f >/dev/null 2>$B.err || { echo "ASAN BENCH FAIL $f"; head -5 $B.err; fail=1; }; done
for n in 1 2 3 100 5000 50000; do $B.asan --bench --n $n >/dev/null 2>$B.err || { echo "ASAN SYNTH BENCH FAIL n=$n"; head -5 $B.err; fail=1; }; done
[ $fail -eq 0 ] && echo "VERIFY PASS" || echo "VERIFY FAIL"
