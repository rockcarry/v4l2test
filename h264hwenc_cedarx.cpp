#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "ffencoder.h"
#include "h264hwenc.h"

// 内部类型定义
// h264hwenc context
typedef struct {
    int         w;
    int         h;
    void       *ffencoder;
} H264ENC;

// 函数实现
void *h264hwenc_cedarx_init(int w, int h, int frate, int bitrate, void *ffencoder)
{
    return NULL;
}

void h264hwenc_cedarx_close(void *ctxt)
{
    return;
}

int h264hwenc_cedarx_picture_format(void *ctxt)
{
    return AV_PIX_FMT_NV12;
}

int h264hwenc_cedarx_picture_alloc(void *ctxt, AVFrame *frame)
{
    return -1;
}

int h264hwenc_cedarx_picture_free(void *ctxt, AVFrame *frame)
{
    return -1;
}

int h264hwenc_cedarx_encode(void *ctxt, AVFrame *frame, int timeout)
{
    return -1;
}

