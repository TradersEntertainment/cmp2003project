# Testing FindFast on Your Own Computer

Follow the steps in order. Each step says what to type and what you should see. The whole process takes about 5 minutes.

> Türkçe: [TESTING_TR.md](TESTING_TR.md)

## Step 0: Install a C++ compiler (once)

| System | What to do | Check it worked |
|---|---|---|
| **Windows** | In a normal PowerShell window: `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser`, then `Invoke-RestMethod -Uri https://get.scoop.sh \| Invoke-Expression`, then `scoop install gcc` | `g++ --version` prints a version |
| **Linux** | `sudo apt update` then `sudo apt install g++` | `g++ --version` |
| **macOS** | `xcode-select --install` and click *Install* | `clang++ --version` |

This is the same setup as section 04 of the brief. If `g++` is "not recognised", close the terminal and open a new one.

## Step 1: Download the project

1. Open https://github.com/TradersEntertainment/cmp2003project
2. Click the green **Code** button, then **Download ZIP**.
3. Extract the ZIP. On Windows: right-click it and choose *Extract All*.
4. Open the extracted folder, the one that contains `findfast.cpp`:
   - **Windows:** right-click an empty area of the folder and choose *Open in Terminal*.
   - **Linux / macOS:** open a terminal and `cd` into the folder.
5. Check you are in the right place:
   - **Windows:** `Get-ChildItem findfast.cpp`
   - **Linux / macOS:** `ls findfast.cpp`

   It must list the file. If it does not, you are in the wrong folder.

## Step 2 (the quick way): run every check with one command

| System | Command |
|---|---|
| Windows | `.\run_tests.bat` (or double-click `run_tests.bat` in File Explorer) |
| Linux / macOS | `bash run_tests.sh` |

The script compiles everything and runs 7 checks, printing `PASS` or `FAIL` for each. **The last line must say `ALL CHECKS PASSED`.**

```
[1/7] Compiling findfast.cpp
   PASS: compiled
[2/7] Official tests: ./findfast --test all
   PASS: SUMMARY 20/20
[3/7] Larger inputs, like the hidden tests (4 scales x 3 seeds)
   PASS: 20/20 at every scale and seed
[4/7] Demo: ./findfast --demo
   PASS: output matches the brief
[5/7] Benchmark at grading scale ...
     peak_bytes 149281
   PASS: all answers correct
[6/7] Benchmark on the sample workload ...
     peak_bytes 32540
   PASS: all answers correct
[7/7] Our own tests: student_tests.cpp
   PASS: SUMMARY 11/11

ALL CHECKS PASSED
```

On Linux and macOS the script also runs an extra memory-error check (AddressSanitizer) when the compiler supports it.

If you prefer to see every step yourself, do Step 3 instead.

## Step 3 (the manual way): one command at a time

The commands below use the Linux/macOS form. **On Windows, write `.\findfast.exe` instead of `./findfast`, and `findfast.exe` after `-o`.** On macOS write `clang++` instead of `g++`.

### 3.1 Compile

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast
```

No output means success, and a new file `findfast` (or `findfast.exe`) appears. On macOS, clang prints **7 warnings about `move(...)`**. They are in the teacher's part of the template, not in our code. **Do not change them**: only the marked region may be edited.

### 3.2 The 20 official tests

```
./findfast --test all
```

Expected: 20 lines `PASS T01` … `PASS T20`, then **`SUMMARY 20/20`**.

### 3.3 Bigger inputs, like the hidden tests

```
./findfast --test all --scale 500 --seed 42
./findfast --test all --scale 5000 --seed 7
```

Expected: **`SUMMARY 20/20`** each time. You can try any scale and seed you like.

### 3.4 The demo from the brief

```
./findfast --demo
```

Expected, exactly as in the brief:

```
> network
results 2
2 notes.txt
3 report.txt
> network AND security
results 1
3 report.txt
> network OR data
results 3
1 book.txt
2 notes.txt
3 report.txt
```

### 3.5 Speed and memory at grading scale

```
./findfast --bench --n 5000 --queries 10000 --updates 5000
./findfast --bench --workload data/small.csv
```

Expected: five lines: `build_seconds`, `lookup_seconds`, `update_seconds`, `total_seconds`, `peak_bytes`.

- **No error** means every answer was correct. A wrong answer stops the benchmark with `error: ...`.
- `peak_bytes` should be **149281** for the first command and **32540** for the second on a 64-bit computer.
- The times depend on your computer. On ours the first command took about 0.7 ms in total.

### 3.6 Our own tests

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
./student_tests
```

Expected: 11 lines `PASS U01` … `PASS U11`, then **`SUMMARY 11/11`**. These test the empty word, very long words, words with unusual bytes, the largest ListId, `clear`/reuse/destructor memory, a 1.4-million-operation comparison against `std::map`, and 200,000 words.

### 3.7 Try the search engine by hand

```
./findfast --app --data docs
```

Then type these commands, pressing Enter after each:

```
search network
search network AND security
search data OR algorithms
list
quit
```

`search network` should print `results 2` (notes.txt and report.txt). You can put your own `.txt` files into the `docs` folder and search them.

## Step 4: test on the grading computer (the Engine Board)

Local tests use small data. The real ranking uses much larger hidden workloads.

1. Open the submission system linked on the course page.
2. Upload `findfast.cpp`. Upload only that file; do not upload `student_tests.cpp`.
3. The board compiles it, runs the hidden tests T01–T20 and the large workloads, and shows the result. It must say **20/20**. Then it shows our efficiency score and rank.

You can submit as many times as you like. The last submission before the deadline counts.

## If something goes wrong

| Problem | Solution |
|---|---|
| `g++` is not recognised | Close the terminal, open a new one. If it still fails, repeat Step 0. |
| `findfast.cpp` not found | You are in the wrong folder. Go back to Step 1.4. |
| Windows says "running scripts is disabled" | That message is about `.ps1` files. `run_tests.bat` is not affected; run `.\run_tests.bat` or double-click it. |
| The file is called `findfast.cpp.txt` | In File Explorer choose View → Show → File name extensions, then rename it. |
| The `.bat` window closes immediately | Run it from a terminal (`.\run_tests.bat`) to read the messages. |
| Results do not change after an edit | Save the file and compile again (step 3.1). |
| Any `FAIL` | Copy the whole output and send it to us. |
