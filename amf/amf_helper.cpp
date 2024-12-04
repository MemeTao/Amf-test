#include <algorithm>
#include <cinttypes>
#include <iostream>

#include "amf_helper.h"

#pragma warning(push)
#pragma warning(disable : 4244)

namespace amf {

#define LOG_DEBUG(...) amf::log(0, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) amf::log(1, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) amf::log(2, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) amf::log(3, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_FILE "amf-test.log"

class Timestamp {
    static int64_t kMicroSecondsPerSecond;
    static int64_t kMicroSecondsPerDay;

public:
    enum class Type : uint8_t {
        kSinceEpoch = 1,
        kSincePowerup,
    };

    Timestamp(int64_t time_ = 0)
        : microseconds_(time_) {}

    inline int64_t microseconds() const { return microseconds_; }

    // 20220114 21:01:04:123456
    std::string to_str(bool show_year = true, bool show_microseconds = true) const;

    static Timestamp now(Type since_power_up = Type::kSincePowerup);

private:
    int64_t microseconds_ = 0;
};
int64_t Timestamp::kMicroSecondsPerSecond = 1000 * 1000;
int64_t Timestamp::kMicroSecondsPerDay = kMicroSecondsPerSecond * 24 * 60 * 60;

Timestamp Timestamp::now(Timestamp::Type t) {
    if (t == Type::kSincePowerup) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    return Timestamp((static_cast<int64_t>(8) * 3600) * kMicroSecondsPerSecond +
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count());
}

std::string Timestamp::to_str(bool show_year, bool show_microseconds) const {
    char buf[32] = {0};
    time_t seconds = static_cast<time_t>(microseconds_ / kMicroSecondsPerSecond);
    struct tm tm_time;
    gmtime_s(&tm_time, &seconds);
    if (show_microseconds) {
        int microseconds = static_cast<int>(microseconds_ % kMicroSecondsPerSecond);
        if (show_year)
            snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d", tm_time.tm_year + 1900,
                     tm_time.tm_mon + 1, tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min,
                     tm_time.tm_sec, microseconds);
        else
            snprintf(buf, sizeof(buf), "%02d%02d %02d:%02d:%02d.%06d", tm_time.tm_mon + 1,
                     tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
                     microseconds);
    }
    else {
        if (show_year)
            snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d", tm_time.tm_year + 1900,
                     tm_time.tm_mon + 1, tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min,
                     tm_time.tm_sec);
        else
            snprintf(buf, sizeof(buf), "%02d%02d %02d:%02d:%02d", tm_time.tm_mon + 1,
                     tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    }
    return buf;
}

static const char* fileNameFromPath(const char* file) {
    const char* end1 = ::strrchr(file, '/');
    const char* end2 = ::strrchr(file, '\\');
    if (!end1 && !end2)
        return file;
    else
        return (end1 > end2) ? end1 + 1 : end2 + 1;
}

void log(int type, const char* file, int line, const char* format, ...) {
    thread_local uint64_t thread_id = GetCurrentThreadId();
    auto cur_time = Timestamp::now(Timestamp::Type::kSinceEpoch);
    static const char* log_types[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
    va_list args;
    va_start(args, format);
    const int buffer_lenth = 1024;
    thread_local char buffer[buffer_lenth] = {0};
    int wrote = snprintf(buffer, buffer_lenth,
                         "[%s][%s][%" PRIu64 "](%s:%d): ", log_types[static_cast<uint8_t>(type)],
                         cur_time.to_str().c_str(), thread_id, fileNameFromPath(file), line);
    wrote = std::min<int>(wrote, buffer_lenth);
    wrote += vsnprintf(buffer + wrote, buffer_lenth - wrote, format, args);
    wrote = std::min<int>(wrote, buffer_lenth - 2);
    buffer[wrote++] = '\n';
    buffer[wrote] = '\0';
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
    std::FILE* pfile = std::fopen((dll_path + LOG_FILE).c_str(), "a");
    std::fwrite(buffer, wrote, 1, pfile);
    std::fclose(pfile);
    va_end(args);
}

NV12Convertor ::~NV12Convertor() {
    uninit();
}

void NV12Convertor::uninit() {
    if (convert_) {
        convert_->Cleanup();
        convert_ = nullptr;
    }
}

bool NV12Convertor::init(Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device, uint32_t width,
                         uint32_t height) {
    uninit();
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    d3d11_device->GetImmediateContext(&context);
    convert_ = std::make_unique<D3D11VideoProcessorConvert>(d3d11_device.Get(), context.Get());
    auto hr = convert_->Init();
    if (FAILED(hr)) {
        LOG_ERROR("Failed to initialize D3D11VideoProcessorConvert, hr:%u", hr);
        return false;
    }
    return true;
}

bool NV12Convertor::convert(Microsoft::WRL::ComPtr<ID3D11Texture2D> input,
                            Microsoft::WRL::ComPtr<ID3D11Texture2D>& output) {
    if (!input) {
        return false;
    }
    auto hr = convert_->Convert(input.Get(), output.Get());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to call D3D11VideoProcessorConvert::Convert, hr:%u", hr);
        return false;
    }
    return true;
}

