# FindFast: Compact Chained Hash Table

Our engine for the **CMP2003 Data Structures FindFast challenge**. The search template needs a word → list-ID index (`StudentIndex`). We built it as a hash table with separate chaining, tuned for the contest score: a balanced (harmonic) mean of speed and **peak** memory.

| | Result |
|---|---|
| Public tests | **20/20** at scales 1–20,000, 6 seeds; clean under AddressSanitizer + UBSan; g++ and clang++ |
| Our extra tests | **11/11** (`student_tests.cpp`) |
| Memory at 5,000 keys | **≈35 bytes per word**, of which ≈14.6 are the word itself (176,022 bytes peak) |
| Speed at 5,000 keys | build 0.38 ms · 10,000 lookups 0.23 ms · 5,000 updates 0.16 ms |
| Against typical designs | 1.7× less memory than `Record` nodes, 3.8× less than a `Record` array |

## Documents

| File | What it is |
|---|---|
| [`TESTING.md`](TESTING.md) · [`TESTING_TR.md`](TESTING_TR.md) | **How to test it on your computer**, step by step (English · Türkçe) |
| [`guide/EXPLANATION.md`](guide/EXPLANATION.md) | **Start here to understand the code.** Every part explained, with complexity, memory model, rule checklist and oral-exam Q&A |
| [`guide/ANLATIM_TR.md`](guide/ANLATIM_TR.md) | The same guide in Turkish (Türkçe anlatım) |
| [`guide/interactive.html`](guide/interactive.html) | Interactive animation of put/find/erase/clear and growth, English and Turkish (download and open in a browser) |
| [`report/report.pdf`](report/report.pdf) | The 2-page report for submission (source: `report/report.html`) |
| [`CONTRIBUTIONS.md`](CONTRIBUTIONS.md) | Contribution statement (fill in names) |
| [`research/`](research) | The designs we raced, the tools and the raw benchmark results |

## The idea in one picture

```
buckets_ ──► [0] ──► node ──► node ──► nullptr        bucket  = top bits of a 64-bit hash
             [1] ──► nullptr                          grow ×2 when words = 2 × buckets
             [2] ──► node ──► nullptr

node = ONE tracked allocation of 14 + L bytes:
[ next 8 ][ list ID 4 ][ fingerprint 1 ][ length 1 (varint) ][ the L characters ]
```

1. **Everything for a word lives in one block.** There is no separate key buffer and no padding.
2. **A one-byte fingerprint** skips wrong words in a chain without comparing characters.
3. **Growth without a memory spike.** The old bucket array is released *before* the new one is allocated, so growing never needs two arrays at once.

## Build and run

**One command runs every check:** `.\run_tests.bat` on Windows, `bash run_tests.sh` on Linux/macOS. The last line must say `ALL CHECKS PASSED`. Step-by-step instructions are in [TESTING.md](TESTING.md).

Or one at a time:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast
./findfast --test all                                      # SUMMARY 20/20
./findfast --demo
./findfast --bench --n 5000 --queries 10000 --updates 5000 # grading-like scale
./findfast --bench --workload data/small.csv
./findfast --app --data docs                               # interactive search

g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
./student_tests                                            # SUMMARY 11/11
```

On Windows, use `.\findfast.exe` instead of `./findfast`.

## Notes for teammates

- **Edit nothing outside the marked region.** On macOS, clang prints 7 warnings about `move(...)` in the *supplied* engine code. The untouched template prints them too. Leave them alone; our region has no warnings.
- **Check the permitted-structure list** on the course page before submitting, and name the structure the way the list does (we call it "hash table with separate chaining").
- Fill in names in `CONTRIBUTIONS.md` and the report header (`report/report.html`, then re-export the PDF).

## Repository layout

```
findfast.cpp          the submission (only the marked region was edited)
student_tests.cpp     our own tests (includes findfast.cpp, renames its main)
run_tests.bat / .sh   one-command test run (Windows / Linux, macOS)
TESTING.md            step-by-step local testing (TESTING_TR.md: Turkish)
CONTRIBUTIONS.md      contribution statement
guide/                explanation in English and Turkish
report/               2-page report (HTML source + PDF)
research/             design variants, benchmark tools, results
data/, docs/          sample workload and documents from the template
```
