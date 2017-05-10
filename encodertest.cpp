// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include "ffjpeg.h"
#include "ffencoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

static void rand_buf(void *buf, int size)
{
    uint32_t *ptr32 = (uint32_t*)buf;
    while (size) {
        *ptr32++ = rand();
        size -= 4;
    }
}

int main(void)
{
    static uint8_t abuf[44100 / 30 * 2 * 1];
    static uint8_t vbuf[320 * 240 * 2];
    void     *encoder = NULL;
    void     *jpegenc = NULL;
    void     *adata   [AV_NUM_DATA_POINTERS] = { abuf };
    void     *vdata   [AV_NUM_DATA_POINTERS] = { vbuf };
    int       linesize[AV_NUM_DATA_POINTERS] = { 320*2};
    int       i;
    AVFrame   frame;

    printf("encode start\n");
    FFENCODER_PARAMS param = {
        // input params
        AV_CH_LAYOUT_MONO,         // in_audio_channel_layout
        AV_SAMPLE_FMT_S16,         // in_audio_sample_fmt
        44100,                     // in_audio_sample_rate
        320,                       // in_video_width
        240,                       // in_video_height
        AV_PIX_FMT_YUYV422,        // in_video_pixfmt
        30,                        // in_video_frame_rate_num
        1,                         // in_video_frame_rate_den

        // output params
        (char*)"/sdcard/test.mp4", // filename
        32000,                     // out_audio_bitrate
        AV_CH_LAYOUT_MONO,         // out_audio_channel_layout
        44100,                     // out_audio_sample_rate
        256000,                    // out_video_bitrate
        320,                       // out_video_width
        240,                       // out_video_height
        25,                        // out_video_frame_rate_num
        1,                         // out_video_frame_rate_den

        // other params
        SWS_POINT,                 // scale_flags
        5,                         // audio_buffer_number
        5,                         // video_buffer_number
        1,                         // timebase by frame rate
        0,                         // video_encoder_type
    };
    encoder = ffencoder_init(&param);

    for (i=0; i<1800; i++)
    {
        rand_buf(abuf, sizeof(abuf));
        rand_buf(vbuf, sizeof(vbuf));
        ffencoder_audio(encoder, adata, 44100/30, -1);
        ffencoder_video(encoder, vdata, linesize, -1);
    }

    jpegenc = ffjpeg_encoder_init();
    frame.format   = param.in_video_pixfmt;
    frame.width    = param.in_video_width;
    frame.height   = param.in_video_height;
    memcpy(frame.data,     vdata   , sizeof(vdata   ));
    memcpy(frame.linesize, linesize, sizeof(linesize));
    ffjpeg_encoder_encode(jpegenc, "/sdcard/test.jpg", frame.width, frame.height, &frame);
    ffjpeg_encoder_free(jpegenc);

    ffencoder_free(encoder);
    printf("encode done\n");
    return 0;
}
