#include "amf_encoder.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <format>

// Win32 for LoadLibrary
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// AMF SDK headers — deliberately confined to this translation unit (PIMPL)
// Include path: ${AMF_SDK_DIR}/amf/public/include  (set in CMakeLists.txt)
#include <core/Factory.h>
#include <core/Context.h>
#include <core/Surface.h>
#include <core/Buffer.h>
#include <core/Data.h>
#include <components/Component.h>
#include <components/VideoEncoderVCE.h>

using namespace std::chrono;

namespace gamestream {

// ---------------------------------------------------------------------------
// AMFEncoderImpl — all AMF state lives here, never exposed in the header
// ---------------------------------------------------------------------------

class AMFEncoderImpl {
public:
    // Resources — destroyed in declaration-reverse order by destructor
    HMODULE                amf_lib_     = nullptr;
    amf::AMFFactory*       factory_     = nullptr;  // owned by the DLL; not deleted manually
    amf::AMFContextPtr     context_;
    amf::AMFComponentPtr   encoder_;

    EncoderConfig   config_;
    EncoderStats    stats_;

    // Thread-safe flag for IDR request (written by DataChannel thread, read by encode thread)
    std::atomic<bool> keyframe_requested_{false};

    bool is_initialized_ = false;
    steady_clock::time_point start_time_;

    // Helpers
    [[nodiscard]] VoidResult load_amf_dll();
    [[nodiscard]] VoidResult create_context(ID3D11Device* device);
    [[nodiscard]] VoidResult create_encoder();
    [[nodiscard]] VoidResult configure_encoder();

