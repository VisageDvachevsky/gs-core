#include "dxgi_capture.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <wincodec.h>
#include <format>

using Microsoft::WRL::ComPtr;

namespace gamestream {

DXGICapture::~DXGICapture() {
    if (frame_acquired_) {
        release_frame();
    }
}

VoidResult DXGICapture::initialize(uintptr_t adapter_index) {
    CaptureConfig config;
    // adapter_index fits comfortably in uint32_t; cast is safe for any realistic index value.
    config.adapter_index = static_cast<uint32_t>(adapter_index);
    config.find_amd_gpu = true;
    return initialize_with_config(config);  // propagate VoidResult directly
}

VoidResult DXGICapture::initialize_with_config(const CaptureConfig& config) {
    config_ = config;
    start_time_ = std::chrono::steady_clock::now();

    if (!create_device(config.adapter_index, config.find_amd_gpu)) {
        return VoidResult::error("Failed to create D3D11 device");
    }

    if (!create_duplication(config.output_index)) {
        return VoidResult::error("Failed to create desktop duplication");
    }

    spdlog::info("[DXGI] Initialized successfully: {}x{}", width_, height_);
    return {};
}

bool DXGICapture::create_device(uint32_t adapter_index, bool find_amd) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] CreateDXGIFactory1 failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    uint32_t current_index = 0;

    for (uint32_t i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Convert WCHAR description to UTF-8 for spdlog (no SPDLOG_WCHAR_TO_UTF8_SUPPORT needed)
        const int needed = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                               nullptr, 0, nullptr, nullptr);
        std::string desc_utf8(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                            desc_utf8.data(), needed, nullptr, nullptr);

        spdlog::debug("[DXGI] Adapter {}: {} (VendorID: 0x{:04X})", i, desc_utf8, desc.VendorId);

        if (find_amd && desc.VendorId == 0x1002) {  // AMD vendor ID
            spdlog::info("[DXGI] Found AMD GPU: {} (adapter {})", desc_utf8, i);
            adapter_ = adapter;
            break;
        }

        if (!find_amd && current_index == adapter_index) {
            adapter_ = adapter;
            break;
        }

        current_index++;
    }

    if (!adapter_) {
        spdlog::error("[DXGI] No suitable adapter found");
        return false;
    }

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL feature_level;

    // D3D11_CREATE_DEVICE_BGRA_SUPPORT required for WGC compatibility and WIC encoding.
    UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(
        adapter_.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,  // Must be UNKNOWN when an explicit adapter is provided
        nullptr,
        create_flags,
        feature_levels,
        _countof(feature_levels),
        D3D11_SDK_VERSION,
        &device_,
        &feature_level,
        &context_
    );

    if (FAILED(hr)) {
        spdlog::error("[DXGI] D3D11CreateDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    spdlog::debug("[DXGI] D3D11 device created successfully");
    return true;
}

bool DXGICapture::create_duplication(uint32_t output_index) {
    // EnumOutputs returns IDXGIOutput; QueryInterface up to IDXGIOutput5 for DuplicateOutput1.
    ComPtr<IDXGIOutput> output;
    HRESULT hr = adapter_->EnumOutputs(output_index, &output);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] EnumOutputs failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    hr = output.As(&output_);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] QueryInterface to IDXGIOutput5 failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    DXGI_OUTPUT_DESC output_desc;
    output_->GetDesc(&output_desc);

    // Convert WCHAR device name to UTF-8 for spdlog
    const int needed = WideCharToMultiByte(CP_UTF8, 0, output_desc.DeviceName, -1,
                                           nullptr, 0, nullptr, nullptr);
    std::string device_name_utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, output_desc.DeviceName, -1,
                        device_name_utf8.data(), needed, nullptr, nullptr);

    spdlog::debug("[DXGI] Output: {} at ({}, {}) to ({}, {})",
                  device_name_utf8,
                  output_desc.DesktopCoordinates.left,
                  output_desc.DesktopCoordinates.top,
                  output_desc.DesktopCoordinates.right,
                  output_desc.DesktopCoordinates.bottom);

    width_  = static_cast<uint32_t>(
        output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left);
    height_ = static_cast<uint32_t>(
        output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top);

    // Query monitor refresh rate for informational logging
    DXGI_MODE_DESC mode_desc{};
    mode_desc.Width  = width_;
    mode_desc.Height = height_;
    mode_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    DXGI_MODE_DESC closest_mode{};
    hr = output_->FindClosestMatchingMode(&mode_desc, &closest_mode, nullptr);
    if (SUCCEEDED(hr)) {
        double refresh_rate = static_cast<double>(closest_mode.RefreshRate.Numerator)
                            / closest_mode.RefreshRate.Denominator;
        spdlog::info("[DXGI] Monitor refresh rate: {:.2f} Hz", refresh_rate);
    }

    // Try DuplicateOutput1 (DXGI 1.5) with explicit BGRA format first.
    // Falls back to DuplicateOutput (DXGI 1.2) if the driver returns UNSUPPORTED
    // (e.g. HDR mode, some AMD configurations, certain remote session types).
    DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
    hr = output_->DuplicateOutput1(device_.Get(), 0, 1, formats, &duplication_);

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        spdlog::warn("[DXGI] DuplicateOutput1 unsupported (HDR/driver limitation), falling back to DuplicateOutput");
        hr = output_->DuplicateOutput(device_.Get(), &duplication_);
    }

    if (FAILED(hr)) {
        spdlog::error("[DXGI] DuplicateOutput failed: 0x{:08X}", static_cast<uint32_t>(hr));
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            spdlog::error("[DXGI] Too many applications using desktop duplication (max 1-2)");
        } else if (hr == E_ACCESSDENIED) {
            spdlog::error("[DXGI] Access denied — another application is already capturing");
        }
        return false;
    }

    spdlog::debug("[DXGI] Desktop duplication created successfully");
    return true;
}

