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
    libui \
    libgui \
    libandroid_runtime

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

