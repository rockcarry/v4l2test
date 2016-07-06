#ifndef __MICDEV_ANDROID_H__
#define __MICDEV_ANDROID_H__

#ifdef __cplusplus
extern "C" {
#endif

// º¯Êý¶¨Òå
void* micdev_android_init (void *env, int samprate, int channels);
void  micdev_android_close(void *ctxt);
void  micdev_android_start_capture(void *ctxt);
void  micdev_android_stop_capture (void *ctxt);
void  micdev_android_set_mute     (void *ctxt, int mute);
void  micdev_android_set_callback (void *ctxt, void *callback, void *recorder);

#ifdef __cplusplus
}
#endif

#endif








