#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "h264hwenc.h"

// 函数实现
void *h264hwenc_init(int w, int h, int frate, int bitrate)
{
    // todo..
    return NULL;
}

void h264hwenc_close(void *ctxt)
{
    // todo..
}

int h264hwenc_enqueue(void *ctxt, void *buf, int pts, int timeout)
{
    // todo..
    return -1;
}

int h264hwenc_dequeue(void *ctxt, void *buf, int timeout)
{
    // todo..
    return -1;
}

