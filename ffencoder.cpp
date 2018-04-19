// 包含头文件
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include "h264hwenc.h"
#include "ffutils.h"
#include "ffencoder.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#ifdef USE_MEDIACODEC_H264ENC
#include <jni.h>
extern    JavaVM* g_jvm;
JNIEXPORT JNIEnv* get_jni_env(void);
#endif

//++ frame dropper
typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    int e;
} FRAMEDROPPER;

static void frame_dropper_init   (FRAMEDROPPER *pfd, int frate_in, int frate_out);
static int  frame_dropper_clocked(FRAMEDROPPER *pfd);

static void frame_dropper_init(FRAMEDROPPER *pfd, int frate_in, int frate_out)
{
    pfd->x  = 0;
    pfd->y  = 0;
    pfd->e  = 0;
    pfd->dx = frate_in;
    pfd->dy = frate_out;
}

static int frame_dropper_clocked(FRAMEDROPPER *pfd)
{
    pfd->x++;
    pfd->e += pfd->dy;
    if (pfd->e * 2 >= pfd->dx) {
        pfd->y++;
        pfd->e -= pfd->dx;
//      printf("%d, %d\n", pfd->x, pfd->y);
        return 1;
    }
    return 0;
}
//-- frame dropper

// 内部类型定义
typedef struct
{
    FFENCODER_PARAMS   params;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;

    AVStream          *astream;
    AVFrame           *aframes;
    int64_t            next_apts;
    AVFrame           *aframecur;
    int                asampavail;
    sem_t              asemr;
    sem_t              asemw;
    int                ahead;
    int                atail;

    AVStream          *vstream;
    AVFrame           *vframes;
    int64_t            next_vpts;
    sem_t              vsemr;
    sem_t              vsemw;
    int                vhead;
    int                vtail;
    FRAMEDROPPER       vdropper;

    AVFormatContext   *ofctxt;
    AVCodec           *acodec;
    AVCodec           *vcodec;
    void              *vhwenc;

    int                have_audio;
    int                have_video;

    #define FFENCODER_TS_EXIT    (1 << 0)
    int                thread_state;
    pthread_t          aencode_thread_id;
    pthread_t          vencode_thread_id;

    //++ for packet queue
    #define PKT_QUEUE_SIZE 64
    AVPacket           pktq_b[PKT_QUEUE_SIZE]; // packets
    AVPacket          *pktq_f[PKT_QUEUE_SIZE]; // free packets queue
    AVPacket          *pktq_w[PKT_QUEUE_SIZE]; // used packets queue
    sem_t              pktq_semf;  // free
    sem_t              pktq_semw;  // used
    int                pktq_headf;
    int                pktq_tailf;
    int                pktq_headw;
    int                pktq_tailw;
    pthread_t          pktq_thread_id;
    pthread_mutex_t    pktq_mutex;
    //-- for packet queue
} FFENCODER;

// 内部全局变量定义
static FFENCODER_PARAMS DEF_FFENCODER_PARAMS =
{
    // input params
    AV_CH_LAYOUT_STEREO,        // in_audio_channel_layout
    AV_SAMPLE_FMT_S16,          // in_audio_sample_fmt
    44100,                      // in_audio_sample_rate
    640,                        // in_video_width
    480,                        // in_video_height
    AV_PIX_FMT_YUYV422,         // in_video_pixfmt
    30,                         // in_video_frame_rate_num
    1,                          // in_video_frame_rate_den

    // output params
    (char*)"/sdcard/test.mp4",  // filename
    22050,                      // out_audio_bitrate
    AV_CH_LAYOUT_MONO,          // out_audio_channel_layout
    22050,                      // out_audio_sample_rate
    256000,                     // out_video_bitrate
    320,                        // out_video_width
    240,                        // out_video_height
    20,                         // out_video_frame_rate_num
    1,                          // out_video_frame_rate_den

    // other params
    SWS_FAST_BILINEAR,          // scale_flags
    6,                          // audio_buffer_number
    6,                          // video_buffer_number
    0,                          // enable_h264hwenc
};

