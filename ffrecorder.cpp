// 包含头文件
#include <limits.h>
#include "micdev.h"
#include "camdev.h"
#include "ffutils.h"
#include "ffjpeg.h"
#include "ffencoder.h"
#include "ffrecorder.h"

extern "C" {
#include <libavformat/avformat.h>
}

// 内部类型定义
typedef struct
{
    FFRECORDER_PARAMS params;
    void             *micdev [MAX_MICDEV_NUM ];
    void             *camdev [MAX_CAMDEV_NUM ];
    int               rsvpts [MAX_CAMDEV_NUM ]; // record start video pts
    void             *encoder[MAX_ENCODER_NUM]; // encoders current running
    void             *enclose[MAX_ENCODER_NUM]; // encoders need to be closed when switch next video file
    #define FRF_RECORDING   (1 << 1)
    int               state;

    int audio_source[MAX_ENCODER_NUM];
    int video_source[MAX_ENCODER_NUM];

    void *jpgenc;
    char  take_photo_file[MAX_CAMDEV_NUM][PATH_MAX];
    int   take_photo_flags;
} FFRECORDER;

// 内部全局变量定义
static FFRECORDER_PARAMS DEF_FFRECORDER_PARAMS =
{
    // micdev input params
    44100,                      // mic_sample_rate
#ifdef USE_MICDEV_ANDROID
    1,                          // mic_channel_num
#else
    2,                          // mic_channel_num
#endif

    // camdev input params
    (char*)"/dev/video0",       // cam_dev_name_0
    0,                          // cam_sub_src_0
    1920,                       // cam_frame_width_0
    1080,                       // cam_frame_height_0
    25,                         // cam_frame_rate_0

    // camdev input params
    (char*)"/dev/video2",       // cam_dev_name_1
    0,                          // cam_sub_src_1
    640,                        // cam_frame_width_1
    480,                        // cam_frame_height_1
    25,                         // cam_frame_rate_1

    // ffencoder output
    16000,                      // out_audio_bitrate_0
    AV_CH_LAYOUT_MONO,          // out_audio_chlayout_0
    16000,                      // out_audio_samprate_0
    10000000,                   // out_video_bitrate_0
    1920,                       // out_video_width_0
    1080,                       // out_video_height_0
    25,                         // out_video_frate_0

    // ffencoder output
    16000,                      // out_audio_bitrate_1
    AV_CH_LAYOUT_MONO,          // out_audio_chlayout_1
    16000,                      // out_audio_samprate_1
    5000000,                    // out_video_bitrate_1
    1280,                       // out_video_width_1
    720,                        // out_video_height_1
    25,                         // out_video_frate_1

    // ffencoder output
    16000,                      // out_audio_bitrate_2
    AV_CH_LAYOUT_MONO,          // out_audio_chlayout_2
    16000,                      // out_audio_samprate_2
    2000000,                    // out_video_bitrate_2
    640,                        // out_video_width_2
    480,                        // out_video_height_2
    25,                         // out_video_frate_2
};

// 内部函数实现
// micdev capture callback
static int micdev0_capture_callback_proc(void *r, void *data[AV_NUM_DATA_POINTERS], int nbsample)
{
    FFRECORDER *recorder = (FFRECORDER*)r;
    if (recorder->state & FRF_RECORDING) {
        for (int i=0; i<MAX_ENCODER_NUM; i++) {
            if (recorder->audio_source[i] == 0) {
                ffencoder_audio(recorder->encoder[i], data, nbsample, -1);
            }
        }
    }
    return 0;
}

