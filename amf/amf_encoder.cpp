

#include "amf_encoder.h"

#include <Windows.h>

#include <cinttypes>

#include "components/ComponentCaps.h"
#include "components/VideoEncoderAV1.h"
#include "components/VideoEncoderHEVC.h"

using namespace std::chrono_literals;

#define LOG_DEBUG(...) amf::log(0, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) amf::log(1, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) amf::log(2, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) amf::log(3, __FILE__, __LINE__, __VA_ARGS__)

#pragma warning(push)
#pragma warning(disable : 4244)

static HMODULE get_lib(const char* lib) {
    HMODULE mod = GetModuleHandleA(lib);
    if (mod) {
        return mod;
    }
    return LoadLibraryA(lib);
}

static int64_t cur_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static Microsoft::WRL::ComPtr<IDXGIAdapter>
findAdapter(Microsoft::WRL::ComPtr<IDXGIFactory2> factory, uint64_t target_luid) {
    DXGI_ADAPTER_DESC desc;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> target_adapter;
    for (uint32_t i = 0;; i++) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        auto hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        hr = adapter->GetDesc(&desc);
        uint64_t luid =
            (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) + desc.AdapterLuid.LowPart;
        LOG_INFO("Enum Adapter[%u,%" PRIu64 "]: %S", i, luid, desc.Description);
        if (target_luid == 0 || (luid > 0 && luid == target_luid)) {
            if (desc.VendorId == AMD_VENDOR_ID) {
                LOG_INFO("Find AMD adapter on %u", i);
                if (target_adapter == nullptr) {
                    target_adapter = adapter;
                    LOG_INFO("Choose adapter %" PRIu64 "", luid);
                }
            }
        }
    }
    return target_adapter;
}

AmfEncoder ::~AmfEncoder() {
    uninit();
}

void AmfEncoder::uninit() {
    if (amf_encoder_) {
        size_t try_times = 0;
        while (try_times++ < 1000) {
            auto res = amf_encoder_->Drain();
            if (res != AMF_INPUT_FULL) {
                break;
            }
            std::this_thread::sleep_for(1ms);
            LOG_INFO("Drain input queue");
        }
        amf_encoder_->Terminate();
        amf_encoder_ = nullptr;
    }
    if (amf_context_) {
        amf_context_->Terminate();
        amf_context_ = nullptr;
    }
    active_textures_.clear();
    available_textures_.clear();

    temp_texture_ = nullptr;
    ZeroMemory(&temp_texture_desc_, sizeof(temp_texture_desc_));

    nv12_convertor_ = nullptr;
    d3d11_dev_ = nullptr;
    d3d11_ctx_ = nullptr;
    help_ctx_.reset();
    luid_ = 0;
    input_format_ = InputFormat::UNKNOWN;
    LOG_INFO("%s", __FUNCTION__);
}

bool AmfEncoder::initD3d11(uint64_t lluid) {
    typedef HRESULT(WINAPI * CREATEDXGIFACTORY1PROC)(REFIID, void**);
    HMODULE dxgi = get_lib("DXGI.dll");
    HMODULE d3d11 = get_lib("D3D11.dll");
    if (!dxgi || !d3d11) {
        LOG_ERROR("Failed to load d3d11|dxgi related library");
        return false;
    }
    CREATEDXGIFACTORY1PROC create_dxgi =
        (CREATEDXGIFACTORY1PROC)GetProcAddress(dxgi, "CreateDXGIFactory1");
    PFN_D3D11_CREATE_DEVICE create_device =
        (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11, "D3D11CreateDevice");
    if (!create_dxgi || !create_device) {
        LOG_ERROR("Failed to load CreateDXGIFactory1|D3D11CreateDevice");
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    auto hr = create_dxgi(__uuidof(IDXGIFactory2), (void**)&factory);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create dxgi factory, hr:%u", hr);
        return false;
    }
    auto adapter = findAdapter(factory, lluid);
    if (!adapter) {
        LOG_ERROR("Failed to find AMD video card on adapter %" PRIu64 "", lluid);
        return false;
    }
    UINT flag = 0;
#ifdef _DEBUG
    flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = create_device(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag, nullptr, 0,
                       D3D11_SDK_VERSION, &d3d11_dev_, nullptr, &d3d11_ctx_);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to call D3D11CreateDevicem, hr:%u", hr);
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D10Multithread> multi_thread = NULL;
    d3d11_dev_->QueryInterface(__uuidof(ID3D10Multithread), (void**)&multi_thread);
    if (multi_thread) {
        multi_thread->SetMultithreadProtected(true);
        multi_thread = nullptr;
    }

    DXGI_ADAPTER_DESC desc;
    hr = adapter->GetDesc(&desc);
    luid_ = (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) + desc.AdapterLuid.LowPart;
    LOG_INFO("D3d11 initialized on adapter %" PRIu64 "", luid_);
    ZeroMemory(&temp_texture_desc_, sizeof(temp_texture_desc_));
    return true;
}

