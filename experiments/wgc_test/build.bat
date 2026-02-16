@echo off
setlocal

echo ========================================
echo   WGC Test - Build Script
echo ========================================
echo.

REM Initialize Visual Studio environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

REM Clean and create build directory
if exist build rmdir /s /q build
mkdir build
cd build

REM Run CMake
echo Running CMake...
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release

REM Build
echo.
echo Building...
nmake

echo.
if exist bin\wgc_test.exe (
    echo ========================================
    echo   BUILD SUCCESSFUL
    echo ========================================
    echo.
    echo Executable: bin\wgc_test.exe
    echo.
    echo To run: cd build\bin ^&^& wgc_test.exe
) else (
    echo ========================================
    echo   BUILD FAILED
    echo ========================================
)

endlocal