// camdev capture callback
static int camdev0_capture_callback_proc(void *r, void *data[AV_NUM_DATA_POINTERS], int linesize[AV_NUM_DATA_POINTERS], int pts)
{
    FFRECORDER *recorder = (FFRECORDER*)r;
    if (recorder->state & FRF_RECORDING) {
        if (recorder->rsvpts[0] == -1) {
            recorder->rsvpts[0] = pts;
            pts  = 0;
        } else {
            pts -= recorder->rsvpts[0];
        }
        for (int i=0; i<MAX_ENCODER_NUM; i++) {
            if (recorder->video_source[i] == 0) {
                ffencoder_video(recorder->encoder[i], data, linesize, pts);
            }
        }
    }

    // use jpgenc to take photo
    if (recorder->take_photo_flags & (1 << 0)) {
        AVFrame frame;
        frame.format = v4l2dev_pixfmt_to_ffmpeg_pixfmt(camdev_get_param(recorder->camdev[0], CAMDEV_PARAM_VIDEO_PIXFMT));
        frame.width  = recorder->params.cam_frame_width_0 ;
        frame.height = recorder->params.cam_frame_height_0;
        memcpy(frame.data    , data    , sizeof(void*)*AV_NUM_DATA_POINTERS);
        memcpy(frame.linesize, linesize, sizeof(int  )*AV_NUM_DATA_POINTERS);
        ffjpeg_encoder_encode(recorder->jpgenc, recorder->take_photo_file[0], frame.width, frame.height, &frame);
        recorder->take_photo_flags &= ~(1 << 0);
    }
    return 0;
}

static int camdev1_capture_callback_proc(void *r, void *data[AV_NUM_DATA_POINTERS], int linesize[AV_NUM_DATA_POINTERS], int pts)
{
    FFRECORDER *recorder = (FFRECORDER*)r;
    if (recorder->state & FRF_RECORDING) {
        if (recorder->rsvpts[1] == -1) {
            recorder->rsvpts[1] = pts;
            pts  = 0;
        } else {
            pts -= recorder->rsvpts[1];
        }
        for (int i=0; i<MAX_ENCODER_NUM; i++) {
            if (recorder->video_source[i] == 1) {
                ffencoder_video(recorder->encoder[i], data, linesize, pts);
            }
        }
    }

    // use jpgenc to take photo
    if (recorder->take_photo_flags & (1 << 1)) {
        AVFrame frame;
        frame.format = v4l2dev_pixfmt_to_ffmpeg_pixfmt(camdev_get_param(recorder->camdev[1], CAMDEV_PARAM_VIDEO_PIXFMT));
        frame.width  = recorder->params.cam_frame_width_1 ;
        frame.height = recorder->params.cam_frame_height_1;
        memcpy(frame.data    , data    , sizeof(data    ));
        memcpy(frame.linesize, linesize, sizeof(linesize));
        ffjpeg_encoder_encode(recorder->jpgenc, recorder->take_photo_file[1], frame.width, frame.height, &frame);
        recorder->take_photo_flags &= ~(1 << 1);
    }
    return 0;
}

