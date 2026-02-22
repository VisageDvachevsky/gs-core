#include "wgc_capture.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <format>
#include <mutex>
#include <condition_variable>

// D3D11 first
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// WinRT base
#include <unknwn.h>
#include <winrt/base.h>

// WinRT projections
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
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

// PIMPL implementation — holds all WinRT types, never visible in the public header.
class WGCCaptureImpl {
public:
    // D3D11 device
    ComPtr<ID3D11Device> d3d11_device_;
    ComPtr<ID3D11DeviceContext> d3d11_context_;

    // WinRT capture objects
    wd3d::IDirect3DDevice direct3d_device_{ nullptr };
    wgc::GraphicsCaptureItem capture_item_{ nullptr };
    wgc::Direct3D11CaptureFramePool frame_pool_{ nullptr };
    wgc::GraphicsCaptureSession capture_session_{ nullptr };

    // Frame lifecycle:
    //   current_frame_  — filled by FrameArrived callback, ownership transferred to held_frame_
    //                     atomically under frame_mutex_ during acquire_frame().
    //   held_frame_     — owned by the caller between acquire_frame() and release_frame().
    //                     Keeps the WGC buffer alive so the texture remains valid.
    wgc::Direct3D11CaptureFrame current_frame_{ nullptr };
    wgc::Direct3D11CaptureFrame held_frame_{ nullptr };

    // CaptureFrame built from held_frame_ for the caller
    CaptureFrame current_capture_frame_{};

    // State
    bool is_initialized_ = false;
    CaptureStats stats_{};
    winrt::Windows::Graphics::SizeInt32 current_size_{};

    // Frame-ready signalling (condition variable pattern)
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    bool has_new_frame_ = false;

    // Resize detection — written under frame_mutex_, read under frame_mutex_
    bool resize_pending_ = false;
    winrt::Windows::Graphics::SizeInt32 pending_size_{};

    // Initialization helpers
    bool init_d3d11_device();
    bool init_direct3d_device();
    bool init_capture_item(uintptr_t window_handle);
    bool init_frame_pool();
    bool start_capture_session();

    // Resize — called from acquire_frame(), never from the callback
    void recreate_frame_pool(winrt::Windows::Graphics::SizeInt32 new_size);

    // Single FrameArrived implementation shared by init_frame_pool and recreate_frame_pool.
    // Called from the WGC compositor thread.
    void on_frame_arrived(wgc::Direct3D11CaptureFramePool const& sender);
};

// ---------------------------------------------------------------------------
// WGCCapture — public API
// ---------------------------------------------------------------------------

WGCCapture::WGCCapture() : impl_(std::make_unique<WGCCaptureImpl>()) {}

WGCCapture::~WGCCapture() {
    if (impl_->capture_session_) {
        try { impl_->capture_session_.Close(); } catch (...) {}
    }
    if (impl_->frame_pool_) {
        try { impl_->frame_pool_.Close(); } catch (...) {}
    }
}

VoidResult WGCCapture::initialize(uintptr_t window_handle) {
    spdlog::debug("[WGC] Initializing Windows.Graphics.Capture...");

    try {
        winrt::init_apartment();
    } catch (...) {
        // Already initialized — ignore
    }

    if (!impl_->init_d3d11_device()) {
        return VoidResult::error("Failed to create D3D11 device");
    }
    if (!impl_->init_direct3d_device()) {
        return VoidResult::error("Failed to create Direct3D device for WGC");
    }
    if (!impl_->init_capture_item(window_handle)) {
        return VoidResult::error("Failed to create GraphicsCaptureItem");
    }
    if (!impl_->init_frame_pool()) {
        return VoidResult::error("Failed to create Direct3D11CaptureFramePool");
    }
    if (!impl_->start_capture_session()) {
        return VoidResult::error("Failed to start GraphicsCaptureSession");
    }

    impl_->is_initialized_ = true;
    spdlog::info("[WGC] Initialized successfully");
    return {};
}