bool AmfEncoder::initCodec(const Config& config) {
    auto res = amf::AmfModuleWrapper::instance()->factory->CreateContext(&amf_context_);
    if (res != AMF_OK || !amf_context_) {
        LOG_ERROR("Failed to call CreateContext, res:%d", res);
        return false;
    }
    assert(d3d11_dev_);
    res = amf_context_->InitDX11(d3d11_dev_.Get(), amf::AMF_DX11_1);
    if (res != AMF_OK) {
        LOG_ERROR("Failed to call InitDX11, res:%d", res);
        return false;
    }
    const wchar_t* codec = AMFVideoEncoderVCE_AVC;
    LOG_INFO("Try to create amf encoder with %S", codec);
    res = amf::AmfModuleWrapper::instance()->factory->CreateComponent(amf_context_, codec,
                                                                      &amf_encoder_);
    if (res != AMF_OK) {
        LOG_ERROR("Failed to call CreateComponent, codec:%S, res:%d", codec, res);
        return false;
    }

    help_ctx_.scenario = amf::AmfContext::Scenario::SCREEN_SHARED_DOCUMENT;
    if (std::wstring(codec) == std::wstring(AMFVideoEncoderVCE_AVC)) {
        if (!applyH264Parameters(config)) {
            LOG_ERROR("Failed to apply avc paraeters");
            return false;
        }
        auto amf_format = amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12;
        help_ctx_.format = amf_format;
        res = amf_encoder_->Init(help_ctx_.format, help_ctx_.width, help_ctx_.height);
        if (res != AMF_OK) {
            LOG_ERROR("Failed to call AMFComponent::Init, ret:%d", res);
            return false;
        }
        set_avc_property(amf_encoder_, FRAMERATE, AMFConstructRate(help_ctx_.frame_rate, 1));
    }
    else if (std::wstring(codec) == std::wstring(AMFVideoEncoder_HEVC)) {
        if (!applyH265Parameters(config)) {
            LOG_ERROR("Failed to apply hevc paraeters");
            return false;
        }
        help_ctx_.format = amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12;
        res = amf_encoder_->Init(help_ctx_.format, help_ctx_.width, help_ctx_.height);
        if (res != AMF_OK) {
            LOG_ERROR("Failed to call AMFComponent::Init, ret:%d", res);
            return false;
        }
        set_hevc_property(amf_encoder_, FRAMERATE, AMFConstructRate(help_ctx_.frame_rate, 1));
        if (help_ctx_.scenario == amf::AmfContext::Scenario::SCREEN_SHARED_DOCUMENT) {
            const uint32_t min_qp_i = config.qp_min;
            const uint32_t max_qp_i = (config.qp_min + config.qp_max) / 2;
            set_hevc_property(amf_encoder_, MIN_QP_I, min_qp_i);
            set_hevc_property(amf_encoder_, MAX_QP_I, max_qp_i);
            LOG_INFO("Limit hevc QP_I to [%u,%u] for screen share document scenario", min_qp_i,
                     max_qp_i);
        }
    }
    else {
        LOG_ERROR("Can not support %S", codec);
        return false;
    }
    config_ = config;
    LOG_INFO("AMF encoder initialized, settings: %s", help_ctx_.to_str().c_str());
    return true;
}

// VideoEncodeAccelerator implementation.
bool AmfEncoder::Initialize(const Config& config) {
    LOG_INFO("%s", __FUNCTION__);
    if (!amf::AmfModuleWrapper::instance()) {
        LOG_ERROR("Failed to get amf module");
        return false;
    }
    if (!initD3d11(luid_)) {
        LOG_ERROR("Failed to initialize d3d11");
        return false;
    }
    if (!initCodec(config)) {
        LOG_ERROR("Failed to initialize Codec");
        return false;
    }
    return true;
}

