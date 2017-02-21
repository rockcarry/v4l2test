#ifndef _H264ENC_MEDIACODEC_H_
#define _H264ENC_MEDIACODEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>

// º¯Êý¶¨Òå
void *h264hwenc_init  (int w, int h, int frate, int bitrate, void *ffencoder);
void  h264hwenc_close (void *ctxt);
int   h264hwenc_encode(void *ctxt, AVFrame *frame, int timeout);
int   h264hwenc_picture_format(void *ctxt);
int   h264hwenc_picture_alloc (void *ctxt, AVFrame *frame);
int   h264hwenc_picture_free  (void *ctxt, AVFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
