# Windows.Graphics.Capture (WGC) Test

Quick prototype to test if WGC API can capture at 60+ FPS from DOOM Eternal.

## Goal

Verify that WGC bypasses DWM throttling and captures at full game FPS (60-100+).

## Implementation

Using WinRT C++/CX to access Windows.Graphics.Capture API.

## Build

```bash
build.bat
```

## Test

1. Start DOOM Eternal in windowed mode
2. Run `wgc_test.exe`
3. Check FPS output

Expected: 60-100 FPS capture (matching monitor refresh rate).
