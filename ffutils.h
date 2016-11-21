#ifndef __FFUTILS_H__
#define __FFUTILS_H__

// 包含头文件
#include <linux/videodev2.h>
#include <libavutil/pixfmt.h>
#include <gui/Surface.h>

#ifdef __cplusplus
extern "C" {
#endif

// 函数定义
inline int v4l2dev_pixfmt_to_ffmpeg_pixfmt(int srcfmt)
{
    // src fmt
    int dst_fmt = 0;
    switch (srcfmt) {
    case V4L2_PIX_FMT_YUYV: dst_fmt = AV_PIX_FMT_YUYV422; break;
    case V4L2_PIX_FMT_NV12: dst_fmt = AV_PIX_FMT_NV12;    break;
    case V4L2_PIX_FMT_NV21: dst_fmt = AV_PIX_FMT_NV21;    break;
    }
    return dst_fmt;
}

inline int android_pixfmt_to_ffmpeg_pixfmt(int srcfmt)
{
    // dst fmt
    int dst_fmt = 0;
    switch (srcfmt) {
    case HAL_PIXEL_FORMAT_RGB_565:      dst_fmt = AV_PIX_FMT_RGB565;  break;
    case HAL_PIXEL_FORMAT_RGBX_8888:    dst_fmt = AV_PIX_FMT_BGR32;   break;
    case HAL_PIXEL_FORMAT_YV12:         dst_fmt = AV_PIX_FMT_YUV420P; break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP: dst_fmt = AV_PIX_FMT_NV21;    break;
    }
    return dst_fmt;
}

inline int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1);
}

inline uint64_t get_tick_count(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#ifdef __cplusplus
}
#endif

#endif