// 函数实现
void *ffrecorder_init(FFRECORDER_PARAMS *params, void *extra)
{
    FFRECORDER *recorder = (FFRECORDER*)calloc(1, sizeof(FFRECORDER));
    if (!recorder) {
        return NULL;
    }

    // using default params if not set
    if (!params                      ) params                       =&DEF_FFRECORDER_PARAMS;
    if (!params->mic_sample_rate     ) params->mic_sample_rate      = DEF_FFRECORDER_PARAMS.mic_sample_rate;
    if (!params->mic_channel_num     ) params->mic_channel_num      = DEF_FFRECORDER_PARAMS.mic_channel_num;
    if (!params->cam_dev_name_0      ) params->cam_dev_name_0       = DEF_FFRECORDER_PARAMS.cam_dev_name_0;
    if (!params->cam_sub_src_0       ) params->cam_sub_src_0        = DEF_FFRECORDER_PARAMS.cam_sub_src_0;
    if (!params->cam_frame_width_0   ) params->cam_frame_width_0    = DEF_FFRECORDER_PARAMS.cam_frame_width_0;
    if (!params->cam_frame_height_0  ) params->cam_frame_height_0   = DEF_FFRECORDER_PARAMS.cam_frame_height_0;
    if (!params->cam_frame_rate_0    ) params->cam_frame_rate_0     = DEF_FFRECORDER_PARAMS.cam_frame_rate_0;
    if (!params->cam_dev_name_1      ) params->cam_dev_name_1       = DEF_FFRECORDER_PARAMS.cam_dev_name_1;
    if (!params->cam_sub_src_1       ) params->cam_sub_src_1        = DEF_FFRECORDER_PARAMS.cam_sub_src_1;
    if (!params->cam_frame_width_1   ) params->cam_frame_width_1    = DEF_FFRECORDER_PARAMS.cam_frame_width_1;
    if (!params->cam_frame_height_1  ) params->cam_frame_height_1   = DEF_FFRECORDER_PARAMS.cam_frame_height_1;
    if (!params->cam_frame_rate_1    ) params->cam_frame_rate_1     = DEF_FFRECORDER_PARAMS.cam_frame_rate_1;
    if (!params->out_audio_bitrate_0 ) params->out_audio_bitrate_0  = DEF_FFRECORDER_PARAMS.out_audio_bitrate_0;
    if (!params->out_audio_chlayout_0) params->out_audio_chlayout_0 = DEF_FFRECORDER_PARAMS.out_audio_chlayout_0;
    if (!params->out_audio_samprate_0) params->out_audio_samprate_0 = DEF_FFRECORDER_PARAMS.out_audio_samprate_0;
    if (!params->out_video_bitrate_0 ) params->out_video_bitrate_0  = DEF_FFRECORDER_PARAMS.out_video_bitrate_0;
    if (!params->out_video_width_0   ) params->out_video_width_0    = DEF_FFRECORDER_PARAMS.out_video_width_0;
    if (!params->out_video_height_0  ) params->out_video_height_0   = DEF_FFRECORDER_PARAMS.out_video_height_0;
    if (!params->out_video_frate_0   ) params->out_video_frate_0    = DEF_FFRECORDER_PARAMS.out_video_frate_0;
    if (!params->out_audio_bitrate_1 ) params->out_audio_bitrate_1  = DEF_FFRECORDER_PARAMS.out_audio_bitrate_1;
    if (!params->out_audio_chlayout_1) params->out_audio_chlayout_1 = DEF_FFRECORDER_PARAMS.out_audio_chlayout_1;
    if (!params->out_audio_samprate_1) params->out_audio_samprate_1 = DEF_FFRECORDER_PARAMS.out_audio_samprate_1;
    if (!params->out_video_bitrate_1 ) params->out_video_bitrate_1  = DEF_FFRECORDER_PARAMS.out_video_bitrate_1;
    if (!params->out_video_width_1   ) params->out_video_width_1    = DEF_FFRECORDER_PARAMS.out_video_width_1;
    if (!params->out_video_height_1  ) params->out_video_height_1   = DEF_FFRECORDER_PARAMS.out_video_height_1;
    if (!params->out_video_frate_1   ) params->out_video_frate_1    = DEF_FFRECORDER_PARAMS.out_video_frate_1;
    if (!params->out_audio_bitrate_2 ) params->out_audio_bitrate_2  = DEF_FFRECORDER_PARAMS.out_audio_bitrate_2;
    if (!params->out_audio_chlayout_2) params->out_audio_chlayout_2 = DEF_FFRECORDER_PARAMS.out_audio_chlayout_2;
    if (!params->out_audio_samprate_2) params->out_audio_samprate_2 = DEF_FFRECORDER_PARAMS.out_audio_samprate_2;
    if (!params->out_video_bitrate_2 ) params->out_video_bitrate_2  = DEF_FFRECORDER_PARAMS.out_video_bitrate_2;
    if (!params->out_video_width_2   ) params->out_video_width_2    = DEF_FFRECORDER_PARAMS.out_video_width_2;
    if (!params->out_video_height_2  ) params->out_video_height_2   = DEF_FFRECORDER_PARAMS.out_video_height_2;
    if (!params->out_video_frate_2   ) params->out_video_frate_2    = DEF_FFRECORDER_PARAMS.out_video_frate_2;
    memcpy(&recorder->params, params, sizeof(FFRECORDER_PARAMS));

    recorder->audio_source[0] = 0;
    recorder->audio_source[1] = 0;
    recorder->audio_source[2] =-1;
    recorder->video_source[0] = 0;
    recorder->video_source[1] = 1;
    recorder->video_source[2] =-1;

    recorder->micdev[0] = micdev_init(params->mic_sample_rate, params->mic_channel_num, extra);
    if (!recorder->micdev[0]) {
        printf("failed to init micdev !\n");
    }

    recorder->camdev[0] = camdev_init(params->cam_dev_name_0, params->cam_sub_src_0,
        params->cam_frame_width_0, params->cam_frame_height_0, params->cam_frame_rate_0);
    if (!recorder->camdev[0]) {
        printf("failed to init camdev0 !\n");
    }

    recorder->camdev[1] = camdev_init(params->cam_dev_name_1, params->cam_sub_src_1,
        params->cam_frame_width_1, params->cam_frame_height_1, params->cam_frame_rate_1);
    if (!recorder->camdev[1]) {
        printf("failed to init camdev1 !\n");
    }

    recorder->jpgenc = ffjpeg_encoder_init();
    if (!recorder->jpgenc) {
        printf("failed to init jpgenc !\n");
    }

    // start micdev capture
    micdev_start_capture(recorder->micdev[0]);

    // start camdev capture
    camdev_capture_start(recorder->camdev[0]);
    camdev_capture_start(recorder->camdev[1]);

    // set callback
    micdev_set_callback(recorder->micdev[0], (void*)micdev0_capture_callback_proc, recorder);
    camdev_set_callback(recorder->camdev[0], (void*)camdev0_capture_callback_proc, recorder);
    camdev_set_callback(recorder->camdev[1], (void*)camdev1_capture_callback_proc, recorder);

    return recorder;
}

