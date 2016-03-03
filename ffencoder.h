#ifndef __FFENCODER_H_
#define __FFENCODER_H_

// 类型定义
typedef struct
{
    // output filename
    char *filename;

    // audio params
    int audio_bitrate;
    int sample_rate;
    int channel_layout;
    int start_apts;

    // video params
    int video_bitrate;
    int video_width;
    int video_height;
    int frame_rate;
    int pixel_fmt;
    int scale_flags;
    int start_vpts;
} FFENCODER_PARAMS;

// 函数声明
void* ffencoder_init (FFENCODER_PARAMS *params);
void  ffencoder_free (void *ctxt);
void  ffencoder_audio(void *ctxt, void *data[8], int nbsample   );
void  ffencoder_video(void *ctxt, void *data[8], int linesize[8]);

#endif


