#define LOG_TAG "camdev"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "micdev.h"
#include "micdev_android.h"

// 内部常量定义
#define ENCODING_INVALID                0
#define ENCODING_DEFAULT                1
#define ENCODING_PCM_16BIT              2
#define ENCODING_PCM_8BIT               3

#define CHANNEL_CONFIGURATION_INVALID   0
#define CHANNEL_CONFIGURATION_DEFAULT   1
#define CHANNEL_CONFIGURATION_MONO      2
#define CHANNEL_CONFIGURATION_STEREO    3

#define AUDIO_SOURCE_DEFAULT            0
#define AUDIO_SOURCE_MIC                1
#define AUDIO_SOURCE_VOICE_UPLINK       2
#define AUDIO_SOURCE_VOICE_DOWNLINK     3
#define AUDIO_SOURCE_VOICE_CALL         4

// 内部类型定义
// micdev context
typedef struct {
    MICDEV_COMMON
    jmethodID   audio_record_read;
    jmethodID   audio_record_startRecording;
    jmethodID   audio_record_stop;
    jmethodID   audio_record_release;
    jobject     audio_record_obj;
    jbyteArray  audio_buffer;
} MICDEV;

extern "C" JavaVM* g_jvm;
extern "C" JNIEXPORT JNIEnv* get_jni_env(void);

// 内部函数实现
static void* micdev_capture_thread_proc(void *param)
{
    JNIEnv *env = get_jni_env();
    MICDEV *mic = (MICDEV*)param;
    int   nread = 0;

    mic->buffer = (uint8_t*)env->GetByteArrayElements(mic->audio_buffer, 0);

    while (!(mic->thread_state & MICDEV_TS_EXIT)) {
        if (mic->thread_state & MICDEV_TS_PAUSE) {
            usleep(10*1000);
            continue;
        }

        if (mic->mute) {
            memset(mic->buffer, 0, mic->buflen);
        }
        else {
            nread = env->CallIntMethod(mic->audio_record_obj, mic->audio_record_read,
                        mic->audio_buffer, 0, mic->buflen);
//          ALOGD("buflen = %d, nread = %d\n", mic->buflen, nread);
        }

        if (mic->callback) {
            void *data[8] = { mic->buffer };
            int   sampnum = nread / (2 * mic->channels);
            mic->callback(mic->recorder, data, sampnum);
        }

        // sleep to release cpu
        usleep(10*1000);
    }

    mic->buffer = (uint8_t*)env->GetByteArrayElements(mic->audio_buffer, 0);

    // need call DetachCurrentThread
    g_jvm->DetachCurrentThread();
    return NULL;
}

// 函数实现
void* micdev_android_init(int samprate, int channels, void *extra)
{
    JNIEnv *env = get_jni_env();
    int   chcfg = channels == 2 ? CHANNEL_CONFIGURATION_STEREO : CHANNEL_CONFIGURATION_MONO;
    MICDEV *mic = (MICDEV*)calloc(1, sizeof(MICDEV));
    if (!mic) {
        ALOGE("failed to allocate micdev context !\n");
        return NULL;
    }

    mic->thread_state = MICDEV_TS_PAUSE;
    mic->samprate     = samprate;
    mic->channels     = channels;
    mic->extra        = extra;

    jclass    local_audio_record_class      = (jclass)env->FindClass("android/media/AudioRecord");
    jmethodID audio_record_constructor      = (jmethodID)env->GetMethodID(local_audio_record_class, "<init>", "(IIIII)V");
    jmethodID audio_record_getMinBufferSize = env->GetStaticMethodID(local_audio_record_class, "getMinBufferSize", "(III)I");
    mic->audio_record_read                  = env->GetMethodID(local_audio_record_class, "read", "([BII)I");
    mic->audio_record_startRecording        = env->GetMethodID(local_audio_record_class, "startRecording", "()V");
    mic->audio_record_stop                  = env->GetMethodID(local_audio_record_class, "stop", "()V");
    mic->audio_record_release               = env->GetMethodID(local_audio_record_class, "release", "()V");

    // AudioRecord.getMinBufferSize
    mic->buflen = 2 * env->CallStaticIntMethod(local_audio_record_class,
        audio_record_getMinBufferSize, samprate, chcfg, ENCODING_PCM_16BIT);

    // new AudioRecord
    jobject local_audio_record_obj = env->NewObject(local_audio_record_class, audio_record_constructor,
        AUDIO_SOURCE_MIC, samprate, chcfg, ENCODING_PCM_16BIT, mic->buflen);

    // new buffer
    jbyteArray local_audio_buffer = env->NewByteArray(mic->buflen);
//  mic->buffer = (uint8_t*)env->GetByteArrayElements(mic->audio_buffer, 0);

    // create global ref
    mic->audio_record_obj = env->NewGlobalRef(local_audio_record_obj);
    mic->audio_buffer     = (jbyteArray)env->NewGlobalRef(local_audio_buffer);

    // delete local ref
    env->DeleteLocalRef(local_audio_record_class);
    env->DeleteLocalRef(local_audio_record_obj  );
    env->DeleteLocalRef(local_audio_buffer      );

    // create thread for micdev
    pthread_create(&mic->thread_id, NULL, micdev_capture_thread_proc, mic);

    return mic;
}

void micdev_android_close(void *ctxt)
{
    JNIEnv *env = get_jni_env();
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // wait thread safely exited
    mic->thread_state |= MICDEV_TS_EXIT;
    pthread_join(mic->thread_id, NULL);

    // release
    env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_release);

    // delete local reference
//  env->ReleaseByteArrayElements(mic->audio_buffer, (jbyte*)mic->buffer, 0);

    // delete global ref
    env->DeleteGlobalRef(mic->audio_record_obj);
    env->DeleteGlobalRef(mic->audio_buffer    );

    // free
    free(mic);
}

void micdev_android_start_capture(void *ctxt)
{
    JNIEnv *env = get_jni_env();
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // startRecording
    env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_startRecording);

    // start capture
    mic->thread_state &= ~MICDEV_TS_PAUSE;
}

void micdev_android_stop_capture(void *ctxt)
{
    JNIEnv *env = get_jni_env();
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // stop capture
    mic->thread_state |= MICDEV_TS_PAUSE;

    // stop
    env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_stop);
}

int micdev_android_get_mute(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return 1;
    return mic->mute;
}

void micdev_android_set_mute(void *ctxt, int mute)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;
    mic->mute = mute;
}

void micdev_android_set_callback(void *ctxt, void *callback, void *recorder)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;
    mic->callback = (MICDEV_CAPTURE_CALLBACK)callback;
    mic->recorder = recorder;
}