void ffrecorder_free(void *ctxt)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;

    // stop micdev capture
    micdev_stop_capture(recorder->micdev[0]);

    // stop camdev capture
    camdev_capture_stop(recorder->camdev[0]);
    camdev_capture_stop(recorder->camdev[1]);

    // free camdev & micdev
    micdev_close(recorder->micdev[0]);
    camdev_close(recorder->camdev[0]);
    camdev_close(recorder->camdev[1]);

    // free jpg encoder
    ffjpeg_encoder_free(recorder->jpgenc);

    // free context
    free(recorder);
}

int ffrecorder_get_mic_mute(void *ctxt, int micidx)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    micidx %= MAX_MICDEV_NUM;
    return micdev_get_mute(recorder->micdev[micidx]);
}

void ffrecorder_set_mic_mute(void *ctxt, int micidx, int mute)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    micidx %= MAX_MICDEV_NUM;
    micdev_set_mute(recorder->micdev[micidx], mute);
}

void ffrecorder_reset_camdev(void *ctxt, int camidx, int w, int h, int frate)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    char       *dev_name = NULL;
    int         sub_src  = 0;
    sp<ANativeWindow> win= NULL;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;

    win = camdev_get_preview_window(recorder->camdev[camidx]);
    camdev_capture_stop(recorder->camdev[camidx]);
    camdev_close(recorder->camdev[camidx]);

    switch (camidx) {
    case 0:
        w     = (w     == -1) ? recorder->params.cam_frame_width_0  : w;
        h     = (h     == -1) ? recorder->params.cam_frame_height_0 : h;
        frate = (frate == -1) ? recorder->params.cam_frame_rate_0   : frate;
        dev_name = recorder->params.cam_dev_name_0;
        sub_src  = recorder->params.cam_sub_src_0;
        recorder->params.cam_frame_width_0  = w;
        recorder->params.cam_frame_height_0 = h;
        recorder->params.cam_frame_rate_0   = frate;
        break;
    case 1:
        w     = (w     == -1) ? recorder->params.cam_frame_width_1  : w;
        h     = (h     == -1) ? recorder->params.cam_frame_height_1 : h;
        frate = (frate == -1) ? recorder->params.cam_frame_rate_1   : frate;
        dev_name = recorder->params.cam_dev_name_1;
        sub_src  = recorder->params.cam_sub_src_1;
        recorder->params.cam_frame_width_1  = w;
        recorder->params.cam_frame_height_1 = h;
        recorder->params.cam_frame_rate_1   = frate;
        break;
    }

    recorder->camdev[camidx] = camdev_init(dev_name, sub_src, w, h, frate);
    camdev_capture_start(recorder->camdev[camidx]);
    camdev_set_callback(recorder->camdev[camidx], (void*)(camidx ? camdev1_capture_callback_proc : camdev0_capture_callback_proc), recorder);
    camdev_set_preview_window(recorder->camdev[camidx], win);
}

