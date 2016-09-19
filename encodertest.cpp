// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "ffencoder.h"

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
    void     *adata   [8] = { abuf };
    void     *vdata   [8] = { vbuf };
    int       linesize[8] = { 320*2};
    int       i;

    printf("encode start\n");
    FFENCODER_PARAMS param = {
        // input params
        AV_CH_LAYOUT_MONO,         // in_audio_channel_layout
        AV_SAMPLE_FMT_S16,         // in_audio_sample_fmt
        44100,                     // in_audio_sample_rate
        320,                       // in_video_width
        240,                       // in_video_height
        AV_PIX_FMT_YUYV422,        // in_video_pixfmt
        30,                        // in_video_frame_rate

        // output params
        (char*)"/sdcard/test.mp4", // filename
        32000,                     // out_audio_bitrate
        AV_CH_LAYOUT_MONO,         // out_audio_channel_layout
        44100,                     // out_audio_sample_rate
        256000,                    // out_video_bitrate
        320,                       // out_video_width
        240,                       // out_video_height
        25,                        // out_video_frame_rate
        320,                       // out_jpeg_width
        240,                       // out_jpeg_height

        // other params
        0,                         // start_apts
        0,                         // start_vpts
        SWS_POINT,                 // scale_flags
        5,                         // audio_buffer_number
        5,                         // video_buffer_number
    };
    encoder = ffencoder_init(&param);

    for (i=0; i<1800; i++)
    {
        rand_buf(abuf, sizeof(abuf));
        rand_buf(vbuf, sizeof(vbuf));
        ffencoder_audio(encoder, adata, 44100/30);
        ffencoder_video(encoder, vdata, linesize);
    }

    ffencoder_jpeg(encoder, "/sdcard/test.jpg", vdata, linesize);
    ffencoder_free(encoder);
    printf("encode done\n");
    return 0;
}
