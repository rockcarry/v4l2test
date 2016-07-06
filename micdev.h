#ifndef __MICDEV_ANDROID_H__
#define __MICDEV_ANDROID_H__

// 包含头文件
#include "micdev_android.h"
#include "micdev_tinyalsa.h"

// 预编译开关定义
// #define USE_MICDEV_ANDROID

// 常量定义
#define MICDEV_TS_EXIT   (1 << 0)
#define MICDEV_TS_PAUSE  (1 << 1)

#define MICDEV_COMMON \
    void            *extra;   \
    int              channels;\
    int              samprate;\
    uint8_t         *buffer;  \
    int              buflen;  \
    int              mute;    \
    pthread_t        thread_id;    \
    int              thread_state; \
    void            *recorder;     \
    MICDEV_CAPTURE_CALLBACK callback;

#ifdef USE_MICDEV_ANDROID
#define micdev_init          micdev_android_init
#define micdev_close         micdev_android_close
#define micdev_start_capture micdev_android_start_capture
#define micdev_stop_capture  micdev_android_stop_capture
#define micdev_set_callback  micdev_android_set_callback
#else
#define micdev_init          micdev_tinyalsa_init
#define micdev_close         micdev_tinyalsa_close
#define micdev_start_capture micdev_tinyalsa_start_capture
#define micdev_stop_capture  micdev_tinyalsa_stop_capture
#define micdev_set_callback  micdev_tinyalsa_set_callback
#endif

// 类型定义
// micdev capture callback
typedef int (*MICDEV_CAPTURE_CALLBACK)(void *recorder, void *data[8], int nbsample);


#endif



