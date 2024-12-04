## Requirements
* vs2022
* Amd gpu
* win11 (win10 maybe ok)
  
This sample requires the [Windows 11 SDK (10.0.22000.194)](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/) and [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) to compile. Neither are required to run the sample once you have a binary. The minimum verison of Windows 10 required to run the sample is build 17134.

## how to run
* First, compile project using vs2022 (double click Win32CaptureSample.sln)
* Second, open test-video.mp4, try to adjust the video player size to 1280X720 (The closer the better)
* Double click Win32CaptureSample.exe and select the video player window by click 'windows' drop-down box
![image](https://github.com/user-attachments/assets/9a159a93-e60a-4076-a735-11ed2a4fb0a6)
And it will start running：
![image](https://github.com/user-attachments/assets/46a42aec-d68c-41d5-b3a3-14de36017859)


Output Files:
* *amf-test.log*: Print QP and Bitrate
* *Win32CaptureSample.exe.log*: Amf debug log
 ![image](https://github.com/user-attachments/assets/57ffbde9-7664-4af3-afb6-9d3286fa07cf)

```shell
 //Statistics for each frame
Frame 0, P, QP: 36, size: 2263 B, Target:900 kbps, 7500 B, 15 FPS 

 //Statistics every 1 seconds: target fps|actual fps, target bitrate|actual bitrate, FrameSize(medium), QP(medium|max)
 //Output every 5 seconds 
[{15|13, 815|575kbps, 2513Byte, 39|40}{15|14, 957|409kbps, 756Byte, 30|33}{15|13, 923|588kbps, 4622Byte, 39|40}{15|14, 829|592kbps, 4097Byte, 39|40}{15|14, 943|357kbps, 1075Byte, 37|40}]
```
## program
* Initialize amf encoder using {720p, qp:20-40, 1mbps}
* The program will capture the content of the selected window and convert it to i420 format from bgra texture
* Deliver i420 data to amf encoder
* Dynamic change target bitrate and request keyframe.
