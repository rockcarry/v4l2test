// 包含头文件
#include <pthread.h>
#include "ffjpeg.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// 内部类型定义
typedef struct
{
    struct SwsContext *sws_ctx;
    AVPixelFormat      ifmt;
    int                iw;
    int                ih;
    int                ow;
    int                oh;
    AVFrame            picture;
    const char        *file;
    pthread_t          thread_id;

#ifdef ENABLE_MEDIARECORDER_JNI
    jclass     jcls_mr;
    jobject    jobj_mr;
    jmethodID  jmid_cb;
#endif
} JPEGENC;

// 内部函数实现
static void* ffjpeg_encode_thread_proc(void *param)
{
    JPEGENC        *encoder    = (JPEGENC*)param;
    AVCodecContext *codec_ctxt = NULL;
    AVCodec        *codec      = NULL;

    AVPacket packet ;
    int      got = 0;
    int      ret = 0;

#ifdef ENABLE_MEDIARECORDER_JNI
    JNIEnv *env = get_jni_env();
#endif

    // init packet
    memset(&packet, 0, sizeof(AVPacket));

    // do encoding
    codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "failed to find encoder !\n");
        goto done;
    }

    codec_ctxt = avcodec_alloc_context3(codec);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate codec context !\n");
        goto done;
    }
    codec_ctxt->width         = encoder->picture.width;
    codec_ctxt->height        = encoder->picture.height;
    codec_ctxt->time_base.num = 1;
    codec_ctxt->time_base.den = 25;
    codec_ctxt->pix_fmt       = AV_PIX_FMT_YUVJ420P;

    if (avcodec_open2(codec_ctxt, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to open encoder !\n");
        goto done;
    }

    avcodec_encode_video2(codec_ctxt, &packet, &encoder->picture, &got);
    if (got) {
        FILE *fp = fopen(encoder->file, "wb");
        if (fp) {
            fwrite(packet.data, packet.size, 1, fp);
            fclose(fp);
        }
    }

done:
    avcodec_close(codec_ctxt);
    av_free(codec_ctxt);
    av_packet_unref(&packet);
    
#ifdef ENABLE_MEDIARECORDER_JNI
    if (env && encoder->jcls_mr && encoder->jobj_mr && encoder->jmid_cb) {
        env->CallVoidMethod(encoder->jobj_mr, encoder->jmid_cb, env->NewStringUTF(encoder->file), encoder->ow, encoder->oh);
    }

    // need call DetachCurrentThread
    g_jvm->DetachCurrentThread();
#endif

    encoder->file = NULL;
    return NULL;
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

// 函数实现
void* ffjpeg_encoder_init(void)
{
    // allocate context for ffencoder
    JPEGENC *encoder = (JPEGENC*)calloc(1, sizeof(JPEGENC));
    if (!encoder) {
        return NULL;
    }

    // init ffmpeg library
    av_register_all();

    return encoder;
}

int ffjpeg_encoder_encode(void *ctxt, const char *file, int w, int h, AVFrame *frame)
{
    JPEGENC *encoder = (JPEGENC*)ctxt;

    // check valid
    if (!ctxt) return -1;

    // check encoder busy or not
    if (encoder->file) {
        return -1; // encoder busy
    }
    else {
        encoder->file = file;
    }

    if (  frame->format != encoder->ifmt
       || frame->width  != encoder->iw || frame->height != encoder->ih
       || w != encoder->ow || h != encoder->oh ) {

        if (encoder->sws_ctx) {
            sws_freeContext(encoder->sws_ctx);
        }
        encoder->sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
            w, h, AV_PIX_FMT_YUVJ420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (!encoder->sws_ctx) {
            printf("could not initialize the conversion context jpg\n");
            exit(1);
        }

        if (w != encoder->ow || h != encoder->oh) {
            // free jpeg picture frame
            av_frame_unref(&encoder->picture);

            // alloc picture
            alloc_picture(&encoder->picture, AV_PIX_FMT_YUVJ420P, w, h);
        }

        encoder->ow   = w;
        encoder->oh   = h;
        encoder->ifmt = (AVPixelFormat)frame->format;
        encoder->iw   = frame->width;
        encoder->ih   = frame->height;
    }

    // scale picture
    sws_scale(
        encoder->sws_ctx,
        frame->data,
        frame->linesize,
        0,
        encoder->ih,
        encoder->picture.data,
        encoder->picture.linesize);

    // create jpeg encoding thread
    pthread_create(&encoder->thread_id, NULL, ffjpeg_encode_thread_proc, encoder);

    return 0;
}

void ffjpeg_encoder_free(void *ctxt)
{
    JPEGENC *encoder = (JPEGENC*)ctxt;
    if (!ctxt) return;

    // wait jpeg encode thread exit
    pthread_join(encoder->thread_id, NULL);

    // free jpeg picture frame
    av_frame_unref(&encoder->picture);

    // free sws context
    sws_freeContext(encoder->sws_ctx);

#ifdef ENABLE_MEDIARECORDER_JNI
    get_jni_env()->DeleteGlobalRef(encoder->jobj_mr);
#endif

    free(ctxt);
}

#ifdef ENABLE_MEDIARECORDER_JNI
void ffjpeg_encoder_init_jni_callback(void *ctxt, JNIEnv *env, jobject obj)
{
    JPEGENC *encoder = (JPEGENC*)ctxt;
    if (!encoder) return;
    encoder->jcls_mr = env->GetObjectClass(obj);
    encoder->jobj_mr = env->NewGlobalRef(obj);
    encoder->jmid_cb = env->GetMethodID(encoder->jcls_mr, "internalTakePhotoCallback", "(Ljava/lang/String;II)V");
}
#endif



// 内部类型定义
typedef struct {
    AVCodec        *codec;
    AVCodecContext *ctxt;
    AVFrame         frame;
} JPEGDEC;

// 函数实现
void* ffjpeg_decoder_init(void)
{
    JPEGDEC *dec    = NULL;
    int      failed = 0;

    /* initialize libavcodec, and register all codecs and formats. */
    av_register_all();
    
    dec = (JPEGDEC*)calloc(1, sizeof(JPEGDEC));
    if (!dec) return NULL;
        
    dec->codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!dec->codec) {
        fprintf(stderr, "codec not found\n");
        failed = 1;
        goto done;
    }

    dec->ctxt = avcodec_alloc_context3(dec->codec);
    if (!dec->ctxt) {
        fprintf(stderr, "could not allocate video codec context\n");
        failed = 1;
        goto done;
    }

    if (dec->codec->capabilities & AV_CODEC_CAP_TRUNCATED) {
        dec->ctxt->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames
    }

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(dec->ctxt, dec->codec, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        failed = 1;
        goto done;
    }

done:
    if (failed) {
        ffjpeg_decoder_free(dec);
        dec = NULL;
    }
    return dec;
}

void ffjpeg_decoder_free(void *ctxt)
{
    JPEGDEC *dec = (JPEGDEC*)ctxt;
    if (!dec) return;
    avcodec_close(dec->ctxt);
    av_free(dec->ctxt);
    av_frame_unref(&dec->frame);
}

AVFrame *ffjpeg_decoder_decode(void *ctxt, void *buf, int len)
{
    JPEGDEC *dec = (JPEGDEC*)ctxt;
    if (!dec) return NULL;

    AVPacket packet;
    int      got = 0;

    av_init_packet(&packet);
    packet.data = (uint8_t*)buf;
    packet.size = len;
    avcodec_decode_video2(dec->ctxt, &dec->frame, &got, &packet);
    return &dec->frame;
}


