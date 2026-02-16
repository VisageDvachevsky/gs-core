@echo off
REM GameStream Core - Build Script
REM Uses VS 2022 BuildTools

echo ========================================
echo   GameStream Core - Build Script
echo ========================================
echo.

REM Set VS 2022 BuildTools paths
set "VSINSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "VCINSTALLDIR=%VSINSTALLDIR%\VC"
set "VCVARSALL=%VCINSTALLDIR%\Auxiliary\Build\vcvarsall.bat"

REM Check if vcvarsall.bat exists
if not exist "%VCVARSALL%" (
    echo ERROR: VS 2022 BuildTools not found!
    echo Expected location: %VCVARSALL%
    pause
    exit /b 1
)

REM Initialize VS environment
echo Initializing Visual Studio environment...
call "%VCVARSALL%" x64
if errorlevel 1 (
    echo ERROR: Failed to initialize VS environment
    pause
    exit /b 1
)

echo.
echo Visual Studio environment initialized
echo.

REM Clean build directory
if exist build rd /s /q build
mkdir build

REM Run CMake
echo Running CMake configuration...
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

echo.
echo Building project...
cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo   BUILD SUCCESSFUL
echo ========================================
echo.
echo Executable: build\bin\capture_test.exe
echo.
pause