bool AmfEncoder::resetDevice(uint64_t luid) {
    uninit();
    if (!initD3d11(luid)) {
        LOG_ERROR("Failed to initialize d3d11");
        return false;
    }
    if (!initCodec(config_)) {
        LOG_ERROR("Failed to initialize Codec");
        return false;
    }
    LOG_INFO("Reset device on %" PRIu64 " successfully", luid);
    return true;
}

bool AmfEncoder::applyH264Parameters(const Config& config) {
    assert(amf_encoder_);
    uint32_t width = config.width;
    uint32_t height = config.height;
    int64_t frame_rate = config.framerate;
    auto rc = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
    set_avc_property(amf_encoder_, USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
    set_avc_property(amf_encoder_, RATE_CONTROL_METHOD, rc);
    set_avc_property(amf_encoder_, FRAMESIZE, AMFConstructSize(width, height));
    set_avc_property(amf_encoder_, B_PIC_PATTERN, 0);
    set_avc_property(amf_encoder_, LOWLATENCY_MODE, true);
    set_avc_property(amf_encoder_, MIN_QP, config.qp_min);
    set_avc_property(amf_encoder_, MAX_QP, config.qp_max);
    set_avc_property(amf_encoder_, ENFORCE_HRD, true);
    set_avc_property(amf_encoder_, IDR_PERIOD, config.framerate);
    set_avc_property(amf_encoder_, QUERY_TIMEOUT, 200);
    set_avc_property(amf_encoder_, OUTPUT_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);
    set_avc_property(amf_encoder_, OUTPUT_TRANSFER_CHARACTERISTIC,
                     AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709);
    set_avc_property(amf_encoder_, OUTPUT_COLOR_PRIMARIES, AMF_COLOR_PRIMARIES_BT709);
    set_avc_property(amf_encoder_, FULL_RANGE_COLOR, false);
    set_avc_property(amf_encoder_, CABAC_ENABLE, AMF_VIDEO_ENCODER_UNDEFINED);
    if (rc != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        set_avc_property(amf_encoder_, ENABLE_VBAQ, true);
    }
    uint32_t bitrate = config.bitrate_kbps * 1000;
    set_avc_property(amf_encoder_, TARGET_BITRATE, bitrate);
    set_avc_property(amf_encoder_, PEAK_BITRATE, bitrate * 1.2);
    // set_avc_property(amf_encoder_, VBV_BUFFER_SIZE, 0);
    set_avc_property(amf_encoder_, PROFILE, AMF_VIDEO_ENCODER_PROFILE_MAIN);
    // set_avc_property(amf_encoder_, PREENCODE_ENABLE, true);
    help_ctx_.rc = rc;
    help_ctx_.frame_rate = frame_rate;
    help_ctx_.target_fps = frame_rate;
    help_ctx_.width = width;
    help_ctx_.height = height;
    help_ctx_.codec = amf::amf_codec_type::AVC;
    help_ctx_.current_bitrate = bitrate;
    help_ctx_.target_bitrate = bitrate;
    help_ctx_.min_qp = config.qp_min;
    help_ctx_.max_qp = config.qp_max;
    return true;
}

bool AmfEncoder::applyH265Parameters(const Config& config) {
    uint32_t width = config.width;
    uint32_t height = config.height;
    int64_t frame_rate = config.framerate;
    auto rc = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
    set_hevc_property(amf_encoder_, RATE_CONTROL_METHOD, rc);
    set_hevc_property(amf_encoder_, FRAMESIZE, AMFConstructSize(width, height));
    set_hevc_property(amf_encoder_, ENFORCE_HRD, false);
    // set_hevc_property(amf_encoder_, GOP_SIZE, config.gop_length.value_or(600));
    set_hevc_property(amf_encoder_, QUERY_TIMEOUT, 200);
    set_hevc_property(amf_encoder_, LOWLATENCY_MODE, true);
    set_hevc_property(amf_encoder_, MIN_QP_P, config.qp_min);
    set_hevc_property(amf_encoder_, MAX_QP_P, config.qp_max);
    set_hevc_property(amf_encoder_, PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);

    set_hevc_property(amf_encoder_, COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_8);
    set_hevc_property(amf_encoder_, OUTPUT_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);
    set_hevc_property(amf_encoder_, OUTPUT_TRANSFER_CHARACTERISTIC,
                      AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709);
    set_hevc_property(amf_encoder_, OUTPUT_COLOR_PRIMARIES, AMF_COLOR_PRIMARIES_BT709);
    set_hevc_property(amf_encoder_, NOMINAL_RANGE, false);
    if (rc != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP) {
        set_hevc_property(amf_encoder_, ENABLE_VBAQ, true);
    }
    set_hevc_property(amf_encoder_, HIGH_MOTION_QUALITY_BOOST_ENABLE, false);
    uint32_t bitrate = config.bitrate_kbps * 1000;
    if (rc != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP &&
        rc != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR) {
        set_hevc_property(amf_encoder_, TARGET_BITRATE, bitrate);
        set_hevc_property(amf_encoder_, PEAK_BITRATE, bitrate);
        // set_hevc_property(amf_encoder_, VBV_BUFFER_SIZE, bitrate);
    }
    help_ctx_.rc = rc;
    help_ctx_.frame_rate = config.framerate;
    help_ctx_.target_fps = config.framerate;
    help_ctx_.width = width;
    help_ctx_.height = height;
    help_ctx_.codec = amf::amf_codec_type::HEVC;
    help_ctx_.current_bitrate = bitrate;
    help_ctx_.target_bitrate = bitrate;
    help_ctx_.min_qp = config.qp_min;
    help_ctx_.max_qp = config.qp_max;
    return true;
}

bool AmfEncoder::applyAV1Parameters(const Config& config) {
    return false;
}

void AMF_STD_CALL AmfEncoder::OnSurfaceDataRelease(amf::AMFSurface* pSurface) {
    std::lock_guard<std::mutex> lock(texture_mtx_);
    auto it = active_textures_.find(pSurface);
    if (it != active_textures_.end()) {
        available_textures_.push_back(it->second);
        active_textures_.erase(it);
    }
}

Microsoft::WRL::ComPtr<ID3D11Texture2D>
AmfEncoder::getAvailableTexture(const D3D11_TEXTURE2D_DESC& desc_src) {
    std::lock_guard<std::mutex> lock(texture_mtx_);
    while (!available_textures_.empty()) {
        auto item = available_textures_.back();
        D3D11_TEXTURE2D_DESC desc;
        item->GetDesc(&desc);
        if (desc.Format == desc_src.Format && desc.Width == desc_src.Width &&
            desc.Height == desc_src.Height) {
            break;
        }
        available_textures_.pop_back();
        continue;
    }
    if (available_textures_.empty()) {
        D3D11_TEXTURE2D_DESC desc = desc_src;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        auto hr = d3d11_dev_->CreateTexture2D(&desc, nullptr, &texture);
        if (hr != S_OK) {
            LOG_ERROR("Failed to create texture, hr:%u", hr);
            return nullptr;
        }
        available_textures_.push_back(texture);
    }
    assert(!available_textures_.empty());
    auto output = available_textures_.back();
    available_textures_.pop_back();
    return output;
}

static void PrintRecording(amf::AmfEncoderDebuger* recorder) {
    auto now = cur_time();
    std::vector<amf::AmfEncoderDebuger::Statistics> outputs;
    if (!recorder->stat(outputs, now, 1000 * 5)) {
        return;
    }
    std::string logs = "[";
    for (auto& output : outputs) {
        logs += output.to_str();
    }
    logs += "]";
    LOG_INFO("%s", logs.c_str());
}

Microsoft::WRL::ComPtr<ID3D11Texture2D>
AmfEncoder::copyFrameToTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height) {
    if (!temp_texture_ || temp_texture_desc_.Width != width ||
        temp_texture_desc_.Height != height ||
        temp_texture_desc_.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE) {
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_NV12;
        desc.ArraySize = 1;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.MipLevels = 1;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.Usage = D3D11_USAGE_STAGING;
        auto hr = d3d11_dev_->CreateTexture2D(&desc, NULL, &temp_texture_);
        if (FAILED(hr)) {
            return nullptr;
        }
        temp_texture_->GetDesc(&temp_texture_desc_);
    }
    if (input_format_ != InputFormat::I420) {
        LOG_INFO("Input format is I420 Pixel");
        input_format_ = InputFormat::I420;
    }
    auto gpu_texture = getAvailableTexture(temp_texture_desc_);
    if (!gpu_texture) {
        LOG_WARN("All textures in busy, drop frame");
        return nullptr;
    }
    D3D11_MAPPED_SUBRESOURCE resource;
    auto hr = d3d11_ctx_->Map(temp_texture_.Get(), 0, D3D11_MAP_WRITE, 0, &resource);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map nv12 texture, hr:%u", hr);
        return nullptr;
    }
    for (uint32_t i = 0; i < height * 3 / 2; i++) {
        memcpy((uint8_t*)resource.pData + i * resource.RowPitch, (uint8_t*)data.data() + i * width,
               width);
    }
    d3d11_ctx_->Unmap(temp_texture_.Get(), 0);
    d3d11_ctx_->CopyResource(gpu_texture.Get(), temp_texture_.Get());
    return gpu_texture;
}