static Microsoft::WRL::ComPtr<IDXGIAdapter>
findAmdAdapter(Microsoft::WRL::ComPtr<IDXGIFactory2> factory, uint64_t target_luid) {
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

static HMODULE get_lib(const char* lib) {
    HMODULE mod = GetModuleHandleA(lib);
    if (mod) {
        return mod;
    }
    return LoadLibraryA(lib);
}

bool InitAmfContextWithD3d11(amf::AMFContextPtr amf_context) {
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
    auto adapter = findAmdAdapter(factory, 0);
    if (!adapter) {
        LOG_INFO("No AMD video card exists on this PC");
        return false;
    }
    UINT flag = 0;
#ifdef _DEBUG
    flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_ctx;
    hr = create_device(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag, nullptr, 0,
                       D3D11_SDK_VERSION, &d3d11_dev, nullptr, &d3d11_ctx);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to call D3D11CreateDevice, hr:%u", hr);
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D10Multithread> multi_thread = NULL;
    d3d11_dev->QueryInterface(__uuidof(ID3D10Multithread), (void**)&multi_thread);
    if (multi_thread) {
        multi_thread->SetMultithreadProtected(true);
        multi_thread = nullptr;
    }
    auto res = amf_context->InitDX11(d3d11_dev.Get());
    if (res != AMF_OK) {
        LOG_ERROR("Initialize amf context with d3d11 device failed, res:%u", res);
        return false;
    }
    return true;
}

bool AmfModule::doInit() {
    auto amf_module = LoadLibraryExW(AMF_DLL_NAME, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!amf_module) {
        LOG_INFO("Can not find %S", AMF_DLL_NAME);
        return false;
    }
    FreeLibrary(amf_module);
    amf_module = LoadLibraryW(AMF_DLL_NAME);
    if (!amf_module) {
        LOG_WARN("Failed to load %S", AMF_DLL_NAME);
        return false;
    }
    AMFInit_Fn init_func = (AMFInit_Fn)GetProcAddress(amf_module, AMF_INIT_FUNCTION_NAME);
    if (!init_func) {
        LOG_WARN("Failed to get AMFInit address");
        return false;
    }
    auto res = init_func(AMF_FULL_VERSION, &factory);
    if (res != AMF_OK) {
        LOG_ERROR("AMFInit failed %d", static_cast<int>(res));
        return false;
    }

    res = factory->GetTrace(&amf_trace);
    if (res != AMF_OK) {
        LOG_WARN("GetTrace failed %d", static_cast<int>(res));
        return false;
    }
    AMFQueryVersion_Fn get_ver_func =
        (AMFQueryVersion_Fn)GetProcAddress(amf_module, AMF_QUERY_VERSION_FUNCTION_NAME);
    if (!get_ver_func) {
        LOG_WARN("Failed to get AMFQueryVersion address");
        return false;
    }
    res = get_ver_func(&amf_version);
    if (res != AMF_OK) {
        LOG_WARN("AMFQueryVersion failed %d", static_cast<int>(res));
        return false;
    }
    LOG_INFO("Version %I64X", amf_version);
    res = factory->GetDebug(&amf_debug);
    if (res == AMF_OK) {
        amf_debug->AssertsEnable(false);
    }

#ifndef _NDEBUG
    amf_int32 traceLevel = AMF_TRACE_TRACE;
    amf_trace->SetGlobalLevel(traceLevel);

    amf_trace->EnableWriter(AMF_TRACE_WRITER_CONSOLE, false);

    amf_trace->EnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);
    amf_trace->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, traceLevel);

    amf_trace->EnableWriter(AMF_TRACE_WRITER_FILE, true);
    amf_trace->SetWriterLevel(AMF_TRACE_WRITER_FILE, traceLevel);
#endif
    amf::AMFContextPtr amf_context;
    if (factory->CreateContext(&amf_context) != AMF_OK || !InitAmfContextWithD3d11(amf_context)) {
        LOG_ERROR("Failed to initialize amf context");
        return false;
    }

    QueryEncoderForCodecAVC(amf_context);
    QueryEncoderForCodecHEVC(amf_context);
    QueryEncoderForCodecAV1(amf_context);
    QueryDecoderForCodec(amf_codec_type::AVC, amf_context);
    QueryDecoderForCodec(amf_codec_type::HEVC, amf_context);
    QueryDecoderForCodec(amf_codec_type::AV1, amf_context);
    amf_context->Terminate();
    return true;
}

