#ifndef __FFRECORDER_H__
#define __FFRECORDER_H__

// 包含头文件
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>

using namespace android;

// 类型定义
typedef struct
{
    // micdev input params
    int   mic_sample_rate;
    int   mic_channel_num;

    // camdev input params
    char *cam_dev_name;
    int   cam_sub_src;
    int   cam_frame_width;
    int   cam_frame_height;
    int   cam_frame_rate;

    // ffencoder output
    int   out_audio_bitrate;
    int   out_audio_chlayout;
    int   out_audio_samprate;
    int   out_video_bitrate;
    int   out_video_width;
    int   out_video_height;
    int   out_video_frate;

    // other params
    int   scale_flags;
    int   audio_buffer_number;
    int   video_buffer_number;
} FFRECORDER_PARAMS;

// 函数定义
void *ffrecorder_init(FFRECORDER_PARAMS *params);
void  ffrecorder_free(void *ctxt);
void  ffrecorder_preview_window(void *ctxt, const sp<ANativeWindow> win);
void  ffrecorder_preview_target(void *ctxt, const sp<IGraphicBufferProducer>& gbp);
void  ffrecorder_preview_start (void *ctxt);
void  ffrecorder_preview_stop  (void *ctxt);
void  ffrecorder_record_start  (void *ctxt, char *filename);
void  ffrecorder_record_stop   (void *ctxt);

#endif


