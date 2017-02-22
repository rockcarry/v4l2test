#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "ffencoder.h"
#include "h264hwenc.h"

// 内部类型定义
// h264hwenc context
typedef struct {
    int         w;
    int         h;
    jmethodID   init;
    jmethodID   free;
    jmethodID   enqueue;
    jmethodID   dequeue;
    jobject     object;
    void       *ffencoder;
} H264ENC;

extern    JavaVM* g_jvm;
JNIEXPORT JNIEnv* get_jni_env(void);

// 函数实现
void *h264hwenc_mediacodec_init(int w, int h, int frate, int bitrate, void *ffencoder)
{
    JNIEnv  *env = get_jni_env();
    H264ENC *enc = (H264ENC*)calloc(1, sizeof(H264ENC));
    if (!enc) {
        ALOGE("failed to allocate h264hwenc context !\n");
        return NULL;
    }

    jclass    h264enc_class = (jclass)env->FindClass("com/apical/dvr/H264HwEncoder");
    jmethodID constructor   = (jmethodID)env->GetMethodID(h264enc_class, "<init>", "()V");
    enc->init      = (jmethodID)env->GetMethodID(h264enc_class, "init", "(IIII)V");
    enc->free      = (jmethodID)env->GetMethodID(h264enc_class, "free", "()V");
    enc->enqueue   = (jmethodID)env->GetMethodID(h264enc_class, "enqueueInputBuffer", "([BJI)Z");
    enc->dequeue   = (jmethodID)env->GetMethodID(h264enc_class, "dequeueOutputBuffer", "(I)[B");
    enc->w         = w;
    enc->h         = h;
    enc->ffencoder = ffencoder;

    // new H264HwEncoder
    jobject obj = env->NewObject(h264enc_class, constructor);

    // create global ref
    enc->object = env->NewGlobalRef(obj);

    // delete local ref
    env->DeleteLocalRef(h264enc_class);
    env->DeleteLocalRef(obj);

    // init
    env->CallVoidMethod(enc->object, enc->init, w, h, frate, bitrate);

    return enc;
}

void h264hwenc_mediacodec_close(void *ctxt)
{
    JNIEnv  *env = get_jni_env();
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return;

    // release
    env->CallVoidMethod(enc->object, enc->free);

    // delete global ref
    env->DeleteGlobalRef(enc->object);

    // free
    free(enc);
}

int h264hwenc_mediacodec_picture_format(void *ctxt)
{
    return AV_PIX_FMT_NV12;
}

int h264hwenc_mediacodec_picture_alloc(void *ctxt, AVFrame *frame)
{
    JNIEnv  *env = get_jni_env();
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    jbyteArray array   = env->NewByteArray(enc->w * enc->h * 12 / 8);
    frame->width       = enc->w;
    frame->height      = enc->h;
    frame->format      = h264hwenc_picture_format(enc);
    frame->opaque      = (jbyteArray)env->NewGlobalRef(array);
    frame->data[0]     = (uint8_t*)env->GetByteArrayElements(array, 0);
    frame->data[1]     = frame->data[0] + enc->w * enc->h;
    frame->linesize[0] = enc->w;
    frame->linesize[1] = enc->w;
    env->DeleteLocalRef(array);
    return 0;
}

int h264hwenc_mediacodec_picture_free(void *ctxt, AVFrame *frame)
{
    JNIEnv  *env = get_jni_env();
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    jbyteArray array = (jbyteArray)frame->opaque;
    env->ReleaseByteArrayElements(array, (jbyte*)frame->data[0], 0);
    env->DeleteGlobalRef(array);
    return 0;
}

int h264hwenc_mediacodec_encode(void *ctxt, AVFrame *frame, int timeout)
{
    JNIEnv  *env = get_jni_env();
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    // enqueue picture data
    jboolean ret = env->CallBooleanMethod(enc->object, enc->enqueue,
                    (jbyteArray)frame->opaque, (jlong)frame->pts, timeout);
    if (!ret) return -1;

    // dequeue h264 data
    jbyteArray array = (jbyteArray)env->CallObjectMethod(enc->object, enc->dequeue, timeout);
    if (!array) return -1;

    uint8_t* buffer = (uint8_t*)env->GetByteArrayElements(array, 0);

    AVPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.data   = buffer;
    pkt.size   = env->GetArrayLength(array);
    pkt.pts    = frame->pts;
    pkt.dts    = frame->pts;
    ffencoder_write_video_frame(enc->ffencoder, &pkt);

    env->ReleaseByteArrayElements(array, (jbyte*)buffer, 0);
    env->DeleteLocalRef(array);

    return ret ? 0 : -1;
}

