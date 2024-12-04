//
//  Agora RTC/MEDIA SDK
//
//  Copyright (c) 2024 Agora.io. All rights reserved.
//
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "amf_helper.h"
#include "core/Factory.h"
#include "core/Trace.h"

struct Config {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t qp_min = 20;
    uint32_t qp_max = 40;
    int framerate = 0;
    uint32_t bitrate_kbps = 0;
};

class AmfEncoder : public amf::AMFSurfaceObserver {
    enum class InputFormat : uint8_t {
        UNKNOWN = 0,
        I420 = 1,
        ARGB_TEXTURE = 2,
        NV12_TEXTURE = 3,
    };

public:
    ~AmfEncoder();

    // VideoEncodeAccelerator implementation.
    bool Initialize(const Config& config);

    int32_t EncodeFrame(const std::vector<uint8_t>& data, uint32_t widht, uint32_t height,
                        bool force_key);

    int32_t RequestEncodingParametersChange(uint32_t bitrate, uint32_t framerate);

private:
    void uninit();

    bool initD3d11(uint64_t luid);

    bool initCodec(const Config& config);

    bool resetDevice(uint64_t luid);

    bool onImageEncoded(amf::AMFDataPtr& pkt);

private:
    void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* pSurface) override;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> copyFrameToTexture(const std::vector<uint8_t>& data,
                                                               uint32_t widht, uint32_t height);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> getAvailableTexture(const D3D11_TEXTURE2D_DESC& desc);

    bool isTimeToChangeTargetFps(int64_t at_time);

    bool isTimeToChangeTargetBitrate(int64_t at_time);

    void applyFramerate(int64_t at_time);

    void applyBitrate(int64_t at_time);

    void applyFrameRateAndBitrate();

    void LimitQPForScc();
    void RecoverQPRange();

private:
    bool applyH264Parameters(const Config& config);

    bool applyH265Parameters(const Config& config);

    bool applyAV1Parameters(const Config& config);

    bool triggleKeyFrame(amf::AMFSurfacePtr& surface);

private:
    uint64_t luid_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_dev_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_ctx_;

    amf::AMFContextPtr amf_context_ = nullptr;
    amf::AMFComponentPtr amf_encoder_ = nullptr;

    std::mutex texture_mtx_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> available_textures_;
    std::unordered_map<amf::AMFSurface*, Microsoft::WRL::ComPtr<ID3D11Texture2D>> active_textures_;

    D3D11_TEXTURE2D_DESC temp_texture_desc_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> temp_texture_;

    amf::AMFDataPtr encoded_pkt_;

    amf::AmfContext help_ctx_;

    std::unique_ptr<amf::NV12Convertor> nv12_convertor_;

    Config config_;

    InputFormat input_format_ = InputFormat::UNKNOWN;

    amf::AmfEncoderDebuger input_output_recorder_;

    bool recover_qp_range_ = false;
};
