#include "dxgi_capture.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <wincodec.h>
#include <format>

namespace gamestream {

DXGICapture::~DXGICapture() {
    if (frame_acquired_) {
        release_frame();
    }
}

VoidResult DXGICapture::initialize(uint32_t adapter_index) {
    CaptureConfig config;
    config.adapter_index = adapter_index;
    config.find_amd_gpu = true;

    if (!initialize_with_config(config)) {
        return VoidResult::error("Failed to initialize DXGI capture");
    }
    return {};
}

bool DXGICapture::initialize_with_config(const CaptureConfig& config) {
    config_ = config;
    start_time_ = std::chrono::steady_clock::now();

    // Step 1: Create D3D11 device on the correct adapter
    if (!create_device(config.adapter_index, config.find_amd_gpu)) {
        spdlog::error("[DXGI] Failed to create D3D11 device");
        return false;
    }

    // Step 2: Create desktop duplication
    if (!create_duplication(config.output_index)) {
        spdlog::error("[DXGI] Failed to create desktop duplication");
        return false;
    }

    spdlog::info("[DXGI] Initialized successfully: {}x{}", width_, height_);
    return true;
}

bool DXGICapture::create_device(uint32_t adapter_index, bool find_amd) {
    // Create DXGI factory
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] CreateDXGIFactory1 failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Enumerate adapters to find AMD GPU
    ComPtr<IDXGIAdapter1> adapter;
    uint32_t current_index = 0;

    for (uint32_t i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        spdlog::debug(L"[DXGI] Adapter {}: {} (VendorID: 0x{:04X})", i, desc.Description, desc.VendorId);

        // Check if this is AMD (VendorId = 0x1002)
        if (find_amd && desc.VendorId == 0x1002) {
            spdlog::info(L"[DXGI] Found AMD GPU, using adapter {}", i);
            adapter_ = adapter;
            break;
        }

        // If not finding AMD specifically, use the requested index
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

    // Create D3D11 device with BGRA support (required for WGC compatibility and WIC encoding)
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL feature_level;

    UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(
        adapter_.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,  // Use adapter, not HARDWARE
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
    // Get output from adapter (first as IDXGIOutput, then QueryInterface to IDXGIOutput1)
    ComPtr<IDXGIOutput> output;
    HRESULT hr = adapter_->EnumOutputs(output_index, &output);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] EnumOutputs failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // QueryInterface to IDXGIOutput1 (required for DuplicateOutput1)
    hr = output.As(&output_);
    if (FAILED(hr)) {
        spdlog::error("[DXGI] QueryInterface to IDXGIOutput1 failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Get output description
    DXGI_OUTPUT_DESC output_desc;
    output_->GetDesc(&output_desc);
    spdlog::debug(L"[DXGI] Output: {} at ({}, {}) to ({}, {})",
        output_desc.DeviceName,
        output_desc.DesktopCoordinates.left,
        output_desc.DesktopCoordinates.top,
        output_desc.DesktopCoordinates.right,
        output_desc.DesktopCoordinates.bottom);

    width_ = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    height_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;

    // Get display mode to check refresh rate
    DXGI_MODE_DESC mode_desc{};
    mode_desc.Width = width_;
    mode_desc.Height = height_;
    mode_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    DXGI_MODE_DESC closest_mode{};
    hr = output_->FindClosestMatchingMode(&mode_desc, &closest_mode, nullptr);
    if (SUCCEEDED(hr)) {
        double refresh_rate = static_cast<double>(closest_mode.RefreshRate.Numerator) / closest_mode.RefreshRate.Denominator;
        spdlog::info("[DXGI] Monitor refresh rate: {:.2f} Hz", refresh_rate);
    }

    // Use DuplicateOutput1 with explicit format (BGRA)
    DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
    hr = output_->DuplicateOutput1(device_.Get(), 0, 1, formats, &duplication_);

    if (FAILED(hr)) {
        spdlog::error("[DXGI] DuplicateOutput1 failed: 0x{:08X}", static_cast<uint32_t>(hr));
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            spdlog::error("[DXGI] Too many applications using desktop duplication (max 1-2)");
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            spdlog::error("[DXGI] Desktop duplication not supported (are you in fullscreen exclusive mode?)");
        } else if (hr == E_ACCESSDENIED) {
            spdlog::error("[DXGI] Access denied - another application is already capturing");
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

    // Release previous frame if still held
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

    // Handle timeout (no new frame)
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        stats_.frames_skipped++;
        return Result<CaptureFrame>::error("timeout");  // Not an error, screen didn't change
    }

    // Handle access lost (resolution change, mode switch, etc.)
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        spdlog::warn("[DXGI] Access lost, attempting to recreate duplication...");
        recreate_duplication();
        return Result<CaptureFrame>::error("access_lost");
    }

    if (FAILED(hr)) {
        return Result<CaptureFrame>::error(std::format("AcquireNextFrame failed: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    // Get texture from resource
    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return Result<CaptureFrame>::error(std::format("Failed to get texture: 0x{:08X}", static_cast<uint32_t>(hr)));
    }

    frame_acquired_ = true;

    // Update statistics
    auto elapsed = std::chrono::steady_clock::now() - start;
    double capture_ms = std::chrono::duration<double, std::milli>(elapsed).count();

    stats_.frames_captured++;
    stats_.avg_capture_ms = (stats_.avg_capture_ms * (stats_.frames_captured - 1) + capture_ms) / stats_.frames_captured;
    if (capture_ms < stats_.min_capture_ms) stats_.min_capture_ms = capture_ms;
    if (capture_ms > stats_.max_capture_ms) stats_.max_capture_ms = capture_ms;

    // Calculate timestamp
    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();

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

bool DXGICapture::recreate_duplication() {
    duplication_.Reset();
    frame_acquired_ = false;

    if (create_duplication(config_.output_index)) {
        spdlog::info("[DXGI] Duplication recreated successfully");
        return true;
    }

    spdlog::error("[DXGI] Failed to recreate duplication");
    return false;
}

// Static utility function: Save texture to BMP using WIC
bool DXGICapture::save_texture_to_bmp(ID3D11Device* device,
                                      ID3D11DeviceContext* context,
                                      ID3D11Texture2D* texture,
                                      const std::wstring& filepath) {
    HRESULT hr;

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create staging texture (CPU-readable)
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging_texture;
    hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to create staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Copy GPU texture to staging (GPU -> CPU)
    context->CopyResource(staging_texture.Get(), texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to map staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Create WIC factory
    ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wic_factory)
    );
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create WIC factory: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Create output stream
    ComPtr<IWICStream> stream;
    hr = wic_factory->CreateStream(&stream);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    hr = stream->InitializeFromFilename(filepath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        spdlog::error("[WIC] Failed to create output file: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Create BMP encoder
    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic_factory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &encoder);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    // Create frame
    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    // Set frame size
    hr = frame->SetSize(desc.Width, desc.Height);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    // Set pixel format (BGRA)
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        context->Unmap(staging_texture.Get(), 0);
        return false;
    }

    // Write pixels
    hr = frame->WritePixels(
        desc.Height,
        mapped.RowPitch,
        mapped.RowPitch * desc.Height,
        reinterpret_cast<BYTE*>(mapped.pData)
    );

    context->Unmap(staging_texture.Get(), 0);

    if (FAILED(hr)) {
        spdlog::error("[WIC] Failed to write pixels: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Commit frame and encoder
    hr = frame->Commit();
    if (FAILED(hr)) {
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

} // namespace gamestream