Result<CaptureFrame> WGCCapture::acquire_frame(uint64_t timeout_ms) {
    if (!impl_->is_initialized_) {
        return Result<CaptureFrame>::error("Not initialized");
    }

    // Step 1: Handle pending resize.
    // Read resize state under the mutex, then execute the resize outside it to avoid
    // holding the lock during frame pool Close/Create (which fires its own callbacks).
    bool needs_resize = false;
    winrt::Windows::Graphics::SizeInt32 new_pool_size{};
    {
        std::unique_lock lock(impl_->frame_mutex_);
        if (impl_->resize_pending_) {
            needs_resize = true;
            new_pool_size = impl_->pending_size_;
            impl_->resize_pending_ = false;
            impl_->has_new_frame_ = false;  // Discard stale signal from old pool
        }
    }
    if (needs_resize) {
        impl_->recreate_frame_pool(new_pool_size);
    }

    auto start = std::chrono::steady_clock::now();

    // Step 2: Wait for FrameArrived signal and take ownership of the frame atomically.
    // The callback stored the frame in current_frame_ under the same mutex —
    // we move it to held_frame_ here, so the callback can safely write the next frame
    // without interfering with the caller's processing.
    {
        std::unique_lock lock(impl_->frame_mutex_);
        if (!impl_->frame_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                       [this] { return impl_->has_new_frame_; })) {
            impl_->stats_.frames_skipped++;
            return Result<CaptureFrame>::error("timeout");
        }
        impl_->has_new_frame_ = false;
        impl_->held_frame_ = std::move(impl_->current_frame_);
    }

    if (!impl_->held_frame_) {
        // Should not happen under normal operation, but guard defensively.
        impl_->stats_.frames_skipped++;
        return Result<CaptureFrame>::error("no frame after signal");
    }

    // Step 3: Extract ID3D11Texture2D from the WinRT frame.
    // held_frame_ remains alive until release_frame() is called, keeping the texture valid.
    auto surface = impl_->held_frame_.Surface();
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

    winrt::com_ptr<ID3D11Texture2D> winrt_texture;
    HRESULT hr = access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), winrt_texture.put_void());
    if (FAILED(hr)) {
        return Result<CaptureFrame>::error(
            std::format("GetInterface(ID3D11Texture2D) failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    // Transfer ownership: winrt::com_ptr → Microsoft::WRL::ComPtr (both AddRef/Release compatible)
    ComPtr<ID3D11Texture2D> wrl_texture;
    wrl_texture.Attach(winrt_texture.detach());

    D3D11_TEXTURE2D_DESC desc;
    wrl_texture->GetDesc(&desc);

    // SystemRelativeTime is synchronized with the DWM compositor (100-ns units → µs)
    const uint64_t pts_us = static_cast<uint64_t>(impl_->held_frame_.SystemRelativeTime().count()) / 10;

    // Update statistics
    auto elapsed = std::chrono::steady_clock::now() - start;
    double capture_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    impl_->stats_.frames_captured++;
    impl_->stats_.avg_capture_ms =
        (impl_->stats_.avg_capture_ms * (impl_->stats_.frames_captured - 1) + capture_ms)
        / impl_->stats_.frames_captured;
    if (capture_ms < impl_->stats_.min_capture_ms) impl_->stats_.min_capture_ms = capture_ms;
    if (capture_ms > impl_->stats_.max_capture_ms) impl_->stats_.max_capture_ms = capture_ms;

    impl_->current_capture_frame_.texture = wrl_texture;
    impl_->current_capture_frame_.width = desc.Width;
    impl_->current_capture_frame_.height = desc.Height;
    impl_->current_capture_frame_.timestamp_us = pts_us;

    return impl_->current_capture_frame_;
}

void WGCCapture::release_frame() {
    // Closing held_frame_ returns its WGC buffer to the pool for reuse.
    impl_->held_frame_ = nullptr;
}

void WGCCapture::get_resolution(uint32_t& width, uint32_t& height) const {
    if (impl_->capture_item_) {
        auto size = impl_->capture_item_.Size();
        width = static_cast<uint32_t>(size.Width);
        height = static_cast<uint32_t>(size.Height);
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

ID3D11Device* WGCCapture::get_device() const {
    return impl_->d3d11_device_.Get();
}

// ---------------------------------------------------------------------------
// WGCCaptureImpl — initialization helpers
// ---------------------------------------------------------------------------

bool WGCCaptureImpl::init_d3d11_device() {
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL feature_level;

    UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &d3d11_device_,
        &feature_level,
        &d3d11_context_
    );

    if (FAILED(hr)) {
        spdlog::error("[WGC] D3D11CreateDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    spdlog::debug("[WGC] D3D11 device created");
    return true;
}

bool WGCCaptureImpl::init_direct3d_device() {
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = d3d11_device_.As(&dxgi_device);
    if (FAILED(hr)) {
        spdlog::error("[WGC] QueryInterface for IDXGIDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), inspectable.put());
    if (FAILED(hr)) {
        spdlog::error("[WGC] CreateDirect3D11DeviceFromDXGIDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    direct3d_device_ = inspectable.as<wd3d::IDirect3DDevice>();
    spdlog::debug("[WGC] Direct3D device created");
    return true;
}

bool WGCCaptureImpl::init_capture_item(uintptr_t window_handle) {
    // uintptr_t → HWND: safe on x64 Windows because HWNDs are allocated in the
    // lower 4 GB of address space for backward compatibility.
    HWND hwnd = reinterpret_cast<HWND>(window_handle);

    if (hwnd == nullptr) {
        hwnd = GetForegroundWindow();
        if (hwnd == nullptr) {
            spdlog::error("[WGC] No foreground window found");
            return false;
        }
    }

    wchar_t window_title[256];
    GetWindowTextW(hwnd, window_title, static_cast<int>(std::size(window_title)));
    spdlog::debug("[WGC] Capturing window: {}", winrt::to_string(window_title));

    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                                  IGraphicsCaptureItemInterop>();
    try {
        winrt::check_hresult(interop->CreateForWindow(
            hwnd,
            winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(capture_item_)
        ));
    } catch (const winrt::hresult_error& e) {
        spdlog::error("[WGC] CreateForWindow failed: 0x{:08X}", static_cast<uint32_t>(e.code()));
        return false;
    }

    auto size = capture_item_.Size();
    spdlog::info("[WGC] Capture item created: {}x{}", size.Width, size.Height);
    return true;
}

bool WGCCaptureImpl::init_frame_pool() {
    current_size_ = capture_item_.Size();

    try {
        frame_pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            direct3d_device_,
            wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            current_size_
        );

        frame_pool_.FrameArrived([this](auto&& sender, auto&&) {
            on_frame_arrived(sender);
        });
    } catch (const winrt::hresult_error& e) {
        spdlog::error("[WGC] Direct3D11CaptureFramePool::CreateFreeThreaded failed: 0x{:08X}",
                      static_cast<uint32_t>(e.code()));
        return false;
    }

    spdlog::debug("[WGC] Frame pool created");
    return true;
}

bool WGCCaptureImpl::start_capture_session() {
    try {
        capture_session_ = frame_pool_.CreateCaptureSession(capture_item_);
        capture_session_.StartCapture();
    } catch (const winrt::hresult_error& e) {
        spdlog::error("[WGC] StartCapture failed: 0x{:08X}", static_cast<uint32_t>(e.code()));
        return false;
    }

    spdlog::debug("[WGC] Capture session started");
    return true;
}

// ---------------------------------------------------------------------------
// WGCCaptureImpl::on_frame_arrived
//
// Called on the WGC compositor thread (CreateFreeThreaded).
// Acquires frame_mutex_ before touching any shared state.
// Calls TryGetNextFrame() inside the lock so that concurrent compositor-thread
// invocations (rare but possible) serialize correctly.
// ---------------------------------------------------------------------------
void WGCCaptureImpl::on_frame_arrived(wgc::Direct3D11CaptureFramePool const& sender) {
    std::unique_lock lock(frame_mutex_);

    auto frame = sender.TryGetNextFrame();
    if (!frame) {
        // Pool was closed or frame already consumed — ignore spurious event.
        return;
    }

    auto new_size = frame.ContentSize();
    if (new_size.Width != current_size_.Width || new_size.Height != current_size_.Height) {
        spdlog::info("[WGC] Window resized: {}x{} -> {}x{}",
                     current_size_.Width, current_size_.Height,
                     new_size.Width, new_size.Height);
        resize_pending_ = true;
        pending_size_ = new_size;
    }

    // Transfer ownership into current_frame_.  acquire_frame() will move this
    // to held_frame_ under the same mutex, so the compositor thread is free to
    // overwrite current_frame_ on the very next FrameArrived without racing.
    current_frame_ = std::move(frame);
    has_new_frame_ = true;
    frame_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// WGCCaptureImpl::recreate_frame_pool
//
// Called from acquire_frame() — never from the FrameArrived callback — to avoid
// a re-entrancy loop.  The resize flag and new size are read under frame_mutex_
// by acquire_frame() before this call, so no further locking is needed here.
// ---------------------------------------------------------------------------
void WGCCaptureImpl::recreate_frame_pool(winrt::Windows::Graphics::SizeInt32 new_size) {
    // Close old session and pool (blocks until pending callbacks finish)
    if (capture_session_) {
        capture_session_.Close();
        capture_session_ = nullptr;
    }
    if (frame_pool_) {
        frame_pool_.Close();
        frame_pool_ = nullptr;
    }

    // Recreate with new dimensions
    frame_pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        direct3d_device_,
        wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        new_size
    );

    // Re-subscribe using the same handler as init_frame_pool
    frame_pool_.FrameArrived([this](auto&& sender, auto&&) {
        on_frame_arrived(sender);
    });

    capture_session_ = frame_pool_.CreateCaptureSession(capture_item_);
    capture_session_.StartCapture();

    current_size_ = new_size;
    spdlog::info("[WGC] Frame pool recreated: {}x{}", new_size.Width, new_size.Height);
}

} // namespace gamestream
