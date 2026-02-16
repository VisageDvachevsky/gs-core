# GameStream Core - PowerShell Build Script
# Uses VS 2022 BuildTools

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  GameStream Core - Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Import VS environment
$vsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vcvarsall = "$vsPath\VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path $vcvarsall)) {
    Write-Host "ERROR: VS 2022 BuildTools not found!" -ForegroundColor Red
    Write-Host "Expected: $vcvarsall" -ForegroundColor Red
    exit 1
}

Write-Host "Initializing Visual Studio environment..." -ForegroundColor Yellow

# Run vcvarsall and capture environment variables
$tempFile = [System.IO.Path]::GetTempFileName()
cmd /c "`"$vcvarsall`" x64 && set > `"$tempFile`""

# Parse and set environment variables
Get-Content $tempFile | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
}
Remove-Item $tempFile

Write-Host "Visual Studio environment initialized" -ForegroundColor Green
Write-Host ""

# Clean build directory
if (Test-Path "build") {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}
New-Item -ItemType Directory -Path "build" | Out-Null

# Run CMake
Write-Host "Running CMake configuration..." -ForegroundColor Yellow
$configResult = & cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release 2>&1
Write-Host $configResult

if ($LASTEXITCODE -ne 0) {
    Write-Host "" -ForegroundColor Red
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Building project..." -ForegroundColor Yellow
$buildResult = & cmake --build build --config Release 2>&1
Write-Host $buildResult

if ($LASTEXITCODE -ne 0) {
    Write-Host "" -ForegroundColor Red
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Executable: build\bin\capture_test.exe" -ForegroundColor Cyan
Write-Host ""