// 内部函数实现
//++ video packet queue
static AVPacket* avpacket_dequeue(FFENCODER *encoder)
{
    AVPacket *pkt = NULL;
    sem_wait(&encoder->pktq_semf);
    pthread_mutex_lock  (&encoder->pktq_mutex);
    pkt = encoder->pktq_f[encoder->pktq_headf];
    if (++encoder->pktq_headf == PKT_QUEUE_SIZE) {
        encoder->pktq_headf = 0;
    }
    pthread_mutex_unlock(&encoder->pktq_mutex);
    return pkt;
}

static void avpacket_enqueue(FFENCODER *encoder, AVPacket *pkt, AVStream *st)
{
    av_packet_rescale_ts(pkt, st->codec->time_base, st->time_base);
    pkt->stream_index = st->index;
    pthread_mutex_lock  (&encoder->pktq_mutex);
    encoder->pktq_w[encoder->pktq_tailw] = pkt;
    if (++encoder->pktq_tailw == PKT_QUEUE_SIZE) {
        encoder->pktq_tailw = 0;
    }
    pthread_mutex_unlock(&encoder->pktq_mutex);
    sem_post(&encoder->pktq_semw);
}

static void avpacket_cancel(FFENCODER *encoder, AVPacket *pkt)
{
    pthread_mutex_lock  (&encoder->pktq_mutex);
    encoder->pktq_f[encoder->pktq_tailf] = pkt;
    if (++encoder->pktq_tailf == PKT_QUEUE_SIZE) {
        encoder->pktq_tailf = 0;
    }
    pthread_mutex_unlock(&encoder->pktq_mutex);
    sem_post(&encoder->pktq_semf);
}
//-- video packet queue

static void* audio_encode_thread_proc(void *param)
{
    FFENCODER *encoder = (FFENCODER*)param;
    AVFrame   *aframe  = NULL;
    AVPacket  *pkt     = NULL;
    int        got     =  0;
    int        ret     =  0;

    while (1) {
        if (0 != sem_trywait(&encoder->asemr)) {
            if (encoder->thread_state & FFENCODER_TS_EXIT) {
                break;
            } else {
                usleep(10*1000);
                continue;
            }
        }

        aframe = &encoder->aframes[encoder->ahead];

        // encode audio
        pkt = avpacket_dequeue(encoder);
        avcodec_encode_audio2(encoder->astream->codec, pkt, aframe, &got);
        if (got) avpacket_enqueue(encoder, pkt, encoder->astream);
        else     avpacket_cancel (encoder, pkt);

        if (++encoder->ahead == encoder->params.audio_buffer_number) {
            encoder->ahead = 0;
        }
        sem_post(&encoder->asemw);
    }

    do {
        pkt = avpacket_dequeue(encoder);
        avcodec_encode_audio2(encoder->astream->codec, pkt, NULL, &got);
        if (got) avpacket_enqueue(encoder, pkt, encoder->astream);
        else     avpacket_cancel (encoder, pkt);
    } while (got);

    return NULL;
}

static void* video_encode_thread_proc(void *param)
{
    FFENCODER *encoder = (FFENCODER*)param;
    AVFrame   *vframe  = NULL;
    AVPacket  *pkt     = NULL;
    int        got     =  0;
    int        ret     =  0;

#ifdef USE_MEDIACODEC_H264ENC
    JNIEnv *env = get_jni_env();
#endif

    while (1) {
        if (0 != sem_trywait(&encoder->vsemr)) {
            if (encoder->thread_state & FFENCODER_TS_EXIT) {
                break;
            } else {
                usleep(10*1000);
                continue;
            }
        }

        vframe = &encoder->vframes[encoder->vhead];

        // encode & write video
        if (encoder->params.in_video_pixfmt == V4L2_PIX_FMT_MJPEG) {
            ffencoder_write_video_frame(encoder, AV_PKT_FLAG_KEY, vframe->data[0], vframe->pkt_size, vframe->pts);
        } else if (encoder->params.enable_h264hwenc) {
            h264hwenc_encode(encoder->vhwenc, vframe, 1000);
        } else {
            pkt = avpacket_dequeue(encoder);
            avcodec_encode_video2(encoder->vstream->codec, pkt, vframe, &got);
            if (got) avpacket_enqueue(encoder, pkt, encoder->vstream);
            else     avpacket_cancel (encoder, pkt);
        }

        if (++encoder->vhead == encoder->params.video_buffer_number) {
            encoder->vhead = 0;
        }
        sem_post(&encoder->vsemw);
    }

    if (  encoder->params.in_video_pixfmt != V4L2_PIX_FMT_MJPEG
       && encoder->params.enable_h264hwenc != 1) {
        do {
            pkt = avpacket_dequeue(encoder);
            avcodec_encode_video2(encoder->vstream->codec, pkt, NULL, &got);
            if (got) avpacket_enqueue(encoder, pkt, encoder->vstream);
            else     avpacket_cancel (encoder, pkt);
        } while (got);
    }

#ifdef USE_MEDIACODEC_H264ENC
    g_jvm->DetachCurrentThread();
#endif

    return NULL;
}

