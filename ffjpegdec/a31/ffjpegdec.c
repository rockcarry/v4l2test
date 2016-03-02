// 包含头文件
#include <ffjpegdec.h>
#include <system/graphics.h>
#include <utils/Log.h>
#include "LibveDecoder.h"

// 内部常量定义
#define DO_USE_VAR(v) do { v = v; } while (0)

// 函数实现
void* ffjpegdec_init(void)
{
    return libveInit(CEDARV_STREAM_FORMAT_MJPEG);
}

void ffjpegdec_decode(void *decoder, void *buf, int len, int pts)
{
    libveDecode((DecodeHandle*)decoder, buf, len, pts, NULL);
}

void ffjpegdec_getframe(void *decoder, void *buf, int w, int h, int stride)
{
    DO_USE_VAR(stride);
    libveGetFrame((DecodeHandle*)decoder, buf, w, h, NULL);
}

void ffjpegdec_free(void *decoder)
{
    libveExit((DecodeHandle*)decoder);
}

int ffjpegdec_outtype(void *decoder)
{
    DO_USE_VAR(decoder);
    return HAL_PIXEL_FORMAT_YV12;
}