void ffrecorder_preview_window(void *ctxt, int camidx, const sp<ANativeWindow> win)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;
    camdev_set_preview_window(recorder->camdev[camidx], win);
}

void ffrecorder_preview_target(void *ctxt, int camidx, const sp<IGraphicBufferProducer>& gbp)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;
    camdev_set_preview_target(recorder->camdev[camidx], gbp);
}

void ffrecorder_preview_start(void *ctxt, int camidx)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;
    camdev_preview_start(recorder->camdev[camidx]);
}

void ffrecorder_preview_stop(void *ctxt, int camidx)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;
    camdev_preview_stop(recorder->camdev[camidx]);
}

void ffrecorder_record_start(void *ctxt, int encidx, char *filename)
{
    FFRECORDER *recorder  = (FFRECORDER*)ctxt;
    int         vidsrc    = 0;
    void       *camdev    = NULL;
    int         abitrate  = 0;
    int         achlayout = 0;
    int         asamprate = 0;
    int         vbitrate  = 0;
    int         vwidth    = 0;
    int         vheight   = 0;
    int         vfrate    = 0;
    if (!recorder || encidx < -1 || encidx >= MAX_ENCODER_NUM) return;

    if (encidx == -1) {
        for (int i=0; i<MAX_ENCODER_NUM; i++) {
            ffencoder_free(recorder->enclose[i]);
            recorder->enclose[i] = NULL;
        }
        return;
    }

    vidsrc = recorder->video_source[encidx];
    if (vidsrc >= 0 && vidsrc < MAX_CAMDEV_NUM) {
        camdev = recorder->camdev[vidsrc];
    }

    switch (encidx) {
    case 0:
        abitrate  = recorder->params.out_audio_bitrate_0;
        achlayout = recorder->params.out_audio_chlayout_0;
        asamprate = recorder->params.out_audio_samprate_0;
        vbitrate  = recorder->params.out_video_bitrate_0;
        vwidth    = recorder->params.out_video_width_0;
        vheight   = recorder->params.out_video_height_0;
        vfrate    = recorder->params.out_video_frate_0;
        break;
    case 1:
        abitrate  = recorder->params.out_audio_bitrate_1;
        achlayout = recorder->params.out_audio_chlayout_1;
        asamprate = recorder->params.out_audio_samprate_1;
        vbitrate  = recorder->params.out_video_bitrate_1;
        vwidth    = recorder->params.out_video_width_1;
        vheight   = recorder->params.out_video_height_1;
        vfrate    = recorder->params.out_video_frate_1;
        break;
    case 2:
        abitrate  = recorder->params.out_audio_bitrate_2;
        achlayout = recorder->params.out_audio_chlayout_2;
        asamprate = recorder->params.out_audio_samprate_2;
        vbitrate  = recorder->params.out_video_bitrate_2;
        vwidth    = recorder->params.out_video_width_2;
        vheight   = recorder->params.out_video_height_2;
        vfrate    = recorder->params.out_video_frate_2;
        break;
    }

    // switch to a new ffencoder for recording
    FFENCODER_PARAMS encoder_params;
    encoder_params.in_audio_channel_layout = recorder->params.mic_channel_num == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    encoder_params.in_audio_sample_fmt     = AV_SAMPLE_FMT_S16;
    encoder_params.in_audio_sample_rate    = recorder->params.mic_sample_rate;
    encoder_params.in_video_width          = camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_WIDTH );
    encoder_params.in_video_height         = camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_HEIGHT);
    encoder_params.in_video_pixfmt         = v4l2dev_pixfmt_to_ffmpeg_pixfmt(camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_PIXFMT));
    encoder_params.in_video_frame_rate     = camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_FRATE );
    encoder_params.out_filename            = filename;
    encoder_params.out_audio_bitrate       = abitrate;
    encoder_params.out_audio_channel_layout= achlayout;
    encoder_params.out_audio_sample_rate   = asamprate;
    encoder_params.out_video_bitrate       = vbitrate;
    encoder_params.out_video_width         = vwidth;
    encoder_params.out_video_height        = vheight;
    encoder_params.out_video_frame_rate    = vfrate;
    encoder_params.scale_flags             = 0; // use default
    encoder_params.audio_buffer_number     = 0; // use default
    encoder_params.video_buffer_number     = 0; // use default
    encoder_params.video_timebase_type     = 1; // timebase by frame rate
