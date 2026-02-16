#include <windows.h>
#include <iostream>
#include <chrono>
#include <format>

// D3D11 first
#include <d3d11.h>
#include <dxgi1_2.h>

// WinRT base
#include <unknwn.h>
#include <winrt/base.h>

// WinRT projections
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// Interop headers
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace wf = winrt::Windows::Foundation;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace std::chrono;

// Global stats
int g_framesCaptured = 0;
auto g_startTime = steady_clock::now();
auto g_lastFpsTime = steady_clock::now();
int g_lastFrameCount = 0;

// Forward declarations
winrt::com_ptr<ID3D11Device> CreateD3D11Device();
wd3d::IDirect3DDevice CreateDirect3DDevice(winrt::com_ptr<ID3D11Device> d3dDevice);
HWND FindGameWindow();

int main() {
    std::cout << "===========================================\n";
    std::cout << "  Windows.Graphics.Capture API Test\n";
    std::cout << "===========================================\n\n";

    std::cout << "Switch to DOOM Eternal window NOW!\n";
    std::cout << "Starting capture in 5 seconds...\n";
    for (int i = 5; i > 0; i--) {
        std::cout << i << "...\n";
        Sleep(1000);
    }
    std::cout << "\n";

    // Initialize WinRT
    winrt::init_apartment();

    // Create D3D11 device
    auto d3dDevice = CreateD3D11Device();
    if (!d3dDevice) {
        std::cerr << "Failed to create D3D11 device\n";
        return 1;
    }

    // Create Direct3D device for WGC
    auto device = CreateDirect3DDevice(d3dDevice);
    if (!device) {
        std::cerr << "Failed to create Direct3D device\n";
        return 1;
    }

    std::cout << "[WGC] Direct3D device created\n";

    // Find DOOM Eternal window (or any game window)
    HWND hwnd = FindGameWindow();
    if (!hwnd) {
        std::cerr << "Game window not found!\n";
        std::cerr << "Please start DOOM Eternal (or another game) in windowed mode.\n";
        return 1;
    }

    WCHAR windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);
    std::wcout << L"[WGC] Found window: " << windowTitle << L"\n";

    // Create capture item from window
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item)));

    std::cout << std::format("[WGC] Capture item created: {}x{}\n", item.Size().Width, item.Size().Height);

    // Create frame pool
    auto framePool = wgc::Direct3D11CaptureFramePool::Create(
        device,
        wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,  // Number of buffers
        item.Size()
    );

    std::cout << "[WGC] Frame pool created\n";

    // Frame arrived handler
    framePool.FrameArrived([&](auto&& sender, auto&&) {
        auto frame = sender.TryGetNextFrame();
        if (frame) {
            g_framesCaptured++;

            // Print FPS every second
            auto now = steady_clock::now();
            auto elapsed = duration<float>(now - g_lastFpsTime).count();
            if (elapsed >= 1.0f) {
                int framesDone = g_framesCaptured - g_lastFrameCount;
                float fps = framesDone / elapsed;

                auto totalTime = duration<float>(now - g_startTime).count();
                std::cout << std::format("[{:.1f}s] FPS: {:.1f} | Total frames: {}\n",
                    totalTime, fps, g_framesCaptured);

                g_lastFpsTime = now;
                g_lastFrameCount = g_framesCaptured;
            }
        }
    });

    // Start capture session
    auto session = framePool.CreateCaptureSession(item);
    session.StartCapture();

    std::cout << "[WGC] Capture session started\n";
    std::cout << "Capturing... Press Ctrl+C to stop\n\n";

    // Keep running
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    session.Close();
    framePool.Close();

    std::cout << "\nCapture stopped. Total frames: " << g_framesCaptured << "\n";
    return 0;
}

winrt::com_ptr<ID3D11Device> CreateD3D11Device() {
    winrt::com_ptr<ID3D11Device> device;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.put(),
        &featureLevel,
        nullptr
    );

    if (FAILED(hr)) {
        return nullptr;
    }

    return device;
}

wd3d::IDirect3DDevice CreateDirect3DDevice(winrt::com_ptr<ID3D11Device> d3dDevice) {
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();

    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));

    return inspectable.as<wd3d::IDirect3DDevice>();
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    WCHAR title[256];
    if (GetWindowTextW(hwnd, title, 256) > 0) {
        std::wcout << L"  - " << title << L"\n";
    }
    return TRUE;
}

HWND FindGameWindow() {
    std::cout << "[WGC] Searching for game window...\n";
    std::cout << "Available windows:\n";
    EnumWindows(EnumWindowsProc, 0);
    std::cout << "\n";

    // Try to find DOOM Eternal
    HWND hwnd = FindWindowW(nullptr, L"DOOM Eternal");
    if (hwnd) {
        std::wcout << L"Found: DOOM Eternal\n";
        return hwnd;
    }

    // Try other common game names
    const wchar_t* gameNames[] = {
        L"DOOM",
        L"DOOMEternalx64vk.exe",
        L"Death Stranding",
        L"Cyberpunk 2077",
        L"The Witcher 3"
    };

    for (const auto* name : gameNames) {
        hwnd = FindWindowW(nullptr, name);
        if (hwnd) {
            std::wcout << L"Found: " << name << L"\n";
            return hwnd;
        }
    }

    // Return foreground window as fallback
    hwnd = GetForegroundWindow();
    if (hwnd) {
        WCHAR title[256];
        GetWindowTextW(hwnd, title, 256);
        std::wcout << L"Using foreground window: " << title << L"\n";
        return hwnd;
    }

    return nullptr;
}
