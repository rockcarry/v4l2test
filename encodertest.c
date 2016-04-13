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
    static uint8_t abuf[44100 / 30 * 2 * 2];
    static uint8_t vbuf[320 * 240 + 320 * 240 / 2];
    void     *encoder = NULL;
    void     *adata   [8] = { abuf };
    void     *vdata   [8] = { vbuf, vbuf + 320 * 240, vbuf + 320 * 240 + 320 * 240 / 4 };
    int       linesize[8] = { 320, 320 / 2, 320 / 2 };
    int       i;

    printf("encode start\n");
    FFENCODER_PARAMS param = {
        "/sdcard/test.mp4", // filename
        128000,             // audio_bitrate
        44100,              // sample_rate
        AV_CH_LAYOUT_STEREO,// audio stereo
        0,                  // start_apts
        512000,             // video_bitrate
        320,                // video_width
        240,                // video_height
        30,                 // frame_rate
        AV_PIX_FMT_YUV420P, // pixel_fmt
        SWS_POINT,          // scale_flags
        0,                  // start_vpts
    };
    encoder = ffencoder_init(&param);

    for (i=0; i<900; i++)
    {
        rand_buf(abuf, sizeof(abuf));
        ffencoder_audio(encoder, adata, 44100/30);

        rand_buf(vbuf, sizeof(vbuf));
        ffencoder_video(encoder, vdata, linesize);
    }

    ffencoder_free(encoder);
    printf("encode done\n");
    return 0;
}
