#ifndef __MICDEV_H__
#define __MICDEV_H__

// 包含头文件
#include <pthread.h>
#include <tinyalsa/asoundlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// micdev capture callback
typedef int (*MICDEV_CAPTURE_CALLBACK)(void *recorder, void *data[8], int nbsample);

// micdev context
typedef struct {
    struct pcm_config config;
    struct pcm       *pcm;
    uint8_t          *buffer;
    int               buflen;
    int               mute;
    #define MICDEV_TS_EXIT  (1 << 0)
    #define MICDEV_TS_PAUSE (1 << 1)
    pthread_t         thread_id;
    int               thread_state;
    MICDEV_CAPTURE_CALLBACK callback;
    void                   *recorder;
} MICDEV;

// 函数定义
void* micdev_init (int samprate);
void  micdev_close(MICDEV *dev );
void  micdev_start_capture(MICDEV *dev);
void  micdev_stop_capture (MICDEV *dev);
void  micdev_set_mute     (MICDEV *dev, int mute);
void  micdev_set_callback (MICDEV *dev, MICDEV_CAPTURE_CALLBACK callback, void *recorder);

#ifdef __cplusplus
}
#endif

#endif








