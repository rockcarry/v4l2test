#ifndef __FFJPEG_H_
#define __FFJPEG_H_

// 包含头文件
extern "C" {
#include <libavutil/frame.h>
}

#ifdef ENABLE_MEDIARECORDER_JNI
#include <jni.h>
extern    JavaVM* g_jvm;
JNIEXPORT JNIEnv* get_jni_env(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* ffjpeg_encoder_init  (void);
void  ffjpeg_encoder_free  (void *ctxt);
int   ffjpeg_encoder_encode(void *ctxt, const char *file, int w, int h, AVFrame *frame);

#ifdef ENABLE_MEDIARECORDER_JNI
void  ffjpeg_encoder_init_jni_callback(void *ctxt, JNIEnv *env, jobject obj);
#endif

void*    ffjpeg_decoder_init  (void);
void     ffjpeg_decoder_free  (void *ctxt);
AVFrame *ffjpeg_decoder_decode(void *ctxt, void *buf, int len);

#ifdef __cplusplus
}
#endif

#endif