Result<CaptureFrame> DXGICapture::acquire_frame(uint64_t timeout_ms) {
    if (!duplication_) {
        return Result<CaptureFrame>::error("Duplication not initialized");
    }

    if (frame_acquired_) {
        release_frame();
    }

    auto start = std::chrono::steady_clock::now();

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info;

    HRESULT hr = duplication_->AcquireNextFrame(
        static_cast<UINT>(timeout_ms),
        &frame_info,
        &resource
    );

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        stats_.frames_skipped++;
        return Result<CaptureFrame>::error("timeout");
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        spdlog::warn("[DXGI] Access lost, attempting to recreate duplication...");
        if (auto r = recreate_duplication(); !r) {
            spdlog::error("[DXGI] Recreate failed: {}", r.error());
        }
        return Result<CaptureFrame>::error("access_lost");
    }

    if (FAILED(hr)) {
        return Result<CaptureFrame>::error(
            std::format("AcquireNextFrame failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return Result<CaptureFrame>::error(
            std::format("QueryInterface for ID3D11Texture2D failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    frame_acquired_ = true;

    // Update statistics
    auto elapsed = std::chrono::steady_clock::now() - start;
    double capture_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    stats_.frames_captured++;
    stats_.avg_capture_ms =
        (stats_.avg_capture_ms * (stats_.frames_captured - 1) + capture_ms) / stats_.frames_captured;
    if (capture_ms < stats_.min_capture_ms) stats_.min_capture_ms = capture_ms;
    if (capture_ms > stats_.max_capture_ms) stats_.max_capture_ms = capture_ms;

    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count();

    CaptureFrame frame;
    frame.texture = texture;
    frame.width = width_;
    frame.height = height_;
    frame.timestamp_us = timestamp_us;

    return frame;
}

void DXGICapture::release_frame() {
    if (duplication_ && frame_acquired_) {
        duplication_->ReleaseFrame();
        frame_acquired_ = false;
    }
}

void DXGICapture::get_resolution(uint32_t& width, uint32_t& height) const {
    width = width_;
    height = height_;
}

VoidResult DXGICapture::recreate_duplication() {
    duplication_.Reset();
    frame_acquired_ = false;

    // E_ACCESSDENIED / DXGI_ERROR_NOT_CURRENTLY_AVAILABLE are transient during
    // a game's fullscreen/presentation-mode transition (HAGS, DirectFlip, etc.).
    // Retry up to kMaxRetries times before giving up.
    static constexpr int   kMaxRetries   = 10;
    static constexpr DWORD kRetryDelayMs = 100;

    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        if (attempt > 0) {
            spdlog::debug("[DXGI] Recreate attempt {}/{}, waiting {}ms...",
                          attempt + 1, kMaxRetries, kRetryDelayMs);
            Sleep(kRetryDelayMs);
        }
        if (create_duplication(config_.output_index)) {
            spdlog::info("[DXGI] Duplication recreated successfully{}",
                         attempt > 0 ? std::format(" (attempt {})", attempt + 1) : "");
            return {};
        }
    }

    return VoidResult::error(
        std::format("Failed to recreate desktop duplication after {} attempts", kMaxRetries));
}

// Static utility — raw D3D11 pointers are passed in (caller owns them).
VoidResult DXGICapture::save_texture_to_bmp(ID3D11Device* device,
                                             ID3D11DeviceContext* context,
                                             ID3D11Texture2D* texture,
                                             const std::wstring& filepath) {
    HRESULT hr;

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Staging texture for CPU read-back
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging_texture;
    hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to create staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: CreateTexture2D failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    context->CopyResource(staging_texture.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to map staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: Map failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&wic_factory));
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create WIC factory: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: CoCreateInstance(WICImagingFactory) failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    ComPtr<IWICStream> stream;
    hr = wic_factory->CreateStream(&stream);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create WIC stream: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: CreateStream failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = stream->InitializeFromFilename(filepath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to open output file: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: InitializeFromFilename failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic_factory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &encoder);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create BMP encoder: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: CreateEncoder(BMP) failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to initialize encoder: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: encoder Initialize failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create frame: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: CreateNewFrame failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to initialize frame: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: frame Initialize failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = frame->SetSize(desc.Width, desc.Height);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to set frame size: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: SetSize failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to set pixel format: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: SetPixelFormat failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = frame->WritePixels(
        desc.Height,
        mapped.RowPitch,
        mapped.RowPitch * desc.Height,
        reinterpret_cast<BYTE*>(mapped.pData)
    );

    context->Unmap(staging_texture.Get(), 0);

    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to write pixels: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: WritePixels failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to commit frame: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: frame Commit failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to commit encoder: 0x{:08X}", static_cast<uint32_t>(hr));
        return VoidResult::error(std::format("WIC: encoder Commit failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    return {};
}

} // namespace gamestream