    void update_stats(double encode_ms, size_t byte_count, bool is_kf);
};

// ---------------------------------------------------------------------------
// AMFEncoderImpl helpers
// ---------------------------------------------------------------------------

VoidResult AMFEncoderImpl::load_amf_dll() {
    amf_lib_ = LoadLibraryW(AMF_DLL_NAME);  // "amfrt64.dll"
    if (!amf_lib_) {
        return VoidResult::error(std::format(
            "LoadLibrary({}) failed: 0x{:08X}",
            AMF_DLL_NAMEA, static_cast<uint32_t>(GetLastError())));
    }

    auto* init_fn = reinterpret_cast<AMFInit_Fn>(
        GetProcAddress(amf_lib_, AMF_INIT_FUNCTION_NAME));
    if (!init_fn) {
        FreeLibrary(amf_lib_);
        amf_lib_ = nullptr;
        return VoidResult::error("GetProcAddress(AMFInit) failed — AMF DLL missing or corrupt");
    }

    AMF_RESULT res = init_fn(AMF_FULL_VERSION, &factory_);
    if (res != AMF_OK) {
        FreeLibrary(amf_lib_);
        amf_lib_ = nullptr;
        return VoidResult::error(std::format("AMFInit failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    spdlog::debug("[AMF] DLL loaded and factory created");
    return {};
}

VoidResult AMFEncoderImpl::create_context(ID3D11Device* device) {
    AMF_RESULT res = factory_->CreateContext(&context_);
    if (res != AMF_OK) {
        return VoidResult::error(std::format(
            "AMFFactory::CreateContext failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    // Attach the shared D3D11 device — enables zero-copy via CreateSurfaceFromDX11Native
    res = context_->InitDX11(device);
    if (res != AMF_OK) {
        return VoidResult::error(std::format(
            "AMFContext::InitDX11 failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    spdlog::debug("[AMF] Context created and bound to D3D11 device");
    return {};
}

VoidResult AMFEncoderImpl::create_encoder() {
    AMF_RESULT res = factory_->CreateComponent(
        context_, AMFVideoEncoderVCE_AVC, &encoder_);
    if (res != AMF_OK) {
        return VoidResult::error(std::format(
            "AMFFactory::CreateComponent(AVC) failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    spdlog::debug("[AMF] H.264 encoder component created");
    return {};
}

VoidResult AMFEncoderImpl::configure_encoder() {
    // Type-safe property setter: converts any integral/enum/bool value to amf_int64
    // and includes the AMF property name (an ASCII-range wchar_t* constant) in the
    // error message.  Template lambda (C++20) avoids the old AMF_SET macro.
    auto narrow_name = [](const wchar_t* w) -> std::string {
        // AMF property name constants are ASCII-range wide strings — convert safely.
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), len, nullptr, nullptr);
        return result;
    };
    auto set = [&]<typename V>(const wchar_t* prop, V val) -> VoidResult {
        AMF_RESULT r = encoder_->SetProperty(prop, static_cast<amf_int64>(val));
        if (r != AMF_OK) {
            return VoidResult::error(std::format(
                "SetProperty({}) failed: 0x{:08X}",
                narrow_name(prop), static_cast<uint32_t>(r)));
        }
        return {};
    };

    // Must be set FIRST — configures all other defaults
    if (auto r = set(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY); !r) return r;

    // H.264 Baseline profile — no CABAC, no B-frames, widest decoder support
    if (auto r = set(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_BASELINE); !r) return r;

    // No B-frames: minimize encode latency (B-frames add 1+ frame delay)
    if (auto r = set(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0); !r) return r;

    // Minimum reference frames: reduces encoder buffering latency
    if (auto r = set(AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, 1); !r) return r;

    // Speed preset: lower quality, much lower latency (< 6 ms target)
    if (auto r = set(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED); !r) return r;

    // Internal low-latency mode (POC mode 2)
    if (auto r = set(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true); !r) return r;

    // CBR: constant bitrate — predictable network load for streaming
    if (auto r = set(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR); !r) return r;

    // Bitrate from config
    if (auto r = set(AMF_VIDEO_ENCODER_TARGET_BITRATE, config_.bitrate_bps); !r) return r;

    // Baseline profile uses CAVLC (not CABAC) — required for compatibility
    if (auto r = set(AMF_VIDEO_ENCODER_CABAC_ENABLE, AMF_VIDEO_ENCODER_UNDEFINED); !r) return r;

    // Frame rate — AMFRate requires a different SetProperty overload (not amf_int64)
    AMFRate fps_rate = AMFConstructRate(config_.fps, 1);
    AMF_RESULT res = encoder_->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, fps_rate);
    if (res != AMF_OK) {
        return VoidResult::error(std::format(
            "SetProperty(FRAMERATE) failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    // IDR every 5 seconds: allows new viewer to start decoding without waiting too long
    const amf_int64 idr_period = static_cast<amf_int64>(config_.fps) * 5;
    if (auto r = set(AMF_VIDEO_ENCODER_IDR_PERIOD, idr_period); !r) return r;

    // Apply configuration and allocate GPU resources
    res = encoder_->Init(
        amf::AMF_SURFACE_BGRA,
        static_cast<amf_int32>(config_.width),
        static_cast<amf_int32>(config_.height));

    if (res != AMF_OK) {
        return VoidResult::error(std::format(
            "AMFComponent::Init(BGRA, {}x{}) failed: 0x{:08X}",
            config_.width, config_.height, static_cast<uint32_t>(res)));
    }

    spdlog::info("[AMF] Encoder configured: {}x{}@{} fps, {} Mbps, Baseline, Ultra-Low-Latency",
                 config_.width, config_.height, config_.fps,
                 config_.bitrate_bps / 1'000'000);
    return {};
}

void AMFEncoderImpl::update_stats(double encode_ms, size_t byte_count, bool is_kf) {
    stats_.frames_encoded++;
    stats_.bytes_encoded += byte_count;

    if (is_kf) {
        stats_.keyframes_encoded++;
    }

    // Running average — numerically stable incremental formula
    stats_.avg_encode_ms =
        (stats_.avg_encode_ms * (stats_.frames_encoded - 1) + encode_ms)
        / stats_.frames_encoded;

    if (encode_ms < stats_.min_encode_ms) stats_.min_encode_ms = encode_ms;
    if (encode_ms > stats_.max_encode_ms) stats_.max_encode_ms = encode_ms;

    // Log statistics once per second (one INFO per 60-frame batch)
    if (stats_.frames_encoded % static_cast<uint64_t>(config_.fps) == 0) {
        spdlog::info("[AMF] avg: {:.2f}ms | min: {:.2f}ms | max: {:.2f}ms | "
                     "frames: {} | keyframes: {}",
                     stats_.avg_encode_ms, stats_.min_encode_ms, stats_.max_encode_ms,
                     stats_.frames_encoded, stats_.keyframes_encoded);
    }
}

// ---------------------------------------------------------------------------
// AMFEncoder — public API implementation
// ---------------------------------------------------------------------------

AMFEncoder::AMFEncoder()
    : impl_(std::make_unique<AMFEncoderImpl>()) {}

AMFEncoder::~AMFEncoder() {
    // AMF objects must be destroyed in order: encoder → context → factory
    // Smart pointers (AMFComponentPtr, AMFContextPtr) release automatically.
    // factory_ is owned by the DLL — only FreeLibrary releases it.
    impl_->encoder_.Release();
    impl_->context_.Release();

    if (impl_->amf_lib_) {
        FreeLibrary(impl_->amf_lib_);
        impl_->amf_lib_ = nullptr;
    }
    spdlog::debug("[AMF] Encoder destroyed");
}

VoidResult AMFEncoder::initialize(ID3D11Device* device, const EncoderConfig& config) {
    if (!device) {
        return VoidResult::error("AMFEncoder::initialize: device must not be nullptr");
    }

    if (impl_->is_initialized_) {
        return VoidResult::error("AMFEncoder::initialize: already initialized");
    }

    impl_->config_ = config;
    impl_->start_time_ = steady_clock::now();

    if (auto r = impl_->load_amf_dll();     !r) return r;
    if (auto r = impl_->create_context(device); !r) return r;
    if (auto r = impl_->create_encoder();   !r) return r;
    if (auto r = impl_->configure_encoder(); !r) return r;

    impl_->is_initialized_ = true;
    spdlog::info("[AMF] Initialized successfully");
    return {};
}

Result<EncodedFrame> AMFEncoder::encode(const CaptureFrame& frame) {
    if (!impl_->is_initialized_) {
        return Result<EncodedFrame>::error("AMFEncoder::encode: not initialized");
    }

    auto t0 = steady_clock::now();

    // --- Step 1: wrap D3D11 texture in an AMF surface (zero-copy) ---
    amf::AMFSurfacePtr surface;
    AMF_RESULT res = impl_->context_->CreateSurfaceFromDX11Native(
        frame.texture.Get(), &surface, nullptr);

    if (res != AMF_OK) {
        return Result<EncodedFrame>::error(std::format(
            "CreateSurfaceFromDX11Native failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    // --- Step 2: set presentation timestamp (AMF uses 100-ns units) ---
    surface->SetPts(static_cast<amf_pts>(frame.timestamp_us) * 10);

    // --- Step 3: force IDR if requested (e.g. from DataChannel PLI signal) ---
    if (impl_->keyframe_requested_.exchange(false, std::memory_order_acq_rel)) {
        res = surface->SetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE,
                                   static_cast<amf_int64>(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));
        if (res != AMF_OK) {
            spdlog::warn("[AMF] SetProperty(FORCE_PICTURE_TYPE=IDR) failed: 0x{:08X}",
                         static_cast<uint32_t>(res));
            // Non-fatal: continue without force IDR this frame
        }
    }

    // --- Step 4: submit frame to encoder pipeline ---
    res = impl_->encoder_->SubmitInput(surface);
    if (res == AMF_INPUT_FULL) {
        // Should not happen in ultra-low-latency mode with 1 ref frame,
        // but handle defensively: drain pending output then retry.
        spdlog::warn("[AMF] SubmitInput returned INPUT_FULL — draining");
        return Result<EncodedFrame>::error("encoder input full, retry next frame");
    }
    if (res != AMF_OK) {
        return Result<EncodedFrame>::error(std::format(
            "SubmitInput failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    // --- Step 5: poll for encoded output (ultra-low-latency = 1 output per input) ---
    amf::AMFDataPtr output_data;
    constexpr int kMaxPollAttempts = 100;

    for (int attempt = 0; attempt < kMaxPollAttempts; ++attempt) {
        res = impl_->encoder_->QueryOutput(&output_data);

        if (res == AMF_OK && output_data) {
            break;  // Got a frame
        }

        if (res == AMF_REPEAT || (res == AMF_OK && !output_data)) {
            // Not ready yet — yield and retry
            Sleep(0);  // yield to OS scheduler without sleeping
            continue;
        }

        return Result<EncodedFrame>::error(std::format(
            "QueryOutput failed: 0x{:08X}", static_cast<uint32_t>(res)));
    }

    if (!output_data) {
        return Result<EncodedFrame>::error("QueryOutput: no output after max poll attempts");
    }

    // --- Step 6: extract bitstream from the output AMFBuffer ---
    amf::AMFBufferPtr output_buffer(output_data);
    if (!output_buffer) {
        return Result<EncodedFrame>::error("QueryOutput: output is not an AMFBuffer");
    }

    const void*  raw_data  = output_buffer->GetNative();
    const size_t data_size = output_buffer->GetSize();

    if (!raw_data || data_size == 0) {
        return Result<EncodedFrame>::error("QueryOutput: empty bitstream buffer");
    }

    // --- Step 7: check frame type ---
    amf_int64 output_type = AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P;
    output_buffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &output_type);
    const bool is_keyframe = (output_type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR);

    // --- Step 8: copy bitstream to output frame ---
    EncodedFrame encoded;
    encoded.data.assign(
        static_cast<const uint8_t*>(raw_data),
        static_cast<const uint8_t*>(raw_data) + data_size);
    encoded.is_keyframe = is_keyframe;
    encoded.pts_us      = frame.timestamp_us;

    // --- Step 9: update stats ---
    auto t1 = steady_clock::now();
    double encode_ms = duration<double, std::milli>(t1 - t0).count();
    impl_->update_stats(encode_ms, data_size, is_keyframe);

    // Per-frame TRACE log (compiled out in Release)
    spdlog::trace("[AMF] {} frame: {} bytes, {:.2f} ms",
                  is_keyframe ? "IDR" : "P",
                  data_size, encode_ms);

    return encoded;
}

void AMFEncoder::request_keyframe() {
    impl_->keyframe_requested_.store(true, std::memory_order_release);
    spdlog::debug("[AMF] IDR requested");
}

bool AMFEncoder::is_initialized() const {
    return impl_->is_initialized_;
}

EncoderStats AMFEncoder::get_stats() const {
    return impl_->stats_;
}

} // namespace gamestream
