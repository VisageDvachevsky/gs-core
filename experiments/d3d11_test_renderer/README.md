# D3D11 Test Renderer

Simple D3D11 application that renders a rotating colored square with VSync enabled.

## Purpose

This tool is used to test DXGI Desktop Duplication capture performance with **real 60+ FPS content**.

Unlike desktop capture (which may throttle to 30 FPS when idle), this renderer:
- ✅ Renders at monitor refresh rate (VSync ON)
- ✅ Constantly updates every frame (rotating square)
- ✅ Forces DWM to composite at full refresh rate
- ✅ Shows real-time FPS counter

## Build

```bash
build.bat
```

## Run

```bash
cd build\bin
test_renderer.exe
```

A window will open showing a rotating colored square. Leave this running in the background while testing DXGI capture with `fps_loop_test.exe`.

## Usage with DXGI Capture

1. Start `test_renderer.exe` (this window)
2. In another terminal, run `fps_loop_test.exe` from GameStream core
3. Check if capture now achieves 60+ FPS (matching monitor refresh rate)

## Expected Results

- **100 Hz monitor**: Should render at ~100 FPS
- **60 Hz monitor**: Should render at ~60 FPS
- **DXGI capture**: Should capture same FPS as renderer (proving 60+ FPS capability)

## Controls

- **ESC**: Exit
- **Close window**: Exit
