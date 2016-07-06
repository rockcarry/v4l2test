#ifndef _MICDEV_TINYALSA_H_
#define _MICDEV_TINYALSA_H_

#ifdef __cplusplus
extern "C" {
#endif

// º¯Êý¶¨Òå
void* micdev_tinyalsa_init (int samprate, int channels, void *extra);
void  micdev_tinyalsa_close(void *ctxt);
void  micdev_tinyalsa_start_capture(void *ctxt);
void  micdev_tinyalsa_stop_capture (void *ctxt);
void  micdev_tinyalsa_set_mute     (void *ctxt, int mute);
void  micdev_tinyalsa_set_callback (void *ctxt, void *callback, void *recorder);

#ifdef __cplusplus
}
#endif

#endif








