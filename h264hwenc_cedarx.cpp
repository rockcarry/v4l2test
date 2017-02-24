#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "cedarx/h264enc.h"
#include "ffencoder.h"
#include "h264hwenc.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// 内部类型定义
// h264hwenc context
typedef struct {
    VENC_DEVICE *encdev;
    int          iw;
    int          ih;
    int          ow;
    int          oh;
    uint8_t      sps_pps_buf[32];
    int          sps_pps_len;
    uint8_t     *output_buf;
    int          output_len;
    int          firstframe;
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

    // cedarx hardware init
    cedarx_hardware_init(0);
    ion_alloc_open();

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

    __data_container sps_pps_info;
    memset(&sps_pps_info, 0, sizeof(sps_pps_info));
    enc->encdev->IoCtrl(enc->encdev, VENC_GET_SPS_PPS_DATA, (unsigned int)(&sps_pps_info));
    enc->sps_pps_len = min(sps_pps_info.length, (int)sizeof(enc->sps_pps_buf));
    memcpy(enc->sps_pps_buf, sps_pps_info.data, enc->sps_pps_len);
    /*
    ALOGD("sps_pps length: %d, data: %0x %0x %0x %0x %0x %0x %0x %0x", enc->sps_pps_len,
        enc->sps_pps_buf[0], enc->sps_pps_buf[1], enc->sps_pps_buf[2], enc->sps_pps_buf[3],
        enc->sps_pps_buf[4], enc->sps_pps_buf[5], enc->sps_pps_buf[6], enc->sps_pps_buf[7]);
    */

    enc->firstframe = 1;
    enc->ffencoder  = ffencoder;
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
    if (enc->output_buf) {
        free(enc->output_buf);
    }
    free(enc);

    // cedarx hardware exit
    ion_alloc_close();
    cedarx_hardware_exit(0);
}

int h264hwenc_cedarx_picture_format(void *ctxt)
{
    return AV_PIX_FMT_NV12;
}

int h264hwenc_cedarx_picture_alloc(void *ctxt, AVFrame *frame)
{
    frame->format = AV_PIX_FMT_NONE;
    return 0;
}

int h264hwenc_cedarx_picture_free(void *ctxt, AVFrame *frame)
{
    // do nothing
    return 0;
}

int h264hwenc_cedarx_encode(void *ctxt, AVFrame *frame, int timeout)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    __venc_frmbuf_info fbufinfo;
    memset(&fbufinfo, 0, sizeof(fbufinfo));
    fbufinfo.addrY      = (uint8_t*)frame->data[4];
    fbufinfo.addrCb     = (uint8_t*)frame->data[5];
    fbufinfo.color_fmt  = PIXEL_YUV420;
    fbufinfo.color_space= BT601;
    fbufinfo.pts_valid  = 1;
    fbufinfo.pts        = frame->pts;
    int ret = enc->encdev->encode(enc->encdev, &fbufinfo);
    if (ret < 0) {
        ALOGD("cedarx h264 encode frame failed !");
        return -1;
    }

    //++ wait until encoder has data output
    do {
        ret = enc->encdev->hasOutputStream(enc->encdev);
//      ALOGD("hasOutputStream ret = %d.", ret);
        if (!ret) { usleep(10000); timeout -= 10; }
    } while (!ret && timeout > 0);
    //-- wait until encoder has data output

    if (ret) {
        __vbv_data_ctrl_info_t datainfo;
        enc->encdev->GetBitStreamInfo(enc->encdev, &datainfo);
        if (enc->firstframe) { enc->firstframe = 0; datainfo.keyFrameFlag = 1; }
        int len = datainfo.uSize0 + datainfo.uSize1 + (datainfo.keyFrameFlag ? enc->sps_pps_len : 0);
        if (enc->output_len < len) {
            enc->output_len = len;
//          ALOGD("current buffer len: %d, need buffer len: %d", enc->output_len, len);
            if (!enc->output_buf) free(enc->output_buf);
            enc->output_buf = (uint8_t*)malloc(len);
            if (!enc->output_buf) {
                ALOGD("failed to allocate output buffer %d !.", len);
            }
        }
        if (enc->output_buf) {
            int offset = 0;
            if (datainfo.keyFrameFlag) {
                memcpy(enc->output_buf + offset, enc->sps_pps_buf, enc->sps_pps_len);
                offset += enc->sps_pps_len;
            }
            memcpy(enc->output_buf + offset, datainfo.pData0, datainfo.uSize0); offset += datainfo.uSize0;
            memcpy(enc->output_buf + offset, datainfo.pData1, datainfo.uSize1); offset += datainfo.uSize1;

            AVPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.flags |= datainfo.keyFrameFlag ? AV_PKT_FLAG_KEY : 0;
            pkt.data   = enc->output_buf;
            pkt.size   = offset;
            pkt.pts    = frame->pts;
            pkt.dts    = frame->pts;
            ffencoder_write_video_frame(enc->ffencoder, &pkt);
        }
        enc->encdev->ReleaseBitStreamInfo(enc->encdev, datainfo.idx);
    }

    return -1;
}

