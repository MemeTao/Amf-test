#pragma once

#include <cassert>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "components/Component.h"
#include "components/VideoDecoderUVD.h"
#include "components/VideoEncoderAV1.h"
#include "components/VideoEncoderHEVC.h"
#include "components/VideoEncoderVCE.h"
#include "core/Factory.h"

#include "nv12_convert.h"

#define AMD_VENDOR_ID 0x1002

namespace amf {

enum class amf_codec_type : uint8_t {
    AVC,
    HEVC,
    AV1,
    HEVC_MAIN_10,
};

inline const wchar_t* get_amf_decoder_name(amf_codec_type type) {
    switch (type) {
    case amf_codec_type::AV1:
        return AMFVideoDecoderHW_AV1;
    case amf_codec_type::AVC:
        return AMFVideoDecoderUVD_H264_AVC;
    case amf_codec_type::HEVC:

        return AMFVideoDecoderHW_H265_HEVC;
    case amf_codec_type::HEVC_MAIN_10:
        return AMFVideoDecoderHW_H265_MAIN10;
    default:
        return L"UNKNOWN";
    }
}

struct AmfCodecCapbility {
    uint32_t min_width = 0;
    uint32_t max_width = 0;
    uint32_t min_height = 0;
    uint32_t max_height = 0;
    uint32_t max_profile = 0;
    uint32_t max_temporal_layers = 0;
    uint32_t max_streams = 1;
    bool b_frame_support = false;
    int32_t vertical_align = 0;
    std::set<amf::AMF_SURFACE_FORMAT> input_formats;
};

class NV12Convertor {
public:
    ~NV12Convertor();

    bool init(Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device, uint32_t width, uint32_t height);

    bool convert(Microsoft::WRL::ComPtr<ID3D11Texture2D> input,
                 Microsoft::WRL::ComPtr<ID3D11Texture2D>& output);

private:
    void uninit();

private:
    std::unique_ptr<D3D11VideoProcessorConvert> convert_;
};

void log(int level, const char* file, int line, const char* format, ...);

class AmfModuleWrapper;
class AmfModule {
    friend class AmfModuleWrapper;

public:
    amf::AMFFactory* factory = nullptr;
    amf::AMFTrace* amf_trace = nullptr;
    amf::AMFDebug* amf_debug = nullptr;
    HMODULE amf_module = nullptr;
    uint64_t amf_version = 0;

public:
    bool isSupportAVCEncode(uint32_t width = 1920, uint32_t height = 1080) const;
    bool isSupportHEVCEncode(uint32_t width = 1920, uint32_t height = 1080) const;
    bool isSupportAV1Encode(uint32_t width = 1920, uint32_t height = 1080) const;

    bool isSupportAVCDecoder(uint32_t width = 1920, uint32_t height = 1080) const;
    bool isSupportHEVCDecoder(uint32_t width = 1920, uint32_t height = 1080) const;
    bool isSupportAV1Decoder(uint32_t width = 1920, uint32_t height = 1080) const;

    bool isEncoderFormatSupport(amf_codec_type type, amf::AMF_SURFACE_FORMAT format) const;

private:
    bool init();
    bool doInit();
    void uninit();
    void doUninit();

    void QueryEncoderForCodecAVC(amf::AMFContextPtr context);
    void QueryEncoderForCodecHEVC(amf::AMFContextPtr context);
    void QueryEncoderForCodecAV1(amf::AMFContextPtr context);

