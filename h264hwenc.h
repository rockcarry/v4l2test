#ifndef _H264ENC_MEDIACODEC_H_
#define _H264ENC_MEDIACODEC_H_

#ifdef __cplusplus
extern "C" {
#endif

// º¯Êý¶¨Òå
void *h264hwenc_init (int w, int h, int frate, int bitrate);
void  h264hwenc_close(void *ctxt);
int   h264hwenc_enqueue(void *ctxt, void *buf, int pts, int timeout);
int   h264hwenc_dequeue(void *ctxt, void *buf, int timeout);

#ifdef __cplusplus
}
#endif

#endif
