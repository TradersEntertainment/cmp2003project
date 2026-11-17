@echo off
rem run_tests.bat -- compiles and runs every local check for FindFast on Windows.
rem Usage: double-click this file, or in PowerShell type:  .\run_tests.bat
setlocal
cd /d "%~dp0"
set FAILED=0

where g++ >nul 2>nul
if errorlevel 1 goto :nocompiler
for /f "delims=" %%V in ('g++ --version') do (
    echo Compiler: %%V
    goto :compile
)

:compile
echo.
echo [1/7] Compiling findfast.cpp
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast.exe 2> compile_output.txt
if errorlevel 1 goto :compilefail
echo    PASS: compiled
for %%F in (compile_output.txt) do if %%~zF gtr 0 (
    echo    Note: the compiler printed warnings:
    type compile_output.txt
)
del compile_output.txt >nul 2>nul

echo [2/7] Official tests: findfast.exe --test all
findfast.exe --test all > test_output.txt
findstr /c:"SUMMARY 20/20" test_output.txt >nul
if errorlevel 1 (
    findstr /v /b /c:"PASS" test_output.txt
    echo    FAIL: not 20/20
    set FAILED=1
) else (
    echo    PASS: SUMMARY 20/20
)
del test_output.txt >nul 2>nul

echo [3/7] Larger inputs, like the hidden tests: 4 scales x 3 seeds
set BAD=0
for %%S in (1 7 500 5000) do for %%D in (1 42 2026) do call :scaled %%S %%D
if "%BAD%"=="0" (
    echo    PASS: 20/20 at every scale and seed
) else (
    echo    FAIL: some runs were not 20/20
    set FAILED=1
)

echo [4/7] Demo: findfast.exe --demo
findfast.exe --demo > demo_output.txt
set DEMO_OK=1
findstr /b /c:"results 2" demo_output.txt >nul || set DEMO_OK=0
findstr /b /c:"results 1" demo_output.txt >nul || set DEMO_OK=0
findstr /b /c:"results 3" demo_output.txt >nul || set DEMO_OK=0
findstr /b /c:"1 book.txt" demo_output.txt >nul || set DEMO_OK=0
if "%DEMO_OK%"=="1" (
    echo    PASS: results 2 / 1 / 3, as in the brief
) else (
    type demo_output.txt
    echo    FAIL: demo output differs from the brief
    set FAILED=1
)
del demo_output.txt >nul 2>nul

echo [5/7] Benchmark at grading scale: --bench --n 5000 --queries 10000 --updates 5000
findfast.exe --bench --n 5000 --queries 10000 --updates 5000
if errorlevel 1 (
    echo    FAIL: benchmark reported an error
    set FAILED=1
) else (
    echo    PASS: all answers correct - a wrong answer would stop the benchmark
)

echo [6/7] Benchmark on the sample workload: --bench --workload data\small.csv
findfast.exe --bench --workload data\small.csv
if errorlevel 1 (
    echo    FAIL: benchmark reported an error
    set FAILED=1
) else (
    echo    PASS: all answers correct
)

echo [7/7] Our own tests: student_tests.cpp
g++ -std=c++17 -O2 student_tests.cpp -o student_tests.exe
if errorlevel 1 (
    echo    FAIL: student_tests.cpp did not compile
    set FAILED=1
    goto :summary
)
student_tests.exe > student_output.txt
findstr /c:"SUMMARY 11/11" student_output.txt >nul
if errorlevel 1 (
    type student_output.txt
    echo    FAIL: not 11/11
    set FAILED=1
) else (
    echo    PASS: SUMMARY 11/11
)
del student_output.txt >nul 2>nul

:summary
echo.
if "%FAILED%"=="0" (
    echo ALL CHECKS PASSED
) else (
    echo SOME CHECKS FAILED - see the FAIL lines above
)
echo.
pause
exit /b %FAILED%

:scaled
findfast.exe --test all --scale %1 --seed %2 > scale_output.txt
findstr /c:"SUMMARY 20/20" scale_output.txt >nul
if errorlevel 1 (
    echo      scale %1 seed %2 was not 20/20
    set BAD=1
)
del scale_output.txt >nul 2>nul
goto :eof

:nocompiler
echo g++ was not found.
echo Install it once in PowerShell:  scoop install gcc
echo then close this window, open a new one and run this file again.
echo.
pause
exit /b 1

:compilefail
type compile_output.txt
del compile_output.txt >nul 2>nul
echo    FAIL: compile error - fix the first error shown above
echo.
pause
exit /b 1
