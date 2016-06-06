#ifndef __MICDEV_H__
#define __MICDEV_H__

// º¯Êý¶¨Òå
void* micdev_init (int samprate, int sampsize, int h);
void  micdev_close(void *dev);
void  micdev_start_capture(void *dev);
void  micdev_stop_capture (void *dev);
void  micdev_set_encoder  (void *dev, void *encoder);

#endif