bool AmfModule::init() {
    bool ret = false;
    __try {
        ret = doInit();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        encoder_capbilities_.clear();
        decoder_capbilities_.clear();
        ret = false;
        LOG_ERROR("Amf exception detected");
    }
    return ret;
}

void AmfModule::uninit() {
    __try {
        uninit();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Amf exception detected, ignore");
    }
}

void AmfModule::doUninit() {
    // no need to unload module
    if (amf_trace) {
        // amf_trace->TraceFlush();
        amf_trace = nullptr;
    }
    amf_debug = nullptr;
    factory = nullptr;
    if (amf_module) {
        FreeLibrary(amf_module);
        amf_module = NULL;
    }
}

std::string AccelTypeToString(amf::AMF_ACCELERATION_TYPE accelType) {
    std::string strValue;
    switch (accelType) {
    case amf::AMF_ACCEL_NOT_SUPPORTED:
        strValue = "Not supported";
        break;
    case amf::AMF_ACCEL_HARDWARE:
        strValue = "Hardware-accelerated";
        break;
    case amf::AMF_ACCEL_GPU:
        strValue = "GPU-accelerated";
        break;
    case amf::AMF_ACCEL_SOFTWARE:
        strValue = "Not accelerated (software)";
        break;
    }
    return strValue;
}

static bool QueryIOCaps(const amf::AMFIOCapsPtr& ioCaps, AmfCodecCapbility& capbility) {
    if (ioCaps == NULL) {
        return false;
    }
    amf_int32 minWidth = 0;
    amf_int32 maxWidth = 0;
    amf_int32 minHeight = 0;
    amf_int32 maxHeight = 0;
    ioCaps->GetWidthRange(&minWidth, &maxWidth);
    ioCaps->GetHeightRange(&minHeight, &maxHeight);
    amf_int32 vertAlign = ioCaps->GetVertAlign();
    capbility.min_height = minHeight;
    capbility.min_width = minWidth;
    capbility.max_width = maxWidth;
    capbility.max_height = maxHeight;
    capbility.vertical_align = vertAlign;
    amf_int32 numOfFormats = ioCaps->GetNumOfFormats();
    for (amf_int32 i = 0; i < numOfFormats; i++) {
        amf::AMF_SURFACE_FORMAT format;
        amf_bool native = false;
        auto res = ioCaps->GetFormatAt(i, &format, &native);
        if (res != AMF_OK) {
            LOG_ERROR("Failed to call GetFormatAt %d, error: %d", i, static_cast<int>(res));
            return false;
        }
        capbility.input_formats.insert(format);
    }
    return true;
}

void AmfModule::QueryEncoderForCodecAVC(amf::AMFContextPtr amf_context) {
    LOG_INFO("Start query codec for avc encoder...");
    amf::AMFComponentPtr pEncoder;
    factory->CreateComponent(amf_context, AMFVideoEncoderVCE_AVC, &pEncoder);
    if (pEncoder == NULL) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf::AMFCapsPtr encoderCaps;
    if (pEncoder->GetCaps(&encoderCaps) != AMF_OK) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf_uint32 NumOfHWInstances = 1;
    encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
    LOG_INFO("Exists %u encoder instances", NumOfHWInstances);
    for (amf_uint32 i = 0; i < NumOfHWInstances; i++) {
        AmfCodecCapbility capbility;
        if (NumOfHWInstances > 1) {
            pEncoder->SetProperty(AMF_VIDEO_ENCODER_INSTANCE_INDEX, i);
        }
        LOG_INFO("Instance %u:", i);
        amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
        LOG_INFO("Acceleration Type: %s", AccelTypeToString(accelType).c_str());

        amf_uint32 maxProfile = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, &maxProfile);
        capbility.max_profile = maxProfile;
        LOG_INFO("Maximum profile: %u", capbility.max_profile);

        amf_uint32 maxLevel = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, &maxLevel);
        LOG_INFO("Maximum level: %u", maxLevel);

        amf_uint32 maxTemporalLayers = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_TEMPORAL_LAYERS, &maxTemporalLayers);
        capbility.max_temporal_layers = maxTemporalLayers;
        LOG_INFO("Number of temporal Layers: %u", capbility.max_temporal_layers);

        bool bBPictureSupported = false;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_BFRAMES, &bBPictureSupported);
        capbility.b_frame_support = bBPictureSupported;
        LOG_INFO("IsBPictureSupported: %d", capbility.b_frame_support);

        amf_uint32 maxNumOfStreams = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
        capbility.max_streams = maxNumOfStreams;
        LOG_INFO("Max Number of streams supported: %u", capbility.max_streams);
        amf::AMFIOCapsPtr inputCaps;
        if (encoderCaps->GetInputCaps(&inputCaps) != AMF_OK || !QueryIOCaps(inputCaps, capbility)) {
            return;
        }
        LOG_INFO("Dimension limit: [%u X %u -> %u X %u]", capbility.min_width, capbility.min_height,
                 capbility.max_width, capbility.max_height);
        std::wstring formats_str;
        for (auto format : capbility.input_formats) {
            formats_str +=
                std::wstring(L" ") + std::wstring(amf_trace->SurfaceGetFormatName(format));
        }
        LOG_INFO("Supported Formats: %S", formats_str.c_str());
        if (i == 0) {
            encoder_capbilities_.emplace(amf_codec_type::AVC, capbility);
        }
    }
}

