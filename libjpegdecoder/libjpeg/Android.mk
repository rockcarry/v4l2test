LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/../Config.mk

ifeq ($(CONFIG_HW_PLAT),libjpeg)

include $(CLEAR_VARS)

LOCAL_MODULE := libjpegdecoder

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)

LOCAL_SHARED_LIBRARIES += \
    libutils \
    libcutils \
    libjpeg

LOCAL_C_INCLUDES += \
    external/jpeg
    
LOCAL_SRC_FILES := \
    jpegdecoder.c

include $(BUILD_SHARED_LIBRARY)

endif

