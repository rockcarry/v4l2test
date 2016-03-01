#ifndef __JPEGDECODER_H__
#define __JPEGDECODER_H__

#ifdef __cplusplus
extern "C" {
#endif

// º¯ÊýÉùÃ÷
void* ffjpegdec_init    (void);
void  ffjpegdec_decode  (void *decoder, void *buf, int len, int pts);
void  ffjpegdec_getframe(void *decoder, void *buf, int w, int h, int stride);
void  ffjpegdec_free    (void *decoder);

#ifdef __cplusplus
}
#endif

#endif