static void* packet_thread_proc(void *param)
{
    FFENCODER *encoder = (FFENCODER*)param;
    AVPacket  *packet  = NULL;
    int        ret;
    int        i;

    while (1) {
        if (0 != sem_trywait(&encoder->pktq_semw)) {
            if (encoder->thread_state & FFENCODER_TS_EXIT) {
                break;
            } else {
                usleep(10*1000);
                continue;
            }
        }

        // dequeue packet from pktq_w
        pthread_mutex_lock  (&encoder->pktq_mutex);
        packet = encoder->pktq_w[encoder->pktq_headw];
        if (++encoder->pktq_headw == PKT_QUEUE_SIZE) {
            encoder->pktq_headw = 0;
        }
        pthread_mutex_unlock(&encoder->pktq_mutex);

        // write packet
        av_interleaved_write_frame(encoder->ofctxt, packet);

        // enqueue packet to pktq_f
        pthread_mutex_lock  (&encoder->pktq_mutex);
        encoder->pktq_f[encoder->pktq_tailf] = packet;
        if (++encoder->pktq_tailf == PKT_QUEUE_SIZE) {
            encoder->pktq_tailf = 0;
        }
        pthread_mutex_unlock(&encoder->pktq_mutex);

        sem_post(&encoder->pktq_semf);
    }

    return NULL;
}

