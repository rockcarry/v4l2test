// 包含头文件
#include <stdlib.h>
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
    AVFrame           *aframe0;
    AVFrame           *aframe1;
    int64_t            next_apts;
    int                samples_filled;

    AVStream          *vstream;
    AVFrame           *vframe;
    int64_t            next_vpts;

    AVFormatContext   *ofctxt;
    AVCodec           *acodec;
    AVCodec           *vcodec;
    AVDictionary      *avopt;

    int                have_audio;
    int                have_video;
} FFENCODER;

// 内部全局变量定义
static FFENCODER_PARAMS DEF_FFENCODER_PARAMS =
{
    "/sdcard/test.mp4", // filename
    64000,              // audio_bitrate
    44100,              // sample_rate
    AV_CH_LAYOUT_STEREO,// audio stereo
    0,                  // start_apts
    512000,             // video_bitrate
    320,                // video_width
    240,                // video_height
    30,                 // frame_rate
    AV_PIX_FMT_BGRA,    // pixel_fmt
    SWS_FAST_BILINEAR,  // scale_flags
    0,                  // start_vpts
};

// 内部函数实现
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
    c->bit_rate    = encoder->params.audio_bitrate;
    c->sample_rate = encoder->params.sample_rate;
    if (encoder->acodec->supported_samplerates)
    {
        c->sample_rate = encoder->acodec->supported_samplerates[0];
        for (i=0; encoder->acodec->supported_samplerates[i]; i++) {
            if (encoder->acodec->supported_samplerates[i] == encoder->params.sample_rate)
                c->sample_rate = encoder->params.sample_rate;
        }
    }

    c->channel_layout = encoder->params.channel_layout;
    if (encoder->acodec->channel_layouts)
    {
        c->channel_layout = encoder->acodec->channel_layouts[0];
        for (i=0; encoder->acodec->channel_layouts[i]; i++) {
            if ((int)encoder->acodec->channel_layouts[i] == encoder->params.channel_layout)
                c->channel_layout = encoder->params.channel_layout;
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
    c->bit_rate = encoder->params.video_bitrate;
    /* Resolution must be a multiple of two. */
    c->width    = encoder->params.video_width;
    c->height   = encoder->params.video_height;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    encoder->vstream->time_base = (AVRational){ 1, encoder->params.frame_rate };
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

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        ALOGE("error allocating an audio frame\n");
        exit(1);
    }

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

    return frame;
}

