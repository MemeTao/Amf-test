## Requirements
* vs2022
* Amd gpu
* win11 (win10 maybe ok)
  
This sample requires the [Windows 11 SDK (10.0.22000.194)](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/) and [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) to compile. Neither are required to run the sample once you have a binary. The minimum verison of Windows 10 required to run the sample is build 17134.

## How to run
* First, compile project using vs2022 (double click Win32CaptureSample.sln)
* Second, open *test-video.mp4*, try to adjust the video player size to 1280X720 (The closer the better)
* Double click Win32CaptureSample.exe and select the video player window by click 'windows' drop-down box
![image](https://github.com/user-attachments/assets/9a159a93-e60a-4076-a735-11ed2a4fb0a6)
And it will start running：
![image](https://github.com/user-attachments/assets/46a42aec-d68c-41d5-b3a3-14de36017859)


Output Files:
* *amf-test.log*: Print QP and Bitrate
* *Win32CaptureSample.exe.log*: Amf debug log
![image](https://github.com/user-attachments/assets/8225b6eb-d93d-4c61-a16e-7142f5adf362)


```shell
 //Statistics for each frame
Frame 0, P, QP: 36, size: 2263 B, Target:900 kbps, 7500 B, 15 FPS 

 //Statistics every 1 seconds: target fps|actual fps, target bitrate|actual bitrate, FrameSize(medium), QP(medium|max)
 //Output every 5 seconds 
[{15|13, 815|575kbps, 2513Byte, 39|40}{15|14, 957|409kbps, 756Byte, 30|33}{15|13, 923|588kbps, 4622Byte, 39|40}{15|14, 829|592kbps, 4097Byte, 39|40}{15|14, 943|357kbps, 1075Byte, 37|40}]
```
## Program
* Initialize amf encoder using {720p, qp:20-40, 1mbps}
* The program will capture the content of the selected window and convert it to i420 format from bgra texture
* Deliver i420 data to amf encoder
* Dynamic change target bitrate and request keyframe.

## H264 Parameters

```c++
struct Config {
    uint32_t width = 0;        // 1280
    uint32_t height = 0;       // 720
    uint32_t qp_min = 20;
    uint32_t qp_max = 40;
    int framerate = 0;           // 15
    uint32_t bitrate_kbps = 0; // 1000 kbps
};

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

```
