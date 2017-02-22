#ifndef _H264ENC_MEDIACODEC_H_
#define _H264ENC_MEDIACODEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>

// º¯Êý¶¨Òå
//++ android media codec hardware h264 encoder ++//
void *h264hwenc_mediacodec_init  (int iw, int ih, int ow, int oh, int frate, int bitrate, void *ffencoder);
void  h264hwenc_mediacodec_close (void *ctxt);
int   h264hwenc_mediacodec_encode(void *ctxt, AVFrame *frame, int timeout);
int   h264hwenc_mediacodec_picture_format(void *ctxt);
int   h264hwenc_mediacodec_picture_alloc (void *ctxt, AVFrame *frame);
int   h264hwenc_mediacodec_picture_free  (void *ctxt, AVFrame *frame);
//-- android media codec hardware h264 encoder --//

//++ allwinner cedarx hardware h264 encoder ++//
void *h264hwenc_cedarx_init  (int iw, int ih, int ow, int oh, int frate, int bitrate, void *ffencoder);
void  h264hwenc_cedarx_close (void *ctxt);
int   h264hwenc_cedarx_encode(void *ctxt, AVFrame *frame, int timeout);
int   h264hwenc_cedarx_picture_format(void *ctxt);
int   h264hwenc_cedarx_picture_alloc (void *ctxt, AVFrame *frame);
int   h264hwenc_cedarx_picture_free  (void *ctxt, AVFrame *frame);
//-- allwinner cedarx hardware h264 encoder --//

#if 1
#define h264hwenc_init              h264hwenc_mediacodec_init
#define h264hwenc_close             h264hwenc_mediacodec_close
#define h264hwenc_encode            h264hwenc_mediacodec_encode
#define h264hwenc_picture_format    h264hwenc_mediacodec_picture_format
#define h264hwenc_picture_alloc     h264hwenc_mediacodec_picture_alloc
#define h264hwenc_picture_free      h264hwenc_mediacodec_picture_free
#else
#define h264hwenc_init              h264hwenc_cedarx_init
#define h264hwenc_close             h264hwenc_cedarx_close
#define h264hwenc_encode            h264hwenc_cedarx_encode
#define h264hwenc_picture_format    h264hwenc_cedarx_picture_format
#define h264hwenc_picture_alloc     h264hwenc_cedarx_picture_alloc
#define h264hwenc_picture_free      h264hwenc_cedarx_picture_free
#endif

#ifdef __cplusplus
}
#endif

#endif
