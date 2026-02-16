#include "wgc_capture.h"
#include <iostream>
#include <chrono>
#include <format>

// D3D11 first
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

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

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

using Microsoft::WRL::ComPtr;

namespace gamestream {

// PIMPL implementation - holds all WinRT types
class WGCCaptureImpl {
public:
    // D3D11 device (using WRL ComPtr to match D3D11CreateDevice)
    ComPtr<ID3D11Device> d3d11_device_;
    ComPtr<ID3D11DeviceContext> d3d11_context_;

    // WinRT capture objects
    wd3d::IDirect3DDevice direct3d_device_{ nullptr };
    wgc::GraphicsCaptureItem capture_item_{ nullptr };
    wgc::Direct3D11CaptureFramePool frame_pool_{ nullptr };
    wgc::GraphicsCaptureSession capture_session_{ nullptr };

    // Current frame
    wgc::Direct3D11CaptureFrame current_frame_{ nullptr };
    CaptureFrame current_capture_frame_{};

    // State
    bool is_initialized_ = false;
    CaptureStats stats_{};

    // Helper methods
    bool init_d3d11_device();
    bool init_direct3d_device();
    bool init_capture_item(uint32_t window_handle);
    bool init_frame_pool();
    bool start_capture_session();
};

WGCCapture::WGCCapture() : impl_(std::make_unique<WGCCaptureImpl>()) {}

WGCCapture::~WGCCapture() {
    if (impl_->capture_session_) {
        try {
            impl_->capture_session_.Close();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }

    if (impl_->frame_pool_) {
        try {
            impl_->frame_pool_.Close();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }
}

VoidResult WGCCapture::initialize(uint32_t window_handle) {
    std::cout << "[WGC] Initializing Windows.Graphics.Capture...\n";

    // Initialize WinRT apartment
    try {
        winrt::init_apartment();
    } catch (...) {
        // Already initialized, ignore
    }

    // Step 1: Create D3D11 device
    if (!impl_->init_d3d11_device()) {
        return VoidResult::error("Failed to create D3D11 device");
    }

    // Step 2: Create Direct3D device for WGC
    if (!impl_->init_direct3d_device()) {
        return VoidResult::error("Failed to create Direct3D device for WGC");
    }

    // Step 3: Create capture item from window
    if (!impl_->init_capture_item(window_handle)) {
        return VoidResult::error("Failed to create GraphicsCaptureItem");
    }

    // Step 4: Create frame pool
    if (!impl_->init_frame_pool()) {
        return VoidResult::error("Failed to create Direct3D11CaptureFramePool");
    }

    // Step 5: Start capture session
    if (!impl_->start_capture_session()) {
        return VoidResult::error("Failed to start GraphicsCaptureSession");
    }

    impl_->is_initialized_ = true;
    std::cout << "[WGC] Initialized successfully\n";
    return {};
}

bool WGCCaptureImpl::init_d3d11_device() {
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
        &d3d11_device_,
        &featureLevel,
        &d3d11_context_
    );

    if (FAILED(hr)) {
        std::cerr << std::format("[WGC] D3D11CreateDevice failed: 0x{:08X}\n", static_cast<uint32_t>(hr));
        return false;
    }

    std::cout << "[WGC] D3D11 device created\n";
    return true;
}

bool WGCCaptureImpl::init_direct3d_device() {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3d11_device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        std::cerr << std::format("[WGC] QueryInterface for IDXGIDevice failed: 0x{:08X}\n", static_cast<uint32_t>(hr));
        return false;
    }

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        std::cerr << std::format("[WGC] CreateDirect3D11DeviceFromDXGIDevice failed: 0x{:08X}\n", static_cast<uint32_t>(hr));
        return false;
    }

    direct3d_device_ = inspectable.as<wd3d::IDirect3DDevice>();
    std::cout << "[WGC] Direct3D device created\n";
    return true;
}

bool WGCCaptureImpl::init_capture_item(uint32_t window_handle) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(window_handle));

    // If no window specified, use foreground window
    if (hwnd == nullptr) {
        hwnd = GetForegroundWindow();
        if (hwnd == nullptr) {
            std::cerr << "[WGC] No foreground window found\n";
            return false;
        }
    }

    WCHAR windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);
    std::wcout << L"[WGC] Capturing window: " << windowTitle << L"\n";

    // Create capture item from window
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    try {
        winrt::check_hresult(interop->CreateForWindow(
            hwnd,
            winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(capture_item_)
        ));
    } catch (const winrt::hresult_error& e) {
        std::cerr << std::format("[WGC] CreateForWindow failed: 0x{:08X}\n", static_cast<uint32_t>(e.code()));
        return false;
    }

    auto size = capture_item_.Size();
    std::cout << std::format("[WGC] Capture item created: {}x{}\n", size.Width, size.Height);
    return true;
}

