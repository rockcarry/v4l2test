#ifndef __FFENCODER_H_
#define __FFENCODER_H_

#ifdef __cplusplus
extern "C" {
#endif

// 类型定义
typedef struct
{
    // input params
    int   in_audio_channel_layout;
    int   in_audio_sample_fmt;
    int   in_audio_sample_rate;
    int   in_video_width;
    int   in_video_height;
    int   in_video_pixfmt;

    // output params
    char *out_filename;
    int   out_audio_bitrate;
    int   out_audio_channel_layout;
    int   out_audio_sample_rate;
    int   out_video_bitrate;
    int   out_video_width;
    int   out_video_height;
    int   out_video_frame_rate;

    // other params
    int   start_apts;
    int   start_vpts;
    int   scale_flags;
    int   audio_buffer_number;
    int   video_buffer_number;
} FFENCODER_PARAMS;

// 函数声明
void* ffencoder_init (FFENCODER_PARAMS *params);
void  ffencoder_free (void *ctxt);
int   ffencoder_audio(void *ctxt, void *data[8], int nbsample   );
int   ffencoder_video(void *ctxt, void *data[8], int linesize[8]);

#ifdef __cplusplus
}
#endif

#endif