int32_t AmfEncoder::EncodeFrame(const std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                                bool force_key) {
    auto texture = copyFrameToTexture(data, width, height);
    if (!texture) {
        return -1;
    }
    amf::AMFSurfacePtr amf_surf;
    auto res = amf_context_->CreateSurfaceFromDX11Native(texture.Get(), &amf_surf, this);
    if (res != AMF_OK) {
        LOG_ERROR("CreateSurfaceFromDX11Native failed, res:%d", res);
        return -1;
    }
    if (force_key) {
        LOG_INFO("Request key frame");
        if (!triggleKeyFrame(amf_surf)) {
            LOG_INFO("Requeset IDR failed, res:%u", res);
            return -1;
        }
    }
    {
        std::lock_guard<std::mutex> lock(texture_mtx_);
        active_textures_[amf_surf.GetPtr()] = texture;
    }
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_STATISTICS_FEEDBACK, true);
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_INSERT_AUD, false);
    }
    else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_HEVC_STATISTICS_FEEDBACK, true);
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, false);
    }
    auto ts_start = cur_time();
    while (true) {
        res = amf_encoder_->SubmitInput(amf_surf);
        if (res == AMF_INPUT_FULL) {
            std::this_thread::sleep_for(1ms);
            constexpr uint64_t kEncodeTimeout = 5 * 1000 * 1000;
            if (cur_time() - ts_start < kEncodeTimeout) {
                continue;
            }
            LOG_ERROR("%s Timeout", __FUNCTION__);
            return -1;
        }
        if (res == AMF_OK || res == AMF_NEED_MORE_INPUT) {
            break;
        }
        LOG_ERROR("Failed to call SubmitInputm, res:%d", res);
        return -1;
    }
    input_output_recorder_.addInput(help_ctx_.frame_rate, cur_time());
    encoded_pkt_ = nullptr;
    res = amf_encoder_->QueryOutput(&encoded_pkt_);
    if (res != AMF_REPEAT && res != AMF_OK) {
        LOG_ERROR("QueryOutput failed, res:%d", res);
        return -1;
    }
    else if (res != AMF_OK) {
        LOG_INFO("AMF res != AMF_OK, value:%u", res);
        if (res == AMF_REPEAT) {
            return -1;
        }
        res = amf_encoder_->QueryOutput(&encoded_pkt_);
        if (res != AMF_OK) {
            LOG_ERROR("Amf encoder maybe blocked, fallback");
            return -1;
        }
    }
    if (encoded_pkt_ && !onImageEncoded(encoded_pkt_)) {
        return -1;
    }
    // Print Statistics
    PrintRecording(&input_output_recorder_);
    return 0;
}

