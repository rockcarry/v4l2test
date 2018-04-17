#define LOG_TAG "micdev"

// 包含头文件
#include <stdlib.h>
#include <pthread.h>
#include <tinyalsa/asoundlib.h>
#include <utils/Log.h>
#include "micdev.h"

// 内部类型定义
// micdev context
typedef struct {
    MICDEV_COMMON
    struct pcm_config config;
    struct pcm       *pcm;
} MICDEV;

// 内部常量定义
#define DEF_PCM_CARD      0
#define DEF_PCM_DEVICE    0
#define DEF_PCM_FORMAT    PCM_FORMAT_S16_LE
#define DEF_PCM_BUF_SIZE  2048
#define DEF_PCM_BUF_COUNT 6

// 内部函数实现
static void* micdev_capture_thread_proc(void *param)
{
    MICDEV *mic = (MICDEV*)param;
    int     ret =  0;

    while (!(mic->thread_state & MICDEV_TS_EXIT)) {
        // read data from pcm
#if 0
        ret = pcm_read(mic->pcm, mic->buffer, mic->buflen);
#else
        ret = pcm_read_ex(mic->pcm, mic->buffer, mic->buflen);
#endif

        if (ret != 0 || (mic->thread_state & MICDEV_TS_PAUSE)) {
            usleep(10*1000);
            continue;
        }

        if (mic->mute) {
            memset(mic->buffer, 0, mic->buflen);
        }

        if (mic->callback) {
            void *data[AV_NUM_DATA_POINTERS] = { mic->buffer };
            int   sampnum = mic->buflen / (2 * mic->config.channels);
            mic->callback(mic->recorder, data, sampnum);
        }
    }

    return NULL;
}

// 函数实现
void* micdev_tinyalsa_init(int samprate, int channels, void *extra)
{
    MICDEV *mic = (MICDEV*)calloc(1, sizeof(MICDEV));
    if (!mic) {
        ALOGE("failed to allocate micdev context !\n");
        return NULL;
    }

    mic->thread_state = MICDEV_TS_PAUSE;
    mic->samprate     = samprate;
    mic->channels     = channels;
    mic->extra        = extra;
    mic->config.channels          = channels;
    mic->config.rate              = samprate;
    mic->config.period_size       = DEF_PCM_BUF_SIZE;
    mic->config.period_count      = DEF_PCM_BUF_COUNT;
    mic->config.format            = DEF_PCM_FORMAT;
    mic->config.start_threshold   = 0;
    mic->config.stop_threshold    = 0;
    mic->config.silence_threshold = 0;
    mic->pcm = pcm_open(DEF_PCM_CARD, DEF_PCM_DEVICE, PCM_IN, &mic->config);
    if (!mic->pcm) {
        ALOGE("pcm_open failed !\n");
        goto failed;
    }

    mic->buflen = pcm_frames_to_bytes(mic->pcm, pcm_get_buffer_size(mic->pcm));
    mic->buffer = (uint8_t*)calloc(mic->buflen, sizeof(uint8_t));
    if (!mic->buffer) {
        ALOGE("unable to allocate %d bytes buffer !\n", mic->buflen);
        goto failed;
    }

    pthread_create(&mic->thread_id, NULL, micdev_capture_thread_proc, mic);

    return mic;

failed:
    if (mic) {
        if (mic->buffer) free(mic->buffer);
        if (mic->pcm   ) pcm_close(mic->pcm);
        free(mic);
    }
    return NULL;
}

void micdev_tinyalsa_close(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // wait thread safely exited
    mic->thread_state |= MICDEV_TS_EXIT;
    pthread_join(mic->thread_id, NULL);

    if (mic->buffer) free(mic->buffer);
    if (mic->pcm   ) pcm_close(mic->pcm);
    free(mic);
}

void micdev_tinyalsa_start_capture(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // start capture
    mic->thread_state &= ~MICDEV_TS_PAUSE;
}

void micdev_tinyalsa_stop_capture(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;

    // stop capture
    mic->thread_state |= MICDEV_TS_PAUSE;
}

int micdev_tinyalsa_get_mute(void *ctxt)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return 1;
    return mic->mute;
}

void micdev_tinyalsa_set_mute(void *ctxt, int mute)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;
    mic->mute = mute;
}

void micdev_tinyalsa_set_callback(void *ctxt, void *callback, void *recorder)
{
    MICDEV *mic = (MICDEV*)ctxt;
    if (!mic) return;
    mic->callback = (MICDEV_CAPTURE_CALLBACK)callback;
    mic->recorder = recorder;
}