bool WGCCaptureImpl::init_frame_pool() {
    auto size = capture_item_.Size();

    try {
        frame_pool_ = wgc::Direct3D11CaptureFramePool::Create(
            direct3d_device_,
            wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,  // Number of buffers
            size
        );
    } catch (const winrt::hresult_error& e) {
        std::cerr << std::format("[WGC] Direct3D11CaptureFramePool::Create failed: 0x{:08X}\n", static_cast<uint32_t>(e.code()));
        return false;
    }

    std::cout << "[WGC] Frame pool created\n";
    return true;
}

bool WGCCaptureImpl::start_capture_session() {
    try {
        capture_session_ = frame_pool_.CreateCaptureSession(capture_item_);
        capture_session_.StartCapture();
    } catch (const winrt::hresult_error& e) {
        std::cerr << std::format("[WGC] StartCapture failed: 0x{:08X}\n", static_cast<uint32_t>(e.code()));
        return false;
    }

    std::cout << "[WGC] Capture session started\n";
    return true;
}

Result<CaptureFrame> WGCCapture::acquire_frame(uint64_t timeout_ms) {
    if (!impl_->is_initialized_) {
        return Result<CaptureFrame>::error("Not initialized");
    }

    auto start = std::chrono::steady_clock::now();

    // Try to get next frame from pool
    try {
        impl_->current_frame_ = impl_->frame_pool_.TryGetNextFrame();
    } catch (const winrt::hresult_error& e) {
        return Result<CaptureFrame>::error(std::format("TryGetNextFrame failed: 0x{:08X}", static_cast<uint32_t>(e.code())));
    }

    // If no frame available, timeout
    if (!impl_->current_frame_) {
        impl_->stats_.frames_skipped++;
        return Result<CaptureFrame>::error("timeout");
    }

    // Get D3D11 surface from frame
    auto surface = impl_->current_frame_.Surface();
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

    // Get ID3D11Texture2D using WinRT, then convert to WRL ComPtr
    winrt::com_ptr<ID3D11Texture2D> winrt_texture;
    HRESULT hr = access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), winrt_texture.put_void());
    if (FAILED(hr)) {
        return Result<CaptureFrame>::error(std::format("GetInterface failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    // Convert winrt::com_ptr to Microsoft::WRL::ComPtr by detaching and attaching
    ComPtr<ID3D11Texture2D> wrl_texture;
    wrl_texture.Attach(winrt_texture.detach());

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    wrl_texture->GetDesc(&desc);

    // Update statistics
    auto elapsed = std::chrono::steady_clock::now() - start;
    double capture_ms = std::chrono::duration<double, std::milli>(elapsed).count();

    impl_->stats_.frames_captured++;
    impl_->stats_.avg_capture_ms = (impl_->stats_.avg_capture_ms * (impl_->stats_.frames_captured - 1) + capture_ms) / impl_->stats_.frames_captured;
    if (capture_ms < impl_->stats_.min_capture_ms) impl_->stats_.min_capture_ms = capture_ms;
    if (capture_ms > impl_->stats_.max_capture_ms) impl_->stats_.max_capture_ms = capture_ms;

    // Build CaptureFrame
    impl_->current_capture_frame_.texture = wrl_texture;
    impl_->current_capture_frame_.width = desc.Width;
    impl_->current_capture_frame_.height = desc.Height;
    impl_->current_capture_frame_.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    return impl_->current_capture_frame_;
}

void WGCCapture::release_frame() {
    // WGC frames are automatically released when TryGetNextFrame() is called again
    // or when the frame object goes out of scope
    impl_->current_frame_ = nullptr;
}

void WGCCapture::get_resolution(uint32_t& width, uint32_t& height) const {
    if (impl_->capture_item_) {
        auto size = impl_->capture_item_.Size();
        width = size.Width;
        height = size.Height;
    } else {
        width = 0;
        height = 0;
    }
}

bool WGCCapture::is_initialized() const {
    return impl_->is_initialized_;
}

CaptureStats WGCCapture::get_stats() const {
    return impl_->stats_;
}

} // namespace gamestream