bool AmfEncoder::triggleKeyFrame(amf::AMFSurfacePtr& amf_surf) {
    AMF_RESULT res = AMF_FAIL;
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        res = amf_surf->SetProperty(
            AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE,
            AMF_VIDEO_ENCODER_PICTURE_TYPE_ENUM::AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR);
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_INSERT_SPS, true);
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_INSERT_PPS, true);
        // Limit Keyframe QP range to avoid 'Vague'
        LimitQPForScc();
    }
    else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
        res = amf_surf->SetProperty(
            AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE,
            AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_ENUM::AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR);
        amf_surf->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER, true);
    }
    else if (help_ctx_.codec == amf::amf_codec_type::AV1) {
        res = amf_surf->SetProperty(AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE,
                                    AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_KEY);
    }
    if (res != AMF_OK) {
        return false;
    }
    return true;
}

bool isKeyFrame(amf::amf_codec_type codec, uint64_t type) {
    if (codec == amf::amf_codec_type::AVC || codec == amf::amf_codec_type::HEVC) {
        switch (type) {
        case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
            return true;
        case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool AmfEncoder::onImageEncoded(amf::AMFDataPtr& pkt) {
    if (!pkt) {
        return false;
    }
    uint64_t frame_type = 0;
    auto res = pkt->GetProperty(get_amf_output_type(help_ctx_.codec), &frame_type);
    if (res != AMF_OK) {
        LOG_ERROR("Failed to get encoded image type,res:%d", res);
        return false;
    }
    uint64_t average_qp = 0;
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        res = pkt->GetProperty(AMF_VIDEO_ENCODER_STATISTIC_AVERAGE_QP, &average_qp);
    }
    else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
        res = pkt->GetProperty(AMF_VIDEO_ENCODER_HEVC_STATISTIC_AVERAGE_QP, &average_qp);
    }
    if (res != AMF_OK) {
        average_qp = 0;
    }
    bool key_frame = isKeyFrame(help_ctx_.codec, frame_type);
    if (key_frame) {
        RecoverQPRange();
    }
    size_t length = ((amf::AMFBufferPtr)pkt)->GetSize();
    LOG_INFO("Frame %u, %s, QP: %u, size: %u B, Target:%u kbps, %u B, %u FPS",
             help_ctx_.encoded_count++, (key_frame ? "I" : "P"), average_qp, length,
             help_ctx_.current_bitrate / 1000, help_ctx_.current_bitrate / help_ctx_.frame_rate / 8,
             help_ctx_.frame_rate);
    // record qp and actual bitrate
    input_output_recorder_.addOuput(length, average_qp, help_ctx_.current_bitrate, cur_time());
#if 0
    {
        static std::string dll_path;
        if (dll_path.empty()) {
            char buffer[1024] = {0};
            GetModuleFileNameA(nullptr, buffer, 1024);
            char* slash = strrchr(buffer, '\\');
            if (slash) {
                slash[1] = 0;
                dll_path = std::string(buffer);
            }
        }
        std::FILE* file = nullptr;
        if (help_ctx_.codec == amf::amf_codec_type::AVC) {
            std::string file_name = dll_path + std::to_string(help_ctx_.width) + "x" +
                                    std::to_string(help_ctx_.height) + "-encoded.h264 ";
            file = std::fopen(file_name.c_str(), "ab");
        }
        else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
            std::string file_name = dll_path + std::to_string(help_ctx_.width) + "x" +
                                    std::to_string(help_ctx_.height) + "encoded.h265";
            file = std::fopen(file_name.c_str(), "ab");
        }
        uint8_t* data = (uint8_t*)((amf::AMFBufferPtr)pkt)->GetNative();
        std::fwrite(data, length, 1, file);
        std::fclose(file);
    }
#endif
    return true;
}

