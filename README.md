## Requirements
* vs2022
* Amd gpu
* win11 (win10 maybe ok)
This sample requires the [Windows 11 SDK (10.0.22000.194)](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/) and [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) to compile. Neither are required to run the sample once you have a binary. The minimum verison of Windows 10 required to run the sample is build 17134.

## how to run
* First, compile project using vs2022
* Second, open test-video.mp4, try to adjust the player size to 1280X720 (The closer the better)
* Double click Win32CaptureSample\x64\Debug\Win32CaptureSample.exe and select the video player window by click 'windows'

![image](https://github.com/user-attachments/assets/3508347b-61af-487c-90ce-bfdde3544665)

And it will start running：

![image](https://github.com/user-attachments/assets/dff1b7b7-c68f-4f36-a880-2ff2b244088f)

Output Files:
* amf-test.log: Print QP and Bitrate
* Win32CaptureSample.exe.log: amf debug log
 ![image](https://github.com/user-attachments/assets/57ffbde9-7664-4af3-afb6-9d3286fa07cf)

```shell```
 //Statistics for each frame
Frame 0, P, QP: 36, size: 2263 B, Target:900 kbps, 7500 B, 15 FPS 

 //Statistics every 1 seconds: target fps|actual fps, target bitrate|actual bitrate, FrameSize(medium), QP(medium|max)
 //Output every 5 seconds 
[{15|13, 815|575kbps, 2513Byte, 39|40}{15|14, 957|409kbps, 756Byte, 30|33}{15|13, 923|588kbps, 4622Byte, 39|40}{15|14, 829|592kbps, 4097Byte, 39|40}{15|14, 943|357kbps, 1075Byte, 37|40}]
```
## Help tips

* 0. Initialize amf encoder using {720p, qp:20-40, 1mbps}
* 1. The program will capture the content of the selected window and convert it to i420 format from bgra texture
* 2. Deliver i420 data to amf encoder
* 3. Dynamic change target bitrate and request keyframe.
