// 包含头文件
#include "LibveDecoder.h"
#include "../jpegdecoder.h"

// 函数实现
void* jpeg_decoder_init(void)
{
    return libveInit(CEDARV_STREAM_FORMAT_MJPEG);
}

void jpeg_decoder_decode(void *decoder, void *buf, int len, int pts)
{
    libveDecode((DecodeHandle*)decoder, buf, len, pts, NULL);
}

void jpeg_decoder_getframe(void *decoder, void *buf, int w, int h)
{
    libveGetFrame((DecodeHandle*)decoder, buf, w, h, NULL);
}

void jpeg_decoder_free(void *decoder)
{
    libveExit((DecodeHandle*)decoder);
}

