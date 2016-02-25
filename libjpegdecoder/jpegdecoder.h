#ifndef __JPEGDECODER_H__
#define __JPEGDECODER_H__

#ifdef __cplusplus
extern "C" {
#endif

// º¯ÊýÉùÃ÷
void* jpeg_decoder_init    (void);
void  jpeg_decoder_decode  (void *decoder, void *buf, int len, int pts);
void  jpeg_decoder_getframe(void *decoder, void *buf, int w, int h);
void  jpeg_decoder_free    (void *decoder);

#ifdef __cplusplus
}
#endif

#endif









