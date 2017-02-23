v4l2test
========

v4l2test is a project for video & audio recording on android platform. The project implements audio capture via tinyalsa interface, and video capture via v4l2 interface, on android platform. Rendering camrea preview on screen, and recording audio and video into mp4 files by ffmpeg library.

In the project, I write a script to customize ffmpeg library building, which only including ACC & H264 encoder and MP4 file muxer, to reduce library size. If you have a android standalone toolchain, it's very easy for you, to build out the ffmpeg library for android platform.

All code are written in C/C++, after building, you get elf binary excutable program, which can execute in adb shell.
v4l2test     - capture video from camera, and rendering on screen
encodertest  - generate randomize audio & video data, and encode into mp4 file.
recordertest - capture audio & video from mic & camera, preview camera on screen, and recording audio & video into mp4 files.

After code stable, I will implement jni and test apks. It aimed to provide api for audio & video recording and live streaming on android platform, with high stability and performace.

Why did I create it
===================

At my work, I am maintaining codes of android DVR app for in-car products, which request high stability and long time work, which using the android standard camera and recorder api. I meet many bugs and issues, on android camera hal, camera api, recorder api.

The android standard api:

android.hardware.Camera
android.media.MediaRecorder

is very unstable. My apks often meet fatal error, and crash many times. And it is very diffcult to position the problem code, and fix bugs. How is the android standard api for camera and recorder? I say, it just a toy. This is why I create the v4l2test project.

Compared with android camera hal & camera api, my project directly access to camera and mic devices, it is KISS, with out any complex architecture, with out any hal, with out any service, the code is very simple and easy to debug. above the project C/C++ code, you can write a simple jni for java, then you can write a apk, to implement the camera preview and video recording. The code is simple to read and debug, is great.


How to build
============
1. using build_ffmpeg_for_android.sh to build ffmpeg library.
2. using NDK or android build environment to build test program
   (I built these codes under AllWinner A33 Android 4.4 source code enviroment.
    So I suggest using NDK version corresponding to Android 4.4.)


Features
========
1.  audio capture from tinyalsa/AudioRecord
2.  video capture from v4l2
3.  audio & video encoding into mp4
4.  rendering video on screen
5.  resize video when recording
6.  resample audio when recording
7.  change frame rate when recording
9.  optimized for allwinner A33 platorm
10. record segmented video with out dropping any frame
11. auto drop frame when could't encoding in time
12. support push rtmp/hls live stream
13. support android mediacodec h264 hardware encoding


How to use
==========
after build success, you will get v4l2test, encodertest, recordertest and libffrecorder_jni.

v4l2test
--------
push it to /system/bin/ (using adb), chmod 755.
in adb shell, try command:
v4l2test /dev/video0 0 640 480
/dev/video0 - video device file name
0           - video device sub source
           (for some platform front and rear camera both share using /dev/video0,
            but use different source, maybe 0 - rear camera, 1 - front camera)
640 480     - camera preview resolution.
if success, the camera preview will be displayed on screen

encodertest
-----------
try command:
encodertest
it will generate random video and audio data, and encode into mp4 file.

recordertest
------------
try command:
recordertest
it will capture video from camera, audio from mic, and encode into mp4 file.

libffrecorder_jni
-----------------
it is a jni library for MediaRecorder.java class file.
by libffrecorder_jni and MediaRecorder.java, you are able to use v4l2test for android apk development with java code.
an other related project is CameraDVR: https://github.com/rockcarry/CameraDVR


TODO
====
1.  implments hardware encoding for A33 platfrom
    plan to write a hardware H264 encoding ffmpeg codec for A33 platform

2.  stability and performance



rockcarry
2017-2-10



