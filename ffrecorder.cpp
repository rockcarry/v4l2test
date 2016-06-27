// 包含头文件
#include "micdev.h"
#include "camdev.h"
#include "ffencoder.h"
#include "ffrecorder.h"

extern "C" {
#include <libavformat/avformat.h>
}

// 内部类型定义
typedef struct
{
    FFRECORDER_PARAMS params;
    MICDEV           *micdev;
    CAMDEV           *camdev;
    void             *encoder;
    #define FRF_RECORDING   (1 << 1 )
    #define FRF_RECORD_REQ  (1 << 2 )
    #define FRF_RECORD_MACK (1 << 16)
    #define FRF_RECORD_CACK (1 << 17)
    int               state;
} FFRECORDER;

// 内部全局变量定义
static FFRECORDER_PARAMS DEF_FFRECORDER_PARAMS =
{
    // micdev input params
    44100,                      // mic_sample_rate
    2,                          // mic_channel_num

    // camdev input params
    (char*)"/dev/video0",       // cam_dev_name
    0,                          // cam_sub_src
    1280,                       // cam_frame_width
    720,                        // cam_frame_height
    25,                         // cam_frame_rate

    // ffencoder output
    16000,                      // out_audio_bitrate;
    AV_CH_LAYOUT_MONO,          // out_audio_chlayout;
    16000,                      // out_audio_samprate;
    384000,                     // out_video_bitrate;
    320,                        // out_video_width;
    200,                        // out_video_height;
    15,                         // out_video_frate;

    // other params
    SWS_POINT,                  // scale_flags;
    5,                          // audio_buffer_number;
    5,                          // video_buffer_number;
};

// 内部函数实现
// micdev capture callback
int micdev_capture_callback_proc(void *r, void *data[8], int nbsample)
{
    FFRECORDER *recorder = (FFRECORDER*)r;
    if (recorder->state & FRF_RECORD_REQ) {
        recorder->state |= (FRF_RECORDING|FRF_RECORD_MACK);
        if (recorder->state & FRF_RECORD_CACK) {
            recorder->state &= ~(FRF_RECORD_REQ|FRF_RECORD_MACK|FRF_RECORD_CACK);
        }
    }

    if (recorder->state & FRF_RECORDING) {
        ffencoder_audio(recorder->encoder, data, nbsample);
    }

    return 0;
}

// camdev capture callback
int camdev_capture_callback_proc(void *r, void *data[8], int linesize[8])
{
    FFRECORDER *recorder = (FFRECORDER*)r;
    if (recorder->state & FRF_RECORD_REQ) {
        recorder->state |= (FRF_RECORDING|FRF_RECORD_CACK);
        if (recorder->state & FRF_RECORD_MACK) {
            recorder->state &= ~(FRF_RECORD_REQ|FRF_RECORD_MACK|FRF_RECORD_CACK);
        }
    }

    if (recorder->state & FRF_RECORDING) {
        ffencoder_video(recorder->encoder, data, linesize);
    }

    return 0;
}

// 函数实现
void *ffrecorder_init(FFRECORDER_PARAMS *params)
{
    FFRECORDER *recorder = (FFRECORDER*)malloc(sizeof(FFRECORDER));
    if (recorder) memset(recorder, 0, sizeof(FFRECORDER));
    else return NULL;

    // using default params if not set
    if (!params                      ) params                       = &DEF_FFRECORDER_PARAMS;
    if (!params->mic_sample_rate     ) params->mic_sample_rate      = DEF_FFRECORDER_PARAMS.mic_sample_rate;
    if (!params->mic_sample_rate     ) params->mic_channel_num      = DEF_FFRECORDER_PARAMS.mic_channel_num;
    if (!params->cam_dev_name        ) params->cam_dev_name         = DEF_FFRECORDER_PARAMS.cam_dev_name;
    if (!params->cam_sub_src         ) params->cam_sub_src          = DEF_FFRECORDER_PARAMS.cam_sub_src;
    if (!params->cam_frame_width     ) params->cam_frame_width      = DEF_FFRECORDER_PARAMS.cam_frame_width;
    if (!params->cam_frame_height    ) params->cam_frame_height     = DEF_FFRECORDER_PARAMS.cam_frame_height;
    if (!params->cam_frame_rate      ) params->cam_frame_rate       = DEF_FFRECORDER_PARAMS.cam_frame_rate;
    if (!params->out_audio_bitrate   ) params->out_audio_bitrate    = DEF_FFRECORDER_PARAMS.out_audio_bitrate;
    if (!params->out_audio_chlayout  ) params->out_audio_chlayout   = DEF_FFRECORDER_PARAMS.out_audio_chlayout;
    if (!params->out_audio_samprate  ) params->out_audio_samprate   = DEF_FFRECORDER_PARAMS.out_audio_samprate;
    if (!params->out_video_bitrate   ) params->out_video_bitrate    = DEF_FFRECORDER_PARAMS.out_video_bitrate;
    if (!params->out_video_width     ) params->out_video_width      = DEF_FFRECORDER_PARAMS.out_video_width;
    if (!params->out_video_height    ) params->out_video_height     = DEF_FFRECORDER_PARAMS.out_video_height;
    if (!params->out_video_frate     ) params->out_video_frate      = DEF_FFRECORDER_PARAMS.out_video_frate;
    if (!params->scale_flags         ) params->scale_flags          = DEF_FFRECORDER_PARAMS.scale_flags;
    if (!params->audio_buffer_number ) params->audio_buffer_number  = DEF_FFRECORDER_PARAMS.audio_buffer_number;
    if (!params->video_buffer_number ) params->video_buffer_number  = DEF_FFRECORDER_PARAMS.video_buffer_number;
    memcpy(&recorder->params, params, sizeof(FFRECORDER_PARAMS));

    recorder->micdev = (MICDEV*)micdev_init(params->mic_sample_rate, params->mic_channel_num);
    if (!recorder->micdev) {
        printf("failed to init micdev !\n");
        exit(1);
    }

    recorder->camdev = camdev_init(params->cam_dev_name, params->cam_sub_src,
        params->cam_frame_width, params->cam_frame_height, params->cam_frame_rate);
    if (!recorder->camdev) {
        printf("failed to init camdev !\n");
        exit(1);
    }

    // start micdev capture
    micdev_start_capture(recorder->micdev);

    // start camdev capture
    camdev_capture_start(recorder->camdev);

    // set callback
    micdev_set_callback(recorder->micdev, micdev_capture_callback_proc, recorder);
    camdev_set_callback(recorder->camdev, camdev_capture_callback_proc, recorder);

    // get actual frame rate
    while (recorder->camdev->act_frate == 0) usleep(10*1000);

    return recorder;
}

