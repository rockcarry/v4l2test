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
    JNIEnv     *jni_env;
    jclass      audio_record_class;
    jmethodID   audio_record_constructor;
    jmethodID   audio_record_getMinBufferSize;
    jmethodID   audio_record_read;
    jmethodID   audio_record_startRecording;
    jmethodID   audio_record_stop;
    jmethodID   audio_record_release;
    jobject     audio_record_obj;
    jbyteArray  read_buf;
} MICDEV;

// 内部函数实现
static void* micdev_capture_thread_proc(void *param)
{
    MICDEV *mic = (MICDEV*)param;
    int   nread = 0;

    while (!(mic->thread_state & MICDEV_TS_EXIT)) {
        if (mic->thread_state & MICDEV_TS_PAUSE) {
            usleep(10*1000);
            continue;
        }

        if (mic->mute) {
            memset(mic->buffer, 0, mic->buflen);
        }
        else {
            nread = mic->jni_env->CallIntMethod(mic->audio_record_obj, mic->audio_record_read,
                        mic->read_buf, 0, mic->buflen / (2 * mic->channels));
            mic->jni_env->GetByteArrayRegion(mic->read_buf, 0, nread, (jbyte*)mic->buffer);
        }

        if (mic->callback) {
            void *data[8] = { mic->buffer };
            int   sampnum = nread / (2 * mic->channels);
            mic->callback(mic->recorder, data, sampnum);
        }

        // sleep to release cpu
        usleep(10*1000);
    }

    return NULL;
}

// 函数实现
void* micdev_android_init(int samprate, int channels, void *extra)
{
    int   chcfg = channels == 2 ? CHANNEL_CONFIGURATION_STEREO : CHANNEL_CONFIGURATION_MONO;
    MICDEV *mic = (MICDEV*)malloc(sizeof(MICDEV));
    if (!mic) {
        ALOGE("failed to allocate micdev context !\n");
        return NULL;
    }
    else memset(mic, 0, sizeof(MICDEV));

    mic->thread_state = MICDEV_TS_PAUSE;
    mic->samprate     = samprate;
    mic->channels     = channels;
    mic->extra        = extra;
    mic->jni_env      = (JNIEnv*)extra;
    mic->audio_record_class            = mic->jni_env->FindClass("android/media/AudioRecord");
    mic->audio_record_constructor      = mic->jni_env->GetMethodID(mic->audio_record_class, "<init>", "(IIIII)V");
    mic->audio_record_getMinBufferSize = mic->jni_env->GetStaticMethodID(mic->audio_record_class, "getMinBufferSize", "(III)I");
    mic->audio_record_read             = mic->jni_env->GetMethodID(mic->audio_record_class, "read", "([BII)I");
    mic->audio_record_startRecording   = mic->jni_env->GetMethodID(mic->audio_record_class, "startRecording", "()V");
    mic->audio_record_stop             = mic->jni_env->GetMethodID(mic->audio_record_class, "stop", "()V");
    mic->audio_record_release          = mic->jni_env->GetMethodID(mic->audio_record_class, "release", "()V");

    // AudioRecord.getMinBufferSize
    mic->buflen = mic->jni_env->CallStaticIntMethod(mic->audio_record_class,
        mic->audio_record_getMinBufferSize, samprate, chcfg, ENCODING_PCM_16BIT);

    // allocate buffer
    mic->buffer = (uint8_t*)malloc(mic->buflen);
    if (!mic->buffer) {
        ALOGE("unable to allocate %d bytes buffer !\n", mic->buflen);
    }

    // new AudioRecord
    mic->audio_record_obj = mic->jni_env->NewObject(mic->audio_record_class, mic->audio_record_constructor,
        AUDIO_SOURCE_MIC, samprate, chcfg, ENCODING_PCM_16BIT, mic->buflen * 2);

    // new buffer
    mic->read_buf = mic->jni_env->NewByteArray(mic->buflen);

    // create thread for micdev
    pthread_create(&mic->thread_id, NULL, micdev_capture_thread_proc, mic);

    return mic;
}

void micdev_android_close(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // wait thread safely exited
    mic->thread_state |= MICDEV_TS_EXIT;
    pthread_join(mic->thread_id, NULL);

    // release
    mic->jni_env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_release);

    // delete local reference
    mic->jni_env->DeleteLocalRef(mic->read_buf);
    mic->jni_env->DeleteLocalRef(mic->audio_record_obj);
    mic->jni_env->DeleteLocalRef(mic->audio_record_class);

    // free
    free(mic->buffer);
    free(mic);
}

void micdev_android_start_capture(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // startRecording
    mic->jni_env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_startRecording);

    // start capture
    mic->thread_state &= ~MICDEV_TS_PAUSE;
}

void micdev_android_stop_capture(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // stop capture
    mic->thread_state |= MICDEV_TS_PAUSE;

    // stop
    mic->jni_env->CallVoidMethod(mic->audio_record_obj, mic->audio_record_stop);
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

