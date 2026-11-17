#!/bin/bash
# run_tests.sh -- compiles and runs every local check for FindFast (Linux and macOS).
# Usage: bash run_tests.sh        (run it from anywhere; it uses its own folder)
cd "$(dirname "$0")" || exit 1

if command -v g++ >/dev/null 2>&1; then CXX=g++
elif command -v clang++ >/dev/null 2>&1; then CXX=clang++
else echo "No C++ compiler found. Install g++ (Linux) or run: xcode-select --install (macOS)"; exit 1; fi

FAILED=0
pass() { echo "   PASS: $1"; }
fail() { echo "   FAIL: $1"; FAILED=1; }

echo "Compiler: $($CXX --version | head -1)"
echo

echo "[1/7] Compiling findfast.cpp"
if $CXX -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast 2> compile_warnings.txt; then
    pass "compiled"
    if [ -s compile_warnings.txt ]; then
        echo "   Note: the compiler printed warnings (clang on macOS prints 7 about move(...) in the"
        echo "   supplied template code; they are not ours and must NOT be edited):"
        grep "warning:" compile_warnings.txt | sed 's/^/     /'
    fi
else
    cat compile_warnings.txt; fail "compile error"; rm -f compile_warnings.txt; exit 1
fi
rm -f compile_warnings.txt

echo "[2/7] Official tests: ./findfast --test all"
if ./findfast --test all | tee test_output.txt | grep -qx "SUMMARY 20/20"; then pass "SUMMARY 20/20"
else grep -v "^PASS" test_output.txt; fail "not 20/20"; fi
rm -f test_output.txt

echo "[3/7] Larger inputs, like the hidden tests (4 scales x 3 seeds)"
bad=0
for scale in 1 7 500 5000; do
    for seed in 1 42 2026; do
        r=$(./findfast --test all --scale $scale --seed $seed | tail -1)
        [ "$r" = "SUMMARY 20/20" ] || { echo "     scale $scale seed $seed: $r"; bad=1; }
    done
done
[ $bad -eq 0 ] && pass "20/20 at every scale and seed" || fail "some runs were not 20/20"

echo "[4/7] Demo: ./findfast --demo"
expected=$'> network\nresults 2\n2 notes.txt\n3 report.txt\n> network AND security\nresults 1\n3 report.txt\n> network OR data\nresults 3\n1 book.txt\n2 notes.txt\n3 report.txt'
if [ "$(./findfast --demo)" = "$expected" ]; then pass "output matches the brief"; else ./findfast --demo; fail "demo output differs"; fi

echo "[5/7] Benchmark at grading scale: ./findfast --bench --n 5000 --queries 10000 --updates 5000"
if out=$(./findfast --bench --n 5000 --queries 10000 --updates 5000 2>&1); then
    echo "$out" | sed 's/^/     /'; pass "all answers correct (a wrong answer would stop the benchmark)"
else echo "$out"; fail "benchmark reported an error"; fi

echo "[6/7] Benchmark on the sample workload: ./findfast --bench --workload data/small.csv"
if out=$(./findfast --bench --workload data/small.csv 2>&1); then
    echo "$out" | sed 's/^/     /'; pass "all answers correct"
else echo "$out"; fail "benchmark reported an error"; fi

echo "[7/7] Our own tests: student_tests.cpp"
if $CXX -std=c++17 -O2 student_tests.cpp -o student_tests 2>/dev/null && ./student_tests > student_output.txt; then
    grep -qx "SUMMARY 11/11" student_output.txt && pass "SUMMARY 11/11" || { cat student_output.txt; fail "not 11/11"; }
else
    cat student_output.txt 2>/dev/null; fail "student tests did not build or failed"
fi
rm -f student_output.txt

# Optional: memory-error check with AddressSanitizer (skipped if the compiler does not support it)
if $CXX -std=c++17 -O1 -g -fsanitize=address,undefined findfast.cpp -o findfast_asan 2>/dev/null; then
    echo "[extra] Memory-error check (AddressSanitizer + UndefinedBehaviorSanitizer)"
    if ./findfast_asan --test all --scale 2000 2>&1 | tail -1 | grep -qx "SUMMARY 20/20"; then pass "no memory errors"
    else fail "sanitizer reported a problem (run ./findfast_asan --test all to see it)"; fi
    rm -f findfast_asan
fi

echo
if [ $FAILED -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "SOME CHECKS FAILED (see FAIL lines above)"; fi
exit $FAILED