    void QueryDecoderForCodec(amf_codec_type codec, amf::AMFContextPtr context);

private:
    std::map<amf_codec_type, AmfCodecCapbility> encoder_capbilities_;
    std::map<amf_codec_type, AmfCodecCapbility> decoder_capbilities_;
};

class AmfModuleWrapper {
public:
    static AmfModule* instance() {
        static AmfModuleWrapper h;
        return h.module_.get();
    }

private:
    AmfModuleWrapper() {
        module_ = std::make_unique<AmfModule>();
        if (!module_->init()) {
            module_ = nullptr;
        }
    }
    ~AmfModuleWrapper() {
        if (module_) {
            module_->uninit();
            module_ = nullptr;
        }
    }

private:
    std::unique_ptr<AmfModule> module_;
};

template <typename T>
inline bool set_amf_property(amf::AMFComponentPtr enc, const wchar_t* name, const T& value) {
    AMF_RESULT res = enc->SetProperty(name, value);
    if (res != AMF_OK) {
        log(3, __FUNCTION__, __LINE__, "Failed to set property '%ls': %ls", name,
            AmfModuleWrapper::instance()->amf_trace->GetResultText(res));
        return false;
    }
    return true;
}

#define set_avc_property(enc, name, value)                                                         \
    amf::set_amf_property(enc, AMF_VIDEO_ENCODER_##name, value)

#define set_hevc_property(enc, name, value)                                                        \
    amf::set_amf_property(enc, AMF_VIDEO_ENCODER_HEVC_##name, value)

#define set_av1_property(enc, name, value)                                                         \
    amf::set_amf_property(enc, AMF_VIDEO_ENCODER_AV1_##name, value)

static AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM get_amf_av_rc(uint32_t value) {
    return static_cast<AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM>(value);
}

static AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM get_amf_hevc_rc(uint32_t value) {
    return static_cast<AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM>(value);
}

static AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM get_amf_av1_rc(uint32_t value) {
    return static_cast<AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM>(value);
}

inline const wchar_t* get_amf_output_type(amf_codec_type type) {
    switch (type) {
    case amf::amf_codec_type::AVC:
        return AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE;
    case amf::amf_codec_type::HEVC:
        return AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE;
        break;
    case amf::amf_codec_type::AV1:
        return AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE;
        break;
    default:
        return L"";
    }
}

struct AmfContext {
    enum class Scenario : uint8_t {
        NORMAL = 0,
        SCREEN_SHARED_DOCUMENT = 1,
    };
    void reset();
    std::string to_str();

    uint32_t width = 0;
    uint32_t height = 0;
    int64_t frame_rate = 0;
    amf_codec_type codec;
    amf::AMF_SURFACE_FORMAT format;
    uint32_t rc;

    amf_int64 max_throughput = 0;
    amf_int64 requested_throughput = 0;
    amf_int64 throughput = 0;

    uint32_t target_fps = 0;
    uint64_t current_bitrate = 0;
    uint64_t target_bitrate = 0;
    uint64_t encoded_count = 0;
    int64_t last_target_fps_changed_time = 0;
    int64_t last_target_bitrate_changed_time = 0;

    uint32_t min_qp = 0;
    uint32_t max_qp = 0;

    Scenario scenario = Scenario::NORMAL;
};

#define INIT_ARRAY_SIZE 1024
#define ARRAY_MAX_SIZE (1LL << 60LL) // extremely large maximum size

class AMFByteArray {
protected:
    amf_uint8* m_pData;
    amf_size m_iSize;
    amf_size m_iMaxSize;

public:
    AMFByteArray()
        : m_pData(0)
        , m_iSize(0)
        , m_iMaxSize(0) {}
    AMFByteArray(const AMFByteArray& other)
        : m_pData(0)
        , m_iSize(0)
        , m_iMaxSize(0) {
        *this = other;
    }
    AMFByteArray(amf_size num)
        : m_pData(0)
        , m_iSize(0)
        , m_iMaxSize(0) {
        SetSize(num);
    }
    virtual ~AMFByteArray() {
        if (m_pData != 0) {
            delete[] m_pData;
        }
    }
    void SetSize(amf_size num) {
        if (num == m_iSize) {
            return;
        }
        if (num < m_iSize) {
            memset(m_pData + num, 0, m_iMaxSize - num);
        }
        else if (num > m_iMaxSize) {
            // This is done to prevent the following error from surfacing
            // for the pNewData allocation on some compilers:
            //     -Werror=alloc-size-larger-than=
            amf_size newSize = (num / INIT_ARRAY_SIZE) * INIT_ARRAY_SIZE + INIT_ARRAY_SIZE;
            if (newSize > ARRAY_MAX_SIZE) {
                return;
            }
            m_iMaxSize = newSize;

            amf_uint8* pNewData = new amf_uint8[m_iMaxSize];
            memset(pNewData, 0, m_iMaxSize);
            if (m_pData != NULL) {
                memcpy(pNewData, m_pData, m_iSize);
                delete[] m_pData;
            }
            m_pData = pNewData;
        }
        m_iSize = num;
    }
    void Copy(const AMFByteArray& old) {
        if (m_iMaxSize < old.m_iSize) {
            m_iMaxSize = old.m_iMaxSize;
            if (m_pData != NULL) {
                delete[] m_pData;
            }
            m_pData = new amf_uint8[m_iMaxSize];
            memset(m_pData, 0, m_iMaxSize);
        }
        memcpy(m_pData, old.m_pData, old.m_iSize);
        m_iSize = old.m_iSize;
    }
    amf_uint8 operator[](amf_size iPos) const { return m_pData[iPos]; }
    amf_uint8& operator[](amf_size iPos) { return m_pData[iPos]; }
    AMFByteArray& operator=(const AMFByteArray& other) {
        SetSize(other.GetSize());
        if (GetSize() > 0) {
            memcpy(GetData(), other.GetData(), GetSize());
        }
        return *this;
    }
    amf_uint8* GetData() const { return m_pData; }
    amf_size GetSize() const { return m_iSize; }
};

// NALU Class
typedef enum {
    NALU_TYPE_SLICE = 1,
    NALU_TYPE_DPA = 2,
    NALU_TYPE_DPB = 3,
    NALU_TYPE_DPC = 4,
    NALU_TYPE_IDR = 5,
    NALU_TYPE_SEI = 6,
    NALU_TYPE_SPS = 7,
    NALU_TYPE_PPS = 8,
    NALU_TYPE_AUD = 9,
    NALU_TYPE_EOSEQ = 10,
    NALU_TYPE_EOSTREAM = 11,
    NALU_TYPE_FILL = 12,
} NaluType;

enum HEVCNaluType {
    HEVC_NAL_VPS = 32,
    HEVC_NAL_SPS = 33,
    HEVC_NAL_PPS = 34,
};

class ExtraDataBuilder {
public:
    ExtraDataBuilder()
        : m_SPSCount(0)
        , m_PPSCount(0) {}

    virtual void AddSPS(const amf_uint8* sps, size_t size) {
        (void)sps;
        (void)size;
    }
    virtual void AddPPS(const amf_uint8* pps, size_t size) {
        (void)pps;
        (void)size;
    }
    virtual bool GetExtradata(AMFByteArray& extradata) {
        (void)extradata;
        return false;
    }
    virtual void SetAnnexB(const amf_uint8* data, size_t size) {
        (void)data;
        (void)size;
    }

protected:
    AMFByteArray m_SPSs;
    AMFByteArray m_PPSs;
    amf_int32 m_SPSCount;
    amf_int32 m_PPSCount;
};

class H264ExtraDataBuilder : public ExtraDataBuilder {
private:
    static const amf_uint16 maxSpsSize = 0xFFFF;
    static const amf_uint16 minSpsSize = 5;
    static const amf_uint16 maxPpsSize = 0xFFFF;
    static const amf_uint8 NalUnitLengthSize = 4U;

private:
    void AddSPS(const amf_uint8* sps, size_t size) override;
    void AddPPS(const amf_uint8* pps, size_t size) override;
    bool GetExtradata(AMFByteArray& extradata) override;
    void SetAnnexB(const amf_uint8* data, size_t size) override { ; }
};

class H265ExtraDataBuilder : public ExtraDataBuilder {
private:
    static const amf_uint16 maxSpsSize = 0xFFFF;
    static const amf_uint16 minSpsSize = 5;
    static const amf_uint16 maxPpsSize = 0xFFFF;

    static const amf_uint32 MacroblocSize = 16;
    static const amf_uint8 NalUnitTypeMask = 0x1F; // b00011111
    static const amf_uint8 NalRefIdcMask = 0x60;   // b01100000
    static const amf_uint8 NalUnitLengthSize = 4U;

private:
    void AddSPS(const amf_uint8* sps, size_t size) override;
    void AddPPS(const amf_uint8* pps, size_t size) override;
    bool GetExtradata(AMFByteArray& extradata) override;
};

class AmfEncoderDebuger {
    struct OutputFrame {
        size_t size = 0;
        int qp = 0;
    };

public:
    AmfEncoderDebuger(uint32_t window_length_second = 30)
        : window_length_ms_(window_length_second * 1000) {
        ;
    }
#pragma warning(push)
#pragma warning(disable : 4267)
    template <typename T> struct DebugValue {
        void add(T v) {
            values_.push_back(v);
            average += v;
            min = std::min<T>(min, v);
            max = std::max<T>(max, v);
        }
        void update() {
            if (values_.size() > 0) {
                average = average / values_.size();
                std::sort(values_.begin(), values_.end());
                medium = values_[values_.size() / 2];
            }
        }

        T min = std::numeric_limits<T>::max();
        T medium = 0;
        T average = 0;
        T max = std::numeric_limits<T>::min();

    private:
        std::vector<T> values_;
    };
#pragma warning(pop)
    struct Statistics {
        uint32_t target_fps = 0;
        uint32_t input_fps = 0;
        uint32_t target_bitrate = 0;
        uint32_t output_bitrate = 0;
        DebugValue<size_t> frame_size;
        DebugValue<uint32_t> qp;

        std::string to_str();
    };

    void addInput(uint32_t target_fps, int64_t at_time);
    void addOuput(size_t size, uint32_t qp, uint32_t target_bitrate, int64_t at_time);
    bool stat(std::vector<Statistics>& output, int64_t at_time, int64_t interval_ms);

private:
    int64_t window_length_ms_ = 0;
    int64_t last_stat_time_ = 0;

    std::map<int64_t /*at_time*/, int> target_fps_;
    std::map<int64_t /*at_time*/, int> input_fps_;

    std::map<int64_t /*at_time*/, int> target_bitrate_;
    std::map<int64_t /*at_time*/, OutputFrame> output_frames_;
};
} // namespace amf
