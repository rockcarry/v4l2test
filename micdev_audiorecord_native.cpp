#define LOG_TAG "micdev"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <utils/Log.h>
#include "micdev.h"

// 内部类型定义
// micdev context
typedef struct {
    MICDEV_COMMON
    android::sp<android::AudioRecord> record;
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

        nread = mic->record->read(mic->buffer, mic->buflen);
//      ALOGD("buflen = %d, nread = %d\n", mic->buflen, nread);

        if (mic->mute) {
            memset(mic->buffer, 0, mic->buflen);
        }

        if (mic->callback) {
            void *data[AV_NUM_DATA_POINTERS] = { mic->buffer };
            int   sampnum = nread / (2 * mic->channels);
            mic->callback(mic->recorder, data, sampnum);
        }

        // sleep to release cpu
//      usleep(10*1000); // no need this
    }

    return NULL;
}

// 函数实现
void* micdev_android_init(int samprate, int channels, void *extra)
{
    int   chcfg = channels == 2 ? AUDIO_CHANNEL_IN_STEREO : AUDIO_CHANNEL_IN_MONO;
    MICDEV *mic = new MICDEV();
    if (!mic) {
        ALOGE("failed to allocate micdev context !\n");
        return NULL;
    }

    mic->thread_state = MICDEV_TS_PAUSE;
    mic->samprate     = samprate;
    mic->channels     = channels;
    mic->extra        = extra;

    size_t minfcount = 0;
    android::AudioRecord::getMinFrameCount(
        &minfcount, samprate, AUDIO_FORMAT_PCM_16_BIT, chcfg); 
    mic->buflen = 2 * minfcount * channels * 2;
    mic->buffer = new uint8_t[mic->buflen];
    mic->record = new android::AudioRecord(AUDIO_SOURCE_MIC, samprate, AUDIO_FORMAT_PCM_16_BIT, chcfg, minfcount);

    if (mic->record->initCheck() != android::NO_ERROR) {
        ALOGE("failed to init audio recorder of android !\n");
    }

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
    mic->record->stop();

    // free
    delete mic->buffer;
    delete mic;
}

void micdev_android_start_capture(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // start recording
    mic->record->start();

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
    mic->record->stop();
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