bool AmfEncoder::isTimeToChangeTargetFps(int64_t at_time) {
    return true;
}

bool AmfEncoder::isTimeToChangeTargetBitrate(int64_t at_time) {
    return true;
}

int32_t AmfEncoder::RequestEncodingParametersChange(uint32_t bitrate, uint32_t frame_rate) {
    if (!amf_encoder_) {
        return -1;
    }
    if (help_ctx_.target_fps == frame_rate && help_ctx_.target_bitrate == bitrate) {
        return 0;
    }
    LOG_INFO("Request encoder paramesters: %ukbps %uFPS", bitrate / 1000, frame_rate);
    help_ctx_.target_bitrate = bitrate;
    help_ctx_.target_fps = frame_rate;
    applyFrameRateAndBitrate();
    return 0;
}

void AmfEncoder::applyFrameRateAndBitrate() {
    auto at_time = cur_time();
    if (isTimeToChangeTargetFps(at_time)) {
        applyFramerate(at_time);
    }
    if (isTimeToChangeTargetBitrate(at_time)) {
        applyBitrate(at_time);
    }
}

void AmfEncoder::LimitQPForScc() {
    if (help_ctx_.scenario != amf::AmfContext::Scenario::SCREEN_SHARED_DOCUMENT) {
        return;
    }
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        const uint32_t min_qp = help_ctx_.min_qp;
        const uint32_t max_qp = (help_ctx_.min_qp + help_ctx_.max_qp) / 2;
        set_avc_property(amf_encoder_, MIN_QP, min_qp);
        set_avc_property(amf_encoder_, MAX_QP, max_qp);
        LOG_INFO("Limit QP to [%u, %u] for screen share document scenario", min_qp, max_qp);
        recover_qp_range_ = true;
    }
}

