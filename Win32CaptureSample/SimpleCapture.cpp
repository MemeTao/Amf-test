#include "pch.h"
#include "SimpleCapture.h"

#include "../amf/amf_helper.h"

#define LOG_DEBUG(...) amf::log(0, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) amf::log(1, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) amf::log(2, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) amf::log(3, __FILE__, __LINE__, __VA_ARGS__)

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::uwp;
}

SimpleCapture::SimpleCapture(winrt::IDirect3DDevice const& device, winrt::GraphicsCaptureItem const& item, winrt::DirectXPixelFormat pixelFormat)
{
    m_item = item;
    m_device = device;
    m_pixelFormat = pixelFormat;

    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    d3dDevice->GetImmediateContext(m_d3dContext.put());

    m_swapChain = util::CreateDXGISwapChain(d3dDevice, static_cast<uint32_t>(m_item.Size().Width), static_cast<uint32_t>(m_item.Size().Height),
        static_cast<DXGI_FORMAT>(m_pixelFormat), 2);

    // Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
    // means that the frame pool's FrameArrived event is called on the thread
    // the frame pool was created on. This also means that the creating thread
    // must have a DispatcherQueue. If you use this method, it's best not to do
    // it on the UI thread. 
    m_framePool = winrt::Direct3D11CaptureFramePool::Create(m_device, m_pixelFormat, 2, m_item.Size());
    m_session = m_framePool.CreateCaptureSession(m_item);
    m_lastSize = m_item.Size();
    m_framePool.FrameArrived({ this, &SimpleCapture::OnFrameArrived });
}

void SimpleCapture::StartCapture()
{
    CheckClosed();
    m_session.StartCapture();
}

winrt::ICompositionSurface SimpleCapture::CreateSurface(winrt::Compositor const& compositor)
{
    CheckClosed();
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_swapChain.get());
}

void SimpleCapture::Close()
{
    auto expected = false;
    if (m_closed.compare_exchange_strong(expected, true))
    {
        m_session.Close();
        m_framePool.Close();

        m_swapChain = nullptr;
        m_framePool = nullptr;
        m_session = nullptr;
        m_item = nullptr;
    }
}

void SimpleCapture::ResizeSwapChain()
{
    winrt::check_hresult(m_swapChain->ResizeBuffers(2, static_cast<uint32_t>(m_lastSize.Width), static_cast<uint32_t>(m_lastSize.Height),
        static_cast<DXGI_FORMAT>(m_pixelFormat), 0));
}

bool SimpleCapture::TryResizeSwapChain(winrt::Direct3D11CaptureFrame const& frame)
{
    auto const contentSize = frame.ContentSize();
    if ((contentSize.Width != m_lastSize.Width) ||
        (contentSize.Height != m_lastSize.Height))
    {
        // The thing we have been capturing has changed size, resize the swap chain to match.
        m_lastSize = contentSize;
        ResizeSwapChain();
        return true;
    }
    return false;
}

bool SimpleCapture::TryUpdatePixelFormat()
{
    auto newFormat = m_pixelFormatUpdate.exchange(std::nullopt);
    if (newFormat.has_value())
    {
        auto pixelFormat = newFormat.value();
        if (pixelFormat != m_pixelFormat)
        {
            m_pixelFormat = pixelFormat;
            ResizeSwapChain();
            return true;
        }
    }
    return false;
}

void SimpleCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&)
{
    auto swapChainResizedToFrame = false;
    {
        auto frame = sender.TryGetNextFrame();
        swapChainResizedToFrame = TryResizeSwapChain(frame);

        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
        auto surfaceTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
        D3D11_TEXTURE2D_DESC desc;
        surfaceTexture->GetDesc(&desc);
        if (!texture_bk_ || desc_bk_.Width != desc.Width || desc_bk_.Height != desc.Height) {
            auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = 0;
            desc.CPUAccessFlags = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            auto hr = d3dDevice->CreateTexture2D(&desc, nullptr, &texture_bk_);
            if (hr != S_OK) {
                LOG_ERROR("Failed to call CreateTexture2D, hr:%0x4", hr);
                return;
            }
            texture_bk_->GetDesc(&desc_bk_);
            desc.Format = DXGI_FORMAT_NV12;
            desc.Width = (desc.Width + 1) / 2 * 2;
            desc.Height = (desc.Height + 1) / 2 * 2;
            hr = d3dDevice->CreateTexture2D(&desc, nullptr, &texture_bk_nv12_);
            if (hr != S_OK) {
                LOG_ERROR("Failed to call CreateTexture2D, hr:%0x4", hr);
                return;
            }
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            hr = d3dDevice->CreateTexture2D(&desc, nullptr, &texture_bk_nv12_cpu_access_);
            if (hr != S_OK) {
                LOG_ERROR("Failed to call CreateTexture2D, hr:%0x4", hr);
                return;
            }
            nv12_convertor_ = std::make_unique<amf::NV12Convertor>();
            if (!nv12_convertor_->init(d3dDevice.get(), desc.Width, desc.Height)) {
                nv12_convertor_ = nullptr;
                LOG_ERROR("Failed to initialize nv12 convertor");
                return;
            }
        }
        m_d3dContext->CopyResource(backBuffer.get(), surfaceTexture.get());
        // Convert bgra to nv12
        m_d3dContext->CopyResource(texture_bk_.Get(), surfaceTexture.get());
        if (nv12_convertor_) {
            nv12_convertor_->convert(texture_bk_, texture_bk_nv12_);
        }
    }

    DXGI_PRESENT_PARAMETERS presentParameters{};
    m_swapChain->Present1(1, 0, &presentParameters);

    swapChainResizedToFrame = swapChainResizedToFrame || TryUpdatePixelFormat();

    if (swapChainResizedToFrame)
    {
        m_framePool.Recreate(m_device, m_pixelFormat, 2, m_lastSize);
    }
}


bool SimpleCapture::GetFrame(Nv12Frame* output) {
    if (!texture_bk_nv12_ || !nv12_convertor_) {
        return false;
    }
    m_d3dContext->CopyResource(texture_bk_nv12_cpu_access_.Get(), texture_bk_nv12_.Get());
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    //SaveNv12Stream(d3dDevice.get(), texture_bk_nv12_cpu_access_.Get(), "nv12capture.nv12");
    D3D11_TEXTURE2D_DESC desc;
    texture_bk_nv12_cpu_access_->GetDesc(&desc);
    output->data.resize(desc.Width * desc.Height * 3 / 2);
    D3D11_MAPPED_SUBRESOURCE resource;
    auto hr = m_d3dContext->Map(texture_bk_nv12_cpu_access_.Get(), 0, D3D11_MAP_WRITE, 0, &resource);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map textuer, hr:%0x", hr);
        return false;
    }
    for (int i = 0; i < desc.Height * 3 / 2; i++) {
        memcpy((uint8_t*)output->data.data() + i * desc.Width, (uint8_t*)resource.pData + i * resource.RowPitch,
            desc.Width);
    }
    m_d3dContext->Unmap(texture_bk_nv12_.Get(), 0);
    output->width = desc.Width;
    output->height = desc.Height;
    output->stride = output->width;
    return true;
}