static int add_astream(FFENCODER *encoder)
{
    enum AVCodecID  codec_id = encoder->ofctxt->oformat->audio_codec;
    AVCodecContext *c        = NULL;
    int i;

    if (codec_id == AV_CODEC_ID_NONE) return 0;

    encoder->acodec = avcodec_find_encoder(codec_id);
    if (!encoder->acodec) {
        printf("could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        return -1;
    }

    encoder->astream = avformat_new_stream(encoder->ofctxt, encoder->acodec);
    if (!encoder->astream) {
        printf("could not allocate stream\n");
        return -1;
    }

    encoder->astream->id = encoder->ofctxt->nb_streams - 1;
    c                    = encoder->astream->codec;

    c->sample_fmt  = encoder->acodec->sample_fmts ? encoder->acodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    c->bit_rate    = encoder->params.out_audio_bitrate;
    c->sample_rate = encoder->params.out_audio_sample_rate;
    if (encoder->acodec->supported_samplerates)
    {
        c->sample_rate = encoder->acodec->supported_samplerates[0];
        for (i=0; encoder->acodec->supported_samplerates[i]; i++) {
            if (encoder->acodec->supported_samplerates[i] == encoder->params.out_audio_sample_rate)
                c->sample_rate = encoder->params.out_audio_sample_rate;
        }
    }

    c->channel_layout = encoder->params.out_audio_channel_layout;
    if (encoder->acodec->channel_layouts)
    {
        c->channel_layout = encoder->acodec->channel_layouts[0];
        for (i=0; encoder->acodec->channel_layouts[i]; i++) {
            if ((int)encoder->acodec->channel_layouts[i] == encoder->params.out_audio_channel_layout)
                c->channel_layout = encoder->params.out_audio_channel_layout;
        }
    }
    c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
    encoder->astream->time_base.num = 1;
    encoder->astream->time_base.den = c->sample_rate;

    /* some formats want stream headers to be separate. */
    if (encoder->ofctxt->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    encoder->have_audio = 1;
    return 0;
}

static int add_vstream(FFENCODER *encoder)
{
    enum AVCodecID  codec_id = encoder->ofctxt->oformat->video_codec;
    AVCodecContext *c        = NULL;

    if (codec_id == AV_CODEC_ID_NONE) return 0;

    encoder->vcodec = avcodec_find_encoder(codec_id);
    if (!encoder->vcodec) {
        printf("could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        return -1;
    }

    encoder->vstream = avformat_new_stream(encoder->ofctxt, encoder->vcodec);
    if (!encoder->vstream) {
        printf("could not allocate stream\n");
        return -1;
    }

    encoder->vstream->id = encoder->ofctxt->nb_streams - 1;
    c                    = encoder->vstream->codec;

    c->codec_id = codec_id;
    c->bit_rate = encoder->params.out_video_bitrate;
    /* Resolution must be a multiple of two. */
    c->width    = encoder->params.out_video_width;
    c->height   = encoder->params.out_video_height;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    encoder->vstream->time_base.num = encoder->params.out_video_frame_rate_den;
    encoder->vstream->time_base.den = encoder->params.out_video_frame_rate_num;
    c->time_base = encoder->vstream->time_base;
    c->gop_size  = encoder->params.out_video_frame_rate_num / encoder->params.out_video_frame_rate_den;
    c->pix_fmt   = AV_PIX_FMT_YUV420P;
    if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        /* needed to avoid using macroblocks in which some coeffs overflow.
         * This does not happen with normal video, it just happens here as
         * the motion of the chroma plane does not match the luma plane. */
        c->mb_decision = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MJPEG) {
        c->pix_fmt = AV_PIX_FMT_YUVJ420P;
    }

    /* some formats want stream headers to be separate. */
    if (encoder->ofctxt->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    encoder->have_video = 1;
    return 0;
}

static void alloc_audio_frame(AVFrame *frame, enum AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples)
{
    int ret;

    frame->format         = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate    = sample_rate;
    frame->nb_samples     = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            printf("error allocating an audio buffer\n");
            exit(1);
        }
    }
}

static void open_audio(FFENCODER *encoder)
{
    AVCodec        *codec     = encoder->acodec;
    AVCodecContext *c         = encoder->astream->codec;
    int             in_layout = encoder->params.in_audio_channel_layout;
    AVSampleFormat  in_sfmt   = (AVSampleFormat)encoder->params.in_audio_sample_fmt;
    int             in_rate   = encoder->params.in_audio_sample_rate;
    int             i, ret;

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        printf("could not open audio codec !\n");
        exit(1);
    }

    /* create resampler context */
    encoder->swr_ctx = swr_alloc_set_opts(NULL,
        c->channel_layout, c->sample_fmt, c->sample_rate,
        in_layout, in_sfmt, in_rate, 0, NULL);
    if (!encoder->swr_ctx) {
        printf("could not allocate resampler context\n");
        exit(1);
    }

    /* initialize the resampling context */
    if ((ret = swr_init(encoder->swr_ctx)) < 0) {
        printf("failed to initialize the resampling context\n");
        exit(1);
    }

    encoder->aframes = (AVFrame*)calloc(encoder->params.audio_buffer_number, sizeof(AVFrame));
    if (!encoder->aframes) {
        printf("failed to allocate memory for aframes !\n");
        exit(1);
    }

    for (i=0; i<encoder->params.audio_buffer_number; i++) {
        alloc_audio_frame(&encoder->aframes[i],
            c->sample_fmt,
            c->channel_layout,
            c->sample_rate,
            c->frame_size);
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(encoder->astream->codecpar, c);
    if (ret < 0) {
        printf("could not copy the stream parameters !\n");
    }

    sem_init(&encoder->asemr, 0, 0                                  );
    sem_init(&encoder->asemw, 0, encoder->params.audio_buffer_number);

    // create audio encoding thread
    pthread_create(&encoder->aencode_thread_id, NULL, audio_encode_thread_proc, encoder);
}

static void alloc_picture(AVFrame *picture, enum AVPixelFormat pix_fmt, int width, int height)
{
    int ret;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        printf("could not allocate frame data.\n");
        exit(1);
    }
}