void AmfEncoder::RecoverQPRange() {
    if (!recover_qp_range_) {
        return;
    }
    recover_qp_range_ = false;
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        set_avc_property(amf_encoder_, MIN_QP, help_ctx_.min_qp);
        set_avc_property(amf_encoder_, MAX_QP, help_ctx_.max_qp);
        LOG_INFO("Recover QP to [%u, %u] for screen share document scenario", help_ctx_.min_qp,
                 help_ctx_.max_qp);
    }
}

void AmfEncoder::applyFramerate(int64_t at_time) {
    if (help_ctx_.frame_rate == help_ctx_.target_fps) {
        return;
    }
    LOG_INFO("Apply target fps[%u->%u]", (uint32_t)help_ctx_.frame_rate, help_ctx_.target_fps);
    help_ctx_.frame_rate = help_ctx_.target_fps;
    help_ctx_.last_target_fps_changed_time = at_time;
    if (help_ctx_.codec == amf::amf_codec_type::AVC) {
        set_avc_property(amf_encoder_, FRAMERATE, AMFConstructRate(help_ctx_.frame_rate, 1));
    }
    else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
        set_hevc_property(amf_encoder_, FRAMERATE, AMFConstructRate(help_ctx_.frame_rate, 1));
    }
    else if (help_ctx_.codec == amf::amf_codec_type::AV1) {
        set_av1_property(amf_encoder_, FRAMERATE, AMFConstructRate(help_ctx_.frame_rate, 1));
    }
    LimitQPForScc();
    applyBitrate(at_time);
}

void AmfEncoder::applyBitrate(int64_t at_time) {
    float scale = std::min(help_ctx_.frame_rate * 1.0 / help_ctx_.target_fps, 1.0);
    uint32_t target_bitrate = static_cast<uint32_t>(help_ctx_.target_bitrate * scale);
    const uint32_t kMinBitrate = 50 * 1000;
    target_bitrate = std::max<uint32_t>(kMinBitrate, target_bitrate);
    if (help_ctx_.current_bitrate == target_bitrate) {
        return;
    }
    if (help_ctx_.rc != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP &&
        help_ctx_.rc != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR) {
        if (help_ctx_.codec == amf::amf_codec_type::AVC) {
            set_avc_property(amf_encoder_, TARGET_BITRATE, target_bitrate);
            set_avc_property(amf_encoder_, PEAK_BITRATE, target_bitrate * 1.2);
            // set_avc_property(amf_encoder_, VBV_BUFFER_SIZE, 0);
        }
        else if (help_ctx_.codec == amf::amf_codec_type::HEVC) {
            set_hevc_property(amf_encoder_, TARGET_BITRATE, target_bitrate);
            set_hevc_property(amf_encoder_, PEAK_BITRATE, target_bitrate);
            // set_hevc_property(amf_encoder_, VBV_BUFFER_SIZE, target_bitrate);
        }
        else if (help_ctx_.codec == amf::amf_codec_type::AV1) {
            set_av1_property(amf_encoder_, TARGET_BITRATE, target_bitrate);
            set_av1_property(amf_encoder_, PEAK_BITRATE, target_bitrate);
            // set_av1_property(amf_encoder_, VBV_BUFFER_SIZE, target_bitrate);
        }
        LOG_INFO("Apply target bitrate[%u->%u kbps], request:%ukbps",
                 (uint32_t)help_ctx_.current_bitrate / 1000, target_bitrate / 1000,
                 help_ctx_.target_bitrate / 1000);
        help_ctx_.current_bitrate = target_bitrate;
        help_ctx_.last_target_bitrate_changed_time = at_time;
    }
}

#pragma warning(pop)