static void open_audio(FFENCODER *encoder)
{
    AVCodec        *codec     = encoder->acodec;
    AVDictionary   *opt_arg   = encoder->avopt;
    AVCodecContext *c         = encoder->astream->codec;
    AVDictionary   *opt       = NULL;
    int             in_sfmt   = AV_SAMPLE_FMT_S16;
    int             in_layout = encoder->params.channel_layout;
    int             in_rate   = encoder->params.sample_rate;
    int             in_chnb   = av_get_channel_layout_nb_channels(in_layout);
    int             ret;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        ALOGE("could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate audio frame */
    encoder->aframe0 = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                         c->sample_rate, c->frame_size);
    encoder->aframe1 = alloc_audio_frame(in_sfmt, in_layout,
                                         in_rate, c->frame_size);

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
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        ALOGE("could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(FFENCODER *encoder)
{
    AVCodec        *codec   = encoder->vcodec;
    AVDictionary   *opt_arg = encoder->avopt;
    AVCodecContext *c       = encoder->vstream->codec;
    AVDictionary   *opt     = NULL;
    int             ret;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        ALOGE("could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    encoder->vframe = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!encoder->vframe) {
        ALOGE("could not allocate video frame\n");
        exit(1);
    }

    encoder->sws_ctx = sws_getContext(c->width, c->height,
                                      encoder->params.pixel_fmt,
                                      c->width, c->height,
                                      c->pix_fmt,
                                      encoder->params.scale_flags, NULL, NULL, NULL);
    if (!encoder->sws_ctx) {
        ALOGE("could not initialize the conversion context\n");
        exit(1);
    }
}

static void close_astream(FFENCODER *encoder)
{
    avcodec_close(encoder->astream->codec);
    av_frame_free(&(encoder->aframe0));
    av_frame_free(&(encoder->aframe1));
    swr_free     (&(encoder->swr_ctx));
}

static void close_vstream(FFENCODER *encoder)
{
    avcodec_close(encoder->vstream->codec);
    av_frame_free(&(encoder->vframe));
    sws_freeContext(encoder->sws_ctx);
}

#define ENABLE_LOG_PACKET 1
#if ENABLE_LOG_PACKET
static void log_packet(AVFormatContext *fmt_ctx, AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    ALOGE("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
#endif

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

#if ENABLE_LOG_PACKET
    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
#endif

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(fmt_ctx, pkt);
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
    if (!params                ) params                = &DEF_FFENCODER_PARAMS;
    if (!params->filename      ) params->filename      = DEF_FFENCODER_PARAMS.filename;
    if (!params->audio_bitrate ) params->audio_bitrate = DEF_FFENCODER_PARAMS.audio_bitrate;
    if (!params->sample_rate   ) params->sample_rate   = DEF_FFENCODER_PARAMS.sample_rate;
    if (!params->channel_layout) params->channel_layout= DEF_FFENCODER_PARAMS.channel_layout;
    if (!params->video_bitrate ) params->video_bitrate = DEF_FFENCODER_PARAMS.video_bitrate;
    if (!params->video_width   ) params->video_width   = DEF_FFENCODER_PARAMS.video_width;
    if (!params->video_height  ) params->video_height  = DEF_FFENCODER_PARAMS.video_height;
    if (!params->frame_rate    ) params->frame_rate    = DEF_FFENCODER_PARAMS.frame_rate;
    if (!params->pixel_fmt     ) params->pixel_fmt     = DEF_FFENCODER_PARAMS.pixel_fmt;
    if (!params->scale_flags   ) params->scale_flags   = DEF_FFENCODER_PARAMS.scale_flags;
    memcpy(&(encoder->params), params, sizeof(FFENCODER_PARAMS));
    encoder->next_apts = params->start_apts;
    encoder->next_vpts = params->start_vpts;

    /* initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    /* allocate the output media context */
    avformat_alloc_output_context2(&(encoder->ofctxt), NULL, NULL, params->filename);
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
        ret = avio_open(&encoder->ofctxt->pb, params->filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            ALOGE("could not open '%s': %s\n", params->filename, av_err2str(ret));
            goto failed;
        }
    }

    /* write the stream header, if any. */
    ret = avformat_write_header(encoder->ofctxt, &(encoder->avopt));
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

void ffencoder_audio(void *ctxt, void *data[8], int nbsample)
{
    AVPacket pkt   = {0};
    int got_packet =  0;
    int ret        =  0;
    int dst_nb_samples;
    int ncopy;
    FFENCODER *encoder = (FFENCODER*)ctxt;
    if (!ctxt) return;

    while (nbsample > 0)
    {
        ncopy = nbsample < (encoder->aframe1->nb_samples - encoder->samples_filled) ?
                nbsample : (encoder->aframe1->nb_samples - encoder->samples_filled);
        memcpy((int16_t*)encoder->aframe1->data[0] + 2 * encoder->samples_filled, (int16_t*)data[0], ncopy * 4);
        encoder->samples_filled += ncopy;
        nbsample                -= ncopy;

        if (encoder->samples_filled == encoder->aframe1->nb_samples)
        {
            /* convert samples from native format to destination codec format, using the resampler */
            /* compute destination number of samples */
            dst_nb_samples = av_rescale_rnd(swr_get_delay(encoder->swr_ctx, encoder->astream->codec->sample_rate) + encoder->samples_filled,
                                            encoder->params.sample_rate, encoder->astream->codec->sample_rate, AV_ROUND_UP);
            av_assert0(dst_nb_samples == encoder->samples_filled);

            /* convert to destination format */
            ret = swr_convert(encoder->swr_ctx, encoder->aframe0->data, dst_nb_samples, (const uint8_t **)encoder->aframe1->data, encoder->aframe1->nb_samples);
            if (ret < 0) {
                ALOGE("error while converting\n");
                exit(1);
            }

            encoder->aframe0->pts = av_rescale_q(encoder->next_apts,
                (AVRational){1, encoder->astream->codec->sample_rate}, encoder->astream->codec->time_base);
            encoder->next_apts += dst_nb_samples;

            /* when we pass a frame to the encoder, it may keep a reference to it
             * internally;
             * make sure we do not overwrite it here
             */
            ret = av_frame_make_writable(encoder->aframe0);
            if (ret < 0) {
                ALOGE("failed to make vframe writable !\n");
                exit(1);
            }

            // encode audio
            ret = avcodec_encode_audio2(encoder->astream->codec, &pkt, encoder->aframe0, &got_packet);
            if (ret < 0) {
                ALOGE("error encoding audio frame: %s\n", av_err2str(ret));
                exit(1);
            }

            // write audio
            if (got_packet) {
                ret = write_frame(encoder->ofctxt, &encoder->astream->codec->time_base, encoder->astream, &pkt);
                if (ret < 0) {
                    ALOGE("error while writing audio frame: %s\n", av_err2str(ret));
                    exit(1);
                }
            }

            // set samples_filled to 0
            encoder->samples_filled = 0;
        }
    }
}

void ffencoder_video(void *ctxt, void *data[8], int linesize[8])
{
    AVPacket pkt   = {0};
    int got_packet =  0;
    int ret        =  0;
    FFENCODER *encoder = (FFENCODER*)ctxt;
    if (!ctxt) return;

    // scale video image
    sws_scale(encoder->sws_ctx, (const uint8_t * const *)data, linesize, 0,
              encoder->vstream->codec->height, encoder->vframe->data, encoder->vframe->linesize);
    encoder->vframe->pts = encoder->next_vpts++; // pts

    // init packet
    av_init_packet(&pkt);

    // encode & write video
    if (encoder->ofctxt->oformat->flags & AVFMT_RAWPICTURE) {
        /* a hack to avoid data copy with some raw video muxers */
        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.data   = (uint8_t*)encoder->vframe;
        pkt.size   = sizeof(AVPicture);
        pkt.pts    = encoder->vframe->pts;
        pkt.dts    = encoder->vframe->pts;
        ret = write_frame(encoder->ofctxt, &(encoder->vstream->codec->time_base), encoder->vstream, &pkt);
    } else {
        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(encoder->vframe);
        if (ret < 0) {
            ALOGE("failed to make vframe writable !\n");
            exit(1);
        }

        /* encode the image */
        ret = avcodec_encode_video2(encoder->vstream->codec, &pkt, encoder->vframe, &got_packet);
        if (ret < 0) {
            ALOGE("error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }

        if (got_packet) {
            ret = write_frame(encoder->ofctxt, &(encoder->vstream->codec->time_base), encoder->vstream, &pkt);
        }
    }

    if (ret < 0) {
        ALOGE("error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
    }
}
