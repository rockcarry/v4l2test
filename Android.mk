LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

include $(LOCAL_PATH)/Config.mk

LOCAL_MODULE := v4l2test

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT)/bin

LOCAL_SRC_FILES := \
    v4l2test.cpp \
    camcdr.cpp

LOCAL_C_INCLUDES += \
    $(JNI_H_INCLUDE) \
    $(LOCAL_PATH)/ffjpegdec

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libz \
    libui \
    libgui \
    libandroid_runtime

#++ for ffmpeg library
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

ifeq ($(CONFIG_FFJPEGDEC_TYPE),a31)
LOCAL_C_INCLUDES += \
    frameworks/av/media/CedarX-Projects/CedarX/include \
    frameworks/av/media/CedarX-Projects/CedarX/include/include_system

LOCAL_SRC_FILES += \
    ffjpegdec/$(CONFIG_FFJPEGDEC_TYPE)/ffjpegdec.c \
    ffjpegdec/$(CONFIG_FFJPEGDEC_TYPE)/LibveDecoder.c \
    ffjpegdec/$(CONFIG_FFJPEGDEC_TYPE)/formatconvert.c

LOCAL_SHARED_LIBRARIES += \
    libsunxi_alloc \
    libcedarxbase \
    libcedarxosal \
    libcedarv
endif

ifeq ($(CONFIG_FFJPEGDEC_TYPE),ljp)
LOCAL_C_INCLUDES += \
    external/jpeg

LOCAL_SRC_FILES += \
    ffjpegdec/$(CONFIG_FFJPEGDEC_TYPE)/ffjpegdec.c

LOCAL_SHARED_LIBRARIES += \
    libjpeg
endif

include $(BUILD_EXECUTABLE)



include $(CLEAR_VARS)

LOCAL_MODULE := encodertest

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT)/bin

LOCAL_SRC_FILES := \
    ffencoder.c \
    encodertest.c

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libz

#++ for ffmpeg library
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS += -ldl \
    $(LOCAL_PATH)/ffmpeg/lib/libavformat.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavcodec.a \
    $(LOCAL_PATH)/ffmpeg/lib/libavutil.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswscale.a \
    $(LOCAL_PATH)/ffmpeg/lib/libswresample.a \
    $(LOCAL_PATH)/ffmpeg/lib/libx264.a
#-- for ffmpeg library

LOCAL_MULTILIB := 32

include $(BUILD_EXECUTABLE)


