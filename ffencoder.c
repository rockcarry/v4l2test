// 包含头文件
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <utils/Log.h>
#include "ffencoder.h"

// 内部类型定义
typedef struct
{
    FFENCODER_PARAMS   params;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;

    AVStream          *astream;
    AVFrame           *aframes;
    int64_t            next_apts;
    uint8_t           *adatacur[8];
    int                asamplenum;
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

    AVFormatContext   *ofctxt;
    AVCodec           *acodec;
    AVCodec           *vcodec;
    AVDictionary      *avopt;

    int                have_audio;
    int                have_video;

    #define FFENCODER_TS_EXIT    (1 << 0)
    int                thread_state;
    pthread_t          aencode_thread_id;
    pthread_t          vencode_thread_id;
} FFENCODER;

// 内部全局变量定义
static FFENCODER_PARAMS DEF_FFENCODER_PARAMS =
{
    // input params
    AV_CH_LAYOUT_STEREO,// in_audio_channel_layout
    AV_SAMPLE_FMT_S16,  // in_audio_sample_fmt
    44100,              // in_audio_sample_rate
    640,                // in_video_width
    480,                // in_video_height
    AV_PIX_FMT_YUYV422, // in_video_pixfmt

    // output params
    "/sdcard/test.mp4", // filename
    32000,              // out_audio_bitrate
    AV_CH_LAYOUT_MONO,  // out_audio_channel_layout
    44100,              // out_audio_sample_rate
    256000,             // out_video_bitrate
    320,                // out_video_width
    240,                // out_video_height
    20,                 // out_video_frame_rate

    // other params
    0,                  // start_apts
    0,                  // start_vpts
    SWS_FAST_BILINEAR,  // scale_flags
    5,                  // audio_buffer_number
    5,                  // video_buffer_number
};

// 内部函数实现
static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

static void* audio_encode_thread_proc(void *param)
{
    FFENCODER *encoder = (FFENCODER*)param;
    AVFrame   *aframe  = NULL;
    AVPacket   pkt     = {0};
    int        got     =  0;
    int        ret     =  0;

    while (1) {
        if (encoder->thread_state & FFENCODER_TS_EXIT) {
            break;
        }

        if (0 != sem_trywait(&encoder->asemr)) {
            usleep(33*1000);
            continue;
        }
        else {
            aframe = &encoder->aframes[encoder->ahead];
        }

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(aframe);
        if (ret < 0) {
            ALOGE("failed to make aframe writable !\n");
            exit(1);
        }

        // encode audio
        ret = avcodec_encode_audio2(encoder->astream->codec, &pkt, aframe, &got);
        if (ret < 0) {
            ALOGE("error encoding audio frame: %s\n", av_err2str(ret));
            exit(1);
        }

        // write audio
        if (got) {
            ret = write_frame(encoder->ofctxt, &encoder->astream->codec->time_base, encoder->astream, &pkt);
            if (ret < 0) {
                ALOGE("error while writing audio frame: %s\n", av_err2str(ret));
                exit(1);
            }
        }

        if (++encoder->ahead == encoder->params.audio_buffer_number) {
            encoder->ahead = 0;
        }
        sem_post(&encoder->asemw);
    }

    return NULL;
}

