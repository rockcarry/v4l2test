LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := v4l2test

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT)/bin

LOCAL_SRC_FILES := \
    v4l2test.cpp \
    ffjpeg.cpp \
    camdev.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libui \
    libgui \
    libandroid_runtime

#++ for ffmpeg library
LOCAL_CFLAGS += \
    -D__STDC_CONSTANT_MACROS

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

LOCAL_MULTILIB := 32

include $(BUILD_EXECUTABLE)



include $(CLEAR_VARS)

LOCAL_MODULE := encodertest

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT)/bin

LOCAL_SRC_FILES := \
    ffjpeg.cpp \
    ffencoder.cpp \
    encodertest.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils

#++ for ffmpeg library
LOCAL_CFLAGS += \
    -D__STDC_CONSTANT_MACROS

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

LOCAL_MULTILIB := 32

include $(BUILD_EXECUTABLE)



include $(CLEAR_VARS)

LOCAL_MODULE := recordertest

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT)/bin

LOCAL_C_INCLUDES := external/tinyalsa/include

LOCAL_SRC_FILES := \
    micdev_tinyalsa.cpp \
    camdev.cpp \
    ffjpeg.cpp \
    ffencoder.cpp \
    ffrecorder.cpp \
    recordertest.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libui \
    libgui \
    libandroid_runtime \
    libtinyalsa

#++ for ffmpeg library
LOCAL_CFLAGS += \
    -D__STDC_CONSTANT_MACROS

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

LOCAL_MULTILIB := 32

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_MODULE := libffrecorder_jni

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += \
    -DUSE_MICDEV_ANDROID \
    -DENABLE_MEDIARECORDER_JNI \
    -DENABLE_H264_HWENC \
    -DUSE_MEDIASERVER_H264ENC

LOCAL_SRC_FILES := \
    h264hwenc_mediacodec.cpp \
    h264hwenc_cedarx.cpp \
    h264hwenc_mediaserver.cpp \
    micdev_audiorecord_native.cpp \
    camdev.cpp \
    ffjpeg.cpp \
    ffencoder.cpp \
    ffrecorder.cpp \
    com_apical_dvr_MediaRecorder.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libbinder \
    libui \
    libgui \
    libandroid_runtime \
    libmedia \
    libion_alloc \
    libcedarxosal \
    libaw_h264enc

#++ for ffmpeg library
LOCAL_CFLAGS += \
    -D__STDC_CONSTANT_MACROS

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

LOCAL_MULTILIB := 32

include $(BUILD_SHARED_LIBRARY)

