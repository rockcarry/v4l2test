#ifndef __MICDEV_H__
#define __MICDEV_H__

// 包含头文件
#include <tinyalsa/asoundlib.h>

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
    void             *encoder;
} MICDEV;

// 函数定义
void* micdev_init (int samprate);
void  micdev_close(MICDEV *dev );
void  micdev_start_capture(MICDEV *dev);
void  micdev_stop_capture (MICDEV *dev);
void  micdev_set_mute     (MICDEV *dev, int mute);
void  micdev_set_encoder  (MICDEV *dev, void *encoder);

#endif