static void open_video(FFENCODER *encoder)
{
    AVCodec        *codec   = encoder->vcodec;
    AVCodecContext *c       = encoder->vstream->codec;
    AVDictionary   *param   = NULL;
    int             i, ret;

    if (c->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset" , "ultrafast", 0);
        av_dict_set(&param, "profile", "baseline" , 0);
        av_dict_set(&param, "tune", "zerolatency" , 0);
    }

    /* open the codec */
    ret = avcodec_open2(c, codec, &param);
    if (ret < 0) {
        printf("could not open video codec !\n");
        exit(1);
    }

    encoder->sws_ctx = sws_getContext(
        encoder->params.in_video_width,
        encoder->params.in_video_height,
        (AVPixelFormat)encoder->params.in_video_pixfmt,
        c->width,
        c->height,
        (AVPixelFormat)(encoder->params.enable_h264hwenc ? h264hwenc_picture_format(encoder->vhwenc) : c->pix_fmt),
        encoder->params.scale_flags,
        NULL, NULL, NULL);
    if (!encoder->sws_ctx) {
        printf("could not initialize the conversion context mp4\n");
        exit(1);
    }

    encoder->vframes = (AVFrame*)calloc(encoder->params.video_buffer_number, sizeof(AVFrame));
    if (!encoder->vframes) {
        printf("failed to allocate memory for vframes !\n");
        exit(1);
    }

    for (i=0; i<encoder->params.video_buffer_number; i++) {
        if (encoder->params.in_video_pixfmt == V4L2_PIX_FMT_MJPEG) {
            alloc_picture(&encoder->vframes[i], AV_PIX_FMT_RGB24, c->width, c->height);
        } else if (encoder->params.enable_h264hwenc) {
            h264hwenc_picture_alloc(encoder->vhwenc, &encoder->vframes[i]);
        } else {
            alloc_picture(&encoder->vframes[i], c->pix_fmt, c->width, c->height);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(encoder->vstream->codecpar, c);
    if (ret < 0) {
        printf("could not copy the stream parameters !\n");
    }

    sem_init(&encoder->vsemr, 0, 0                                  );
    sem_init(&encoder->vsemw, 0, encoder->params.video_buffer_number);

    // create video encoding thread
    pthread_create(&encoder->vencode_thread_id, NULL, video_encode_thread_proc, encoder);
}

static void close_astream(FFENCODER *encoder)
{
    int i;

    encoder->thread_state |= FFENCODER_TS_EXIT;
    pthread_join(encoder->aencode_thread_id, NULL);

    sem_destroy(&encoder->asemr);
    sem_destroy(&encoder->asemw);

    //+ free audio frames
    for (i=0; i<encoder->params.audio_buffer_number; i++) {
        av_frame_unref(&encoder->aframes[i]);
    }
    free(encoder->aframes);
    //- free audio frames

    avcodec_close(encoder->astream->codec);
    swr_free(&encoder->swr_ctx);
}

static void close_vstream(FFENCODER *encoder)
{
    int i;

    encoder->thread_state |= FFENCODER_TS_EXIT;
    pthread_join(encoder->vencode_thread_id, NULL);

    sem_destroy(&encoder->vsemr);
    sem_destroy(&encoder->vsemw);

    for (i=0; i<encoder->params.audio_buffer_number; i++) {
        if (encoder->params.enable_h264hwenc) {
            h264hwenc_picture_free(encoder->vhwenc, &encoder->vframes[i]);
        } else {
            av_frame_unref(&encoder->vframes[i]);
        }
    }

    avcodec_close(encoder->vstream->codec);
    sws_freeContext(encoder->sws_ctx);
}

static void ffplayer_log_callback(void* ptr, int level, const char *fmt, va_list vl) {
    if (level <= av_log_get_level()) {
        char str[1024];
        vsprintf(str, fmt, vl);
        ALOGD("%s", str);
    }
}

// 函数实现
void* ffencoder_init(FFENCODER_PARAMS *params)
{
    char *str;
    int   len;
    int   ret;

    // allocate context for ffencoder
    FFENCODER *encoder = (FFENCODER*)calloc(1, sizeof(FFENCODER));
    if (!encoder) {
        return NULL;
    }

    // using default params if not set
    if (!params                          ) params                          =&DEF_FFENCODER_PARAMS;
    if (!params->in_audio_channel_layout ) params->in_audio_channel_layout = DEF_FFENCODER_PARAMS.in_audio_channel_layout;
    if (!params->in_audio_sample_fmt     ) params->in_audio_sample_fmt     = DEF_FFENCODER_PARAMS.in_audio_sample_fmt;
    if (!params->in_audio_sample_rate    ) params->in_audio_sample_rate    = DEF_FFENCODER_PARAMS.in_audio_sample_rate;
    if (!params->in_video_width          ) params->in_video_width          = DEF_FFENCODER_PARAMS.in_video_width;
    if (!params->in_video_height         ) params->in_video_height         = DEF_FFENCODER_PARAMS.in_video_height;
    if (!params->in_video_pixfmt         ) params->in_video_pixfmt         = DEF_FFENCODER_PARAMS.in_video_pixfmt;
    if (!params->in_video_frame_rate_num ) params->in_video_frame_rate_num = DEF_FFENCODER_PARAMS.in_video_frame_rate_num;
    if (!params->in_video_frame_rate_den ) params->in_video_frame_rate_den = DEF_FFENCODER_PARAMS.in_video_frame_rate_den;
    if (!params->out_filename            ) params->out_filename            = DEF_FFENCODER_PARAMS.out_filename;
    if (!params->out_audio_bitrate       ) params->out_audio_bitrate       = DEF_FFENCODER_PARAMS.out_audio_bitrate;
    if (!params->out_audio_channel_layout) params->out_audio_channel_layout= DEF_FFENCODER_PARAMS.out_audio_channel_layout;
    if (!params->out_audio_sample_rate   ) params->out_audio_sample_rate   = DEF_FFENCODER_PARAMS.out_audio_sample_rate;
    if (!params->out_video_bitrate       ) params->out_video_bitrate       = DEF_FFENCODER_PARAMS.out_video_bitrate;
    if (!params->out_video_width         ) params->out_video_width         = DEF_FFENCODER_PARAMS.out_video_width;
    if (!params->out_video_height        ) params->out_video_height        = DEF_FFENCODER_PARAMS.out_video_height;
    if (!params->out_video_frame_rate_num) params->out_video_frame_rate_num= DEF_FFENCODER_PARAMS.out_video_frame_rate_num;
    if (!params->out_video_frame_rate_den) params->out_video_frame_rate_den= DEF_FFENCODER_PARAMS.out_video_frame_rate_den;
    if (!params->scale_flags             ) params->scale_flags             = DEF_FFENCODER_PARAMS.scale_flags;
    if (!params->audio_buffer_number     ) params->audio_buffer_number     = DEF_FFENCODER_PARAMS.audio_buffer_number;
    if (!params->video_buffer_number     ) params->video_buffer_number     = DEF_FFENCODER_PARAMS.video_buffer_number;
    if (!params->enable_h264hwenc        ) params->enable_h264hwenc        = DEF_FFENCODER_PARAMS.enable_h264hwenc;
    memcpy(&encoder->params, params, sizeof(FFENCODER_PARAMS));

    /* initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    // init network
    avformat_network_init();

    // setup log
//  av_log_set_level(AV_LOG_WARNING);
//  av_log_set_callback(ffplayer_log_callback);

    /* allocate the output media context */
    avformat_alloc_output_context2(&encoder->ofctxt, NULL, NULL, params->out_filename);
    if (!encoder->ofctxt)
    {
        printf("could not deduce output format from file extension: using MPEG.\n");
        goto failed;
    }

    encoder->ofctxt->oformat->audio_codec = AV_CODEC_ID_AAC;
    encoder->ofctxt->oformat->video_codec = encoder->params.in_video_pixfmt == V4L2_PIX_FMT_MJPEG ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_H264;

    /* init h264hwenc */
    encoder->vhwenc = h264hwenc_init(
        encoder->params.in_video_width,
        encoder->params.in_video_height,
        encoder->params.out_video_width,
        encoder->params.out_video_height,
        encoder->params.out_video_frame_rate_num / encoder->params.out_video_frame_rate_den,
        encoder->params.out_video_bitrate,
        encoder);

    /* add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (add_astream(encoder) < 0)
    {
        printf("failed to add audio stream.\n");
        goto failed;
    }

    if (add_vstream(encoder) < 0)
    {
        printf("failed to add video stream.\n");
        goto failed;
    }

    // for packet queue
    pthread_mutex_init(&encoder->pktq_mutex, NULL  );
    sem_init(&encoder->pktq_semf, 0, PKT_QUEUE_SIZE);
    sem_init(&encoder->pktq_semw, 0, 0             );
    for (int i=0; i<PKT_QUEUE_SIZE; i++) {
        encoder->pktq_f[i] = &encoder->pktq_b[i];
    }

    // create video packet writing thread
    pthread_create(&encoder->pktq_thread_id, NULL, packet_thread_proc, encoder);

    /* now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (encoder->have_audio) open_audio(encoder);
    if (encoder->have_video) open_video(encoder);

    /* open the output file, if needed */
    if (!(encoder->ofctxt->oformat->flags & AVFMT_NOFILE)) {
        AVDictionary *param = NULL;
//      av_dict_set_int(&param, "blocksize", 128*1024, AV_OPT_FLAG_ENCODING_PARAM);
        ret = avio_open2(&encoder->ofctxt->pb, params->out_filename, AVIO_FLAG_WRITE, NULL, &param);
        if (ret < 0) {
            printf("could not open '%s' !\n", params->out_filename);
            goto failed;
        }
    }

    /* write the stream header, if any. */
    ret = avformat_write_header(encoder->ofctxt, NULL);
    if (ret < 0) {
        printf("error occurred when opening output file !\n");
        goto failed;
    }

    // init frame dropper
    frame_dropper_init(&encoder->vdropper,
        params->in_video_frame_rate_num / params->in_video_frame_rate_den,
        params->out_video_frame_rate_num/ params->out_video_frame_rate_den);

    // successed
    return encoder;

failed:
    ffencoder_free(encoder);
    return NULL;
}

void ffencoder_free(void *ctxt)
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    if (!ctxt) return;

    /* close each codec. */
    if (encoder->have_audio) close_astream(encoder);
    if (encoder->have_video) close_vstream(encoder);

    /* close h264hwenc */
    h264hwenc_close(encoder->vhwenc);

    encoder->thread_state |= FFENCODER_TS_EXIT;
    pthread_join(encoder->pktq_thread_id, NULL);
    pthread_mutex_destroy(&encoder->pktq_mutex);
    sem_destroy(&encoder->pktq_semf);
    sem_destroy(&encoder->pktq_semw);
    for (int i=0; i<PKT_QUEUE_SIZE; i++) {
        av_packet_unref(&encoder->pktq_b[i]);
    }

    /* write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(encoder->ofctxt);

    /* close the output file. */
    if (!(encoder->ofctxt->oformat->flags & AVFMT_NOFILE)) avio_close(encoder->ofctxt->pb);

    /* free the stream */
    avformat_free_context(encoder->ofctxt);

    // free encoder context
    free(encoder);

    // deinit network
    avformat_network_deinit();
}

int ffencoder_audio(void *ctxt, void *data[AV_NUM_DATA_POINTERS], int nbsample, int64_t pts)
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    uint8_t   *adatacur[AV_NUM_DATA_POINTERS];
    int        sampnum, i;
    if (!ctxt) return -1;

    do {
        // resample audio
        if (encoder->asampavail == 0) {
            if (0 != sem_wait(&encoder->asemw)) {
                ALOGD("audio frame dropped by encoder !\n");
                return -1;
            }
            encoder->aframecur  = &encoder->aframes[encoder->atail];
            encoder->asampavail =  encoder->aframes[encoder->atail].nb_samples;
        }

        for (i=0; i<8; i++) {
            adatacur[i] = encoder->aframecur->data[i]
                        + (encoder->aframecur->nb_samples - encoder->asampavail)
                            * av_get_bytes_per_sample(encoder->astream->codec->sample_fmt)
                            * encoder->astream->codec->channels;
        }

        sampnum  = swr_convert(encoder->swr_ctx, adatacur, encoder->asampavail, (const uint8_t**)data, nbsample);
        data     = NULL;
        nbsample = 0;
        encoder->asampavail -= sampnum;

        if (encoder->asampavail == 0) {
            encoder->aframecur->pts = encoder->next_apts;
            encoder->next_apts     += encoder->aframecur->nb_samples;

            if (++encoder->atail == encoder->params.audio_buffer_number) {
                encoder->atail = 0;
            }
            sem_post(&encoder->asemr);
        }
    } while (sampnum > 0);

    return 0;
}

