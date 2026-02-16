@echo off
setlocal

echo ========================================
echo   Building wgc_test
echo ========================================
echo.

REM Initialize Visual Studio environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

REM Navigate to build directory
cd build

REM Build wgc_test target
echo Building wgc_test...
nmake wgc_test

if exist bin\wgc_test.exe (
    echo.
    echo ========================================
    echo   BUILD SUCCESSFUL
    echo ========================================
    echo.
    echo Executable: bin\wgc_test.exe
) else (
    echo.
    echo ========================================
    echo   BUILD FAILED
    echo ========================================
)

endlocal
