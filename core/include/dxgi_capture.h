#pragma once

#include "iframe_capture.h"
#include "result.h"
#include "capture_types.h"

#include <d3d11.h>
#include <dxgi1_5.h>   // IDXGIOutput5 (DuplicateOutput1 with explicit format)
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <chrono>

namespace gamestream {

/// DXGI Desktop Duplication implementation
class DXGICapture : public IFrameCapture {
public:
    DXGICapture() = default;
    ~DXGICapture();

    // Non-copyable (holds COM resources)
    DXGICapture(const DXGICapture&) = delete;
    DXGICapture& operator=(const DXGICapture&) = delete;

    // Movable (per principle 15.7)
    DXGICapture(DXGICapture&&) noexcept = default;
    DXGICapture& operator=(DXGICapture&&) noexcept = default;

    // IFrameCapture interface
    [[nodiscard]] VoidResult initialize(uintptr_t adapter_index = 0) override;
    [[nodiscard]] Result<CaptureFrame> acquire_frame(uint64_t timeout_ms = 16) override;
    void release_frame() override;
    void get_resolution(uint32_t& width, uint32_t& height) const override;
    bool is_initialized() const override { return duplication_ != nullptr; }
    CaptureStats get_stats() const override { return stats_; }
    ID3D11Device* get_device() const override { return device_.Get(); }

    // Additional DXGI-specific methods
    [[nodiscard]] VoidResult initialize_with_config(const CaptureConfig& config);
    [[nodiscard]] VoidResult recreate_duplication();

    // Utility: Save texture to BMP file using WIC
    [[nodiscard]] static VoidResult save_texture_to_bmp(ID3D11Device* device,
                                                        ID3D11DeviceContext* context,
                                                        ID3D11Texture2D* texture,
                                                        const std::wstring& filepath);

private:
    bool create_device(uint32_t adapter_index, bool find_amd);
    bool find_adapter(uint32_t adapter_index, bool find_amd,
                      Microsoft::WRL::ComPtr<IDXGIFactory1>& factory);
    bool create_d3d11_device_from_adapter();
    bool create_duplication(uint32_t output_index);
    bool open_output(uint32_t output_index, DXGI_OUTPUT_DESC& desc, std::string& name_utf8);
    void log_output_desc(const DXGI_OUTPUT_DESC& desc, const std::string& name_utf8) const;
    void update_output_size(const DXGI_OUTPUT_DESC& desc);
    void log_refresh_rate() const;
    bool create_output_duplication();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<IDXGIOutput5> output_;  // DuplicateOutput1 lives in IDXGIOutput5

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    CaptureConfig config_;
    bool frame_acquired_ = false;

    // Statistics
    CaptureStats stats_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace gamestream