void AmfModule::QueryEncoderForCodecHEVC(amf::AMFContextPtr amf_context) {
    LOG_INFO("Start query codec for hevc encoder...");
    amf::AMFComponentPtr pEncoder;
    factory->CreateComponent(amf_context, AMFVideoEncoder_HEVC, &pEncoder);
    if (pEncoder == NULL) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf::AMFCapsPtr encoderCaps;
    if (pEncoder->GetCaps(&encoderCaps) != AMF_OK) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf_uint32 NumOfHWInstances = 1;
    encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
    LOG_INFO("Exists %u encoder instances", NumOfHWInstances);
    for (amf_uint32 i = 0; i < NumOfHWInstances; i++) {
        AmfCodecCapbility capbility;
        if (NumOfHWInstances > 1) {
            pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSTANCE_INDEX, i);
        }
        LOG_INFO("Instance %u:", i);
        amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
        LOG_INFO("Acceleration Type: %s", AccelTypeToString(accelType).c_str());

        amf_uint32 maxProfile = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, &maxProfile);
        capbility.max_profile = maxProfile;
        LOG_INFO("Maximum profile: %u", capbility.max_profile);

        amf_uint32 maxTier = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_TIER, &maxTier);
        LOG_INFO("Maximum tier: %u", maxTier);

        amf_uint32 maxLevel = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_LEVEL, &maxLevel);
        LOG_INFO("Maximum level: %u", maxLevel);

        amf_uint32 maxNumOfStreams = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
        capbility.max_streams = maxNumOfStreams;
        LOG_INFO("Max Number of streams supported: %u", capbility.max_streams);
        amf::AMFIOCapsPtr inputCaps;
        if (encoderCaps->GetInputCaps(&inputCaps) != AMF_OK || !QueryIOCaps(inputCaps, capbility)) {
            return;
        }
        LOG_INFO("Dimension limit [%u X %u -> %u X %u]", capbility.min_width, capbility.min_height,
                 capbility.max_width, capbility.max_height);
        std::wstring formats_str;
        for (auto format : capbility.input_formats) {
            formats_str +=
                std::wstring(L" ") + std::wstring(amf_trace->SurfaceGetFormatName(format));
        }
        LOG_INFO("Supported Formats: %S", formats_str.c_str());
        if (i == 0) {
            encoder_capbilities_.emplace(amf_codec_type::HEVC, capbility);
        }
    }
}

void AmfModule::QueryEncoderForCodecAV1(amf::AMFContextPtr amf_context) {
    LOG_INFO("Start query codec for av1 encoder...");
    amf::AMFComponentPtr pEncoder;
    factory->CreateComponent(amf_context, AMFVideoEncoder_AV1, &pEncoder);
    if (pEncoder == NULL) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf::AMFCapsPtr encoderCaps;
    if (pEncoder->GetCaps(&encoderCaps) != AMF_OK) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf_uint32 NumOfHWInstances = 1;
    encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
    LOG_INFO("Exists %u encoder instances", NumOfHWInstances);
    for (amf_uint32 i = 0; i < NumOfHWInstances; i++) {
        AmfCodecCapbility capbility;
        if (NumOfHWInstances > 1) {
            pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX, i);
        }
        LOG_INFO("Instance %u:", i);
        amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
        LOG_INFO("Acceleration Type: %s", AccelTypeToString(accelType).c_str());

        amf_uint32 maxProfile = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_PROFILE, &maxProfile);
        capbility.max_profile = maxProfile;
        LOG_INFO("Maximum profile: %u", capbility.max_profile);

        amf_uint32 maxLevel = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_LEVEL, &maxLevel);
        LOG_INFO("Maximum level: %u", maxLevel);

        amf::AMFIOCapsPtr inputCaps;
        if (encoderCaps->GetInputCaps(&inputCaps) != AMF_OK || !QueryIOCaps(inputCaps, capbility)) {
            return;
        }
        LOG_INFO("Dimension limit [%u X %u -> %u X %u]", capbility.min_width, capbility.min_height,
                 capbility.max_width, capbility.max_height);
        std::wstring formats_str;
        for (auto format : capbility.input_formats) {
            formats_str +=
                std::wstring(L" ") + std::wstring(amf_trace->SurfaceGetFormatName(format));
        }
        LOG_INFO("Supported Formats: %S", formats_str.c_str());
        if (i == 0) {
            encoder_capbilities_.emplace(amf_codec_type::AV1, capbility);
        }
    }
}

