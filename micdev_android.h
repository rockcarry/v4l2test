#ifndef _MICDEV_ANDROID_H_
#define _MICDEV_ANDROID_H_

#ifdef __cplusplus
extern "C" {
#endif

// º¯Êý¶¨Òå
void* micdev_android_init (int samprate, int channels, void *extra);
void  micdev_android_close(void *ctxt);
void  micdev_android_start_capture(void *ctxt);
void  micdev_android_stop_capture (void *ctxt);
int   micdev_android_get_mute     (void *ctxt);
void  micdev_android_set_mute     (void *ctxt, int mute);
void  micdev_android_set_callback (void *ctxt, void *callback, void *recorder);

#ifdef __cplusplus
}
#endif

#endif








