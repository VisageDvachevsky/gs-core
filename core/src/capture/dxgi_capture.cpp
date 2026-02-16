#include "dxgi_capture.h"
#include <iostream>
#include <vector>
#include <wincodec.h>

namespace gamestream {

DXGICapture::~DXGICapture() {
    if (frame_acquired_) {
        ReleaseFrame();
    }
}

bool DXGICapture::Initialize(const CaptureConfig& config) {
    config_ = config;

    // Step 1: Create D3D11 device on the correct adapter
    if (!CreateDevice(config.adapter_index, config.find_amd_gpu)) {
        std::cerr << "[DXGI] Failed to create D3D11 device\n";
        return false;
    }

    // Step 2: Create desktop duplication
    if (!CreateDuplication(config.output_index)) {
        std::cerr << "[DXGI] Failed to create desktop duplication\n";
        return false;
    }

    std::cout << "[DXGI] Initialized successfully: " << width_ << "x" << height_ << "\n";
    return true;
}

bool DXGICapture::CreateDevice(uint32_t adapter_index, bool find_amd) {
    // Create DXGI factory
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    if (FAILED(hr)) {
        std::cerr << "[DXGI] CreateDXGIFactory1 failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Enumerate adapters to find AMD GPU
    ComPtr<IDXGIAdapter1> adapter;
    uint32_t current_index = 0;

    for (uint32_t i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        std::wcout << L"[DXGI] Adapter " << i << L": " << desc.Description << L"\n";
        std::wcout << L"       VendorID: 0x" << std::hex << desc.VendorId << std::dec << L"\n";

        // Check if this is AMD (VendorId = 0x1002)
        if (find_amd && desc.VendorId == 0x1002) {
            std::wcout << L"[DXGI] Found AMD GPU, using this adapter\n";
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
        std::cerr << "[DXGI] No suitable adapter found\n";
        return false;
    }

    // Create D3D11 device
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL feature_level;

    hr = D3D11CreateDevice(
        adapter_.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,  // Use adapter, not HARDWARE
        nullptr,
        0,  // No debug flags for release
        feature_levels,
        _countof(feature_levels),
        D3D11_SDK_VERSION,
        &device_,
        &feature_level,
        &context_
    );

    if (FAILED(hr)) {
        std::cerr << "[DXGI] D3D11CreateDevice failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    std::cout << "[DXGI] D3D11 device created successfully\n";
    return true;
}

bool DXGICapture::CreateDuplication(uint32_t output_index) {
    // Get output from adapter
    HRESULT hr = adapter_->EnumOutputs(output_index, reinterpret_cast<IDXGIOutput**>(output_.GetAddressOf()));
    if (FAILED(hr)) {
        std::cerr << "[DXGI] EnumOutputs failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Get output description
    DXGI_OUTPUT_DESC output_desc;
    output_->GetDesc(&output_desc);
    std::wcout << L"[DXGI] Output: " << output_desc.DeviceName << L"\n";
    std::cout << "[DXGI] Desktop coordinates: ("
              << output_desc.DesktopCoordinates.left << ", "
              << output_desc.DesktopCoordinates.top << ") to ("
              << output_desc.DesktopCoordinates.right << ", "
              << output_desc.DesktopCoordinates.bottom << ")\n";

    width_ = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    height_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;

    // Create desktop duplication using standard DuplicateOutput API
    hr = output_->DuplicateOutput(device_.Get(), &duplication_);

    if (FAILED(hr)) {
        std::cerr << "[DXGI] DuplicateOutput failed: 0x" << std::hex << hr << "\n";
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            std::cerr << "[DXGI] Too many applications using desktop duplication (max 1-2)\n";
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            std::cerr << "[DXGI] Desktop duplication not supported (are you in fullscreen exclusive mode?)\n";
        }
        return false;
    }

    std::cout << "[DXGI] Desktop duplication created successfully\n";
    return true;
}

bool DXGICapture::CaptureFrame(ID3D11Texture2D** out_texture, uint64_t timeout_ms) {
    if (!duplication_) {
        return false;
    }

    // Release previous frame if still held
    if (frame_acquired_) {
        ReleaseFrame();
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info;

    HRESULT hr = duplication_->AcquireNextFrame(
        static_cast<UINT>(timeout_ms),
        &frame_info,
        &resource
    );

    // Handle timeout (no new frame)
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // Not an error - screen didn't change
        return false;
    }

    // Handle access lost (resolution change, mode switch, etc.)
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        std::cout << "[DXGI] Access lost, attempting to recreate duplication...\n";
        RecreateDuplication();
        return false;
    }

    if (FAILED(hr)) {
        std::cerr << "[DXGI] AcquireNextFrame failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Get texture from resource
    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) {
        std::cerr << "[DXGI] Failed to get texture from resource: 0x" << std::hex << hr << "\n";
        duplication_->ReleaseFrame();
        return false;
    }

    *out_texture = texture.Detach();
    frame_acquired_ = true;
    return true;
}

void DXGICapture::ReleaseFrame() {
    if (duplication_ && frame_acquired_) {
        duplication_->ReleaseFrame();
        frame_acquired_ = false;
    }
}

void DXGICapture::GetResolution(uint32_t& width, uint32_t& height) const {
    width = width_;
    height = height_;
}

bool DXGICapture::RecreateDuplication() {
    duplication_.Reset();
    frame_acquired_ = false;

    if (CreateDuplication(config_.output_index)) {
        std::cout << "[DXGI] Duplication recreated successfully\n";
        return true;
    }

    std::cerr << "[DXGI] Failed to recreate duplication\n";
    return false;
}

// Utility function: Save texture to BMP using WIC
bool SaveTextureToBMP(ID3D11Device* device,
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
        std::cerr << "[WIC] Failed to create staging texture: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Copy GPU texture to staging (GPU -> CPU)
    context->CopyResource(staging_texture.Get(), texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        std::cerr << "[WIC] Failed to map staging texture: 0x" << std::hex << hr << "\n";
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
        std::cerr << "[WIC] Failed to create WIC factory: 0x" << std::hex << hr << "\n";
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
        std::cerr << "[WIC] Failed to create output file: " << std::hex << hr << "\n";
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
        std::cerr << "[WIC] Failed to write pixels: 0x" << std::hex << hr << "\n";
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
