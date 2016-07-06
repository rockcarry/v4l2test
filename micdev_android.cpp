#define LOG_TAG "camdev"

// 包含头文件
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "micdev.h"
#include "micdev_android.h"

// 内部类型定义
// micdev context
typedef struct {
    MICDEV_COMMON
} MICDEV;

// 内部函数实现
static void* micdev_capture_thread_proc(void *param)
{
    MICDEV *mic = (MICDEV*)param;

    return NULL;
}

// 函数实现
void* micdev_android_init(int samprate, int channels, void *extra) { return NULL; }
void  micdev_android_close(void *ctxt) {}
void  micdev_android_start_capture(void *ctxt) {}
void  micdev_android_stop_capture(void *ctxt) {}
void  micdev_android_set_mute(void *ctxt, int mute) {}
void  micdev_android_set_callback(void *ctxt, void *callback, void *recorder) {}

