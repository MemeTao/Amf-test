#include "pch.h"
#include "App.h"
#include "SampleWindow.h"

#include "../amf/amf_encoder.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace winrt
{
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Storage::Pickers;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

static int64_t cur_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // works but everything draws small
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Check to see that capture is supported
    auto isCaptureSupported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
    if (!isCaptureSupported)
    {
        MessageBoxW(nullptr,
            L"Screen capture is not supported on this device for this release of Windows!",
            L"Win32CaptureSample",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Initialize Composition
    auto compositor = winrt::Compositor();
    auto root = compositor.CreateContainerVisual();
    root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    root.Size({ -220.0f, 0.0f });
    root.Offset({ 220.0f, 0.0f, 0.0f });

    // Create the pickers
    auto capturePicker = winrt::GraphicsCapturePicker();
    auto savePicker = winrt::FileSavePicker();

    // Create the app
    auto app = std::make_shared<App>(root, capturePicker, savePicker);

    auto window = SampleWindow(800, 600, app);

    // Provide the window handle to the pickers (explicit HWND initialization)
    window.InitializeObjectWithWindowHandle(capturePicker);
    window.InitializeObjectWithWindowHandle(savePicker);

    // Hookup the visual tree to the window
    auto target = window.CreateWindowTarget(compositor);
    target.Root(root);

    Config config;
    std::unique_ptr<AmfEncoder> amf_encoder;
    // Message pump
    MSG msg = {};
    auto t1 = cur_time();
    auto key_frame_time = cur_time();
    auto bitrate_update_time = cur_time();
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        const uint32_t frame_rate = 15;
        const uint32_t bitrate_kbps = 1000;
        if (cur_time() - t1 < (1000 * 1000 / frame_rate)) {
            continue;
        }
        t1 = cur_time();
        Nv12Frame frame;
        auto capturer = window.GetCapturer();
        if (!capturer || !capturer->GetFrame(&frame)) {
            continue;
        }
        if (!amf_encoder || config.width != frame.width || config.height != frame.height) {
            amf_encoder = std::make_unique<AmfEncoder>();
            config.bitrate_kbps = bitrate_kbps;
            config.width = frame.width;
            config.height = frame.height;
            config.qp_min = 20;
            config.qp_max = 40;
            config.framerate = frame_rate;
            if (!amf_encoder->Initialize(config)) {
                amf_encoder = nullptr;
                config.width = 0;
                continue;
            }
        }
        assert(amf_encoder);
        bool key_frame = false;
        // key frame interval: 8 sec
        if (cur_time() - key_frame_time >= 1000 * 1000 * 8) {
            key_frame = true;
            key_frame_time = cur_time();
        }
        // dynamic change bitrate: 1sec
        if (cur_time() - bitrate_update_time >= 1000 * 1000) {
            bitrate_update_time = cur_time();
            static uint32_t round = 0;
            //[700 - 1000] kbps,
            const uint32_t new_bitrate = bitrate_kbps * 1000 - (round++ % 3) * 100 * 1000;
            amf_encoder->RequestEncodingParametersChange(new_bitrate, frame_rate);
        }
        amf_encoder->EncodeFrame(frame.data, frame.width, frame.height, key_frame);
    }
    return util::ShutdownDispatcherQueueControllerAndWait(controller, static_cast<int>(msg.wParam));
}