int ffencoder_video(void *ctxt, void *data[AV_NUM_DATA_POINTERS], int linesize[AV_NUM_DATA_POINTERS], int64_t pts)
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    AVFrame   *vframe  = NULL;
    int        drop    = 0;
    if (!ctxt) return -1;

    drop = !frame_dropper_clocked(&encoder->vdropper);
    if (drop) {
//      printf("frame dropped by frame dropper !\n");
        return 0;
    }

    if (0 != sem_trywait(&encoder->vsemw)) {
        ALOGD("video frame dropped by encoder !\n");
        encoder->next_vpts++;
        return -1;
    }

    // vframe pts
    vframe = &encoder->vframes[encoder->vtail];
    if (pts == -1) {
        vframe->pts = encoder->next_vpts++;
    } else {
        vframe->pts = av_rescale_q_rnd(pts, AV_TIME_BASE_Q, encoder->vstream->codec->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        vframe->pts = vframe->pts < encoder->next_vpts ? encoder->next_vpts : vframe->pts;
        encoder->next_vpts = vframe->pts + 1;
    }

    if (encoder->params.in_video_pixfmt == V4L2_PIX_FMT_MJPEG) {
        uint8_t *srcbuf = (uint8_t*)data[0];
        int      srclen = linesize[AV_NUM_DATA_POINTERS-1];
        uint8_t *dstbuf = (uint8_t*)vframe->data[0];
        int      dstlen = vframe->linesize[0] * vframe->height;
        int      cpylen = dstlen < srclen ? dstlen : srclen;
        memcpy(dstbuf, srcbuf, cpylen);
        vframe->pkt_size = cpylen;
    } else {
#ifdef USE_MEDIACODEC_H264ENC
        memcpy(vframe->data[0], data[0], linesize[AV_NUM_DATA_POINTERS-1]);
#elif defined USE_MEDIASERVER_H264ENC
        memcpy(vframe->data, data, sizeof(void*) * AV_NUM_DATA_POINTERS);
#else
        sws_scale(
            encoder->sws_ctx,
            (const uint8_t * const *)data,
            linesize,
            0,
            encoder->params.in_video_height,
            vframe->data,
            vframe->linesize);
#endif
    }

    if (++encoder->vtail == encoder->params.video_buffer_number) {
        encoder->vtail = 0;
    }

    sem_post(&encoder->vsemr);
    return 0;
}

int ffencoder_write_video_frame(void *ctxt, int flags, void *data, int size, int64_t pts)
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    AVPacket  *packet  = NULL;
    packet = avpacket_dequeue(encoder);
    av_new_packet(packet, size);
    memcpy(packet->data, data, size);
    packet->flags |= flags;
    packet->pts    = pts;
    packet->dts    = pts;
    avpacket_enqueue(encoder, packet, encoder->vstream);
    return 0;
}

