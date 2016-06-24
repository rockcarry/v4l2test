#define LOG_TAG "camdev"

// 包含头文件
#include <stdlib.h>
#include <utils/Log.h>
#include "micdev.h"

// 内部常量定义
#define DEF_PCM_CARD      0
#define DEF_PCM_DEVICE    0
#define DEF_PCM_FORMAT    PCM_FORMAT_S16_LE
#define DEF_PCM_BUF_SIZE  2048
#define DEF_PCM_BUF_COUNT 3

// 内部函数实现
static void* micdev_capture_thread_proc(void *param)
{
    MICDEV *dev = (MICDEV*)param;

    while (1) {
        if (dev->thread_state & MICDEV_TS_EXIT) {
            break;
        }

        if (dev->thread_state & MICDEV_TS_PAUSE) {
            usleep(33*1000);
            continue;
        }

        if (dev->mute) {
            memset(dev->pcm, 0, dev->buflen);
        }
        else {
            // read data from pcm
#if 0
            pcm_read(dev->pcm, dev->buffer, dev->buflen);
#else
            pcm_read_ex(dev->pcm, dev->buffer, dev->buflen);
#endif
        }

        if (dev->callback) {
            void *data[8] = { dev->buffer };
            int   sampnum = dev->buflen / (2 * dev->config.channels);
            dev->callback(dev->recorder, data, sampnum);
        }
    }

    return NULL;
}

// 函数实现
void* micdev_init(int samprate, int channals)
{
    MICDEV *dev = (MICDEV*)malloc(sizeof(MICDEV));
    if (!dev) {
        ALOGE("failed to allocate micdev context !\n");
        goto failed;
    }
    else memset(dev, 0, sizeof(MICDEV));

    dev->config.channels          = channals;
    dev->config.rate              = samprate;
    dev->config.period_size       = DEF_PCM_BUF_SIZE;
    dev->config.period_count      = DEF_PCM_BUF_COUNT;
    dev->config.format            = DEF_PCM_FORMAT;
    dev->config.start_threshold   = 0;
    dev->config.stop_threshold    = 0;
    dev->config.silence_threshold = 0;
    dev->pcm = pcm_open(DEF_PCM_CARD, DEF_PCM_DEVICE, PCM_IN, &dev->config);
    if (!dev->pcm) {
        ALOGE("pcm_open failed !\n");
        goto failed;
    }

    dev->buflen = pcm_frames_to_bytes(dev->pcm, pcm_get_buffer_size(dev->pcm));
    dev->buffer = (uint8_t*)malloc(dev->buflen);
    if (!dev->buffer) {
        ALOGE("unable to allocate %d bytes buffer !\n", dev->buflen);
        goto failed;
    }

    pthread_create(&dev->thread_id, NULL, micdev_capture_thread_proc, dev);

    return dev;

failed:
    if (dev) {
        if (dev->buffer) free(dev->buffer);
        if (dev->pcm   ) pcm_close(dev->pcm);
        free(dev);
    }
    return NULL;
}

void micdev_close(MICDEV *dev)
{
    if (!dev) return;

    // wait thread safely exited
    dev->thread_state |= MICDEV_TS_EXIT;
    pthread_join(dev->thread_id, NULL);

    if (dev->buffer) free(dev->buffer);
    if (dev->pcm   ) pcm_close(dev->pcm);
    free(dev);
}

void micdev_start_capture(MICDEV *dev)
{
    // start capture
    dev->thread_state &= ~MICDEV_TS_PAUSE;
}

void micdev_stop_capture(MICDEV *dev)
{
    // stop capture
    dev->thread_state |= MICDEV_TS_PAUSE;
}

void micdev_set_mute(MICDEV *dev, int mute)
{
    dev->mute = mute;
}

void micdev_set_callback(MICDEV *dev, MICDEV_CAPTURE_CALLBACK callback, void *recorder)
{
    dev->callback = callback;
    dev->recorder = recorder;
}

