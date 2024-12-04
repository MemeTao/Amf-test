
#include "nv12_convert.h"

#if !defined(SAFE_RELEASE)
#define SAFE_RELEASE(X)                                                                            \
    if (X) {                                                                                       \
        X->Release();                                                                              \
        X = nullptr;                                                                               \
    }
#endif

/// Constructor
D3D11VideoProcessorConvert::D3D11VideoProcessorConvert(ID3D11Device* pDev,
                                                       ID3D11DeviceContext* pCtx)
    : m_pDev(pDev)
    , m_pCtx(pCtx) {
    m_pDev->AddRef();
    m_pCtx->AddRef();
}

/// Initialize Video Context
HRESULT D3D11VideoProcessorConvert::Init() {
    /// Obtain Video device and Video device context
    HRESULT hr = m_pDev->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVid);
    if (FAILED(hr)) {

        return hr;
    }
    hr = m_pCtx->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVidCtx);
    if (FAILED(hr)) {
        ;
    }

    return hr;
}

/// Release all Resources
void D3D11VideoProcessorConvert::Cleanup() {
    if (!m_pVid || !m_pVidCtx) {
        return;
    }
    for (auto& it : output_view_map_) {
        ID3D11VideoProcessorOutputView* pVPOV = it.second;
        pVPOV->Release();
    }

    for (auto& it : input_view_map_) {
        ID3D11VideoProcessorInputView* pVPIn = it.second;
        pVPIn->Release();
    }

    SAFE_RELEASE(m_pVP);
    SAFE_RELEASE(m_pVPEnum);
    SAFE_RELEASE(m_pVidCtx);
    SAFE_RELEASE(m_pVid);
    SAFE_RELEASE(m_pCtx);
    SAFE_RELEASE(m_pDev);
}

HRESULT D3D11VideoProcessorConvert::ConvertAndCrop(ID3D11Texture2D* pRGB, ID3D11Texture2D* pYUV,
                                                   int offset_x, int offset_y, int cropped_width,
                                                   int cropped_height) {
    HRESULT hr = S_OK;
    if (!pRGB) {
        return -1;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> input_texture_device;
    pRGB->GetDevice(input_texture_device.GetAddressOf());

    if (!corp_temp_texture_) {
        CD3D11_TEXTURE2D_DESC input_desc = {};
        pRGB->GetDesc(&input_desc);
        input_desc.Width = cropped_width;
        input_desc.Height = cropped_height;

        hr = input_texture_device->CreateTexture2D(&input_desc, nullptr,
                                                   corp_temp_texture_.GetAddressOf());
        if (FAILED(hr)) {

            return hr;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> input_texture_device_ctx;
    input_texture_device->GetImmediateContext(&input_texture_device_ctx);

    D3D11_BOX source_region;
    source_region.left = offset_x;
    source_region.right = offset_x + cropped_width;
    source_region.top = offset_y;
    source_region.bottom = offset_y + cropped_height;
    source_region.front = 0;
    source_region.back = 1;
    input_texture_device_ctx->CopySubresourceRegion(corp_temp_texture_.Get(), 0, 0, 0, 0, pRGB, 0,
                                                    &source_region);
    return Convert(corp_temp_texture_.Get(), pYUV);
}

/// Perform Colorspace conversion
HRESULT D3D11VideoProcessorConvert::Convert(ID3D11Texture2D* pRGB, ID3D11Texture2D* pYUV,
                                            bool store_texture) {
    HRESULT hr = S_OK;

    D3D11_TEXTURE2D_DESC inDesc = {0};
    D3D11_TEXTURE2D_DESC outDesc = {0};
    pRGB->GetDesc(&inDesc);
    pYUV->GetDesc(&outDesc);

    /// Check if VideoProcessor needs to be reconfigured
    /// Reconfiguration is required if input/output dimensions have changed
    if (m_pVP) {
        if (m_inDesc.Width != inDesc.Width || m_inDesc.Height != inDesc.Height ||
            m_outDesc.Width != outDesc.Width || m_outDesc.Height != outDesc.Height) {
            SAFE_RELEASE(m_pVPEnum);
            SAFE_RELEASE(m_pVP);
        }
    }

    if (!m_pVP) {
        /// Initialize Video Processor
        m_inDesc = inDesc;
        m_outDesc = outDesc;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
                                                          {1, 1},
                                                          inDesc.Width,
                                                          inDesc.Height,
                                                          {1, 1},
                                                          outDesc.Width,
                                                          outDesc.Height,
                                                          D3D11_VIDEO_USAGE_PLAYBACK_NORMAL};
        hr = m_pVid->CreateVideoProcessorEnumerator(&contentDesc, &m_pVPEnum);
        if (FAILED(hr)) {
        }
        hr = m_pVid->CreateVideoProcessor(m_pVPEnum, 0, &m_pVP);
        if (FAILED(hr)) {
        }
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE space = {0};
        space.YCbCr_Matrix = 1;
        m_pVidCtx->VideoProcessorSetOutputColorSpace(m_pVP, &space);
    }

    // Obtain Video Processor Iutput view from input texture
    ID3D11VideoProcessorInputView* pVPIn = nullptr;
    auto it_input_view = input_view_map_.find(pRGB);
    /// Optimization: Check if we already created a video processor output view
    /// for this texture
    if (it_input_view == input_view_map_.end()) {
        /// We don't have a video processor output view for this texture, create one
        /// now.
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputVD = {0, D3D11_VPIV_DIMENSION_TEXTURE2D, {0, 0}};
        hr = m_pVid->CreateVideoProcessorInputView(pRGB, m_pVPEnum, &inputVD, &pVPIn);
        if (FAILED(hr)) {
            SAFE_RELEASE(pVPIn);

            return hr;
        }
        if (store_texture) {
            input_view_map_.insert({pRGB, pVPIn});
        }
    }
    else {
        pVPIn = it_input_view->second;
    }

    /// Obtain Video Processor Output view from output texture
    ID3D11VideoProcessorOutputView* pVPOV = nullptr;
    auto it_output_view = output_view_map_.find(pYUV);
    /// Optimization: Check if we already created a video processor output view
    /// for this texture
    if (it_output_view == output_view_map_.end()) {
        /// We don't have a video processor output view for this texture, create one
        /// now.
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovD = {D3D11_VPOV_DIMENSION_TEXTURE2D};
        hr = m_pVid->CreateVideoProcessorOutputView(pYUV, m_pVPEnum, &ovD, &pVPOV);
        if (FAILED(hr)) {
            SAFE_RELEASE(pVPOV);

            return hr;
        }
        if (store_texture) {
            output_view_map_.insert({pYUV, pVPOV});
        }
    }
    else {
        pVPOV = it_output_view->second;
    }

    /// Create a Video Processor Stream to run the operation
    D3D11_VIDEO_PROCESSOR_STREAM stream = {TRUE, 0, 0, 0, 0, nullptr, pVPIn, nullptr};

    /// Perform the Colorspace conversion
    hr = m_pVidCtx->VideoProcessorBlt(m_pVP, pVPOV, 0, 1, &stream);
    if (!store_texture) {
        SAFE_RELEASE(pVPIn);
        SAFE_RELEASE(pVPOV);
    }
    if (FAILED(hr)) {

        return hr;
    }
    return hr;
}

ID3D11Device* D3D11VideoProcessorConvert::GetConvertD3DDevice() {
    return m_pDev;
}

ID3D11DeviceContext* D3D11VideoProcessorConvert::GetConvertD3DContext() {
    return m_pCtx;
}
