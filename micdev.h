#ifndef __MICDEV_H__
#define __MICDEV_H__

// 包含头文件
#include <tinyalsa/asoundlib.h>

typedef struct {
    struct pcm_config config;
    struct pcm       *pcm;
    uint8_t          *buffer;
    int               buflen;
    pthread_t               thread_id;
    #define MICDEV_TS_EXIT      (1 << 0)
    #define MICDEV_TS_PAUSE     (1 << 1)
    #define MICDEV_TS_ENCODE    (1 << 2)
    int                     thread_state;
} MICDEV;

// 函数定义
void* micdev_init (int samprate, int sampsize, int h);
void  micdev_close(MICDEV *dev);
void  micdev_start_capture(MICDEV *dev);
void  micdev_stop_capture (MICDEV *dev);
void  micdev_set_encoder  (MICDEV *dev, void *encoder);

#endif









