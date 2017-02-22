#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "cedarx/h264enc.h"
#include "ffencoder.h"
#include "h264hwenc.h"

// 内部类型定义
// h264hwenc context
typedef struct {
    VENC_DEVICE *encdev;
    int          iw;
    int          ih;
    int          ow;
    int          oh;
    void        *ffencoder;
} H264ENC;

// 函数实现
void *h264hwenc_cedarx_init(int iw, int ih, int ow, int oh, int frate, int bitrate, void *ffencoder)
{
    H264ENC *enc = (H264ENC*)calloc(1, sizeof(H264ENC));
    if (!enc) {
        ALOGE("failed to allocate h264hwenc context !\n");
        return NULL;
    }

    int level = 0;
    int ret   = 0;
    enc->encdev = H264EncInit(&ret);
    if (ret < 0) {
        ALOGD("H264EncInit failed !");
        goto failed;
    }

    if (ow >= 1080) { // 1080p
        level = 41;
    } else if (ow >= 720) { // 720p
        level = 31;
    } else { // 640p
        level = 30;
    }

    __video_encode_format_t enc_fmt;
    memset(&enc_fmt, 0, sizeof(__video_encode_format_t));
    enc_fmt.src_width       = iw;
    enc_fmt.src_height      = ih;
    enc_fmt.width           = ow;
    enc_fmt.height          = oh;
    enc_fmt.frame_rate      = frate * 1000;
    enc_fmt.color_format    = PIXEL_YUV420;
    enc_fmt.color_space     = BT601;
    enc_fmt.qp_max          = 40;
    enc_fmt.qp_min          = 20;
    enc_fmt.avg_bit_rate    = bitrate;
    enc_fmt.maxKeyInterval  = frate;
    enc_fmt.profileIdc      = 66; /* baseline profile */
    enc_fmt.levelIdc        = level;
    
    enc->encdev->IoCtrl(enc->encdev, VENC_SET_ENC_INFO_CMD, (unsigned int)(&enc_fmt));
    ret = enc->encdev->open(enc->encdev);
    if (ret < 0) {
        ALOGD("open H264Enc failed !");
        goto failed;
    }

    return enc;

failed:
    if (enc->encdev) {
        H264EncExit(enc->encdev);
    }
    free(enc);
    return NULL;
}

void h264hwenc_cedarx_close(void *ctxt)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return;

    if (enc->encdev) {
        enc->encdev->close(enc->encdev);
        H264EncExit(enc->encdev);
    }
    free(enc);
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