// TDOO(tao.chen): 10bit hevc
void AmfModule::QueryDecoderForCodec(amf_codec_type codec, amf::AMFContextPtr context) {
    const wchar_t* codec_name = nullptr;
    switch (codec) {
    case amf_codec_type::AVC:
        codec_name = AMFVideoDecoderUVD_H264_AVC;
        break;
    case amf_codec_type::HEVC:
        codec_name = AMFVideoEncoder_HEVC;
        break;
    case amf_codec_type::AV1:
        codec_name = AMFVideoDecoderHW_AV1;
        break;
    default:
        codec_name = AMFVideoDecoderUVD_H264_AVC;
        break;
    }
    LOG_INFO("Start query codec for %S decoer...", codec_name);
    amf::AMFComponentPtr pDecoder;
    factory->CreateComponent(context, codec_name, &pDecoder);
    if (pDecoder == NULL) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf::AMFCapsPtr decoderCaps;
    if (pDecoder->GetCaps(&decoderCaps) != AMF_OK) {
        LOG_INFO("%s", AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED).c_str());
        return;
    }
    amf::AMF_ACCELERATION_TYPE accelType = decoderCaps->GetAccelerationType();
    LOG_INFO("Acceleration Type: %s", AccelTypeToString(accelType).c_str());
    amf::AMFIOCapsPtr inputCaps;
    AmfCodecCapbility capbility;
    if (decoderCaps->GetInputCaps(&inputCaps) != AMF_OK || !QueryIOCaps(inputCaps, capbility)) {
        return;
    }
    LOG_INFO("Dimension limit: [%u X %u -> %u X %u]", capbility.min_width, capbility.min_height,
             capbility.max_width, capbility.max_height);
    std::wstring formats_str;
    for (auto format : capbility.input_formats) {
        formats_str += std::wstring(L" ") + std::wstring(amf_trace->SurfaceGetFormatName(format));
    }
    LOG_INFO("Supported Formats: %S", formats_str.c_str());
    decoder_capbilities_.emplace(codec, capbility);
}