static void* video_encode_thread_proc(void *param)
{
    FFENCODER *encoder = (FFENCODER*)param;
    AVFrame   *vframe  = NULL;
    AVPacket   pkt     = {0};
    int        got     =  0;
    int        ret     =  0;

    while (1) {
        if (encoder->thread_state & FFENCODER_TS_EXIT) {
            break;
        }

        if (0 != sem_trywait(&encoder->vsemr)) {
            usleep(33*1000);
            continue;
        }
        else {
            vframe = &encoder->vframes[encoder->vhead];
        }

        // encode & write video
        if (encoder->ofctxt->oformat->flags & AVFMT_RAWPICTURE) {
            /* a hack to avoid data copy with some raw video muxers */
            pkt.flags |= AV_PKT_FLAG_KEY;
            pkt.data   = (uint8_t*)vframe;
            pkt.size   = sizeof(AVPicture);
            pkt.pts    = vframe->pts;
            pkt.dts    = vframe->pts;
            ret = write_frame(encoder->ofctxt, &(encoder->vstream->codec->time_base), encoder->vstream, &pkt);
        } else {
            /* when we pass a frame to the encoder, it may keep a reference to it
             * internally;
             * make sure we do not overwrite it here
             */
            ret = av_frame_make_writable(vframe);
            if (ret < 0) {
                ALOGE("failed to make vframe writable !\n");
                exit(1);
            }

            /* encode the image */
            ret = avcodec_encode_video2(encoder->vstream->codec, &pkt, vframe, &got);
            if (ret < 0) {
                ALOGE("error encoding video frame: %s\n", av_err2str(ret));
                exit(1);
            }

            if (got) {
                ret = write_frame(encoder->ofctxt, &(encoder->vstream->codec->time_base), encoder->vstream, &pkt);
            }
        }

        if (ret < 0) {
            ALOGE("error while writing video frame: %s\n", av_err2str(ret));
            exit(1);
        }

        if (++encoder->vhead == encoder->params.video_buffer_number) {
            encoder->vhead = 0;
        }
        sem_post(&encoder->vsemw);
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
        ALOGE("could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        return -1;
    }

    encoder->astream = avformat_new_stream(encoder->ofctxt, encoder->acodec);
    if (!encoder->astream) {
        ALOGE("could not allocate stream\n");
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
    encoder->astream->time_base = (AVRational){ 1, c->sample_rate };

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
        ALOGE("could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        return -1;
    }

    encoder->vstream = avformat_new_stream(encoder->ofctxt, encoder->vcodec);
    if (!encoder->vstream) {
        ALOGE("could not allocate stream\n");
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
    encoder->vstream->time_base = (AVRational){ 1, encoder->params.out_video_frame_rate };
    c->time_base = encoder->vstream->time_base;
    c->gop_size  = 12; /* emit one intra frame every twelve frames at most */
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
            ALOGE("error allocating an audio buffer\n");
            exit(1);
        }
    }
}

static void open_audio(FFENCODER *encoder)
{
    AVCodec        *codec     = encoder->acodec;
    AVDictionary   *opt_arg   = encoder->avopt;
    AVCodecContext *c         = encoder->astream->codec;
    AVDictionary   *opt       = NULL;
    int             in_layout = encoder->params.in_audio_channel_layout;
    int             in_sfmt   = encoder->params.in_audio_sample_fmt;
    int             in_rate   = encoder->params.in_audio_sample_rate;
    int             in_chnb   = av_get_channel_layout_nb_channels(in_layout);
    int             i, ret;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        ALOGE("could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* create resampler context */
    encoder->swr_ctx = swr_alloc();
    if (!encoder->swr_ctx) {
        ALOGE("could not allocate resampler context\n");
        exit(1);
    }

    /* set options */
    av_opt_set_int       (encoder->swr_ctx, "in_channel_count",  in_chnb,        0);
    av_opt_set_int       (encoder->swr_ctx, "in_sample_rate",    in_rate,        0);
    av_opt_set_sample_fmt(encoder->swr_ctx, "in_sample_fmt",     in_sfmt,        0);
    av_opt_set_int       (encoder->swr_ctx, "out_channel_count", c->channels,    0);
    av_opt_set_int       (encoder->swr_ctx, "out_sample_rate",   c->sample_rate, 0);
    av_opt_set_sample_fmt(encoder->swr_ctx, "out_sample_fmt",    c->sample_fmt,  0);

    /* initialize the resampling context */
    if ((ret = swr_init(encoder->swr_ctx)) < 0) {
        ALOGE("failed to initialize the resampling context\n");
        exit(1);
    }

    encoder->aframes = (AVFrame*)malloc(sizeof(AVFrame) * encoder->params.audio_buffer_number);
    if (!encoder->aframes) {
        ALOGE("failed to allocate memory for aframes !\n");
        exit(1);
    }
    for (i=0; i<encoder->params.audio_buffer_number; i++) {
        alloc_audio_frame(&encoder->aframes[i],
            c->sample_fmt,
            c->channel_layout,
            c->sample_rate,
            c->frame_size);
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
        ALOGE("could not allocate frame data.\n");
        exit(1);
    }
}

static void open_video(FFENCODER *encoder)
{
    AVCodec        *codec   = encoder->vcodec;
    AVDictionary   *opt_arg = encoder->avopt;
    AVCodecContext *c       = encoder->vstream->codec;
    AVDictionary   *opt     = NULL;
    int             i, ret;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        ALOGE("could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    encoder->sws_ctx = sws_getContext(
        encoder->params.in_video_width,
        encoder->params.in_video_height,
        encoder->params.in_video_pixfmt,
        c->width,
        c->height,
        c->pix_fmt,
        encoder->params.scale_flags,
        NULL, NULL, NULL);
    if (!encoder->sws_ctx) {
        ALOGE("could not initialize the conversion context\n");
        exit(1);
    }

    encoder->vframes = (AVFrame*)malloc(sizeof(AVFrame) * encoder->params.video_buffer_number);
    if (!encoder->vframes) {
        ALOGE("failed to allocate memory for vframes !\n");
        exit(1);
    }
    for (i=0; i<encoder->params.video_buffer_number; i++) {
        alloc_picture(&encoder->vframes[i], c->pix_fmt, c->width, c->height);
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

    //++ for audio frames
    for (i=0; i<encoder->params.audio_buffer_number; i++) {
        AVFrame *frame = &encoder->aframes[i];
        av_frame_free(&frame);
    }
    free(encoder->aframes);
    //-- for audio frames

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

    //++ for video frames
    for (i=0; i<encoder->params.video_buffer_number; i++) {
        AVFrame *frame = &encoder->vframes[i];
        av_frame_free(&frame);
    }
    free(encoder->vframes);
    //-- for video frames

    avcodec_close(encoder->vstream->codec);
    sws_freeContext(encoder->sws_ctx);
}


// 函数实现
void* ffencoder_init(FFENCODER_PARAMS *params)
{
    int ret;

    // allocate context for ffencoder
    FFENCODER *encoder = malloc(sizeof(FFENCODER));
    if (encoder) memset(encoder, 0, sizeof(FFENCODER));
    else return NULL;

    // using default params if not set
    if (!params                          ) params                          = &DEF_FFENCODER_PARAMS;
    if (!params->in_audio_channel_layout ) params->in_audio_channel_layout = DEF_FFENCODER_PARAMS.in_audio_channel_layout;
    if (!params->in_audio_sample_fmt     ) params->in_audio_sample_fmt     = DEF_FFENCODER_PARAMS.in_audio_sample_fmt;
    if (!params->in_audio_sample_rate    ) params->in_audio_sample_rate    = DEF_FFENCODER_PARAMS.in_audio_sample_rate;
    if (!params->in_video_width          ) params->in_video_width          = DEF_FFENCODER_PARAMS.in_video_width;
    if (!params->in_video_height         ) params->in_video_height         = DEF_FFENCODER_PARAMS.in_video_height;
    if (!params->in_video_pixfmt         ) params->in_video_pixfmt         = DEF_FFENCODER_PARAMS.in_video_pixfmt;
    if (!params->out_filename            ) params->out_filename            = DEF_FFENCODER_PARAMS.out_filename;
    if (!params->out_audio_bitrate       ) params->out_audio_bitrate       = DEF_FFENCODER_PARAMS.out_audio_bitrate;
    if (!params->out_audio_channel_layout) params->out_audio_channel_layout= DEF_FFENCODER_PARAMS.out_audio_channel_layout;
    if (!params->out_audio_sample_rate   ) params->out_audio_sample_rate   = DEF_FFENCODER_PARAMS.out_audio_sample_rate;
    if (!params->out_video_bitrate       ) params->out_video_bitrate       = DEF_FFENCODER_PARAMS.out_video_bitrate;
    if (!params->out_video_width         ) params->out_video_width         = DEF_FFENCODER_PARAMS.out_video_width;
    if (!params->out_video_height        ) params->out_video_height        = DEF_FFENCODER_PARAMS.out_video_height;
    if (!params->out_video_frame_rate    ) params->out_video_frame_rate    = DEF_FFENCODER_PARAMS.out_video_frame_rate;
    if (!params->start_apts              ) params->start_apts              = DEF_FFENCODER_PARAMS.start_apts;
    if (!params->start_vpts              ) params->start_vpts              = DEF_FFENCODER_PARAMS.start_vpts;
    if (!params->scale_flags             ) params->scale_flags             = DEF_FFENCODER_PARAMS.scale_flags;
    if (!params->audio_buffer_number     ) params->audio_buffer_number     = DEF_FFENCODER_PARAMS.audio_buffer_number;
    if (!params->video_buffer_number     ) params->video_buffer_number     = DEF_FFENCODER_PARAMS.video_buffer_number;
    memcpy(&encoder->params, params, sizeof(FFENCODER_PARAMS));
    encoder->next_apts = params->start_apts;
    encoder->next_vpts = params->start_vpts;

    /* initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    /* allocate the output media context */
    avformat_alloc_output_context2(&encoder->ofctxt, NULL, NULL, params->out_filename);
    if (!encoder->ofctxt)
    {
        ALOGE("could not deduce output format from file extension: using MPEG.\n");
        goto failed;
    }

    /* add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (add_astream(encoder) < 0)
    {
        ALOGE("failed to add audio stream.\n");
        goto failed;
    }

    if (add_vstream(encoder) < 0)
    {
        ALOGE("failed to add video stream.\n");
        goto failed;
    }

    /* now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (encoder->have_audio) open_audio(encoder);
    if (encoder->have_video) open_video(encoder);

    /* open the output file, if needed */
    if (!(encoder->ofctxt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&encoder->ofctxt->pb, params->out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            ALOGE("could not open '%s': %s\n", params->out_filename, av_err2str(ret));
            goto failed;
        }
    }

    /* write the stream header, if any. */
    ret = avformat_write_header(encoder->ofctxt, &encoder->avopt);
    if (ret < 0) {
        ALOGE("error occurred when opening output file: %s\n", av_err2str(ret));
        goto failed;
    }

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

    AVPacket pkt = {0};
    int      got =  0;

    do {
        avcodec_encode_audio2(encoder->astream->codec, &pkt, NULL, &got);
        if (got) {
            write_frame(encoder->ofctxt, &encoder->astream->codec->time_base, encoder->astream, &pkt);
        }
    } while (got);

    do {
        avcodec_encode_video2(encoder->vstream->codec, &pkt, NULL, &got);
        if (got) {
            write_frame(encoder->ofctxt, &encoder->vstream->codec->time_base, encoder->vstream, &pkt);
        }
    } while (got);

    /* close each codec. */
    if (encoder->have_audio) close_astream(encoder);
    if (encoder->have_video) close_vstream(encoder);

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
}

int ffencoder_audio(void *ctxt, void *data[8], int nbsample)
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    AVFrame   *aframe  = NULL;
    int        sampnum, i;

    if (!ctxt) return -1;

    do {
        // resample audio
        if (encoder->asamplenum == 0) {
            if (sem_trywait(&encoder->asemw) != 0) {
                return -1;
            }
            else {
                aframe = &encoder->aframes[encoder->atail];
            }

            for (i=0; i<8; i++) {
                encoder->adatacur[i] = aframe->data[i];
            }
            encoder->asamplenum = aframe->nb_samples;
        }

        sampnum  = swr_convert(encoder->swr_ctx, encoder->adatacur,
                        encoder->asamplenum, (const uint8_t**)data, nbsample);
        data     = NULL;
        nbsample = 0;
        for (i=0; i<8; i++) {
            encoder->adatacur[i] += sampnum * encoder->astream->codec->channels * 2;
        }
        encoder->asamplenum -= sampnum;

        if (encoder->asamplenum == 0) {
            aframe->pts = av_rescale_q(encoder->next_apts,
                (AVRational){1, encoder->astream->codec->sample_rate}, encoder->astream->codec->time_base);
            encoder->next_apts += aframe->nb_samples;

            if (++encoder->atail == encoder->params.audio_buffer_number) {
                encoder->atail = 0;
            }
            sem_post(&encoder->asemr);
        }
    } while (sampnum > 0);

    return 0;
}

int ffencoder_video(void *ctxt, void *data[8], int linesize[8])
{
    FFENCODER *encoder = (FFENCODER*)ctxt;
    AVFrame   *vframe  = NULL;
    if (!ctxt) return -1;

    if (sem_trywait(&encoder->vsemw) != 0) {
        return -1;
    }
    else {
        vframe = &encoder->vframes[encoder->vtail];
    }

    // scale video image
    sws_scale(
        encoder->sws_ctx,
        (const uint8_t * const *)data,
        linesize,
        0,
        encoder->params.out_video_height,
        vframe->data,
        vframe->linesize);
    vframe->pts = encoder->next_vpts++;

    if (++encoder->vtail == encoder->params.video_buffer_number) {
        encoder->vtail = 0;
    }

    sem_post(&encoder->vsemr);
    return 0;
}