#ifdef ENABLE_H264_HWENC
    encoder_params.video_encoder_type      = camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_PIXFMT) == V4L2_PIX_FMT_MJPEG ? 2 : 1;
#else
    encoder_params.video_encoder_type      = camdev_get_param(camdev, CAMDEV_PARAM_VIDEO_PIXFMT) == V4L2_PIX_FMT_MJPEG ? 2 : 0;
#endif
    recorder->enclose[encidx] = recorder->encoder[encidx];
    recorder->encoder[encidx] = ffencoder_init(&encoder_params);
    if (!recorder->encoder[encidx]) {
        printf("failed to init encoder !\n");
    }

    // reset record start vpts
    recorder->rsvpts[vidsrc] = -1;
    recorder->state |= (FRF_RECORDING);
}

void ffrecorder_record_stop(void *ctxt, int encidx)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || encidx < -1 || encidx >= MAX_ENCODER_NUM) return;

    if (encidx == -1) {
        for (int i=0; i<MAX_ENCODER_NUM; i++) {
            ffencoder_free(recorder->enclose[i]);
            recorder->enclose[i] = NULL;
        }
        return;
    }

    recorder->enclose[encidx] = recorder->encoder[encidx];
    recorder->encoder[encidx] = NULL;
    recorder->state &=~(FRF_RECORDING);
}

void ffrecorder_record_audio_source(void *ctxt, int encidx, int source)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || encidx < 0 || encidx >= MAX_ENCODER_NUM) return;
    recorder->audio_source[encidx] = source;
}

void ffrecorder_record_video_source(void *ctxt, int encidx, int source)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || encidx < 0 || encidx >= MAX_ENCODER_NUM) return;
    recorder->video_source[encidx] = source;
}

void ffrecorder_take_photo(void *ctxt, int camidx, char *filename)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder || camidx < 0 || camidx >= MAX_CAMDEV_NUM) return;
    strcpy(recorder->take_photo_file[camidx], filename);
    recorder->take_photo_flags |= (1 << camidx);
}

#ifdef ENABLE_MEDIARECORDER_JNI
void ffrecorder_init_jni_callback(void *ctxt, JNIEnv *env, jobject obj)
{
    FFRECORDER *recorder = (FFRECORDER*)ctxt;
    if (!recorder) return;
    ffjpeg_encoder_init_jni_callback(recorder->jpgenc, env, obj);
}
#endif

