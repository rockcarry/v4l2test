LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libjpegdecoder

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)

LOCAL_SHARED_LIBRARIES += \
    libsunxi_alloc \
    libcedarxbase \
    libcedarxosal \
    libcedarv \

LOCAL_C_INCLUDES += \
    frameworks/av/media/CedarX-Projects/CedarX/include \
    frameworks/av/media/CedarX-Projects/CedarX/include/include_system
    
LOCAL_SRC_FILES := \
    jpegdecoder.c \
    formatconvert.c \
    LibveDecoder.c

include $(BUILD_SHARED_LIBRARY)