bool AmfModule::isSupportAVCEncode(uint32_t width, uint32_t height) const {
    auto iter = encoder_capbilities_.find(amf_codec_type::AVC);
    if (iter == encoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isSupportHEVCEncode(uint32_t width, uint32_t height) const {
    auto iter = encoder_capbilities_.find(amf_codec_type::HEVC);
    if (iter == encoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isSupportAV1Encode(uint32_t width, uint32_t height) const {
    auto iter = encoder_capbilities_.find(amf_codec_type::AV1);
    if (iter == encoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isSupportAVCDecoder(uint32_t width, uint32_t height) const {
    auto iter = decoder_capbilities_.find(amf_codec_type::AVC);
    if (iter == decoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isSupportHEVCDecoder(uint32_t width, uint32_t height) const {
    auto iter = decoder_capbilities_.find(amf_codec_type::HEVC);
    if (iter == decoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isSupportAV1Decoder(uint32_t width, uint32_t height) const {
    auto iter = decoder_capbilities_.find(amf_codec_type::AV1);
    if (iter == decoder_capbilities_.end()) {
        return false;
    }
    if (iter->second.max_height < height || iter->second.max_width < width) {
        return false;
    }
    if (iter->second.min_height > height || iter->second.min_width > width) {
        return false;
    }
    return true;
}

bool AmfModule::isEncoderFormatSupport(amf_codec_type type, amf::AMF_SURFACE_FORMAT format) const {
    auto iter = encoder_capbilities_.find(type);
    if (iter == encoder_capbilities_.end()) {
        return false;
    }
    auto& capbility = iter->second;
    if (capbility.input_formats.find(format) == capbility.input_formats.end()) {
        return false;
    }
    return true;
}

std::string codecstr(amf_codec_type type) {
    switch (type) {
    case amf_codec_type::AV1:
        return "AV1";
    case amf_codec_type::AVC:
        return "H264";
    case amf_codec_type::HEVC:
        return "H265";
    default:
        return "UNKNOWN";
    }
}

std::string rcstr(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM rc) {
    switch (rc) {
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP:
        return "CQP";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR:
        return "CBR";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR:
        return "HighQualityCBR";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
        return "PeakConstrainedVBR";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
        return "LatencyConstrainedVBR";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR:
        return "QualityVBR";
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR:
        return "HighQualityVBR";
    default:
        return "UNKNOWN";
    }
}

std::string rcstr_hevc(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM rc) {
    switch (rc) {
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP:
        return "CQP";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR:
        return "CBR";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR:
        return "HighQualityCBR";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
        return "PeakConstrainedVBR";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
        return "LatencyConstrainedVBR";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR:
        return "QualityVBR";
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR:
        return "HighQualityVBR";
    default:
        return "UNKNOWN";
    }
}

std::string rcstr_av1(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM rc) {
    switch (rc) {
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP:
        return "CQP";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR:
        return "CBR";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR:
        return "HighQualityCBR";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
        return "PeakConstrainedVBR";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
        return "LatencyConstrainedVBR";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR:
        return "QualityVBR";
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR:
        return "HighQualityVBR";
    default:
        return "UNKNOWN";
    }
}

std::string formatstr(amf::AMF_SURFACE_FORMAT format) {
    switch (format) {
    case AMF_SURFACE_FORMAT::AMF_SURFACE_RGBA:
        return "RGBA";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_ARGB:
        return "ARGB";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_BGRA:
        return "BGRA";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_NV12:
        return "NV12";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_P010:
        return "P010";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_YUV420P:
        return "YUV420P";
    case AMF_SURFACE_FORMAT::AMF_SURFACE_UNKNOWN:
        return "UNKNOWN";
    default:
        return std::to_string((uint16_t)format);
    }
}

void AmfContext::reset() {
    width = 0;
    height = 0;
    frame_rate = 0;
    throughput = 0;
    max_throughput = 0;
    requested_throughput = 0;
    codec = amf_codec_type::AVC;
    format = AMF_SURFACE_FORMAT::AMF_SURFACE_UNKNOWN;
    rc = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM::AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
    target_bitrate = 0;
    encoded_count = 0;
}

std::string AmfContext::to_str() {
    char buffer[1024] = {0};
    std::string rc_str;
    switch (codec) {
    case amf_codec_type::AVC:
        rc_str = rcstr(static_cast<AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM>(rc));
        break;
    case amf_codec_type::HEVC:
        rc_str = rcstr_hevc(static_cast<AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM>(rc));
        break;
    case amf_codec_type::AV1:
        rc_str = rcstr_av1(static_cast<AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM>(rc));
        break;
    default:
        rc_str = "UNKNOWN";
        break;
    }
    std::string scenario_str;
    switch (scenario) {
    case Scenario::NORMAL:
        scenario_str = "Normal";
        break;
    case Scenario::SCREEN_SHARED_DOCUMENT:
        scenario_str = "Doc";
        break;
    default:
        scenario_str = "Normal";
        break;
    }
    snprintf(buffer, sizeof(buffer) - 1,
             "%u X %u, %" AMFPRId64 "FPS,%" AMFPRId64 " %" AMFPRId64 " %" AMFPRId64
             ", %s, %s, %s, %s, qp:%u,%u",
             width, height, frame_rate, throughput, max_throughput, requested_throughput,
             codecstr(codec).c_str(), rc_str.c_str(), formatstr(format).c_str(),
             scenario_str.c_str(), min_qp, max_qp);
    return buffer;
}

inline char getLowByte(amf_uint16 data) {
    return (data >> 8);
}

inline char getHiByte(amf_uint16 data) {
    return (data & 0xFF);
}

inline bool getBit(const amf_uint8* data, size_t& bitIdx) {
    bool ret = (data[bitIdx / 8] >> (7 - bitIdx % 8) & 1);
    bitIdx++;
    return ret;
}
inline amf_uint32 getBitToUint32(const amf_uint8* data, size_t& bitIdx) {
    amf_uint32 ret = (data[bitIdx / 8] >> (7 - bitIdx % 8) & 1);
    bitIdx++;
    return ret;
}

inline amf_uint32 readBits(const amf_uint8* data, size_t& startBitIdx, size_t bitsToRead) {
    if (bitsToRead > 32) {
        return 0; // assert(0);
    }
    amf_uint32 result = 0;
    for (size_t i = 0; i < bitsToRead; i++) {
        result = result << 1;
        result |= getBitToUint32(data, startBitIdx); // startBitIdx incremented inside
    }
    return result;
}

void H264ExtraDataBuilder::AddSPS(const amf_uint8* sps, size_t size) {
    m_SPSCount++;
    size_t pos = m_SPSs.GetSize();
    amf_uint16 spsSize = size & maxSpsSize;
    m_SPSs.SetSize(pos + spsSize + 2);
    amf_uint8* data = m_SPSs.GetData() + pos;
    *data++ = getLowByte(spsSize);
    *data++ = getHiByte(spsSize);
    memcpy(data, sps, (size_t)spsSize);
}

void H264ExtraDataBuilder::AddPPS(const amf_uint8* pps, size_t size) {
    m_PPSCount++;
    size_t pos = m_PPSs.GetSize();
    amf_uint16 ppsSize = size & maxPpsSize;
    m_PPSs.SetSize(pos + ppsSize + 2);
    amf_uint8* data = m_PPSs.GetData() + pos;
    *data++ = getLowByte(ppsSize);
    *data++ = getHiByte(ppsSize);
    memcpy(data, pps, (size_t)ppsSize);
}

bool H264ExtraDataBuilder::GetExtradata(AMFByteArray& extradata) {
    if (m_SPSs.GetSize() == 0 || m_PPSs.GetSize() == 0) {
        return false;
    }

    if (m_SPSCount > 0x1F) {
        return false;
    }

    if (m_SPSs.GetSize() < minSpsSize) {
        return false;
    }

    extradata.SetSize(7 + m_SPSs.GetSize() + m_PPSs.GetSize());

    amf_uint8* data = extradata.GetData();
    amf_uint8* sps0 = m_SPSs.GetData();
    // c

    *data++ = 0x01;                             // configurationVersion
    *data++ = sps0[3];                          // AVCProfileIndication
    *data++ = sps0[4];                          // profile_compatibility
    *data++ = sps0[5];                          // AVCLevelIndication
    *data++ = (0xFC | (NalUnitLengthSize - 1)); // reserved(11111100) + lengthSizeMinusOne
    *data++ = (0xE0 | static_cast<amf_uint8>(
                          m_SPSCount)); // reserved(11100000) + numOfSequenceParameterSets
    memcpy(data, m_SPSs.GetData(), m_SPSs.GetSize());
    data += m_SPSs.GetSize();
    //    if (m_PPSCount > 0xFF)
    //    {
    //        return false;
    //    }
    *data++ = (static_cast<amf_uint8>(m_PPSCount)); // numOfPictureParameterSets
    memcpy(data, m_PPSs.GetData(), m_PPSs.GetSize());
    data += m_PPSs.GetSize();
    return true;
}

void H265ExtraDataBuilder::AddSPS(const amf_uint8* sps, size_t size) {
    m_SPSCount++;
    size_t pos = m_SPSs.GetSize();
    amf_uint16 spsSize = size & maxSpsSize;
    m_SPSs.SetSize(pos + spsSize + 2);
    amf_uint8* data = m_SPSs.GetData() + pos;
    *data++ = getLowByte(spsSize);
    *data++ = getHiByte(spsSize);
    memcpy(data, sps, (size_t)spsSize);
}
void H265ExtraDataBuilder::AddPPS(const amf_uint8* pps, size_t size) {
    m_PPSCount++;
    size_t pos = m_PPSs.GetSize();
    amf_uint16 ppsSize = size & maxPpsSize;
    m_PPSs.SetSize(pos + ppsSize + 2);
    amf_uint8* data = m_PPSs.GetData() + pos;
    *data++ = getLowByte(ppsSize);
    *data++ = getHiByte(ppsSize);
    memcpy(data, pps, (size_t)ppsSize);
}

enum NalUnitType {
    NAL_UNIT_VPS = 32, // 32
    NAL_UNIT_SPS = 33, // 33
    NAL_UNIT_PPS = 34, // 34
    NAL_UNIT_INVALID = 64,
};

bool H265ExtraDataBuilder::GetExtradata(AMFByteArray& extradata) {
    if (m_SPSs.GetSize() == 0 || m_PPSs.GetSize() == 0) {
        return false;
    }

    if (m_SPSCount > 0x1F) {
        return false;
    }

    if (m_SPSs.GetSize() < minSpsSize) {
        return false;
    }

    extradata.SetSize(21 +                   // reserved
                      1 +                    // length size
                      1 +                    // array size
                      3 +                    // SPS type + SPS count (2)
                      m_SPSs.GetSize() + 3 + // PPS type + PPS count (2)
                      m_PPSs.GetSize());

    amf_uint8* data = extradata.GetData();

    memset(data, 0, extradata.GetSize());

    *data = 0x01; // configurationVersion
    data += 21;
    // reserved(11111100) + lengthSizeMinusOne
    *data++ = (0xFC | (NalUnitLengthSize - 1));
    // reserved(11100000) + numOfSequenceParameterSets
    *data++ = static_cast<amf_uint8>(2);

    *data++ = NAL_UNIT_SPS;
    *data++ = getLowByte(static_cast<amf_int16>(m_SPSCount));
    *data++ = getHiByte(static_cast<amf_int16>(m_SPSCount));

    memcpy(data, m_SPSs.GetData(), m_SPSs.GetSize());
    data += m_SPSs.GetSize();

    *data++ = NAL_UNIT_PPS;
    *data++ = getLowByte(static_cast<amf_int16>(m_PPSCount));
    *data++ = getHiByte(static_cast<amf_int16>(m_PPSCount));
    memcpy(data, m_PPSs.GetData(), m_PPSs.GetSize());
    data += m_PPSs.GetSize();
    return true;
}

void AmfEncoderDebuger::addInput(uint32_t target_fps, int64_t at_time) {
    target_fps_.emplace(at_time, target_fps);
    input_fps_.emplace(at_time, 1);
    if (at_time - target_fps_.begin()->first >= window_length_ms_ * 1000) {
        auto iter = target_fps_.lower_bound(at_time - window_length_ms_ * 1000 / 2);
        target_fps_.erase(target_fps_.begin(), iter);
    }
    if (at_time - input_fps_.begin()->first >= window_length_ms_ * 1000) {
        auto iter = input_fps_.lower_bound(at_time - window_length_ms_ * 1000 / 2);
        input_fps_.erase(input_fps_.begin(), iter);
    }
}

void AmfEncoderDebuger::addOuput(size_t size, uint32_t qp, uint32_t target_bitrate,
                                 int64_t at_time) {
    OutputFrame frame;
    frame.size = size;
    frame.qp = qp;
    output_frames_.emplace(at_time, frame);
    if (at_time - output_frames_.begin()->first >= window_length_ms_ * 1000) {
        auto iter = output_frames_.lower_bound(at_time - window_length_ms_ * 1000 / 2);
        output_frames_.erase(output_frames_.begin(), iter);
    }
    target_bitrate_.emplace(at_time, target_bitrate);
    if (at_time - target_bitrate_.begin()->first >= window_length_ms_ * 1000) {
        auto iter = target_bitrate_.lower_bound(at_time - window_length_ms_ * 1000 / 2);
        target_bitrate_.erase(target_bitrate_.begin(), iter);
    }
}

bool AmfEncoderDebuger::stat(std::vector<Statistics>& outputs, int64_t at_time,
                             int64_t interval_ms) {
    if (output_frames_.empty() || at_time - last_stat_time_ <= interval_ms * 1000) {
        return false;
    }
    last_stat_time_ = at_time - interval_ms * 1000;

    while (last_stat_time_ < at_time) {
        Statistics output;
        auto start_time = last_stat_time_;
        auto end_time = start_time + 1000 * 1000;

        auto iter1 = target_fps_.lower_bound(start_time);
        auto iter2 = target_fps_.upper_bound(end_time);
        size_t count = std::distance(iter1, iter2);
        if (count == 0) {
            last_stat_time_ = end_time;
            continue;
        }
        std::for_each(iter1, iter2, [&output](const std::pair<int64_t, int>& iter) {
            output.target_fps += iter.second;
        });
        output.target_fps = static_cast<uint32_t>(output.target_fps * 1.0f / count + 0.5);

        iter1 = input_fps_.lower_bound(start_time);
        iter2 = input_fps_.upper_bound(end_time);
        std::for_each(iter1, iter2, [&output](const std::pair<int64_t, int>& iter) {
            output.input_fps += iter.second;
        });

        iter1 = target_bitrate_.lower_bound(start_time);
        iter2 = target_bitrate_.upper_bound(end_time);
        count = std::distance(iter1, iter2);
        if (count > 0) {
            std::for_each(iter1, iter2, [&output](const std::pair<int64_t, int>& iter) {
                output.target_bitrate += iter.second;
            });
            output.target_bitrate =
                static_cast<uint32_t>(output.target_bitrate * 1.0f / count + 0.5);
        }
        {
            auto iter1 = output_frames_.lower_bound(start_time);
            auto iter2 = output_frames_.upper_bound(end_time);
            std::for_each(iter1, iter2, [&output](const std::pair<int64_t, OutputFrame>& iter) {
                output.frame_size.add(iter.second.size);
                output.qp.add(iter.second.qp);
            });
            size_t count = std::distance(iter1, iter2);
            if (count > 0) {
                output.output_bitrate =
                    static_cast<uint32_t>(output.frame_size.average * 8.0f + 0.5);
            }
            output.frame_size.update();
            output.qp.update();
        }
        last_stat_time_ = end_time;
        outputs.push_back(output);
    }
    last_stat_time_ = std::min(at_time, last_stat_time_);
    return true;
}

std::string AmfEncoderDebuger::Statistics::to_str() {
    /*
      uint32_t target_fps = 0;
      uint32_t input_fps = 0;
      uint32_t target_bitrate = 0;
      uint32_t output_bitrate = 0;
      DebugValue<size_t> frame_size;
      DebugValue<uint32_t> qp;
    */
    char buffer[1024] = {0};
    snprintf(buffer, sizeof(buffer), "{%u|%u, %u|%ukbps, %uByte, %u|%u}", target_fps, input_fps,
             static_cast<uint32_t>(target_bitrate / 1000.f + 0.5),
             static_cast<uint32_t>(output_bitrate / 1000.f + 0.5), (uint32_t)frame_size.medium,
             qp.medium, qp.max);
    return buffer;
}
} // namespace amf

#pragma warning(pop)
