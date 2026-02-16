#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

namespace gamestream {

struct CaptureConfig {
    uint32_t adapter_index = 0;  // 0 = first adapter, will search for AMD
    uint32_t output_index = 0;   // 0 = primary monitor
    bool find_amd_gpu = true;    // Automatically find AMD GPU
};

class DXGICapture {
public:
    DXGICapture() = default;
    ~DXGICapture();

    // Initialize DXGI capture
    bool Initialize(const CaptureConfig& config = {});

    // Capture next frame
    // Returns true if frame captured, false if timeout or error
    bool CaptureFrame(ID3D11Texture2D** out_texture, uint64_t timeout_ms = 16);

    // Release current frame (must be called after CaptureFrame)
    void ReleaseFrame();

    // Get capture resolution
    void GetResolution(uint32_t& width, uint32_t& height) const;

    // Check if initialized
    bool IsInitialized() const { return duplication_ != nullptr; }

    // Recreate duplication (call after DXGI_ERROR_ACCESS_LOST)
    bool RecreateDuplication();

private:
    bool CreateDevice(uint32_t adapter_index, bool find_amd);
    bool CreateDuplication(uint32_t output_index);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<IDXGIOutput1> output_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    CaptureConfig config_;
    bool frame_acquired_ = false;
};

// Utility: Save texture to BMP file using WIC
bool SaveTextureToBMP(ID3D11Device* device,
                      ID3D11DeviceContext* context,
                      ID3D11Texture2D* texture,
                      const std::wstring& filepath);

} // namespace gamestream