void ffrecorder_free(void *ctxt)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;

    // stop micdev capture
    micdev_stop_capture(recorder->micdev);

    // stop camdev capture
    camdev_capture_stop(recorder->camdev);

    // free camdev & micdev
    camdev_close(recorder->camdev);
    micdev_close(recorder->micdev);

    // free context
    free(recorder);
}

void ffrecorder_preview_window(void *ctxt, const sp<ANativeWindow> win)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;
    camdev_set_preview_window(recorder->camdev, win);
}

void ffrecorder_preview_target(void *ctxt, const sp<IGraphicBufferProducer>& gbp)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;
    camdev_set_preview_target(recorder->camdev, gbp);
}

void ffrecorder_preview_start(void *ctxt)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;
    camdev_preview_start(recorder->camdev);
}

void ffrecorder_preview_stop(void *ctxt)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;
    camdev_preview_stop(recorder->camdev);
}

void ffrecorder_record_start(void *ctxt, char *filename)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    void       *last_enc = NULL;
    if (!recorder) return;

    // switch to a new ffencoder for recording
    FFENCODER_PARAMS encoder_params;
    encoder_params.in_audio_channel_layout = AV_CH_LAYOUT_STEREO;
    encoder_params.in_audio_sample_fmt     = AV_SAMPLE_FMT_S16;
    encoder_params.in_audio_sample_rate    = recorder->params.mic_sample_rate;
    encoder_params.in_video_width          = recorder->camdev->cam_w;
    encoder_params.in_video_height         = recorder->camdev->cam_h;
    encoder_params.in_video_pixfmt         = v4l2dev_pixfmt_to_ffmpeg_pixfmt(recorder->camdev->cam_pixfmt);
    encoder_params.in_video_frame_rate     = recorder->camdev->act_frate;
    encoder_params.out_filename            = filename;
    encoder_params.out_audio_bitrate       = recorder->params.out_audio_bitrate;
    encoder_params.out_audio_channel_layout= recorder->params.out_audio_chlayout;
    encoder_params.out_audio_sample_rate   = recorder->params.out_audio_samprate;
    encoder_params.out_video_bitrate       = recorder->params.out_video_bitrate;
    encoder_params.out_video_width         = recorder->params.out_video_width;
    encoder_params.out_video_height        = recorder->params.out_video_height;
    encoder_params.out_video_frame_rate    = recorder->params.out_video_frate;
    encoder_params.start_apts              = 0;
    encoder_params.start_vpts              = 0;
    encoder_params.scale_flags             = recorder->params.scale_flags;
    encoder_params.audio_buffer_number     = recorder->params.audio_buffer_number;
    encoder_params.video_buffer_number     = recorder->params.video_buffer_number;
    last_enc          = recorder->encoder;
    recorder->encoder = ffencoder_init(&encoder_params);
    if (!recorder->encoder) {
        printf("failed to init encoder !\n");
        exit(1);
    }

    // set request recording flag and wait switch done
    recorder->state |= FRF_RECORD_REQ;
    while (recorder->state & FRF_RECORD_REQ) usleep(10*1000);

    // free last encoder
    ffencoder_free(last_enc);
}

void ffrecorder_record_stop(void *ctxt)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    void       *last_enc = NULL;
    if (!recorder) return;

    last_enc          = recorder->encoder;
    recorder->encoder = NULL;

    recorder->state |= FRF_RECORD_REQ;
    while (recorder->state & FRF_RECORD_REQ) usleep(10*1000);
    recorder->state &=~FRF_RECORDING;

    // free last encoder
    ffencoder_free(last_enc);
}

